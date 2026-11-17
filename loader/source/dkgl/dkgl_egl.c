/* dkgl_egl.c -- EGL 1.4 on deko3d
 *
 * One display, a few RGBA8888 configs, contexts that share every object, a
 * window surface backed by a deko3d swapchain on the libnx NWindow, and
 * pbuffers for UE's loading context (which never draws to them).
 */
#include "dkgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "config.h"
#include "error.h"
#include "util.h"

DkCmdBuf dkgl_cmd(void);
void dkgl_context_defaults(DkglContext *ctx);
void dkgl_memory_report(char *out, size_t size);

#define DISPLAY ((EGLDisplay)(uintptr_t)0xd1d1)
#define CONTEXT_MAGIC 0x5458434bu
#define SURFACE_MAGIC 0x46525553u
#define SYNC_MAGIC 0x4c474553u

static __thread EGLint egl_error = EGL_SUCCESS;
static bool egl_initialized;

typedef struct {
  EGLint id;
  EGLint red, green, blue, alpha, depth, stencil, samples;
} EglConfigDesc;

static const EglConfigDesc configs[] = {
    {1, 8, 8, 8, 8, 24, 8, 0},
    {2, 8, 8, 8, 8, 16, 0, 0},
    {3, 8, 8, 8, 8, 0, 0, 0},
    {4, 8, 8, 8, 0, 24, 8, 0},
};
#define NUM_CONFIGS (sizeof(configs) / sizeof(configs[0]))

static const EglConfigDesc *config_of(EGLConfig config) {
  for (size_t i = 0; i < NUM_CONFIGS; i++)
    if ((EGLConfig)&configs[i] == config)
      return &configs[i];
  return NULL;
}

static EGLBoolean fail(EGLint error) {
  egl_error = error;
  return EGL_FALSE;
}

static void egl_ensure_init(void) {
  DKGL_LOCK();
  if (!egl_initialized) {
    dkgl_device_init();
    dkgl_textures_init();
    dkgl_shaders_init();
    dkgl_capture_check(); /* before the first shader compiles */
    egl_initialized = true;
  }
  DKGL_UNLOCK();
}

EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id) {
  (void)display_id;
  return DISPLAY;
}

EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor) {
  if (dpy != DISPLAY)
    return fail(EGL_BAD_DISPLAY);
  egl_ensure_init();
  if (major)
    *major = 1;
  if (minor)
    *minor = 4;
  egl_error = EGL_SUCCESS;
  return EGL_TRUE;
}

EGLBoolean eglTerminate(EGLDisplay dpy) {
  (void)dpy;
  return EGL_TRUE;
}

EGLBoolean eglBindAPI(EGLenum api) {
  return api == EGL_OPENGL_ES_API ? EGL_TRUE : fail(EGL_BAD_PARAMETER);
}

EGLenum eglQueryAPI(void) { return EGL_OPENGL_ES_API; }

EGLint eglGetError(void) {
  EGLint e = egl_error;
  egl_error = EGL_SUCCESS;
  return e;
}

const char *eglQueryString(EGLDisplay dpy, EGLint name) {
  (void)dpy;
  switch (name) {
  case EGL_VENDOR:
    return "Mesa Project";
  case EGL_VERSION:
    return "1.4";
  case EGL_CLIENT_APIS:
    return "OpenGL_ES";
  case EGL_EXTENSIONS:
    /* Mesa's, which Unity read before choosing its context */
    return "EGL_KHR_config_attribs EGL_KHR_create_context "
           "EGL_KHR_get_all_proc_addresses EGL_KHR_surfaceless_context ";
  default:
    egl_error = EGL_BAD_PARAMETER;
    return NULL;
  }
}

static EGLint config_attrib(const EglConfigDesc *c, EGLint attribute) {
  switch (attribute) {
  case EGL_CONFIG_ID:
    return c->id;
  case EGL_RED_SIZE:
    return c->red;
  case EGL_GREEN_SIZE:
    return c->green;
  case EGL_BLUE_SIZE:
    return c->blue;
  case EGL_ALPHA_SIZE:
    return c->alpha;
  case EGL_BUFFER_SIZE:
    return c->red + c->green + c->blue + c->alpha;
  case EGL_DEPTH_SIZE:
    return c->depth;
  case EGL_STENCIL_SIZE:
    return c->stencil;
  case EGL_SAMPLES:
    return c->samples;
  case EGL_SAMPLE_BUFFERS:
    return c->samples ? 1 : 0;
  case EGL_SURFACE_TYPE:
    return EGL_WINDOW_BIT | EGL_PBUFFER_BIT;
  case EGL_RENDERABLE_TYPE:
  case EGL_CONFORMANT:
    return EGL_OPENGL_ES2_BIT | 0x40; /* EGL_OPENGL_ES3_BIT_KHR */
  case EGL_COLOR_BUFFER_TYPE:
    return EGL_RGB_BUFFER;
  case EGL_CONFIG_CAVEAT:
    return EGL_NONE;
  case EGL_NATIVE_RENDERABLE:
    return EGL_TRUE;
  case EGL_NATIVE_VISUAL_ID:
    return 1; /* HAL_PIXEL_FORMAT_RGBA_8888 */
  case EGL_NATIVE_VISUAL_TYPE:
    return EGL_NONE;
  case EGL_LEVEL:
  case EGL_TRANSPARENT_RED_VALUE:
  case EGL_TRANSPARENT_GREEN_VALUE:
  case EGL_TRANSPARENT_BLUE_VALUE:
  case EGL_LUMINANCE_SIZE:
  case EGL_ALPHA_MASK_SIZE:
  case EGL_MIN_SWAP_INTERVAL:
    return 0;
  case EGL_MAX_SWAP_INTERVAL:
    return 4;
  case EGL_TRANSPARENT_TYPE:
    return EGL_NONE;
  case EGL_BIND_TO_TEXTURE_RGB:
  case EGL_BIND_TO_TEXTURE_RGBA:
    return EGL_FALSE;
  case EGL_MAX_PBUFFER_WIDTH:
  case EGL_MAX_PBUFFER_HEIGHT:
    return 16384;
  case EGL_MAX_PBUFFER_PIXELS:
    return 16384 * 16384;
  default:
    return 0;
  }
}

EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config,
                              EGLint attribute, EGLint *value) {
  (void)dpy;
  const EglConfigDesc *c = config_of(config);
  if (!c)
    return fail(EGL_BAD_CONFIG);
  *value = config_attrib(c, attribute);
  return EGL_TRUE;
}

EGLBoolean eglGetConfigs(EGLDisplay dpy, EGLConfig *out, EGLint size,
                         EGLint *num) {
  (void)dpy;
  EGLint n = 0;
  for (size_t i = 0; i < NUM_CONFIGS; i++) {
    if (out && n < size)
      out[n] = (EGLConfig)&configs[i];
    n++;
  }
  if (num)
    *num = out ? (n < size ? n : size) : n;
  return EGL_TRUE;
}

EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
                           EGLConfig *out, EGLint size, EGLint *num) {
  (void)dpy;
  EGLint n = 0;
  for (size_t i = 0; i < NUM_CONFIGS; i++) {
    const EglConfigDesc *c = &configs[i];
    bool ok = true;
    for (int a = 0; attrib_list && attrib_list[a] != EGL_NONE && a < 64; a += 2) {
      const EGLint key = attrib_list[a], want = attrib_list[a + 1];
      if (want == EGL_DONT_CARE)
        continue;
      switch (key) {
      case EGL_RED_SIZE:
      case EGL_GREEN_SIZE:
      case EGL_BLUE_SIZE:
      case EGL_ALPHA_SIZE:
      case EGL_DEPTH_SIZE:
      case EGL_STENCIL_SIZE:
      case EGL_BUFFER_SIZE:
        if (config_attrib(c, key) < want)
          ok = false;
        break;
      case EGL_SAMPLES:
      case EGL_SAMPLE_BUFFERS:
        if (want > 0)
          ok = false;
        break;
      case EGL_CONFIG_ID:
        if (c->id != want)
          ok = false;
        break;
      default:
        break;
      }
    }
    if (!ok)
      continue;
    if (out && n < size)
      out[n] = (EGLConfig)c;
    n++;
  }
  if (num)
    *num = out ? (n < size ? n : size) : n;
  debugPrintf("dkgl: eglChooseConfig -> %d configs\n", n);
  return EGL_TRUE;
}

/* ------------------------------------------------------------ contexts */

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config,
                            EGLContext share_context,
                            const EGLint *attrib_list) {
  (void)dpy;
  (void)config;
  egl_ensure_init();
  DkglContext *ctx = calloc(1, sizeof(*ctx));
  if (!ctx) {
    egl_error = EGL_BAD_ALLOC;
    return EGL_NO_CONTEXT;
  }
  ctx->magic = CONTEXT_MAGIC;
  ctx->share = share_context;
  ctx->client_version = 3;
  for (int a = 0; attrib_list && attrib_list[a] != EGL_NONE && a < 64; a += 2)
    if (attrib_list[a] == EGL_CONTEXT_CLIENT_VERSION)
      ctx->client_version = attrib_list[a + 1];
  dkgl_context_defaults(ctx);
  debugPrintf("dkgl: eglCreateContext share=%p version=%d -> %p\n",
              share_context, ctx->client_version, ctx);
  return ctx;
}

static DkglContext *context_of(EGLContext c) {
  DkglContext *ctx = c;
  return ctx && ctx->magic == CONTEXT_MAGIC ? ctx : NULL;
}

EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext context) {
  (void)dpy;
  DkglContext *ctx = context_of(context);
  if (!ctx)
    return fail(EGL_BAD_CONTEXT);
  DKGL_LOCK();
  if (dkgl_tls_ctx == ctx)
    dkgl_tls_ctx = NULL;
  ctx->magic = 0;
  dkgl_draw_invalidate_all();
  DKGL_UNLOCK();
  free(ctx);
  return EGL_TRUE;
}

EGLContext eglGetCurrentContext(void) {
  return dkgl_tls_ctx ? (EGLContext)dkgl_tls_ctx : EGL_NO_CONTEXT;
}

EGLDisplay eglGetCurrentDisplay(void) {
  return dkgl_tls_ctx ? DISPLAY : EGL_NO_DISPLAY;
}

/* ------------------------------------------------------------ surfaces */

static DkglSurface *surface_of(EGLSurface s) {
  DkglSurface *surf = s;
  return surf && surf->magic == SURFACE_MAGIC ? surf : NULL;
}

static DkglSurface *window_surfaces[4];

