/* vsync.c -- the display's vertical blank, as the frame clock (see vsync.h)
 *
 * Android's Choreographer calls its frame callbacks once per display vsync,
 * and Unity 6 paces its frames by them (a frame waits for the next callback,
 * whose time stamp is the frame's). The swap (eglSwapBuffers, interval 1)
 * waits for the real vblank too, so the callbacks must come from the same
 * clock: two free-running 60 Hz clocks drift against each other and cost a
 * whole frame now and then (mvl's finding).
 *
 * A thread waits on the display's own vsync event (vi GetDisplayVsyncEvent)
 * and, on each, stamps the time (the clock System.nanoTime and the game
 * see) and calls every listener. With no event (or none for 50 ms: the HOME
 * menu has the display) a 60 Hz timer keeps the callbacks coming.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdint.h>
#include <switch.h>

#include "libc_shim.h"
#include "log.h"
#include "vsync.h"

#define PERIOD_NS 16666667ll
#define MAX_LISTENERS 8

static Thread s_thread;
static Event s_event;
static int s_have_event, s_started;
static volatile int64_t s_last_ns;
static volatile uint64_t s_count;
static Mutex s_lock;
static struct { void (*fn)(int64_t ns, void *arg); void *arg; } s_listeners[MAX_LISTENERS];
static int s_nlisteners;

static void fire(void) {
  const int64_t now = android_monotonic_ns();
  s_last_ns = now;
  s_count++;
  mutexLock(&s_lock);
  const int n = s_nlisteners;
  for (int i = 0; i < n; i++)
    s_listeners[i].fn(now, s_listeners[i].arg);
  mutexUnlock(&s_lock);
}

static void vsync_thread(void *arg) {
  (void)arg;
  int64_t last = android_monotonic_ns();
  for (;;) {
    int fired = 0;
    if (s_have_event && R_SUCCEEDED(eventWait(&s_event, 50000000ull)))
      fired = 1;
    const int64_t now = android_monotonic_ns();
    if (!fired && !s_have_event && now - last < PERIOD_NS) {
      svcSleepThread(PERIOD_NS - (now - last));
      fired = 1;
    }
    if (fired || android_monotonic_ns() - last >= PERIOD_NS) {
      fire();
      last = android_monotonic_ns();
    }
  }
}

void vsync_start(void) {
  if (s_started)
    return;
  s_started = 1;
  mutexInit(&s_lock);
  // the display libnx opened for the window (default_window.c): a second
  // OpenDisplay("Default") fails
  ViDisplay *nwindowGetDefaultDisplay(void);
  ViDisplay *disp = nwindowGetDefaultDisplay();
  Result r1 = 0, r2 = disp ? 0 : 1, r3 = 1;
  if (disp)
    r3 = viGetDisplayVsyncEvent(disp, &s_event);
  s_have_event = R_SUCCEEDED(r3);
  // above the game's threads (priority 59 for them, pthr.c): a late vsync
  // is a late frame
  if (R_FAILED(threadCreate(&s_thread, vsync_thread, NULL, NULL, 0x4000, 0x2a, -2)) ||
      R_FAILED(threadStart(&s_thread))) {
    log_printf("[vsync] the vsync thread did not start");
    return;
  }
  if (s_have_event)
    log_printf("[vsync] frame clock: the display's vsync");
  else
    log_printf("[vsync] frame clock: a 60 Hz timer (no vsync event: vi 0x%x, display 0x%x, event 0x%x)",
               r1, r2, r3);
}

void vsync_listen(void (*fn)(int64_t ns, void *arg), void *arg) {
  vsync_start();
  mutexLock(&s_lock);
  if (s_nlisteners < MAX_LISTENERS) {
    s_listeners[s_nlisteners].fn = fn;
    s_listeners[s_nlisteners].arg = arg;
    s_nlisteners++;
  }
  mutexUnlock(&s_lock);
}

int64_t vsync_last_ns(void) {
  return s_last_ns;
}

uint64_t vsync_count(void) {
  return s_count;
}
