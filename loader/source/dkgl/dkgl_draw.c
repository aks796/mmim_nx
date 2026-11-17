/* dkgl_draw.c -- vertex arrays, state flush and draw/dispatch calls
 *
 * GL state is per context; the GPU state it maps to is global. The cache
 * below remembers what was last emitted, so a draw only records what
 * changed. A draw from a different context than the previous one revalidates
 * everything.
 */
#include "dkgl.h"

#include <stdio.h>
#include <stdlib.h>

#include "util.h"

DkCmdBuf dkgl_cmd(void);
const DkShader *dkgl_binary_shader(DkglStageBinary *bin);
void dkgl_uniforms_forget(void);

extern uint32_t dkgl_rt_real_mask, dkgl_rt_count, dkgl_rt_width, dkgl_rt_height;
extern bool dkgl_rt_has_depth, dkgl_rt_has_stencil;

#define CONST_VB 15 /* vertex buffer slot holding current attribute values */

static struct {
  DkglContext *ctx;
  bool raster_valid, color_valid, write_valid, blend_valid, ds_valid;
  bool stencil_valid, viewport_valid, scissor_valid, bias_valid, ms_valid;
  bool blend_const_valid, restart_valid, line_valid;
  DkRasterizerState raster;
  DkColorState color;
  DkColorWriteState write;
  DkBlendState blend[DKGL_DRAW_BUFFERS];
  DkDepthStencilState ds;
  uint8_t stencil[2][3];
  DkViewport viewport;
  DkScissor scissor;
  float bias[3];
  bool alpha_to_coverage;
  float blend_const[4];
  bool restart;
  uint32_t restart_index;
  float line_width;
  uint64_t shader_uid[2]; /* binaries' uids: a freed node's address comes back */
  uint64_t compute_uid;
  DkResHandle tex[6][32];
  uint8_t tex_valid[6];
  DkResHandle img[6][8];
  uint8_t img_valid[6];
  DkVtxAttribState attribs[32];
  uint32_t num_attribs;
  bool attribs_valid;
  DkVtxBufferState vb_state[DKGL_VBO_BINDINGS];
  DkBufExtents vb[DKGL_VBO_BINDINGS];
  uint32_t vb_count;
  bool vb_valid;
  DkGpuAddr idx_addr;
  DkIdxFormat idx_format;
  bool idx_valid;
  uint32_t texture_epoch;
  /* current-attribute snapshot in the ring */
  DkGpuAddr const_addr;
  uint64_t const_serial;
} gs;

static uint32_t last_draw_prog; /* for the GPU profile's labels */
static uint64_t last_draw_fs;

void dkgl_draw_invalidate_all(void) {
  DkglContext *ctx = gs.ctx;
  memset(&gs, 0, sizeof(gs));
  gs.ctx = ctx;
  dkgl_uniforms_forget();
  dkgl_fbo_forget_targets();
}

void dkgl_draw_forget_buffer(GLuint name) {
  (void)name;
  gs.vb_valid = false;
  gs.idx_valid = false;
}

/* ------------------------------------------------------------ VAOs */

static void vao_defaults(DkglVao *vao) {
  memset(vao, 0, sizeof(*vao));
  for (int i = 0; i < DKGL_ATTRIBS; i++) {
    vao->attribs[i].size = 4;
    vao->attribs[i].type = GL_FLOAT;
    vao->attribs[i].binding = (uint8_t)i;
  }
}

void dkgl_context_init_vao(DkglContext *ctx);
void dkgl_context_init_vao(DkglContext *ctx) {
  vao_defaults(&ctx->default_vao);
  ctx->vao = &ctx->default_vao;
  ctx->vao_name = 0;
  for (int i = 0; i < DKGL_ATTRIBS; i++) {
    ctx->current_attrib[i].f[0] = ctx->current_attrib[i].f[1] =
        ctx->current_attrib[i].f[2] = 0.0f;
    ctx->current_attrib[i].f[3] = 1.0f;
  }
}

void glGenVertexArrays(GLsizei n, GLuint *arrays) {
  DKGL_LOCK();
  for (GLsizei i = 0; i < n; i++) {
    GLuint name = dkgl_name_gen(DKGL_OBJ_VAO);
    DkglVao *vao = calloc(1, sizeof(*vao));
    if (vao) {
      vao_defaults(vao);
      vao->name = name;
      dkgl_name_set(DKGL_OBJ_VAO, name, vao);
    }
    arrays[i] = name;
  }
  DKGL_UNLOCK();
}

void glDeleteVertexArrays(GLsizei n, const GLuint *arrays) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  for (GLsizei i = 0; i < n; i++) {
    DkglVao *vao = dkgl_name_get(DKGL_OBJ_VAO, arrays[i]);
    if (!vao)
      continue;
    if (ctx && ctx->vao == vao) {
      ctx->vao = &ctx->default_vao;
      ctx->vao_name = 0;
      ctx->dirty |= DKGL_DIRTY_VERTEX;
    }
    dkgl_name_free(DKGL_OBJ_VAO, arrays[i]);
    free(vao);
  }
  DKGL_UNLOCK();
}

GLboolean glIsVertexArray(GLuint array) {
  DKGL_LOCK();
  GLboolean r = dkgl_name_get(DKGL_OBJ_VAO, array) ? GL_TRUE : GL_FALSE;
  DKGL_UNLOCK();
  return r;
}

void glBindVertexArray(GLuint array) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  if (ctx) {
    DkglVao *vao = array ? dkgl_name_get(DKGL_OBJ_VAO, array) : &ctx->default_vao;
    if (!vao) {
      DKGL_ERROR(ctx, GL_INVALID_OPERATION);
    } else {
      ctx->vao = vao;
      ctx->vao_name = array;
      ctx->dirty |= DKGL_DIRTY_VERTEX;
    }
  }
  DKGL_UNLOCK();
}

static uint32_t type_size(GLenum type) {
  switch (type) {
  case GL_BYTE:
  case GL_UNSIGNED_BYTE:
    return 1;
  case GL_SHORT:
  case GL_UNSIGNED_SHORT:
  case GL_HALF_FLOAT:
  case GL_HALF_FLOAT_OES:
    return 2;
  default:
    return 4;
  }
}

static uint32_t attrib_bytes(const DkglAttrib *a) {
  if (a->type == GL_INT_2_10_10_10_REV || a->type == GL_UNSIGNED_INT_2_10_10_10_REV)
    return 4;
  return type_size(a->type) * (a->size == 5 ? 4 : a->size);
}

static void attrib_pointer(GLuint index, GLint size, GLenum type,
                           GLboolean normalized, bool integer, GLsizei stride,
                           const void *pointer) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  if (!ctx || index >= DKGL_ATTRIBS) {
    if (ctx)
      DKGL_ERROR(ctx, GL_INVALID_VALUE);
    DKGL_UNLOCK();
    return;
  }
  DkglAttrib *a = &ctx->vao->attribs[index];
  a->size = (uint8_t)(size == GL_BGRA_EXT ? 5 : size);
  a->type = type;
  a->normalized = normalized;
  a->integer = integer;
  a->relative_offset = 0;
  a->binding = (uint8_t)index;
  DkglVertexBinding *b = &ctx->vao->bindings[index];
  b->buffer = ctx->array_buffer;
  b->offset = (GLintptr)pointer;
  b->client = ctx->array_buffer ? NULL : pointer;
  b->stride = stride ? (uint32_t)stride : attrib_bytes(a);
  ctx->dirty |= DKGL_DIRTY_VERTEX;
  DKGL_UNLOCK();
}

void glVertexAttribPointer(GLuint index, GLint size, GLenum type,
                           GLboolean normalized, GLsizei stride,
                           const void *pointer) {
  attrib_pointer(index, size, type, normalized, false, stride, pointer);
}

void glVertexAttribIPointer(GLuint index, GLint size, GLenum type,
                            GLsizei stride, const void *pointer) {
  attrib_pointer(index, size, type, GL_FALSE, true, stride, pointer);
}

void glEnableVertexAttribArray(GLuint index) {
  DkglContext *ctx = dkgl_ctx();
  if (ctx && index < DKGL_ATTRIBS) {
    ctx->vao->attribs[index].enabled = true;
    ctx->dirty |= DKGL_DIRTY_VERTEX;
  }
}