static bool surface_depth(DkglSurface *surf) {
  DkImageLayoutMaker maker;
  dkImageLayoutMakerDefaults(&maker, dkg.device);
  maker.flags = DkImageFlags_UsageRender;
  maker.format = DkImageFormat_Z24S8;
  maker.dimensions[0] = surf->width;
  maker.dimensions[1] = surf->height;
  dkImageLayoutInitialize(&surf->depth_layout, &maker);
  if (!dkgl_alloc(DKGL_HEAP_IMAGE, (uint32_t)dkImageLayoutGetSize(&surf->depth_layout),
                  dkImageLayoutGetAlignment(&surf->depth_layout), &surf->depth_mem))
    return false;
  dkImageInitialize(&surf->depth, &surf->depth_layout, surf->depth_mem.block,
                    surf->depth_mem.offset);
  surf->has_depth = true;
  return true;
}

EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                  EGLNativeWindowType win,
                                  const EGLint *attrib_list) {
  (void)dpy;
  (void)attrib_list;
  /* A depth buffer only for a config with one: Unity's ES 2.0 renderer
   * takes a config without, draws into its own targets and blits to the
   * window with depth testing left on, which with no depth buffer always
   * passes (with a never-cleared one, it failed at random: black frames). */
  const EglConfigDesc *cfg = config_of(config);
  const bool want_depth = !cfg || cfg->depth || cfg->stencil;
  egl_ensure_init();
  NWindow *nwin = (NWindow *)win;
  if (!nwin)
    nwin = nwindowGetDefault();
  DKGL_LOCK();
  for (int i = 0; i < 4; i++) {
    if (window_surfaces[i] && window_surfaces[i]->nwindow == nwin) {
      DKGL_UNLOCK();
      debugPrintf("dkgl: eglCreateWindowSurface reuses %p\n", window_surfaces[i]);
      return window_surfaces[i];
    }
  }
  DkglSurface *surf = calloc(1, sizeof(*surf));
  if (!surf) {
    DKGL_UNLOCK();
    egl_error = EGL_BAD_ALLOC;
    return EGL_NO_SURFACE;
  }
  surf->magic = SURFACE_MAGIC;
  surf->window = true;
  surf->nwindow = nwin;
  surf->acquired = -1;
  surf->swap_interval = 1;
  u32 w = 0, h = 0;
  nwindowGetDimensions(nwin, &w, &h);
  if (!w || !h) {
    w = (u32)screen_width;
    h = (u32)screen_height;
    nwindowSetDimensions(nwin, w, h);
  }
  surf->width = w;
  surf->height = h;

  DkImageLayoutMaker maker;
  dkImageLayoutMakerDefaults(&maker, dkg.device);
  maker.flags = DkImageFlags_UsageRender | DkImageFlags_UsagePresent |
                DkImageFlags_Usage2DEngine;
  maker.format = DkImageFormat_RGBA8_Unorm;
  maker.dimensions[0] = w;
  maker.dimensions[1] = h;
  dkImageLayoutInitialize(&surf->image_layout, &maker);
  surf->num_images = 3;
  const DkImage *images[3];
  for (int i = 0; i < surf->num_images; i++) {
    if (!dkgl_alloc(DKGL_HEAP_IMAGE,
                    (uint32_t)dkImageLayoutGetSize(&surf->image_layout),
                    dkImageLayoutGetAlignment(&surf->image_layout),
                    &surf->image_mem[i]))
      fatal_error("dkgl: swapchain image allocation failed");
    dkImageInitialize(&surf->images[i], &surf->image_layout,
                      surf->image_mem[i].block, surf->image_mem[i].offset);
    images[i] = &surf->images[i];
  }
  DkSwapchainMaker sw;
  dkSwapchainMakerDefaults(&sw, dkg.device, nwin, images, (uint32_t)surf->num_images);
  surf->swapchain = dkSwapchainCreate(&sw);
  if (!surf->swapchain)
    fatal_error("dkgl: swapchain creation failed");
  dkSwapchainSetSwapInterval(surf->swapchain, 1);
  if (want_depth && !surface_depth(surf))
    fatal_error("dkgl: depth buffer allocation failed");
  for (int i = 0; i < 4; i++)
    if (!window_surfaces[i]) {
      window_surfaces[i] = surf;
      break;
    }
  DKGL_UNLOCK();
  debugPrintf("dkgl: eglCreateWindowSurface %ux%u swapchain=%d images, config %d (%s) -> %p\n",
              w, h, surf->num_images, cfg ? cfg->id : 0,
              want_depth ? "depth/stencil" : "no depth", surf);
  return surf;
}

EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config,
                                   const EGLint *attrib_list) {
  (void)dpy;
  (void)config;
  egl_ensure_init();
  DkglSurface *surf = calloc(1, sizeof(*surf));
  if (!surf) {
    egl_error = EGL_BAD_ALLOC;
    return EGL_NO_SURFACE;
  }
  surf->magic = SURFACE_MAGIC;
  surf->width = surf->height = 1;
  surf->acquired = -1;
  for (int a = 0; attrib_list && attrib_list[a] != EGL_NONE && a < 64; a += 2) {
    if (attrib_list[a] == EGL_WIDTH && attrib_list[a + 1] > 0)
      surf->width = (uint32_t)attrib_list[a + 1];
    if (attrib_list[a] == EGL_HEIGHT && attrib_list[a + 1] > 0)
      surf->height = (uint32_t)attrib_list[a + 1];
  }
  debugPrintf("dkgl: eglCreatePbufferSurface %ux%u -> %p\n", surf->width,
              surf->height, surf);
  return surf;
}

EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface) {
  (void)dpy;
  DkglSurface *surf = surface_of(surface);
  if (!surf)
    return fail(EGL_BAD_SURFACE);
  DKGL_LOCK();
  if (surf->window) {
    dkgl_finish();
    dkSwapchainDestroy(surf->swapchain);
    for (int i = 0; i < surf->num_images; i++)
      dkgl_free_deferred(&surf->image_mem[i]);
    for (int i = 0; i < 4; i++)
      if (window_surfaces[i] == surf)
        window_surfaces[i] = NULL;
  }
  if (surf->has_depth)
    dkgl_free_deferred(&surf->depth_mem);
  if (surf->has_color)
    dkgl_free_deferred(&surf->color_mem);
  surf->magic = 0;
  dkgl_fbo_forget_targets();
  DKGL_UNLOCK();
  free(surf);
  debugPrintf("dkgl: eglDestroySurface %p\n", surface);
  return EGL_TRUE;
}

EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute,
                           EGLint *value) {
  (void)dpy;
  DkglSurface *surf = surface_of(surface);
  if (!surf || !value)
    return fail(EGL_BAD_SURFACE);
  switch (attribute) {
  case EGL_WIDTH:
    *value = (EGLint)surf->width;
    break;
  case EGL_HEIGHT:
    *value = (EGLint)surf->height;
    break;
  case EGL_CONFIG_ID:
    *value = 1;
    break;
  case EGL_RENDER_BUFFER:
    *value = EGL_BACK_BUFFER;
    break;
  case EGL_SWAP_BEHAVIOR:
    *value = EGL_BUFFER_DESTROYED;
    break;
  case EGL_HORIZONTAL_RESOLUTION:
  case EGL_VERTICAL_RESOLUTION:
  case EGL_PIXEL_ASPECT_RATIO:
    *value = EGL_UNKNOWN;
    break;
  default:
    *value = 0;
    break;
  }
  return EGL_TRUE;
}

EGLBoolean eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface,
                            EGLint attribute, EGLint value) {
  (void)dpy;
  (void)surface;
  (void)attribute;
  (void)value;
  return EGL_TRUE;
}

EGLSurface eglGetCurrentSurface(EGLint readdraw) {
  DkglContext *ctx = dkgl_tls_ctx;
  if (!ctx)
    return EGL_NO_SURFACE;
  return readdraw == EGL_READ ? (EGLSurface)ctx->read_surface
                              : (EGLSurface)ctx->draw_surface;
}

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read,
                          EGLContext context) {
  (void)dpy;
  if (context == EGL_NO_CONTEXT) {
    DKGL_LOCK();
    /* hand recorded work to the GPU before the thread lets go */
    if (dkg.initialized)
      dkgl_flush();
    dkgl_tls_ctx = NULL;
    DKGL_UNLOCK();
    return EGL_TRUE;
  }
  DkglContext *ctx = context_of(context);
  if (!ctx)
    return fail(EGL_BAD_CONTEXT);
  DkglSurface *ds = surface_of(draw);
  DkglSurface *rs = surface_of(read);
  DKGL_LOCK();
  const bool first = !ctx->draw_surface && ds;
  ctx->draw_surface = ds;
  ctx->read_surface = rs ? rs : ds;
  if (first) {
    ctx->viewport[2] = ctx->scissor[2] = (GLint)ds->width;
    ctx->viewport[3] = ctx->scissor[3] = (GLint)ds->height;
  }
  ctx->dirty = DKGL_DIRTY_ALL;
  dkgl_tls_ctx = ctx;
  DKGL_UNLOCK();
  debugPrintf("dkgl: eglMakeCurrent ctx=%p draw=%p%s\n", ctx, ds,
              ds && !ds->window ? " (pbuffer)" : "");
  return EGL_TRUE;
}

EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval) {
  (void)dpy;
  DkglContext *ctx = dkgl_tls_ctx;
  if (!ctx || !ctx->draw_surface || !ctx->draw_surface->window)
    return EGL_TRUE;
  DKGL_LOCK();
  if (interval < 0)
    interval = 0;
  ctx->draw_surface->swap_interval = interval;
  dkSwapchainSetSwapInterval(ctx->draw_surface->swapchain, (uint32_t)interval);
  DKGL_UNLOCK();
  return EGL_TRUE;
}

/* ---------------------------------------------------------------- swap */

/* Debug frame dumps: when dkgl_capture.txt sits in the runtime folder, the
 * back buffer of every Nth frame (N from the file; 0 or empty = only on
 * L3+R3; at most 16 dumps) is written next to it as dkgl_frame_NNNNN.bmp.
 * Lets a run in an emulator or on hardware show what was drawn without a
 * screen grab. */
static int capture_every = -1; /* -1 unchecked, 0 off */
static int capture_count;
/* The frame before a dump is also traced to debug.log (one line per draw,
 * shader sources into dkgl_trace/) when the previous frame looked like a
 * match: at most 3 traced frames. */
/* mvl_nx: Mario vs. Luigi (and MMIM) render in linear colour space */
bool dkgl_window_srgb = true;
bool dkgl_trace_enabled;
bool dkgl_trace_on;
static int trace_count;
static uint32_t trace_last_draws;
/* L3+R3 (both sticks clicked): trace, GPU-profile and dump the next frame,
 * for an artefact that is on screen right now. Works without the file;
 * shader sources are only kept when dkgl_capture.txt was there at boot. */
