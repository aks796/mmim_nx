/* dkgl_prof.c -- GPU time from deko3d timestamp reports
 *
 * Every frame gets a pipeline-top timestamp where its commands start, a
 * bottom-of-pipe one at swap, and a pair around the swapchain acquire wait:
 * time the GPU spends waiting for a free swapchain image is not GPU work and
 * is taken out. frame_stats prints the average and worst GPU time per frame.
 *
 * A profiled frame (requested by dkgl_egl.c) also gets a bottom-of-pipe
 * timestamp at every render-target change, dispatch, blit, acquire and draw.
 * Once the frame has completed, the time between consecutive marks is logged
 * per pass, and the time up to each draw's mark is summed per program to list
 * the most expensive ones. GPU timestamps tick at 384/625 per nanosecond.
 */
#include "dkgl.h"

#include <stdio.h>
#include <string.h>

#include "util.h"

DkCmdBuf dkgl_cmd(void);

#define FRAME_SLOTS 4
#define MAX_MARKS 640
enum { R_START, R_ACQ0, R_ACQ1, R_END, R_COUNT };

typedef struct {
  uint64_t counter, ts;
} Report;

typedef struct {
  char kind; /* S start, F framebuffer, D dispatch, B blit, W acquire wait,
                d end of a draw */
  uint32_t id;
  uint32_t prog;
  uint64_t fs; /* draws: fragment shader hash */
} Mark;

static DkglAlloc mem;
static Report *reports; /* [FRAME_SLOTS][R_COUNT], then [MAX_MARKS] */
static bool ready;
static struct {
  uint64_t serial;
  bool started, waited;
} slots[FRAME_SLOTS];
static unsigned cur;
static uint64_t sum_ns, max_ns, frames;

static Mark marks[MAX_MARKS];
static int nmarks;
static bool profiling, profile_next;
static bool prof_pending;
static uint64_t prof_serial;
static uint32_t prof_frame;
static const char *prof_reason, *next_reason;

static DkGpuAddr report_addr(unsigned index) {
  return dkgl_gpu(&mem) + (DkGpuAddr)index * sizeof(Report);
}

static uint64_t ticks_to_ns(uint64_t ticks) { return ticks * 625 / 384; }

void dkgl_prof_init(void) {
  const uint32_t size = (FRAME_SLOTS * R_COUNT + MAX_MARKS) * sizeof(Report);
  if (!dkgl_alloc(DKGL_HEAP_DATA, size, 16, &mem))
    return;
  reports = dkgl_cpu(&mem);
  memset(reports, 0, size);
  ready = true;
}

void dkgl_prof_request(const char *reason) {
  profile_next = true;
  next_reason = reason;
}

static void mark(char kind, uint32_t id, uint32_t prog) {
  if (!profiling || nmarks >= MAX_MARKS)
    return;
  dkCmdBufReportCounter(dkgl_cmd(), DkCounter_Timestamp,
                        report_addr(FRAME_SLOTS * R_COUNT + (unsigned)nmarks));
  marks[nmarks++] = (Mark){kind, id, prog, 0};
}

void dkgl_prof_draw(uint32_t prog, uint64_t fs) {
  if (!profiling || nmarks >= MAX_MARKS)
    return;
  mark('d', 0, prog);
  marks[nmarks - 1].fs = fs;
}

void dkgl_prof_frame_start(void) {
  if (!ready)
    return;
  cur = (cur + 1) % FRAME_SLOTS;
  slots[cur].started = true;
  slots[cur].waited = false;
  slots[cur].serial = 0;
  dkCmdBufReportCounter(dkgl_cmd(), DkCounter_TimestampPipelineTop,
                        report_addr(cur * R_COUNT + R_START));
  if (profile_next && !prof_pending) {
    profile_next = false;
    profiling = true;
    prof_reason = next_reason;
    nmarks = 0;
    mark('S', 0, 0);
  }
}

void dkgl_prof_acquire(bool after) {
  if (!ready || !slots[cur].started)
    return;
  if (!after) {
    mark('W', 0, 0);
    dkCmdBufReportCounter(dkgl_cmd(), DkCounter_Timestamp,
                          report_addr(cur * R_COUNT + R_ACQ0));
  } else {
    dkCmdBufReportCounter(dkgl_cmd(), DkCounter_TimestampPipelineTop,
                          report_addr(cur * R_COUNT + R_ACQ1));
    slots[cur].waited = true;
  }
}

void dkgl_prof_pass(char kind, uint32_t id, uint32_t prog) { mark(kind, id, prog); }

void dkgl_prof_frame_end(uint32_t frame) {
  if (!ready || !slots[cur].started)
    return;
  if (profiling) {
    mark('E', 0, 0);
    profiling = false;
    prof_pending = true;
    prof_frame = frame;
  }
  dkCmdBufReportCounter(dkgl_cmd(), DkCounter_Timestamp,
                        report_addr(cur * R_COUNT + R_END));
  slots[cur].serial = dkgl_signal_point();
  if (prof_pending && !prof_serial)
    prof_serial = slots[cur].serial;
}