void glDisableVertexAttribArray(GLuint index) {
  DkglContext *ctx = dkgl_ctx();
  if (ctx && index < DKGL_ATTRIBS) {
    ctx->vao->attribs[index].enabled = false;
    ctx->dirty |= DKGL_DIRTY_VERTEX;
  }
}

void glBindVertexBuffer(GLuint bindingindex, GLuint buffer, GLintptr offset,
                        GLsizei stride) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  if (ctx && bindingindex < DKGL_VBO_BINDINGS) {
    if (buffer)
      dkgl_buffer(buffer, true);
    DkglVertexBinding *b = &ctx->vao->bindings[bindingindex];
    b->buffer = buffer;
    b->offset = offset;
    b->stride = (uint32_t)stride;
    b->client = NULL;
    ctx->dirty |= DKGL_DIRTY_VERTEX;
  }
  DKGL_UNLOCK();
}

static void attrib_format(GLuint index, GLint size, GLenum type,
                          GLboolean normalized, bool integer,
                          GLuint relativeoffset) {
  DkglContext *ctx = dkgl_ctx();
  if (!ctx || index >= DKGL_ATTRIBS)
    return;
  DkglAttrib *a = &ctx->vao->attribs[index];
  a->size = (uint8_t)(size == GL_BGRA_EXT ? 5 : size);
  a->type = type;
  a->normalized = normalized;
  a->integer = integer;
  a->relative_offset = relativeoffset;
  ctx->dirty |= DKGL_DIRTY_VERTEX;
}

void glVertexAttribFormat(GLuint attribindex, GLint size, GLenum type,
                          GLboolean normalized, GLuint relativeoffset) {
  attrib_format(attribindex, size, type, normalized, false, relativeoffset);
}

void glVertexAttribIFormat(GLuint attribindex, GLint size, GLenum type,
                           GLuint relativeoffset) {
  attrib_format(attribindex, size, type, GL_FALSE, true, relativeoffset);
}

void glVertexAttribBinding(GLuint attribindex, GLuint bindingindex) {
  DkglContext *ctx = dkgl_ctx();
  if (ctx && attribindex < DKGL_ATTRIBS && bindingindex < DKGL_VBO_BINDINGS) {
    ctx->vao->attribs[attribindex].binding = (uint8_t)bindingindex;
    ctx->dirty |= DKGL_DIRTY_VERTEX;
  }
}

void glVertexBindingDivisor(GLuint bindingindex, GLuint divisor) {
  DkglContext *ctx = dkgl_ctx();
  if (ctx && bindingindex < DKGL_VBO_BINDINGS) {
    ctx->vao->bindings[bindingindex].divisor = divisor;
    ctx->dirty |= DKGL_DIRTY_VERTEX;
  }
}

void glVertexAttribDivisor(GLuint index, GLuint divisor) {
  DkglContext *ctx = dkgl_ctx();
  if (ctx && index < DKGL_ATTRIBS) {
    ctx->vao->attribs[index].binding = (uint8_t)index;
    ctx->vao->bindings[index].divisor = divisor;
    ctx->dirty |= DKGL_DIRTY_VERTEX;
  }
}

static void set_current(GLuint index, const void *v, int n, int kind) {
  DkglContext *ctx = dkgl_ctx();
  if (!ctx || index >= DKGL_ATTRIBS)
    return;
  if (kind == 0) {
    const float *f = v;
    float out[4] = {0, 0, 0, 1};
    for (int i = 0; i < n; i++)
      out[i] = f[i];
    memcpy(ctx->current_attrib[index].f, out, sizeof(out));
  } else {
    memcpy(ctx->current_attrib[index].i, v, 16);
  }
  ctx->dirty |= DKGL_DIRTY_VERTEX;
}

void glVertexAttrib1f(GLuint index, GLfloat x) { set_current(index, &x, 1, 0); }
void glVertexAttrib2f(GLuint index, GLfloat x, GLfloat y) {
  const float v[2] = {x, y};
  set_current(index, v, 2, 0);
}
void glVertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z) {
  const float v[3] = {x, y, z};
  set_current(index, v, 3, 0);
}
void glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z,
                      GLfloat w) {
  const float v[4] = {x, y, z, w};
  set_current(index, v, 4, 0);
}
void glVertexAttrib1fv(GLuint index, const GLfloat *v) { set_current(index, v, 1, 0); }
void glVertexAttrib2fv(GLuint index, const GLfloat *v) { set_current(index, v, 2, 0); }
void glVertexAttrib3fv(GLuint index, const GLfloat *v) { set_current(index, v, 3, 0); }
void glVertexAttrib4fv(GLuint index, const GLfloat *v) { set_current(index, v, 4, 0); }
void glVertexAttribI4i(GLuint index, GLint x, GLint y, GLint z, GLint w) {
  const GLint v[4] = {x, y, z, w};
  set_current(index, v, 4, 1);
}
void glVertexAttribI4ui(GLuint index, GLuint x, GLuint y, GLuint z, GLuint w) {
  const GLuint v[4] = {x, y, z, w};
  set_current(index, v, 4, 1);
}
void glVertexAttribI4iv(GLuint index, const GLint *v) { set_current(index, v, 4, 1); }
void glVertexAttribI4uiv(GLuint index, const GLuint *v) { set_current(index, v, 4, 1); }

void glGetVertexAttribiv(GLuint index, GLenum pname, GLint *params) {
  DkglContext *ctx = dkgl_ctx();
  if (!ctx || index >= DKGL_ATTRIBS)
    return;
  const DkglAttrib *a = &ctx->vao->attribs[index];
  const DkglVertexBinding *b = &ctx->vao->bindings[a->binding];
  switch (pname) {
  case GL_VERTEX_ATTRIB_ARRAY_ENABLED:
    *params = a->enabled;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_SIZE:
    *params = a->size == 5 ? GL_BGRA_EXT : a->size;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_STRIDE:
    *params = (GLint)b->stride;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_TYPE:
    *params = (GLint)a->type;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
    *params = a->normalized;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_INTEGER:
    *params = a->integer;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_DIVISOR:
    *params = (GLint)b->divisor;
    break;
  case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING:
    *params = (GLint)b->buffer;
    break;
  case GL_VERTEX_ATTRIB_BINDING:
    *params = a->binding;
    break;
  case GL_VERTEX_ATTRIB_RELATIVE_OFFSET:
    *params = (GLint)a->relative_offset;
    break;
  case GL_CURRENT_VERTEX_ATTRIB:
    for (int i = 0; i < 4; i++)
      params[i] = (GLint)ctx->current_attrib[index].f[i];
    break;
  default:
    DKGL_ERROR(ctx, GL_INVALID_ENUM);
    break;
  }
}

void glGetVertexAttribfv(GLuint index, GLenum pname, GLfloat *params) {
  DkglContext *ctx = dkgl_ctx();
  if (!ctx || index >= DKGL_ATTRIBS)
    return;
  if (pname == GL_CURRENT_VERTEX_ATTRIB) {
    memcpy(params, ctx->current_attrib[index].f, 16);
    return;
  }
  GLint v[4] = {0};
  glGetVertexAttribiv(index, pname, v);
  params[0] = (GLfloat)v[0];
}

void glGetVertexAttribPointerv(GLuint index, GLenum pname, void **pointer) {
  DkglContext *ctx = dkgl_ctx();
  if (!ctx || index >= DKGL_ATTRIBS || pname != GL_VERTEX_ATTRIB_ARRAY_POINTER)
    return;
  const DkglAttrib *a = &ctx->vao->attribs[index];
  *pointer = (void *)ctx->vao->bindings[a->binding].offset;
}

/* -------------------------------------------------------- state flush */

static DkCompareOp cmp(GLenum f) {
  switch (f) {
  case GL_NEVER:
    return DkCompareOp_Never;
  case GL_LESS:
    return DkCompareOp_Less;
  case GL_EQUAL:
    return DkCompareOp_Equal;
  case GL_LEQUAL:
    return DkCompareOp_Lequal;
  case GL_GREATER:
    return DkCompareOp_Greater;
  case GL_NOTEQUAL:
    return DkCompareOp_NotEqual;
  case GL_GEQUAL:
    return DkCompareOp_Gequal;
  default:
    return DkCompareOp_Always;
  }
}