static int combo_count;
static bool combo_capture;
static uint32_t last_auto_profile;

/* A/B mode (dkgl_ab.txt in the runtime folder): during matches the sampler
 * filtering policy cycles through ab_settings every 1200 match frames, so
 * one session measures what each costs on the GPU. Every change is logged
 * and every stats line names the active policy (tex=aNtN). */
static const struct {
  int aniso, trilinear;
} ab_settings[] = {{8, 1}, {4, 1}, {2, 1}, {8, 0}, {4, 0}, {2, 0}};
static int ab_mode = -1;
static unsigned ab_index;
static uint32_t ab_frames;

static void ab_step(uint32_t frame_draws) {
  if (ab_mode < 0) {
    FILE *f = fopen(dkgl_path("dkgl_ab.txt"), "rb");
    ab_mode = f != NULL;
    if (f)
      fclose(f);
    if (ab_mode) {
      dkgl_set_sampler_policy(ab_settings[0].aniso, ab_settings[0].trilinear);
      debugPrintf("dkgl: A/B mode: sampler policy cycles every 1200 match "
                  "frames; ab setting 0 aniso=%d trilinear=%d\n",
                  ab_settings[0].aniso, ab_settings[0].trilinear);
    }
  }
  if (!ab_mode || frame_draws < 60 || ++ab_frames < 1200)
    return;
  ab_frames = 0;
  ab_index = (ab_index + 1) % (sizeof(ab_settings) / sizeof(ab_settings[0]));
  dkgl_set_sampler_policy(ab_settings[ab_index].aniso,
                          ab_settings[ab_index].trilinear);
  debugPrintf("dkgl: ab setting %u aniso=%d trilinear=%d\n", ab_index,
              ab_settings[ab_index].aniso, ab_settings[ab_index].trilinear);
}

static bool combo_pressed(void) {
  static PadState pad;
  static bool init;
  if (!init) {
    padInitializeDefault(&pad);
    init = true;
  }
  padUpdate(&pad);
  const u64 both = HidNpadButton_StickL | HidNpadButton_StickR;
  return (padGetButtons(&pad) & both) == both &&
         (padGetButtonsDown(&pad) & both) != 0;
}

void dkgl_capture_check(void) {
  if (capture_every >= 0)
    return;
  capture_every = 0;
  FILE *f = fopen(dkgl_path("dkgl_capture.txt"), "rb");
  if (!f)
    return;
  int every = 0;
  if (fscanf(f, "%d", &every) != 1 || every < 0)
    every = 0; /* 0 or empty: dumps only on L3+R3 */
  fclose(f);
  capture_every = every;
  dkgl_trace_enabled = true;
  mkdir(dkgl_path("dkgl_trace"), 0777);
  debugPrintf("dkgl: frame capture every %d frames (0 = L3+R3 only), trace on\n",
              every);
}

static void put_le(uint8_t *p, uint32_t v, int bytes) {
  for (int i = 0; i < bytes; i++)
    p[i] = (uint8_t)(v >> (8 * i));
}

static void capture_frame(DkglSurface *surf, int slot) {
  const uint32_t w = surf->width, h = surf->height, row = w * 4;
  DkglRing staging;
  if (!dkgl_ring_alloc(row * h, 4096, &staging)) { /* see glReadPixels */
    debugPrintf("dkgl: frame capture skipped (ring full)\n");
    return;
  }
  DkImageView view;
  dkImageViewDefaults(&view, &surf->images[slot]);
  dkgl_rt_barrier();
  dkgl_before_copy();
  DkImageRect rect = {0, 0, 0, w, h, 1};
  DkCopyBuf dst = {staging.gpu, row, row * h};
  dkCmdBufCopyImageToBuffer(dkgl_cmd(), &view, &rect, &dst, 0);
  dkgl_finish();

  char name[64], path[700];
  snprintf(name, sizeof(name), "dkgl_frame_%05u.bmp", surf->frame);
  snprintf(path, sizeof(path), "%s", dkgl_path(name));
  FILE *f = fopen(path, "wb");
  if (!f) {
    debugPrintf("dkgl: frame capture: cannot write %s\n", path);
    return;
  }
  /* 24-bit BMP, rows bottom-up: the swapchain image is stored the same way
   * (lower-left origin), so memory rows go out in order. */
  const uint32_t out_row = (w * 3 + 3) & ~3u;
  uint8_t hdr[54] = {'B', 'M'};
  put_le(hdr + 2, 54 + out_row * h, 4);
  put_le(hdr + 10, 54, 4);
  put_le(hdr + 14, 40, 4);
  put_le(hdr + 18, w, 4);
  put_le(hdr + 22, h, 4);
  put_le(hdr + 26, 1, 2);
  put_le(hdr + 28, 24, 2);
  put_le(hdr + 34, out_row * h, 4);
  fwrite(hdr, 1, sizeof(hdr), f);
  uint8_t *line = calloc(1, out_row);
  uint64_t sum = 0;
  uint32_t lit = 0;
  for (uint32_t y = 0; y < h && line; y++) {
    const uint8_t *s = (const uint8_t *)staging.cpu + (size_t)y * row;
    for (uint32_t x = 0; x < w; x++) {
      line[3 * x + 0] = s[4 * x + 2];
      line[3 * x + 1] = s[4 * x + 1];
      line[3 * x + 2] = s[4 * x + 0];
      const uint32_t l = s[4 * x] + s[4 * x + 1] + s[4 * x + 2];
      sum += l;
      lit += l > 24;
    }
    fwrite(line, 1, out_row, f);
  }
  free(line);
  fclose(f);
  debugPrintf("dkgl: captured %s (%ux%u, mean rgb %llu, %u%% of pixels lit)\n",
              path, w, h, (unsigned long long)(sum / ((uint64_t)w * h * 3)),
              (unsigned)((uint64_t)lit * 100 / ((uint64_t)w * h)));
}

