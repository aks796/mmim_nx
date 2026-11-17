/* log.c -- debug.log next to the .nro
 *
 * Every line also goes to svcOutputDebugString (an attached debugger, an
 * emulator's log). The card only commits a file's length when it is fsync()ed
 * or closed, so a crash leaves a cut log unless it is synced: until
 * log_boot_done() (start-up is where crashes happen and SD writes are cheap
 * there) every line is written and fsync()ed at once. After it, lines go
 * into memory and a thread of the log's own writes them out every 100 ms
 * and syncs the file every second: the game's warnings (a few a second in
 * a match) and the port's own lines no longer wait for the SD on the
 * thread that logs. log_flush() (the crash handler, quitting) writes and
 * syncs at once. The previous launch's log is kept as debug.prev.log.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <switch.h>

#include "log.h"

#define BUF_SIZE (64 * 1024)
#define WRITE_NS 100000000ull
#define SYNC_NS 1000000000ull

static FILE *s_log;
static Mutex s_lock;     // the buffers
static Mutex s_io;       // the file; taken before s_lock
static CondVar s_cv;
static volatile int s_boot_done;
static char s_buf[2][BUF_SIZE];
static size_t s_len;
static int s_cur;
static int s_unsynced;
static Thread s_thread;

void log_init(const char *dir) {
  char path[600], prev[600];
  snprintf(path, sizeof(path), "%s/debug.log", dir);
  snprintf(prev, sizeof(prev), "%s/debug.prev.log", dir);
  remove(prev);
  rename(path, prev);
  mutexInit(&s_lock);
  mutexInit(&s_io);
  condvarInit(&s_cv);
  s_log = fopen(path, "w");
}

// what is in memory, into the file (s_io held)
static void drain(int sync) {
  mutexLock(&s_lock);
  const int b = s_cur;
  const size_t n = s_len;
  s_cur ^= 1;
  s_len = 0;
  mutexUnlock(&s_lock);
  if (n) {
    fwrite(s_buf[b], 1, n, s_log);
    fflush(s_log);
    s_unsynced = 1;
  }
  if (sync && s_unsynced) {
    fsync(fileno(s_log));
    s_unsynced = 0;
  }
}

static void writer(void *arg) {
  (void)arg;
  u64 last_sync = armGetSystemTick();
  for (;;) {
    mutexLock(&s_lock);
    if (!s_len)
      condvarWaitTimeout(&s_cv, &s_lock, SYNC_NS);
    mutexUnlock(&s_lock);
    const int sync = armTicksToNs(armGetSystemTick() - last_sync) >= SYNC_NS;
    mutexLock(&s_io);
    drain(sync);
    mutexUnlock(&s_io);
    if (sync)
      last_sync = armGetSystemTick();
    svcSleepThread(WRITE_NS); // lines that come meanwhile go out together
  }
}

void log_boot_done(void) {
  if (s_boot_done || !s_log)
    return;
  // above the game's threads: it mostly waits for the card
  if (R_FAILED(threadCreate(&s_thread, writer, NULL, NULL, 0x4000, 0x2c, -2)) ||
      R_FAILED(threadStart(&s_thread)))
    return; // lines keep being written at once
  s_boot_done = 1;
}

void log_printf(const char *fmt, ...) {
  char line[1200];
  const u64 ms = armTicksToNs(armGetSystemTick()) / 1000000ull;
  int n = snprintf(line, sizeof(line), "[%6llu.%03llu] ", (unsigned long long)(ms / 1000),
                   (unsigned long long)(ms % 1000));
  va_list va;
  va_start(va, fmt);
  vsnprintf(line + n, sizeof(line) - n - 1, fmt, va);
  va_end(va);

  svcOutputDebugString(line, strlen(line));
  if (!s_log)
    return;
  size_t len = strlen(line);
  line[len++] = '\n';
  if (!s_boot_done) {
    mutexLock(&s_io);
    fwrite(line, 1, len, s_log);
    fflush(s_log);
    fsync(fileno(s_log));
    mutexUnlock(&s_io);
    return;
  }
  for (;;) {
    mutexLock(&s_lock);
    if (s_len + len <= BUF_SIZE) {
      memcpy(s_buf[s_cur] + s_len, line, len);
      s_len += len;
      mutexUnlock(&s_lock);
      condvarWakeOne(&s_cv);
      return;
    }
    mutexUnlock(&s_lock);
    // full (a burst): out now, on this thread
    mutexLock(&s_io);
    drain(0);
    mutexUnlock(&s_io);
  }
}

void log_flush(void) {
  if (!s_log)
    return;
  mutexLock(&s_io);
  drain(0);
  fsync(fileno(s_log));
  s_unsynced = 0;
  mutexUnlock(&s_io);
}
