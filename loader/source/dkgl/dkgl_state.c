/* dkgl_state.c -- fixed-function state, queries, syncs and glGet* */
#include "dkgl.h"

#include <stdlib.h>

#include "util.h"

DkCmdBuf dkgl_cmd(void);
void dkgl_context_init_vao(DkglContext *ctx);
extern uint32_t dkgl_rt_width, dkgl_rt_height;

/* The strings Mesa 20.1 reported on this hardware, so the engine's device
 * checks take the same branches as before. Mario Kart Tour: the extension
 * list is Mesa's own, entry for entry (Unity's ES 2.0 renderer reads it to
 * pick its paths; what it then called under Mesa is what dkgl covers). */
static const char gl_vendor[] = "nouveau";
static const char gl_renderer[] = "NV120";
/* mvl_nx: Unity 2022 is told ES 3.1. On 3.0 it binds URP's UnityPerDraw and
 * UnityPerMaterial blocks but never fills them (HW round 2, 2026-11-04: every
 * 3D model -- players, enemies -- drew with a zero object-to-world matrix and
 * vanished); on 3.1+ it declares the blocks' bindings in the shader
 * (UNITY_SUPPORTS_UNIFORM_LOCATION 1), reads them back
 * (GL_UNIFORM_BLOCK_BINDING) and writes each draw's data into mapped buffers
 * bound by range, as under Mesa. It still finds uniforms and samplers by
 * name; the ES 3.1 program interface queries answer "none" (as Mesa did for
 * its atomic counter and storage block queries). Shaders compile as ES 3.20
 * inside dkgl. */
static const char gl_version[] = "OpenGL ES 3.1 Mesa 20.1.0-rc3";
static const char gl_glsl[] = "OpenGL ES GLSL ES 3.10";

static const char *const gl_extension_list[] = {
    "GL_EXT_blend_minmax",
    "GL_EXT_multi_draw_arrays",
    "GL_EXT_texture_filter_anisotropic",
    "GL_EXT_texture_compression_s3tc",
    "GL_EXT_texture_compression_dxt1",
    "GL_EXT_texture_compression_rgtc",
    "GL_EXT_texture_format_BGRA8888",
    "GL_OES_compressed_ETC1_RGB8_texture",
    "GL_OES_depth24",
    "GL_OES_element_index_uint",
    "GL_OES_fbo_render_mipmap",
    "GL_OES_mapbuffer",
    "GL_OES_rgb8_rgba8",
    "GL_OES_standard_derivatives",
    "GL_OES_stencil8",
    "GL_OES_texture_3D",
    "GL_OES_texture_float",
    "GL_OES_texture_float_linear",
    "GL_OES_texture_half_float",
    "GL_OES_texture_half_float_linear",
    "GL_OES_texture_npot",
    "GL_OES_vertex_half_float",
    "GL_EXT_draw_instanced",
    "GL_EXT_texture_sRGB_decode",
    "GL_OES_EGL_image",
    "GL_OES_depth_texture",
    "GL_AMD_performance_monitor",
    "GL_OES_packed_depth_stencil",
    "GL_EXT_texture_type_2_10_10_10_REV",
    "GL_NV_conditional_render",
    "GL_OES_get_program_binary",
    "GL_APPLE_texture_max_level",
    "GL_EXT_discard_framebuffer",
    "GL_EXT_read_format_bgra",
    "GL_EXT_frag_depth",
    "GL_NV_fbo_color_attachments",
    "GL_OES_EGL_image_external",
    "GL_OES_EGL_sync",
    "GL_OES_vertex_array_object",
    "GL_OES_viewport_array",
    "GL_ANGLE_texture_compression_dxt3",
    "GL_ANGLE_texture_compression_dxt5",
    "GL_EXT_occlusion_query_boolean",
    "GL_EXT_robustness",
    "GL_EXT_texture_rg",
    "GL_EXT_unpack_subimage",
    "GL_NV_draw_buffers",
    "GL_NV_read_buffer",
    "GL_NV_read_depth",
    "GL_NV_read_depth_stencil",
    "GL_NV_read_stencil",
    "GL_EXT_draw_buffers",
    "GL_EXT_map_buffer_range",
    "GL_KHR_debug",
    "GL_KHR_robustness",
    "GL_KHR_texture_compression_astc_ldr",
    "GL_NV_pixel_buffer_object",
    "GL_OES_depth_texture_cube_map",
    "GL_OES_required_internalformat",
    "GL_OES_surfaceless_context",
    "GL_EXT_color_buffer_float",
    "GL_EXT_sRGB_write_control",
    "GL_EXT_separate_shader_objects",
    "GL_EXT_shader_implicit_conversions",
    "GL_EXT_shader_integer_mix",
    "GL_EXT_tessellation_point_size",
    "GL_EXT_tessellation_shader",
    "GL_ANDROID_extension_pack_es31a",
    "GL_EXT_base_instance",
    "GL_EXT_compressed_ETC1_RGB8_sub_texture",
    "GL_EXT_copy_image",
    "GL_EXT_draw_buffers_indexed",
    "GL_EXT_draw_elements_base_vertex",
    "GL_EXT_gpu_shader5",
    "GL_EXT_polygon_offset_clamp",
    "GL_EXT_primitive_bounding_box",
    "GL_EXT_render_snorm",
    "GL_EXT_shader_io_blocks",
    "GL_EXT_texture_border_clamp",
    "GL_EXT_texture_buffer",
    "GL_EXT_texture_cube_map_array",
    "GL_EXT_texture_norm16",
    "GL_EXT_texture_view",
    "GL_KHR_blend_equation_advanced",
    "GL_KHR_context_flush_control",
    "GL_KHR_robust_buffer_access_behavior",
    "GL_NV_image_formats",
    "GL_OES_copy_image",
    "GL_OES_draw_buffers_indexed",
    "GL_OES_draw_elements_base_vertex",
    "GL_OES_gpu_shader5",
    "GL_OES_primitive_bounding_box",
    "GL_OES_sample_shading",
    "GL_OES_sample_variables",
    "GL_OES_shader_io_blocks",
    "GL_OES_shader_multisample_interpolation",
    "GL_OES_tessellation_point_size",
    "GL_OES_tessellation_shader",
    "GL_OES_texture_border_clamp",
    "GL_OES_texture_buffer",
    "GL_OES_texture_cube_map_array",
    "GL_OES_texture_stencil8",
    "GL_OES_texture_storage_multisample_2d_array",
    "GL_OES_texture_view",
    "GL_EXT_blend_func_extended",
    "GL_EXT_buffer_storage",
    "GL_EXT_float_blend",
    "GL_EXT_geometry_point_size",
    "GL_EXT_geometry_shader",
    "GL_KHR_no_error",
    "GL_KHR_texture_compression_astc_sliced_3d",
    "GL_NV_conservative_raster",
    "GL_NV_conservative_raster_dilate",
    "GL_NV_sample_locations",
    "GL_NV_viewport_array2",
    "GL_NV_viewport_swizzle",
    "GL_OES_EGL_image_external_essl3",
    "GL_OES_geometry_point_size",
    "GL_OES_geometry_shader",
    "GL_OES_shader_image_atomic",
    "GL_EXT_clip_cull_distance",
    "GL_EXT_disjoint_timer_query",
    "GL_EXT_texture_compression_s3tc_srgb",
    "GL_EXT_window_rectangles",
    "GL_MESA_shader_integer_functions",
    "GL_EXT_clip_control",
    "GL_EXT_texture_compression_bptc",
    "GL_KHR_parallel_shader_compile",
    "GL_EXT_EGL_image_storage",
    "GL_EXT_texture_shadow_lod",
    "GL_MESA_framebuffer_flip_y",
    "GL_EXT_demote_to_helper_invocation",
    "GL_EXT_depth_clamp",
    "GL_EXT_texture_query_lod",
};
#define NUM_EXTENSIONS (sizeof(gl_extension_list) / sizeof(gl_extension_list[0]))
static char gl_extensions[4096];