/* mvl_nx (HW round 3): the layers of the texture arrays a traced frame
 * sampled, as dkgl_tex_<name>_L<layer>.bmp (4-byte formats only), to see
 * what the GPU holds. Queued by trace_call, written at present. */
static DkglTexture *tex_dump_queue[8];
static int tex_dump_count;
void dkgl_trace_queue_texture(DkglTexture *t) {
  static GLuint done[32];
  static int ndone;
  if (!t || t->depth < 2 || !t->fmt || t->fmt->bytes != 4 || (t->fmt->flags & DKGL_FMT_COMPRESSED))
    return;
  for (int i = 0; i < ndone; i++)
    if (done[i] == t->name)
      return;
  if (ndone >= 32 || tex_dump_count >= 8)
    return;
  done[ndone++] = t->name;
  tex_dump_queue[tex_dump_count++] = t;
}

static void dump_queued_textures(void) {
  for (int q = 0; q < tex_dump_count; q++) {
    DkglTexture *t = tex_dump_queue[q];
    const uint32_t w = t->width, h = t->height, row = w * 4;
    for (uint32_t layer = 0; layer < t->depth && layer < 16; layer++) {
      DkglRing staging;
      if (!dkgl_ring_alloc(row * h, 4096, &staging))
        return;
      DkImageView view;
      dkImageViewDefaults(&view, &t->image);
      dkgl_before_copy();
      DkImageRect rect = {0, 0, layer, w, h, 1};
      DkCopyBuf dst = {staging.gpu, row, row * h};
      dkCmdBufCopyImageToBuffer(dkgl_cmd(), &view, &rect, &dst, 0);
      dkgl_finish();
      char name[64], path[700];
      snprintf(name, sizeof(name), "dkgl_tex_%u_L%u.bmp", t->name, layer);
      snprintf(path, sizeof(path), "%s", dkgl_path(name));
      FILE *f = fopen(path, "wb");
      if (!f)
        continue;
      const uint32_t out_row = (w * 3 + 3) & ~3u;
      uint8_t hdr[54] = {'B', 'M'};
      put_le(hdr + 2, 54 + out_row * h, 4);
      put_le(hdr + 10, 54, 4);
      put_le(hdr + 14, 40, 4);
      put_le(hdr + 18, w, 4);
      put_le(hdr + 22, h, 4);
      put_le(hdr + 26, 1, 2);
      put_le(hdr + 28, 24, 2);
      put_le(hdr + 34, out_row * h, 4);
      fwrite(hdr, 1, sizeof(hdr), f);
      uint8_t line[3 * 4096 + 4];
      for (uint32_t y = 0; y < h; y++) {
        const uint8_t *src = (const uint8_t *)staging.cpu + (size_t)y * row;
        memset(line, 0, out_row);
        for (uint32_t x = 0; x < w && x < 4096; x++) {
          line[3 * x + 0] = src[4 * x + 2];
          line[3 * x + 1] = src[4 * x + 1];
          line[3 * x + 2] = src[4 * x + 0];
        }
        fwrite(line, 1, out_row, f);
      }
      fclose(f);
      debugPrintf("dkgl: dumped texture %u layer %u (%ux%u f%x) to %s\n", t->name, layer, w, h,
                  t->fmt->internal, path);
    }
  }
  tex_dump_count = 0;
}

static uint64_t frame_serial[3];
static uint64_t stat_frame_tick;
static uint64_t stat_frames;
static uint64_t stat_min, stat_max, stat_sum;

