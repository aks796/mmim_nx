/* vfs.c -- Nabe's files served straight from the APK (see vfs.h)
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <switch.h>

#include "apkfile.h"
#include "config.h"
#include "log.h"
#include "nabe.h"
#include "paths.h"
#include "vfs.h"

// --- string sets (overlay and whiteouts) ------------------------------------

typedef struct SetNode {
  struct SetNode *next;
  char key[];
} SetNode;

typedef struct {
  SetNode **b;
  unsigned cap, n;
} StrSet;

static uint32_t h32(const char *s) {
  uint32_t h = 2166136261u;
  for (; *s; s++)
    h = (h ^ (uint8_t)*s) * 16777619u;
  return h;
}

static void set_init(StrSet *s) {
  s->cap = 1024;
  s->n = 0;
  s->b = calloc(s->cap, sizeof(SetNode *));
}

static int set_has(const StrSet *s, const char *k) {
  if (!s->n)
    return 0;
  for (SetNode *e = s->b[h32(k) & (s->cap - 1)]; e; e = e->next)
    if (!strcmp(e->key, k))
      return 1;
  return 0;
}

static int set_add(StrSet *s, const char *k) {
  if (set_has(s, k))
    return 0;
  const size_t l = strlen(k);
  SetNode *e = malloc(sizeof(*e) + l + 1);
  memcpy(e->key, k, l + 1);
  const unsigned i = h32(k) & (s->cap - 1);
  e->next = s->b[i];
  s->b[i] = e;
  s->n++;
  return 1;
}

static int set_del(StrSet *s, const char *k) {
  SetNode **pp = &s->b[h32(k) & (s->cap - 1)];
  for (; *pp; pp = &(*pp)->next) {
    if (!strcmp((*pp)->key, k)) {
      SetNode *e = *pp;
      *pp = e->next;
      free(e);
      s->n--;
      return 1;
    }
  }
  return 0;
}

static StrSet s_overlay;   // rel paths of tar files that also exist on the SD
static StrSet s_whiteout;  // rel paths of tar files the game deleted
static RMutex s_lock;
static int s_ready;
static size_t s_files_len;
static unsigned s_opens;
static uint64_t s_read_bytes;

int vfs_ready(void) { return s_ready; }

void vfs_stats(unsigned *opens, unsigned *reads_mb) {
  if (opens) *opens = s_opens;
  if (reads_mb) *reads_mb = (unsigned)(s_read_bytes >> 20);
}

// the part of path inside the files dir, or NULL
static const char *rel_of(const char *path) {
  if (!path || strncmp(path, path_files(), s_files_len) != 0)
    return NULL;
  if (path[s_files_len] == '\0')
    return "";
  if (path[s_files_len] != '/')
    return NULL;
  const char *r = path + s_files_len;
  while (*r == '/')
    r++;
  return r;
}

// a normalised rel (no "//", no trailing '/', no "./")
static const char *norm_rel(const char *rel, char *buf, size_t n) {
  size_t o = 0;
  for (const char *p = rel; *p && o + 1 < n; p++) {
    if (*p == '/' && (o == 0 || buf[o - 1] == '/'))
      continue;
    if (*p == '.' && (p[1] == '/' || !p[1]) && (o == 0 || buf[o - 1] == '/')) {
      if (p[1]) p++;
      continue;
    }
    buf[o++] = *p;
  }
  while (o && buf[o - 1] == '/')
    o--;
  buf[o] = '\0';
  return buf;
}

static void whiteout_path(char *out, size_t n) {
  snprintf(out, n, "%s/nabe_whiteout.txt", path_internal());
}

static void whiteout_save(void) {
  char p[512];
  whiteout_path(p, sizeof p);
  FILE *f = fopen(p, "w");
  if (!f)
    return;
  for (unsigned i = 0; i < s_whiteout.cap; i++)
    for (SetNode *e = s_whiteout.b[i]; e; e = e->next)
      fprintf(f, "%s\n", e->key);
  fclose(f);
}

static void whiteout_load(void) {
  char p[512];
  whiteout_path(p, sizeof p);
  FILE *f = fopen(p, "r");
  if (!f)
    return;
  char line[1024];
  while (fgets(line, sizeof line, f)) {
    size_t l = strlen(line);
    while (l && (line[l - 1] == '\n' || line[l - 1] == '\r'))
      line[--l] = '\0';
    if (l)
      set_add(&s_whiteout, line);
  }
  fclose(f);
}

// every real file under the tar's directories that shadows a tar file
static void scan_overlay(const char *rel) {
  char dir[768];
  snprintf(dir, sizeof dir, "%s/%s", path_files(), rel);
  DIR *d = opendir(dir);
  if (!d)
    return;
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
      continue;
    char child[768];
    snprintf(child, sizeof child, "%s%s%s", rel, rel[0] ? "/" : "", e->d_name);
    if (nabe_is_dir(child))
      scan_overlay(child);
    else if (nabe_lookup(child))
      set_add(&s_overlay, child);
  }
  closedir(d);
}

void vfs_init(void) {
  rmutexInit(&s_lock);
  set_init(&s_overlay);
  set_init(&s_whiteout);
  s_files_len = strlen(path_files());
  whiteout_load();
  // only the tar's top-level directories can hold shadowing files
  const u64 t0 = armGetSystemTick();
  scan_overlay("");
  s_ready = 1;
  log_printf("[vfs] %d tar files; %u shadowed by SD files, %u deleted (%llu ms)", nabe_count(),
             s_overlay.n, s_whiteout.n,
             (unsigned long long)armTicksToNs(armGetSystemTick() - t0) / 1000000ull);
}

VfsKind vfs_classify(const char *path, const NabeFile **f) {
  if (f)
    *f = NULL;
  if (!s_ready)
    return VFS_REAL;
  const char *rel = rel_of(path);
  if (!rel)
    return VFS_REAL;
  char buf[768];
  rel = norm_rel(rel, buf, sizeof buf);
  rmutexLock(&s_lock);
  VfsKind k = VFS_REAL;
  const NabeFile *nf = nabe_lookup(rel);
  if (nf) {
    if (set_has(&s_overlay, rel))
      k = VFS_REAL;
    else if (set_has(&s_whiteout, rel))
      k = VFS_GONE;
    else {
      k = VFS_VFILE;
      if (f)
        *f = nf;
    }
  } else if (nabe_is_dir(rel)) {
    k = VFS_VDIR;
  }
  rmutexUnlock(&s_lock);
  return k;
}

// --- virtual fds -------------------------------------------------------------

typedef struct {
  int used;
  int refs;          // dup'd fds share the position (like POSIX)
  uint64_t base, size, pos;
  const char *mem; // a file served from memory (vfd_open_mem), not the APK
} Vfd;

static Vfd s_vfd[VFD_MAX];
static Mutex s_vfd_lock;

static Vfd *vfd_get(int fd) {
  if (fd < VFD_BASE || fd >= VFD_BASE + VFD_MAX)
    return NULL;
  Vfd *v = &s_vfd[fd - VFD_BASE];
  return v->used ? v : NULL;
}

int vfd_is(int fd) {
  return vfd_get(fd) != NULL;
}

int vfd_open(const NabeFile *f) {
  mutexLock(&s_vfd_lock);
  for (int i = 0; i < VFD_MAX; i++) {
    if (!s_vfd[i].used) {
      s_vfd[i].used = 1;
      s_vfd[i].refs = 1;
      s_vfd[i].base = f->off;
      s_vfd[i].size = f->size;
      s_vfd[i].pos = 0;
      s_vfd[i].mem = NULL;
      mutexUnlock(&s_vfd_lock);
      s_opens++;
      return VFD_BASE + i;
    }
  }
  mutexUnlock(&s_vfd_lock);
  log_printf("[vfs] out of virtual fds");
  errno = EMFILE;
  return -1;
}

int vfd_open_mem(const char *data, size_t size) {
  const NabeFile f = { "", 0, size };
  const int fd = vfd_open(&f);
  if (fd >= 0)
    s_vfd[fd - VFD_BASE].mem = data;
  return fd;
}

long vfd_pread(int fd, void *buf, size_t n, int64_t off) {
  Vfd *v = vfd_get(fd);
  if (!v) {
    errno = EBADF;
    return -1;
  }
  if (off < 0) {
    errno = EINVAL;
    return -1;
  }
  if ((uint64_t)off >= v->size || !n)
    return 0;
  if (n > v->size - (uint64_t)off)
    n = (size_t)(v->size - (uint64_t)off);
  if (v->mem) {
    memcpy(buf, v->mem + off, n);
    return (long)n;
  }
  const long got = apkfile_read(v->base + (uint64_t)off, buf, n);
  if (got < 0) {
    errno = EIO;
    return -1;
  }
  s_read_bytes += (uint64_t)got;
  return got;
}

long vfd_read(int fd, void *buf, size_t n) {
  Vfd *v = vfd_get(fd);
  if (!v) {
    errno = EBADF;
    return -1;
  }
  const long got = vfd_pread(fd, buf, n, (int64_t)v->pos);
  if (got > 0)
    v->pos += (uint64_t)got;
  return got;
}

int64_t vfd_lseek(int fd, int64_t off, int whence) {
  Vfd *v = vfd_get(fd);
  if (!v) {
    errno = EBADF;
    return -1;
  }
  int64_t np;
  switch (whence) {
    case SEEK_SET: np = off; break;
    case SEEK_CUR: np = (int64_t)v->pos + off; break;
    case SEEK_END: np = (int64_t)v->size + off; break;
    default: errno = EINVAL; return -1;
  }
  if (np < 0) {
    errno = EINVAL;
    return -1;
  }
  v->pos = (uint64_t)np;
  return np;
}

uint64_t vfd_size(int fd) {
  Vfd *v = vfd_get(fd);
  return v ? v->size : 0;
}

int vfd_close(int fd) {
  mutexLock(&s_vfd_lock);
  Vfd *v = vfd_get(fd);
  if (!v) {
    mutexUnlock(&s_vfd_lock);
    errno = EBADF;
    return -1;
  }
  v->used = 0;
  mutexUnlock(&s_vfd_lock);
  return 0;
}

int vfd_dup(int fd) {
  Vfd *v = vfd_get(fd);
  if (!v) {
    errno = EBADF;
    return -1;
  }
  NabeFile f = { NULL, v->base, v->size };
  const int nfd = vfd_open(&f);
  if (nfd >= 0)
    s_vfd[nfd - VFD_BASE].pos = v->pos;
  return nfd;
}

// --- FILE* over a virtual fd --------------------------------------------------

#define MAX_VFILES 256
static struct { FILE *fp; int fd; } s_vfiles[MAX_VFILES];

static ssize_t ck_read(void *c, char *buf, size_t n) {
  return vfd_read((int)(intptr_t)c, buf, n);
}

static int ck_seek(void *c, off_t *off, int whence) {
  const int64_t r = vfd_lseek((int)(intptr_t)c, *off, whence);
  if (r < 0)
    return -1;
  *off = r;
  return 0;
}

static int ck_close(void *c) {
  return vfd_close((int)(intptr_t)c);
}

FILE *vfs_fopen(const NabeFile *f) {
  const int fd = vfd_open(f);
  if (fd < 0)
    return NULL;
  cookie_io_functions_t io = { ck_read, NULL, ck_seek, ck_close };
  FILE *fp = fopencookie((void *)(intptr_t)fd, "r", io);
  if (!fp) {
    vfd_close(fd);
    return NULL;
  }
  setvbuf(fp, NULL, _IOFBF, 64 * 1024);
  mutexLock(&s_vfd_lock);
  for (int i = 0; i < MAX_VFILES; i++) {
    if (!s_vfiles[i].fp) {
      s_vfiles[i].fp = fp;
      s_vfiles[i].fd = fd;
      break;
    }
  }
  mutexUnlock(&s_vfd_lock);
  return fp;
}

int vfs_file_fd(FILE *fp) {
  int fd = -1;
  mutexLock(&s_vfd_lock);
  for (int i = 0; i < MAX_VFILES; i++)
    if (s_vfiles[i].fp == fp) {
      fd = s_vfiles[i].fd;
      break;
    }
  mutexUnlock(&s_vfd_lock);
  return fd;
}

void vfs_file_closed(FILE *fp) {
  mutexLock(&s_vfd_lock);
  for (int i = 0; i < MAX_VFILES; i++)
    if (s_vfiles[i].fp == fp)
      s_vfiles[i].fp = NULL;
  mutexUnlock(&s_vfd_lock);
}

// --- writes into the tree -------------------------------------------------------

static void mkdirs_for(const char *path) {
  char tmp[768];
  snprintf(tmp, sizeof tmp, "%s", path);
  char *slash = strrchr(tmp, '/');
  if (!slash)
    return;
  *slash = '\0';
  for (char *s = tmp + 1; *s; s++) {
    if (*s == '/') {
      *s = '\0';
      mkdir(tmp, 0777);
      *s = '/';
    }
  }
  mkdir(tmp, 0777);
}

static int copy_out(const NabeFile *f, const char *dest) {
  FILE *o = fopen(dest, "wb");
  if (!o)
    return -1;
  const size_t CH = 1 << 20;
  uint8_t *buf = malloc(CH);
  uint64_t done = 0;
  int ok = buf != NULL;
  while (ok && done < f->size) {
    const size_t n = f->size - done > CH ? CH : (size_t)(f->size - done);
    ok = apkfile_read(f->off + done, buf, n) == (long)n && fwrite(buf, 1, n, o) == n;
    done += n;
  }
  free(buf);
  if (fclose(o) != 0)
    ok = 0;
  if (!ok)
    remove(dest);
  return ok ? 0 : -1;
}

int vfs_prepare_write(const char *path, int truncate) {
  if (!s_ready)
    return 0;
  const char *rel0 = rel_of(path);
  if (!rel0)
    return 0;
  char buf[768];
  const char *rel = norm_rel(rel0, buf, sizeof buf);
  // the tar's directories may not exist on the SD yet
  mkdirs_for(path);
  rmutexLock(&s_lock);
  const NabeFile *f = nabe_lookup(rel);
  int r = 0;
  if (f && !set_has(&s_overlay, rel) && !set_has(&s_whiteout, rel) && !truncate) {
    // rewriting part of a tar file: it needs its content on the SD first
    r = copy_out(f, path);
    if (r == 0)
      log_printf("[vfs] copied %s out of the APK to change it", rel);
    else
      errno = EIO;
  }
  rmutexUnlock(&s_lock);
  return r;
}

void vfs_note_real(const char *path) {
  if (!s_ready)
    return;
  const char *rel0 = rel_of(path);
  if (!rel0)
    return;
  char buf[768];
  const char *rel = norm_rel(rel0, buf, sizeof buf);
  rmutexLock(&s_lock);
  if (nabe_lookup(rel)) {
    set_add(&s_overlay, rel);
    if (set_del(&s_whiteout, rel))
      whiteout_save();
  }
  rmutexUnlock(&s_lock);
}

int vfs_note_removed(const char *path) {
  if (!s_ready)
    return 0;
  const char *rel0 = rel_of(path);
  if (!rel0)
    return 0;
  char buf[768];
  const char *rel = norm_rel(rel0, buf, sizeof buf);
  int was_tar = 0;
  rmutexLock(&s_lock);
  if (nabe_lookup(rel)) {
    set_del(&s_overlay, rel);
    if (set_add(&s_whiteout, rel)) {
      whiteout_save();
      was_tar = 1;
    }
    if (config.log_fs)
      log_printf("[vfs] %s deleted (hidden from the tar)", rel);
  }
  rmutexUnlock(&s_lock);
  return was_tar;
}

int vfs_rename_virtual(const NabeFile *f, const char *from, const char *to) {
  mkdirs_for(to);
  remove(to);
  if (copy_out(f, to) < 0) {
    errno = EIO;
    return -1;
  }
  vfs_note_real(to);
  vfs_note_removed(from);
  return 0;
}

typedef struct {
  const char *dir_rel;
  void (*fn)(const char *, int, void *);
  void *arg;
} ListCtx;

static void list_cb(const char *name, int is_dir, void *arg) {
  ListCtx *c = arg;
  if (!is_dir) {
    char rel[768];
    snprintf(rel, sizeof rel, "%s%s%s", c->dir_rel, c->dir_rel[0] ? "/" : "", name);
    if (set_has(&s_whiteout, rel))
      return;
  }
  c->fn(name, is_dir, c->arg);
}

int vfs_list_virtual(const char *path, void (*fn)(const char *, int, void *), void *arg) {
  if (!s_ready)
    return -1;
  const char *rel0 = rel_of(path);
  if (!rel0)
    return -1;
  char buf[768];
  const char *rel = norm_rel(rel0, buf, sizeof buf);
  ListCtx c = { rel, fn, arg };
  rmutexLock(&s_lock);
  const int n = nabe_list(rel, list_cb, &c);
  rmutexUnlock(&s_lock);
  return n;
}