static void log_pass(const Mark *m, uint64_t ns, uint32_t draws, uint32_t prog) {
  const char *what = m->kind == 'S'   ? "start"
                     : m->kind == 'F' ? "fbo"
                     : m->kind == 'D' ? "dispatch"
                     : m->kind == 'B' ? "blit"
                     : m->kind == 'W' ? "acquire-wait"
                                      : "?";
  debugPrintf("gpu pass %-12s id=%-4u us=%-6llu draws=%-4u prog=%u\n", what,
              m->id, (unsigned long long)(ns / 1000), draws,
              m->kind == 'D' ? m->prog : prog);
}

typedef struct {
  uint32_t prog;
  uint64_t fs, ns;
  uint32_t draws;
} ProgCost;

static void log_profile(void) {
  const Report *r = reports + FRAME_SLOTS * R_COUNT;
  const uint64_t total =
      nmarks > 1 && r[nmarks - 1].ts > r[0].ts ? r[nmarks - 1].ts - r[0].ts : 0;
  debugPrintf("gpu profile frame=%u (%s): %d marks, %llu us from first to last\n",
              prof_frame, prof_reason ? prof_reason : "?", nmarks,
              (unsigned long long)(ticks_to_ns(total) / 1000));
  static ProgCost costs[160];
  int ncosts = 0;
  int pass = -1;
  uint64_t pass_ns = 0;
  uint32_t pass_draws = 0, pass_prog = 0;
  for (int i = 0; i < nmarks; i++) {
    const Mark *m = &marks[i];
    if (m->kind != 'd' && m->kind != 'E') {
      if (pass >= 0)
        log_pass(&marks[pass], pass_ns, pass_draws, pass_prog);
      pass = i;
      pass_ns = 0;
      pass_draws = 0;
      pass_prog = 0;
    } else if (m->kind == 'd') {
      pass_draws++;
      if (!pass_prog)
        pass_prog = m->prog;
    }
    if (i + 1 >= nmarks)
      break;
    const uint64_t d =
        ticks_to_ns(r[i + 1].ts > r[i].ts ? r[i + 1].ts - r[i].ts : 0);
    pass_ns += d;
    const Mark *next = &marks[i + 1];
    if (next->kind != 'd')
      continue;
    int c = 0;
    while (c < ncosts && costs[c].prog != next->prog)
      c++;
    if (c == ncosts) {
      if (ncosts == (int)(sizeof(costs) / sizeof(costs[0])))
        continue;
      costs[ncosts++] = (ProgCost){next->prog, next->fs, 0, 0};
    }
    costs[c].ns += d;
    costs[c].draws++;
  }
  if (pass >= 0)
    log_pass(&marks[pass], pass_ns, pass_draws, pass_prog);
  /* the most expensive programs, by summed time up to their draws' marks */
  for (int k = 0; k < 12 && k < ncosts; k++) {
    int best = k;
    for (int c = k + 1; c < ncosts; c++)
      if (costs[c].ns > costs[best].ns)
        best = c;
    const ProgCost t = costs[k];
    costs[k] = costs[best];
    costs[best] = t;
    debugPrintf("gpu cost prog=%-4u fs=%016llx draws=%-3u us=%llu\n",
                costs[k].prog, (unsigned long long)costs[k].fs, costs[k].draws,
                (unsigned long long)(costs[k].ns / 1000));
  }
}

void dkgl_prof_collect(void) {
  if (!ready)
    return;
  for (unsigned s = 0; s < FRAME_SLOTS; s++) {
    if (!slots[s].started || !slots[s].serial || slots[s].serial > dkg.completed)
      continue;
    const Report *r = reports + s * R_COUNT;
    if (r[R_END].ts > r[R_START].ts) {
      uint64_t busy = r[R_END].ts - r[R_START].ts;
      if (slots[s].waited && r[R_ACQ1].ts > r[R_ACQ0].ts) {
        const uint64_t idle = r[R_ACQ1].ts - r[R_ACQ0].ts;
        busy = idle < busy ? busy - idle : 0;
      }
      const uint64_t ns = ticks_to_ns(busy);
      sum_ns += ns;
      if (ns > max_ns)
        max_ns = ns;
      frames++;
    }
    slots[s].started = false;
  }
  if (prof_pending && prof_serial && prof_serial <= dkg.completed) {
    log_profile();
    prof_pending = false;
    prof_serial = 0;
  }
}

void dkgl_prof_take(uint64_t *avg_us, uint64_t *max_us) {
  *avg_us = frames ? sum_ns / frames / 1000 : 0;
  *max_us = max_ns / 1000;
  sum_ns = max_ns = frames = 0;
}
