/* android_ndk.c -- the libandroid / libmediandk functions the game's libraries
 * import directly:
 *
 *   ANativeWindow_*   the console's default window (libnx NWindow); Unity
 *                     makes its own EGL surface on it
 *   ALooper_*         a per-thread wait/wake (Unity's threads park in it)
 *   ASensor*          no sensors (yet)
 *   AAssetManager_*   the APK's assets/ read straight out of the zip
 *                     (Wwise reads its banks this way)
 *   AMedia*, AImage*, AHardwareBuffer_*: no video decoding; every call fails
 *                     cleanly, which Unity's VideoPlayer reports and skips
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <switch.h>

#include "apkfile.h"
#include "config.h"
#include "jni.h"
#include "libc_shim.h"
#include "log.h"
#include "net_shim.h"
#include "pthr.h"
#include "vsync.h"
#include "so_util.h"
#include "vfs.h"

// --- ANativeWindow ----------------------------------------------------------------------

typedef struct ANativeWindow ANativeWindow;

static int s_win_w = 1280, s_win_h = 720;

static void win_geometry(int w, int h) {
  NWindow *nw = nwindowGetDefault();
  nwindowSetDimensions(nw, (u32)w, (u32)h);
  nwindowSetCrop(nw, 0, 0, (u32)w, (u32)h);
  s_win_w = w;
  s_win_h = h;
}

void android_window_init(int w, int h) {
  win_geometry(w, h);
}

// Destroying an EGL surface releases the window's buffers, and libnx then
// forgets its size (0x0): the next surface would be made at the default
// 1280x720 under the panel-sized crop, which the compositor refuses at the
// first present (QueueBuffer BadValue, mesa's fatal 2349-0004). Unity
// destroys and remakes its surface while it starts, so the size and crop
// go back on before each new one.
void android_window_prepare(void) {
  if (!nwindowGetDefault()->slots_configured)
    win_geometry(s_win_w, s_win_h);
}

static ANativeWindow *ANativeWindow_fromSurface(void *env, void *surface) {
  (void)env; (void)surface;
  return (ANativeWindow *)nwindowGetDefault();
}

static void *ANativeWindow_toSurface(void *env, ANativeWindow *w) {
  (void)env; (void)w;
  return jni_surface();
}

static void ANativeWindow_acquire(ANativeWindow *w) { (void)w; }
static void ANativeWindow_release(ANativeWindow *w) { (void)w; }
static int32_t ANativeWindow_getWidth(ANativeWindow *w) { (void)w; return s_win_w; }
static int32_t ANativeWindow_getHeight(ANativeWindow *w) { (void)w; return s_win_h; }
static int32_t ANativeWindow_getFormat(ANativeWindow *w) { (void)w; return 1; } // RGBA_8888

// The window is the panel's size, fixed: a smaller swapchain is never
// presented by the console (the first swap would block forever). Android
// devices without hardware scaling behave the same -- the call succeeds and
// the size stays -- and Unity then scales its own back buffer.
static int32_t ANativeWindow_setBuffersGeometry(ANativeWindow *w, int32_t width, int32_t height,
                                                int32_t format) {
  (void)w; (void)format;
  if (width > 0 && height > 0 && (width != s_win_w || height != s_win_h))
    log_printf("[ndk] setBuffersGeometry %dx%d ignored (the window stays %dx%d)", width, height,
               s_win_w, s_win_h);
  return 0;
}

static int32_t ANativeWindow_setFrameRate(ANativeWindow *w, float rate, int8_t compat) {
  (void)w; (void)rate; (void)compat;
  return 0;
}

// --- ALooper ---------------------------------------------------------------------------
//
// A looper is a per-thread wait: pollOnce sleeps until it is woken
// (ALooper_wake), a watched fd is ready (ALooper_addFd: the callback runs,
// or its ident is returned), or a frame callback of its AChoreographer is
// due (below). The wait is a poll() over the shim's in-process pipes
// (net_shim.c): one pipe per looper for wake(), and Unity's own handler
// pipes (NdkLooperHandler: 24-byte messages, read by its fd callback).
//
// Android's UI thread has a looper Unity uses (JNI_OnLoad keeps
// ALooper_forThread() as the UI looper and posts messages to it from
// UnityMain). Here the main thread runs UnityMain, so the UI looper is a
// thread of its own (ndk_ui_looper_start) that only polls, and
// ALooper_forThread() on the main thread answers with it.

#define ALOOPER_POLL_WAKE     (-1)
#define ALOOPER_POLL_CALLBACK (-2)
#define ALOOPER_POLL_TIMEOUT  (-3)
#define ALOOPER_POLL_ERROR    (-4)
#define LOOPER_MAX_FDS 32

typedef struct Choreo Choreo;

typedef struct {
  int fd, ident, events;
  int (*cb)(int fd, int events, void *data);
  void *data;
} LooperFd;

typedef struct {
  Mutex m;              // the fd list
  int wake_r, wake_w;   // the wake pipe
  LooperFd fds[LOOPER_MAX_FDS];
  int nfds;
  Choreo *choreo;
} Looper;

static __thread Looper *t_looper;
static __thread int t_is_main;
static Looper *s_ui_looper;

static Looper *looper_new(void) {
  Looper *l = calloc(1, sizeof(Looper));
  int p[2] = { -1, -1 };
  if (pipe2_fake(p, 0x800 /* O_NONBLOCK */) < 0)
    log_printf("[ndk] ALooper: no pipe for its wake()");
  l->wake_r = p[0];
  l->wake_w = p[1];
  return l;
}

