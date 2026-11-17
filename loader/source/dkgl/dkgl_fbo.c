/* dkgl_fbo.c -- renderbuffers, framebuffers, clears, blits and readback */
#include "dkgl.h"

#include <stdlib.h>

#include "util.h"

DkCmdBuf dkgl_cmd(void);
void dkgl_apply_clear_scissor(DkglContext *ctx);
bool dkgl_texture_define_level(DkglTexture *tex, GLenum target, GLint level,
                               const DkglFormat *fmt, uint32_t w, uint32_t h);

/* Write-mask bits (4 per target) for colour targets that are real; gaps in
 * glDrawBuffers are bound to a stand-in view and masked off. */
uint32_t dkgl_rt_real_mask;
uint32_t dkgl_rt_count;
uint32_t dkgl_rt_width, dkgl_rt_height;
bool dkgl_rt_integer[DKGL_DRAW_BUFFERS];
bool dkgl_rt_has_depth, dkgl_rt_has_stencil;

static uint64_t bound_key[2 * DKGL_DRAW_BUFFERS + 4];
static bool bound_valid;

/* ------------------------------------------------------- renderbuffers */

DkglRenderbuffer *dkgl_renderbuffer(GLuint name, bool create) {
  if (!name)
    return NULL;
  DkglRenderbuffer *rb = dkgl_name_get(DKGL_OBJ_RENDERBUFFER, name);
  if (rb || !create)
    return rb;
  rb = calloc(1, sizeof(*rb));
  if (!rb)
    return NULL;
  rb->name = name;
  dkgl_name_set(DKGL_OBJ_RENDERBUFFER, name, rb);
  return rb;
}

void glGenRenderbuffers(GLsizei n, GLuint *renderbuffers) {
  DKGL_LOCK();
  for (GLsizei i = 0; i < n; i++) {
    renderbuffers[i] = dkgl_name_gen(DKGL_OBJ_RENDERBUFFER);
    dkgl_renderbuffer(renderbuffers[i], true);
  }
  DKGL_UNLOCK();
}

void dkgl_fbo_detach_renderbuffer(DkglContext *ctx, GLuint name);

void glDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  for (GLsizei i = 0; i < n; i++) {
    DkglRenderbuffer *rb = dkgl_renderbuffer(renderbuffers[i], false);
    if (!rb)
      continue;
    if (ctx) {
      if (ctx->renderbuffer == renderbuffers[i])
        ctx->renderbuffer = 0;
      dkgl_fbo_detach_renderbuffer(ctx, renderbuffers[i]);
    }
    dkgl_free_deferred(&rb->mem);
    dkgl_name_free(DKGL_OBJ_RENDERBUFFER, renderbuffers[i]);
    free(rb);
  }
  DKGL_UNLOCK();
}

GLboolean glIsRenderbuffer(GLuint renderbuffer) {
  DKGL_LOCK();
  GLboolean r = dkgl_renderbuffer(renderbuffer, false) ? GL_TRUE : GL_FALSE;
  DKGL_UNLOCK();
  return r;
}

void glBindRenderbuffer(GLenum target, GLuint renderbuffer) {
  (void)target;
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  if (ctx) {
    if (renderbuffer)
      dkgl_renderbuffer(renderbuffer, true);
    ctx->renderbuffer = renderbuffer;
  }
  DKGL_UNLOCK();
}

void glRenderbufferStorageMultisample(GLenum target, GLsizei samples,
                                      GLenum internalformat, GLsizei width,
                                      GLsizei height) {
  (void)target;
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglRenderbuffer *rb = ctx ? dkgl_renderbuffer(ctx->renderbuffer, false) : NULL;
  const DkglFormat *fmt = dkgl_format(internalformat);
  if (!fmt && internalformat == GL_DEPTH_STENCIL)
    fmt = dkgl_format(GL_DEPTH24_STENCIL8);
  if (!rb || !fmt || width <= 0 || height <= 0) {
    if (ctx)
      DKGL_ERROR(ctx, !fmt ? GL_INVALID_ENUM : GL_INVALID_OPERATION);
    DKGL_UNLOCK();
    return;
  }
  if (samples > 1)
    dkgl_log_once("multisampled renderbuffer stored single-sampled");
  DkImageLayoutMaker maker;
  dkImageLayoutMakerDefaults(&maker, dkg.device);
  maker.type = DkImageType_2D;
  maker.format = fmt->dk;
  maker.dimensions[0] = (uint32_t)width;
  maker.dimensions[1] = (uint32_t)height;
  maker.flags = DkImageFlags_UsageRender;
  if (fmt->flags & DKGL_FMT_2D_ENGINE)
    maker.flags |= DkImageFlags_Usage2DEngine;
  DkImageLayout layout;
  dkImageLayoutInitialize(&layout, &maker);
  DkglAlloc mem;
  if (!dkgl_alloc(DKGL_HEAP_IMAGE, (uint32_t)dkImageLayoutGetSize(&layout),
                  dkImageLayoutGetAlignment(&layout), &mem)) {
    DKGL_ERROR(ctx, GL_OUT_OF_MEMORY);
    DKGL_UNLOCK();
    return;
  }
  dkgl_free_deferred(&rb->mem);
  rb->layout = layout;
  rb->mem = mem;
  dkImageInitialize(&rb->image, &rb->layout, mem.block, mem.offset);
  rb->fmt = fmt;
  rb->internal_format = internalformat;
  rb->width = (uint32_t)width;
  rb->height = (uint32_t)height;
  rb->samples = 0;
  rb->has_storage = true;
  rb->generation++;
  bound_valid = false;
  DKGL_UNLOCK();
}

void glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width,
                           GLsizei height) {
  glRenderbufferStorageMultisample(target, 0, internalformat, width, height);
}

void glRenderbufferStorageMultisampleEXT(GLenum target, GLsizei samples,
                                         GLenum internalformat, GLsizei width,
                                         GLsizei height) {
  glRenderbufferStorageMultisample(target, samples, internalformat, width,
                                   height);
}

void glGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params) {
  (void)target;
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglRenderbuffer *rb = ctx ? dkgl_renderbuffer(ctx->renderbuffer, false) : NULL;
  if (!rb) {
    if (ctx)
      DKGL_ERROR(ctx, GL_INVALID_OPERATION);
    DKGL_UNLOCK();
    return;
  }
  switch (pname) {
  case GL_RENDERBUFFER_WIDTH:
    *params = (GLint)rb->width;
    break;
  case GL_RENDERBUFFER_HEIGHT:
    *params = (GLint)rb->height;
    break;
  case GL_RENDERBUFFER_INTERNAL_FORMAT:
    *params = (GLint)(rb->internal_format ? rb->internal_format : GL_RGBA4);
    break;
  case GL_RENDERBUFFER_SAMPLES:
    *params = 0;
    break;
  default:
    *params = 0;
    break;
  }
  DKGL_UNLOCK();
}

/* -------------------------------------------------------- framebuffers */

DkglFramebuffer *dkgl_framebuffer(GLuint name, bool create) {
  if (!name)
    return NULL;
  DkglFramebuffer *fb = dkgl_name_get(DKGL_OBJ_FRAMEBUFFER, name);
  if (fb || !create)
    return fb;
  fb = calloc(1, sizeof(*fb));
  if (!fb)
    return NULL;
  fb->name = name;
  fb->draw_buffers[0] = GL_COLOR_ATTACHMENT0;
  fb->read_buffer = GL_COLOR_ATTACHMENT0;
  dkgl_name_set(DKGL_OBJ_FRAMEBUFFER, name, fb);
  return fb;
}

void glGenFramebuffers(GLsizei n, GLuint *framebuffers) {
  DKGL_LOCK();
  for (GLsizei i = 0; i < n; i++) {
    framebuffers[i] = dkgl_name_gen(DKGL_OBJ_FRAMEBUFFER);
    dkgl_framebuffer(framebuffers[i], true);
  }
  DKGL_UNLOCK();
}

void glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  for (GLsizei i = 0; i < n; i++) {
    DkglFramebuffer *fb = dkgl_framebuffer(framebuffers[i], false);
    if (!fb)
      continue;
    if (ctx) {
      if (ctx->draw_fbo == framebuffers[i]) {
        ctx->draw_fbo = 0;
        ctx->dirty |= DKGL_DIRTY_FRAMEBUFFER;
      }
      if (ctx->read_fbo == framebuffers[i])
        ctx->read_fbo = 0;
    }
    dkgl_name_free(DKGL_OBJ_FRAMEBUFFER, framebuffers[i]);
    free(fb);
  }
  bound_valid = false;
  DKGL_UNLOCK();
}

GLboolean glIsFramebuffer(GLuint framebuffer) {
  DKGL_LOCK();
  GLboolean r = dkgl_framebuffer(framebuffer, false) ? GL_TRUE : GL_FALSE;
  DKGL_UNLOCK();
  return r;
}

void glBindFramebuffer(GLenum target, GLuint framebuffer) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  if (!ctx) {
    DKGL_UNLOCK();
    return;
  }
  if (framebuffer)
    dkgl_framebuffer(framebuffer, true);
  if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) {
    if (ctx->draw_fbo != framebuffer)
      ctx->dirty |= DKGL_DIRTY_FRAMEBUFFER;
    ctx->draw_fbo = framebuffer;
  }
  if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER)
    ctx->read_fbo = framebuffer;
  DKGL_UNLOCK();
}

static DkglFramebuffer *target_fbo(DkglContext *ctx, GLenum target) {
  return dkgl_framebuffer(target == GL_READ_FRAMEBUFFER ? ctx->read_fbo
                                                        : ctx->draw_fbo,
                          false);
}

static DkglAttachment *attachment_point(DkglFramebuffer *fb, GLenum attachment,
                                        DkglAttachment **second) {
  *second = NULL;
  if (attachment >= GL_COLOR_ATTACHMENT0 &&
      attachment < GL_COLOR_ATTACHMENT0 + DKGL_DRAW_BUFFERS)
    return &fb->color[attachment - GL_COLOR_ATTACHMENT0];
  if (attachment == GL_DEPTH_ATTACHMENT)
    return &fb->depth;
  if (attachment == GL_STENCIL_ATTACHMENT)
    return &fb->stencil;
  if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
    *second = &fb->stencil;
    return &fb->depth;
  }
  return NULL;
}

static void attach(GLenum target, GLenum attachment, GLenum type, GLuint name,
                   GLint level, GLint layer) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglFramebuffer *fb = ctx ? target_fbo(ctx, target) : NULL;
  DkglAttachment *second;
  DkglAttachment *a = fb ? attachment_point(fb, attachment, &second) : NULL;
  if (!a) {
    if (ctx)
      DKGL_ERROR(ctx, GL_INVALID_OPERATION);
    DKGL_UNLOCK();
    return;
  }
  DkglAttachment value = {name ? type : GL_NONE, name, level, layer};
  *a = value;
  if (second)
    *second = value;
  fb->version++;
  ctx->dirty |= DKGL_DIRTY_FRAMEBUFFER;
  DKGL_UNLOCK();
}

void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget,
                            GLuint texture, GLint level) {
  GLint layer = 0;
  if (textarget >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
      textarget <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)
    layer = (GLint)(textarget - GL_TEXTURE_CUBE_MAP_POSITIVE_X);
  if (texture) {
    DKGL_LOCK();
    DkglTexture *tex = dkgl_texture(texture, true);
    if (tex && !tex->target)
      tex->target = layer || textarget != GL_TEXTURE_2D ? GL_TEXTURE_CUBE_MAP
                                                        : GL_TEXTURE_2D;
    DKGL_UNLOCK();
  }
  attach(target, attachment, GL_TEXTURE, texture, level, layer);
}

void glFramebufferTextureLayer(GLenum target, GLenum attachment,
                               GLuint texture, GLint level, GLint layer) {
  attach(target, attachment, GL_TEXTURE, texture, level, layer);
}