static const GLenum compressed_formats[] = {
    GL_COMPRESSED_RGBA_ASTC_4x4_KHR,   GL_COMPRESSED_RGBA_ASTC_5x4_KHR,
    GL_COMPRESSED_RGBA_ASTC_5x5_KHR,   GL_COMPRESSED_RGBA_ASTC_6x5_KHR,
    GL_COMPRESSED_RGBA_ASTC_6x6_KHR,   GL_COMPRESSED_RGBA_ASTC_8x5_KHR,
    GL_COMPRESSED_RGBA_ASTC_8x6_KHR,   GL_COMPRESSED_RGBA_ASTC_8x8_KHR,
    GL_COMPRESSED_RGBA_ASTC_10x5_KHR,  GL_COMPRESSED_RGBA_ASTC_10x6_KHR,
    GL_COMPRESSED_RGBA_ASTC_10x8_KHR,  GL_COMPRESSED_RGBA_ASTC_10x10_KHR,
    GL_COMPRESSED_RGBA_ASTC_12x10_KHR, GL_COMPRESSED_RGBA_ASTC_12x12_KHR,
    GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR,
    GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x4_KHR,
    GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x5_KHR,
    GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x5_KHR,
    GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR,
    GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x5_KHR,
    GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x6_KHR,
    GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR,
    GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x5_KHR,
    GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x6_KHR,
    GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x8_KHR,
    GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x10_KHR,
    GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x10_KHR,
    GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR,
    GL_COMPRESSED_R11_EAC,             GL_COMPRESSED_SIGNED_R11_EAC,
    GL_COMPRESSED_RG11_EAC,            GL_COMPRESSED_SIGNED_RG11_EAC,
    GL_COMPRESSED_RGB8_ETC2,           GL_COMPRESSED_SRGB8_ETC2,
    GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2,
    GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2,
    GL_COMPRESSED_RGBA8_ETC2_EAC,      GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC,
    GL_ETC1_RGB8_OES,
};

void dkgl_context_defaults(DkglContext *ctx);
void dkgl_context_defaults(DkglContext *ctx) {
  ctx->error = GL_NO_ERROR;
  ctx->dirty = DKGL_DIRTY_ALL;
  ctx->depth_range[0] = 0.0f;
  ctx->depth_range[1] = 1.0f;
  ctx->dither = true;
  ctx->cull_mode = GL_BACK;
  ctx->front_face = GL_CCW;
  ctx->depth_func = GL_LESS;
  ctx->depth_mask = GL_TRUE;
  for (int f = 0; f < 2; f++) {
    ctx->stencil[f].func = GL_ALWAYS;
    ctx->stencil[f].ref = 0;
    ctx->stencil[f].func_mask = 0xffffffffu;
    ctx->stencil[f].fail = ctx->stencil[f].zfail = ctx->stencil[f].zpass = GL_KEEP;
    ctx->stencil[f].write_mask = 0xffffffffu;
  }
  for (int i = 0; i < DKGL_DRAW_BUFFERS; i++) {
    ctx->blend[i].eq_rgb = ctx->blend[i].eq_a = GL_FUNC_ADD;
    ctx->blend[i].src_rgb = ctx->blend[i].src_a = GL_ONE;
    ctx->blend[i].dst_rgb = ctx->blend[i].dst_a = GL_ZERO;
    ctx->color_mask[i] = 0xf;
  }
  ctx->line_width = 1.0f;
  ctx->sample_coverage_value = 1.0f;
  ctx->clear_depth = 1.0f;
  ctx->unpack_alignment = 4;
  ctx->pack_alignment = 4;
  ctx->hints[0] = ctx->hints[1] = ctx->hints[2] = ctx->hints[3] = GL_DONT_CARE;
  dkgl_context_init_vao(ctx);
  if (!gl_extensions[0]) {
    size_t at = 0;
    for (size_t i = 0; i < NUM_EXTENSIONS; i++) {
      const size_t n = strlen(gl_extension_list[i]);
      if (at + n + 2 >= sizeof(gl_extensions))
        break;
      memcpy(gl_extensions + at, gl_extension_list[i], n);
      at += n;
      gl_extensions[at++] = ' ';
    }
    gl_extensions[at] = '\0';
  }
}

/* ---------------------------------------------------------- capabilities */

static bool *cap_flag(DkglContext *ctx, GLenum cap, uint32_t *dirty) {
  switch (cap) {
  case GL_CULL_FACE:
    *dirty = DKGL_DIRTY_RASTER;
    return &ctx->cull_face;
  case GL_POLYGON_OFFSET_FILL:
    *dirty = DKGL_DIRTY_RASTER;
    return &ctx->polygon_offset_fill;
  case GL_RASTERIZER_DISCARD:
    *dirty = DKGL_DIRTY_RASTER;
    return &ctx->rasterizer_discard;
  case GL_DEPTH_TEST:
    *dirty = DKGL_DIRTY_DEPTH_STENCIL;
    return &ctx->depth_test;
  case GL_STENCIL_TEST:
    *dirty = DKGL_DIRTY_DEPTH_STENCIL;
    return &ctx->stencil_test;
  case GL_SCISSOR_TEST:
    *dirty = DKGL_DIRTY_SCISSOR;
    return &ctx->scissor_test;
  case GL_SAMPLE_ALPHA_TO_COVERAGE:
    *dirty = DKGL_DIRTY_MULTISAMPLE;
    return &ctx->alpha_to_coverage;
  case GL_SAMPLE_COVERAGE:
    *dirty = DKGL_DIRTY_MULTISAMPLE;
    return &ctx->sample_coverage;
  case GL_DITHER:
    *dirty = 0;
    return &ctx->dither;
  case GL_PRIMITIVE_RESTART_FIXED_INDEX:
    *dirty = DKGL_DIRTY_PRIM_RESTART;
    return &ctx->primitive_restart;
  default:
    return NULL;
  }
}

static void set_cap(GLenum cap, bool on, int index) {
  DkglContext *ctx = dkgl_ctx();
  if (!ctx)
    return;
  if (cap == GL_BLEND) {
    const uint8_t bits = index < 0 ? 0xff : (uint8_t)(1u << index);
    if (on)
      ctx->blend_enable |= bits;
    else
      ctx->blend_enable &= (uint8_t)~bits;
    ctx->dirty |= DKGL_DIRTY_BLEND;
    return;
  }
  uint32_t dirty = 0;
  bool *flag = cap_flag(ctx, cap, &dirty);
  if (!flag) {
    if (cap != GL_DEBUG_OUTPUT_KHR && cap != GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR &&
        cap != GL_SAMPLE_SHADING && cap != GL_SAMPLE_MASK)
      DKGL_ERROR(ctx, GL_INVALID_ENUM);
    return;
  }
  if (*flag != on) {
    *flag = on;
    ctx->dirty |= dirty;
  }
}

void glEnable(GLenum cap) { set_cap(cap, true, -1); }
void glDisable(GLenum cap) { set_cap(cap, false, -1); }
void glEnablei(GLenum target, GLuint index) { set_cap(target, true, (int)index); }
void glDisablei(GLenum target, GLuint index) { set_cap(target, false, (int)index); }
void glEnableiEXT(GLenum target, GLuint index) { glEnablei(target, index); }
void glDisableiEXT(GLenum target, GLuint index) { glDisablei(target, index); }