static Looper *ALooper_prepare(int opts) {
  (void)opts;
  if (!t_looper)
    t_looper = looper_new();
  return t_looper;
}

static Looper *ALooper_forThread(void) {
  if (!t_looper && t_is_main)
    return s_ui_looper;
  return t_looper;
}
static void ALooper_acquire(Looper *l) { (void)l; }
static void ALooper_release(Looper *l) { (void)l; }

static int choreo_run(Looper *l); // the due frame callbacks; 1 when any ran

#define EV_TO_POLL(e) (((e) & 1 ? 0x1 : 0) | ((e) & 2 ? 0x4 : 0))
#define POLL_TO_EV(r) (((r) & 0x1 ? 1 : 0) | ((r) & 0x4 ? 2 : 0) | ((r) & 0x8 ? 4 : 0) | ((r) & 0x10 ? 8 : 0))

typedef struct { int fd; short events, revents; } PollFd;

static int ALooper_pollOnce(int timeout_ms, int *ofd, int *oevents, void **odata) {
  if (ofd) *ofd = -1;
  if (oevents) *oevents = 0;
  if (odata) *odata = NULL;
  Looper *l = t_looper;
  if (!l) {
    if (timeout_ms > 0)
      svcSleepThread((s64)timeout_ms * 1000000ll);
    return ALOOPER_POLL_TIMEOUT;
  }
  if (choreo_run(l))
    return ALOOPER_POLL_CALLBACK;
  PollFd pf[LOOPER_MAX_FDS + 1];
  LooperFd copy[LOOPER_MAX_FDS];
  mutexLock(&l->m);
  const int n = l->nfds;
  memcpy(copy, l->fds, sizeof(LooperFd) * (size_t)n);
  mutexUnlock(&l->m);
  pf[0] = (PollFd){ l->wake_r, 0x1, 0 };
  for (int i = 0; i < n; i++)
    pf[i + 1] = (PollFd){ copy[i].fd, (short)EV_TO_POLL(copy[i].events), 0 };
  const int r = poll_fake(pf, (unsigned)n + 1, timeout_ms);
  if (r < 0)
    return ALOOPER_POLL_ERROR;
  int result = r == 0 ? ALOOPER_POLL_TIMEOUT : ALOOPER_POLL_WAKE;
  if (pf[0].revents) {
    char buf[64];
    long got;
    while (pipe_read_fake(l->wake_r, buf, sizeof buf, &got) && got > 0) {
    }
  }
  for (int i = 0; i < n; i++) {
    if (!pf[i + 1].revents)
      continue;
    const int ev = POLL_TO_EV(pf[i + 1].revents);
    if (copy[i].cb) {
      if (!copy[i].cb(copy[i].fd, ev, copy[i].data)) {
        mutexLock(&l->m);
        for (int k = 0; k < l->nfds; k++)
          if (l->fds[k].fd == copy[i].fd) {
            l->fds[k] = l->fds[--l->nfds];
            break;
          }
        mutexUnlock(&l->m);
      }
      result = ALOOPER_POLL_CALLBACK;
    } else if (result != ALOOPER_POLL_CALLBACK) {
      if (ofd) *ofd = copy[i].fd;
      if (oevents) *oevents = ev;
      if (odata) *odata = copy[i].data;
      return copy[i].ident;
    }
  }
  if (choreo_run(l))
    return ALOOPER_POLL_CALLBACK;
  return result;
}

static int ALooper_pollAll(int timeout_ms, int *ofd, int *oevents, void **odata) {
  for (;;) {
    const int r = ALooper_pollOnce(timeout_ms, ofd, oevents, odata);
    if (r != ALOOPER_POLL_CALLBACK)
      return r;
  }
}

static void ALooper_wake(Looper *l) {
  if (!l || l->wake_w < 0)
    return;
  long ret;
  const char one = 1;
  pipe_write_fake(l->wake_w, &one, 1, &ret); // full: a wake is pending anyway
}