void glFramebufferTexture(GLenum target, GLenum attachment, GLuint texture,
                          GLint level) {
  attach(target, attachment, GL_TEXTURE, texture, level, 0);
}

void glFramebufferRenderbuffer(GLenum target, GLenum attachment,
                               GLenum renderbuffertarget,
                               GLuint renderbuffer) {
  (void)renderbuffertarget;
  attach(target, attachment, GL_RENDERBUFFER, renderbuffer, 0, 0);
}

void glFramebufferTexture2DMultisampleEXT(GLenum target, GLenum attachment,
                                          GLenum textarget, GLuint texture,
                                          GLint level, GLsizei samples) {
  (void)samples;
  glFramebufferTexture2D(target, attachment, textarget, texture, level);
}

static void detach_matching(DkglFramebuffer *fb, GLenum type, GLuint name) {
  if (!fb)
    return;
  bool changed = false;
  for (int i = 0; i < DKGL_DRAW_BUFFERS; i++)
    if (fb->color[i].type == type && fb->color[i].name == name) {
      memset(&fb->color[i], 0, sizeof(fb->color[i]));
      changed = true;
    }
  if (fb->depth.type == type && fb->depth.name == name) {
    memset(&fb->depth, 0, sizeof(fb->depth));
    changed = true;
  }
  if (fb->stencil.type == type && fb->stencil.name == name) {
    memset(&fb->stencil, 0, sizeof(fb->stencil));
    changed = true;
  }
  if (changed)
    fb->version++;
}

void dkgl_fbo_detach_texture(DkglContext *ctx, GLuint name) {
  detach_matching(dkgl_framebuffer(ctx->draw_fbo, false), GL_TEXTURE, name);
  detach_matching(dkgl_framebuffer(ctx->read_fbo, false), GL_TEXTURE, name);
  bound_valid = false;
}

void dkgl_fbo_detach_renderbuffer(DkglContext *ctx, GLuint name) {
  detach_matching(dkgl_framebuffer(ctx->draw_fbo, false), GL_RENDERBUFFER, name);
  detach_matching(dkgl_framebuffer(ctx->read_fbo, false), GL_RENDERBUFFER, name);
  bound_valid = false;
}

void dkgl_fbo_forget_targets(void) { bound_valid = false; }

/* Resolves an attachment to an image view and its format. */
static bool attachment_view(const DkglAttachment *a, DkImageView *view,
                            const DkglFormat **fmt, uint32_t *w, uint32_t *h,
                            uint64_t *key) {
  if (a->type == GL_TEXTURE) {
    DkglTexture *tex = dkgl_texture(a->name, false);
    if (!dkgl_texture_view(tex, a->level, a->layer, view))
      return false;
    *fmt = tex->fmt;
    *w = tex->width >> a->level;
    *h = tex->height >> a->level;
    if (!*w)
      *w = 1;
    if (!*h)
      *h = 1;
    *key = ((uint64_t)a->name << 32) ^ ((uint64_t)tex->generation << 12) ^
           ((uint64_t)a->level << 8) ^ (uint64_t)a->layer;
    dkgl_texture_used(tex);
    return true;
  }
  if (a->type == GL_RENDERBUFFER) {
    DkglRenderbuffer *rb = dkgl_renderbuffer(a->name, false);
    if (!rb || !rb->has_storage)
      return false;
    dkImageViewDefaults(view, &rb->image);
    *fmt = rb->fmt;
    *w = rb->width;
    *h = rb->height;
    *key = ((uint64_t)a->name << 32) ^ ((uint64_t)rb->generation << 12) ^
           0x80000000ull;
    return true;
  }
  return false;
}

void dkgl_surface_acquire(DkglSurface *surf) {
  if (!surf || !surf->window || surf->acquired >= 0)
    return;
  int slot = 0;
  const uint64_t t0 = armGetSystemTick();
  dkSwapchainAcquireImage(surf->swapchain, &slot, &surf->acquire_fence);
  dkg.stat_acquire_ticks += armGetSystemTick() - t0;
  dkgl_prof_acquire(false);
  dkCmdBufWaitFence(dkgl_cmd(), &surf->acquire_fence);
  dkgl_prof_acquire(true);
  surf->acquired = slot;
}