GLboolean glIsEnabled(GLenum cap) {
  DkglContext *ctx = dkgl_ctx();
  if (!ctx)
    return GL_FALSE;
  if (cap == GL_BLEND)
    return (ctx->blend_enable & 1) ? GL_TRUE : GL_FALSE;
  uint32_t dirty;
  bool *flag = cap_flag(ctx, cap, &dirty);
  return flag && *flag ? GL_TRUE : GL_FALSE;
}

GLboolean glIsEnabledi(GLenum target, GLuint index) {
  DkglContext *ctx = dkgl_ctx();
  if (ctx && target == GL_BLEND && index < DKGL_DRAW_BUFFERS)
    return (ctx->blend_enable >> index) & 1 ? GL_TRUE : GL_FALSE;
  return glIsEnabled(target);
}

/* --------------------------------------------------------- fixed function */

#define CTX_OR_RETURN()                                                        \
  DkglContext *ctx = dkgl_ctx();                                               \
  if (!ctx)                                                                    \
  return

void glBlendColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
  CTX_OR_RETURN();
  ctx->blend_color[0] = red;
  ctx->blend_color[1] = green;
  ctx->blend_color[2] = blue;
  ctx->blend_color[3] = alpha;
  ctx->dirty |= DKGL_DIRTY_BLEND_CONST;
}

static void blend_eq(int index, GLenum rgb, GLenum alpha) {
  CTX_OR_RETURN();
  for (int i = 0; i < DKGL_DRAW_BUFFERS; i++) {
    if (index >= 0 && i != index)
      continue;
    ctx->blend[i].eq_rgb = rgb;
    ctx->blend[i].eq_a = alpha;
  }
  ctx->dirty |= DKGL_DIRTY_BLEND;
}

static void blend_func(int index, GLenum src_rgb, GLenum dst_rgb,
                       GLenum src_a, GLenum dst_a) {
  CTX_OR_RETURN();
  for (int i = 0; i < DKGL_DRAW_BUFFERS; i++) {
    if (index >= 0 && i != index)
      continue;
    ctx->blend[i].src_rgb = src_rgb;
    ctx->blend[i].dst_rgb = dst_rgb;
    ctx->blend[i].src_a = src_a;
    ctx->blend[i].dst_a = dst_a;
  }
  ctx->dirty |= DKGL_DIRTY_BLEND;
}

void glBlendEquation(GLenum mode) { blend_eq(-1, mode, mode); }
void glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha) {
  blend_eq(-1, modeRGB, modeAlpha);
}
void glBlendEquationi(GLuint buf, GLenum mode) { blend_eq((int)buf, mode, mode); }
void glBlendEquationSeparatei(GLuint buf, GLenum modeRGB, GLenum modeAlpha) {
  blend_eq((int)buf, modeRGB, modeAlpha);
}
void glBlendFunc(GLenum sfactor, GLenum dfactor) {
  blend_func(-1, sfactor, dfactor, sfactor, dfactor);
}
void glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB,
                         GLenum sfactorAlpha, GLenum dfactorAlpha) {
  blend_func(-1, sfactorRGB, dfactorRGB, sfactorAlpha, dfactorAlpha);
}
void glBlendFunci(GLuint buf, GLenum src, GLenum dst) {
  blend_func((int)buf, src, dst, src, dst);
}
void glBlendFuncSeparatei(GLuint buf, GLenum srcRGB, GLenum dstRGB,
                          GLenum srcAlpha, GLenum dstAlpha) {
  blend_func((int)buf, srcRGB, dstRGB, srcAlpha, dstAlpha);
}

static void color_mask(int index, GLboolean r, GLboolean g, GLboolean b,
                       GLboolean a) {
  CTX_OR_RETURN();
  const uint8_t m = (uint8_t)((r ? 1 : 0) | (g ? 2 : 0) | (b ? 4 : 0) | (a ? 8 : 0));
  for (int i = 0; i < DKGL_DRAW_BUFFERS; i++)
    if (index < 0 || i == index)
      ctx->color_mask[i] = m;
  ctx->dirty |= DKGL_DIRTY_COLOR_WRITE;
}

void glColorMask(GLboolean red, GLboolean green, GLboolean blue,
                 GLboolean alpha) {
  color_mask(-1, red, green, blue, alpha);
}
void glColorMaski(GLuint index, GLboolean r, GLboolean g, GLboolean b,
                  GLboolean a) {
  color_mask((int)index, r, g, b, a);
}

void glCullFace(GLenum mode) {
  CTX_OR_RETURN();
  ctx->cull_mode = mode;
  ctx->dirty |= DKGL_DIRTY_RASTER;
}
void glFrontFace(GLenum mode) {
  CTX_OR_RETURN();
  ctx->front_face = mode;
  ctx->dirty |= DKGL_DIRTY_RASTER;
}
void glDepthFunc(GLenum func) {
  CTX_OR_RETURN();
  ctx->depth_func = func;
  ctx->dirty |= DKGL_DIRTY_DEPTH_STENCIL;
}
void glDepthMask(GLboolean flag) {
  CTX_OR_RETURN();
  ctx->depth_mask = flag;
  ctx->dirty |= DKGL_DIRTY_DEPTH_STENCIL;
}
void glDepthRangef(GLfloat n, GLfloat f) {
  CTX_OR_RETURN();
  ctx->depth_range[0] = n < 0 ? 0 : (n > 1 ? 1 : n);
  ctx->depth_range[1] = f < 0 ? 0 : (f > 1 ? 1 : f);
  ctx->dirty |= DKGL_DIRTY_VIEWPORT;
}
void glLineWidth(GLfloat width) {
  CTX_OR_RETURN();
  ctx->line_width = width > 0 ? width : 1.0f;
  ctx->dirty |= DKGL_DIRTY_LINE;
}
void glPolygonOffset(GLfloat factor, GLfloat units) {
  CTX_OR_RETURN();
  ctx->polygon_offset_factor = factor;
  ctx->polygon_offset_units = units;
  ctx->dirty |= DKGL_DIRTY_DEPTH_BIAS;
}
void glSampleCoverage(GLfloat value, GLboolean invert) {
  CTX_OR_RETURN();
  ctx->sample_coverage_value = value;
  ctx->sample_coverage_invert = invert;
}

static void stencil_func(int face, GLenum func, GLint ref, GLuint mask) {
  CTX_OR_RETURN();
  for (int f = 0; f < 2; f++)
    if (face < 0 || f == face) {
      ctx->stencil[f].func = func;
      ctx->stencil[f].ref = ref;
      ctx->stencil[f].func_mask = mask;
    }
  ctx->dirty |= DKGL_DIRTY_DEPTH_STENCIL | DKGL_DIRTY_STENCIL_REF;
}

static int face_index(GLenum face) {
  return face == GL_FRONT ? 0 : face == GL_BACK ? 1 : -1;
}

void glStencilFunc(GLenum func, GLint ref, GLuint mask) {
  stencil_func(-1, func, ref, mask);
}
void glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask) {
  stencil_func(face_index(face), func, ref, mask);
}

static void stencil_mask(int face, GLuint mask) {
  CTX_OR_RETURN();
  for (int f = 0; f < 2; f++)
    if (face < 0 || f == face)
      ctx->stencil[f].write_mask = mask;
  ctx->dirty |= DKGL_DIRTY_STENCIL_REF;
}

void glStencilMask(GLuint mask) { stencil_mask(-1, mask); }
void glStencilMaskSeparate(GLenum face, GLuint mask) {
  stencil_mask(face_index(face), mask);
}

static void stencil_op(int face, GLenum fail, GLenum zfail, GLenum zpass) {
  CTX_OR_RETURN();
  for (int f = 0; f < 2; f++)
    if (face < 0 || f == face) {
      ctx->stencil[f].fail = fail;
      ctx->stencil[f].zfail = zfail;
      ctx->stencil[f].zpass = zpass;
    }
  ctx->dirty |= DKGL_DIRTY_DEPTH_STENCIL;
}