static int ALooper_addFd(Looper *l, int fd, int ident, int events, void *cb, void *data) {
  if (!l)
    return -1;
  mutexLock(&l->m);
  int i = 0;
  while (i < l->nfds && l->fds[i].fd != fd)
    i++;
  if (i == l->nfds) {
    if (l->nfds == LOOPER_MAX_FDS) {
      mutexUnlock(&l->m);
      log_printf("[ndk] ALooper_addFd: too many fds");
      return -1;
    }
    l->nfds++;
  }
  l->fds[i] = (LooperFd){ fd, cb ? -2 : ident, events, cb, data };
  mutexUnlock(&l->m);
  ALooper_wake(l); // a poll under way picks the new fd up
  return 1;
}

static int ALooper_removeFd(Looper *l, int fd) {
  if (!l)
    return -1;
  int found = 0;
  mutexLock(&l->m);
  for (int i = 0; i < l->nfds; i++)
    if (l->fds[i].fd == fd) {
      l->fds[i] = l->fds[--l->nfds];
      found = 1;
      break;
    }
  mutexUnlock(&l->m);
  if (found)
    ALooper_wake(l);
  return found;
}

// --- the UI thread ------------------------------------------------------------------------

static volatile int s_ui_ready;

static void *ui_thread(void *arg) {
  (void)arg;
  s_ui_looper = ALooper_prepare(0);
  s_ui_ready = 1;
  for (;;)
    ALooper_pollOnce(-1, NULL, NULL, NULL);
  return NULL;
}

void ndk_ui_looper_start(void) {
  t_is_main = 1;
  pthread_t t;
  if (pthread_create_soloader(&t, NULL, ui_thread, NULL) != 0) {
    log_printf("[ndk] the UI looper thread did not start");
    return;
  }
  pthr_set_name(t, "UI looper");
  while (!s_ui_ready)
    svcSleepThread(100000);
}

// --- AChoreographer -------------------------------------------------------------------------
//
// One per looper thread. postFrameCallback* queue a callback; at the next
// display vsync (at or after its delay) the vsync thread marks the frame due
// and wakes the looper, whose pollOnce runs it with the vsync's time. Unity
// 6 drives its frame pacing with these (its frame waits for the callback).

#define CHOREO_MAX 32
#define VSYNC_PERIOD_NS 16666667ll

typedef struct {
  void *fn;
  void *data;
  int kind;         // 0: (long, void*)  1: (int64_t, void*)  2: vsync (FrameData*, void*)
  int64_t not_before;
} FrameCb;

typedef struct {
  int64_t frame_ns; // AChoreographerFrameCallbackData
  int64_t vsync_id;
} FrameData;

struct Choreo {
  Looper *l;
  FrameCb cbs[CHOREO_MAX];
  int n;
  int64_t due_ns;   // the vsync that made the frame due (0: none yet)
  int64_t vsync_id;
  Choreo *next;
};

static Choreo *s_choreos;
static Mutex s_choreo_lock;
static int s_choreo_listening;
static unsigned s_choreo_frames;

static void choreo_vsync(int64_t ns, void *arg) {
  (void)arg;
  mutexLock(&s_choreo_lock);
  for (Choreo *c = s_choreos; c; c = c->next) {
    if (!c->n)
      continue;
    c->due_ns = ns;
    c->vsync_id++;
    ALooper_wake(c->l);
  }
  mutexUnlock(&s_choreo_lock);
}

static int choreo_run(Looper *l) {
  Choreo *c = l->choreo;
  if (!c)
    return 0;
  FrameCb run[CHOREO_MAX];
  int nrun = 0;
  int64_t ns;
  FrameData fd;
  mutexLock(&s_choreo_lock);
  ns = c->due_ns;
  if (ns) {
    int k = 0;
    for (int i = 0; i < c->n; i++) {
      if (c->cbs[i].not_before <= ns)
        run[nrun++] = c->cbs[i];
      else
        c->cbs[k++] = c->cbs[i];
    }
    c->n = k;
    c->due_ns = 0;
  }
  fd.frame_ns = ns;
  fd.vsync_id = c->vsync_id;
  mutexUnlock(&s_choreo_lock);
  for (int i = 0; i < nrun; i++) {
    if (run[i].kind == 2)
      ((void (*)(const FrameData *, void *))run[i].fn)(&fd, run[i].data);
    else
      ((void (*)(int64_t, void *))run[i].fn)(ns, run[i].data);
  }
  if (nrun)
    s_choreo_frames++;
  return nrun > 0;
}