static DkStencilOp stencil_op(GLenum op) {
  switch (op) {
  case GL_ZERO:
    return DkStencilOp_Zero;
  case GL_REPLACE:
    return DkStencilOp_Replace;
  case GL_INCR:
    return DkStencilOp_Incr;
  case GL_DECR:
    return DkStencilOp_Decr;
  case GL_INVERT:
    return DkStencilOp_Invert;
  case GL_INCR_WRAP:
    return DkStencilOp_IncrWrap;
  case GL_DECR_WRAP:
    return DkStencilOp_DecrWrap;
  default:
    return DkStencilOp_Keep;
  }
}

static DkBlendOp blend_op(GLenum e) {
  switch (e) {
  case GL_FUNC_SUBTRACT:
    return DkBlendOp_Sub;
  case GL_FUNC_REVERSE_SUBTRACT:
    return DkBlendOp_RevSub;
  case GL_MIN:
    return DkBlendOp_Min;
  case GL_MAX:
    return DkBlendOp_Max;
  default:
    return DkBlendOp_Add;
  }
}

static DkBlendFactor blend_factor(GLenum f) {
  switch (f) {
  case GL_ZERO:
    return DkBlendFactor_Zero;
  case GL_ONE:
    return DkBlendFactor_One;
  case GL_SRC_COLOR:
    return DkBlendFactor_SrcColor;
  case GL_ONE_MINUS_SRC_COLOR:
    return DkBlendFactor_InvSrcColor;
  case GL_SRC_ALPHA:
    return DkBlendFactor_SrcAlpha;
  case GL_ONE_MINUS_SRC_ALPHA:
    return DkBlendFactor_InvSrcAlpha;
  case GL_DST_ALPHA:
    return DkBlendFactor_DstAlpha;
  case GL_ONE_MINUS_DST_ALPHA:
    return DkBlendFactor_InvDstAlpha;
  case GL_DST_COLOR:
    return DkBlendFactor_DstColor;
  case GL_ONE_MINUS_DST_COLOR:
    return DkBlendFactor_InvDstColor;
  case GL_SRC_ALPHA_SATURATE:
    return DkBlendFactor_SrcAlphaSaturate;
  case GL_CONSTANT_COLOR:
    return DkBlendFactor_ConstColor;
  case GL_ONE_MINUS_CONSTANT_COLOR:
    return DkBlendFactor_InvConstColor;
  case GL_CONSTANT_ALPHA:
    return DkBlendFactor_ConstAlpha;
  case GL_ONE_MINUS_CONSTANT_ALPHA:
    return DkBlendFactor_InvConstAlpha;
  default:
    return DkBlendFactor_One;
  }
}

static void apply_scissor(DkglContext *ctx, DkCmdBuf cmd) {
  DkScissor s;
  if (ctx->scissor_test) {
    s.x = (uint32_t)(ctx->scissor[0] > 0 ? ctx->scissor[0] : 0);
    s.y = (uint32_t)(ctx->scissor[1] > 0 ? ctx->scissor[1] : 0);
    s.width = (uint32_t)(ctx->scissor[2] > 0 ? ctx->scissor[2] : 0);
    s.height = (uint32_t)(ctx->scissor[3] > 0 ? ctx->scissor[3] : 0);
    if (s.x + s.width > 0xffff)
      s.width = 0xffff - s.x;
    if (s.y + s.height > 0xffff)
      s.height = 0xffff - s.y;
  } else {
    s.x = s.y = 0;
    s.width = s.height = 0xffff;
  }
  if (!gs.scissor_valid || memcmp(&s, &gs.scissor, sizeof(s))) {
    dkCmdBufSetScissors(cmd, 0, &s, 1);
    gs.scissor = s;
    gs.scissor_valid = true;
  }
}

void dkgl_apply_clear_scissor(DkglContext *ctx) {
  if (gs.ctx != ctx) {
    gs.ctx = ctx;
    ctx->dirty = DKGL_DIRTY_ALL;
  }
  apply_scissor(ctx, dkgl_cmd());
}

static void apply_fixed_function(DkglContext *ctx, DkCmdBuf cmd) {
  const uint32_t dirty = ctx->dirty;
  if (dirty & DKGL_DIRTY_RASTER || !gs.raster_valid) {
    DkRasterizerState r;
    memset(&r, 0, sizeof(r));
    dkRasterizerStateDefaults(&r);
    r.rasterizerEnable = !ctx->rasterizer_discard;
    r.cullMode = !ctx->cull_face                  ? DkFace_None
                 : ctx->cull_mode == GL_FRONT     ? DkFace_Front
                 : ctx->cull_mode == GL_BACK      ? DkFace_Back
                                                  : DkFace_FrontAndBack;
    r.frontFace = ctx->front_face == GL_CW ? DkFrontFace_CW : DkFrontFace_CCW;
    r.depthBiasEnableMask = ctx->polygon_offset_fill ? DkPolygonFlag_All : 0;
    if (!gs.raster_valid || memcmp(&r, &gs.raster, sizeof(r))) {
      dkCmdBufBindRasterizerState(cmd, &r);
      gs.raster = r;
      gs.raster_valid = true;
    }
  }
  if (dirty & DKGL_DIRTY_BLEND || !gs.color_valid || !gs.blend_valid) {
    DkColorState c;
    memset(&c, 0, sizeof(c));
    dkColorStateDefaults(&c);
    c.blendEnableMask = ctx->blend_enable;
    if (!gs.color_valid || memcmp(&c, &gs.color, sizeof(c))) {
      dkCmdBufBindColorState(cmd, &c);
      gs.color = c;
      gs.color_valid = true;
    }
    DkBlendState b[DKGL_DRAW_BUFFERS];
    memset(b, 0, sizeof(b));
    for (int i = 0; i < DKGL_DRAW_BUFFERS; i++) {
      const DkglBlend *gl = &ctx->blend[i];
      b[i].colorBlendOp = blend_op(gl->eq_rgb);
      b[i].alphaBlendOp = blend_op(gl->eq_a);
      b[i].srcColorBlendFactor = blend_factor(gl->src_rgb);
      b[i].dstColorBlendFactor = blend_factor(gl->dst_rgb);
      b[i].srcAlphaBlendFactor = blend_factor(gl->src_a);
      b[i].dstAlphaBlendFactor = blend_factor(gl->dst_a);
    }
    if (!gs.blend_valid || memcmp(b, gs.blend, sizeof(b))) {
      dkCmdBufBindBlendStates(cmd, 0, b, DKGL_DRAW_BUFFERS);
      memcpy(gs.blend, b, sizeof(b));
      gs.blend_valid = true;
    }
  }
  if (dirty & (DKGL_DIRTY_COLOR_WRITE | DKGL_DIRTY_FRAMEBUFFER) || !gs.write_valid) {
    DkColorWriteState w;
    w.masks = 0;
    for (int i = 0; i < DKGL_DRAW_BUFFERS; i++)
      w.masks |= (uint32_t)(ctx->color_mask[i] & 0xf) << (4 * i);
    w.masks &= dkgl_rt_real_mask;
    if (!gs.write_valid || w.masks != gs.write.masks) {
      dkCmdBufBindColorWriteState(cmd, &w);
      gs.write = w;
      gs.write_valid = true;
    }
  }
  if (dirty & (DKGL_DIRTY_DEPTH_STENCIL | DKGL_DIRTY_FRAMEBUFFER) || !gs.ds_valid) {
    DkDepthStencilState d;
    memset(&d, 0, sizeof(d));
    dkDepthStencilStateDefaults(&d);
    d.depthTestEnable = ctx->depth_test && dkgl_rt_has_depth;
    d.depthWriteEnable = ctx->depth_test && ctx->depth_mask && dkgl_rt_has_depth;
    d.depthCompareOp = cmp(ctx->depth_func);
    d.stencilTestEnable = ctx->stencil_test && dkgl_rt_has_stencil;
    d.stencilFrontFailOp = stencil_op(ctx->stencil[0].fail);
    d.stencilFrontDepthFailOp = stencil_op(ctx->stencil[0].zfail);
    d.stencilFrontPassOp = stencil_op(ctx->stencil[0].zpass);
    d.stencilFrontCompareOp = cmp(ctx->stencil[0].func);
    d.stencilBackFailOp = stencil_op(ctx->stencil[1].fail);
    d.stencilBackDepthFailOp = stencil_op(ctx->stencil[1].zfail);
    d.stencilBackPassOp = stencil_op(ctx->stencil[1].zpass);
    d.stencilBackCompareOp = cmp(ctx->stencil[1].func);
    if (!gs.ds_valid || memcmp(&d, &gs.ds, sizeof(d))) {
      dkCmdBufBindDepthStencilState(cmd, &d);
      gs.ds = d;
      gs.ds_valid = true;
    }
  }
  if (dirty & DKGL_DIRTY_STENCIL_REF || !gs.stencil_valid) {
    for (int f = 0; f < 2; f++) {
      uint8_t v[3] = {(uint8_t)ctx->stencil[f].write_mask,
                      (uint8_t)ctx->stencil[f].ref,
                      (uint8_t)ctx->stencil[f].func_mask};
      if (!gs.stencil_valid || memcmp(v, gs.stencil[f], 3)) {
        dkCmdBufSetStencil(cmd, f ? DkFace_Back : DkFace_Front, v[0], v[1], v[2]);
        memcpy(gs.stencil[f], v, 3);
      }
    }
    gs.stencil_valid = true;
  }
  if (dirty & DKGL_DIRTY_VIEWPORT || !gs.viewport_valid) {
    DkViewport v = {(float)ctx->viewport[0], (float)ctx->viewport[1],
                    (float)ctx->viewport[2], (float)ctx->viewport[3],
                    ctx->depth_range[0], ctx->depth_range[1]};
    if (!gs.viewport_valid || memcmp(&v, &gs.viewport, sizeof(v))) {
      dkCmdBufSetViewports(cmd, 0, &v, 1);
      gs.viewport = v;
      gs.viewport_valid = true;
    }
  }
  apply_scissor(ctx, cmd);
  if (dirty & DKGL_DIRTY_DEPTH_BIAS || !gs.bias_valid) {
    float b[3] = {ctx->polygon_offset_units, 0.0f, ctx->polygon_offset_factor};
    if (!gs.bias_valid || memcmp(b, gs.bias, sizeof(b))) {
      dkCmdBufSetDepthBias(cmd, b[0], b[1], b[2]);
      memcpy(gs.bias, b, sizeof(b));
      gs.bias_valid = true;
    }
  }
  if (dirty & DKGL_DIRTY_BLEND_CONST || !gs.blend_const_valid) {
    if (!gs.blend_const_valid ||
        memcmp(ctx->blend_color, gs.blend_const, sizeof(gs.blend_const))) {
      dkCmdBufSetBlendConst(cmd, ctx->blend_color[0], ctx->blend_color[1],
                            ctx->blend_color[2], ctx->blend_color[3]);
      memcpy(gs.blend_const, ctx->blend_color, sizeof(gs.blend_const));
      gs.blend_const_valid = true;
    }
  }
  if (dirty & DKGL_DIRTY_MULTISAMPLE || !gs.ms_valid) {
    if (!gs.ms_valid || gs.alpha_to_coverage != ctx->alpha_to_coverage) {
      DkMultisampleState ms;
      memset(&ms, 0, sizeof(ms));
      dkMultisampleStateDefaults(&ms);
      ms.alphaToCoverageEnable = ctx->alpha_to_coverage;
      dkCmdBufBindMultisampleState(cmd, &ms);
      gs.alpha_to_coverage = ctx->alpha_to_coverage;
      gs.ms_valid = true;
    }
  }
  if (dirty & DKGL_DIRTY_LINE || !gs.line_valid) {
    if (!gs.line_valid || gs.line_width != ctx->line_width) {
      dkCmdBufSetLineWidth(cmd, ctx->line_width);
      gs.line_width = ctx->line_width;
      gs.line_valid = true;
    }
  }
}