void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass) {
  stencil_op(-1, fail, zfail, zpass);
}
void glStencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail,
                         GLenum dppass) {
  stencil_op(face_index(face), sfail, dpfail, dppass);
}

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
  CTX_OR_RETURN();
  ctx->viewport[0] = x;
  ctx->viewport[1] = y;
  ctx->viewport[2] = width;
  ctx->viewport[3] = height;
  ctx->dirty |= DKGL_DIRTY_VIEWPORT;
}

void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
  CTX_OR_RETURN();
  ctx->scissor[0] = x;
  ctx->scissor[1] = y;
  ctx->scissor[2] = width;
  ctx->scissor[3] = height;
  ctx->dirty |= DKGL_DIRTY_SCISSOR;
}

void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
  CTX_OR_RETURN();
  ctx->clear_color[0] = red;
  ctx->clear_color[1] = green;
  ctx->clear_color[2] = blue;
  ctx->clear_color[3] = alpha;
}
void glClearDepthf(GLfloat d) {
  CTX_OR_RETURN();
  ctx->clear_depth = d;
}
void glClearStencil(GLint s) {
  CTX_OR_RETURN();
  ctx->clear_stencil = s;
}

void glHint(GLenum target, GLenum mode) {
  CTX_OR_RETURN();
  if (target == GL_GENERATE_MIPMAP_HINT)
    ctx->hints[0] = mode;
  else if (target == GL_FRAGMENT_SHADER_DERIVATIVE_HINT)
    ctx->hints[1] = mode;
}

void glPixelStorei(GLenum pname, GLint param) {
  CTX_OR_RETURN();
  switch (pname) {
  case GL_UNPACK_ALIGNMENT:
    ctx->unpack_alignment = param;
    break;
  case GL_UNPACK_ROW_LENGTH:
    ctx->unpack_row_length = param;
    break;
  case GL_UNPACK_IMAGE_HEIGHT:
    ctx->unpack_image_height = param;
    break;
  case GL_UNPACK_SKIP_PIXELS:
    ctx->unpack_skip_pixels = param;
    break;
  case GL_UNPACK_SKIP_ROWS:
    ctx->unpack_skip_rows = param;
    break;
  case GL_UNPACK_SKIP_IMAGES:
    ctx->unpack_skip_images = param;
    break;
  case GL_PACK_ALIGNMENT:
    ctx->pack_alignment = param;
    break;
  case GL_PACK_ROW_LENGTH:
    ctx->pack_row_length = param;
    break;
  case GL_PACK_SKIP_PIXELS:
    ctx->pack_skip_pixels = param;
    break;
  case GL_PACK_SKIP_ROWS:
    ctx->pack_skip_rows = param;
    break;
  default:
    DKGL_ERROR(ctx, GL_INVALID_ENUM);
    break;
  }
}

GLenum glGetError(void) {
  DkglContext *ctx = dkgl_ctx();
  if (!ctx)
    return GL_NO_ERROR;
  GLenum e = ctx->error;
  ctx->error = GL_NO_ERROR;
  return e;
}

void glFlush(void) {
  DKGL_LOCK();
  dkgl_flush();
  DKGL_UNLOCK();
}

void glFinish(void) {
  DKGL_LOCK();
  dkgl_finish();
  DKGL_UNLOCK();
}

/* -------------------------------------------------------------- strings */

const GLubyte *glGetString(GLenum name) {
  switch (name) {
  case GL_VENDOR:
    return (const GLubyte *)gl_vendor;
  case GL_RENDERER:
    return (const GLubyte *)gl_renderer;
  case GL_VERSION:
    return (const GLubyte *)gl_version;
  case GL_SHADING_LANGUAGE_VERSION:
    return (const GLubyte *)gl_glsl;
  case GL_EXTENSIONS:
    return (const GLubyte *)gl_extensions;
  default:
    if (dkgl_ctx())
      DKGL_ERROR(dkgl_ctx(), GL_INVALID_ENUM);
    return NULL;
  }
}

const GLubyte *glGetStringi(GLenum name, GLuint index) {
  if (name == GL_EXTENSIONS && index < NUM_EXTENSIONS)
    return (const GLubyte *)gl_extension_list[index];
  if (dkgl_ctx())
    DKGL_ERROR(dkgl_ctx(), GL_INVALID_VALUE);
  return NULL;
}

/* -------------------------------------------------------------- glGet* */