static Choreo *AChoreographer_getInstance(void) {
  Looper *l = ALooper_prepare(0);
  if (!l->choreo) {
    Choreo *c = calloc(1, sizeof(*c));
    c->l = l;
    mutexLock(&s_choreo_lock);
    c->next = s_choreos;
    s_choreos = c;
    mutexUnlock(&s_choreo_lock);
    l->choreo = c;
    if (!s_choreo_listening) {
      s_choreo_listening = 1;
      vsync_listen(choreo_vsync, NULL);
    }
    log_printf("[ndk] AChoreographer for thread %p", (void *)l);
  }
  return l->choreo;
}

static void choreo_post(Choreo *c, void *fn, void *data, int kind, int64_t delay_ms) {
  if (!c || !fn)
    return;
  mutexLock(&s_choreo_lock);
  if (c->n < CHOREO_MAX) {
    c->cbs[c->n].fn = fn;
    c->cbs[c->n].data = data;
    c->cbs[c->n].kind = kind;
    c->cbs[c->n].not_before = delay_ms > 0 ? android_monotonic_ns() + delay_ms * 1000000ll : 0;
    c->n++;
  } else {
    log_printf("[ndk] AChoreographer: too many frame callbacks queued");
  }
  mutexUnlock(&s_choreo_lock);
}

static void AChoreographer_postFrameCallback(Choreo *c, void *fn, void *data) { choreo_post(c, fn, data, 0, 0); }
static void AChoreographer_postFrameCallback64(Choreo *c, void *fn, void *data) { choreo_post(c, fn, data, 1, 0); }
static void AChoreographer_postFrameCallbackDelayed(Choreo *c, void *fn, void *data, long ms) {
  choreo_post(c, fn, data, 0, ms);
}
static void AChoreographer_postFrameCallbackDelayed64(Choreo *c, void *fn, void *data, uint32_t ms) {
  choreo_post(c, fn, data, 1, ms);
}
static int AChoreographer_postVsyncCallback(Choreo *c, void *fn, void *data) {
  choreo_post(c, fn, data, 2, 0);
  return 0;
}
static int AChoreographer_registerRefreshRateCallback(Choreo *c, void *fn, void *data) {
  (void)c;
  if (fn)
    ((void (*)(int64_t, void *))fn)(VSYNC_PERIOD_NS, data);
  return 0;
}
static void AChoreographer_unregisterRefreshRateCallback(Choreo *c, void *fn, void *data) {
  (void)c, (void)fn, (void)data;
}

static int64_t FrameData_getFrameTimeNanos(const FrameData *d) { return d->frame_ns; }
static size_t FrameData_getFrameTimelinesLength(const FrameData *d) { (void)d; return 1; }
static size_t FrameData_getPreferredFrameTimelineIndex(const FrameData *d) { (void)d; return 0; }
static int64_t FrameData_getFrameTimelineVsyncId(const FrameData *d, size_t i) { (void)i; return d->vsync_id; }
static int64_t FrameData_getFrameTimelineExpectedPresentationTimeNanos(const FrameData *d, size_t i) {
  return d->frame_ns + (int64_t)(i + 1) * VSYNC_PERIOD_NS;
}
static int64_t FrameData_getFrameTimelineDeadlineNanos(const FrameData *d, size_t i) {
  return d->frame_ns + (int64_t)(i + 1) * VSYNC_PERIOD_NS - 2000000;
}

unsigned ndk_choreographer_frames(void) { return s_choreo_frames; }

// --- ASensor: none ----------------------------------------------------------------------

static int s_sensor_manager;
static void *ASensorManager_getInstance(void) { return &s_sensor_manager; }
static void *ASensorManager_getInstanceForPackage(const char *p) { (void)p; return &s_sensor_manager; }
static const void *ASensorManager_getDefaultSensor(void *m, int type) { (void)m; (void)type; return NULL; }
static int ASensorManager_getSensorList(void *m, const void ***list) {
  (void)m;
  static const void *empty[1];
  if (list)
    *list = empty;
  return 0;
}
static int s_event_queue;
static void *ASensorManager_createEventQueue(void *m, void *looper, int ident, void *cb, void *data) {
  (void)m; (void)looper; (void)ident; (void)cb; (void)data;
  return &s_event_queue;
}
static int ASensorManager_destroyEventQueue(void *m, void *q) { (void)m; (void)q; return 0; }
static int ASensorEventQueue_enableSensor(void *q, const void *s) { (void)q; (void)s; return -1; }
static int ASensorEventQueue_disableSensor(void *q, const void *s) { (void)q; (void)s; return 0; }
static int ASensorEventQueue_setEventRate(void *q, const void *s, int us) { (void)q; (void)s; (void)us; return 0; }
static int ASensorEventQueue_hasEvents(void *q) { (void)q; return 0; }
static long ASensorEventQueue_getEvents(void *q, void *ev, size_t n) { (void)q; (void)ev; (void)n; return 0; }
static const char *ASensor_getName(const void *s) { (void)s; return ""; }
static const char *ASensor_getVendor(const void *s) { (void)s; return ""; }
static int ASensor_getType(const void *s) { (void)s; return 0; }
static float ASensor_getResolution(const void *s) { (void)s; return 0.f; }
static int ASensor_getMinDelay(const void *s) { (void)s; return 0; }