/* Binds the draw framebuffer's attachments as deko3d render targets. */
bool dkgl_bind_draw_targets(DkglContext *ctx) {
  DkImageView views[DKGL_DRAW_BUFFERS];
  const DkImageView *colors[DKGL_DRAW_BUFFERS];
  DkImageView depth_view;
  const DkImageView *depth = NULL;
  uint64_t key[2 * DKGL_DRAW_BUFFERS + 4];
  memset(key, 0, sizeof(key));
  uint32_t count = 0, real = 0, w = 0xffff, h = 0xffff;
  bool has_depth = false, has_stencil = false;
  bool integer[DKGL_DRAW_BUFFERS] = {false};

  if (!ctx->draw_fbo) {
    DkglSurface *surf = ctx->draw_surface;
    if (!surf)
      return false;
    if (surf->window) {
      dkgl_surface_acquire(surf);
      dkImageViewDefaults(&views[0], &surf->images[surf->acquired]);
      /* mvl_nx: the window is drawn as sRGB (writes encoded, blending in
       * linear). Unity's linear colour space ends with a plain copy of an
       * sRGB texture to the window; sampling decodes it, so without this the
       * picture came out in linear values: dark, oversaturated. */
      if (dkgl_window_srgb)
        views[0].format = DkImageFormat_RGBA8_Unorm_sRGB;
      key[0] = 0x5157000000000000ull ^ ((uint64_t)(uintptr_t)surf << 4) ^
               (uint64_t)surf->acquired ^ ((uint64_t)dkgl_window_srgb << 40);
    } else {
      if (!surf->has_color)
        return false;
      dkImageViewDefaults(&views[0], &surf->color);
      key[0] = 0x5042000000000000ull ^ (uint64_t)(uintptr_t)surf;
    }
    colors[0] = &views[0];
    count = 1;
    real = 0xf;
    w = surf->width;
    h = surf->height;
    if (surf->has_depth) {
      dkImageViewDefaults(&depth_view, &surf->depth);
      depth = &depth_view;
      has_depth = has_stencil = true;
      key[2 * DKGL_DRAW_BUFFERS] = 1;
    }
  } else {
    DkglFramebuffer *fb = dkgl_framebuffer(ctx->draw_fbo, false);
    if (!fb)
      return false;
    int last = -1;
    for (int i = 0; i < DKGL_DRAW_BUFFERS; i++) {
      GLenum db = fb->draw_buffers[i];
      if (db < GL_COLOR_ATTACHMENT0 ||
          db >= GL_COLOR_ATTACHMENT0 + DKGL_DRAW_BUFFERS)
        continue;
      const DkglFormat *fmt;
      uint32_t aw, ah;
      const DkglAttachment *ca = &fb->color[db - GL_COLOR_ATTACHMENT0];
      if (ca->type == GL_TEXTURE)
        dkgl_texture_promote_rt(dkgl_texture(ca->name, false));
      if (!attachment_view(ca, &views[i], &fmt, &aw, &ah, &key[i]))
        continue;
      colors[i] = &views[i];
      real |= 0xfu << (4 * i);
      integer[i] = (fmt->flags & DKGL_FMT_INTEGER) != 0;
      if (aw < w)
        w = aw;
      if (ah < h)
        h = ah;
      last = i;
    }
    count = (uint32_t)(last + 1);
    /* fill gaps with a real view; their writes are masked off */
    for (uint32_t i = 0; i < count; i++) {
      if (real & (0xfu << (4 * i)))
        continue;
      for (uint32_t j = 0; j < count; j++)
        if (real & (0xfu << (4 * j))) {
          views[i] = views[j];
          colors[i] = &views[i];
          break;
        }
    }
    const DkglFormat *dfmt;
    uint32_t dw, dh;
    const DkglAttachment *da = fb->depth.type ? &fb->depth : &fb->stencil;
    if (da->type &&
        attachment_view(da, &depth_view, &dfmt, &dw, &dh,
                        &key[2 * DKGL_DRAW_BUFFERS])) {
      depth = &depth_view;
      has_depth = (dfmt->flags & DKGL_FMT_DEPTH) != 0;
      has_stencil = (dfmt->flags & DKGL_FMT_STENCIL) != 0;
      if (dw < w)
        w = dw;
      if (dh < h)
        h = dh;
    }
    if (!count && !depth)
      return false;
    key[2 * DKGL_DRAW_BUFFERS + 1] = ctx->draw_fbo;
  }
  key[2 * DKGL_DRAW_BUFFERS + 2] = count;
  key[2 * DKGL_DRAW_BUFFERS + 3] = real;
  if (bound_valid && !memcmp(key, bound_key, sizeof(key)))
    return true;
  dkgl_rt_barrier();
  dkCmdBufBindRenderTargets(dkgl_cmd(), colors, count, depth);
  dkgl_prof_pass('F', ctx->draw_fbo, 0);
  memcpy(bound_key, key, sizeof(key));
  bound_valid = true;
  dkgl_rt_real_mask = real;
  dkgl_rt_count = count;
  dkgl_rt_width = w;
  dkgl_rt_height = h;
  dkgl_rt_has_depth = has_depth;
  dkgl_rt_has_stencil = has_stencil;
  memcpy(dkgl_rt_integer, integer, sizeof(integer));
  ctx->dirty |= DKGL_DIRTY_COLOR_WRITE | DKGL_DIRTY_SCISSOR |
                DKGL_DIRTY_DEPTH_STENCIL;
  return true;
}

/* Read framebuffer's selected colour buffer. */
bool dkgl_read_attachment(DkglContext *ctx, DkImageView *view,
                          const DkglFormat **fmt, uint32_t *w, uint32_t *h) {
  if (!ctx->read_fbo) {
    DkglSurface *surf = ctx->read_surface ? ctx->read_surface : ctx->draw_surface;
    if (!surf)
      return false;
    if (surf->window) {
      dkgl_surface_acquire(surf);
      dkImageViewDefaults(view, &surf->images[surf->acquired]);
    } else if (surf->has_color) {
      dkImageViewDefaults(view, &surf->color);
    } else {
      return false;
    }
    *fmt = dkgl_format(GL_RGBA8);
    *w = surf->width;
    *h = surf->height;
    return true;
  }
  DkglFramebuffer *fb = dkgl_framebuffer(ctx->read_fbo, false);
  if (!fb || fb->read_buffer < GL_COLOR_ATTACHMENT0 ||
      fb->read_buffer >= GL_COLOR_ATTACHMENT0 + DKGL_DRAW_BUFFERS)
    return false;
  uint64_t key;
  const DkglAttachment *ra = &fb->color[fb->read_buffer - GL_COLOR_ATTACHMENT0];
  /* every caller hands the view to the copy or 2D engine */
  if (ra->type == GL_TEXTURE)
    dkgl_texture_prepare_transfer(dkgl_texture(ra->name, false));
  return attachment_view(ra, view, fmt, w, h, &key);
}

static bool read_depth_attachment(DkglContext *ctx, bool draw, DkImageView *view,
                                  const DkglFormat **fmt, uint32_t *w,
                                  uint32_t *h) {
  GLuint name = draw ? ctx->draw_fbo : ctx->read_fbo;
  if (!name) {
    DkglSurface *surf = draw ? ctx->draw_surface
                             : (ctx->read_surface ? ctx->read_surface : ctx->draw_surface);
    if (!surf || !surf->has_depth)
      return false;
    dkImageViewDefaults(view, &surf->depth);
    *fmt = dkgl_format(GL_DEPTH24_STENCIL8);
    *w = surf->width;
    *h = surf->height;
    return true;
  }
  DkglFramebuffer *fb = dkgl_framebuffer(name, false);
  if (!fb)
    return false;
  uint64_t key;
  const DkglAttachment *a = fb->depth.type ? &fb->depth : &fb->stencil;
  return a->type && attachment_view(a, view, fmt, w, h, &key);
}