/* Writes up to 16 values; returns how many (0 = unknown pname). */
static int get_values(DkglContext *ctx, GLenum pname, double *v) {
  switch (pname) {
  /* limits */
  case GL_MAX_TEXTURE_SIZE:
  case GL_MAX_CUBE_MAP_TEXTURE_SIZE:
  case GL_MAX_RENDERBUFFER_SIZE:
  case GL_MAX_FRAMEBUFFER_WIDTH:
  case GL_MAX_FRAMEBUFFER_HEIGHT:
    v[0] = 16384;
    return 1;
  case GL_MAX_3D_TEXTURE_SIZE:
  case GL_MAX_ARRAY_TEXTURE_LAYERS:
    v[0] = 2048;
    return 1;
  case GL_MAX_VIEWPORT_DIMS:
    v[0] = v[1] = 16384;
    return 2;
  case GL_MAX_TEXTURE_IMAGE_UNITS:
  case GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS:
  case GL_MAX_COMPUTE_TEXTURE_IMAGE_UNITS:
    v[0] = 16;
    return 1;
  case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
    v[0] = 48;
    return 1;
  case GL_MAX_VERTEX_ATTRIBS:
    v[0] = DKGL_ATTRIBS;
    return 1;
  case GL_MAX_VERTEX_ATTRIB_BINDINGS:
    v[0] = DKGL_VBO_BINDINGS - 1;
    return 1;
  case GL_MAX_VERTEX_ATTRIB_RELATIVE_OFFSET:
    v[0] = 2047;
    return 1;
  case GL_MAX_VERTEX_ATTRIB_STRIDE:
    v[0] = 2048;
    return 1;
  case GL_MAX_VERTEX_UNIFORM_VECTORS:
  case GL_MAX_FRAGMENT_UNIFORM_VECTORS:
    v[0] = 4096;
    return 1;
  case GL_MAX_VERTEX_UNIFORM_COMPONENTS:
  case GL_MAX_FRAGMENT_UNIFORM_COMPONENTS:
  case GL_MAX_COMPUTE_UNIFORM_COMPONENTS:
    v[0] = 16384;
    return 1;
  case GL_MAX_VARYING_VECTORS:
    v[0] = 31;
    return 1;
  case GL_MAX_VARYING_COMPONENTS:
    v[0] = 124;
    return 1;
  case GL_MAX_VERTEX_OUTPUT_COMPONENTS:
  case GL_MAX_FRAGMENT_INPUT_COMPONENTS:
    v[0] = 128;
    return 1;
  case GL_MAX_UNIFORM_BLOCK_SIZE:
    v[0] = 65536;
    return 1;
  case GL_MAX_VERTEX_UNIFORM_BLOCKS:
  case GL_MAX_FRAGMENT_UNIFORM_BLOCKS:
  case GL_MAX_COMPUTE_UNIFORM_BLOCKS:
    v[0] = 14;
    return 1;
  case GL_MAX_COMBINED_UNIFORM_BLOCKS:
  case GL_MAX_UNIFORM_BUFFER_BINDINGS:
    v[0] = DKGL_UBO_BINDINGS;
    return 1;
  case GL_MAX_COMBINED_VERTEX_UNIFORM_COMPONENTS:
  case GL_MAX_COMBINED_FRAGMENT_UNIFORM_COMPONENTS:
  case GL_MAX_COMBINED_COMPUTE_UNIFORM_COMPONENTS:
    v[0] = 16384 + 14 * 16384;
    return 1;
  case GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT:
    v[0] = 256;
    return 1;
  case GL_TEXTURE_BUFFER_OFFSET_ALIGNMENT:
    v[0] = 16;
    return 1;
  case GL_MAX_TEXTURE_BUFFER_SIZE:
    v[0] = 134217728.0;
    return 1;
  case GL_MAX_DRAW_BUFFERS:
  case GL_MAX_COLOR_ATTACHMENTS:
    v[0] = DKGL_DRAW_BUFFERS;
    return 1;
  case GL_MAX_SAMPLES:
  case GL_MAX_COLOR_TEXTURE_SAMPLES:
  case GL_MAX_DEPTH_TEXTURE_SAMPLES:
  case GL_MAX_INTEGER_SAMPLES:
    v[0] = 4;
    return 1;
  case GL_MAX_SAMPLE_MASK_WORDS:
    v[0] = 1;
    return 1;
  case GL_MAX_ELEMENTS_VERTICES:
  case GL_MAX_ELEMENTS_INDICES:
    v[0] = 1048576;
    return 1;
  case GL_MAX_ELEMENT_INDEX:
    v[0] = 4294967295.0;
    return 1;
  case GL_MAX_TEXTURE_LOD_BIAS:
    v[0] = 15;
    return 1;
  case GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT:
    v[0] = 16;
    return 1;
  case GL_ALIASED_LINE_WIDTH_RANGE:
    v[0] = 1;
    v[1] = 10;
    return 2;
  case GL_ALIASED_POINT_SIZE_RANGE:
    v[0] = 1;
    v[1] = 2047;
    return 2;
  case GL_MIN_PROGRAM_TEXEL_OFFSET:
    v[0] = -8;
    return 1;
  case GL_MAX_PROGRAM_TEXEL_OFFSET:
    v[0] = 7;
    return 1;
  case GL_MAX_SERVER_WAIT_TIMEOUT:
    v[0] = 0;
    return 1;
  case GL_MAX_IMAGE_UNITS:
  case GL_MAX_COMPUTE_IMAGE_UNIFORMS:
  case GL_MAX_FRAGMENT_IMAGE_UNIFORMS:
  case GL_MAX_COMBINED_IMAGE_UNIFORMS:
    v[0] = DKGL_IMAGE_UNITS;
    return 1;
  case GL_MAX_VERTEX_IMAGE_UNIFORMS:
  case GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS:
  case GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS:
  case GL_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS:
  case GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS:
  case GL_MAX_COMBINED_SHADER_STORAGE_BLOCKS:
  case GL_MAX_ATOMIC_COUNTER_BUFFER_BINDINGS:
  case GL_MAX_VERTEX_ATOMIC_COUNTERS:
  case GL_MAX_FRAGMENT_ATOMIC_COUNTERS:
  case GL_MAX_COMPUTE_ATOMIC_COUNTERS:
  case GL_MAX_COMBINED_ATOMIC_COUNTERS:
  case 0x90D7: /* GL_MAX_GEOMETRY_SHADER_STORAGE_BLOCKS: Unity on ES 3.1 asks */
  case 0x90D8: /* GL_MAX_TESS_CONTROL_SHADER_STORAGE_BLOCKS */
  case 0x90D9: /* GL_MAX_TESS_EVALUATION_SHADER_STORAGE_BLOCKS */
    v[0] = 0;
    return 1;
  case GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT:
    v[0] = 16;
    return 1;
  case GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS:
    v[0] = 1024;
    return 1;
  case GL_MAX_COMPUTE_SHARED_MEMORY_SIZE:
    v[0] = 49152;
    return 1;
  case GL_NUM_COMPRESSED_TEXTURE_FORMATS:
    v[0] = sizeof(compressed_formats) / sizeof(compressed_formats[0]);
    return 1;
  case GL_COMPRESSED_TEXTURE_FORMATS: {
    int n = (int)(sizeof(compressed_formats) / sizeof(compressed_formats[0]));
    for (int i = 0; i < n; i++)
      v[i] = compressed_formats[i];
    return n;
  }
  case GL_NUM_PROGRAM_BINARY_FORMATS:
  case GL_NUM_SHADER_BINARY_FORMATS:
    v[0] = 0;
    return 1;
  case GL_SHADER_COMPILER:
    v[0] = 1;
    return 1;
  case GL_MAJOR_VERSION:
    v[0] = 3;
    return 1;
  case GL_MINOR_VERSION:
    v[0] = 1;
    return 1;
  case GL_NUM_EXTENSIONS:
    v[0] = NUM_EXTENSIONS;
    return 1;
  case GL_SUBPIXEL_BITS:
    v[0] = 8;
    return 1;
  case GL_IMPLEMENTATION_COLOR_READ_FORMAT:
    v[0] = GL_RGBA;
    return 1;
  case GL_IMPLEMENTATION_COLOR_READ_TYPE:
    v[0] = GL_UNSIGNED_BYTE;
    return 1;
  case GL_RED_BITS:
  case GL_GREEN_BITS:
  case GL_BLUE_BITS:
  case GL_ALPHA_BITS:
    v[0] = 8;
    return 1;
  case GL_DEPTH_BITS:
    v[0] = 24;
    return 1;
  case GL_STENCIL_BITS:
    v[0] = 8;
    return 1;
  case GL_SAMPLE_BUFFERS:
  case GL_SAMPLES:
    v[0] = 0;
    return 1;
  /* state */
  case GL_VIEWPORT:
    for (int i = 0; i < 4; i++)
      v[i] = ctx->viewport[i];
    return 4;
  case GL_SCISSOR_BOX:
    for (int i = 0; i < 4; i++)
      v[i] = ctx->scissor[i];
    return 4;
  case GL_DEPTH_RANGE:
    v[0] = ctx->depth_range[0];
    v[1] = ctx->depth_range[1];
    return 2;
  case GL_COLOR_CLEAR_VALUE:
    for (int i = 0; i < 4; i++)
      v[i] = ctx->clear_color[i];
    return 4;
  case GL_DEPTH_CLEAR_VALUE:
    v[0] = ctx->clear_depth;
    return 1;
  case GL_STENCIL_CLEAR_VALUE:
    v[0] = ctx->clear_stencil;
    return 1;
  case GL_COLOR_WRITEMASK:
    for (int i = 0; i < 4; i++)
      v[i] = (ctx->color_mask[0] >> i) & 1;
    return 4;
  case GL_DEPTH_WRITEMASK:
    v[0] = ctx->depth_mask;
    return 1;
  case GL_DEPTH_FUNC:
    v[0] = ctx->depth_func;
    return 1;
  case GL_CULL_FACE_MODE:
    v[0] = ctx->cull_mode;
    return 1;
  case GL_FRONT_FACE:
    v[0] = ctx->front_face;
    return 1;
  case GL_BLEND_SRC_RGB:
    v[0] = ctx->blend[0].src_rgb;
    return 1;
  case GL_BLEND_DST_RGB:
    v[0] = ctx->blend[0].dst_rgb;
    return 1;
  case GL_BLEND_SRC_ALPHA:
    v[0] = ctx->blend[0].src_a;
    return 1;
  case GL_BLEND_DST_ALPHA:
    v[0] = ctx->blend[0].dst_a;
    return 1;
  case GL_BLEND_EQUATION_RGB:
    v[0] = ctx->blend[0].eq_rgb;
    return 1;
  case GL_BLEND_EQUATION_ALPHA:
    v[0] = ctx->blend[0].eq_a;
    return 1;
  case GL_BLEND_COLOR:
    for (int i = 0; i < 4; i++)
      v[i] = ctx->blend_color[i];
    return 4;
  case GL_STENCIL_FUNC:
    v[0] = ctx->stencil[0].func;
    return 1;
  case GL_STENCIL_REF:
    v[0] = ctx->stencil[0].ref;
    return 1;
  case GL_STENCIL_VALUE_MASK:
    v[0] = ctx->stencil[0].func_mask;
    return 1;
  case GL_STENCIL_WRITEMASK:
    v[0] = ctx->stencil[0].write_mask;
    return 1;
  case GL_STENCIL_FAIL:
    v[0] = ctx->stencil[0].fail;
    return 1;
  case GL_STENCIL_PASS_DEPTH_FAIL:
    v[0] = ctx->stencil[0].zfail;
    return 1;
  case GL_STENCIL_PASS_DEPTH_PASS:
    v[0] = ctx->stencil[0].zpass;
    return 1;
  case GL_STENCIL_BACK_FUNC:
    v[0] = ctx->stencil[1].func;
    return 1;
  case GL_STENCIL_BACK_REF:
    v[0] = ctx->stencil[1].ref;
    return 1;
  case GL_STENCIL_BACK_VALUE_MASK:
    v[0] = ctx->stencil[1].func_mask;
    return 1;
  case GL_STENCIL_BACK_WRITEMASK:
    v[0] = ctx->stencil[1].write_mask;
    return 1;
  case GL_STENCIL_BACK_FAIL:
    v[0] = ctx->stencil[1].fail;
    return 1;
  case GL_STENCIL_BACK_PASS_DEPTH_FAIL:
    v[0] = ctx->stencil[1].zfail;
    return 1;
  case GL_STENCIL_BACK_PASS_DEPTH_PASS:
    v[0] = ctx->stencil[1].zpass;
    return 1;
  case GL_LINE_WIDTH:
    v[0] = ctx->line_width;
    return 1;
  case GL_POLYGON_OFFSET_FACTOR:
    v[0] = ctx->polygon_offset_factor;
    return 1;
  case GL_POLYGON_OFFSET_UNITS:
    v[0] = ctx->polygon_offset_units;
    return 1;
  case GL_SAMPLE_COVERAGE_VALUE:
    v[0] = ctx->sample_coverage_value;
    return 1;
  case GL_SAMPLE_COVERAGE_INVERT:
    v[0] = ctx->sample_coverage_invert;
    return 1;
  case GL_GENERATE_MIPMAP_HINT:
    v[0] = ctx->hints[0];
    return 1;
  case GL_FRAGMENT_SHADER_DERIVATIVE_HINT:
    v[0] = ctx->hints[1];
    return 1;
  case GL_ACTIVE_TEXTURE:
    v[0] = GL_TEXTURE0 + ctx->active_texture;
    return 1;
  case GL_TEXTURE_BINDING_2D:
    v[0] = ctx->textures[ctx->active_texture][DKGL_TEX_2D];
    return 1;
  case GL_TEXTURE_BINDING_3D:
    v[0] = ctx->textures[ctx->active_texture][DKGL_TEX_3D];
    return 1;
  case GL_TEXTURE_BINDING_CUBE_MAP:
    v[0] = ctx->textures[ctx->active_texture][DKGL_TEX_CUBE];
    return 1;
  case GL_TEXTURE_BINDING_2D_ARRAY:
    v[0] = ctx->textures[ctx->active_texture][DKGL_TEX_2D_ARRAY];
    return 1;
  case GL_TEXTURE_BINDING_BUFFER:
    v[0] = ctx->textures[ctx->active_texture][DKGL_TEX_BUFFER];
    return 1;
  case GL_SAMPLER_BINDING:
    v[0] = ctx->samplers[ctx->active_texture];
    return 1;
  case GL_ARRAY_BUFFER_BINDING:
    v[0] = ctx->array_buffer;
    return 1;
  case GL_ELEMENT_ARRAY_BUFFER_BINDING:
    v[0] = ctx->vao->element_buffer;
    return 1;
  case GL_VERTEX_ARRAY_BINDING:
    v[0] = ctx->vao_name;
    return 1;
  case GL_DRAW_FRAMEBUFFER_BINDING:
    v[0] = ctx->draw_fbo;
    return 1;
  case GL_READ_FRAMEBUFFER_BINDING:
    v[0] = ctx->read_fbo;
    return 1;
  case GL_RENDERBUFFER_BINDING:
    v[0] = ctx->renderbuffer;
    return 1;
  case GL_CURRENT_PROGRAM:
    v[0] = ctx->program;
    return 1;
  case GL_PIXEL_PACK_BUFFER_BINDING:
    v[0] = ctx->pixel_pack;
    return 1;
  case GL_PIXEL_UNPACK_BUFFER_BINDING:
    v[0] = ctx->pixel_unpack;
    return 1;
  case GL_UNIFORM_BUFFER_BINDING:
    v[0] = ctx->generic_uniform;
    return 1;
  case GL_COPY_READ_BUFFER_BINDING:
    v[0] = ctx->copy_read;
    return 1;
  case GL_COPY_WRITE_BUFFER_BINDING:
    v[0] = ctx->copy_write;
    return 1;
  case GL_DRAW_INDIRECT_BUFFER_BINDING:
    v[0] = ctx->draw_indirect;
    return 1;
  case GL_DISPATCH_INDIRECT_BUFFER_BINDING:
    v[0] = ctx->dispatch_indirect;
    return 1;
  case GL_UNPACK_ALIGNMENT:
    v[0] = ctx->unpack_alignment;
    return 1;
  case GL_UNPACK_ROW_LENGTH:
    v[0] = ctx->unpack_row_length;
    return 1;
  case GL_UNPACK_IMAGE_HEIGHT:
    v[0] = ctx->unpack_image_height;
    return 1;
  case GL_UNPACK_SKIP_PIXELS:
    v[0] = ctx->unpack_skip_pixels;
    return 1;
  case GL_UNPACK_SKIP_ROWS:
    v[0] = ctx->unpack_skip_rows;
    return 1;
  case GL_UNPACK_SKIP_IMAGES:
    v[0] = ctx->unpack_skip_images;
    return 1;
  case GL_PACK_ALIGNMENT:
    v[0] = ctx->pack_alignment;
    return 1;
  case GL_PACK_ROW_LENGTH:
    v[0] = ctx->pack_row_length;
    return 1;
  case GL_PACK_SKIP_PIXELS:
    v[0] = ctx->pack_skip_pixels;
    return 1;
  case GL_PACK_SKIP_ROWS:
    v[0] = ctx->pack_skip_rows;
    return 1;
  case GL_READ_BUFFER: {
    DkglFramebuffer *fb = dkgl_framebuffer(ctx->read_fbo, false);
    v[0] = fb ? fb->read_buffer : GL_BACK;
    return 1;
  }
  case GL_RESET_NOTIFICATION_STRATEGY_EXT:
    v[0] = GL_NO_RESET_NOTIFICATION_EXT;
    return 1;
  case GL_CONTEXT_FLAGS:
    v[0] = 0;
    return 1;
  default:
    break;
  }
  if (pname >= GL_DRAW_BUFFER0 && pname < GL_DRAW_BUFFER0 + DKGL_DRAW_BUFFERS) {
    DkglFramebuffer *fb = dkgl_framebuffer(ctx->draw_fbo, false);
    v[0] = fb ? fb->draw_buffers[pname - GL_DRAW_BUFFER0]
              : (pname == GL_DRAW_BUFFER0 ? GL_BACK : GL_NONE);
    return 1;
  }
  uint32_t dirty;
  bool *flag = cap_flag(ctx, pname, &dirty);
  if (flag) {
    v[0] = *flag;
    return 1;
  }
  if (pname == GL_BLEND) {
    v[0] = ctx->blend_enable & 1;
    return 1;
  }
  return 0;
}