static void apply_restart(DkglContext *ctx, DkCmdBuf cmd, GLenum index_type) {
  const bool enable = ctx->primitive_restart && index_type;
  const uint32_t index = index_type == GL_UNSIGNED_BYTE    ? 0xffu
                         : index_type == GL_UNSIGNED_SHORT ? 0xffffu
                                                           : 0xffffffffu;
  if (!gs.restart_valid || gs.restart != enable ||
      (enable && gs.restart_index != index)) {
    dkCmdBufSetPrimitiveRestart(cmd, enable, index);
    gs.restart = enable;
    gs.restart_index = index;
    gs.restart_valid = true;
  }
}

/* Texture and image handles for one stage of the program. */
static void bind_stage_resources(DkglContext *ctx, DkglProgram *prog, int s,
                                 DkStage stage, DkCmdBuf cmd, bool force) {
  const int n = prog->num_tex[s];
  if (n > 0 || force) {
    DkResHandle handles[32];
    for (int b = 0; b < n; b++) {
      const int slot = prog->tex[s][b].slot;
      if (slot < 0) {
        handles[b] = dkgl_texture_handle(ctx, DKGL_TEXTURE_UNITS, 0, false);
        continue;
      }
      const DkglUniformSlot *u = &prog->slots[slot];
      const uint8_t unit = u->units[prog->tex[s][b].element];
      handles[b] = dkgl_texture_handle(ctx, unit, prog->tex[s][b].target, false);
    }
    int first = -1, last = -1;
    for (int b = 0; b < n; b++) {
      if (!(gs.tex_valid[stage] && b < 32 && gs.tex[stage][b] == handles[b])) {
        if (first < 0)
          first = b;
        last = b;
      }
    }
    if (first >= 0) {
      dkCmdBufBindTextures(cmd, stage, (uint32_t)first, &handles[first],
                           (uint32_t)(last - first + 1));
      memcpy(&gs.tex[stage][first], &handles[first],
             (size_t)(last - first + 1) * sizeof(DkResHandle));
    }
    gs.tex_valid[stage] = 1;
  }
  const int ni = prog->num_img[s];
  if (ni > 0) {
    DkResHandle handles[8];
    for (int b = 0; b < ni; b++) {
      const int slot = prog->img[s][b].slot;
      const uint8_t unit =
          slot >= 0 ? prog->slots[slot].units[prog->img[s][b].element] : 0;
      handles[b] = dkgl_image_handle(ctx, unit, prog->img[s][b].target);
    }
    if (!gs.img_valid[stage] || memcmp(handles, gs.img[stage], (size_t)ni * sizeof(DkResHandle))) {
      dkCmdBufBindImages(cmd, stage, 0, handles, (uint32_t)ni);
      memcpy(gs.img[stage], handles, (size_t)ni * sizeof(DkResHandle));
      gs.img_valid[stage] = 1;
    }
  }
}

static bool vertex_format(const DkglAttrib *a, DkVtxAttribState *out) {
  const int comps = a->size == 5 ? 4 : a->size;
  const GLenum t = a->type;
  if (t == GL_INT_2_10_10_10_REV || t == GL_UNSIGNED_INT_2_10_10_10_REV) {
    out->size = DkVtxAttribSize_10_10_10_2;
  } else {
    static const DkVtxAttribSize s32[4] = {DkVtxAttribSize_1x32, DkVtxAttribSize_2x32,
                                           DkVtxAttribSize_3x32, DkVtxAttribSize_4x32};
    static const DkVtxAttribSize s16[4] = {DkVtxAttribSize_1x16, DkVtxAttribSize_2x16,
                                           DkVtxAttribSize_3x16, DkVtxAttribSize_4x16};
    static const DkVtxAttribSize s8[4] = {DkVtxAttribSize_1x8, DkVtxAttribSize_2x8,
                                          DkVtxAttribSize_3x8, DkVtxAttribSize_4x8};
    if (comps < 1 || comps > 4)
      return false;
    const uint32_t bytes = type_size(t);
    out->size = bytes == 1 ? s8[comps - 1] : bytes == 2 ? s16[comps - 1] : s32[comps - 1];
  }
  const bool is_signed = t == GL_BYTE || t == GL_SHORT || t == GL_INT ||
                         t == GL_INT_2_10_10_10_REV;
  if (t == GL_FLOAT || t == GL_HALF_FLOAT || t == GL_HALF_FLOAT_OES)
    out->type = DkVtxAttribType_Float;
  else if (a->integer)
    out->type = is_signed ? DkVtxAttribType_Sint : DkVtxAttribType_Uint;
  else if (a->normalized)
    out->type = is_signed ? DkVtxAttribType_Snorm : DkVtxAttribType_Unorm;
  else
    out->type = is_signed ? DkVtxAttribType_Sscaled : DkVtxAttribType_Uscaled;
  out->isBgra = a->size == 5;
  return true;
}