static void frame_stats(DkglSurface *surf) {
  const uint64_t now = armGetSystemTick();
  if (stat_frame_tick) {
    const uint64_t period = now - stat_frame_tick;
    if (!stat_min || period < stat_min)
      stat_min = period;
    if (period > stat_max)
      stat_max = period;
    stat_sum += period;
    stat_frames++;
  }
  stat_frame_tick = now;
  if (surf->frame > 10 && (surf->frame % 120) != 0)
    return;
  const uint64_t hz = armGetSystemTickFreq();
  char mem[192];
  dkgl_memory_report(mem, sizeof(mem));
  const uint64_t frames = stat_frames ? stat_frames : 1;
  uint64_t gpu_avg, gpu_max;
  dkgl_prof_take(&gpu_avg, &gpu_max);
  /* docked/handheld and the performance mode decide the GPU clock */
  static int last_mode = -1, last_perf = -1;
  const int mode = (int)appletGetOperationMode();
  const int perf = (int)appletGetPerformanceMode();
  if (mode != last_mode || perf != last_perf) {
    debugPrintf("dkgl: operation mode %s, performance mode %d\n",
                mode == AppletOperationMode_Console ? "docked" : "handheld", perf);
    last_mode = mode;
    last_perf = perf;
  }
  debugPrintf("dkgl frame=%u fps=%llu.%02llu period_us=%llu/%llu/%llu "
              "gpu_us=%llu/%llu acquire_us=%llu tex=a%dt%d "
              "draws=%u dispatches=%u flushes=%u uploads=%u/%lluKiB "
              "uniforms=%lluKiB renames=%u rt_barriers=%u copy_barriers=%u "
              "compiles=%u/%llums cache_hits=%u gpu_wait_us=%llu %s\n",
              surf->frame,
              stat_sum ? (unsigned long long)(hz * frames / stat_sum) : 0ull,
              stat_sum ? (unsigned long long)((hz * frames * 100 / stat_sum) % 100) : 0ull,
              (unsigned long long)(stat_min * 1000000 / hz),
              (unsigned long long)(stat_sum / frames * 1000000 / hz),
              (unsigned long long)(stat_max * 1000000 / hz),
              (unsigned long long)gpu_avg, (unsigned long long)gpu_max,
              (unsigned long long)(dkg.stat_acquire_ticks / frames * 1000000 / hz),
              dkgl_policy_aniso, dkgl_policy_trilinear,
              dkg.stat_draws / (uint32_t)frames, dkg.stat_dispatches,
              dkg.stat_flushes / (uint32_t)frames, dkg.stat_uploads,
              (unsigned long long)(dkg.stat_upload_bytes >> 10),
              (unsigned long long)(dkg.stat_uniform_bytes / frames >> 10),
              dkg.stat_renames / (uint32_t)frames,
              dkg.stat_rt_barriers / (uint32_t)frames,
              dkg.stat_copies_barriers / (uint32_t)frames, dkg.stat_compiles,
              (unsigned long long)(dkg.stat_compile_ticks * 1000 / hz),
              dkg.stat_cache_hits,
              (unsigned long long)(dkg.stat_wait_ticks / frames * 1000000 / hz),
              mem);
  thread_registry_report_cpu();
  dkg.stat_draws = dkg.stat_dispatches = dkg.stat_flushes = 0;
  dkg.stat_uploads = dkg.stat_renames = dkg.stat_rt_barriers = 0;
  dkg.stat_copies_barriers = dkg.stat_compiles = dkg.stat_cache_hits = 0;
  dkg.stat_upload_bytes = dkg.stat_uniform_bytes = dkg.stat_compile_ticks = 0;
  dkg.stat_wait_ticks = 0;
  dkg.stat_acquire_ticks = 0;
  stat_frames = stat_sum = stat_max = stat_min = 0;
}

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
  (void)dpy;
  DkglSurface *surf = surface_of(surface);
  if (!surf)
    return fail(EGL_BAD_SURFACE);
  if (!surf->window)
    return EGL_TRUE;
  DKGL_LOCK();
  /* a frame that never touched the back buffer still presents one */
  dkgl_surface_acquire(surf);
  const int slot = surf->acquired;
  dkgl_capture_check();
  /* draws in the frame just finished (frame_stats resets the counter) */
  const uint32_t frame_draws = dkg.stat_draws - trace_last_draws;
  if (combo_capture) {
    combo_capture = false;
    capture_frame(surf, slot);
  } else if (capture_every > 0 && capture_count < 16 && surf->frame > 0 &&
             surf->frame % (uint32_t)capture_every == 0 &&
             (capture_count < 2 || frame_draws >= 60)) {
    /* after two menu dumps, only frames that look like a match are dumped */
    capture_count++;
    capture_frame(surf, slot);
  }
  if (dkgl_trace_on) {
    debugPrintf("trace present frame=%u slot=%d\n", surf->frame, slot);
    dump_queued_textures();
  }
  /* With dkgl_capture.txt present, creating dkgl_capture_now does the same
   * as L3+R3 (checked twice a second; the file is removed). Used where the
   * stick clicks cannot be pressed together, e.g. from a PC in an emulator. */
  bool file_trigger = false;
  if (dkgl_trace_enabled && surf->frame % 30 == 0) {
    struct stat st;
    /* next to dkgl_capture.txt (a bare name went to the working folder,
     * which is not the port's folder when an emulator starts the NRO) */
    if (stat(dkgl_path("dkgl_capture_now"), &st) == 0) {
      remove(dkgl_path("dkgl_capture_now"));
      file_trigger = true;
    }
  }
  if (combo_count < 8 && (file_trigger || combo_pressed())) {
    combo_count++;
    combo_capture = true;
    dkgl_trace_on = true;
    dkgl_prof_request("L3+R3");
    debugPrintf("trace begin frame=%u (L3+R3, previous frame %u draws)\n",
                surf->frame + 1, frame_draws);
  } else {
    /* mvl_nx: any frame with draws (UE's matches had 60+, a 2D menu has ~8) */
    dkgl_trace_on = capture_every > 0 && trace_count < 3 && frame_draws >= 1 &&
                    (surf->frame + 1) % (uint32_t)capture_every == 0;
    if (dkgl_trace_on) {
      trace_count++;
      debugPrintf("trace begin frame=%u (previous frame %u draws)\n",
                  surf->frame + 1, frame_draws);
    }
  }
  ab_step(frame_draws);
  /* a pass-by-pass GPU profile of a match frame every 30 s or so */
  if (frame_draws >= 60 && surf->frame - last_auto_profile >= 1800) {
    last_auto_profile = surf->frame;
    dkgl_prof_request("auto");
  }
  dkgl_prof_frame_end(surf->frame);
  dkgl_flush();
  if (dkQueueIsInErrorState(dkg.queue)) {
    debug_log_flush();
    fatal_error("deko3d: the GPU queue faulted before present (frame %u).\n"
                "See debug.log.", surf->frame);
  }
  dkQueuePresentImage(dkg.queue, surf->swapchain, slot);
  surf->acquired = -1;
  dkgl_fbo_forget_targets();
  surf->frame++;
  /* Keep the CPU at most two frames ahead of the GPU. */
  frame_serial[surf->frame % 3] = dkgl_signal_point();
  const uint64_t wait = frame_serial[(surf->frame + 1) % 3];
  if (wait)
    dkgl_wait_serial(wait); /* counted in stat_wait_ticks by wait_oldest */
  dkgl_prof_collect();