static int get_or_error(GLenum pname, double *v) {
  DkglContext *ctx = dkgl_ctx();
  if (!ctx)
    return 0;
  int n = get_values(ctx, pname, v);
  if (!n) {
    static uint32_t reported[32];
    static int count;
    bool seen = false;
    for (int i = 0; i < count; i++)
      if (reported[i] == pname)
        seen = true;
    if (!seen && count < 32) {
      reported[count++] = pname;
      debugPrintf("dkgl: glGet 0x%x not handled\n", pname);
    }
    DKGL_ERROR(ctx, GL_INVALID_ENUM);
  }
  return n;
}

void glGetIntegerv(GLenum pname, GLint *data) {
  double v[64];
  int n = get_or_error(pname, v);
  for (int i = 0; i < n; i++)
    data[i] = v[i] > 2147483647.0 ? 2147483647 : (GLint)v[i];
}

void glGetInteger64v(GLenum pname, GLint64 *data) {
  double v[64];
  int n = get_or_error(pname, v);
  for (int i = 0; i < n; i++)
    data[i] = (GLint64)v[i];
}

void glGetFloatv(GLenum pname, GLfloat *data) {
  double v[64];
  int n = get_or_error(pname, v);
  for (int i = 0; i < n; i++)
    data[i] = (GLfloat)v[i];
}