/* Largest index referenced (for client-memory vertex arrays). */
static uint32_t max_index(GLenum type, const void *indices, GLsizei count) {
  uint32_t m = 0;
  for (GLsizei i = 0; i < count; i++) {
    uint32_t v = type == GL_UNSIGNED_BYTE    ? ((const uint8_t *)indices)[i]
                 : type == GL_UNSIGNED_SHORT ? ((const uint16_t *)indices)[i]
                                             : ((const uint32_t *)indices)[i];
    if (v > m)
      m = v;
  }
  return m;
}

static bool bind_vertex_input(DkglContext *ctx, DkglProgram *prog, DkCmdBuf cmd,
                              uint32_t vertex_end, uint32_t instance_count) {
  DkglVao *vao = ctx->vao;
  DkVtxAttribState attribs[32];
  memset(attribs, 0, sizeof(attribs));
  uint32_t num_attribs = 0;
  uint32_t used_bindings = 0;
  uint32_t const_mask = 0;
  for (int loc = 0; loc < 32; loc++) {
    if (!(prog->input_mask & (1u << loc)))
      continue;
    num_attribs = (uint32_t)loc + 1;
    DkVtxAttribState *s = &attribs[loc];
    const int gi = prog->attr_index[loc]; /* the GL attribute bound to it */
    if (gi < DKGL_ATTRIBS && vao->attribs[gi].enabled) {
      const DkglAttrib *a = &vao->attribs[gi];
      const DkglVertexBinding *b = &vao->bindings[a->binding];
      if ((b->buffer || b->client) && vertex_format(a, s) && a->binding != CONST_VB) {
        s->bufferId = a->binding;
        s->offset = a->relative_offset & 0x3fff;
        used_bindings |= 1u << a->binding;
        continue;
      }
    }
    /* current attribute value from the constant slot */
    s->bufferId = CONST_VB;
    s->offset = (uint32_t)gi * 16;
    s->size = DkVtxAttribSize_4x32;
    s->type = prog->input_base[loc] == DKGL_BASE_INT    ? DkVtxAttribType_Sint
              : prog->input_base[loc] == DKGL_BASE_UINT ? DkVtxAttribType_Uint
                                                        : DkVtxAttribType_Float;
    const_mask |= 1u << loc;
  }
  /* Locations the shader does not read, including gaps below the highest
   * one, get the fixed state deko3d pads with: hardware rejects an all-zero
   * VertexAttribState (no size/type) with a GPU method error. */
  for (int loc = 0; loc < 32; loc++) {
    if (prog->input_mask & (1u << loc))
      continue;
    attribs[loc].isFixed = 1;
    attribs[loc].size = DkVtxAttribSize_1x32;
    attribs[loc].type = DkVtxAttribType_Float;
  }
  if (!gs.attribs_valid || gs.num_attribs != num_attribs ||
      memcmp(attribs, gs.attribs, num_attribs * sizeof(DkVtxAttribState))) {
    /* Ryujinx accepts states the GPU faults on, so check them here. */
    for (uint32_t i = 0; i < num_attribs; i++)
      if (!attribs[i].size || !attribs[i].type)
        debugPrintf("dkgl: invalid vertex attribute state loc=%u prog=%u "
                    "size=%u type=%u\n",
                    i, prog->name, attribs[i].size, attribs[i].type);
    dkCmdBufBindVtxAttribState(cmd, attribs, num_attribs ? num_attribs : 1);
    memcpy(gs.attribs, attribs, sizeof(attribs));
    gs.num_attribs = num_attribs;
    gs.attribs_valid = true;
  }

  DkVtxBufferState states[DKGL_VBO_BINDINGS];
  DkBufExtents extents[DKGL_VBO_BINDINGS];
  memset(states, 0, sizeof(states));
  memset(extents, 0, sizeof(extents));
  uint32_t count = 0;
  for (int b = 0; b < DKGL_VBO_BINDINGS; b++) {
    if (!(used_bindings & (1u << b)))
      continue;
    count = (uint32_t)b + 1;
    const DkglVertexBinding *vb = &vao->bindings[b];
    states[b].stride = vb->stride;
    states[b].divisor = vb->divisor;
    if (vb->buffer) {
      DkglBuffer *buf = dkgl_buffer(vb->buffer, false);
      if (!buf || !buf->mem.node || (uint64_t)vb->offset >= buf->size)
        continue;
      extents[b].addr = dkgl_gpu(&buf->mem) + (uint64_t)vb->offset;
      extents[b].size = buf->size - (uint32_t)vb->offset;
      dkgl_buffer_used(buf);
    } else if (vb->client) {
      uint32_t elements = vb->divisor ? (instance_count + vb->divisor - 1) / vb->divisor
                                      : vertex_end;
      uint32_t bytes = elements * (vb->stride ? vb->stride : 16) + 16;
      DkglRing ring;
      if (!dkgl_ring_alloc(bytes, 16, &ring))
        continue;
      memcpy(ring.cpu, vb->client, bytes - 16);
      extents[b].addr = ring.gpu;
      extents[b].size = bytes;
      gs.vb_valid = false;
    }
  }
  if (const_mask) {
    if (gs.const_serial != dkg.serial || (ctx->dirty & DKGL_DIRTY_VERTEX) || !gs.const_addr) {
      DkglRing ring;
      if (dkgl_ring_alloc(32 * 16, 16, &ring)) {
        memset(ring.cpu, 0, 32 * 16);
        for (int loc = 0; loc < DKGL_ATTRIBS; loc++)
          memcpy((uint8_t *)ring.cpu + loc * 16, ctx->current_attrib[loc].f, 16);
        gs.const_addr = ring.gpu;
        gs.const_serial = dkg.serial;
      }
    }
    count = CONST_VB + 1;
    states[CONST_VB].stride = 0;
    states[CONST_VB].divisor = 0;
    extents[CONST_VB].addr = gs.const_addr;
    extents[CONST_VB].size = 32 * 16;
  }
  if (!count)
    return true;
  if (!gs.vb_valid || gs.vb_count != count ||
      memcmp(states, gs.vb_state, count * sizeof(DkVtxBufferState))) {
    dkCmdBufBindVtxBufferState(cmd, states, count);
    memcpy(gs.vb_state, states, sizeof(states));
  }
  if (!gs.vb_valid || gs.vb_count != count ||
      memcmp(extents, gs.vb, count * sizeof(DkBufExtents))) {
    dkCmdBufBindVtxBuffers(cmd, 0, extents, count);
    memcpy(gs.vb, extents, sizeof(extents));
  }
  gs.vb_count = count;
  gs.vb_valid = true;
  return true;
}

/* Everything a draw needs bound. Returns the program or NULL to skip. */
/* One line per draw/dispatch while dkgl_trace_on: program, target,
 * viewport, scissor and the textures each sampler reads. */