#ifdef DEBUG_LOG
  frame_stats(surf);
#else
  (void)frame_stats;
#endif
  trace_last_draws = dkg.stat_draws;
  dkgl_prof_frame_start();
  DKGL_UNLOCK();
  return EGL_TRUE;
}

/* ---------------------------------------------------------- fence sync */

typedef struct {
  uint32_t magic;
  uint64_t serial;
} EglSync;

static EGLSyncKHR egl_create_sync(EGLDisplay dpy, EGLenum type,
                                  const EGLint *attrib_list) {
  (void)dpy;
  (void)attrib_list;
  if (type != EGL_SYNC_FENCE_KHR) {
    egl_error = EGL_BAD_ATTRIBUTE;
    return EGL_NO_SYNC_KHR;
  }
  EglSync *s = calloc(1, sizeof(*s));
  if (!s)
    return EGL_NO_SYNC_KHR;
  DKGL_LOCK();
  s->magic = SYNC_MAGIC;
  s->serial = dkg.initialized ? dkgl_signal_point() : 0;
  if (dkg.initialized)
    dkgl_flush();
  DKGL_UNLOCK();
  return s;
}

static EglSync *egl_sync(EGLSyncKHR sync) {
  EglSync *s = sync;
  return s && s->magic == SYNC_MAGIC ? s : NULL;
}

static EGLBoolean egl_destroy_sync(EGLDisplay dpy, EGLSyncKHR sync) {
  (void)dpy;
  EglSync *s = egl_sync(sync);
  if (!s)
    return fail(EGL_BAD_PARAMETER);
  s->magic = 0;
  free(s);
  return EGL_TRUE;
}

static EGLint egl_client_wait_sync(EGLDisplay dpy, EGLSyncKHR sync,
                                   EGLint flags, EGLTimeKHR timeout) {
  (void)dpy;
  (void)flags;
  EglSync *s = egl_sync(sync);
  if (!s)
    return EGL_FALSE;
  const uint64_t start = armTicksToNs(armGetSystemTick());
  for (;;) {
    DKGL_LOCK();
    const bool done = dkgl_serial_done(s->serial);
    DKGL_UNLOCK();
    if (done)
      return EGL_CONDITION_SATISFIED_KHR;
    if (!timeout || (timeout != EGL_FOREVER_KHR &&
                     armTicksToNs(armGetSystemTick()) - start >= timeout))
      return EGL_TIMEOUT_EXPIRED_KHR;
    svcSleepThread(100000);
  }
}

static EGLBoolean egl_get_sync_attrib(EGLDisplay dpy, EGLSyncKHR sync,
                                      EGLint attribute, EGLint *value) {
  (void)dpy;
  EglSync *s = egl_sync(sync);
  if (!s || !value)
    return fail(EGL_BAD_PARAMETER);
  switch (attribute) {
  case EGL_SYNC_TYPE_KHR:
    *value = EGL_SYNC_FENCE_KHR;
    break;
  case EGL_SYNC_STATUS_KHR:
    DKGL_LOCK();
    *value = dkgl_serial_done(s->serial) ? EGL_SIGNALED_KHR : EGL_UNSIGNALED_KHR;
    DKGL_UNLOCK();
    break;
  case EGL_SYNC_CONDITION_KHR:
    *value = EGL_SYNC_PRIOR_COMMANDS_COMPLETE_KHR;
    break;
  default:
    return fail(EGL_BAD_ATTRIBUTE);
  }
  return EGL_TRUE;
}

void *dkgl_egl_proc(const char *name);
void *dkgl_egl_proc(const char *name) {
  if (!strcmp(name, "eglCreateSyncKHR"))
    return (void *)egl_create_sync;
  if (!strcmp(name, "eglDestroySyncKHR"))
    return (void *)egl_destroy_sync;
  if (!strcmp(name, "eglClientWaitSyncKHR"))
    return (void *)egl_client_wait_sync;
  if (!strcmp(name, "eglGetSyncAttribKHR"))
    return (void *)egl_get_sync_attrib;
  return NULL;
}

__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *procname) {
  if (!procname)
    return NULL;
  void *p = dkgl_get_proc(procname);
  static int logged;
  if (logged < 64) {
    logged++;
    debugPrintf("dkgl: eglGetProcAddress(%s) -> %p\n", procname, p);
  }
  return (__eglMustCastToProperFunctionPointerType)p;
}

EGLBoolean eglReleaseThread(void) {
  DKGL_LOCK();
  if (dkg.initialized)
    dkgl_flush();
  dkgl_tls_ctx = NULL;
  DKGL_UNLOCK();
  return EGL_TRUE;
}

EGLBoolean eglWaitClient(void) {
  glFinish();
  return EGL_TRUE;
}

EGLBoolean eglWaitGL(void) { return eglWaitClient(); }

EGLBoolean eglWaitNative(EGLint engine) {
  (void)engine;
  return EGL_TRUE;
}