GLenum glCheckFramebufferStatus(GLenum target) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  GLenum status = GL_FRAMEBUFFER_COMPLETE;
  DkglFramebuffer *fb = ctx ? target_fbo(ctx, target) : NULL;
  if (fb) {
    bool any = false;
    const DkglAttachment *all[DKGL_DRAW_BUFFERS + 2];
    for (int i = 0; i < DKGL_DRAW_BUFFERS; i++)
      all[i] = &fb->color[i];
    all[DKGL_DRAW_BUFFERS] = &fb->depth;
    all[DKGL_DRAW_BUFFERS + 1] = &fb->stencil;
    for (int i = 0; i < DKGL_DRAW_BUFFERS + 2; i++) {
      if (!all[i]->type)
        continue;
      DkImageView view;
      const DkglFormat *fmt;
      uint32_t w, h;
      uint64_t key;
      if (!attachment_view(all[i], &view, &fmt, &w, &h, &key)) {
        status = GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        break;
      }
      if (i < DKGL_DRAW_BUFFERS && !(fmt->flags & DKGL_FMT_RENDER)) {
        status = GL_FRAMEBUFFER_UNSUPPORTED;
        break;
      }
      any = true;
    }
    if (status == GL_FRAMEBUFFER_COMPLETE && !any)
      status = GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
  }
  DKGL_UNLOCK();
  return status;
}

void glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment,
                                           GLenum pname, GLint *params) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglFramebuffer *fb = ctx ? target_fbo(ctx, target) : NULL;
  *params = 0;
  if (!fb) {
    /* default framebuffer */
    switch (pname) {
    case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE:
      *params = GL_FRAMEBUFFER_DEFAULT;
      break;
    case GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE:
    case GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE:
    case GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE:
    case GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE:
      *params = attachment == GL_BACK || attachment == GL_FRONT ? 8 : 0;
      break;
    case GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE:
      *params = attachment == GL_DEPTH ? 24 : 0;
      break;
    case GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE:
      *params = attachment == GL_STENCIL ? 8 : 0;
      break;
    case GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE:
      *params = GL_UNSIGNED_NORMALIZED;
      break;
    case GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING:
      *params = dkgl_window_srgb && (attachment == GL_BACK || attachment == GL_FRONT) ? GL_SRGB : GL_LINEAR;
      break;
    default:
      break;
    }
    DKGL_UNLOCK();
    return;
  }
  DkglAttachment *second;
  DkglAttachment *a = attachment_point(fb, attachment, &second);
  if (!a) {
    DKGL_ERROR(ctx, GL_INVALID_ENUM);
    DKGL_UNLOCK();
    return;
  }
  const DkglFormat *fmt = NULL;
  if (a->type == GL_TEXTURE) {
    DkglTexture *tex = dkgl_texture(a->name, false);
    fmt = tex ? tex->fmt : NULL;
  } else if (a->type == GL_RENDERBUFFER) {
    DkglRenderbuffer *rb = dkgl_renderbuffer(a->name, false);
    fmt = rb ? rb->fmt : NULL;
  }
  switch (pname) {
  case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE:
    *params = (GLint)a->type;
    break;
  case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME:
    *params = (GLint)a->name;
    break;
  case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL:
    *params = a->level;
    break;
  case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE:
    *params = a->layer ? (GLint)(GL_TEXTURE_CUBE_MAP_POSITIVE_X + a->layer) : 0;
    break;
  case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER:
    *params = a->layer;
    break;
  case GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING:
    *params = fmt && (fmt->dk == DkImageFormat_RGBA8_Unorm_sRGB ||
                      fmt->dk == DkImageFormat_RGBX8_Unorm_sRGB)
                  ? GL_SRGB
                  : GL_LINEAR;
    break;
  case GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE:
    *params = !fmt ? GL_NONE
              : (fmt->flags & DKGL_FMT_FLOAT)   ? GL_FLOAT
              : (fmt->flags & DKGL_FMT_INTEGER) ? ((fmt->flags & DKGL_FMT_SIGNED) ? GL_INT : GL_UNSIGNED_INT)
              : (fmt->flags & DKGL_FMT_SIGNED)  ? GL_SIGNED_NORMALIZED
                                                : GL_UNSIGNED_NORMALIZED;
    break;
  case GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE:
  case GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE:
  case GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE:
  case GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE:
    *params = fmt && !(fmt->flags & (DKGL_FMT_DEPTH | DKGL_FMT_STENCIL)) ? 8 : 0;
    break;
  case GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE:
    *params = fmt && (fmt->flags & DKGL_FMT_DEPTH) ? 24 : 0;
    break;
  case GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE:
    *params = fmt && (fmt->flags & DKGL_FMT_STENCIL) ? 8 : 0;
    break;
  default:
    break;
  }
  DKGL_UNLOCK();
}

void glDrawBuffers(GLsizei n, const GLenum *bufs) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglFramebuffer *fb = ctx ? dkgl_framebuffer(ctx->draw_fbo, false) : NULL;
  if (fb) {
    for (int i = 0; i < DKGL_DRAW_BUFFERS; i++)
      fb->draw_buffers[i] = i < n ? bufs[i] : GL_NONE;
    fb->version++;
    ctx->dirty |= DKGL_DIRTY_FRAMEBUFFER;
  }
  DKGL_UNLOCK();
}

void glReadBuffer(GLenum src) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglFramebuffer *fb = ctx ? dkgl_framebuffer(ctx->read_fbo, false) : NULL;
  if (fb)
    fb->read_buffer = src;
  DKGL_UNLOCK();
}

void glInvalidateFramebuffer(GLenum target, GLsizei numAttachments,
                             const GLenum *attachments) {
  (void)target;
  (void)numAttachments;
  (void)attachments;
}

void glInvalidateSubFramebuffer(GLenum target, GLsizei numAttachments,
                                const GLenum *attachments, GLint x, GLint y,
                                GLsizei width, GLsizei height) {
  (void)target;
  (void)numAttachments;
  (void)attachments;
  (void)x;
  (void)y;
  (void)width;
  (void)height;
}