void dkgl_trace_queue_texture(DkglTexture *t); /* dkgl_egl.c */
static void trace_call(DkglContext *ctx, DkglProgram *prog, int stage,
                       const char *what, uint32_t a, uint32_t b) {
  char tex[768];
  size_t o = 0;
  tex[0] = '\0';
  for (int i = 0; i < prog->num_tex[stage] && o + 96 < sizeof(tex); i++) {
    const int slot = prog->tex[stage][i].slot;
    if (slot < 0)
      continue;
    const uint8_t unit = prog->slots[slot].units[prog->tex[stage][i].element];
    const int target = prog->tex[stage][i].target;
    DkglTexture *t = dkgl_texture(ctx->textures[unit][target], false);
    if (dkgl_trace_on)
      dkgl_trace_queue_texture(t);
    o += (size_t)snprintf(tex + o, sizeof(tex) - o,
                          " u%u=%u(%ux%ux%u f%x w%x/%x s%u L%u d%x%s)", unit, t ? t->name : 0,
                          t ? t->width : 0, t ? t->height : 0, t ? t->depth : 0,
                          t ? t->internal_format : 0, t ? t->sampler.wrap_s : 0,
                          t ? t->sampler.wrap_t : 0, ctx->samplers[unit],
                          t ? t->levels : 0, t ? t->level_defined : 0, t && t->immutable ? " imm" : "");
  }
  for (int i = 0; i < prog->num_img[stage] && o + 64 < sizeof(tex); i++) {
    const int slot = prog->img[stage][i].slot;
    const uint8_t unit = slot >= 0 ? prog->slots[slot].units[prog->img[stage][i].element] : 0;
    o += (size_t)snprintf(tex + o, sizeof(tex) - o, " img%u=%u", unit,
                          ctx->images[unit].texture);
  }
  uint64_t hashes[2] = {0, 0};
  for (int st = 0; st < 2; st++) {
    DkglStageBinary *bin = prog->bin[st];
    if (!bin)
      continue;
    hashes[st] = bin->hash;
    if (bin->trace_source && !bin->trace_dumped) {
      bin->trace_dumped = true;
      char name[64], path[700];
      snprintf(name, sizeof(name), "dkgl_trace/%016llx.glsl",
               (unsigned long long)bin->hash);
      snprintf(path, sizeof(path), "%s", dkgl_path(name));
      FILE *f = fopen(path, "wb");
      if (f) {
        fputs(bin->trace_source, f);
        fclose(f);
      }
    }
  }
  debugPrintf("trace %s %u/%u prog=%u vs=%016llx fs=%016llx fbo=%u "
              "vp=%d,%d,%d,%d sc=%d:%d,%d,%d,%d blend=%u depth=%d/%d%s\n",
              what, a, b, prog->name, (unsigned long long)hashes[0],
              (unsigned long long)hashes[1], ctx->draw_fbo, ctx->viewport[0],
              ctx->viewport[1], ctx->viewport[2], ctx->viewport[3],
              ctx->scissor_test, ctx->scissor[0], ctx->scissor[1],
              ctx->scissor[2], ctx->scissor[3], ctx->blend_enable,
              ctx->depth_test, ctx->depth_mask, tex);
}

/* Second trace line for a draw: what its vertex stage reads. Texture buffers
 * show their format, range and first eight values; instanced or divisor
 * draws also list each attribute's layout. */
static void trace_vertex_inputs(DkglContext *ctx, DkglProgram *prog,
                                uint32_t instances) {
  char line[640];
  size_t o = 0;
  line[0] = '\0';
  for (int i = 0; i < prog->num_tex[0] && o + 200 < sizeof(line); i++) {
    const int slot = prog->tex[0][i].slot;
    if (slot < 0)
      continue;
    const uint8_t unit = prog->slots[slot].units[prog->tex[0][i].element];
    const int target = prog->tex[0][i].target;
    DkglTexture *t = dkgl_texture(ctx->textures[unit][target], false);
    if (!t) {
      o += (size_t)snprintf(line + o, sizeof(line) - o, " vu%u=none", unit);
      continue;
    }
    if (target != DKGL_TEX_BUFFER) {
      o += (size_t)snprintf(line + o, sizeof(line) - o, " vu%u=%u(%ux%u f%x)",
                            unit, t->name, t->width, t->height,
                            t->internal_format);
      continue;
    }
    DkglBuffer *buf = dkgl_buffer(t->tbo_buffer, false);
    o += (size_t)snprintf(line + o, sizeof(line) - o,
                          " vu%u=tbo%u(f%x buf=%u off=%u size=%u of %u)", unit,
                          t->name, t->internal_format, t->tbo_buffer,
                          t->tbo_offset, t->tbo_size, buf ? buf->size : 0);
    if (buf && buf->mem.node && t->tbo_offset + 32 <= buf->size) {
      const float *v =
          (const float *)((const uint8_t *)dkgl_cpu(&buf->mem) + t->tbo_offset);
      o += (size_t)snprintf(line + o, sizeof(line) - o,
                            " [%g %g %g %g %g %g %g %g]", v[0], v[1], v[2],
                            v[3], v[4], v[5], v[6], v[7]);
    }
  }
  DkglVao *vao = ctx->vao;
  bool instanced = instances > 1;
  for (int loc = 0; loc < DKGL_ATTRIBS && !instanced; loc++)
    if ((prog->input_mask & (1u << loc)) && vao->attribs[loc].enabled &&
        vao->bindings[vao->attribs[loc].binding].divisor)
      instanced = true;
  for (int loc = 0; instanced && loc < DKGL_ATTRIBS && o + 96 < sizeof(line); loc++) {
    if (!(prog->input_mask & (1u << loc)))
      continue;
    const DkglAttrib *a = &vao->attribs[loc];
    if (!a->enabled) {
      o += (size_t)snprintf(line + o, sizeof(line) - o, " a%d=const", loc);
      continue;
    }
    const DkglVertexBinding *b = &vao->bindings[a->binding];
    DkglBuffer *buf = b->buffer ? dkgl_buffer(b->buffer, false) : NULL;
    o += (size_t)snprintf(line + o, sizeof(line) - o,
                          " a%d=%ux%x%s%s off=%u b%u(stride=%u div=%u %s%u)",
                          loc, a->size, a->type, a->normalized ? "n" : "",
                          a->integer ? "i" : "", a->relative_offset,
                          a->binding, b->stride, b->divisor,
                          b->client ? "client" : "buf",
                          buf ? buf->size : 0);
  }
  if (o)
    debugPrintf("trace   inputs%s\n", line);
}

/* Third trace line, for depth-writing (3D) draws (mvl_nx, HW round 2: the
 * players and enemies drew nothing): the vertex stage's GL-buffer uniform
 * blocks with their first 16 floats (UnityPerDraw: unity_ObjectToWorld), the
 * loose block's last 16 (unity_MatrixVP in URP's shaders) and the first
 * vertex's position. */
