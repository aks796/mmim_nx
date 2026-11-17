/* egl_shim.c -- the EGL calls Unity makes, around mesa's switch driver
 *
 * Unity owns EGL: it makes its context and window surface on the console's
 * default NWindow (ANativeWindow_fromSurface) and swaps by itself. These
 * wrappers sit in the linker's override table and log what it does to the
 * window -- surfaces made and destroyed, current surfaces, the swap interval,
 * the first swaps -- with the NWindow's state (buffers configured, size,
 * crop, swap interval), since mesa turns a failed present into a fatal error
 * without saying why. A new window surface first gets the window's size and
 * crop back (android_window_prepare).
 *
 * Mesa answers eglQuerySurface(EGL_WIDTH / EGL_HEIGHT) on a new window
 * surface with 0 (its size comes with the first buffer); Android answers the
 * window's size, and Unity sizes its screen from it (Screen.width/height,
 * currentResolution). A 0 for a surface on the window becomes the window's.
 *
 * The controller's pointer (gamepad.c) is drawn over each finished frame
 * before it is presented, with scissored clears only.
 *
 * config capture_at ("20,60"): the frame about to be presented at those
 * seconds after start is saved as internal/cap_<seconds>.bmp.
 *
 * Shaders: Unity's release player does not say when a shader fails (it
 * draws the object with its magenta error shader). A shader that does not
 * compile, or a program that does not link, is logged with mesa's info log,
 * and its sources go to internal/shader_fail/ (the first 40).
 * Unity's GLSL ES 1.00 shaders bring their own trunc() (it is not built in
 * to ES 1.00), which the console's mesa refuses as a redefinition of a
 * built-in (the item boxes were magenta): a shader refused for that is
 * compiled again with its own function renamed, calls included.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "saltynx.h"
#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <switch.h>

#include "config.h"
#include "gamepad.h"
#include "log.h"
#include "paths.h"
#include "so_util.h"

void android_window_prepare(void); // android_ndk.c

static EGLSurface s_window_surface; // the surface on the default NWindow

static u64 tid(void) {
  u64 id = 0;
  svcGetThreadId(&id, CUR_THREAD_HANDLE);
  return id;
}

static void nw_state(const char *when) {
  NWindow *nw = nwindowGetDefault();
  log_printf("[egl]   window %s: %ux%u fmt %u usage 0x%x, buffers configured 0x%llx requested "
             "0x%llx, slot %d, crop %d,%d-%d,%d, scaling %u, transform %u/%u, swap interval %u, "
             "%sconnected",
             when, nw->width, nw->height, nw->format, nw->usage,
             (unsigned long long)nw->slots_configured, (unsigned long long)nw->slots_requested,
             nw->cur_slot, nw->crop.left, nw->crop.top, nw->crop.right, nw->crop.bottom,
             nw->scaling_mode, nw->transform, nw->sticky_transform, nw->swap_interval,
             nw->is_connected ? "" : "not ");
}

static void attribs(char *out, size_t n, const EGLint *a) {
  size_t o = 0;
  out[0] = '\0';
  for (int i = 0; a && a[i] != EGL_NONE && i < 64 && o + 24 < n; i += 2)
    o += (size_t)snprintf(out + o, n - o, " 0x%x=%d", a[i], a[i + 1]);
}

static EGLSurface w_eglCreateWindowSurface(EGLDisplay dpy, EGLConfig cfg, EGLNativeWindowType win,
                                           const EGLint *attr) {
  char a[512];
  attribs(a, sizeof a, attr);
  const int ours = win == (EGLNativeWindowType)nwindowGetDefault();
  log_printf("[egl] eglCreateWindowSurface(window %p%s) on thread %llu, attribs:%s", (void *)win,
             ours ? " = the default NWindow" : "", (unsigned long long)tid(), a);
  if (ours)
    android_window_prepare();
  nw_state("before");
  EGLSurface s = eglCreateWindowSurface(dpy, cfg, win, attr);
  log_printf("[egl]   -> surface %p (error 0x%x)", s, s ? EGL_SUCCESS : eglGetError());
  if (ours && s)
    s_window_surface = s;
  nw_state("after");
  return s;
}

static EGLBoolean w_eglQuerySurface(EGLDisplay dpy, EGLSurface s, EGLint attr, EGLint *value) {
  const EGLBoolean r = eglQuerySurface(dpy, s, attr, value);
  if (s != s_window_surface || (attr != EGL_WIDTH && attr != EGL_HEIGHT) || !value || (r && *value > 0))
    return r;
  const NWindow *nw = nwindowGetDefault();
  const EGLint size = (EGLint)(attr == EGL_WIDTH ? nw->width : nw->height);
  static unsigned n;
  if (++n <= 4)
    log_printf("[egl] eglQuerySurface(%p, %s) -> %d from mesa, the window's %d instead", s,
               attr == EGL_WIDTH ? "EGL_WIDTH" : "EGL_HEIGHT", r ? *value : -1, size);
  *value = size;
  return EGL_TRUE;
}

static EGLSurface w_eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig cfg, const EGLint *attr) {
  char a[512];
  attribs(a, sizeof a, attr);
  EGLSurface s = eglCreatePbufferSurface(dpy, cfg, attr);
  log_printf("[egl] eglCreatePbufferSurface(%s) -> %p", a, s);
  return s;
}

static EGLBoolean w_eglDestroySurface(EGLDisplay dpy, EGLSurface s) {
  log_printf("[egl] eglDestroySurface(%p) on thread %llu", s, (unsigned long long)tid());
  const EGLBoolean r = eglDestroySurface(dpy, s);
  if (s == s_window_surface)
    s_window_surface = EGL_NO_SURFACE;
  nw_state("after");
  return r;
}

static EGLBoolean w_eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx) {
  static unsigned n;
  const EGLBoolean r = eglMakeCurrent(dpy, draw, read, ctx);
  if (++n <= 20)
    log_printf("[egl] eglMakeCurrent(draw %p, read %p, context %p) on thread %llu -> %d", draw, read,
               ctx, (unsigned long long)tid(), r);
  return r;
}

static EGLBoolean w_eglSwapInterval(EGLDisplay dpy, EGLint interval) {
  const EGLBoolean r = eglSwapInterval(dpy, interval);
  log_printf("[egl] eglSwapInterval(%d) on thread %llu -> %d", interval, (unsigned long long)tid(), r);
  return r;
}

static EGLBoolean w_eglSurfaceAttrib(EGLDisplay dpy, EGLSurface s, EGLint attr, EGLint value) {
  const EGLBoolean r = eglSurfaceAttrib(dpy, s, attr, value);
  log_printf("[egl] eglSurfaceAttrib(%p, 0x%x, %d) -> %d", s, attr, value, r);
  return r;
}

// --- capture_at: the back buffer as a BMP --------------------------------------------

static u64 s_t0;
static int s_caps[16], s_ncaps, s_next_cap;

// one framebuffer (0 = the window; others by GL name, for debugging what an
// offscreen pass holds: config capture_fbos) as cap_<sec>[_fb<n>].bmp
static void capture_fb(int sec, GLuint fb) {
  const NWindow *nw = nwindowGetDefault();
  const int w = (int)nw->width, h = (int)nw->height;
  uint8_t *px = w > 0 && h > 0 ? malloc((size_t)w * h * 4) : NULL;
  if (!px)
    return;
  // read with plain pack state, then put Unity's back
  GLint rfb = 0, pbo = 0, align = 4, row = 0;
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &rfb);
  glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pbo);
  glGetIntegerv(GL_PACK_ALIGNMENT, &align);
  glGetIntegerv(GL_PACK_ROW_LENGTH, &row);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fb);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);
  memset(px, 0x40, (size_t)w * h * 4); // grey where nothing was read
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
  const GLenum err = glGetError();
  glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)rfb);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)pbo);
  glPixelStorei(GL_PACK_ALIGNMENT, align);
  glPixelStorei(GL_PACK_ROW_LENGTH, row);

  char path[600];
  if (fb)
    snprintf(path, sizeof path, "%s/cap_%d_fb%u.bmp", path_internal(), sec, fb);
  else
    snprintf(path, sizeof path, "%s/cap_%d.bmp", path_internal(), sec);
  FILE *f = fopen(path, "wb");
  if (f) {
    // 24-bit, bottom-up rows: GL's order already
    const uint32_t stride = ((uint32_t)w * 3 + 3) & ~3u, size = 54 + stride * (uint32_t)h;
    uint8_t hd[54] = { 'B', 'M' };
    const uint32_t v[] = { size, 0, 54, 40, (uint32_t)w, (uint32_t)h };
    memcpy(hd + 2, &v[0], 4);
    memcpy(hd + 10, &v[2], 4);
    memcpy(hd + 14, &v[3], 4);
    memcpy(hd + 18, &v[4], 4);
    memcpy(hd + 22, &v[5], 4);
    hd[26] = 1;
    hd[28] = 24;
    fwrite(hd, 1, sizeof hd, f);
    uint8_t *line = calloc(1, stride);
    for (int y = 0; y < h; y++) {
      const uint8_t *src = px + (size_t)y * w * 4;
      for (int x = 0; x < w; x++) {
        line[x * 3] = src[x * 4 + 2];
        line[x * 3 + 1] = src[x * 4 + 1];
        line[x * 3 + 2] = src[x * 4];
      }
      fwrite(line, 1, stride, f);
    }
    free(line);
    fclose(f);
  }
  free(px);
  log_printf("[egl] captured framebuffer %u (%dx%d, GL error 0x%x) to %s%s", fb, w, h, err, path,
             f ? "" : " (could not write)");
}

static void capture(int sec) {
  capture_fb(sec, 0);
  for (const char *p = config.capture_fbos; *p;) {
    char *end;
    const long n = strtol(p, &end, 10);
    if (end == p) {
      p++;
      continue;
    }
    if (n > 0)
      capture_fb(sec, (GLuint)n);
    p = end;
  }
}

static void capture_due(void) {
  if (s_next_cap >= s_ncaps)
    return;
  const int now = (int)(armTicksToNs(armGetSystemTick() - s_t0) / 1000000000ull);
  if (now >= s_caps[s_next_cap])
    capture(s_caps[s_next_cap++]);
}

// --- the controller's pointer ----------------------------------------------------------------


// a rectangle of colour, by scissored clear (no shaders, buffers or
// textures of Unity's touched); y from the top
static void fill(int x, int y, int w, int h, int surface_h, float r, float g, float b) {
  glScissor(x, surface_h - y - h, w, h);
  glClearColor(r, g, b, 1);
  glClear(GL_COLOR_BUFFER_BIT);
}

// a frame t thick around (x0, y0)-(x1, y1)
static void frame(int x0, int y0, int x1, int y1, int t, int sh, float r, float g, float b) {
  const int w = x1 - x0, h = y1 - y0;
  if (w <= 2 * t || h <= 2 * t)
    return;
  fill(x0, y0, w, t, sh, r, g, b);
  fill(x0, y1 - t, w, t, sh, r, g, b);
  fill(x0, y0 + t, t, h - 2 * t, sh, r, g, b);
  fill(x1 - t, y0 + t, t, h - 2 * t, sh, r, g, b);
}

// over the finished frame: the controller's ring around the button it is on
// (a yellow band between dark edges, gently pulsing), or its pointer (a white
// ring with a dark edge and a dot in its centre); sized for the window
static void draw_pointer(void) {
  float px = 0, py = 0, rx0 = 0, ry0 = 0, rx1 = 0, ry1 = 0;
  const int ring = gamepad_ring(&rx0, &ry0, &rx1, &ry1);
  if (!ring && !gamepad_pointer(&px, &py))
    return;
  // the window's size (mesa answers 0 for a window surface's)
  const NWindow *nw = nwindowGetDefault();
  const int sh = (int)nw->height;
  if (nw->width <= 0 || sh <= 0)
    return;
  GLint fb = 0, box[4];
  GLfloat cc[4];
  GLboolean cm[4];
  const GLboolean sc = glIsEnabled(GL_SCISSOR_TEST);
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fb);
  glGetIntegerv(GL_SCISSOR_BOX, box);
  glGetFloatv(GL_COLOR_CLEAR_VALUE, cc);
  glGetBooleanv(GL_COLOR_WRITEMASK, cm);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glEnable(GL_SCISSOR_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  const int u = sh >= 1000 ? 3 : 2; // a "pixel" of the pointer and the ring
  if (ring) {
    const float pulse = 0.5f + 0.5f * sinf((float)armTicksToNs(armGetSystemTick()) / 1e9f * 5.0f);
    const int x0 = (int)rx0 - 2 * u, y0 = (int)ry0 - 2 * u, x1 = (int)rx1 + 2 * u, y1 = (int)ry1 + 2 * u;
    frame(x0 - u, y0 - u, x1 + u, y1 + u, u, sh, 0.08f, 0.08f, 0.08f);
    frame(x0, y0, x1, y1, 2 * u, sh, 1, 0.78f + 0.17f * pulse, 0.1f + 0.35f * pulse);
    frame(x0 + 2 * u, y0 + 2 * u, x1 - 2 * u, y1 - 2 * u, u, sh, 0.08f, 0.08f, 0.08f);
  } else {
    const int r = 7 * u, x = (int)px, y = (int)py;
    // the ring: dark edge, white band, dark inside edge; then the dot
    fill(x - r - u, y - r - u, 2 * (r + u), 2 * (r + u), sh, 0.1f, 0.1f, 0.1f);
    fill(x - r, y - r, 2 * r, 2 * r, sh, 1, 1, 1);
    fill(x - r + 2 * u, y - r + 2 * u, 2 * (r - 2 * u), 2 * (r - 2 * u), sh, 0.1f, 0.1f, 0.1f);
    fill(x - r + 3 * u, y - r + 3 * u, 2 * (r - 3 * u), 2 * (r - 3 * u), sh, 1, 0.85f, 0.2f);
    fill(x - u, y - u, 2 * u, 2 * u, sh, 0.1f, 0.1f, 0.1f);
  }
  glColorMask(cm[0], cm[1], cm[2], cm[3]);
  glClearColor(cc[0], cc[1], cc[2], cc[3]);
  glScissor(box[0], box[1], box[2], box[3]);
  if (!sc)
    glDisable(GL_SCISSOR_TEST);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)fb);
}

// time spent presenting (waiting on the GPU and the display), for [perf]
static u64 s_swap_ticks, s_swap_max;

void egl_swap_time(u64 *total, u64 *max) {
  *total = s_swap_ticks;
  *max = s_swap_max;
  s_swap_ticks = s_swap_max = 0;
}

// --- GPU time per frame (EXT_disjoint_timer_query) --------------------------------------
// A TIME_ELAPSED query spans each presented frame's work, from its first
// draw to the swap, on the thread that swaps (Unity's render thread: its
// context). Read back a few frames later, without waiting. The GPU's time
// for a frame near or over 16.7 ms means the GPU sets the frame rate.
// (Round 2 spanned swap to swap: that is the frame interval whatever the
// GPU does. Mesa's result here is in GPU timer ticks, not nanoseconds:
// ns = ticks * 625 / 384, as deko3d's dkGpuTicksToNs.)

#define GL_TIME_ELAPSED_EXT 0x88BF
#define GL_GPU_DISJOINT_EXT 0x8FBB
#define NQ 4
static GLuint s_q[NQ];
static int s_q_open = -1, s_q_next, s_q_ok = -1;
static int s_q_pending[NQ];
static u64 s_gpu_ns, s_gpu_max_ns;
static unsigned s_gpu_frames;
static void (*p_getq64)(GLuint, GLenum, GLuint64 *);
static int s_q_want;            // a query to begin at the next draw
static __thread int t_swapper;  // this thread swaps the window (it has the context)

static void gpu_timer_begin(void) {
  s_q_want = 0;
  if (s_q_pending[s_q_next])
    return; // still waiting for that one's result: this frame is not timed
  glBeginQuery(GL_TIME_ELAPSED_EXT, s_q[s_q_next]);
  s_q_open = s_q_next;
  s_q_next = (s_q_next + 1) % NQ;
}

static void gpu_timer_frame(void) {
  if (s_q_ok < 0) {
    const char *ext = (const char *)glGetString(GL_EXTENSIONS);
    p_getq64 = (void (*)(GLuint, GLenum, GLuint64 *))eglGetProcAddress("glGetQueryObjectui64vEXT");
    s_q_ok = ext && strstr(ext, "GL_EXT_disjoint_timer_query") && p_getq64;
    if (s_q_ok)
      glGenQueries(NQ, s_q);
    log_printf("[egl] GPU frame timing %s", s_q_ok ? "on (EXT_disjoint_timer_query)" : "not available");
  }
  if (!s_q_ok)
    return;
  if (s_q_open >= 0) {
    glEndQuery(GL_TIME_ELAPSED_EXT);
    s_q_pending[s_q_open] = 1;
    s_q_open = -1;
  }
  // results that are in
  for (int i = 0; i < NQ; i++) {
    if (!s_q_pending[i])
      continue;
    GLuint avail = 0;
    glGetQueryObjectuiv(s_q[i], GL_QUERY_RESULT_AVAILABLE, &avail);
    if (!avail)
      continue;
    GLuint64 ns = 0;
    p_getq64(s_q[i], GL_QUERY_RESULT, &ns);
    ns = ns * 625 / 384;
    s_q_pending[i] = 0;
    GLint disjoint = 0;
    glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
    if (disjoint || ns > 1000000000ull)
      continue;
    s_gpu_ns += ns;
    s_gpu_frames++;
    if (ns > s_gpu_max_ns)
      s_gpu_max_ns = ns;
  }
  t_swapper = 1;
  s_q_want = 1;
  while (glGetError() != GL_NO_ERROR) {
  }
}

void egl_gpu_time(double *avg_ms, double *max_ms) {
  *avg_ms = s_gpu_frames ? (double)s_gpu_ns / s_gpu_frames / 1e6 : 0;
  *max_ms = (double)s_gpu_max_ns / 1e6;
  s_gpu_ns = s_gpu_max_ns = 0;
  s_gpu_frames = 0;
}

static EGLBoolean w_eglSwapBuffers(EGLDisplay dpy, EGLSurface s) {
  static unsigned n;
  const unsigned k = ++n;
  if (s == s_window_surface && config.profile)
    gpu_timer_frame();
  if (s == s_window_surface)
    draw_pointer();
  capture_due();
  if (k <= 3) {
    log_printf("[egl] eglSwapBuffers(%p) #%u on thread %llu", s, k, (unsigned long long)tid());
    nw_state("before");
  }
  const u64 t0 = armGetSystemTick();
  const EGLBoolean r = eglSwapBuffers(dpy, s);
  const u64 dt = armGetSystemTick() - t0;
  if (s == s_window_surface) {
    // Status Monitor's FPS and RES (saltynx.c): Unity's window, then the screen
    const NWindow *nw = nwindowGetDefault();
    const int docked = appletGetOperationMode() == AppletOperationMode_Console;
    saltynx_present(nw->width, nw->height, docked ? 1920 : 1280, docked ? 1080 : 720);
  }
  s_swap_ticks += dt;
  if (dt > s_swap_max)
    s_swap_max = dt;
  if (k <= 3 || !r)
    log_printf("[egl]   swap #%u -> %d (error 0x%x)", k, r, r ? EGL_SUCCESS : eglGetError());
  return r;
}

// --- shader failures --------------------------------------------------------------------

// a shader's source into internal/shader_fail/<n>_<what>.glsl
static void save_source(GLuint sh, unsigned n, const char *what) {
  GLint len = 0;
  glGetShaderiv(sh, GL_SHADER_SOURCE_LENGTH, &len);
  if (len <= 0)
    return;
  char *src = malloc((size_t)len);
  glGetShaderSource(sh, len, NULL, src);
  char dir[600], path[700];
  snprintf(dir, sizeof dir, "%s/shader_fail", path_internal());
  mkdir(dir, 0777);
  snprintf(path, sizeof path, "%s/%02u_%s.glsl", dir, n, what);
  FILE *f = fopen(path, "w");
  if (f) {
    fputs(src, f);
    fclose(f);
  }
  free(src);
}

static int ident_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// src with every identifier `name` followed by "(" renamed to name_u (malloc'd)
static char *rename_function(const char *src, const char *name) {
  const size_t nlen = strlen(name);
  size_t cap = strlen(src) + 1, len = 0;
  for (const char *p = src; (p = strstr(p, name)); p += nlen)
    cap += 2;
  char *out = malloc(cap);
  for (const char *p = src; *p;) {
    const char *q = p + nlen;
    while (*q == ' ' || *q == '\t')
      q++;
    if (!strncmp(p, name, nlen) && (p == src || !ident_char(p[-1])) && !ident_char(p[nlen]) && *q == '(') {
      memcpy(out + len, name, nlen);
      len += nlen;
      out[len++] = '_';
      out[len++] = 'u';
      p += nlen;
    } else {
      out[len++] = *p++;
    }
  }
  out[len] = '\0';
  return out;
}

// a shader mesa refused for redefining a built-in: its function renamed,
// compiled again; 1 when it compiles now, 0 when it still does not (another
// one may be next), -1 when that was not the reason
static int retry_renamed(GLuint sh, const char *info) {
  static const char k_redef[] = "cannot redefine built-in function `";
  const char *at = strstr(info, k_redef);
  if (!at)
    return -1;
  char name[64];
  at += sizeof k_redef - 1;
  const size_t n = strcspn(at, "'");
  if (!n || n >= sizeof name)
    return -1;
  memcpy(name, at, n);
  name[n] = '\0';
  GLint len = 0;
  glGetShaderiv(sh, GL_SHADER_SOURCE_LENGTH, &len);
  if (len <= 0)
    return -1;
  char *src = malloc((size_t)len);
  glGetShaderSource(sh, len, NULL, src);
  char *fixed = rename_function(src, name);
  free(src);
  const GLchar *strs[1] = { fixed };
  glShaderSource(sh, 1, strs, NULL);
  free(fixed);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  static unsigned logged;
  if (logged++ < 3)
    log_printf("[gl] shader %u: its own %s() renamed (mesa holds it built in): %s", sh, name,
               ok ? "compiles" : "still fails");
  return ok;
}

// shader work, for [perf]: compiles and links, and their time
static unsigned s_compiles, s_links;
static u64 s_compile_ticks, s_link_ticks;

void egl_shader_time(unsigned *compiles, u64 *compile_ticks, unsigned *links, u64 *link_ticks) {
  *compiles = s_compiles;
  *compile_ticks = s_compile_ticks;
  *links = s_links;
  *link_ticks = s_link_ticks;
  s_compiles = s_links = 0;
  s_compile_ticks = s_link_ticks = 0;
}

// --- where the render thread waits for the GPU, and how much it draws ------------------
// Unity's GLES device waits on fences (glClientWaitSync) to keep a frame or
// two ahead of the GPU at most: time in them means the GPU is behind. Draw
// calls are counted for the [perf] line (mesa's cost is per draw).

static u64 s_gpu_wait_ticks, s_gpu_wait_max;
static unsigned s_gpu_waits, s_draws;

static void gpu_wait_done(u64 t0) {
  const u64 d = armGetSystemTick() - t0;
  __atomic_add_fetch(&s_gpu_wait_ticks, d, __ATOMIC_RELAXED);
  __atomic_add_fetch(&s_gpu_waits, 1, __ATOMIC_RELAXED);
  if (d > s_gpu_wait_max)
    s_gpu_wait_max = d;
}

static GLenum w_glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
  const u64 t0 = armGetSystemTick();
  const GLenum r = glClientWaitSync(sync, flags, timeout);
  if (timeout)
    gpu_wait_done(t0);
  return r;
}

static void w_glFinish(void) {
  const u64 t0 = armGetSystemTick();
  glFinish();
  gpu_wait_done(t0);
}

static void w_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
  s_draws++;
  if (s_q_want && t_swapper)
    gpu_timer_begin();
  glDrawElements(mode, count, type, indices);
}

static void w_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
  s_draws++;
  if (s_q_want && t_swapper)
    gpu_timer_begin();
  glDrawArrays(mode, first, count);
}

static void w_glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei n) {
  s_draws++;
  if (s_q_want && t_swapper)
    gpu_timer_begin();
  glDrawElementsInstanced(mode, count, type, indices, n);
}

void egl_gpu_stats(u64 *wait_ticks, u64 *wait_max, unsigned *waits, unsigned *draws) {
  *wait_ticks = s_gpu_wait_ticks;
  *wait_max = s_gpu_wait_max;
  *waits = s_gpu_waits;
  *draws = s_draws;
  s_gpu_wait_ticks = s_gpu_wait_max = 0;
  s_gpu_waits = s_draws = 0;
}

static void w_glCompileShader(GLuint sh) {
  const u64 t0 = armGetSystemTick();
  glCompileShader(sh);
  s_compiles++;
  s_compile_ticks += armGetSystemTick() - t0;
  GLint ok = 1;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  for (int tries = 0; !ok && tries < 4; tries++) {
    char info[1024] = "";
    glGetShaderInfoLog(sh, sizeof info, NULL, info);
    const int r = retry_renamed(sh, info);
    if (r < 0)
      break;
    ok = r;
  }
  if (ok)
    return;
  static unsigned n;
  const unsigned k = n++;
  if (k >= 40)
    return;
  char info[1024] = "";
  glGetShaderInfoLog(sh, sizeof info, NULL, info);
  GLint type = 0;
  glGetShaderiv(sh, GL_SHADER_TYPE, &type);
  log_printf("[gl] %s shader %u did not compile (#%u): %s", type == GL_VERTEX_SHADER ? "vertex" : "fragment", sh, k,
             info);
  save_source(sh, k, type == GL_VERTEX_SHADER ? "compile_vs" : "compile_fs");
}

static void w_glLinkProgram(GLuint prog) {
  const u64 t0 = armGetSystemTick();
  glLinkProgram(prog);
  s_links++;
  s_link_ticks += armGetSystemTick() - t0;
  GLint ok = 1;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (ok)
    return;
  static unsigned n;
  const unsigned k = n++;
  if (k >= 40)
    return;
  char info[1024] = "";
  glGetProgramInfoLog(prog, sizeof info, NULL, info);
  log_printf("[gl] program %u did not link (#%u): %s", prog, k, info);
  GLuint sh[4];
  GLsizei count = 0;
  glGetAttachedShaders(prog, 4, &count, sh);
  for (GLsizei i = 0; i < count; i++) {
    GLint type = 0;
    glGetShaderiv(sh[i], GL_SHADER_TYPE, &type);
    save_source(sh[i], k, type == GL_VERTEX_SHADER ? "link_vs" : "link_fs");
  }
}

// --- uniform buffer call log (mvl_nx, HW round 2: 3D models invisible on
// deko3d). The same lines from both renderers, to compare how Unity fills its
// constant buffers: binds of GL_UNIFORM_BUFFER, and writes to any buffer
// that was bound as one, or created at 688 bytes (UnityPerDraw). ---

#define UL_MAX 400
static int s_ul_lines;
static int s_n_ssbo;
static GLuint s_ubo_names[96];
static int s_ubo_count;
static GLuint s_bound[8]; // per target below
static int tgt_slot(GLenum t) {
  switch (t) {
    case GL_ARRAY_BUFFER: return 0;
    case GL_ELEMENT_ARRAY_BUFFER: return 1;
    case GL_COPY_READ_BUFFER: return 2;
    case GL_COPY_WRITE_BUFFER: return 3;
    case GL_UNIFORM_BUFFER: return 4;
    case GL_PIXEL_UNPACK_BUFFER: return 5;
    default: return 6;
  }
}
static int is_ubo(GLuint b) {
  for (int i = 0; i < s_ubo_count; i++)
    if (s_ubo_names[i] == b)
      return 1;
  return 0;
}
static void ubo_add(GLuint b) {
  if (b && !is_ubo(b) && s_ubo_count < 96)
    s_ubo_names[s_ubo_count++] = b;
}
#define UL(...) do { if (config.log_ubo == 1 && s_ul_lines < UL_MAX) { s_ul_lines++; log_printf("[ubo] " __VA_ARGS__); } } while (0)
// log_ubo N > 1: every call below during [N, N + 0.05) s of uptime, one
// frame of a match (W lines), instead of the first UL_MAX
static int in_window(void) {
  if (config.log_ubo <= 1)
    return 0;
  const double t = (double)armTicksToNs(armGetSystemTick() - s_t0) / 1e9;
  return t >= config.log_ubo && t < config.log_ubo + 0.05;
}
#define W(...) do { if (in_window()) log_printf("[ubo-w] " __VA_ARGS__); } while (0)
#define REAL(name) static __typeof__(&w_##name) r_##name; if (!r_##name) r_##name = (__typeof__(&w_##name))eglGetProcAddress(#name)

static void w_glBindBuffer(GLenum t, GLuint b) {
  REAL(glBindBuffer);
  if (t == GL_UNIFORM_BUFFER || t == GL_COPY_WRITE_BUFFER)
    W("BindBuffer %x %u", t, b);
  s_bound[tgt_slot(t)] = b;
  r_glBindBuffer(t, b);
}
static void w_glBindBufferBase(GLenum t, GLuint i, GLuint b) {
  REAL(glBindBufferBase);
  W("BindBufferBase %x point %u buf %u", t, i, b);
  if (t == GL_SHADER_STORAGE_BUFFER && b && s_n_ssbo++ < 12)
    UL("BindBufferBase SSBO point %u buf %u", i, b);
  if (t == GL_UNIFORM_BUFFER) {
    if (b && !is_ubo(b))
      UL("BindBufferBase point %u buf %u (first)", i, b);
    ubo_add(b);
    s_bound[4] = b;
  }
  r_glBindBufferBase(t, i, b);
}
static void w_glBindBufferRange(GLenum t, GLuint i, GLuint b, GLintptr o, GLsizeiptr n) {
  REAL(glBindBufferRange);
  W("BindBufferRange %x point %u buf %u off %ld size %ld", t, i, b, (long)o, (long)n);
  if (t == GL_SHADER_STORAGE_BUFFER && s_n_ssbo++ < 12)
    UL("BindBufferRange SSBO point %u buf %u off %ld size %ld", i, b, (long)o, (long)n);
  if (t == GL_UNIFORM_BUFFER) {
    ubo_add(b);
    s_bound[4] = b;
  }
  r_glBindBufferRange(t, i, b, o, n);
}
static void w_glBufferData(GLenum t, GLsizeiptr n, const void *d, GLenum u) {
  REAL(glBufferData);
  const GLuint b = s_bound[tgt_slot(t)];
  W("BufferData %x buf %u size %ld %s", t, b, (long)n, d ? "data" : "null");
  if (t == GL_UNIFORM_BUFFER || is_ubo(b) || n == 688)
    UL("BufferData target %x buf %u size %ld %s usage %x", t, b, (long)n, d ? "data" : "null", u);
  r_glBufferData(t, n, d, u);
}
static void w_glBufferSubData(GLenum t, GLintptr o, GLsizeiptr n, const void *d) {
  REAL(glBufferSubData);
  const GLuint b = s_bound[tgt_slot(t)];
  W("BufferSubData %x buf %u off %ld size %ld", t, b, (long)o, (long)n);

  r_glBufferSubData(t, o, n, d);
}
static void *w_glMapBufferRange(GLenum t, GLintptr o, GLsizeiptr n, GLbitfield a) {
  REAL(glMapBufferRange);
  const GLuint b = s_bound[tgt_slot(t)];
  void *p = r_glMapBufferRange(t, o, n, a);
  W("MapBufferRange %x buf %u off %ld size %ld access %x", t, b, (long)o, (long)n, a);

  return p;
}
static void w_glBufferStorageEXT(GLenum t, GLsizeiptr n, const void *d, GLbitfield f) {
  REAL(glBufferStorageEXT);
  UL("BufferStorageEXT target %x buf %u size %ld flags %x", t, s_bound[tgt_slot(t)], (long)n, f);
  r_glBufferStorageEXT(t, n, d, f);
}
static void w_glUniformBlockBinding(GLuint p, GLuint i, GLuint b) {
  REAL(glUniformBlockBinding);
  UL("UniformBlockBinding prog %u block %u -> point %u", p, i, b);
  r_glUniformBlockBinding(p, i, b);
}
static GLuint w_glGetUniformBlockIndex(GLuint p, const GLchar *n) {
  REAL(glGetUniformBlockIndex);
  const GLuint r = r_glGetUniformBlockIndex(p, n);
  UL("GetUniformBlockIndex prog %u %s -> %d", p, n ? n : "?", (int)r);
  return r;
}
static void w_glGetActiveUniformBlockiv(GLuint p, GLuint i, GLenum pn, GLint *v) {
  REAL(glGetActiveUniformBlockiv);
  r_glGetActiveUniformBlockiv(p, i, pn, v);
  UL("GetActiveUniformBlockiv prog %u block %u pname %x -> %d", p, i, pn, v ? v[0] : -1);
}
static void w_glGetActiveUniformBlockName(GLuint p, GLuint i, GLsizei bs, GLsizei *len, GLchar *n) {
  REAL(glGetActiveUniformBlockName);
  r_glGetActiveUniformBlockName(p, i, bs, len, n);
  UL("GetActiveUniformBlockName prog %u block %u -> %s", p, i, n ? n : "?");
}
static void w_glGetProgramiv(GLuint p, GLenum pn, GLint *v) {
  REAL(glGetProgramiv);
  r_glGetProgramiv(p, pn, v);
  if (pn == GL_ACTIVE_UNIFORM_BLOCKS || pn == GL_ACTIVE_UNIFORMS)
    UL("GetProgramiv prog %u %s -> %d", p, pn == GL_ACTIVE_UNIFORMS ? "ACTIVE_UNIFORMS" : "ACTIVE_UNIFORM_BLOCKS",
       v ? v[0] : -1);
}

static int s_n_getloc, s_n_dispatch, s_n_res, s_n_u1i;
static GLint w_glGetUniformLocation(GLuint p, const GLchar *n) {
  REAL(glGetUniformLocation);
  const GLint r = r_glGetUniformLocation(p, n);
  if (s_n_getloc++ < 12)
    UL("GetUniformLocation prog %u %s -> %d", p, n ? n : "?", r);
  return r;
}
static void w_glUniform1i(GLint l, GLint v) {
  REAL(glUniform1i);
  if (s_n_u1i++ < 16)
    UL("Uniform1i loc %d = %d", l, v);
  r_glUniform1i(l, v);
}
static void w_glDispatchCompute(GLuint x, GLuint y, GLuint z) {
  REAL(glDispatchCompute);
  if (s_n_dispatch++ < 8)
    UL("DispatchCompute %u %u %u", x, y, z);
  r_glDispatchCompute(x, y, z);
}
static void w_glGetProgramInterfaceiv(GLuint p, GLenum i, GLenum pn, GLint *v) {
  REAL(glGetProgramInterfaceiv);
  r_glGetProgramInterfaceiv(p, i, pn, v);
  if (s_n_res++ < 16)
    UL("GetProgramInterfaceiv prog %u iface %x pname %x -> %d", p, i, pn, v ? v[0] : -1);
}

static void w_glGetActiveUniform(GLuint p, GLuint i, GLsizei bs, GLsizei *len, GLint *size, GLenum *type, GLchar *n) {
  REAL(glGetActiveUniform);
  r_glGetActiveUniform(p, i, bs, len, size, type, n);
  UL("GetActiveUniform prog %u #%u -> %s size %d type %x", p, i, n ? n : "?", size ? *size : -1, type ? *type : 0);
}
static void w_glGetActiveUniformsiv(GLuint p, GLsizei c, const GLuint *idx, GLenum pn, GLint *v) {
  REAL(glGetActiveUniformsiv);
  r_glGetActiveUniformsiv(p, c, idx, pn, v);
  UL("GetActiveUniformsiv prog %u count %d pname %x first #%u -> %d", p, c, pn, idx ? idx[0] : 0, v ? v[0] : -1);
}
static void w_glGetUniformIndices(GLuint p, GLsizei c, const GLchar *const *names, GLuint *idx) {
  REAL(glGetUniformIndices);
  r_glGetUniformIndices(p, c, names, idx);
  UL("GetUniformIndices prog %u count %d first %s -> %d", p, c, names && names[0] ? names[0] : "?", idx ? (int)idx[0] : -1);
}

// the logging wrapper for a GL function (config log_ubo), or NULL: used by
// eglGetProcAddress above and the linker's GL resolver (main.c, dlsym)
void *egl_shim_gl_wrap(const char *name) {
  static const struct { const char *n; void *f; } ubo_wraps[] = {
    { "glBindBuffer", (void *)w_glBindBuffer }, { "glBindBufferBase", (void *)w_glBindBufferBase },
    { "glBindBufferRange", (void *)w_glBindBufferRange }, { "glBufferData", (void *)w_glBufferData },
    { "glBufferSubData", (void *)w_glBufferSubData }, { "glMapBufferRange", (void *)w_glMapBufferRange },
    { "glBufferStorageEXT", (void *)w_glBufferStorageEXT },
    { "glUniformBlockBinding", (void *)w_glUniformBlockBinding },
    { "glGetUniformBlockIndex", (void *)w_glGetUniformBlockIndex },
    { "glGetActiveUniformBlockiv", (void *)w_glGetActiveUniformBlockiv },
    { "glGetActiveUniformBlockName", (void *)w_glGetActiveUniformBlockName },
    { "glGetProgramiv", (void *)w_glGetProgramiv },
    { "glGetUniformLocation", (void *)w_glGetUniformLocation }, { "glUniform1i", (void *)w_glUniform1i },
    { "glDispatchCompute", (void *)w_glDispatchCompute },
    { "glGetProgramInterfaceiv", (void *)w_glGetProgramInterfaceiv },
    { "glGetActiveUniform", (void *)w_glGetActiveUniform },
    { "glGetActiveUniformsiv", (void *)w_glGetActiveUniformsiv },
    { "glGetUniformIndices", (void *)w_glGetUniformIndices },
  };
  for (size_t i = 0; name && config.log_ubo && i < sizeof ubo_wraps / sizeof ubo_wraps[0]; i++)
    if (!strcmp(name, ubo_wraps[i].n))
      return ubo_wraps[i].f;
  return NULL;
}

static __eglMustCastToProperFunctionPointerType w_eglGetProcAddress(const char *name) {
  if (name && !strcmp(name, "glCompileShader"))
    return (__eglMustCastToProperFunctionPointerType)w_glCompileShader;
  if (name && !strcmp(name, "glLinkProgram"))
    return (__eglMustCastToProperFunctionPointerType)w_glLinkProgram;
  if (name && !strcmp(name, "glClientWaitSync"))
    return (__eglMustCastToProperFunctionPointerType)w_glClientWaitSync;
  if (name && !strcmp(name, "glFinish"))
    return (__eglMustCastToProperFunctionPointerType)w_glFinish;
  if (name && !strcmp(name, "glDrawElements"))
    return (__eglMustCastToProperFunctionPointerType)w_glDrawElements;
  if (name && !strcmp(name, "glDrawArrays"))
    return (__eglMustCastToProperFunctionPointerType)w_glDrawArrays;
  if (name && !strcmp(name, "glDrawElementsInstanced"))
    return (__eglMustCastToProperFunctionPointerType)w_glDrawElementsInstanced;
  void *w = egl_shim_gl_wrap(name);
  if (w)
    return (__eglMustCastToProperFunctionPointerType)w;
  __eglMustCastToProperFunctionPointerType f = eglGetProcAddress(name);
  if (name && !strncmp(name, "egl", 3))
    log_printf("[egl] eglGetProcAddress(%s) -> %s", name, f ? "found" : "NULL");
  return f;
}

static DynLibFunction egl_overrides[] = {
  { "eglCreateWindowSurface", (uintptr_t)&w_eglCreateWindowSurface },
  { "eglCreatePbufferSurface", (uintptr_t)&w_eglCreatePbufferSurface },
  { "eglDestroySurface", (uintptr_t)&w_eglDestroySurface },
  { "eglQuerySurface", (uintptr_t)&w_eglQuerySurface },
  { "eglMakeCurrent", (uintptr_t)&w_eglMakeCurrent },
  { "eglSwapInterval", (uintptr_t)&w_eglSwapInterval },
  { "eglSurfaceAttrib", (uintptr_t)&w_eglSurfaceAttrib },
  { "eglSwapBuffers", (uintptr_t)&w_eglSwapBuffers },
  { "eglGetProcAddress", (uintptr_t)&w_eglGetProcAddress },
  { "glCompileShader", (uintptr_t)&w_glCompileShader },
  { "glLinkProgram", (uintptr_t)&w_glLinkProgram },
  { "glClientWaitSync", (uintptr_t)&w_glClientWaitSync },
  { "glFinish", (uintptr_t)&w_glFinish },
  { "glDrawElements", (uintptr_t)&w_glDrawElements },
  { "glDrawArrays", (uintptr_t)&w_glDrawArrays },
  { "glDrawElementsInstanced", (uintptr_t)&w_glDrawElementsInstanced },
};

void egl_shim_init(void) {
  s_t0 = armGetSystemTick();
  for (const char *p = config.capture_at; *p && s_ncaps < 16;) {
    char *end;
    const long v = strtol(p, &end, 10);
    if (end == p) {
      p++;
      continue;
    }
    if (v >= 0 && (!s_ncaps || v > s_caps[s_ncaps - 1]))
      s_caps[s_ncaps++] = (int)v;
    p = end;
  }
  so_set_overrides(egl_overrides, sizeof(egl_overrides) / sizeof(*egl_overrides));
}
