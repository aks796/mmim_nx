/* fs_cache.c -- stat()/access() answers kept in memory
 *
 * Pixel Gun asks "is this file there?" dozens of times a frame: its account
 * sync (get_HasV2SyncWork and friends) runs File.Exists on a few paths
 * under Application.persistentDataPath, and Unity's getter for that path
 * looks at the directory again (stat, access) on every call. On Android
 * each is a few microseconds in the kernel's cache; on the Switch every one
 * is a request to the SD card's file system service, and together they
 * took half the main thread's time in a match and three quarters in menus
 * (HW round 2, [prof]).
 *
 * Only this process changes its folder on the SD, and every change goes
 * through the port: the game's libraries through libc_shim.c /
 * bionic_extra.c, the port's own files through fs_mkdir() and the prefs
 * writer. So an answer stays good until the next change, and any change
 * (open for writing, a write, close, rename, remove, mkdir, truncate) drops
 * them all: a generation counter. A lookup remembers the generation it
 * started in, so an answer read while a change was being made is never
 * kept.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>

#include "fs_cache.h"

#define SLOTS 512         // power of two
#define MAX_FDS 4096

typedef struct {
  char *path;
  uint32_t hash;
  uint32_t gen;
  int ret, err;
  struct stat st;
} Entry;

static Entry s_e[SLOTS];
static Mutex s_lock;
static volatile uint32_t s_gen = 1;
static uint8_t s_writer[MAX_FDS / 8];

// per report period
static unsigned s_checks, s_hits;
static uint32_t s_top_hash;
static unsigned s_top_n;
static char s_top[256];
static struct { uint32_t hash; unsigned n; } s_counts[64];

static uint32_t hash_of(const char *s) {
  uint32_t h = 2166136261u;
  for (; *s; s++)
    h = (h ^ (uint8_t)*s) * 16777619u;
  return h;
}

// how often each path is checked (a few dozen distinct paths: the top one
// named in [perf])
static void count(const char *path, uint32_t h) {
  unsigned i = h % 64;
  if (s_counts[i].hash != h) {
    s_counts[i].hash = h;
    s_counts[i].n = 0;
  }
  if (++s_counts[i].n > s_top_n || s_top_hash == h) {
    s_top_n = s_counts[i].n;
    if (s_top_hash != h) {
      s_top_hash = h;
      strncpy(s_top, path, sizeof s_top - 1);
    }
  }
}

int fs_cache_stat(const char *path, struct stat *st) {
  const uint32_t h = hash_of(path);
  Entry *e = &s_e[h & (SLOTS - 1)];
  mutexLock(&s_lock);
  s_checks++;
  count(path, h);
  const uint32_t gen = s_gen;
  if (e->path && e->gen == gen && e->hash == h && !strcmp(e->path, path)) {
    s_hits++;
    const int ret = e->ret;
    if (ret == 0)
      *st = e->st;
    else
      errno = e->err;
    mutexUnlock(&s_lock);
    return ret;
  }
  mutexUnlock(&s_lock);

  struct stat real;
  const int ret = stat(path, &real);
  const int err = errno;

  mutexLock(&s_lock);
  if (gen == s_gen) { // nothing changed meanwhile
    if (!e->path || e->hash != h || strcmp(e->path, path)) {
      free(e->path);
      e->path = strdup(path);
    }
    e->hash = h;
    e->gen = gen;
    e->ret = ret;
    e->err = err;
    e->st = real;
  }
  mutexUnlock(&s_lock);
  if (ret == 0)
    *st = real;
  else
    errno = err;
  return ret;
}

void fs_cache_changed(void) {
  __atomic_add_fetch(&s_gen, 1, __ATOMIC_SEQ_CST);
}

void fs_cache_note_writer(int fd) {
  if (fd >= 0 && fd < MAX_FDS)
    __atomic_or_fetch(&s_writer[fd / 8], (uint8_t)(1u << (fd % 8)), __ATOMIC_RELAXED);
  fs_cache_changed();
}

void fs_cache_wrote(int fd) {
  if (fd >= 0 && fd < MAX_FDS && (s_writer[fd / 8] & (1u << (fd % 8))))
    fs_cache_changed();
}

void fs_cache_closed(int fd) {
  if (fd >= 0 && fd < MAX_FDS && (s_writer[fd / 8] & (1u << (fd % 8)))) {
    __atomic_and_fetch(&s_writer[fd / 8], (uint8_t)~(1u << (fd % 8)), __ATOMIC_RELAXED);
    fs_cache_changed();
  }
}

void fs_mkdir(const char *path) {
  struct stat st;
  if (fs_cache_stat(path, &st) == 0)
    return;
  mkdir(path, 0777);
  fs_cache_changed();
}

void fs_cache_stats(unsigned *checks, unsigned *hits, const char **top, unsigned *top_n) {
  mutexLock(&s_lock);
  *checks = s_checks;
  *hits = s_hits;
  *top = s_top;
  *top_n = s_top_n;
  s_checks = s_hits = 0;
  s_top_n = 0;
  s_top_hash = 0;
  memset(s_counts, 0, sizeof s_counts);
  mutexUnlock(&s_lock);
}