void glDiscardFramebufferEXT(GLenum target, GLsizei numAttachments,
                             const GLenum *attachments) {
  (void)target;
  (void)numAttachments;
  (void)attachments;
}

/* ------------------------------------------------------------- clears */

static void clear_targets(DkglContext *ctx, GLbitfield mask, int only_buffer,
                          const void *color_value, bool color_int,
                          float depth, GLint stencil) {
  if (ctx->rasterizer_discard)
    return;
  if (!dkgl_bind_draw_targets(ctx))
    return;
  if (dkgl_trace_on)
    debugPrintf("trace clear fbo=%u mask=%x buffer=%d sc=%d:%d,%d,%d,%d\n",
                ctx->draw_fbo, mask, only_buffer, ctx->scissor_test,
                ctx->scissor[0], ctx->scissor[1], ctx->scissor[2],
                ctx->scissor[3]);
  dkgl_before_3d();
  dkgl_apply_clear_scissor(ctx);
  DkCmdBuf cmd = dkgl_cmd();
  if (mask & GL_COLOR_BUFFER_BIT) {
    for (uint32_t i = 0; i < dkgl_rt_count; i++) {
      if (only_buffer >= 0 && (int)i != only_buffer)
        continue;
      uint32_t write = ctx->color_mask[i] & ((dkgl_rt_real_mask >> (4 * i)) & 0xf);
      if (!write)
        continue;
      if (color_int || dkgl_rt_integer[i]) {
        if (!color_int)
          continue; /* glClear on an integer target is undefined */
        dkCmdBufClearColor(cmd, i, write, color_value);
      } else {
        dkCmdBufClearColor(cmd, i, write, color_value);
      }
    }
  }
  const bool clear_depth =
      (mask & GL_DEPTH_BUFFER_BIT) && ctx->depth_mask && dkgl_rt_has_depth;
  const uint8_t stencil_mask = (mask & GL_STENCIL_BUFFER_BIT) && dkgl_rt_has_stencil
                                   ? (uint8_t)(ctx->stencil[0].write_mask & 0xff)
                                   : 0;
  if (clear_depth || stencil_mask) {
    float d = depth < 0.0f ? 0.0f : (depth > 1.0f ? 1.0f : depth);
    dkCmdBufClearDepthStencil(cmd, clear_depth, d, stencil_mask,
                              (uint8_t)stencil);
  }
  dkg.rt_written = true;
}

void glClear(GLbitfield mask) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  if (ctx)
    clear_targets(ctx, mask, -1, ctx->clear_color, false, ctx->clear_depth,
                  ctx->clear_stencil);
  DKGL_UNLOCK();
}

void glClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat *value) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  if (ctx) {
    if (buffer == GL_COLOR)
      clear_targets(ctx, GL_COLOR_BUFFER_BIT, drawbuffer, value, false, 0, 0);
    else if (buffer == GL_DEPTH)
      clear_targets(ctx, GL_DEPTH_BUFFER_BIT, -1, NULL, false, value[0], 0);
  }
  DKGL_UNLOCK();
}

void glClearBufferiv(GLenum buffer, GLint drawbuffer, const GLint *value) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  if (ctx) {
    if (buffer == GL_COLOR)
      clear_targets(ctx, GL_COLOR_BUFFER_BIT, drawbuffer, value, true, 0, 0);
    else if (buffer == GL_STENCIL)
      clear_targets(ctx, GL_STENCIL_BUFFER_BIT, -1, NULL, false, 0, value[0]);
  }
  DKGL_UNLOCK();
}

void glClearBufferuiv(GLenum buffer, GLint drawbuffer, const GLuint *value) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  if (ctx && buffer == GL_COLOR)
    clear_targets(ctx, GL_COLOR_BUFFER_BIT, drawbuffer, value, true, 0, 0);
  DKGL_UNLOCK();
}

void glClearBufferfi(GLenum buffer, GLint drawbuffer, GLfloat depth,
                     GLint stencil) {
  (void)buffer;
  (void)drawbuffer;
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  if (ctx)
    clear_targets(ctx, GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, -1, NULL,
                  false, depth, stencil);
  DKGL_UNLOCK();
}

/* -------------------------------------------------------------- blits */

static uint32_t min_u(int a, int b) { return (uint32_t)(a < b ? a : b); }
static uint32_t span(int a, int b) { return (uint32_t)(a < b ? b - a : a - b); }

void glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                       GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                       GLbitfield mask, GLenum filter) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  if (!ctx || !span(srcX0, srcX1) || !span(srcY0, srcY1) ||
      !span(dstX0, dstX1) || !span(dstY0, dstY1)) {
    DKGL_UNLOCK();
    return;
  }
  if (dkgl_trace_on)
    debugPrintf("trace blit read=%u draw=%u src=%d,%d,%d,%d dst=%d,%d,%d,%d "
                "mask=%x filter=%x\n",
                ctx->read_fbo, ctx->draw_fbo, srcX0, srcY0, srcX1, srcY1, dstX0,
                dstY0, dstX1, dstY1, mask, filter);
  dkgl_rt_barrier();
  dkgl_prof_pass('B', ctx->draw_fbo, 0);
  DkImageRect sr = {min_u(srcX0, srcX1), min_u(srcY0, srcY1), 0,
                    span(srcX0, srcX1), span(srcY0, srcY1), 1};
  DkImageRect dr = {min_u(dstX0, dstX1), min_u(dstY0, dstY1), 0,
                    span(dstX0, dstX1), span(dstY0, dstY1), 1};
  uint32_t flags = filter == GL_LINEAR ? DkBlitFlag_FilterLinear
                                       : DkBlitFlag_FilterNearest;
  if ((srcX1 < srcX0) != (dstX1 < dstX0))
    flags |= DkBlitFlag_FlipX;
  if ((srcY1 < srcY0) != (dstY1 < dstY0))
    flags |= DkBlitFlag_FlipY;
  const bool same_size = sr.width == dr.width && sr.height == dr.height &&
                         !(flags & (DkBlitFlag_FlipX | DkBlitFlag_FlipY));

  if (mask & GL_COLOR_BUFFER_BIT) {
    DkImageView src;
    const DkglFormat *sfmt;
    uint32_t sw, sh;
    if (dkgl_read_attachment(ctx, &src, &sfmt, &sw, &sh)) {
      /* destination: every draw buffer of the draw framebuffer */
      DkImageView dst[DKGL_DRAW_BUFFERS];
      const DkglFormat *dfmt[DKGL_DRAW_BUFFERS];
      int ndst = 0;
      if (!ctx->draw_fbo) {
        DkglSurface *surf = ctx->draw_surface;
        if (surf && surf->window) {
          dkgl_surface_acquire(surf);
          dkImageViewDefaults(&dst[0], &surf->images[surf->acquired]);
          dfmt[0] = dkgl_format(GL_RGBA8);
          ndst = 1;
        } else if (surf && surf->has_color) {
          dkImageViewDefaults(&dst[0], &surf->color);
          dfmt[0] = dkgl_format(GL_RGBA8);
          ndst = 1;
        }
      } else {
        DkglFramebuffer *fb = dkgl_framebuffer(ctx->draw_fbo, false);
        for (int i = 0; fb && i < DKGL_DRAW_BUFFERS; i++) {
          GLenum db = fb->draw_buffers[i];
          if (db < GL_COLOR_ATTACHMENT0 ||
              db >= GL_COLOR_ATTACHMENT0 + DKGL_DRAW_BUFFERS)
            continue;
          uint32_t w, h;
          uint64_t key;
          const DkglAttachment *da = &fb->color[db - GL_COLOR_ATTACHMENT0];
          if (da->type == GL_TEXTURE)
            dkgl_texture_prepare_transfer(dkgl_texture(da->name, false));
          if (attachment_view(da, &dst[ndst], &dfmt[ndst], &w, &h, &key))
            ndst++;
        }
      }
      dkgl_before_copy();
      DkCmdBuf cmd = dkgl_cmd();
      for (int i = 0; i < ndst; i++) {
        const bool engine2d = (sfmt->flags & DKGL_FMT_2D_ENGINE) &&
                              (dfmt[i]->flags & DKGL_FMT_2D_ENGINE);
        if (engine2d)
          dkCmdBufBlitImage(cmd, &src, &sr, &dst[i], &dr, flags, 0);
        else if (same_size && sfmt->bytes == dfmt[i]->bytes)
          dkCmdBufCopyImage(cmd, &src, &sr, &dst[i], &dr, 0);
        else
          dkgl_log_once("glBlitFramebuffer: unsupported colour blit");
      }
    }
  }
  if (mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) {
    DkImageView src, dst;
    const DkglFormat *sfmt, *dfmt;
    uint32_t sw, sh, dw, dh;
    if (read_depth_attachment(ctx, false, &src, &sfmt, &sw, &sh) &&
        read_depth_attachment(ctx, true, &dst, &dfmt, &dw, &dh)) {
      if (same_size && sfmt->dk == dfmt->dk) {
        dkgl_before_copy();
        dkCmdBufCopyImage(dkgl_cmd(), &src, &sr, &dst, &dr, 0);
      } else {
        dkgl_log_once("glBlitFramebuffer: scaled depth/stencil blit skipped");
      }
    }
  }
  bound_valid = false; /* the 2D engine leaves 3D render targets alone, but be safe */
  DKGL_UNLOCK();
}

/* --------------------------------------------------------- readback */

static void half_row_to_float(const uint16_t *src, float *dst, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    uint16_t h = src[i];
    uint32_t sign = (uint32_t)(h & 0x8000) << 16;
    uint32_t exp = (h >> 10) & 0x1f;
    uint32_t mant = h & 0x3ff;
    uint32_t bits;
    if (!exp) {
      if (!mant) {
        bits = sign;
      } else {
        exp = 1;
        while (!(mant & 0x400)) {
          mant <<= 1;
          exp--;
        }
        mant &= 0x3ff;
        bits = sign | ((exp + 112) << 23) | (mant << 13);
      }
    } else if (exp == 31) {
      bits = sign | 0x7f800000 | (mant << 13);
    } else {
      bits = sign | ((exp + 112) << 23) | (mant << 13);
    }
    memcpy(&dst[i], &bits, 4);
  }
}

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type, void *pixels) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkImageView view;
  const DkglFormat *fmt;
  uint32_t w, h;
  if (!ctx || width <= 0 || height <= 0 ||
      !dkgl_read_attachment(ctx, &view, &fmt, &w, &h) ||
      (fmt->flags & DKGL_FMT_COMPRESSED)) {
    if (ctx)
      DKGL_ERROR(ctx, GL_INVALID_OPERATION);
    DKGL_UNLOCK();
    return;
  }

  if (x < 0 || y < 0 || (uint32_t)(x + width) > w || (uint32_t)(y + height) > h) {
    /* clamp to the attachment; GL leaves the rest undefined */
    if (x < 0)
      x = 0;
    if (y < 0)
      y = 0;
    if ((uint32_t)x >= w || (uint32_t)y >= h) {
      DKGL_UNLOCK();
      return;
    }
    if ((uint32_t)(x + width) > w)
      width = (GLsizei)(w - (uint32_t)x);
    if ((uint32_t)(y + height) > h)
      height = (GLsizei)(h - (uint32_t)y);
  }
  const uint32_t row = (uint32_t)width * fmt->bytes;
  DkglRing staging;
  /* Page-aligned: with a 64-byte aligned destination, Ryujinx lands the
   * copied pixels a few KiB off (the image comes back rolled sideways). */
  if (!dkgl_ring_alloc(row * (uint32_t)height, 4096, &staging)) {
    DKGL_UNLOCK();
    return;
  }
  dkgl_rt_barrier();
  dkgl_before_copy();
  DkImageRect rect = {(uint32_t)x, (uint32_t)y, 0, (uint32_t)width,
                      (uint32_t)height, 1};
  DkCopyBuf dst = {staging.gpu, row, row * (uint32_t)height};
  dkCmdBufCopyImageToBuffer(dkgl_cmd(), &view, &rect, &dst, 0);
  dkgl_finish();

  uint8_t *out;
  DkglBuffer *pbo = ctx->pixel_pack ? dkgl_buffer(ctx->pixel_pack, false) : NULL;
  if (pbo) {
    if (!pbo->mem.node) {
      DKGL_UNLOCK();
      return;
    }
    out = (uint8_t *)dkgl_cpu(&pbo->mem) + (uintptr_t)pixels;
  } else {
    out = pixels;
  }
  const uint32_t bpp = dkgl_client_bpp(format, type);
  const uint32_t align = ctx->pack_alignment > 0 ? (uint32_t)ctx->pack_alignment : 4;
  const uint32_t row_length =
      ctx->pack_row_length > 0 ? (uint32_t)ctx->pack_row_length : (uint32_t)width;
  const uint32_t pitch = (row_length * bpp + align - 1) / align * align;
  out += (uintptr_t)ctx->pack_skip_rows * pitch +
         (uintptr_t)ctx->pack_skip_pixels * bpp;
  const uint8_t *src = staging.cpu;
  for (GLsizei r = 0; r < height; r++) {
    const uint8_t *s = src + (size_t)r * row;
    uint8_t *d = out + (size_t)r * pitch;
    if (bpp == fmt->bytes && !(format == GL_BGRA_EXT && fmt->dk != DkImageFormat_BGRA8_Unorm)) {
      memcpy(d, s, (size_t)width * bpp);
    } else if (fmt->bytes == 8 && type == GL_FLOAT && format == GL_RGBA) {
      half_row_to_float((const uint16_t *)s, (float *)d, (uint32_t)width * 4);
    } else if (fmt->bytes == 4 && format == GL_BGRA_EXT) {
      for (GLsizei i = 0; i < width; i++) {
        d[4 * i + 0] = s[4 * i + 2];
        d[4 * i + 1] = s[4 * i + 1];
        d[4 * i + 2] = s[4 * i + 0];
        d[4 * i + 3] = s[4 * i + 3];
      }
    } else if (fmt->bytes == 4 && bpp == 3) {
      for (GLsizei i = 0; i < width; i++)
        memcpy(d + 3 * i, s + 4 * i, 3);
    } else {
      dkgl_log_once("glReadPixels: unhandled conversion");
      memset(d, 0, (size_t)width * bpp);
    }
  }
  DKGL_UNLOCK();
}