// --- AAssetManager over the APK -----------------------------------------------------------

typedef struct {
  const ApkEntry *e;
  uint8_t *buf;      // the whole content (deflated entries; getBuffer)
  uint64_t pos;
} Asset;

typedef struct {
  char prefix[512];  // "assets/<dir>/"
  int idx;
  char name[256];
} AssetDir;

static int s_asset_manager;

static void *AAssetManager_fromJava(void *env, void *obj) {
  (void)env; (void)obj;
  return &s_asset_manager;
}

static const ApkEntry *asset_entry(const char *name) {
  char full[600];
  while (name[0] == '/')
    name++;
  snprintf(full, sizeof full, "assets/%s", name);
  return apkfile_find(full);
}

static Asset *AAssetManager_open(void *mgr, const char *name, int mode) {
  (void)mgr; (void)mode;
  const ApkEntry *e = asset_entry(name);
  if (config.log_fs)
    log_printf("[ndk] AAsset open %s -> %s", name, e ? "ok" : "missing");
  if (!e)
    return NULL;
  Asset *a = calloc(1, sizeof(Asset));
  a->e = e;
  if (e->method != 0) {
    size_t n = 0;
    a->buf = apkfile_load(e, &n);
    if (!a->buf) {
      free(a);
      return NULL;
    }
  }
  return a;
}

static int AAsset_read(Asset *a, void *buf, size_t n) {
  if (!a)
    return -1;
  if (a->pos >= a->e->usize)
    return 0;
  if (n > a->e->usize - a->pos)
    n = (size_t)(a->e->usize - a->pos);
  if (a->buf) {
    memcpy(buf, a->buf + a->pos, n);
  } else {
    const long got = apkfile_read(a->e->data_off + a->pos, buf, n);
    if (got < 0)
      return -1;
    n = (size_t)got;
  }
  a->pos += n;
  return (int)n;
}

static int64_t AAsset_seek64(Asset *a, int64_t off, int whence) {
  if (!a)
    return -1;
  int64_t np;
  switch (whence) {
    case 0: np = off; break;
    case 1: np = (int64_t)a->pos + off; break;
    case 2: np = (int64_t)a->e->usize + off; break;
    default: return -1;
  }
  if (np < 0)
    return -1;
  a->pos = (uint64_t)np;
  return np;
}

static int64_t AAsset_getLength64(Asset *a) { return a ? (int64_t)a->e->usize : 0; }
static int64_t AAsset_getRemainingLength64(Asset *a) {
  return a && a->pos < a->e->usize ? (int64_t)(a->e->usize - a->pos) : 0;
}

static const void *AAsset_getBuffer(Asset *a) {
  if (!a)
    return NULL;
  if (!a->buf) {
    size_t n = 0;
    a->buf = apkfile_load(a->e, &n);
  }
  return a->buf;
}

// a stored asset can be handed out as a file: a virtual fd on its bytes
static int AAsset_openFileDescriptor64(Asset *a, int64_t *start, int64_t *len) {
  if (!a || a->e->method != 0)
    return -1;
  NabeFile f = { a->e->name, a->e->data_off, a->e->usize };
  const int fd = vfd_open(&f);
  if (fd < 0)
    return -1;
  if (start) *start = 0;
  if (len) *len = (int64_t)a->e->usize;
  return fd;
}

static int AAsset_isAllocated(Asset *a) { return a && a->buf != NULL; }

static void AAsset_close(Asset *a) {
  if (!a)
    return;
  free(a->buf);
  free(a);
}

static AssetDir *AAssetManager_openDir(void *mgr, const char *dir) {
  (void)mgr;
  AssetDir *d = calloc(1, sizeof(AssetDir));
  while (dir[0] == '/')
    dir++;
  if (dir[0])
    snprintf(d->prefix, sizeof d->prefix, "assets/%s/", dir);
  else
    snprintf(d->prefix, sizeof d->prefix, "assets/");
  return d;
}

static const char *AAssetDir_getNextFileName(AssetDir *d) {
  if (!d)
    return NULL;
  const size_t pl = strlen(d->prefix);
  while (d->idx < apkfile_count()) {
    const ApkEntry *e = apkfile_entry_at(d->idx++);
    if (!e || strncmp(e->name, d->prefix, pl) != 0)
      continue;
    const char *rest = e->name + pl;
    if (!rest[0] || strchr(rest, '/'))
      continue; // files directly in this directory only
    snprintf(d->name, sizeof d->name, "%s", rest);
    return d->name;
  }
  return NULL;
}