static void trace_3d(DkglContext *ctx, DkglProgram *prog) {
  char line[1800];
  size_t o = 0;
  line[0] = '\0';
  for (int st = 0; st < 2; st++) {
  const DkglGlslResult *r = prog->bin[st] ? prog->bin[st]->refl : NULL;
  for (int k = 0; r && k < r->num_blocks && k < 16 && o + 300 < sizeof(line); k++) {
    const int idx = prog->blk_index[st][k];
    if (st == 1 && prog->bin[0]) { /* the fragment's own blocks only */
      bool in_vs = false;
      for (int j = 0; j < 16; j++)
        in_vs |= prog->blk_index[0][j] == idx;
      if (in_vs)
        continue;
    }
    const GLuint point = idx >= 0 ? prog->ubo_block_binding[idx] : 0xffff;
    o += (size_t)snprintf(line + o, sizeof(line) - o, " %s(dk%u gl#%d point %u", r->blocks[k].name,
                          r->blocks[k].binding, idx, point);
    if (point < DKGL_UBO_BINDINGS) {
      DkglBuffer *buf = dkgl_buffer(ctx->ubo_bindings[point].buffer, false);
      const uint32_t off = (uint32_t)ctx->ubo_bindings[point].offset;
      o += (size_t)snprintf(line + o, sizeof(line) - o, " buf=%u off=%u size=%ld of %u",
                            ctx->ubo_bindings[point].buffer, off, (long)ctx->ubo_bindings[point].size,
                            buf ? buf->size : 0);
      if (buf && buf->mem.node && off + 64 <= buf->size) {
        const float *v = (const float *)((const uint8_t *)dkgl_cpu(&buf->mem) + off);
        o += (size_t)snprintf(line + o, sizeof(line) - o, " [");
        for (int i = 0; i < 16; i++)
          o += (size_t)snprintf(line + o, sizeof(line) - o, "%s%.3g", i ? " " : "", v[i]);
        o += (size_t)snprintf(line + o, sizeof(line) - o, "]");
      }
    }
    o += (size_t)snprintf(line + o, sizeof(line) - o, ")");
  }
  }
  /* the vertex stage's loose uniforms: the first binding that has any (an
   * explicit block can hold binding 0, as Unity's UnityPerDraw does) */
  const DkglUbo *u = &prog->ubo[0][0];
  for (int b = 0; b < prog->num_ubo[0] && o + 160 < sizeof(line); b++) {
    const DkglUbo *l = &prog->ubo[0][b];
    if (!l->size || !l->shadow)
      continue;
    const float *v = (const float *)l->shadow;
    o += (size_t)snprintf(line + o, sizeof(line) - o, " loose@%d(%u)=[", b, l->size);
    for (int i = 0; i < 8 && 4 * (uint32_t)(i + 1) <= l->size; i++)
      o += (size_t)snprintf(line + o, sizeof(line) - o, "%s%.3g", i ? " " : "", v[i]);
    o += (size_t)snprintf(line + o, sizeof(line) - o, "]");
    if (l->size > u->size)
      u = l; /* the tail printed below: the biggest (the shared block) */
  }
  if (prog->num_ubo[0] > 0 && u->shadow && u->size > 64 && o + 200 < sizeof(line)) {
    /* its start too: _TimeParameters, the first matrix rows */
    const float *v = (const float *)u->shadow;
    o += (size_t)snprintf(line + o, sizeof(line) - o, " loose%u[0..]=[", u->size);
    for (int i = 0; i < 8; i++)
      o += (size_t)snprintf(line + o, sizeof(line) - o, "%s%.3g", i ? " " : "", v[i]);
    o += (size_t)snprintf(line + o, sizeof(line) - o, "]");
  }
  if (prog->num_ubo[0] > 0 && u->shadow && u->size >= 64 && o + 200 < sizeof(line)) {
    const float *v = (const float *)(u->shadow + u->size - 64);
    o += (size_t)snprintf(line + o, sizeof(line) - o, " loose%u[-64..]=[", u->size);
    for (int i = 0; i < 16; i++)
      o += (size_t)snprintf(line + o, sizeof(line) - o, "%s%.3g", i ? " " : "", v[i]);
    o += (size_t)snprintf(line + o, sizeof(line) - o, "]");
  }
  DkglVao *vao = ctx->vao;
  const DkglAttrib *a = &vao->attribs[0];
  if (vao && (prog->input_mask & 1u) && o + 160 < sizeof(line)) {
    const DkglVertexBinding *b = &vao->bindings[a->binding];
    DkglBuffer *buf = b->buffer ? dkgl_buffer(b->buffer, false) : NULL;
    o += (size_t)snprintf(line + o, sizeof(line) - o, " a0=%s %ux%x off=%u b%u(buf %u +%ld stride %u of %u)",
                          a->enabled ? "on" : "const", a->size, a->type, a->relative_offset, a->binding,
                          b->buffer, (long)b->offset, b->stride, buf ? buf->size : 0);
    const uint32_t at = (uint32_t)b->offset + a->relative_offset;
    if (a->enabled && buf && buf->mem.node && a->type == GL_FLOAT && at + 12 <= buf->size) {
      const float *v = (const float *)((const uint8_t *)dkgl_cpu(&buf->mem) + at);
      o += (size_t)snprintf(line + o, sizeof(line) - o, " v0=[%g %g %g]", v[0], v[1], v[2]);
    }
    o += (size_t)snprintf(line + o, sizeof(line) - o, " ebo=%u cull=%d/%x front=%x df=%x",
                          vao->element_buffer, ctx->cull_face, ctx->cull_mode, ctx->front_face,
                          ctx->depth_func);
  }
  if (o)
    debugPrintf("trace   3d%s\n", line);
}

static DkglProgram *validate_draw(DkglContext *ctx, uint32_t vertex_end,
                                  uint32_t instances) {
  DkglProgram *prog = dkgl_program(ctx->program);
  if (!prog || !prog->link_status || prog->compute)
    return NULL;
  if (gs.ctx != ctx) {
    gs.ctx = ctx;
    ctx->dirty = DKGL_DIRTY_ALL;
  }
  if (!dkgl_bind_draw_targets(ctx))
    return NULL;
  dkgl_before_3d();
  DkCmdBuf cmd = dkgl_cmd();
  bool program_changed = false;
  const DkShader *vs = dkgl_binary_shader_wait(prog->bin[0], 2000);
  const DkShader *fs = dkgl_binary_shader_wait(prog->bin[1], 2000);
  if (!vs || !fs) {
    if (debug_rate_limit("dkgl draw: shader", "", 8))
      debugPrintf("dkgl: draw dropped: program %u's %s did not compile\n", prog->name,
                  !vs ? "vertex shader" : "fragment shader");
    return NULL;
  }
  if (gs.shader_uid[0] != prog->bin[0]->uid || gs.shader_uid[1] != prog->bin[1]->uid) {
    const DkShader *list[2] = {vs, fs};
    dkCmdBufBindShaders(cmd, DkStageFlag_Vertex | DkStageFlag_Fragment, list, 2);
    gs.shader_uid[0] = prog->bin[0]->uid;
    gs.shader_uid[1] = prog->bin[1]->uid;
    program_changed = true;
  }
  apply_fixed_function(ctx, cmd);
  dkgl_program_bind_uniforms(ctx, prog, program_changed);
  const bool tex_dirty = (ctx->dirty & (DKGL_DIRTY_TEXTURES | DKGL_DIRTY_IMAGES |
                                        DKGL_DIRTY_PROGRAM)) ||
                         gs.texture_epoch != dkgl_texture_epoch || program_changed;
  bool has_tbo = false;
  for (int s = 0; s < 2 && !has_tbo; s++)
    for (int b = 0; b < prog->num_tex[s]; b++)
      if (prog->tex[s][b].target == DKGL_TEX_BUFFER)
        has_tbo = true;
  if (tex_dirty || has_tbo || prog->num_img[0] || prog->num_img[1]) {
    bind_stage_resources(ctx, prog, 0, DkStage_Vertex, cmd, false);
    bind_stage_resources(ctx, prog, 1, DkStage_Fragment, cmd, false);
    gs.texture_epoch = dkgl_texture_epoch;
  }
  bind_vertex_input(ctx, prog, cmd, vertex_end, instances);
  ctx->dirty = 0;
  prog->last_use = dkg.serial;
  last_draw_prog = prog->name;
  last_draw_fs = prog->bin[1] ? prog->bin[1]->hash : 0;
  if (dkgl_trace_on) {
    trace_call(ctx, prog, 1, "draw", vertex_end, instances);
    trace_vertex_inputs(ctx, prog, instances);
    trace_3d(ctx, prog); /* 2D draws too: a sprite's blocks decide where it lands */
  } else if (!prog->first_drawn) {
    prog->first_drawn = true;
    trace_call(ctx, prog, 1, "first", vertex_end, instances);
    if (dkgl_trace_enabled && ctx->depth_test && ctx->depth_mask)
      trace_3d(ctx, prog);
  }
  return prog;
}

static void after_draw(void) {
  dkg.rt_written = true;
  dkgl_prof_draw(last_draw_prog, last_draw_fs);
  dkgl_note_draw();
}

static bool draw_begin(DkglContext **out) {
  DkglContext *ctx = dkgl_ctx();
  *out = ctx;
  return ctx != NULL;
}

void glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count,
                           GLsizei instancecount) {
  if (count <= 0 || instancecount <= 0 || first < 0)
    return;
  DKGL_LOCK();
  DkglContext *ctx;
  if (draw_begin(&ctx) &&
      validate_draw(ctx, (uint32_t)(first + count), (uint32_t)instancecount)) {
    DkCmdBuf cmd = dkgl_cmd();
    apply_restart(ctx, cmd, 0);
    dkCmdBufDraw(cmd, (DkPrimitive)mode, (uint32_t)count,
                 (uint32_t)instancecount, (uint32_t)first, 0);
    after_draw();
  }
  DKGL_UNLOCK();
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
  glDrawArraysInstanced(mode, first, count, 1);
}

