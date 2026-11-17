/* apkfile.c -- random access to the player's APK (see apkfile.h)
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <zlib.h>
#include <switch.h>

#include "apkfile.h"
#include "apkcache.h"
#include "log.h"

typedef struct {
  ApkEntry e;
  uint64_t lho;      // local header offset
  int resolved;      // data_off known
} Entry;

static FsFile s_file;
static int s_open;
static uint64_t s_size;
static char s_path[512];
static char s_err[256];
static Entry *s_entries;
static int s_count;
static char *s_names;      // all entry names, NUL-separated
static int *s_hash;        // open addressing, -1 empty
static unsigned s_hash_cap;
static Mutex s_lock;

static void set_err(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void set_err(const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);
  vsnprintf(s_err, sizeof s_err, fmt, va);
  va_end(va);
  log_printf("[apk] %s", s_err);
}

const char *apkfile_error(void) { return s_err; }
const char *apkfile_path(void) { return s_path; }
uint64_t apkfile_size(void) { return s_size; }
int apkfile_count(void) { return s_count; }

long apkfile_read_sd(uint64_t off, void *buf, size_t len) {
  if (!s_open)
    return -1;
  if (off >= s_size)
    return 0;
  if (len > s_size - off)
    len = (size_t)(s_size - off);
  u64 got = 0;
  size_t total = 0;
  // FS reads are capped per request on some firmwares; keep them moderate
  while (total < len) {
    const size_t want = len - total > (32u << 20) ? (32u << 20) : len - total;
    Result rc = fsFileRead(&s_file, (s64)(off + total), (u8 *)buf + total, want,
                           FsReadOption_None, &got);
    if (R_FAILED(rc)) {
      log_printf("[apk] read of %zu bytes at 0x%llx failed: 0x%x", want,
                 (unsigned long long)(off + total), rc);
      return total ? (long)total : -1;
    }
    if (!got)
      break;
    total += got;
  }
  return (long)total;
}

// the game data Unity reads (data.unity3d, the .resource files) comes from
// RAM once apkcache.c has read it in
long apkfile_read(uint64_t off, void *buf, size_t len) {
  if (len && off < s_size && len <= s_size - off && apkcache_read(off, buf, len))
    return (long)len;
  return apkfile_read_sd(off, buf, len);
}

static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline uint64_t rd64(const uint8_t *p) { return rd32(p) | (uint64_t)rd32(p + 4) << 32; }

static uint32_t name_hash(const char *s) {
  uint32_t h = 2166136261u;
  for (; *s; s++)
    h = (h ^ (uint8_t)*s) * 16777619u;
  return h;
}

static void hash_insert(int idx) {
  unsigned i = name_hash(s_entries[idx].e.name) & (s_hash_cap - 1);
  while (s_hash[i] >= 0)
    i = (i + 1) & (s_hash_cap - 1);
  s_hash[i] = idx;
}

void apkfile_close(void) {
  if (s_open)
    fsFileClose(&s_file);
  s_open = 0;
  free(s_entries);
  free(s_names);
  free(s_hash);
  s_entries = NULL;
  s_names = NULL;
  s_hash = NULL;
  s_count = 0;
}

int apkfile_open(const char *path) {
  apkfile_close();
  mutexInit(&s_lock);
  snprintf(s_path, sizeof s_path, "%s", path);
  const char *fspath = path;
  const char *colon = strchr(path, ':');
  if (colon)
    fspath = colon + 1;
  FsFileSystem *fs = fsdevGetDeviceFileSystem("sdmc");
  if (!fs) {
    set_err("no sdmc filesystem");
    return -1;
  }
  char p[FS_MAX_PATH];
  snprintf(p, sizeof p, "%s", fspath);
  Result rc = fsFsOpenFile(fs, p, FsOpenMode_Read, &s_file);
  if (R_FAILED(rc)) {
    set_err("cannot open %s (0x%x)", path, rc);
    return -1;
  }
  s_open = 1;
  s64 sz = 0;
  fsFileGetSize(&s_file, &sz);
  s_size = (uint64_t)sz;

  // end of central directory: in the last 64 KB + 22 bytes
  const size_t tail_len = s_size < 65557 ? (size_t)s_size : 65557;
  uint8_t *tail = malloc(tail_len);
  if (!tail || apkfile_read(s_size - tail_len, tail, tail_len) != (long)tail_len) {
    free(tail);
    set_err("cannot read the end of %s", path);
    return -1;
  }
  long eocd = -1;
  for (long i = (long)tail_len - 22; i >= 0; i--)
    if (rd32(tail + i) == 0x06054b50) { eocd = i; break; }
  if (eocd < 0) {
    free(tail);
    set_err("%s is not a zip/APK file", path);
    return -1;
  }
  uint64_t count = rd16(tail + eocd + 10);
  uint64_t cd_size = rd32(tail + eocd + 12);
  uint64_t cd_off = rd32(tail + eocd + 16);
  if ((count == 0xffff || cd_size == 0xffffffffu || cd_off == 0xffffffffu) && eocd >= 20 &&
      rd32(tail + eocd - 20) == 0x07064b50) {
    // zip64: the locator points at the zip64 end record
    const uint64_t z64 = rd64(tail + eocd - 20 + 8);
    uint8_t rec[56];
    if (apkfile_read(z64, rec, sizeof rec) == (long)sizeof rec && rd32(rec) == 0x06064b50) {
      count = rd64(rec + 32);
      cd_size = rd64(rec + 40);
      cd_off = rd64(rec + 48);
    }
  }
  free(tail);
  if (cd_off + cd_size > s_size || count == 0 || count > 1000000) {
    set_err("%s has a damaged zip directory", path);
    return -1;
  }

  uint8_t *cd = malloc(cd_size);
  if (!cd || apkfile_read(cd_off, cd, cd_size) != (long)cd_size) {
    free(cd);
    set_err("cannot read the zip directory of %s", path);
    return -1;
  }
  s_entries = calloc(count, sizeof(Entry));
  s_names = malloc(cd_size); // names are shorter than their records
  size_t names_used = 0;
  const uint8_t *p8 = cd, *end = cd + cd_size;
  int n = 0;
  while (p8 + 46 <= end && rd32(p8) == 0x02014b50 && (uint64_t)n < count) {
    const uint16_t nlen = rd16(p8 + 28), xlen = rd16(p8 + 30), clen = rd16(p8 + 32);
    if (p8 + 46 + nlen + xlen + clen > end)
      break;
    Entry *e = &s_entries[n];
    e->e.method = rd16(p8 + 10);
    e->e.mtime_dos = (uint32_t)rd16(p8 + 14) << 16 | rd16(p8 + 12);
    e->e.crc = rd32(p8 + 16);
    e->e.csize = rd32(p8 + 20);
    e->e.usize = rd32(p8 + 24);
    e->lho = rd32(p8 + 42);
    // zip64 extra: the fields that were 0xffffffff, in order
    const uint8_t *x = p8 + 46 + nlen, *xend = x + xlen;
    while (x + 4 <= xend) {
      const uint16_t id = rd16(x), sz2 = rd16(x + 2);
      if (id == 1) {
        const uint8_t *q = x + 4;
        if (e->e.usize == 0xffffffffu && q + 8 <= x + 4 + sz2) { e->e.usize = rd64(q); q += 8; }
        if (e->e.csize == 0xffffffffu && q + 8 <= x + 4 + sz2) { e->e.csize = rd64(q); q += 8; }
        if (e->lho == 0xffffffffu && q + 8 <= x + 4 + sz2) { e->lho = rd64(q); q += 8; }
      }
      x += 4 + sz2;
    }
    memcpy(s_names + names_used, p8 + 46, nlen);
    s_names[names_used + nlen] = '\0';
    e->e.name = s_names + names_used;
    names_used += nlen + 1;
    n++;
    p8 += 46 + nlen + xlen + clen;
  }
  free(cd);
  s_count = n;
  s_hash_cap = 1;
  while (s_hash_cap < (unsigned)n * 2 + 16)
    s_hash_cap <<= 1;
  s_hash = malloc(s_hash_cap * sizeof(int));
  memset(s_hash, 0xff, s_hash_cap * sizeof(int));
  for (int i = 0; i < n; i++)
    hash_insert(i);
  log_printf("[apk] %s: %d entries, %llu MB", path, n, (unsigned long long)(s_size >> 20));
  return 0;
}

// the local header's own name/extra lengths decide where the data starts
static int resolve(Entry *e) {
  if (e->resolved)
    return 0;
  uint8_t lh[30];
  if (apkfile_read(e->lho, lh, sizeof lh) != (long)sizeof lh || rd32(lh) != 0x04034b50) {
    log_printf("[apk] bad local header for %s", e->e.name);
    return -1;
  }
  e->e.data_off = e->lho + 30 + rd16(lh + 26) + rd16(lh + 28);
  e->resolved = 1;
  return 0;
}

const ApkEntry *apkfile_entry_at(int i) {
  if (i < 0 || i >= s_count)
    return NULL;
  mutexLock(&s_lock);
  int ok = resolve(&s_entries[i]) == 0;
  mutexUnlock(&s_lock);
  return ok ? &s_entries[i].e : NULL;
}

const ApkEntry *apkfile_find(const char *name) {
  if (!s_hash || !name)
    return NULL;
  unsigned i = name_hash(name) & (s_hash_cap - 1);
  while (s_hash[i] >= 0) {
    Entry *e = &s_entries[s_hash[i]];
    if (!strcmp(e->e.name, name)) {
      mutexLock(&s_lock);
      int ok = resolve(e) == 0;
      mutexUnlock(&s_lock);
      return ok ? &e->e : NULL;
    }
    i = (i + 1) & (s_hash_cap - 1);
  }
  return NULL;
}

void *apkfile_load(const ApkEntry *e, size_t *size_out) {
  if (!e)
    return NULL;
  uint8_t *out = malloc(e->usize ? e->usize : 1);
  if (!out)
    return NULL;
  if (e->method == 0) {
    if (apkfile_read(e->data_off, out, e->usize) != (long)e->usize) {
      free(out);
      return NULL;
    }
  } else if (e->method == 8) {
    uint8_t *in = malloc(e->csize ? e->csize : 1);
    if (!in || apkfile_read(e->data_off, in, e->csize) != (long)e->csize) {
      free(in);
      free(out);
      return NULL;
    }
    z_stream zs;
    memset(&zs, 0, sizeof zs);
    inflateInit2(&zs, -MAX_WBITS);
    zs.next_in = in;
    zs.avail_in = (uInt)e->csize;
    zs.next_out = out;
    zs.avail_out = (uInt)e->usize;
    const int r = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    free(in);
    if (r != Z_STREAM_END || zs.total_out != e->usize) {
      log_printf("[apk] inflating %s failed (%d)", e->name, r);
      free(out);
      return NULL;
    }
  } else {
    log_printf("[apk] %s: compression method %u is not supported", e->name, e->method);
    free(out);
    return NULL;
  }
  if (size_out)
    *size_out = (size_t)e->usize;
  return out;
}

int apkfile_extract(const ApkEntry *e, const char *dest, void (*progress)(uint64_t, void *),
                    void *arg) {
  char tmp[600];
  snprintf(tmp, sizeof tmp, "%s.part", dest);
  FILE *f = fopen(tmp, "wb");
  if (!f) {
    log_printf("[apk] cannot write %s (errno %d)", tmp, errno);
    return -1;
  }
  const size_t CH = 1 << 20;
  uint8_t *in = malloc(CH), *out = malloc(CH);
  int ok = in && out;
  uint64_t done = 0;
  if (ok && e->method == 0) {
    while (done < e->usize && ok) {
      const size_t n = e->usize - done > CH ? CH : (size_t)(e->usize - done);
      ok = apkfile_read(e->data_off + done, in, n) == (long)n && fwrite(in, 1, n, f) == n;
      done += n;
      if (progress)
        progress(done, arg);
    }
  } else if (ok && e->method == 8) {
    z_stream zs;
    memset(&zs, 0, sizeof zs);
    inflateInit2(&zs, -MAX_WBITS);
    uint64_t read_off = 0;
    int r = Z_OK;
    while (ok && r != Z_STREAM_END) {
      if (zs.avail_in == 0 && read_off < e->csize) {
        const size_t n = e->csize - read_off > CH ? CH : (size_t)(e->csize - read_off);
        ok = apkfile_read(e->data_off + read_off, in, n) == (long)n;
        read_off += n;
        zs.next_in = in;
        zs.avail_in = (uInt)n;
      }
      zs.next_out = out;
      zs.avail_out = (uInt)CH;
      r = inflate(&zs, Z_NO_FLUSH);
      if (r != Z_OK && r != Z_STREAM_END && !(r == Z_BUF_ERROR && zs.avail_in == 0 && read_off < e->csize))
        ok = 0;
      const size_t have = CH - zs.avail_out;
      if (ok && have && fwrite(out, 1, have, f) != have)
        ok = 0;
      done += have;
      if (progress)
        progress(done, arg);
      if (ok && r != Z_STREAM_END && zs.avail_in == 0 && read_off >= e->csize && have == 0)
        ok = 0; // truncated stream
    }
    inflateEnd(&zs);
    ok = ok && done == e->usize;
  } else {
    ok = 0;
  }
  free(in);
  free(out);
  if (fclose(f) != 0)
    ok = 0;
  if (!ok) {
    log_printf("[apk] extracting %s failed", e->name);
    remove(tmp);
    return -1;
  }
  remove(dest);
  if (rename(tmp, dest) != 0) {
    log_printf("[apk] cannot rename %s (errno %d)", tmp, errno);
    remove(tmp);
    return -1;
  }
  return 0;
}