static void AAssetDir_rewind(AssetDir *d) { if (d) d->idx = 0; }
static void AAssetDir_close(AssetDir *d) { free(d); }

// --- media: unavailable -----------------------------------------------------------------------

#define AMEDIA_ERROR_UNKNOWN (-10000)
#define AMEDIA_ERROR_UNSUPPORTED (-10003)

static void *ret_null_fn(void) { return NULL; }
static int media_fail(void) { return AMEDIA_ERROR_UNSUPPORTED; }
static int media_ok(void) { return 0; }
static long media_try_again(void) { return -1; } // AMEDIACODEC_INFO_TRY_AGAIN_LATER

static const char *const k_mime = "mime", *const k_width = "width", *const k_height = "height",
                         *const k_duration = "durationUs", *const k_frame_rate = "frame-rate",
                         *const k_rotation = "rotation-degrees", *const k_color = "color-format",
                         *const k_stride = "stride", *const k_slice = "slice-height",
                         *const k_channels = "channel-count", *const k_rate = "sample-rate",
                         *const k_lang = "language", *const k_delay = "encoder-delay",
                         *const k_crange = "color-range", *const k_cstd = "color-standard";

DynLibFunction ndk_functions[] = {
  { "ANativeWindow_fromSurface", (uintptr_t)&ANativeWindow_fromSurface },
  { "ANativeWindow_toSurface", (uintptr_t)&ANativeWindow_toSurface },
  { "ANativeWindow_acquire", (uintptr_t)&ANativeWindow_acquire },
  { "ANativeWindow_release", (uintptr_t)&ANativeWindow_release },
  { "ANativeWindow_getWidth", (uintptr_t)&ANativeWindow_getWidth },
  { "ANativeWindow_getHeight", (uintptr_t)&ANativeWindow_getHeight },
  { "ANativeWindow_getFormat", (uintptr_t)&ANativeWindow_getFormat },
  { "ANativeWindow_setBuffersGeometry", (uintptr_t)&ANativeWindow_setBuffersGeometry },
  { "ANativeWindow_setFrameRate", (uintptr_t)&ANativeWindow_setFrameRate },

  { "ALooper_prepare", (uintptr_t)&ALooper_prepare },
  { "ALooper_forThread", (uintptr_t)&ALooper_forThread },
  { "ALooper_acquire", (uintptr_t)&ALooper_acquire },
  { "ALooper_release", (uintptr_t)&ALooper_release },
  { "ALooper_pollOnce", (uintptr_t)&ALooper_pollOnce },
  { "ALooper_pollAll", (uintptr_t)&ALooper_pollAll },
  { "ALooper_wake", (uintptr_t)&ALooper_wake },
  { "ALooper_addFd", (uintptr_t)&ALooper_addFd },
  { "ALooper_removeFd", (uintptr_t)&ALooper_removeFd },
  { "AChoreographer_getInstance", (uintptr_t)&AChoreographer_getInstance },
  { "AChoreographer_postFrameCallback", (uintptr_t)&AChoreographer_postFrameCallback },
  { "AChoreographer_postFrameCallback64", (uintptr_t)&AChoreographer_postFrameCallback64 },
  { "AChoreographer_postFrameCallbackDelayed", (uintptr_t)&AChoreographer_postFrameCallbackDelayed },
  { "AChoreographer_postFrameCallbackDelayed64", (uintptr_t)&AChoreographer_postFrameCallbackDelayed64 },
  { "AChoreographer_postVsyncCallback", (uintptr_t)&AChoreographer_postVsyncCallback },
  { "AChoreographer_registerRefreshRateCallback", (uintptr_t)&AChoreographer_registerRefreshRateCallback },
  { "AChoreographer_unregisterRefreshRateCallback", (uintptr_t)&AChoreographer_unregisterRefreshRateCallback },
  { "AChoreographerFrameCallbackData_getFrameTimeNanos", (uintptr_t)&FrameData_getFrameTimeNanos },
  { "AChoreographerFrameCallbackData_getFrameTimelinesLength", (uintptr_t)&FrameData_getFrameTimelinesLength },
  { "AChoreographerFrameCallbackData_getPreferredFrameTimelineIndex",
    (uintptr_t)&FrameData_getPreferredFrameTimelineIndex },
  { "AChoreographerFrameCallbackData_getFrameTimelineVsyncId", (uintptr_t)&FrameData_getFrameTimelineVsyncId },
  { "AChoreographerFrameCallbackData_getFrameTimelineExpectedPresentationTimeNanos",
    (uintptr_t)&FrameData_getFrameTimelineExpectedPresentationTimeNanos },
  { "AChoreographerFrameCallbackData_getFrameTimelineDeadlineNanos",
    (uintptr_t)&FrameData_getFrameTimelineDeadlineNanos },

  { "ASensorManager_getInstance", (uintptr_t)&ASensorManager_getInstance },
  { "ASensorManager_getInstanceForPackage", (uintptr_t)&ASensorManager_getInstanceForPackage },
  { "ASensorManager_getDefaultSensor", (uintptr_t)&ASensorManager_getDefaultSensor },
  { "ASensorManager_getSensorList", (uintptr_t)&ASensorManager_getSensorList },
  { "ASensorManager_createEventQueue", (uintptr_t)&ASensorManager_createEventQueue },
  { "ASensorManager_destroyEventQueue", (uintptr_t)&ASensorManager_destroyEventQueue },
  { "ASensorEventQueue_enableSensor", (uintptr_t)&ASensorEventQueue_enableSensor },
  { "ASensorEventQueue_disableSensor", (uintptr_t)&ASensorEventQueue_disableSensor },
  { "ASensorEventQueue_setEventRate", (uintptr_t)&ASensorEventQueue_setEventRate },
  { "ASensorEventQueue_hasEvents", (uintptr_t)&ASensorEventQueue_hasEvents },
  { "ASensorEventQueue_getEvents", (uintptr_t)&ASensorEventQueue_getEvents },
  { "ASensor_getName", (uintptr_t)&ASensor_getName },
  { "ASensor_getVendor", (uintptr_t)&ASensor_getVendor },
  { "ASensor_getType", (uintptr_t)&ASensor_getType },
  { "ASensor_getResolution", (uintptr_t)&ASensor_getResolution },
  { "ASensor_getMinDelay", (uintptr_t)&ASensor_getMinDelay },

  { "AAssetManager_fromJava", (uintptr_t)&AAssetManager_fromJava },
  { "AAssetManager_open", (uintptr_t)&AAssetManager_open },
  { "AAssetManager_openDir", (uintptr_t)&AAssetManager_openDir },
  { "AAsset_read", (uintptr_t)&AAsset_read },
  { "AAsset_seek", (uintptr_t)&AAsset_seek64 },
  { "AAsset_seek64", (uintptr_t)&AAsset_seek64 },
  { "AAsset_getLength", (uintptr_t)&AAsset_getLength64 },
  { "AAsset_getLength64", (uintptr_t)&AAsset_getLength64 },
  { "AAsset_getRemainingLength", (uintptr_t)&AAsset_getRemainingLength64 },
  { "AAsset_getRemainingLength64", (uintptr_t)&AAsset_getRemainingLength64 },
  { "AAsset_getBuffer", (uintptr_t)&AAsset_getBuffer },
  { "AAsset_openFileDescriptor", (uintptr_t)&AAsset_openFileDescriptor64 },
  { "AAsset_openFileDescriptor64", (uintptr_t)&AAsset_openFileDescriptor64 },
  { "AAsset_isAllocated", (uintptr_t)&AAsset_isAllocated },
  { "AAsset_close", (uintptr_t)&AAsset_close },
  { "AAssetDir_getNextFileName", (uintptr_t)&AAssetDir_getNextFileName },
  { "AAssetDir_rewind", (uintptr_t)&AAssetDir_rewind },
  { "AAssetDir_close", (uintptr_t)&AAssetDir_close },

  // media: every entry point fails
  { "AMediaExtractor_new", (uintptr_t)&ret_null_fn },
  { "AMediaExtractor_delete", (uintptr_t)&media_ok },
  { "AMediaExtractor_setDataSource", (uintptr_t)&media_fail },
  { "AMediaExtractor_setDataSourceFd", (uintptr_t)&media_fail },
  { "AMediaExtractor_setDataSourceCustom", (uintptr_t)&media_fail },
  { "AMediaExtractor_getTrackCount", (uintptr_t)&media_ok },
  { "AMediaExtractor_getTrackFormat", (uintptr_t)&ret_null_fn },
  { "AMediaExtractor_selectTrack", (uintptr_t)&media_fail },
  { "AMediaExtractor_readSampleData", (uintptr_t)&media_try_again },
  { "AMediaExtractor_getSampleTrackIndex", (uintptr_t)&media_try_again },
  { "AMediaExtractor_getSampleTime", (uintptr_t)&media_try_again },
  { "AMediaExtractor_advance", (uintptr_t)&media_ok },
  { "AMediaExtractor_seekTo", (uintptr_t)&media_fail },
  { "AMediaCodec_createDecoderByType", (uintptr_t)&ret_null_fn },
  { "AMediaCodec_configure", (uintptr_t)&media_fail },
  { "AMediaCodec_start", (uintptr_t)&media_fail },
  { "AMediaCodec_stop", (uintptr_t)&media_ok },
  { "AMediaCodec_flush", (uintptr_t)&media_ok },
  { "AMediaCodec_delete", (uintptr_t)&media_ok },
  { "AMediaCodec_dequeueInputBuffer", (uintptr_t)&media_try_again },
  { "AMediaCodec_dequeueOutputBuffer", (uintptr_t)&media_try_again },
  { "AMediaCodec_getInputBuffer", (uintptr_t)&ret_null_fn },
  { "AMediaCodec_getOutputBuffer", (uintptr_t)&ret_null_fn },
  { "AMediaCodec_getOutputFormat", (uintptr_t)&ret_null_fn },
  { "AMediaCodec_queueInputBuffer", (uintptr_t)&media_fail },
  { "AMediaCodec_releaseOutputBuffer", (uintptr_t)&media_ok },
  { "AMediaCodec_setOutputSurface", (uintptr_t)&media_fail },
  { "AMediaDataSource_new", (uintptr_t)&ret_null_fn },
  { "AMediaDataSource_delete", (uintptr_t)&media_ok },
  { "AMediaDataSource_setClose", (uintptr_t)&media_ok },
  { "AMediaDataSource_setGetSize", (uintptr_t)&media_ok },
  { "AMediaDataSource_setReadAt", (uintptr_t)&media_ok },
  { "AMediaDataSource_setUserdata", (uintptr_t)&media_ok },
  { "AMediaFormat_delete", (uintptr_t)&media_ok },
  { "AMediaFormat_getFloat", (uintptr_t)&media_ok },
  { "AMediaFormat_getInt32", (uintptr_t)&media_ok },
  { "AMediaFormat_getInt64", (uintptr_t)&media_ok },
  { "AMediaFormat_getString", (uintptr_t)&media_ok },
  { "AMediaFormat_setInt32", (uintptr_t)&media_ok },
  { "AImageReader_newWithUsage", (uintptr_t)&media_fail },
  { "AImageReader_delete", (uintptr_t)&media_ok },
  { "AImageReader_getWindow", (uintptr_t)&media_fail },
  { "AImageReader_acquireLatestImage", (uintptr_t)&media_fail },
  { "AImageReader_setImageListener", (uintptr_t)&media_fail },
  { "AImageReader_setBufferRemovedListener", (uintptr_t)&media_fail },
  { "AImage_delete", (uintptr_t)&media_ok },
  { "AImage_deleteAsync", (uintptr_t)&media_ok },
  { "AImage_getHardwareBuffer", (uintptr_t)&media_fail },
  { "AImage_getTimestamp", (uintptr_t)&media_fail },
  { "AImage_getWidth", (uintptr_t)&media_fail },
  { "AHardwareBuffer_describe", (uintptr_t)&media_ok },
  { "AHardwareBuffer_release", (uintptr_t)&media_ok },
  { "AMEDIAFORMAT_KEY_MIME", (uintptr_t)&k_mime },
  { "AMEDIAFORMAT_KEY_WIDTH", (uintptr_t)&k_width },
  { "AMEDIAFORMAT_KEY_HEIGHT", (uintptr_t)&k_height },
  { "AMEDIAFORMAT_KEY_DURATION", (uintptr_t)&k_duration },
  { "AMEDIAFORMAT_KEY_FRAME_RATE", (uintptr_t)&k_frame_rate },
  { "AMEDIAFORMAT_KEY_ROTATION", (uintptr_t)&k_rotation },
  { "AMEDIAFORMAT_KEY_COLOR_FORMAT", (uintptr_t)&k_color },
  { "AMEDIAFORMAT_KEY_COLOR_RANGE", (uintptr_t)&k_crange },
  { "AMEDIAFORMAT_KEY_COLOR_STANDARD", (uintptr_t)&k_cstd },
  { "AMEDIAFORMAT_KEY_STRIDE", (uintptr_t)&k_stride },
  { "AMEDIAFORMAT_KEY_SLICE_HEIGHT", (uintptr_t)&k_slice },
  { "AMEDIAFORMAT_KEY_CHANNEL_COUNT", (uintptr_t)&k_channels },
  { "AMEDIAFORMAT_KEY_SAMPLE_RATE", (uintptr_t)&k_rate },
  { "AMEDIAFORMAT_KEY_LANGUAGE", (uintptr_t)&k_lang },
  { "AMEDIAFORMAT_KEY_ENCODER_DELAY", (uintptr_t)&k_delay },
};

size_t ndk_numfunctions = sizeof(ndk_functions) / sizeof(*ndk_functions);