void glGetBooleanv(GLenum pname, GLboolean *data) {
  double v[64];
  int n = get_or_error(pname, v);
  for (int i = 0; i < n; i++)
    data[i] = v[i] != 0.0 ? GL_TRUE : GL_FALSE;
}

static int get_indexed(DkglContext *ctx, GLenum target, GLuint index,
                       int64_t *out) {
  switch (target) {
  case GL_UNIFORM_BUFFER_BINDING:
    if (index >= DKGL_UBO_BINDINGS)
      return 0;
    *out = ctx->ubo_bindings[index].buffer;
    return 1;
  case GL_UNIFORM_BUFFER_START:
    if (index >= DKGL_UBO_BINDINGS)
      return 0;
    *out = ctx->ubo_bindings[index].offset;
    return 1;
  case GL_UNIFORM_BUFFER_SIZE:
    if (index >= DKGL_UBO_BINDINGS)
      return 0;
    *out = ctx->ubo_bindings[index].size;
    return 1;
  case GL_MAX_COMPUTE_WORK_GROUP_COUNT:
    *out = 65535;
    return 1;
  case GL_MAX_COMPUTE_WORK_GROUP_SIZE:
    *out = index < 2 ? 1024 : 64;
    return 1;
  case GL_IMAGE_BINDING_NAME:
    *out = index < DKGL_IMAGE_UNITS ? ctx->images[index].texture : 0;
    return 1;
  case GL_VERTEX_BINDING_BUFFER:
    *out = index < DKGL_VBO_BINDINGS ? ctx->vao->bindings[index].buffer : 0;
    return 1;
  case GL_VERTEX_BINDING_OFFSET:
    *out = index < DKGL_VBO_BINDINGS ? ctx->vao->bindings[index].offset : 0;
    return 1;
  case GL_VERTEX_BINDING_STRIDE:
    *out = index < DKGL_VBO_BINDINGS ? ctx->vao->bindings[index].stride : 0;
    return 1;
  case GL_VERTEX_BINDING_DIVISOR:
    *out = index < DKGL_VBO_BINDINGS ? ctx->vao->bindings[index].divisor : 0;
    return 1;
  case GL_BLEND_EQUATION_RGB:
    *out = index < DKGL_DRAW_BUFFERS ? ctx->blend[index].eq_rgb : 0;
    return 1;
  case GL_BLEND_EQUATION_ALPHA:
    *out = index < DKGL_DRAW_BUFFERS ? ctx->blend[index].eq_a : 0;
    return 1;
  case GL_BLEND_SRC_RGB:
    *out = index < DKGL_DRAW_BUFFERS ? ctx->blend[index].src_rgb : 0;
    return 1;
  case GL_BLEND_DST_RGB:
    *out = index < DKGL_DRAW_BUFFERS ? ctx->blend[index].dst_rgb : 0;
    return 1;
  case GL_BLEND_SRC_ALPHA:
    *out = index < DKGL_DRAW_BUFFERS ? ctx->blend[index].src_a : 0;
    return 1;
  case GL_BLEND_DST_ALPHA:
    *out = index < DKGL_DRAW_BUFFERS ? ctx->blend[index].dst_a : 0;
    return 1;
  default:
    return 0;
  }
}

void glGetIntegeri_v(GLenum target, GLuint index, GLint *data) {
  DkglContext *ctx = dkgl_ctx();
  int64_t v = 0;
  if (ctx && get_indexed(ctx, target, index, &v))
    *data = (GLint)v;
  else if (ctx)
    DKGL_ERROR(ctx, GL_INVALID_ENUM);
}

void glGetInteger64i_v(GLenum target, GLuint index, GLint64 *data) {
  DkglContext *ctx = dkgl_ctx();
  int64_t v = 0;
  if (ctx && get_indexed(ctx, target, index, &v))
    *data = v;
  else if (ctx)
    DKGL_ERROR(ctx, GL_INVALID_ENUM);
}

void glGetBooleani_v(GLenum target, GLuint index, GLboolean *data) {
  DkglContext *ctx = dkgl_ctx();
  if (!ctx)
    return;
  if (target == GL_COLOR_WRITEMASK && index < DKGL_DRAW_BUFFERS) {
    for (int i = 0; i < 4; i++)
      data[i] = (ctx->color_mask[index] >> i) & 1;
    return;
  }
  int64_t v = 0;
  if (get_indexed(ctx, target, index, &v))
    *data = v ? GL_TRUE : GL_FALSE;
}

void glGetInternalformativ(GLenum target, GLenum internalformat, GLenum pname,
                           GLsizei bufSize, GLint *params) {
  (void)target;
  (void)internalformat;
  if (bufSize < 1)
    return;
  if (pname == GL_NUM_SAMPLE_COUNTS)
    params[0] = 1;
  else if (pname == GL_SAMPLES)
    params[0] = 4;
}

/* ------------------------------------------------------------- queries */

static DkglQuery *query(GLuint name) {
  return name ? dkgl_name_get(DKGL_OBJ_QUERY, name) : NULL;
}

void glGenQueries(GLsizei n, GLuint *ids) {
  DKGL_LOCK();
  for (GLsizei i = 0; i < n; i++) {
    GLuint name = dkgl_name_gen(DKGL_OBJ_QUERY);
    DkglQuery *q = calloc(1, sizeof(*q));
    if (q) {
      q->name = name;
      dkgl_name_set(DKGL_OBJ_QUERY, name, q);
    }
    ids[i] = name;
  }
  DKGL_UNLOCK();
}

void glDeleteQueries(GLsizei n, const GLuint *ids) {
  DKGL_LOCK();
  for (GLsizei i = 0; i < n; i++) {
    DkglQuery *q = query(ids[i]);
    if (!q)
      continue;
    dkgl_free_deferred(&q->report);
    dkgl_name_free(DKGL_OBJ_QUERY, ids[i]);
    free(q);
  }
  DKGL_UNLOCK();
}

GLboolean glIsQuery(GLuint id) {
  DKGL_LOCK();
  GLboolean r = query(id) ? GL_TRUE : GL_FALSE;
  DKGL_UNLOCK();
  return r;
}

void glBeginQuery(GLenum target, GLuint id) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglQuery *q = query(id);
  if (!ctx || !q || q->active) {
    if (ctx)
      DKGL_ERROR(ctx, GL_INVALID_OPERATION);
    DKGL_UNLOCK();
    return;
  }
  if (!q->report.node && !dkgl_alloc(DKGL_HEAP_DATA, 64, 256, &q->report)) {
    DKGL_UNLOCK();
    return;
  }
  q->target = target;
  q->active = true;
  q->pending = true;
  memset(dkgl_cpu(&q->report), 0, 32);
  if (target == GL_ANY_SAMPLES_PASSED ||
      target == GL_ANY_SAMPLES_PASSED_CONSERVATIVE)
    dkCmdBufReportCounter(dkgl_cmd(), DkCounter_SamplesPassed,
                          dkgl_gpu(&q->report));
  ctx->active_query = id;
  DKGL_UNLOCK();
}