static bool bind_indices(DkglContext *ctx, DkCmdBuf cmd, GLenum type,
                         const void *indices, GLsizei count,
                         uint32_t *first_index) {
  const uint32_t isize = type == GL_UNSIGNED_BYTE ? 1 : type == GL_UNSIGNED_SHORT ? 2 : 4;
  const DkIdxFormat fmt = type == GL_UNSIGNED_BYTE    ? DkIdxFormat_Uint8
                          : type == GL_UNSIGNED_SHORT ? DkIdxFormat_Uint16
                                                      : DkIdxFormat_Uint32;
  DkGpuAddr addr;
  DkglBuffer *ebo = dkgl_buffer(ctx->vao->element_buffer, false);
  if (ebo) {
    if (!ebo->mem.node)
      return false;
    const uintptr_t offset = (uintptr_t)indices;
    if (offset % isize) {
      addr = dkgl_gpu(&ebo->mem) + offset;
      *first_index = 0;
    } else {
      addr = dkgl_gpu(&ebo->mem);
      *first_index = (uint32_t)(offset / isize);
    }
    dkgl_buffer_used(ebo);
  } else {
    if (!indices)
      return false;
    DkglRing ring;
    if (!dkgl_ring_alloc((uint32_t)count * isize, 16, &ring))
      return false;
    memcpy(ring.cpu, indices, (size_t)count * isize);
    addr = ring.gpu;
    *first_index = 0;
  }
  if (!gs.idx_valid || gs.idx_addr != addr || gs.idx_format != fmt) {
    dkCmdBufBindIdxBuffer(cmd, fmt, addr);
    gs.idx_addr = addr;
    gs.idx_format = fmt;
    gs.idx_valid = true;
  }
  return true;
}

static uint32_t client_vertex_end(DkglContext *ctx, GLenum type,
                                  const void *indices, GLsizei count) {
  /* Only client-memory arrays need the index range. */
  bool client = false;
  for (int b = 0; b < DKGL_VBO_BINDINGS; b++)
    if (ctx->vao->bindings[b].client)
      client = true;
  if (!client)
    return 0;
  DkglBuffer *ebo = dkgl_buffer(ctx->vao->element_buffer, false);
  const void *data = indices;
  if (ebo) {
    if (!ebo->mem.node)
      return 0;
    dkgl_buffer_wait_cpu(ebo);
    data = (const uint8_t *)dkgl_cpu(&ebo->mem) + (uintptr_t)indices;
  }
  return data ? max_index(type, data, count) + 1 : 0;
}

/* base_vertex: added to every index (ES 3.2 / OES_draw_elements_base_vertex,
 * which Unity's ES 2.0 renderer draws nearly everything with) */
static void draw_elements(GLenum mode, GLsizei count, GLenum type,
                          const void *indices, GLsizei instancecount,
                          GLint base_vertex) {
  if (count <= 0 || instancecount <= 0)
    return;
  DKGL_LOCK();
  DkglContext *ctx;
  if (draw_begin(&ctx)) {
    uint32_t vertex_end = client_vertex_end(ctx, type, indices, count);
    if (vertex_end)
      vertex_end += (uint32_t)(base_vertex > 0 ? base_vertex : 0);
    if (validate_draw(ctx, vertex_end, (uint32_t)instancecount)) {
      DkCmdBuf cmd = dkgl_cmd();
      uint32_t first = 0;
      if (bind_indices(ctx, cmd, type, indices, count, &first)) {
        apply_restart(ctx, cmd, type);
        dkCmdBufDrawIndexed(cmd, (DkPrimitive)mode, (uint32_t)count,
                            (uint32_t)instancecount, first, base_vertex, 0);
        after_draw();
      }
    }
  }
  DKGL_UNLOCK();
}

void glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type,
                             const void *indices, GLsizei instancecount) {
  draw_elements(mode, count, type, indices, instancecount, 0);
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type,
                    const void *indices) {
  draw_elements(mode, count, type, indices, 1, 0);
}

void glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type,
                              const void *indices, GLint basevertex) {
  draw_elements(mode, count, type, indices, 1, basevertex);
}

void glDrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type,
                                       const void *indices,
                                       GLsizei instancecount, GLint basevertex) {
  draw_elements(mode, count, type, indices, instancecount, basevertex);
}

void glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count,
                         GLenum type, const void *indices) {
  (void)start;
  (void)end;
  glDrawElementsInstanced(mode, count, type, indices, 1);
}

void glDrawArraysIndirect(GLenum mode, const void *indirect) {
  DKGL_LOCK();
  DkglContext *ctx;
  if (draw_begin(&ctx)) {
    DkglBuffer *buf = dkgl_buffer(ctx->draw_indirect, false);
    if (buf && buf->mem.node && validate_draw(ctx, 0, 1)) {
      DkCmdBuf cmd = dkgl_cmd();
      apply_restart(ctx, cmd, 0);
      dkCmdBufDrawIndirect(cmd, (DkPrimitive)mode,
                           dkgl_gpu(&buf->mem) + (uintptr_t)indirect);
      dkgl_buffer_used(buf);
      after_draw();
    } else if (!buf) {
      DKGL_ERROR(ctx, GL_INVALID_OPERATION);
    }
  }
  DKGL_UNLOCK();
}

void glDrawElementsIndirect(GLenum mode, GLenum type, const void *indirect) {
  DKGL_LOCK();
  DkglContext *ctx;
  if (draw_begin(&ctx)) {
    DkglBuffer *buf = dkgl_buffer(ctx->draw_indirect, false);
    DkglBuffer *ebo = dkgl_buffer(ctx->vao->element_buffer, false);
    if (buf && buf->mem.node && ebo && ebo->mem.node && validate_draw(ctx, 0, 1)) {
      DkCmdBuf cmd = dkgl_cmd();
      uint32_t first;
      bind_indices(ctx, cmd, type, NULL, 0, &first);
      apply_restart(ctx, cmd, type);
      dkCmdBufDrawIndexedIndirect(cmd, (DkPrimitive)mode,
                                  dkgl_gpu(&buf->mem) + (uintptr_t)indirect);
      dkgl_buffer_used(buf);
      after_draw();
    } else {
      DKGL_ERROR(ctx, GL_INVALID_OPERATION);
    }
  }
  DKGL_UNLOCK();
}

/* ------------------------------------------------------------ compute */

static DkglProgram *validate_compute(DkglContext *ctx) {
  DkglProgram *prog = dkgl_program(ctx->program);
  if (!prog || !prog->link_status || !prog->compute)
    return NULL;
  if (gs.ctx != ctx) {
    gs.ctx = ctx;
    ctx->dirty = DKGL_DIRTY_ALL;
  }
  /* rendering that the dispatch may read has to land first */
  dkgl_rt_barrier();
  dkgl_before_3d();
  DkCmdBuf cmd = dkgl_cmd();
  const DkShader *cs = dkgl_binary_shader(prog->bin[0]);
  if (!cs)
    return NULL;
  if (gs.compute_uid != prog->bin[0]->uid) {
    dkCmdBufBindShaders(cmd, DkStageFlag_Compute, &cs, 1);
    gs.compute_uid = prog->bin[0]->uid;
  }
  dkgl_program_bind_uniforms(ctx, prog, false);
  bind_stage_resources(ctx, prog, 0, DkStage_Compute, cmd, false);
  prog->last_use = dkg.serial;
  dkgl_prof_pass('D', 0, prog->name);
  if (dkgl_trace_on)
    trace_call(ctx, prog, 0, "dispatch", 0, 0);
  return prog;
}

void glDispatchCompute(GLuint num_groups_x, GLuint num_groups_y,
                       GLuint num_groups_z) {
  if (!num_groups_x || !num_groups_y || !num_groups_z)
    return;
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  if (ctx && validate_compute(ctx)) {
    dkCmdBufDispatchCompute(dkgl_cmd(), num_groups_x, num_groups_y, num_groups_z);
    dkg.stat_dispatches++;
  }
  DKGL_UNLOCK();
}

void glDispatchComputeIndirect(GLintptr indirect) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglBuffer *buf = ctx ? dkgl_buffer(ctx->dispatch_indirect, false) : NULL;
  if (ctx && buf && buf->mem.node && validate_compute(ctx)) {
    dkCmdBufDispatchComputeIndirect(dkgl_cmd(), dkgl_gpu(&buf->mem) + (uint64_t)indirect);
    dkgl_buffer_used(buf);
    dkg.stat_dispatches++;
  }
  DKGL_UNLOCK();
}

void glMemoryBarrier(GLbitfield barriers) {
  (void)barriers;
  DKGL_LOCK();
  if (dkg.initialized)
    dkCmdBufBarrier(dkgl_cmd(), DkBarrier_Primitives,
                    DkInvalidateFlags_Image | DkInvalidateFlags_Shader |
                        DkInvalidateFlags_L2Cache);
  DKGL_UNLOCK();
}

void glMemoryBarrierByRegion(GLbitfield barriers) { glMemoryBarrier(barriers); }
