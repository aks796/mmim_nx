/* gcstop.c -- IL2CPP's garbage collector stopping the world, without signals
 *
 * The game's runtime collects with Boehm GC. To scan the stacks of the other
 * threads it stops them: GC_suspend_all() pthread_kill()s each with
 * GC_sig_suspend, and each thread's signal handler records where its stack
 * is (stop_info.stack_ptr, below its saved registers), posts
 * GC_suspend_ack_sem and parks in sigsuspend() until GC_sig_thr_restart.
 * Horizon delivers no signals: without this the first collection waits for
 * acknowledgements forever (or, acknowledged blindly, scans threads that
 * keep running and a stack pointer that was never set).
 *
 * So pthread_kill() does the handler's work for the target, from the
 * collector's side:
 *   suspend: pause the thread (svcSetThreadActivity), read its registers
 *            (svcGetThreadContext3), spill them onto its own stack just below
 *            its stack pointer (where the GC will scan them), set
 *            stop_info.stack_ptr there and last_stop_count to GC_stop_count,
 *            and post the acknowledgement;
 *   restart: let it run again, and acknowledge when GC_retry_signals asks.
 *
 * Pausing a thread anywhere means it may hold a lock of the port's: the
 * collector's side takes none that a game thread could be holding once the
 * first thread is paused. Each collection's thread handles and stacks are
 * read in one go, at its first suspend, when nothing is paused yet (the
 * thread list's lock is taken then only); futexes need no lock at all
 * (libc_shim.c).
 *
 * The GC's globals are found when libil2cpp.so is loaded: GC_stop_init and
 * GC_suspend_handler_inner are located by fingerprint (gcstop_fp.h; the GC
 * is compiled the same in every Unity 6000.3 IL2CPP build), and the globals
 * decoded from their adrp/lo12 pairs. Either function missing or found more
 * than once, or a global outside the module: the shim logs it and stays out
 * (pthread_kill succeeds, nothing more -- the old, unsafe behaviour).
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdint.h>
#include <string.h>
#include <switch.h>

#include "gcstop.h"
#include "gcstop_fp.h"
#include "so_util.h"
#include "libc_shim.h"
#include "log.h"
#include "pthr.h"

// the GC's globals (gcstop_init)
static uintptr_t s_threads;      // GC_threads[256]
static uintptr_t s_stop_count;   // GC_stop_count
static uintptr_t s_ack_sem;      // GC_suspend_ack_sem (bionic sem_t)
static uintptr_t s_retry;        // GC_retry_signals
static uintptr_t s_sig_suspend;  // GC_sig_suspend
static uintptr_t s_sig_restart;  // GC_sig_thr_restart

// GC_Thread_Rep
#define T_NEXT       0
#define T_ID         8
#define T_LAST_STOP  16
#define T_STACK_PTR  24
#define T_FLAGS      32

static int s_ok;
static unsigned s_stops, s_unknown;

#define MAX_PAUSED 128
static struct { void *thread; Handle h; } s_paused[MAX_PAUSED];
static Mutex s_lock;
static int s_npaused;      // threads the collector has paused now

// the profiler (prof.c) pauses threads too: never while the collector has
// any paused, and never at the same moment
Mutex gcstop_pause_lock;

int gcstop_world_paused(void) {
  return __atomic_load_n(&s_npaused, __ATOMIC_ACQUIRE) > 0;
}

static uintptr_t fp_find(uintptr_t lo, uintptr_t hi, const GcFpWord *fp, int n, int *hits) {
  int anchor = 0;
  while (anchor < n && fp[anchor].mask != 0xffffffffu)
    anchor++;
  uintptr_t found = 0;
  *hits = 0;
  for (uintptr_t c = lo; c + (uintptr_t)n * 4 <= hi; c += 4) {
    const uint32_t *w = (const uint32_t *)c;
    if ((w[anchor] & fp[anchor].mask) != fp[anchor].pattern)
      continue;
    int i = 0;
    while (i < n && (w[i] & fp[i].mask) == fp[i].pattern)
      i++;
    if (i == n) {
      found = c;
      (*hits)++;
    }
  }
  return *hits == 1 ? found : 0;
}

// the address an adrp (word ia) + add/ldr/str lo12 (word ib) pair forms
static uintptr_t pair_target(uintptr_t fn, int ia, int ib) {
  const uintptr_t pc = fn + (uintptr_t)ia * 4;
  const uint32_t a = *(const uint32_t *)pc, b = *(const uint32_t *)(fn + (uintptr_t)ib * 4);
  if ((a & 0x9f000000u) != 0x90000000u)
    return 0;
  int64_t imm = (int64_t)((((a >> 5) & 0x7ffffu) << 2) | ((a >> 29) & 3u));
  if (imm & (1ll << 20))
    imm -= 1ll << 21;
  const uintptr_t page = (pc & ~(uintptr_t)0xfff) + (uintptr_t)(imm << 12);
  int64_t lo;
  if ((b & 0x7f800000u) == 0x11000000u) { // add (immediate)
    lo = (int64_t)(((b >> 10) & 0xfffu) << (((b >> 22) & 1u) * 12));
  } else if ((b & 0x3b000000u) == 0x39000000u) { // ldr/str (unsigned offset)
    uint32_t size = (b >> 30) & 3u;
    if (((b >> 26) & 1u) && size == 0 && (((b >> 22) & 3u) & 2u))
      size = 4;
    lo = (int64_t)(((b >> 10) & 0xfffu) << size);
  } else {
    return 0;
  }
  return page + (uintptr_t)lo;
}

#define PAIR(fn, which) pair_target(fn, which)

void gcstop_init(const so_module *m) {
  s_ok = 0;
  const uintptr_t lo = m->base, hi = m->base + m->text_size;
  int h1, h2;
  const uint64_t t0 = armGetSystemTick();
  uintptr_t init = fp_find(lo, hi, k_fp_stop_init, sizeof(k_fp_stop_init) / sizeof(*k_fp_stop_init), &h1);
  uintptr_t hand = fp_find(lo, hi, k_fp_handler, sizeof(k_fp_handler) / sizeof(*k_fp_handler), &h2);
  const char *which = "Unity 6000.3";
  if (init && hand) {
    s_sig_suspend = PAIR(init, FP_INIT_SUSPEND);
    s_sig_restart = PAIR(init, FP_INIT_RESTART);
    s_ack_sem = PAIR(init, FP_INIT_ACKSEM);
    s_stop_count = PAIR(hand, FP_HAND_STOPCOUNT);
    s_threads = PAIR(hand, FP_HAND_THREADS);
    s_retry = PAIR(hand, FP_HAND_RETRY);
  } else {
    int g1, g2;
    init = fp_find(lo, hi, k_fp22_stop_init, sizeof(k_fp22_stop_init) / sizeof(*k_fp22_stop_init), &g1);
    hand = fp_find(lo, hi, k_fp22_handler, sizeof(k_fp22_handler) / sizeof(*k_fp22_handler), &g2);
    which = "Unity 2022.2";
    if (!init || !hand) {
      int g3, g4;
      init = fp_find(lo, hi, k_fp21_stop_init, sizeof(k_fp21_stop_init) / sizeof(*k_fp21_stop_init), &g3);
      hand = fp_find(lo, hi, k_fp21_handler, sizeof(k_fp21_handler) / sizeof(*k_fp21_handler), &g4);
      if (!init || !hand) {
        log_printf("[gc] stop-the-world emulation OFF: GC_stop_init found %d/%d/%d time(s), "
                   "GC_suspend_handler_inner %d/%d/%d (Unity 6000.3 / 2022.2 / 2022.1 code)", h1, g1, g3, h2, g2,
                   g4);
        return;
      }
      which = "Unity 2022.1";
    }
    s_sig_suspend = PAIR(init, FP22_INIT_SUSPEND);
    s_sig_restart = PAIR(init, FP22_INIT_RESTART);
    s_ack_sem = PAIR(init, FP22_INIT_ACKSEM);
    s_stop_count = PAIR(hand, FP22_HAND_STOPCOUNT);
    s_threads = PAIR(hand, FP22_HAND_THREADS);
    s_retry = PAIR(hand, FP22_HAND_RETRY);
  }
  log_printf("[gc] GC code found (%s fingerprints)", which);
  const uintptr_t g[] = { s_sig_suspend, s_sig_restart, s_ack_sem, s_stop_count, s_threads, s_retry };
  for (unsigned i = 0; i < sizeof(g) / sizeof(*g); i++)
    if (g[i] < m->base || g[i] + 8 > m->base + m->size || (g[i] == s_threads && g[i] + 256 * 8 > m->base + m->size)) {
      log_printf("[gc] stop-the-world emulation OFF: global %u at %p is outside libil2cpp.so", i, (void *)g[i]);
      return;
    }
  s_ok = 1;
  log_printf("[gc] stop-the-world emulation on (%llu ms): suspend signal %d at +0x%lx, restart %d at +0x%lx, "
             "ack +0x%lx, stop count +0x%lx, threads +0x%lx, retry +0x%lx",
             (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000),
             *(int *)s_sig_suspend, (unsigned long)(s_sig_suspend - m->base), *(int *)s_sig_restart,
             (unsigned long)(s_sig_restart - m->base), (unsigned long)(s_ack_sem - m->base),
             (unsigned long)(s_stop_count - m->base), (unsigned long)(s_threads - m->base),
             (unsigned long)(s_retry - m->base));
}

static uintptr_t *thread_rep(void *t) {
  uintptr_t **tab = (uintptr_t **)s_threads;
  for (int i = 0; i < 256; i++)
    for (uintptr_t *p = tab[i]; p; p = (uintptr_t *)p[T_NEXT / 8])
      if ((void *)p[T_ID / 8] == t)
        return p;
  return NULL;
}

static void ack(void) {
  sem_post_fake((void **)s_ack_sem);
}

// every thread's handle and stack, read at a collection's first suspend
static uintptr_t s_snap_stop = ~(uintptr_t)0;
static int s_nsnap;
static pthread_t s_snap_t[MAX_PAUSED];
static Handle s_snap_h[MAX_PAUSED];
static uintptr_t s_snap_lo[MAX_PAUSED], s_snap_hi[MAX_PAUSED];

static int thread_info(void *t, Handle *h, uintptr_t *lo, uintptr_t *hi) {
  const uintptr_t stop = *(volatile uintptr_t *)s_stop_count;
  if (stop != s_snap_stop) {
    s_snap_stop = stop;
    s_nsnap = pthr_snapshot(s_snap_t, s_snap_h, s_snap_lo, s_snap_hi, MAX_PAUSED);
  }
  for (int i = 0; i < s_nsnap; i++)
    if (s_snap_t[i] == (pthread_t)t) {
      *h = s_snap_h[i];
      *lo = s_snap_lo[i];
      *hi = s_snap_hi[i];
      return 1;
    }
  return 0;
}

static int suspend(void *t) {
  Handle h;
  uintptr_t lo, hi;
  if (!thread_info(t, &h, &lo, &hi)) {
    s_unknown++;
    return 3; // ESRCH: the collector skips a thread that has gone
  }
  uintptr_t *rep = thread_rep(t);
  if (!rep) {
    s_unknown++;
    return 3;
  }
  mutexLock(&gcstop_pause_lock);
  __atomic_add_fetch(&s_npaused, 1, __ATOMIC_ACQ_REL);
  const Result pr = svcSetThreadActivity(h, ThreadActivity_Paused);
  mutexUnlock(&gcstop_pause_lock);
  if (R_FAILED(pr)) {
    __atomic_sub_fetch(&s_npaused, 1, __ATOMIC_ACQ_REL);
    return 3;
  }
  ThreadContext ctx;
  uintptr_t sp;
  if (R_SUCCEEDED(svcGetThreadContext3(&ctx, h)) && ctx.sp > lo && ctx.sp <= hi) {
    sp = ctx.sp;
    // the registers, where the collector will find them: below the stack
    // pointer (nothing lives there while the thread is paused)
    const int n = 29 + 3 + 32;
    uintptr_t at = (sp - (uintptr_t)n * 8) & ~(uintptr_t)15;
    if (at < lo)
      at = sp;
    uint64_t *w = (uint64_t *)at;
    if (at != sp) {
      for (int i = 0; i < 29; i++)
        *w++ = ctx.cpu_gprs[i].x;
      *w++ = ctx.fp;
      *w++ = ctx.lr;
      *w++ = ctx.pc.x;
      for (int i = 0; i < 32; i++)
        *w++ = (uint64_t)ctx.fpu_gprs[i].d;
    }
    sp = at;
  } else {
    // no context: the whole stack is scanned (conservative, never short)
    sp = lo;
  }
  mutexLock(&s_lock);
  for (int i = 0; i < MAX_PAUSED; i++)
    if (!s_paused[i].thread) {
      s_paused[i].thread = t;
      s_paused[i].h = h;
      break;
    }
  mutexUnlock(&s_lock);
  rep[T_STACK_PTR / 8] = sp;
  __atomic_store_n(&rep[T_LAST_STOP / 8], *(volatile uintptr_t *)s_stop_count,
                   __ATOMIC_RELEASE);
  ack();
  return 0;
}

static int restart(void *t) {
  Handle h = INVALID_HANDLE;
  mutexLock(&s_lock);
  for (int i = 0; i < MAX_PAUSED; i++)
    if (s_paused[i].thread == t) {
      h = s_paused[i].h;
      s_paused[i].thread = NULL;
      break;
    }
  mutexUnlock(&s_lock);
  if (h != INVALID_HANDLE) {
    svcSetThreadActivity(h, ThreadActivity_Runnable);
    __atomic_sub_fetch(&s_npaused, 1, __ATOMIC_ACQ_REL);
  }
  if (*(volatile int *)s_retry) {
    uintptr_t *rep = thread_rep(t);
    ack();
    if (rep)
      __atomic_store_n(&rep[T_LAST_STOP / 8],
                       *(volatile uintptr_t *)s_stop_count | 1, __ATOMIC_RELEASE);
  }
  return 0;
}

int gcstop_kill(void *thread, int sig, int *handled) {
  *handled = 0;
  if (!s_ok || !sig)
    return 0;
  const int susp = *(volatile int *)s_sig_suspend;
  const int rest = *(volatile int *)s_sig_restart;
  if (sig != susp && sig != rest)
    return 0;
  *handled = 1;
  int is_suspend = sig == susp;
  if (susp == rest) { // one signal for both: a paused thread is being restarted
    is_suspend = 1;
    mutexLock(&s_lock);
    for (int i = 0; i < MAX_PAUSED; i++)
      if (s_paused[i].thread == thread)
        is_suspend = 0;
    mutexUnlock(&s_lock);
  }
  if (is_suspend) {
    s_stops++;
    return suspend(thread);
  }
  return restart(thread);
}

void gcstop_stats(unsigned *stops, unsigned *unknown) {
  if (stops) *stops = s_stops;
  if (unknown) *unknown = s_unknown;
}
