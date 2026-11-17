/* apkcache.c -- the APK's game data kept in RAM (see apkcache.h)
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <switch.h>

#include "apkcache.h"
#include "apkfile.h"
#include "config.h"
#include "log.h"
#include "paths.h"

#define MAX_RANGES 8
#define CHUNK (1u << 20)
#define MAX_FILES 32

typedef struct {
  const char *name;
  uint64_t off, len;
  uint8_t *buf;
  _Atomic uint64_t filled; // bytes from off that are in buf
} Range;

static Range s_r[MAX_RANGES];
static int s_nr;
static Thread s_thread;
static void *_Atomic s_files[MAX_FILES];
static _Atomic unsigned s_hits, s_misses;
static _Atomic uint64_t s_hit_bytes, s_miss_bytes;

// "sdmc:/switch/x/y.apk" and "/switch/x/y.apk" are the same file
static const char *strip_dev(const char *p) {
  const char *c = strchr(p, ':');
  const char *slash = strchr(p, '/');
  return c && slash && c < slash ? c + 1 : p;
}

void apkcache_fopened(void *file, const char *path) {
  if (!file || !s_nr || !path || strcmp(strip_dev(path), strip_dev(path_apk())))
    return;
  for (int i = 0; i < MAX_FILES; i++) {
    void *expect = NULL;
    if (atomic_compare_exchange_strong(&s_files[i], &expect, file))
      return;
  }
}

void apkcache_fclosed(void *file) {
  if (!file)
    return;
  for (int i = 0; i < MAX_FILES; i++) {
    void *expect = file;
    atomic_compare_exchange_strong(&s_files[i], &expect, NULL);
  }
}

int apkcache_is_file(void *file) {
  if (!file || !s_nr)
    return 0;
  for (int i = 0; i < MAX_FILES; i++)
    if (atomic_load_explicit(&s_files[i], memory_order_relaxed) == file)
      return 1;
  return 0;
}

int apkcache_read(uint64_t off, void *buf, size_t len) {
  for (int i = 0; i < s_nr; i++) {
    const Range *r = &s_r[i];
    if (off < r->off || off + len > r->off + r->len)
      continue;
    const uint64_t have = atomic_load_explicit(&s_r[i].filled, memory_order_acquire);
    if (off + len > r->off + have)
      break; // not read in yet: the SD
    memcpy(buf, r->buf + (off - r->off), len);
    atomic_fetch_add_explicit(&s_hits, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&s_hit_bytes, len, memory_order_relaxed);
    return 1;
  }
  atomic_fetch_add_explicit(&s_misses, 1, memory_order_relaxed);
  atomic_fetch_add_explicit(&s_miss_bytes, len, memory_order_relaxed);
  return 0;
}

void apkcache_report(char *out, size_t size) {
  const unsigned h = atomic_exchange(&s_hits, 0), m = atomic_exchange(&s_misses, 0);
  const uint64_t hb = atomic_exchange(&s_hit_bytes, 0), mb = atomic_exchange(&s_miss_bytes, 0);
  snprintf(out, size, "%u from memory (%llu KB), %u from the SD (%llu KB)", h,
           (unsigned long long)(hb >> 10), m, (unsigned long long)(mb >> 10));
}

static void filler(void *arg) {
  (void)arg;
  const u64 t0 = armGetSystemTick();
  uint64_t total = 0;
  for (int i = 0; i < s_nr; i++) {
    Range *r = &s_r[i];
    uint64_t done = 0;
    while (done < r->len) {
      const size_t want = r->len - done > CHUNK ? CHUNK : (size_t)(r->len - done);
      const long got = apkfile_read_sd(r->off + done, r->buf + done, want);
      if (got != (long)want) {
        log_printf("[apkcache] %s: read failed at %llu; the rest stays on the SD", r->name,
                   (unsigned long long)done);
        break;
      }
      done += want;
      atomic_store_explicit(&r->filled, done, memory_order_release);
    }
    total += done;
  }
  log_printf("[apkcache] %d part(s), %llu MB in memory after %.1f s", s_nr,
             (unsigned long long)(total >> 20), armTicksToNs(armGetSystemTick() - t0) / 1e9);
}

// data.unity3d first (every scene and Resources object is in it), then the
// .resource files (audio, textures' pixel data) from the smallest up
static int range_order(const void *a, const void *b) {
  const Range *x = a, *y = b;
  const int dx = strstr(x->name, "data.unity3d") != NULL, dy = strstr(y->name, "data.unity3d") != NULL;
  if (dx != dy)
    return dy - dx;
  return x->len < y->len ? -1 : x->len > y->len;
}

void apkcache_start(void) {
  if (!config.apk_cache)
    return;
  const int n = apkfile_count();
  uint64_t bytes = 0;
  for (int i = 0; i < n && s_nr < MAX_RANGES; i++) {
    const ApkEntry *e = apkfile_entry_at(i);
    if (!e || e->method != 0 || e->usize < (64u << 10) || strncmp(e->name, "assets/bin/Data/", 16))
      continue;
    Range *r = &s_r[s_nr];
    r->buf = malloc(e->usize);
    if (!r->buf) {
      log_printf("[apkcache] no memory for %s (%llu MB)", e->name, (unsigned long long)(e->usize >> 20));
      continue;
    }
    r->name = e->name;
    r->off = e->data_off;
    r->len = e->usize;
    atomic_store(&r->filled, 0);
    bytes += e->usize;
    s_nr++;
  }
  if (!s_nr)
    return;
  qsort(s_r, (size_t)s_nr, sizeof(Range), range_order);
  // lowest priority: the engine's own loading goes first
  if (R_FAILED(threadCreate(&s_thread, filler, NULL, NULL, 0x4000, 0x3b, -2)) ||
      R_FAILED(threadStart(&s_thread))) {
    log_printf("[apkcache] no thread: reads stay on the SD");
    for (int i = 0; i < s_nr; i++)
      free(s_r[i].buf);
    s_nr = 0;
    return;
  }
  log_printf("[apkcache] reading %d part(s) of the APK into memory, %llu MB (%s first)", s_nr,
             (unsigned long long)(bytes >> 20), s_r[0].name);
}