void glEndQuery(GLenum target) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglQuery *q = ctx ? query(ctx->active_query) : NULL;
  if (!q || !q->active) {
    if (ctx)
      DKGL_ERROR(ctx, GL_INVALID_OPERATION);
    DKGL_UNLOCK();
    return;
  }
  if (target == GL_ANY_SAMPLES_PASSED ||
      target == GL_ANY_SAMPLES_PASSED_CONSERVATIVE)
    dkCmdBufReportCounter(dkgl_cmd(), DkCounter_SamplesPassed,
                          dkgl_gpu(&q->report) + 16);
  q->active = false;
  q->serial = dkg.serial;
  ctx->active_query = 0;
  DKGL_UNLOCK();
}

void glGetQueryiv(GLenum target, GLenum pname, GLint *params) {
  DkglContext *ctx = dkgl_ctx();
  (void)target;
  if (!ctx)
    return;
  if (pname == GL_CURRENT_QUERY)
    *params = (GLint)ctx->active_query;
  else
    *params = 0;
}

void glGetQueryObjectuiv(GLuint id, GLenum pname, GLuint *params) {
  DKGL_LOCK();
  DkglQuery *q = query(id);
  if (!q || q->active) {
    if (dkgl_ctx())
      DKGL_ERROR(dkgl_ctx(), GL_INVALID_OPERATION);
    DKGL_UNLOCK();
    return;
  }
  if (pname == GL_QUERY_RESULT_AVAILABLE) {
    if (q->serial >= dkg.serial)
      dkgl_flush(); /* the result can only arrive once submitted */
    *params = dkgl_serial_done(q->serial) ? GL_TRUE : GL_FALSE;
  } else if (pname == GL_QUERY_RESULT) {
    dkgl_wait_serial(q->serial);
    const uint64_t *r = dkgl_cpu(&q->report);
    *params = r[2] > r[0] ? GL_TRUE : GL_FALSE;
  }
  DKGL_UNLOCK();
}

/* --------------------------------------------------------------- syncs */

#define SYNC_MAGIC 0x434e5953u

GLsync glFenceSync(GLenum condition, GLbitfield flags) {
  (void)condition;
  (void)flags;
  DKGL_LOCK();
  DkglSync *s = calloc(1, sizeof(*s));
  if (s) {
    s->magic = SYNC_MAGIC;
    s->serial = dkgl_signal_point();
    /* GL requires the fence to signal eventually without further calls. */
    dkgl_flush();
  }
  DKGL_UNLOCK();
  return (GLsync)s;
}

static DkglSync *sync_object(GLsync sync) {
  DkglSync *s = (DkglSync *)sync;
  return s && s->magic == SYNC_MAGIC ? s : NULL;
}

void glDeleteSync(GLsync sync) {
  DkglSync *s = sync_object(sync);
  if (s) {
    s->magic = 0;
    free(s);
  }
}

GLboolean glIsSync(GLsync sync) { return sync_object(sync) ? GL_TRUE : GL_FALSE; }

GLenum glClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
  (void)flags;
  DkglSync *s = sync_object(sync);
  if (!s)
    return GL_WAIT_FAILED;
  DKGL_LOCK();
  if (dkgl_serial_done(s->serial)) {
    DKGL_UNLOCK();
    return GL_ALREADY_SIGNALED;
  }
  if (!timeout) {
    DKGL_UNLOCK();
    return GL_TIMEOUT_EXPIRED;
  }
  DKGL_UNLOCK();
  /* Poll without holding the lock so other threads keep recording. */
  const uint64_t start = armTicksToNs(armGetSystemTick());
  for (;;) {
    DKGL_LOCK();
    const bool done = dkgl_serial_done(s->serial);
    DKGL_UNLOCK();
    if (done)
      return GL_CONDITION_SATISFIED;
    if (armTicksToNs(armGetSystemTick()) - start >= timeout)
      return GL_TIMEOUT_EXPIRED;
    svcSleepThread(100000);
  }
}

void glWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
  (void)sync;
  (void)flags;
  (void)timeout;
  /* One ordered command stream: server-side waits are implicit. */
}

void glGetSynciv(GLsync sync, GLenum pname, GLsizei bufSize, GLsizei *length,
                 GLint *values) {
  DkglSync *s = sync_object(sync);
  if (!s || bufSize < 1)
    return;
  GLint v = 0;
  switch (pname) {
  case GL_OBJECT_TYPE:
    v = GL_SYNC_FENCE;
    break;
  case GL_SYNC_STATUS:
    DKGL_LOCK();
    v = dkgl_serial_done(s->serial) ? GL_SIGNALED : GL_UNSIGNALED;
    DKGL_UNLOCK();
    break;
  case GL_SYNC_CONDITION:
    v = GL_SYNC_GPU_COMMANDS_COMPLETE;
    break;
  default:
    v = 0;
    break;
  }
  values[0] = v;
  if (length)
    *length = 1;
}

/* ------------------------------------------------- KHR_debug (no-op) */

void glDebugMessageControlKHR(GLenum source, GLenum type, GLenum severity,
                              GLsizei count, const GLuint *ids,
                              GLboolean enabled) {
  (void)source;
  (void)type;
  (void)severity;
  (void)count;
  (void)ids;
  (void)enabled;
}
void glDebugMessageInsertKHR(GLenum source, GLenum type, GLuint id,
                             GLenum severity, GLsizei length,
                             const GLchar *buf) {
  (void)source;
  (void)type;
  (void)id;
  (void)severity;
  (void)length;
  (void)buf;
}
void glDebugMessageCallbackKHR(GLDEBUGPROCKHR callback, const void *userParam) {
  (void)callback;
  (void)userParam;
}
GLuint glGetDebugMessageLogKHR(GLuint count, GLsizei bufSize, GLenum *sources,
                               GLenum *types, GLuint *ids, GLenum *severities,
                               GLsizei *lengths, GLchar *messageLog) {
  (void)count;
  (void)bufSize;
  (void)sources;
  (void)types;
  (void)ids;
  (void)severities;
  (void)lengths;
  (void)messageLog;
  return 0;
}
void glPushDebugGroupKHR(GLenum source, GLuint id, GLsizei length,
                         const GLchar *message) {
  (void)source;
  (void)id;
  (void)length;
  (void)message;
}
void glPopDebugGroupKHR(void) {}
void glObjectLabelKHR(GLenum identifier, GLuint name, GLsizei length,
                      const GLchar *label) {
  (void)identifier;
  (void)name;
  (void)length;
  (void)label;
}
void glGetObjectLabelKHR(GLenum identifier, GLuint name, GLsizei bufSize,
                         GLsizei *length, GLchar *label) {
  (void)identifier;
  (void)name;
  if (length)
    *length = 0;
  if (label && bufSize > 0)
    label[0] = '\0';
}
void glObjectPtrLabelKHR(const void *ptr, GLsizei length, const GLchar *label) {
  (void)ptr;
  (void)length;
  (void)label;
}
void glGetObjectPtrLabelKHR(const void *ptr, GLsizei bufSize, GLsizei *length,
                            GLchar *label) {
  (void)ptr;
  if (length)
    *length = 0;
  if (label && bufSize > 0)
    label[0] = '\0';
}
void glGetPointervKHR(GLenum pname, void **params) {
  (void)pname;
  *params = NULL;
}
