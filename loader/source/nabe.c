/* nabe.c -- assets/nabe.tar as a read-only file tree (see nabe.h)
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "apkfile.h"
#include "log.h"
#include "nabe.h"
#include "paths.h"

#define IDX_MAGIC "NABEIDX2"

typedef struct {
  char *path;
  int nchildren, cap;
  struct { const char *name; int is_dir; } *child;
} Dir;

static NabeFile *s_files;
static int s_nfiles;
static char *s_pool;       // names
static size_t s_pool_used, s_pool_cap;
static int *s_fhash;       // file hash (indices), -1 empty
static unsigned s_fcap;
static Dir *s_dirs;
static int s_ndirs, s_dcap;
static int *s_dhash;
static unsigned s_dhcap;

static uint32_t h32(const char *s) {
  uint32_t h = 2166136261u;
  for (; *s; s++)
    h = (h ^ (uint8_t)*s) * 16777619u;
  return h;
}

static const char *pool_add(const char *s, size_t n) {
  if (s_pool_used + n + 1 > s_pool_cap) {
    // pointers into the pool must stay valid: grow by chaining a new block
    const size_t cap = (n + 1) > (1u << 20) ? n + 1 : (1u << 20);
    s_pool = malloc(cap);
    s_pool_cap = cap;
    s_pool_used = 0;
  }
  char *p = s_pool + s_pool_used;
  memcpy(p, s, n);
  p[n] = '\0';
  s_pool_used += n + 1;
  return p;
}

static int dir_find(const char *path) {
  if (!s_dhash)
    return -1;
  unsigned i = h32(path) & (s_dhcap - 1);
  while (s_dhash[i] >= 0) {
    if (!strcmp(s_dirs[s_dhash[i]].path, path))
      return s_dhash[i];
    i = (i + 1) & (s_dhcap - 1);
  }
  return -1;
}

static void dir_rehash(void) {
  free(s_dhash);
  s_dhcap = 64;
  while (s_dhcap < (unsigned)s_ndirs * 2 + 16)
    s_dhcap <<= 1;
  s_dhash = malloc(s_dhcap * sizeof(int));
  memset(s_dhash, 0xff, s_dhcap * sizeof(int));
  for (int d = 0; d < s_ndirs; d++) {
    unsigned i = h32(s_dirs[d].path) & (s_dhcap - 1);
    while (s_dhash[i] >= 0)
      i = (i + 1) & (s_dhcap - 1);
    s_dhash[i] = d;
  }
}

static void dir_add_child(int d, const char *name, int is_dir) {
  Dir *dd = &s_dirs[d];
  if (dd->nchildren == dd->cap) {
    dd->cap = dd->cap ? dd->cap * 2 : 16;
    dd->child = realloc(dd->child, dd->cap * sizeof(*dd->child));
  }
  dd->child[dd->nchildren].name = name;
  dd->child[dd->nchildren].is_dir = is_dir;
  dd->nchildren++;
}

// the directory for path, created with its parents
static int dir_get(const char *path) {
  int d = dir_find(path);
  if (d >= 0)
    return d;
  if (s_ndirs == s_dcap) {
    s_dcap = s_dcap ? s_dcap * 2 : 512;
    s_dirs = realloc(s_dirs, s_dcap * sizeof(Dir));
  }
  d = s_ndirs++;
  memset(&s_dirs[d], 0, sizeof(Dir));
  s_dirs[d].path = (char *)pool_add(path, strlen(path));
  if ((unsigned)s_ndirs * 2 + 16 > s_dhcap)
    dir_rehash();
  else {
    unsigned i = h32(path) & (s_dhcap - 1);
    while (s_dhash[i] >= 0)
      i = (i + 1) & (s_dhcap - 1);
    s_dhash[i] = d;
  }
  // link into the parent ("" is the root: the files dir)
  if (path[0]) {
    const char *slash = strrchr(path, '/');
    char parent[512];
    const char *name = path;
    if (slash) {
      snprintf(parent, sizeof parent, "%.*s", (int)(slash - path), path);
      name = s_dirs[d].path + (slash - path) + 1;
    } else {
      parent[0] = '\0';
      name = s_dirs[d].path;
    }
    const int pd = dir_get(parent);
    dir_add_child(pd, name, 1);
  }
  return d;
}

static void add_file(const char *path, uint64_t off, uint64_t size) {
  if (s_nfiles % 4096 == 0)
    s_files = realloc(s_files, (s_nfiles + 4096) * sizeof(NabeFile));
  const char *p = pool_add(path, strlen(path));
  s_files[s_nfiles].path = p;
  s_files[s_nfiles].off = off;
  s_files[s_nfiles].size = size;
  s_nfiles++;
  const char *slash = strrchr(p, '/');
  char parent[512];
  if (slash)
    snprintf(parent, sizeof parent, "%.*s", (int)(slash - p), p);
  else
    parent[0] = '\0';
  dir_add_child(dir_get(parent), slash ? slash + 1 : p, 0);
}

static void build_file_hash(void) {
  free(s_fhash);
  s_fcap = 1;
  while (s_fcap < (unsigned)s_nfiles * 2 + 16)
    s_fcap <<= 1;
  s_fhash = malloc(s_fcap * sizeof(int));
  memset(s_fhash, 0xff, s_fcap * sizeof(int));
  for (int f = 0; f < s_nfiles; f++) {
    unsigned i = h32(s_files[f].path) & (s_fcap - 1);
    while (s_fhash[i] >= 0)
      i = (i + 1) & (s_fcap - 1);
    s_fhash[i] = f;
  }
}

static uint64_t octal(const char *p, int n) {
  uint64_t v = 0;
  // GNU base-256 for sizes over 8 GB
  if ((uint8_t)p[0] & 0x80) {
    for (int i = 1; i < n; i++)
      v = v << 8 | (uint8_t)p[i];
    return v;
  }
  for (int i = 0; i < n && p[i]; i++) {
    if (p[i] == ' ')
      continue;
    if (p[i] < '0' || p[i] > '7')
      break;
    v = v * 8 + (uint64_t)(p[i] - '0');
  }
  return v;
}

// Walk the tar headers. Small members are read many to a window; a big one
// is skipped over with a fresh window at the next header.
static int build(uint64_t tar_off, uint64_t tar_size, void (*progress)(int, void *), void *arg) {
  const size_t WIN = 256 * 1024;
  uint8_t *win = malloc(WIN);
  if (!win)
    return -1;
  uint64_t win_off = (uint64_t)-1, win_len = 0;
  uint64_t pos = 0;
  char longname[1024];
  longname[0] = '\0';
  int last_pm = -1;
  while (pos + 512 <= tar_size) {
    if (win_off == (uint64_t)-1 || pos < win_off || pos + 512 > win_off + win_len) {
      const uint64_t want = tar_size - pos < WIN ? tar_size - pos : WIN;
      const long got = apkfile_read(tar_off + pos, win, (size_t)want);
      if (got < 512) {
        free(win);
        return -1;
      }
      win_off = pos;
      win_len = (uint64_t)got;
    }
    const char *h = (const char *)win + (pos - win_off);
    int zero = 1;
    for (int i = 0; i < 512 && zero; i++)
      zero = h[i] == 0;
    if (zero)
      break; // end of archive
    const uint64_t size = octal(h + 124, 12);
    const char type = h[156];
    char name[1024];
    if (longname[0]) {
      snprintf(name, sizeof name, "%s", longname);
      longname[0] = '\0';
    } else if (!memcmp(h + 257, "ustar", 5) && h[345]) {
      snprintf(name, sizeof name, "%.155s/%.100s", h + 345, h);
    } else {
      snprintf(name, sizeof name, "%.100s", h);
    }
    const uint64_t data = pos + 512;
    if (type == 'L') {
      // GNU long name: the next member's name is this member's data
      const size_t n = size < sizeof longname ? (size_t)size : sizeof longname - 1;
      if (apkfile_read(tar_off + data, longname, n) != (long)n) {
        free(win);
        return -1;
      }
      longname[n] = '\0';
    } else {
      // strip "./" and trailing slashes
      char *nm = name;
      while (nm[0] == '.' && nm[1] == '/')
        nm += 2;
      size_t l = strlen(nm);
      while (l && nm[l - 1] == '/')
        nm[--l] = '\0';
      if (l) {
        if (type == '5')
          dir_get(nm);
        else if (type == '0' || type == '\0' || type == '7')
          add_file(nm, tar_off + data, size);
      }
    }
    pos = data + ((size + 511) & ~(uint64_t)511);
    const int pm = (int)(pos * 1000 / tar_size);
    if (progress && pm != last_pm) {
      last_pm = pm;
      progress(pm, arg);
    }
  }
  free(win);
  return 0;
}

static void idx_path(char *out, size_t n) {
  snprintf(out, n, "%s/nabe.idx", path_internal());
}

static int save_index(uint64_t tar_off, uint64_t tar_size) {
  char p[512], tmp[520];
  idx_path(p, sizeof p);
  snprintf(tmp, sizeof tmp, "%s.part", p);
  FILE *f = fopen(tmp, "wb");
  if (!f)
    return -1;
  setvbuf(f, NULL, _IOFBF, 256 * 1024);
  const uint64_t hdr[3] = { apkfile_size(), tar_off, tar_size };
  const uint32_t n = (uint32_t)s_nfiles;
  int ok = fwrite(IDX_MAGIC, 1, 8, f) == 8 && fwrite(hdr, sizeof hdr, 1, f) == 1 &&
           fwrite(&n, 4, 1, f) == 1;
  for (int i = 0; ok && i < s_nfiles; i++) {
    const uint16_t l = (uint16_t)strlen(s_files[i].path);
    ok = fwrite(&s_files[i].off, 8, 1, f) == 1 && fwrite(&s_files[i].size, 8, 1, f) == 1 &&
         fwrite(&l, 2, 1, f) == 1 && fwrite(s_files[i].path, 1, l, f) == l;
  }
  if (fclose(f) != 0)
    ok = 0;
  if (!ok) {
    remove(tmp);
    return -1;
  }
  remove(p);
  return rename(tmp, p);
}

static int load_index(uint64_t tar_off, uint64_t tar_size) {
  char p[512];
  idx_path(p, sizeof p);
  FILE *f = fopen(p, "rb");
  if (!f)
    return -1;
  fseek(f, 0, SEEK_END);
  const long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *buf = len > 0 ? malloc(len) : NULL;
  const int rd = buf && fread(buf, 1, len, f) == (size_t)len;
  fclose(f);
  if (!rd || len < 36 || memcmp(buf, IDX_MAGIC, 8) != 0) {
    free(buf);
    return -1;
  }
  uint64_t hdr[3];
  uint32_t n;
  memcpy(hdr, buf + 8, sizeof hdr);
  memcpy(&n, buf + 32, 4);
  if (hdr[0] != apkfile_size() || hdr[1] != tar_off || hdr[2] != tar_size) {
    free(buf);
    return -1;
  }
  const uint8_t *q = buf + 36, *end = buf + len;
  for (uint32_t i = 0; i < n; i++) {
    if (q + 18 > end)
      break;
    uint64_t off, size;
    uint16_t l;
    memcpy(&off, q, 8);
    memcpy(&size, q + 8, 8);
    memcpy(&l, q + 16, 2);
    q += 18;
    if (q + l > end)
      break;
    char name[1024];
    snprintf(name, sizeof name, "%.*s", (int)l, (const char *)q);
    q += l;
    add_file(name, off, size);
  }
  free(buf);
  return s_nfiles == (int)n ? 0 : -1;
}

static void reset(void) {
  for (int d = 0; d < s_ndirs; d++)
    free(s_dirs[d].child);
  free(s_dirs);
  free(s_files);
  free(s_fhash);
  free(s_dhash);
  s_dirs = NULL;
  s_files = NULL;
  s_fhash = s_dhash = NULL;
  s_ndirs = s_dcap = s_nfiles = 0;
  s_dhcap = s_fcap = 0;
  // the name pool blocks are leaked on a reset (it only follows a bad index)
  s_pool = NULL;
  s_pool_used = s_pool_cap = 0;
  dir_rehash();
  dir_get(""); // the root
}

int nabe_init(void (*progress)(int, void *), void *arg) {
  const ApkEntry *e = apkfile_find("assets/nabe.tar");
  if (!e) {
    log_printf("[nabe] the APK has no assets/nabe.tar");
    return -1;
  }
  if (e->method != 0) {
    log_printf("[nabe] assets/nabe.tar is compressed in this APK (method %u)", e->method);
    return -1;
  }
  const u64 t0 = armGetSystemTick();
  reset();
  if (load_index(e->data_off, e->usize) == 0) {
    build_file_hash();
    log_printf("[nabe] index loaded: %d files, %d directories (%llu ms)", s_nfiles, s_ndirs,
               (unsigned long long)armTicksToNs(armGetSystemTick() - t0) / 1000000ull);
    return s_nfiles;
  }
  reset();
  log_printf("[nabe] building the index of nabe.tar (%llu MB at 0x%llx)",
             (unsigned long long)(e->usize >> 20), (unsigned long long)e->data_off);
  if (build(e->data_off, e->usize, progress, arg) < 0) {
    log_printf("[nabe] reading the tar failed");
    return -1;
  }
  build_file_hash();
  if (save_index(e->data_off, e->usize) < 0)
    log_printf("[nabe] could not save the index (it is rebuilt next time)");
  log_printf("[nabe] index built: %d files, %d directories (%llu ms)", s_nfiles, s_ndirs,
             (unsigned long long)armTicksToNs(armGetSystemTick() - t0) / 1000000ull);
  return s_nfiles;
}

int nabe_count(void) { return s_nfiles; }

const NabeFile *nabe_lookup(const char *rel) {
  if (!s_fhash || !rel)
    return NULL;
  unsigned i = h32(rel) & (s_fcap - 1);
  while (s_fhash[i] >= 0) {
    const NabeFile *f = &s_files[s_fhash[i]];
    if (!strcmp(f->path, rel))
      return f;
    i = (i + 1) & (s_fcap - 1);
  }
  return NULL;
}

int nabe_is_dir(const char *rel) {
  return rel && dir_find(rel) >= 0;
}

int nabe_list(const char *rel, void (*fn)(const char *, int, void *), void *arg) {
  const int d = dir_find(rel ? rel : "");
  if (d < 0)
    return -1;
  for (int i = 0; i < s_dirs[d].nchildren; i++)
    fn(s_dirs[d].child[i].name, s_dirs[d].child[i].is_dir, arg);
  return s_dirs[d].nchildren;
}
