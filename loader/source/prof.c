/* prof.c -- where the main thread and the render thread spend their time
 * (config profile)
 *
 * Every 4 ms a thread of the port's stops the main thread (Unity's main
 * loop) and Unity's render thread ("UnityGfxDeviceW") for a moment, reads
 * where each is (svcGetThreadContext3) and lets it go. A thread inside a
 * system call is waiting (a lock, a condition, the GPU, the display, a
 * service: the SD card, sockets); on the console its pc is the `svc`
 * itself (HW round 2: every wait was counted as running port code),
 * elsewhere the instruction after it. Anything else is running. A sample in
 * the port's own code (the shims, mesa) is put down to the first game
 * library frame that called it (the port's code keeps frame records), so
 * "waiting <- libunity+0x..." names the engine call that waited. Running
 * game code is counted by function (the library's .eh_frame_hdr table of
 * function starts): one line per busy function. Every 10 s the most common
 * places go to debug.log as [prof] lines; libunity / libil2cpp offsets are
 * named offline (tools/unity_sym.py, the IL2CPP dump's methods.tsv).
 *
 * config hitch_log 1 (the default for MMIM): every sample
 * is kept a while with its time instead, and a frame of the main loop that
 * took over 50 ms (main.c) logs where the main and render threads were
 * during it ([hitch] lines, the first 40), for one-off stalls that a 10 s
 * window drowns.
 *
 * The garbage collector pauses threads too (gcstop.c): the profiler skips
 * while it has any paused and never pauses one at the same moment.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "config.h"
#include "log.h"
#include "pthr.h"
#include "so_util.h"

#define PERIOD_NS 8000000ll
#define REPORT_NS 10000000000ll
#define SLOTS 1024
#define TOP 20

extern Mutex gcstop_pause_lock;
int gcstop_world_paused(void);

typedef struct {
  uintptr_t key;   // pc (running) or the waiting caller | 1
  unsigned n;
  uintptr_t via;   // waiting: the port's function that made the system call
} Slot;

typedef struct {
  const char *name;
  Handle h;
  unsigned samples, waiting, in_port, in_unity, in_il2cpp;
  Slot slots[SLOTS];
} Target;

static Target s_t[2] = { { .name = "main" }, { .name = "UnityGfxDeviceW" } };
static Thread s_thread;
static int s_started;
static volatile int s_match, s_match_reset; // a split match runs (MMIM: LocalPlay.Profile): 10 s windows

typedef struct {
  u64 tick;
  uintptr_t key, via;
  u8 target;
} Rec;
#define RING 2048
static Rec s_ring[RING];
static unsigned s_ring_n;
static Mutex s_ring_lock;

static int in_nro(uintptr_t v) {
  extern char _start[];
  return v >= (uintptr_t)_start && v - (uintptr_t)_start < (64u << 20);
}

static const char *where(uintptr_t v, char *buf, size_t n) {
  extern char _start[];
  uintptr_t base;
  const char *name = so_name_at_unlocked(v, &base);
  if (name)
    snprintf(buf, n, "%s+0x%lx", name, (unsigned long)(v - base));
  else if (in_nro(v))
    snprintf(buf, n, "nro+0x%lx", (unsigned long)(v - (uintptr_t)_start));
  else
    snprintf(buf, n, "0x%lx", (unsigned long)v);
  return buf;
}

// --- function starts from .eh_frame_hdr --------------------------------------------------
// version 1, then eh_frame_ptr (4 or 8 bytes), fde_count (udata4), and the
// sorted table of (function start, FDE) pairs as sdata4 from the header

typedef struct {
  const so_module *m;
  const uint8_t *hdr;
  const int32_t *table;
  uint32_t count;
} EhTable;

static EhTable s_eh[8];

// the table of module m (looked up once per module; NULL when it has none)
static const EhTable *eh_table(const so_module *m) {
  int i = 0;
  for (; i < 8 && s_eh[i].m; i++)
    if (s_eh[i].m == m)
      return s_eh[i].table ? &s_eh[i] : NULL;
  if (i == 8)
    return NULL;
  EhTable *e = &s_eh[i];
  e->m = m;
  for (int k = 0; k < m->phnum; k++) {
    if (m->phdr[k].p_type != PT_GNU_EH_FRAME)
      continue;
    const uint8_t *h = (const uint8_t *)(m->base + m->phdr[k].p_vaddr);
    const int ptr_size = (h[1] & 0x07) == 0x03 ? 4 : (h[1] & 0x07) == 0x04 ? 8 : 0;
    if (h[0] == 1 && ptr_size && h[2] == 0x03 && h[3] == 0x3b) {
      e->hdr = h;
      e->count = *(const uint32_t *)(h + 4 + ptr_size);
      e->table = (const int32_t *)(h + 4 + ptr_size + 4);
    }
    break;
  }
  return e->table ? e : NULL;
}

// the start of the function holding pc, 0 when unknown
static uintptr_t func_start(uintptr_t pc) {
  const so_module *m = so_module_at(pc);
  const EhTable *e = m ? eh_table(m) : NULL;
  if (!e || !e->count)
    return 0;
  const intptr_t off = (intptr_t)(pc - (uintptr_t)e->hdr);
  uint32_t lo = 0, hi = e->count;
  if (e->table[0] > off)
    return 0;
  while (hi - lo > 1) {
    const uint32_t mid = (lo + hi) / 2;
    if (e->table[2 * mid] <= off)
      lo = mid;
    else
      hi = mid;
  }
  return (uintptr_t)e->hdr + e->table[2 * lo];
}

static void count(Target *t, uintptr_t key, uintptr_t via) {
  if (config.hitch_log) {
    mutexLock(&s_ring_lock);
    Rec *r = &s_ring[s_ring_n++ % RING];
    r->tick = armGetSystemTick();
    r->key = key;
    r->via = via;
    r->target = (u8)(t - s_t);
    mutexUnlock(&s_ring_lock);
  }
  unsigned i = (unsigned)((key >> 2) * 2654435761u) % SLOTS;
  for (int probe = 0; probe < SLOTS; probe++, i = (i + 1) % SLOTS) {
    if (t->slots[i].key == key || !t->slots[i].key) {
      t->slots[i].key = key;
      t->slots[i].n++;
      if (via)
        t->slots[i].via = via;
      return;
    }
  }
}

// up the port's frame records from fp: the first return address in the
// port that is not libnx's svc wrapper's caller chain top (the function
// that waited: a futex, a condition, mesa's fence wait)
static uintptr_t port_waiter(uintptr_t fp, uintptr_t lr, uintptr_t lo, uintptr_t hi) {
  if (in_nro(lr))
    return lr;
  if (fp >= lo && fp + 16 <= hi && !(fp & 7)) {
    const uintptr_t ret = ((const uintptr_t *)fp)[1];
    if (in_nro(ret))
      return ret;
  }
  return 0;
}

// the first return address in a game library up the frame records from fp
static uintptr_t game_caller(uintptr_t fp, uintptr_t lr, uintptr_t lo, uintptr_t hi) {
  uintptr_t base;
  if (lr && so_name_at_unlocked(lr, &base))
    return lr;
  for (int i = 0; i < 24 && fp >= lo && fp + 16 <= hi && !(fp & 7); i++) {
    const uintptr_t next = ((const uintptr_t *)fp)[0], ret = ((const uintptr_t *)fp)[1];
    if (ret && so_name_at_unlocked(ret, &base))
      return ret;
    if (next <= fp)
      break;
    fp = next;
  }
  return 0;
}

static void sample(Target *t) {
  if (!t->h && !pthr_find(t->name, &t->h))
    return;
  uintptr_t lo = 0, hi = 0;
  ThreadContext ctx;
  mutexLock(&gcstop_pause_lock);
  if (gcstop_world_paused()) {
    mutexUnlock(&gcstop_pause_lock);
    return;
  }
  if (R_FAILED(svcSetThreadActivity(t->h, ThreadActivity_Paused))) {
    mutexUnlock(&gcstop_pause_lock);
    t->h = 0; // gone: looked up again
    return;
  }
  const Result r = svcGetThreadContext3(&ctx, t->h);
  svcSetThreadActivity(t->h, ThreadActivity_Runnable);
  mutexUnlock(&gcstop_pause_lock);
  if (R_FAILED(r))
    return;
  const uintptr_t pc = ctx.pc.x, lr = ctx.lr, fp = ctx.fp;
  // the stack's bounds: from sp to the end of its memory region (only frame
  // records are read; past the region is unmapped)
  lo = ctx.sp;
  hi = lo + (1u << 20);
  MemoryInfo mi;
  u32 pi;
  if (R_SUCCEEDED(svcQueryMemory(&mi, &pi, ctx.sp)) && mi.addr + mi.size > lo)
    hi = mi.addr + mi.size;
  t->samples++;
  uintptr_t base;
  const char *mod = so_name_at_unlocked(pc, &base);
  // in a system call: the console reports the svc's own address, others the
  // next instruction
#define IS_SVC(w) (((w) & 0xffe0001f) == 0xd4000001)
  const int waiting = (in_nro(pc) || !mod) &&
                      (IS_SVC(*(const uint32_t *)pc) || IS_SVC(*(const uint32_t *)(pc - 4)));
#undef IS_SVC
  if (waiting) {
    t->waiting++;
    count(t, game_caller(fp, lr, lo, hi) | 1, port_waiter(fp, lr, lo, hi));
  } else if (!mod) {
    t->in_port++;
    const uintptr_t c = game_caller(fp, lr, lo, hi);
    count(t, c ? c | 2 : pc, c ? pc : 0);
  } else {
    if (!strcmp(mod, "libunity.so"))
      t->in_unity++;
    else if (!strcmp(mod, "libil2cpp.so"))
      t->in_il2cpp++;
    const uintptr_t fn = func_start(pc);
    count(t, fn ? fn : pc, 0);
  }
}

static int by_n(const void *a, const void *b) {
  return (int)((const Slot *)b)->n - (int)((const Slot *)a)->n;
}

static void report(Target *t) {
  if (!t->samples)
    return;
  const double k = 100.0 / t->samples;
  log_printf("[prof] %s: %u samples; waiting %.0f%%, running libunity %.0f%%, libil2cpp %.0f%%, port %.0f%%, other %.0f%%",
             t->name, t->samples, t->waiting * k, t->in_unity * k, t->in_il2cpp * k, t->in_port * k,
             (t->samples - t->waiting - t->in_unity - t->in_il2cpp - t->in_port) * k);
  static Slot top[SLOTS];
  memcpy(top, t->slots, sizeof top);
  qsort(top, SLOTS, sizeof *top, by_n);
  char a[96], b[96];
  for (int i = 0; i < TOP && top[i].n; i++) {
    const uintptr_t key = top[i].key;
    const char *what = key & 1 ? "waiting <- " : key & 2 ? "port <- " : "";
    log_printf("[prof]   %4.1f%% %s%s%s%s", top[i].n * k, what, key == 1 ? "?" : where(key & ~(uintptr_t)3, a, sizeof a),
               top[i].via ? " in " : "", top[i].via ? where(top[i].via, b, sizeof b) : "");
  }
  memset(t->slots, 0, sizeof t->slots);
  t->samples = t->waiting = t->in_port = t->in_unity = t->in_il2cpp = 0;
}

static volatile unsigned s_frames; // main.c: frames run so far

void prof_frame(unsigned frames) {
  s_frames = frames;
}

static void prof_thread(void *arg) {
  (void)arg;
  // not while the engine starts: pausing its threads then stalled it in
  // Ryubing (no first frame); a few seconds of frames first
  while (s_frames < 300)
    svcSleepThread(100000000ll);
  log_printf("[prof] sampling starts");
  u64 last = armGetSystemTick();
  for (;;) {
    svcSleepThread(PERIOD_NS);
    if (s_match_reset) {
      // a match began: its windows from its start (not the menus' samples)
      s_match_reset = 0;
      for (int i = 0; i < 2; i++) {
        memset(s_t[i].slots, 0, sizeof s_t[i].slots);
        s_t[i].samples = s_t[i].waiting = s_t[i].in_port = s_t[i].in_unity = s_t[i].in_il2cpp = 0;
      }
      last = armGetSystemTick();
    }
    for (int i = 0; i < 2; i++)
      sample(&s_t[i]);
    if ((config.profile || s_match) && armTicksToNs(armGetSystemTick() - last) >= (u64)REPORT_NS) {
      last = armGetSystemTick();
      for (int i = 0; i < 2; i++)
        report(&s_t[i]);
    }
  }
}

// a long frame of the main loop, from t0 to t1 (ticks): where both threads
// were during it, the most common places first
void prof_hitch(u64 t0, u64 t1) {
  static int logged;
  if (!config.hitch_log || logged >= 80 || s_frames < 300)
    return; // nothing sampled yet (the engine's start-up frames)
  static Slot agg[2][64];
  unsigned total[2] = { 0, 0 };
  memset(agg, 0, sizeof agg);
  mutexLock(&s_ring_lock);
  const unsigned n = s_ring_n < RING ? s_ring_n : RING;
  for (unsigned k = 0; k < n; k++) {
    const Rec *r = &s_ring[(s_ring_n - 1 - k) % RING];
    if (r->tick < t0)
      break; // older from here
    if (r->tick > t1 || r->target > 1)
      continue;
    total[r->target]++;
    Slot *a = agg[r->target];
    int i = 0;
    while (i < 64 && a[i].key && a[i].key != r->key)
      i++;
    if (i < 64) {
      a[i].key = r->key;
      a[i].n++;
      if (r->via)
        a[i].via = r->via;
    }
  }
  mutexUnlock(&s_ring_lock);
  if (!total[0] && !total[1])
    return;
  logged++;
  log_printf("[hitch] frame of %llu ms: main thread %u samples, render thread %u",
             (unsigned long long)(armTicksToNs(t1 - t0) / 1000000ull), total[0], total[1]);
  char a[96], b[96];
  for (int t = 0; t < 2; t++) {
    if (!total[t])
      continue;
    qsort(agg[t], 64, sizeof agg[t][0], by_n);
    for (int i = 0; i < 6 && agg[t][i].n; i++) {
      const uintptr_t key = agg[t][i].key;
      const char *what = key & 1 ? "waiting <- " : key & 2 ? "port <- " : "";
      log_printf("[hitch]   %s %3u%% %s%s%s%s", t ? "render" : "main  ", agg[t][i].n * 100 / total[t], what,
                 key == 1 ? "?" : where(key & ~(uintptr_t)3, a, sizeof a), agg[t][i].via ? " in " : "",
                 agg[t][i].via ? where(agg[t][i].via, b, sizeof b) : "");
    }
  }
}

void prof_start_now(void);

// MMIM's split matches (game_patch.c, LocalPlay.Native op 4): where the
// threads were, in 10 s windows, while one runs -- the profiler started now
// if the config did not start it
void prof_match(int on) {
  if (on && !s_match)
    s_match_reset = 1;
  s_match = on;
  log_printf("[prof] split match %s", on ? "began: 10 s windows" : "ended");
  if (on)
    prof_start_now();
}

void prof_start(void) {
  if (!config.profile && !config.hitch_log)
    return;
  prof_start_now();
}

void prof_start_now(void) {
  if (s_started)
    return;
  s_started = 1;
  mutexInit(&s_ring_lock);
  // above the game's threads, so samples come on time
  if (R_FAILED(threadCreate(&s_thread, prof_thread, NULL, NULL, 0x4000, 0x29, -2)) ||
      R_FAILED(threadStart(&s_thread))) {
    log_printf("[prof] the profiler thread did not start");
    return;
  }
  log_printf("[prof] sampling the main and render threads every 8 ms:%s%s", config.profile ? " 10 s windows" : "",
             config.hitch_log ? " frames over 50 ms (hitch_log)" : "");
}