/* --------------------------------------------------------- tex copies */

static void copy_tex_sub(DkglContext *ctx, DkglTexture *tex, GLint level,
                         GLint layer, GLint xoffset, GLint yoffset, GLint x,
                         GLint y, GLsizei width, GLsizei height) {
  DkImageView src, dst;
  dkgl_texture_prepare_transfer(tex);
  const DkglFormat *sfmt;
  uint32_t sw, sh;
  if (width <= 0 || height <= 0 ||
      !dkgl_read_attachment(ctx, &src, &sfmt, &sw, &sh) ||
      !dkgl_texture_view(tex, level, layer, &dst)) {
    DKGL_ERROR(ctx, GL_INVALID_OPERATION);
    return;
  }
  DkImageRect sr = {(uint32_t)x, (uint32_t)y, 0, (uint32_t)width,
                    (uint32_t)height, 1};
  DkImageRect dr = {(uint32_t)xoffset, (uint32_t)yoffset, 0, (uint32_t)width,
                    (uint32_t)height, 1};
  dkgl_rt_barrier();
  dkgl_before_copy();
  if ((sfmt->flags & DKGL_FMT_2D_ENGINE) && (tex->fmt->flags & DKGL_FMT_2D_ENGINE))
    dkCmdBufBlitImage(dkgl_cmd(), &src, &sr, &dst, &dr, 0, 0);
  else if (sfmt->bytes == tex->fmt->bytes)
    dkCmdBufCopyImage(dkgl_cmd(), &src, &sr, &dst, &dr, 0);
  else
    dkgl_log_once("glCopyTexSubImage: incompatible formats");
}

void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                         GLint yoffset, GLint x, GLint y, GLsizei width,
                         GLsizei height) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglTexture *tex = ctx ? dkgl_bound_texture(ctx, target) : NULL;
  if (tex) {
    GLint layer = target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
                          target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
                      ? (GLint)(target - GL_TEXTURE_CUBE_MAP_POSITIVE_X)
                      : 0;
    copy_tex_sub(ctx, tex, level, layer, xoffset, yoffset, x, y, width, height);
  }
  DKGL_UNLOCK();
}

void glCopyTexSubImage3D(GLenum target, GLint level, GLint xoffset,
                         GLint yoffset, GLint zoffset, GLint x, GLint y,
                         GLsizei width, GLsizei height) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglTexture *tex = ctx ? dkgl_bound_texture(ctx, target) : NULL;
  if (tex)
    copy_tex_sub(ctx, tex, level, zoffset, xoffset, yoffset, x, y, width,
                 height);
  DKGL_UNLOCK();
}

void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat,
                      GLint x, GLint y, GLsizei width, GLsizei height,
                      GLint border) {
  (void)border;
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglTexture *tex = ctx ? dkgl_bound_texture(ctx, target) : NULL;
  const DkglFormat *fmt = dkgl_format(internalformat);
  if (!fmt)
    fmt = dkgl_format_unsized(internalformat, GL_UNSIGNED_BYTE);
  if (!tex || !fmt || width <= 0 || height <= 0 ||
      !dkgl_texture_define_level(tex, target, level, fmt, (uint32_t)width,
                                 (uint32_t)height)) {
    if (ctx)
      DKGL_ERROR(ctx, GL_INVALID_OPERATION);
    DKGL_UNLOCK();
    return;
  }
  GLint layer = target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
                        target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
                    ? (GLint)(target - GL_TEXTURE_CUBE_MAP_POSITIVE_X)
                    : 0;
  copy_tex_sub(ctx, tex, level, layer, 0, 0, x, y, width, height);
  ctx->dirty |= DKGL_DIRTY_TEXTURES;
  DKGL_UNLOCK();
}
