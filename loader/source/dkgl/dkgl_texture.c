/* dkgl_texture.c -- textures, samplers, formats and uploads */
#include "dkgl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "error.h"
#include "util.h"

DkCmdBuf dkgl_cmd(void);

uint32_t dkgl_texture_epoch = 1;

/* ------------------------------------------------------------ formats */

#define R DKGL_FMT_RENDER
#define E DKGL_FMT_2D_ENGINE
#define I DKGL_FMT_INTEGER
#define F DKGL_FMT_FLOAT
#define S DKGL_FMT_SIGNED
#define X DKGL_FMT_RGB_TO_RGBX
#define P DKGL_FMT_TO_RGBA8
#define Z DKGL_FMT_DEPTH
#define ST DKGL_FMT_STENCIL
#define C DKGL_FMT_COMPRESSED
/* Format swizzles are stored as DkImageSwizzle + 1, so 0 means identity. */
#define SW(a, b, c, d)                                                         \
  { DkImageSwizzle_##a + 1, DkImageSwizzle_##b + 1, DkImageSwizzle_##c + 1,    \
    DkImageSwizzle_##d + 1 }

static const DkglFormat formats[] = {
    {GL_RGBA8, DkImageFormat_RGBA8_Unorm, 4, 1, 1, R | E, GL_RGBA, GL_UNSIGNED_BYTE, {0}},
    {GL_SRGB8_ALPHA8, DkImageFormat_RGBA8_Unorm_sRGB, 4, 1, 1, R | E, GL_RGBA, GL_UNSIGNED_BYTE, {0}},
    {GL_RGB8, DkImageFormat_RGBX8_Unorm, 4, 1, 1, R | E | X, GL_RGB, GL_UNSIGNED_BYTE, {0}},
    {GL_SRGB8, DkImageFormat_RGBX8_Unorm_sRGB, 4, 1, 1, E | X, GL_RGB, GL_UNSIGNED_BYTE, {0}},
    {GL_RGBA8_SNORM, DkImageFormat_RGBA8_Snorm, 4, 1, 1, S, GL_RGBA, GL_BYTE, {0}},
    {GL_RGB8_SNORM, DkImageFormat_RGBX8_Snorm, 4, 1, 1, S | X, GL_RGB, GL_BYTE, {0}},
    {GL_R8, DkImageFormat_R8_Unorm, 1, 1, 1, R | E, GL_RED, GL_UNSIGNED_BYTE, {0}},
    {GL_R8_SNORM, DkImageFormat_R8_Snorm, 1, 1, 1, S, GL_RED, GL_BYTE, {0}},
    {GL_RG8, DkImageFormat_RG8_Unorm, 2, 1, 1, R | E, GL_RG, GL_UNSIGNED_BYTE, {0}},
    {GL_RG8_SNORM, DkImageFormat_RG8_Snorm, 2, 1, 1, S, GL_RG, GL_BYTE, {0}},
    /* EXT_texture_norm16 (Unity 2022 probes GL_R16 at start-up) */
    {0x822A /* GL_R16_EXT */, DkImageFormat_R16_Unorm, 2, 1, 1, R | E, GL_RED, GL_UNSIGNED_SHORT, {0}},
    {0x822C /* GL_RG16_EXT */, DkImageFormat_RG16_Unorm, 4, 1, 1, R | E, GL_RG, GL_UNSIGNED_SHORT, {0}},
    {0x805B /* GL_RGBA16_EXT */, DkImageFormat_RGBA16_Unorm, 8, 1, 1, R | E, GL_RGBA, GL_UNSIGNED_SHORT, {0}},
    {GL_R16F, DkImageFormat_R16_Float, 2, 1, 1, R | E | F, GL_RED, GL_HALF_FLOAT, {0}},
    {GL_RG16F, DkImageFormat_RG16_Float, 4, 1, 1, R | E | F, GL_RG, GL_HALF_FLOAT, {0}},
    {GL_RGB16F, DkImageFormat_RGBX16_Float, 8, 1, 1, R | E | F | X, GL_RGB, GL_HALF_FLOAT, {0}},
    {GL_RGBA16F, DkImageFormat_RGBA16_Float, 8, 1, 1, R | E | F, GL_RGBA, GL_HALF_FLOAT, {0}},
    {GL_R32F, DkImageFormat_R32_Float, 4, 1, 1, R | E | F, GL_RED, GL_FLOAT, {0}},
    {GL_RG32F, DkImageFormat_RG32_Float, 8, 1, 1, R | E | F, GL_RG, GL_FLOAT, {0}},
    {GL_RGB32F, DkImageFormat_RGB32_Float, 12, 1, 1, F, GL_RGB, GL_FLOAT, {0}},
    {GL_RGBA32F, DkImageFormat_RGBA32_Float, 16, 1, 1, R | E | F, GL_RGBA, GL_FLOAT, {0}},
    {GL_R11F_G11F_B10F, DkImageFormat_RG11B10_Float, 4, 1, 1, R | E | F, GL_RGB, GL_UNSIGNED_INT_10F_11F_11F_REV, {0}},
    {GL_RGB9_E5, DkImageFormat_E5BGR9_Float, 4, 1, 1, F, GL_RGB, GL_UNSIGNED_INT_5_9_9_9_REV, {0}},
    {GL_RGB10_A2, DkImageFormat_RGB10A2_Unorm, 4, 1, 1, R | E, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, {0}},
    {GL_RGB10_A2UI, DkImageFormat_RGB10A2_Uint, 4, 1, 1, R | I, GL_RGBA_INTEGER, GL_UNSIGNED_INT_2_10_10_10_REV, {0}},
    {GL_RGB565, DkImageFormat_RGBA8_Unorm, 4, 1, 1, R | E | P, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, {0}},
    {GL_RGBA4, DkImageFormat_RGBA8_Unorm, 4, 1, 1, R | E | P, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, {0}},
    {GL_RGB5_A1, DkImageFormat_RGBA8_Unorm, 4, 1, 1, R | E | P, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, {0}},
    {GL_R8UI, DkImageFormat_R8_Uint, 1, 1, 1, R | I, GL_RED_INTEGER, GL_UNSIGNED_BYTE, {0}},
    {GL_R8I, DkImageFormat_R8_Sint, 1, 1, 1, R | I | S, GL_RED_INTEGER, GL_BYTE, {0}},
    {GL_R16UI, DkImageFormat_R16_Uint, 2, 1, 1, R | I, GL_RED_INTEGER, GL_UNSIGNED_SHORT, {0}},
    {GL_R16I, DkImageFormat_R16_Sint, 2, 1, 1, R | I | S, GL_RED_INTEGER, GL_SHORT, {0}},
    {GL_R32UI, DkImageFormat_R32_Uint, 4, 1, 1, R | I, GL_RED_INTEGER, GL_UNSIGNED_INT, {0}},
    {GL_R32I, DkImageFormat_R32_Sint, 4, 1, 1, R | I | S, GL_RED_INTEGER, GL_INT, {0}},
    {GL_RG8UI, DkImageFormat_RG8_Uint, 2, 1, 1, R | I, GL_RG_INTEGER, GL_UNSIGNED_BYTE, {0}},
    {GL_RG8I, DkImageFormat_RG8_Sint, 2, 1, 1, R | I | S, GL_RG_INTEGER, GL_BYTE, {0}},
    {GL_RG16UI, DkImageFormat_RG16_Uint, 4, 1, 1, R | I, GL_RG_INTEGER, GL_UNSIGNED_SHORT, {0}},
    {GL_RG16I, DkImageFormat_RG16_Sint, 4, 1, 1, R | I | S, GL_RG_INTEGER, GL_SHORT, {0}},
    {GL_RG32UI, DkImageFormat_RG32_Uint, 8, 1, 1, R | I, GL_RG_INTEGER, GL_UNSIGNED_INT, {0}},
    {GL_RG32I, DkImageFormat_RG32_Sint, 8, 1, 1, R | I | S, GL_RG_INTEGER, GL_INT, {0}},
    {GL_RGBA8UI, DkImageFormat_RGBA8_Uint, 4, 1, 1, R | I, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, {0}},
    {GL_RGBA8I, DkImageFormat_RGBA8_Sint, 4, 1, 1, R | I | S, GL_RGBA_INTEGER, GL_BYTE, {0}},
    {GL_RGBA16UI, DkImageFormat_RGBA16_Uint, 8, 1, 1, R | I, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT, {0}},
    {GL_RGBA16I, DkImageFormat_RGBA16_Sint, 8, 1, 1, R | I | S, GL_RGBA_INTEGER, GL_SHORT, {0}},
    {GL_RGBA32UI, DkImageFormat_RGBA32_Uint, 16, 1, 1, R | I, GL_RGBA_INTEGER, GL_UNSIGNED_INT, {0}},
    {GL_RGBA32I, DkImageFormat_RGBA32_Sint, 16, 1, 1, R | I | S, GL_RGBA_INTEGER, GL_INT, {0}},
    {GL_RGB8UI, DkImageFormat_RGBX8_Uint, 4, 1, 1, I | X, GL_RGB_INTEGER, GL_UNSIGNED_BYTE, {0}},
    {GL_RGB8I, DkImageFormat_RGBX8_Sint, 4, 1, 1, I | S | X, GL_RGB_INTEGER, GL_BYTE, {0}},
    {GL_RGB16UI, DkImageFormat_RGBX16_Uint, 8, 1, 1, I | X, GL_RGB_INTEGER, GL_UNSIGNED_SHORT, {0}},
    {GL_RGB16I, DkImageFormat_RGBX16_Sint, 8, 1, 1, I | S | X, GL_RGB_INTEGER, GL_SHORT, {0}},
    {GL_RGB32UI, DkImageFormat_RGB32_Uint, 12, 1, 1, I, GL_RGB_INTEGER, GL_UNSIGNED_INT, {0}},
    {GL_RGB32I, DkImageFormat_RGB32_Sint, 12, 1, 1, I | S, GL_RGB_INTEGER, GL_INT, {0}},
    {GL_BGRA8_EXT, DkImageFormat_BGRA8_Unorm, 4, 1, 1, R | E, GL_BGRA_EXT, GL_UNSIGNED_BYTE, {0}},
    {GL_DEPTH_COMPONENT16, DkImageFormat_Z16, 2, 1, 1, R | Z, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, SW(Red, Zero, Zero, One)},
    {GL_DEPTH_COMPONENT24, DkImageFormat_Z24X8, 4, 1, 1, R | Z, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, SW(Red, Zero, Zero, One)},
    {GL_DEPTH_COMPONENT32F, DkImageFormat_ZF32, 4, 1, 1, R | Z | F, GL_DEPTH_COMPONENT, GL_FLOAT, SW(Red, Zero, Zero, One)},
    {GL_DEPTH24_STENCIL8, DkImageFormat_Z24S8, 4, 1, 1, R | Z | ST, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, SW(Red, Zero, Zero, One)},
    {GL_DEPTH32F_STENCIL8, DkImageFormat_ZF32_X24S8, 8, 1, 1, R | Z | ST | F, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, SW(Red, Zero, Zero, One)},
    {GL_STENCIL_INDEX8, DkImageFormat_S8, 1, 1, 1, R | ST, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, {0}},
    {GL_LUMINANCE, DkImageFormat_R8_Unorm, 1, 1, 1, E, GL_LUMINANCE, GL_UNSIGNED_BYTE, SW(Red, Red, Red, One)},
    {GL_ALPHA, DkImageFormat_R8_Unorm, 1, 1, 1, E, GL_ALPHA, GL_UNSIGNED_BYTE, SW(Zero, Zero, Zero, Red)},
    {GL_LUMINANCE_ALPHA, DkImageFormat_RG8_Unorm, 2, 1, 1, E, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, SW(Red, Red, Red, Green)},
    {GL_COMPRESSED_R11_EAC, DkImageFormat_R_ETC2_Unorm, 8, 4, 4, C, 0, 0, {0}},
    {GL_COMPRESSED_SIGNED_R11_EAC, DkImageFormat_R_ETC2_Snorm, 8, 4, 4, C, 0, 0, {0}},
    {GL_COMPRESSED_RG11_EAC, DkImageFormat_RG_ETC2_Unorm, 16, 4, 4, C, 0, 0, {0}},
    {GL_COMPRESSED_SIGNED_RG11_EAC, DkImageFormat_RG_ETC2_Snorm, 16, 4, 4, C, 0, 0, {0}},
    {GL_COMPRESSED_RGB8_ETC2, DkImageFormat_RGB_ETC2, 8, 4, 4, C, 0, 0, {0}},
    {GL_COMPRESSED_SRGB8_ETC2, DkImageFormat_RGB_ETC2_sRGB, 8, 4, 4, C, 0, 0, {0}},
    {GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2, DkImageFormat_RGB_PTA_ETC2, 8, 4, 4, C, 0, 0, {0}},
    {GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2, DkImageFormat_RGB_PTA_ETC2_sRGB, 8, 4, 4, C, 0, 0, {0}},
    {GL_COMPRESSED_RGBA8_ETC2_EAC, DkImageFormat_RGBA_ETC2, 16, 4, 4, C, 0, 0, {0}},
    {GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC, DkImageFormat_RGBA_ETC2_sRGB, 16, 4, 4, C, 0, 0, {0}},
    {GL_ETC1_RGB8_OES, DkImageFormat_RGB_ETC2, 8, 4, 4, C, 0, 0, {0}},
};

#undef R
#undef E
#undef I
#undef F
#undef S
#undef X
#undef P
#undef Z
#undef ST
#undef C

static const uint8_t astc_blocks[14][2] = {
    {4, 4}, {5, 4}, {5, 5}, {6, 5}, {6, 6}, {8, 5}, {8, 6},
    {8, 8}, {10, 5}, {10, 6}, {10, 8}, {10, 10}, {12, 10}, {12, 12}};
static DkglFormat astc_formats[28];

static void formats_init(void) {
  for (int i = 0; i < 14; i++) {
    for (int srgb = 0; srgb < 2; srgb++) {
      DkglFormat *f = &astc_formats[srgb * 14 + i];
      f->internal = (srgb ? GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR
                          : GL_COMPRESSED_RGBA_ASTC_4x4_KHR) +
                    (GLenum)i;
      f->dk = (DkImageFormat)((srgb ? DkImageFormat_RGBA_ASTC_4x4_sRGB
                                    : DkImageFormat_RGBA_ASTC_4x4) +
                              i);
      f->bytes = 16;
      f->bw = astc_blocks[i][0];
      f->bh = astc_blocks[i][1];
      f->flags = DKGL_FMT_COMPRESSED;
    }
  }
}

const DkglFormat *dkgl_format(GLenum internal) {
  for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); i++)
    if (formats[i].internal == internal)
      return &formats[i];
  for (int i = 0; i < 28; i++)
    if (astc_formats[i].internal == internal)
      return &astc_formats[i];
  switch (internal) {
  case GL_LUMINANCE8_EXT:
    return dkgl_format(GL_LUMINANCE);
  case GL_ALPHA8_EXT:
    return dkgl_format(GL_ALPHA);
  case GL_LUMINANCE8_ALPHA8_EXT:
    return dkgl_format(GL_LUMINANCE_ALPHA);
  case GL_BGRA_EXT:
    return dkgl_format(GL_BGRA8_EXT);
  case GL_DEPTH_COMPONENT32_OES:
    return dkgl_format(GL_DEPTH_COMPONENT24);
  default:
    return NULL;
  }
}

const DkglFormat *dkgl_format_unsized(GLenum format, GLenum type) {
  const bool half = type == GL_HALF_FLOAT || type == GL_HALF_FLOAT_OES;
  switch (format) {
  case GL_RGBA:
    if (type == GL_UNSIGNED_BYTE)
      return dkgl_format(GL_RGBA8);
    if (half)
      return dkgl_format(GL_RGBA16F);
    if (type == GL_FLOAT)
      return dkgl_format(GL_RGBA32F);
    if (type == GL_UNSIGNED_SHORT_4_4_4_4)
      return dkgl_format(GL_RGBA4);
    if (type == GL_UNSIGNED_SHORT_5_5_5_1)
      return dkgl_format(GL_RGB5_A1);
    if (type == GL_UNSIGNED_INT_2_10_10_10_REV)
      return dkgl_format(GL_RGB10_A2);
    break;
  case GL_RGB:
    if (type == GL_UNSIGNED_BYTE)
      return dkgl_format(GL_RGB8);
    if (half)
      return dkgl_format(GL_RGB16F);
    if (type == GL_FLOAT)
      return dkgl_format(GL_RGB32F);
    if (type == GL_UNSIGNED_SHORT_5_6_5)
      return dkgl_format(GL_RGB565);
    break;
  case GL_RED:
    if (type == GL_UNSIGNED_BYTE)
      return dkgl_format(GL_R8);
    if (half)
      return dkgl_format(GL_R16F);
    if (type == GL_FLOAT)
      return dkgl_format(GL_R32F);
    break;
  case GL_RG:
    if (type == GL_UNSIGNED_BYTE)
      return dkgl_format(GL_RG8);
    if (half)
      return dkgl_format(GL_RG16F);
    if (type == GL_FLOAT)
      return dkgl_format(GL_RG32F);
    break;
  case GL_LUMINANCE:
  case GL_ALPHA:
  case GL_LUMINANCE_ALPHA:
    return dkgl_format(format);
  case GL_BGRA_EXT:
    return dkgl_format(GL_BGRA8_EXT);
  case GL_DEPTH_COMPONENT:
    if (type == GL_UNSIGNED_SHORT)
      return dkgl_format(GL_DEPTH_COMPONENT16);
    if (type == GL_FLOAT)
      return dkgl_format(GL_DEPTH_COMPONENT32F);
    return dkgl_format(GL_DEPTH_COMPONENT24);
  case GL_DEPTH_STENCIL:
    return dkgl_format(GL_DEPTH24_STENCIL8);
  default:
    break;
  }
  return NULL;
}

static uint32_t format_components(GLenum format) {
  switch (format) {
  case GL_RED:
  case GL_RED_INTEGER:
  case GL_ALPHA:
  case GL_LUMINANCE:
  case GL_DEPTH_COMPONENT:
  case GL_STENCIL_INDEX:
    return 1;
  case GL_RG:
  case GL_RG_INTEGER:
  case GL_LUMINANCE_ALPHA:
  case GL_DEPTH_STENCIL:
    return 2;
  case GL_RGB:
  case GL_RGB_INTEGER:
    return 3;
  default:
    return 4;
  }
}

uint32_t dkgl_client_bpp(GLenum format, GLenum type) {
  switch (type) {
  case GL_UNSIGNED_SHORT_5_6_5:
  case GL_UNSIGNED_SHORT_4_4_4_4:
  case GL_UNSIGNED_SHORT_5_5_5_1:
    return 2;
  case GL_UNSIGNED_INT_2_10_10_10_REV:
  case GL_UNSIGNED_INT_10F_11F_11F_REV:
  case GL_UNSIGNED_INT_5_9_9_9_REV:
  case GL_UNSIGNED_INT_24_8:
    return 4;
  case GL_FLOAT_32_UNSIGNED_INT_24_8_REV:
    return 8;
  case GL_UNSIGNED_BYTE:
  case GL_BYTE:
    return format_components(format);
  case GL_UNSIGNED_SHORT:
  case GL_SHORT:
  case GL_HALF_FLOAT:
  case GL_HALF_FLOAT_OES:
    return 2 * format_components(format);
  default:
    return 4 * format_components(format);
  }
}

static uint16_t float_to_half(float f) {
  union {
    float f;
    uint32_t u;
  } v = {f};
  uint32_t sign = (v.u >> 16) & 0x8000;
  int32_t exp = (int32_t)((v.u >> 23) & 0xff) - 127 + 15;
  uint32_t mant = v.u & 0x7fffff;
  if (((v.u >> 23) & 0xff) == 0xff)
    return (uint16_t)(sign | 0x7c00 | (mant ? 0x200 : 0));
  if (exp <= 0) {
    if (exp < -10)
      return (uint16_t)sign;
    mant |= 0x800000;
    return (uint16_t)(sign | (mant >> (14 - exp)));
  }
  if (exp >= 31)
    return (uint16_t)(sign | 0x7c00);
  return (uint16_t)(sign | ((uint32_t)exp << 10) | (mant >> 13));
}

/* Converts one row of w client pixels into the texture's storage layout.
 * Returns false for combinations that are not handled (the row is zeroed). */
static bool convert_row(const DkglFormat *f, GLenum format, GLenum type,
                        const uint8_t *src, uint8_t *dst, uint32_t w) {
  const uint32_t src_bpp = dkgl_client_bpp(format, type);
  const bool half = type == GL_HALF_FLOAT || type == GL_HALF_FLOAT_OES;
  /* Same bytes per texel and component layout: plain copy. */
  if (!(f->flags & (DKGL_FMT_RGB_TO_RGBX | DKGL_FMT_TO_RGBA8)) &&
      src_bpp == f->bytes &&
      !(format == GL_BGRA_EXT && f->dk != DkImageFormat_BGRA8_Unorm) &&
      !(type == GL_FLOAT && (f->flags & DKGL_FMT_FLOAT) && f->bytes != 4 * format_components(format))) {
    memcpy(dst, src, (size_t)w * f->bytes);
    return true;
  }
  if (f->flags & DKGL_FMT_RGB_TO_RGBX) {
    if (type == GL_UNSIGNED_BYTE || type == GL_BYTE) {
      for (uint32_t x = 0; x < w; x++) {
        dst[4 * x + 0] = src[3 * x + 0];
        dst[4 * x + 1] = src[3 * x + 1];
        dst[4 * x + 2] = src[3 * x + 2];
        dst[4 * x + 3] = 0xff;
      }
      return true;
    }
    if (half && f->bytes == 8) {
      const uint16_t *s = (const uint16_t *)src;
      uint16_t *d = (uint16_t *)dst;
      for (uint32_t x = 0; x < w; x++) {
        d[4 * x + 0] = s[3 * x + 0];
        d[4 * x + 1] = s[3 * x + 1];
        d[4 * x + 2] = s[3 * x + 2];
        d[4 * x + 3] = 0x3c00;
      }
      return true;
    }
    if (type == GL_FLOAT && f->bytes == 8) {
      const float *s = (const float *)src;
      uint16_t *d = (uint16_t *)dst;
      for (uint32_t x = 0; x < w; x++) {
        d[4 * x + 0] = float_to_half(s[3 * x + 0]);
        d[4 * x + 1] = float_to_half(s[3 * x + 1]);
        d[4 * x + 2] = float_to_half(s[3 * x + 2]);
        d[4 * x + 3] = 0x3c00;
      }
      return true;
    }
  }
  if (f->flags & DKGL_FMT_TO_RGBA8) {
    const uint16_t *s = (const uint16_t *)src;
    for (uint32_t x = 0; x < w; x++) {
      uint8_t *d = dst + 4 * x;
      if (type == GL_UNSIGNED_SHORT_5_6_5) {
        uint16_t p = s[x];
        d[0] = (uint8_t)(((p >> 11) & 31) * 255 / 31);
        d[1] = (uint8_t)(((p >> 5) & 63) * 255 / 63);
        d[2] = (uint8_t)((p & 31) * 255 / 31);
        d[3] = 255;
      } else if (type == GL_UNSIGNED_SHORT_4_4_4_4) {
        uint16_t p = s[x];
        d[0] = (uint8_t)(((p >> 12) & 15) * 17);
        d[1] = (uint8_t)(((p >> 8) & 15) * 17);
        d[2] = (uint8_t)(((p >> 4) & 15) * 17);
        d[3] = (uint8_t)((p & 15) * 17);
      } else if (type == GL_UNSIGNED_SHORT_5_5_5_1) {
        uint16_t p = s[x];
        d[0] = (uint8_t)(((p >> 11) & 31) * 255 / 31);
        d[1] = (uint8_t)(((p >> 6) & 31) * 255 / 31);
        d[2] = (uint8_t)(((p >> 1) & 31) * 255 / 31);
        d[3] = (p & 1) ? 255 : 0;
      } else if (type == GL_UNSIGNED_BYTE && format == GL_RGB) {
        d[0] = src[3 * x];
        d[1] = src[3 * x + 1];
        d[2] = src[3 * x + 2];
        d[3] = 255;
      } else if (type == GL_UNSIGNED_BYTE) {
        memcpy(d, src + 4 * x, 4);
      } else {
        return false;
      }
    }
    return true;
  }
  if (f->flags & DKGL_FMT_DEPTH && !(f->flags & DKGL_FMT_STENCIL) &&
      type == GL_UNSIGNED_INT) {
    /* 32-bit normalised depth into Z16 (top 16 bits) or Z24X8 (top 24) */
    const uint32_t *s = (const uint32_t *)src;
    for (uint32_t x = 0; x < w; x++) {
      if (f->bytes == 2)
        ((uint16_t *)dst)[x] = (uint16_t)(s[x] >> 16);
      else
        ((uint32_t *)dst)[x] = s[x] >> 8;
    }
    return true;
  }
  if (format == GL_BGRA_EXT && type == GL_UNSIGNED_BYTE && f->bytes == 4) {
    for (uint32_t x = 0; x < w; x++) {
      dst[4 * x + 0] = src[4 * x + 2];
      dst[4 * x + 1] = src[4 * x + 1];
      dst[4 * x + 2] = src[4 * x + 0];
      dst[4 * x + 3] = src[4 * x + 3];
    }
    return true;
  }
  if (type == GL_FLOAT && (f->flags & DKGL_FMT_FLOAT) &&
      f->bytes == 2 * format_components(format)) {
    const float *s = (const float *)src;
    uint16_t *d = (uint16_t *)dst;
    for (uint32_t i = 0; i < w * format_components(format); i++)
      d[i] = float_to_half(s[i]);
    return true;
  }
  memset(dst, 0, (size_t)w * f->bytes);
  return false;
}

/* ------------------------------------------------------------ objects */

static void sampler_defaults(DkglSamplerState *s) {
  s->min_filter = GL_NEAREST_MIPMAP_LINEAR;
  s->mag_filter = GL_LINEAR;
  s->wrap_s = s->wrap_t = s->wrap_r = GL_REPEAT;
  s->compare_mode = GL_NONE;
  s->compare_func = GL_LEQUAL;
  s->min_lod = -1000.0f;
  s->max_lod = 1000.0f;
  s->max_aniso = 0.0f; /* 0 = never set */
  s->border[0] = s->border[1] = s->border[2] = s->border[3] = 0.0f;
}

DkglTexture *dkgl_texture(GLuint name, bool create) {
  if (!name)
    return NULL;
  DkglTexture *tex = dkgl_name_get(DKGL_OBJ_TEXTURE, name);
  if (tex || !create)
    return tex;
  tex = calloc(1, sizeof(*tex));
  if (!tex)
    return NULL;
  tex->name = name;
  sampler_defaults(&tex->sampler);
  tex->max_level = 1000;
  tex->swizzle[0] = GL_RED;
  tex->swizzle[1] = GL_GREEN;
  tex->swizzle[2] = GL_BLUE;
  tex->swizzle[3] = GL_ALPHA;
  tex->ds_mode = GL_DEPTH_COMPONENT;
  tex->view_slot = -1;
  tex->store_slot = -1;
  tex->sampler_dirty = true;
  tex->view_dirty = true;
  dkgl_name_set(DKGL_OBJ_TEXTURE, name, tex);
  return tex;
}

int dkgl_target_index(GLenum target) {
  switch (target) {
  case GL_TEXTURE_2D:
    return DKGL_TEX_2D;
  case GL_TEXTURE_3D:
    return DKGL_TEX_3D;
  case GL_TEXTURE_CUBE_MAP:
  case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
  case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
  case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
  case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
  case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
  case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
    return DKGL_TEX_CUBE;
  case GL_TEXTURE_2D_ARRAY:
    return DKGL_TEX_2D_ARRAY;
  case GL_TEXTURE_BUFFER:
    return DKGL_TEX_BUFFER;
  case GL_TEXTURE_CUBE_MAP_ARRAY:
    return DKGL_TEX_CUBE_ARRAY;
  case GL_TEXTURE_2D_MULTISAMPLE:
    return DKGL_TEX_2D_MS;
  case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
    return DKGL_TEX_2D_MS_ARRAY;
  case GL_TEXTURE_EXTERNAL_OES:
    return DKGL_TEX_2D;
  default:
    return -1;
  }
}

DkglTexture *dkgl_bound_texture(DkglContext *ctx, GLenum target) {
  int index = dkgl_target_index(target);
  if (index < 0)
    return NULL;
  return dkgl_texture(ctx->textures[ctx->active_texture][index], false);
}

/* Colour formats whose deko3d traits lack CanLoadStore (the debug library
 * rejects DkImageFlags_UsageLoadStore on them). BGRA8 has the trait but is
 * kept off: UE never binds it as an image. */
static bool format_load_store(DkImageFormat dk) {
  switch (dk) {
  case DkImageFormat_RGBX8_Unorm:
  case DkImageFormat_RGBX8_Snorm:
  case DkImageFormat_RGBX8_Uint:
  case DkImageFormat_RGBX8_Sint:
  case DkImageFormat_RGBX16_Float:
  case DkImageFormat_RGBX16_Uint:
  case DkImageFormat_RGBX16_Sint:
  case DkImageFormat_RGBX8_Unorm_sRGB:
  case DkImageFormat_RGBA8_Unorm_sRGB:
  case DkImageFormat_BGRA8_Unorm:
  case DkImageFormat_E5BGR9_Float:
  case DkImageFormat_RGB32_Float:
  case DkImageFormat_RGB32_Uint:
  case DkImageFormat_RGB32_Sint:
    return false;
  default:
    return true;
  }
}

static DkImageType image_type(GLenum target) {
  switch (dkgl_target_index(target)) {
  case DKGL_TEX_3D:
    return DkImageType_3D;
  case DKGL_TEX_CUBE:
    return DkImageType_Cubemap;
  case DKGL_TEX_2D_ARRAY:
    return DkImageType_2DArray;
  case DKGL_TEX_CUBE_ARRAY:
    return DkImageType_CubemapArray;
  case DKGL_TEX_BUFFER:
    return DkImageType_Buffer;
  default:
    return DkImageType_2D;
  }
}

/* Whether level `level` of size w x h x d belongs to the texture's current
 * image. A level's size is its base size halved per level and clamped to
 * 1, so the base cannot be rebuilt as w << level: the 1-texel side of a
 * non-square texture's small levels gave a square base, and the texture
 * was reallocated with every level uploaded before it lost. */
static bool level_fits(const DkglTexture *tex, const DkglFormat *fmt, int index,
                       GLint level, uint32_t w, uint32_t h, uint32_t d) {
  if (!tex->has_storage || tex->fmt != fmt)
    return false;
  const uint32_t lw = tex->width >> level, lh = tex->height >> level;
  if ((lw ? lw : 1) != (w ? w : 1) || (lh ? lh : 1) != (h ? h : 1))
    return false;
  if (index == DKGL_TEX_3D) {
    const uint32_t ld = tex->depth >> level;
    return (ld ? ld : 1) == (d ? d : 1);
  }
  return index == DKGL_TEX_CUBE || tex->depth == (d ? d : 1);
}

static uint32_t full_levels(uint32_t w, uint32_t h, uint32_t d) {
  uint32_t m = w > h ? w : h;
  if (d > m)
    m = d;
  uint32_t levels = 1;
  while (m > 1) {
    m >>= 1;
    levels++;
  }
  return levels;
}

void dkgl_texture_used(DkglTexture *tex) { tex->last_use = dkg.serial; }

static void tex_release_storage(DkglTexture *tex) {
  if (tex->view_slot > 0)
    dkgl_image_slot_free(tex->view_slot);
  if (tex->store_slot > 0)
    dkgl_image_slot_free(tex->store_slot);
  tex->view_slot = -1;
  tex->store_slot = -1;
  dkgl_free_deferred(&tex->mem);
  tex->has_storage = false;
}

/* Builds image storage. Existing levels are copied into the new image when
 * keep_levels is non-zero (growing a mutable mip chain). */
static bool tex_allocate(DkglTexture *tex, GLenum target, const DkglFormat *fmt,
                         uint32_t w, uint32_t h, uint32_t d, uint32_t levels,
                         uint32_t keep_levels) {
  const int index = dkgl_target_index(target);
  DkImageLayoutMaker maker;
  dkImageLayoutMakerDefaults(&maker, dkg.device);
  maker.type = image_type(target);
  maker.format = fmt->dk;
  maker.mipLevels = levels;
  maker.dimensions[0] = w ? w : 1;
  maker.dimensions[1] = h ? h : 1;
  maker.dimensions[2] = 1;
  if (index == DKGL_TEX_3D || index == DKGL_TEX_2D_ARRAY)
    maker.dimensions[2] = d ? d : 1;
  else if (index == DKGL_TEX_CUBE_ARRAY)
    maker.dimensions[2] = d >= 6 ? d / 6 : 1;
  if (fmt->flags & DKGL_FMT_RENDER)
    maker.flags |= DkImageFlags_UsageRender;
  if ((fmt->flags & DKGL_FMT_2D_ENGINE) && index != DKGL_TEX_3D)
    maker.flags |= DkImageFlags_Usage2DEngine;
  if (!(fmt->flags & (DKGL_FMT_COMPRESSED | DKGL_FMT_DEPTH | DKGL_FMT_STENCIL)) &&
      format_load_store(fmt->dk))
    maker.flags |= DkImageFlags_UsageLoadStore;
  const bool compress = tex->want_compression;
  if (compress) /* load/store cannot address a compressed surface */
    maker.flags = (maker.flags & ~DkImageFlags_UsageLoadStore) |
                  DkImageFlags_HwCompression;
  DkImageLayout layout;
  dkImageLayoutInitialize(&layout, &maker);
  DkglAlloc mem;
  const uint64_t size = dkImageLayoutGetSize(&layout);
  if (size > 0xfffff000ull ||
      !dkgl_alloc(DKGL_HEAP_IMAGE, (uint32_t)size,
                  dkImageLayoutGetAlignment(&layout), &mem)) {
    debugPrintf("dkgl: texture %u allocation failed (%ux%ux%u levels=%u "
                "format=0x%x size=%llu)\n",
                tex->name, w, h, d, levels, fmt->internal,
                (unsigned long long)size);
    return false;
  }
  DkImage image;
  dkImageInitialize(&image, &layout, mem.block, mem.offset);

  if (keep_levels && tex->has_storage) {
    dkgl_texture_prepare_transfer(tex);
    dkgl_before_copy();
    DkCmdBuf cmd = dkgl_cmd();
    for (uint32_t level = 0; level < keep_levels && level < levels; level++) {
      if (!(tex->level_defined & (1u << level)))
        continue;
      DkImageView src, dst;
      dkImageViewDefaults(&src, &tex->image);
      dkImageViewDefaults(&dst, &image);
      src.mipLevelOffset = dst.mipLevelOffset = (uint8_t)level;
      uint32_t lw = tex->width >> level, lh = tex->height >> level;
      uint32_t ld = index == DKGL_TEX_3D ? tex->depth >> level : tex->depth;
      if (index == DKGL_TEX_CUBE)
        ld = 6;
      DkImageRect rect = {0, 0, 0, lw ? lw : 1, lh ? lh : 1, ld ? ld : 1};
      dkCmdBufCopyImage(cmd, &src, &rect, &dst, &rect, 0);
    }
  } else {
    tex->level_defined = 0;
  }
  if (tex->has_storage)
    tex_release_storage(tex);
  tex->layout = layout;
  tex->image = image;
  tex->mem = mem;
  tex->fmt = fmt;
  tex->internal_format = fmt->internal;
  tex->width = w ? w : 1;
  tex->height = h ? h : 1;
  tex->depth = index == DKGL_TEX_CUBE ? 6 : (d ? d : 1);
  tex->levels = levels;
  tex->has_storage = true;
  tex->compressed = compress;
  tex->generation++;
  tex->view_dirty = true;
  dkgl_texture_epoch++;
  return true;
}

/* ---------------------------------------------- render-target compression */

/* Opt-in since r65 (dkgl_rt_compression_on in the runtime folder): r61 on
 * hardware showed no measurable GPU-time gain, and the goal frames' glow
 * started flickering in the builds that had it. */
static int rt_compression = -1;

static bool rt_compressible(const DkglFormat *f) {
  switch (f->dk) {
  case DkImageFormat_RGBA8_Unorm:
  case DkImageFormat_RGBA8_Unorm_sRGB:
  case DkImageFormat_BGRA8_Unorm:
  case DkImageFormat_RGB10A2_Unorm:
  case DkImageFormat_RG11B10_Float:
  case DkImageFormat_RGBA16_Float:
    return true;
  default:
    return false;
  }
}

void dkgl_texture_promote_rt(DkglTexture *tex) {
  if (!tex || tex->rt_checked || !tex->has_storage)
    return;
  tex->rt_checked = true;
  if (rt_compression < 0) {
    FILE *f = fopen("dkgl_rt_compression_on", "rb");
    rt_compression = f != NULL;
    if (f)
      fclose(f);
    debugPrintf("dkgl: colour render-target compression %s\n",
                rt_compression ? "on (dkgl_rt_compression_on)" : "off");
  }
  /* Only a target whose contents are still undefined can be reallocated.
   * Tiny targets (UE's 1x1 eye-adaptation and histogram outputs, which
   * compute writes as storage images after clearing them as targets) gain
   * nothing and stay uncompressed. */
  if (!rt_compression || tex->level_defined || tex->levels != 1 ||
      tex->width * tex->height < 64 * 64 ||
      dkgl_target_index(tex->target) != DKGL_TEX_2D || !rt_compressible(tex->fmt))
    return;
  tex->want_compression = true;
  if (!tex_allocate(tex, tex->target, tex->fmt, tex->width, tex->height, 1, 1, 0))
    debugPrintf("dkgl: texture %u: compressed reallocation failed\n", tex->name);
  tex->want_compression = false;
}

void dkgl_texture_prepare_transfer(DkglTexture *tex) {
  if (!tex || !tex->compressed || !tex->has_storage)
    return;
  dkgl_before_3d();
  dkgl_rt_barrier();
  dkCmdBufDecompressImage(dkgl_cmd(), &tex->image);
}

/* ----------------------------------------------------------- dummies */

static DkImage dummy_images[DKGL_TEX_TARGETS];
static DkImageLayout dummy_layouts[DKGL_TEX_TARGETS];
static int dummy_slots[DKGL_TEX_TARGETS];
static DkglAlloc dummy_mem[DKGL_TEX_TARGETS];

void dkgl_textures_init(void) {
  formats_init();
  dkgl_policy_aniso = 1;     /* (Unity asks for anisotropy itself) */
  dkgl_policy_trilinear = 0;
  /* sampler slot 0: buffer textures and the stand-ins below use it */
  DkSampler default_sampler;
  memset(&default_sampler, 0, sizeof(default_sampler));
  dkSamplerDefaults(&default_sampler);
  dkgl_sampler_slot(&default_sampler);
  const DkglFormat *rgba8 = dkgl_format(GL_RGBA8);
  for (int t = 0; t < DKGL_TEX_TARGETS; t++) {
    DkImageLayoutMaker maker;
    dkImageLayoutMakerDefaults(&maker, dkg.device);
    maker.format = rgba8->dk;
    maker.dimensions[0] = maker.dimensions[1] = maker.dimensions[2] = 1;
    switch (t) {
    case DKGL_TEX_3D:
      maker.type = DkImageType_3D;
      break;
    case DKGL_TEX_CUBE:
      maker.type = DkImageType_Cubemap;
      break;
    case DKGL_TEX_2D_ARRAY:
      maker.type = DkImageType_2DArray;
      break;
    case DKGL_TEX_CUBE_ARRAY:
      maker.type = DkImageType_CubemapArray;
      break;
    case DKGL_TEX_BUFFER:
      maker.type = DkImageType_Buffer;
      maker.dimensions[1] = 0;
      break;
    default:
      maker.type = DkImageType_2D;
      break;
    }
    dkImageLayoutInitialize(&dummy_layouts[t], &maker);
    const uint32_t size = (uint32_t)dkImageLayoutGetSize(&dummy_layouts[t]);
    const int heap = t == DKGL_TEX_BUFFER ? DKGL_HEAP_DATA : DKGL_HEAP_IMAGE;
    if (!dkgl_alloc(heap, size ? size : 256,
                    dkImageLayoutGetAlignment(&dummy_layouts[t]), &dummy_mem[t]))
      fatal_error("dkgl: dummy texture allocation failed");
    dkImageInitialize(&dummy_images[t], &dummy_layouts[t], dummy_mem[t].block,
                      dummy_mem[t].offset);
    /* Missing textures sample as (0,0,0,1), like an incomplete GL texture. */
    DkglRing staging;
    static const uint8_t black[4 * 6] = {0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255,
                                         0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
    if (t == DKGL_TEX_BUFFER) {
      memcpy(dkgl_cpu(&dummy_mem[t]), black, 4);
    } else if (dkgl_ring_alloc(sizeof(black), 16, &staging)) {
      memcpy(staging.cpu, black, sizeof(black));
      DkImageView view;
      dkImageViewDefaults(&view, &dummy_images[t]);
      const uint32_t layers =
          (t == DKGL_TEX_CUBE || t == DKGL_TEX_CUBE_ARRAY) ? 6 : 1;
      DkImageRect rect = {0, 0, 0, 1, 1, layers};
      DkCopyBuf src = {staging.gpu, 4, 4};
      dkgl_before_copy();
      dkCmdBufCopyBufferToImage(dkgl_cmd(), &src, &view, &rect, 0);
    }
    dummy_slots[t] = dkgl_image_slot_alloc();
    DkImageView view;
    dkImageViewDefaults(&view, &dummy_images[t]);
    dkgl_write_image_descriptor(dummy_slots[t], &view, false);
  }
  dkgl_flush();
}

/* ------------------------------------------------------------- views */

static DkImageSwizzle gl_swizzle(GLenum s, const uint8_t fmt_swz[4], int lane) {
  switch (s) {
  case GL_RED:
    return fmt_swz[0] ? (DkImageSwizzle)(fmt_swz[0] - 1) : DkImageSwizzle_Red;
  case GL_GREEN:
    return fmt_swz[1] ? (DkImageSwizzle)(fmt_swz[1] - 1) : DkImageSwizzle_Green;
  case GL_BLUE:
    return fmt_swz[2] ? (DkImageSwizzle)(fmt_swz[2] - 1) : DkImageSwizzle_Blue;
  case GL_ALPHA:
    return fmt_swz[3] ? (DkImageSwizzle)(fmt_swz[3] - 1) : DkImageSwizzle_Alpha;
  case GL_ZERO:
    return DkImageSwizzle_Zero;
  case GL_ONE:
    return DkImageSwizzle_One;
  default:
    (void)lane;
    return DkImageSwizzle_Red;
  }
}

/* A view of one level/layer for render targets and copies. */
bool dkgl_texture_view(DkglTexture *tex, GLint level, GLint layer,
                       DkImageView *view) {
  if (!tex || !tex->has_storage || level < 0 || (uint32_t)level >= tex->levels)
    return false;
  dkImageViewDefaults(view, &tex->image);
  view->mipLevelOffset = (uint8_t)level;
  view->mipLevelCount = 1;
  const int index = dkgl_target_index(tex->target);
  if (index == DKGL_TEX_CUBE || index == DKGL_TEX_2D_ARRAY ||
      index == DKGL_TEX_CUBE_ARRAY || index == DKGL_TEX_3D) {
    view->type = DkImageType_2D;
    view->layerOffset = (uint16_t)(layer > 0 ? layer : 0);
    view->layerCount = 1;
  }
  return true;
}

static uint32_t view_key(const DkglTexture *tex) {
  uint32_t k = tex->generation * 2654435761u;
  k ^= (uint32_t)tex->base_level * 31u + (uint32_t)tex->max_level * 131u;
  k ^= tex->level_defined * 2246822519u;
  for (int i = 0; i < 4; i++)
    k = k * 7u + (uint32_t)tex->swizzle[i];
  k ^= tex->ds_mode * 17u;
  k ^= tex->tbo_version * 977u + tex->tbo_offset * 13u + tex->tbo_size;
  return k;
}

static bool tex_buffer_image(DkglTexture *tex, DkImage *image) {
  DkglBuffer *buf = dkgl_buffer(tex->tbo_buffer, false);
  if (!buf || !buf->mem.node || !tex->fmt)
    return false;
  uint32_t size = tex->tbo_size ? tex->tbo_size : buf->size - tex->tbo_offset;
  if (tex->tbo_offset >= buf->size)
    return false;
  if (tex->tbo_offset + size > buf->size)
    size = buf->size - tex->tbo_offset;
  DkImageLayoutMaker maker;
  dkImageLayoutMakerDefaults(&maker, dkg.device);
  maker.type = DkImageType_Buffer;
  maker.format = tex->fmt->dk;
  maker.dimensions[0] = size / tex->fmt->bytes;
  if (!maker.dimensions[0])
    return false;
  dkImageLayoutInitialize(&tex->layout, &maker);
  dkImageInitialize(image, &tex->layout, buf->mem.block,
                    buf->mem.offset + tex->tbo_offset);
  tex->tbo_version = buf->version;
  dkgl_buffer_used(buf);
  return true;
}

static int tex_view_slot(DkglTexture *tex) {
  const int index = dkgl_target_index(tex->target);
  if (index == DKGL_TEX_BUFFER) {
    DkglBuffer *buf = dkgl_buffer(tex->tbo_buffer, false);
    if (buf && buf->version != tex->tbo_version)
      tex->view_dirty = true;
    if (buf)
      dkgl_buffer_used(buf);
  }
  if (!tex->view_dirty && tex->view_slot > 0)
    return tex->view_slot;
  const uint32_t key = view_key(tex);
  if (tex->view_slot > 0 && key == tex->view_key && index != DKGL_TEX_BUFFER) {
    tex->view_dirty = false;
    return tex->view_slot;
  }
  DkImageView view;
  if (index == DKGL_TEX_BUFFER) {
    if (!tex_buffer_image(tex, &tex->image))
      return -1;
    dkImageViewDefaults(&view, &tex->image);
  } else {
    if (!tex->has_storage)
      return -1;
    dkImageViewDefaults(&view, &tex->image);
    uint32_t base = tex->base_level < 0 ? 0 : (uint32_t)tex->base_level;
    if (base >= tex->levels)
      base = tex->levels - 1;
    uint32_t max = tex->max_level < 0 ? 0 : (uint32_t)tex->max_level;
    if (max >= tex->levels)
      max = tex->levels - 1;
    /* a mutable texture: only the levels defined so far, from the base on
     * (its image holds the whole chain, the rest never written) */
    if (!tex->immutable && tex->level_defined) {
      uint32_t top = base;
      while (top + 1 <= max && (tex->level_defined & (1u << (top + 1))))
        top++;
      max = top;
    }
    if (max < base)
      max = base;
    view.mipLevelOffset = (uint8_t)base;
    view.mipLevelCount = (uint8_t)(max - base + 1);
    const uint8_t *fs = tex->fmt->swizzle;
    for (int i = 0; i < 4; i++)
      view.swizzle[i] = gl_swizzle(tex->swizzle[i], fs, i);
    if ((tex->fmt->flags & DKGL_FMT_STENCIL) &&
        (!(tex->fmt->flags & DKGL_FMT_DEPTH) ||
         tex->ds_mode == GL_STENCIL_INDEX)) {
      view.dsSource = DkDsSource_Stencil;
      for (int i = 0; i < 4; i++)
        view.swizzle[i] = i == 0 ? DkImageSwizzle_Red
                                 : (i == 3 ? DkImageSwizzle_One : DkImageSwizzle_Zero);
    }
  }
  if (tex->view_slot > 0)
    dkgl_image_slot_free(tex->view_slot);
  tex->view_slot = dkgl_image_slot_alloc();
  dkgl_write_image_descriptor(tex->view_slot, &view, false);
  tex->view_key = key;
  tex->view_dirty = false;
  return tex->view_slot;
}

/* ---------------------------------------------------------- samplers */

static DkWrapMode wrap_mode(GLenum w) {
  switch (w) {
  case GL_CLAMP_TO_EDGE:
    return DkWrapMode_ClampToEdge;
  case GL_MIRRORED_REPEAT:
    return DkWrapMode_MirroredRepeat;
  case GL_CLAMP_TO_BORDER:
    return DkWrapMode_ClampToBorder;
  default:
    return DkWrapMode_Repeat;
  }
}

static DkCompareOp compare_op(GLenum f) {
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

int dkgl_policy_aniso = 8, dkgl_policy_trilinear = 1;
static uint32_t sampler_policy_gen;

void dkgl_set_sampler_policy(int aniso, int trilinear) {
  if (aniso == dkgl_policy_aniso && trilinear == dkgl_policy_trilinear)
    return;
  dkgl_policy_aniso = aniso;
  dkgl_policy_trilinear = trilinear;
  sampler_policy_gen++; /* cached sampler slots are rebuilt at next use */
  dkgl_texture_epoch++;
}

static uint32_t sampler_slot_for(const DkglSamplerState *s, bool integer) {
  DkSampler d;
  memset(&d, 0, sizeof(d));
  dkSamplerDefaults(&d);
  GLenum min = s->min_filter;
  if (min == GL_LINEAR_MIPMAP_NEAREST && dkgl_policy_trilinear)
    min = GL_LINEAR_MIPMAP_LINEAR;
  switch (min) {
  case GL_NEAREST:
    d.minFilter = DkFilter_Nearest;
    d.mipFilter = DkMipFilter_None;
    break;
  case GL_LINEAR:
    d.minFilter = DkFilter_Linear;
    d.mipFilter = DkMipFilter_None;
    break;
  case GL_NEAREST_MIPMAP_NEAREST:
    d.minFilter = DkFilter_Nearest;
    d.mipFilter = DkMipFilter_Nearest;
    break;
  case GL_LINEAR_MIPMAP_NEAREST:
    d.minFilter = DkFilter_Linear;
    d.mipFilter = DkMipFilter_Nearest;
    break;
  case GL_NEAREST_MIPMAP_LINEAR:
    d.minFilter = DkFilter_Nearest;
    d.mipFilter = DkMipFilter_Linear;
    break;
  default:
    d.minFilter = DkFilter_Linear;
    d.mipFilter = DkMipFilter_Linear;
    break;
  }
  d.magFilter = s->mag_filter == GL_NEAREST ? DkFilter_Nearest : DkFilter_Linear;
  if (integer) {
    d.minFilter = d.magFilter = DkFilter_Nearest;
    if (d.mipFilter == DkMipFilter_Linear)
      d.mipFilter = DkMipFilter_Nearest;
  }
  d.wrapMode[0] = wrap_mode(s->wrap_s);
  d.wrapMode[1] = wrap_mode(s->wrap_t);
  d.wrapMode[2] = wrap_mode(s->wrap_r);
  d.lodClampMin = s->min_lod < 0.0f ? 0.0f : s->min_lod;
  d.lodClampMax = s->max_lod;
  if (s->compare_mode == GL_COMPARE_REF_TO_TEXTURE) {
    d.compareEnable = true;
    d.compareOp = compare_op(s->compare_func);
  }
  for (int i = 0; i < 4; i++)
    d.borderColor[i].value_f = s->border[i];
  /* r54 policy: a sampler that asks for anisotropy gets at least the
   * configured level (UE applies r.MaxAnisotropy this way). */
  float aniso = s->max_aniso;
  if (aniso > 0.0f && dkgl_policy_aniso > aniso)
    aniso = (float)dkgl_policy_aniso;
  if (aniso < 1.0f)
    aniso = 1.0f;
  if (aniso > 16.0f)
    aniso = 16.0f;
  d.maxAnisotropy = d.mipFilter == DkMipFilter_None ? 1.0f : aniso;
  return dkgl_sampler_slot(&d);
}

static DkglSampler *sampler_object(GLuint name) {
  return name ? dkgl_name_get(DKGL_OBJ_SAMPLER, name) : NULL;
}

DkResHandle dkgl_texture_handle(DkglContext *ctx, GLuint unit, int target,
                                bool shadow) {
  (void)shadow;
  if (unit >= DKGL_TEXTURE_UNITS || target < 0 || target >= DKGL_TEX_TARGETS)
    return dkMakeTextureHandle(dummy_slots[0], 0);
  DkglTexture *tex = dkgl_texture(ctx->textures[unit][target], false);
  int slot = tex ? tex_view_slot(tex) : -1;
  if (slot <= 0)
    return dkMakeTextureHandle(dummy_slots[target], 0);
  dkgl_texture_used(tex);
  uint32_t sampler_slot = 0;
  if (target != DKGL_TEX_BUFFER) {
    const bool integer = tex->fmt && (tex->fmt->flags & DKGL_FMT_INTEGER);
    DkglSampler *smp = sampler_object(ctx->samplers[unit]);
    if (smp) {
      if (smp->dirty || smp->gen != sampler_policy_gen) {
        smp->slot = sampler_slot_for(&smp->state, false);
        smp->dirty = false;
        smp->gen = sampler_policy_gen;
      }
      sampler_slot = integer ? sampler_slot_for(&smp->state, true) : smp->slot;
    } else {
      if (tex->sampler_dirty || tex->sampler_gen != sampler_policy_gen) {
        tex->sampler_slot = sampler_slot_for(&tex->sampler, integer);
        tex->sampler_dirty = false;
        tex->sampler_gen = sampler_policy_gen;
      }
      sampler_slot = tex->sampler_slot;
    }
  }
  return dkMakeTextureHandle((uint32_t)slot, sampler_slot);
}

DkResHandle dkgl_image_handle(DkglContext *ctx, GLuint unit, int target) {
  (void)target;
  if (unit >= DKGL_IMAGE_UNITS)
    return dkMakeImageHandle(dummy_slots[0]);
  DkglTexture *tex = dkgl_texture(ctx->images[unit].texture, false);
  if (!tex)
    return dkMakeImageHandle(dummy_slots[0]);
  const int index = dkgl_target_index(tex->target);
  const DkglFormat *fmt = dkgl_format(ctx->images[unit].format);
  uint32_t key = tex->generation * 2654435761u ^
                 (uint32_t)ctx->images[unit].level * 131u ^
                 (uint32_t)(ctx->images[unit].layered ? 0xffff : ctx->images[unit].layer) * 7u ^
                 (fmt ? fmt->dk : 0) * 977u;
  if (index == DKGL_TEX_BUFFER) {
    DkglBuffer *buf = dkgl_buffer(tex->tbo_buffer, false);
    if (buf) {
      key ^= buf->version * 31337u;
      dkgl_buffer_used(buf);
    }
  }
  if (tex->store_slot > 0 && tex->store_key == key)
    return dkMakeImageHandle((uint32_t)tex->store_slot);
  DkImageView view;
  if (index == DKGL_TEX_BUFFER) {
    if (!tex_buffer_image(tex, &tex->image))
      return dkMakeImageHandle(dummy_slots[DKGL_TEX_BUFFER]);
    dkImageViewDefaults(&view, &tex->image);
  } else {
    if (!tex->has_storage)
      return dkMakeImageHandle(dummy_slots[0]);
    dkImageViewDefaults(&view, &tex->image);
    view.mipLevelOffset = (uint8_t)ctx->images[unit].level;
    view.mipLevelCount = 1;
    if (!ctx->images[unit].layered &&
        (index == DKGL_TEX_2D_ARRAY || index == DKGL_TEX_CUBE ||
         index == DKGL_TEX_3D)) {
      view.type = DkImageType_2D;
      view.layerOffset = (uint16_t)ctx->images[unit].layer;
      view.layerCount = 1;
    }
  }
  if (fmt && fmt->dk != tex->fmt->dk)
    view.format = fmt->dk;
  if (tex->compressed)
    dkgl_log_once("storage image bound on a compressed render target");
  if (tex->store_slot > 0)
    dkgl_image_slot_free(tex->store_slot);
  tex->store_slot = dkgl_image_slot_alloc();
  dkgl_write_image_descriptor(tex->store_slot, &view, true);
  tex->store_key = key;
  dkgl_texture_used(tex);
  return dkMakeImageHandle((uint32_t)tex->store_slot);
}

/* ------------------------------------------------------------ uploads */

void dkgl_upload_pixels(DkglContext *ctx, DkglTexture *tex, GLint level,
                        GLint x, GLint y, GLint z, GLsizei w, GLsizei h,
                        GLsizei d, GLenum format, GLenum type,
                        const void *pixels, bool compressed,
                        GLsizei image_size) {
  if (!tex || !tex->has_storage || w <= 0 || h <= 0 || d <= 0)
    return;
  const DkglFormat *f = tex->fmt;
  DkglBuffer *pbo = ctx->pixel_unpack ? dkgl_buffer(ctx->pixel_unpack, false) : NULL;
  if (!pbo && !pixels)
    return;
  dkgl_texture_prepare_transfer(tex);

  uint32_t src_pitch, src_slice, rows, row_bytes, dst_pitch;
  uintptr_t src_offset = 0;
  bool direct;
  if (compressed || (f->flags & DKGL_FMT_COMPRESSED)) {
    const uint32_t bx = ((uint32_t)w + f->bw - 1) / f->bw;
    const uint32_t by = ((uint32_t)h + f->bh - 1) / f->bh;
    row_bytes = bx * f->bytes;
    rows = by;
    src_pitch = row_bytes;
    src_slice = row_bytes * by;
    dst_pitch = row_bytes;
    direct = true;
    (void)image_size;
  } else {
    const uint32_t bpp = dkgl_client_bpp(format, type);
    const uint32_t row_length =
        ctx->unpack_row_length > 0 ? (uint32_t)ctx->unpack_row_length : (uint32_t)w;
    const uint32_t align = ctx->unpack_alignment > 0 ? (uint32_t)ctx->unpack_alignment : 4;
    src_pitch = (row_length * bpp + align - 1) / align * align;
    const uint32_t image_height = ctx->unpack_image_height > 0
                                      ? (uint32_t)ctx->unpack_image_height
                                      : (uint32_t)h;
    src_slice = src_pitch * image_height;
    src_offset = (uintptr_t)ctx->unpack_skip_images * src_slice +
                 (uintptr_t)ctx->unpack_skip_rows * src_pitch +
                 (uintptr_t)ctx->unpack_skip_pixels * bpp;
    rows = (uint32_t)h;
    row_bytes = (uint32_t)w * f->bytes;
    dst_pitch = row_bytes;
    direct = bpp == f->bytes &&
             !(f->flags & (DKGL_FMT_RGB_TO_RGBX | DKGL_FMT_TO_RGBA8)) &&
             !(format == GL_BGRA_EXT && f->dk != DkImageFormat_BGRA8_Unorm) &&
             !(type == GL_FLOAT && f->bytes != 4 * format_components(format));
  }

  DkCopyBuf src;
  if (pbo && direct && pbo->mem.node) {
    src.addr = dkgl_gpu(&pbo->mem) + (uintptr_t)pixels + src_offset;
    src.rowLength = src_pitch;
    src.imageHeight = src_slice;
    dkgl_buffer_used(pbo);
  } else {
    const uint8_t *base;
    if (pbo) {
      if (!pbo->mem.node)
        return;
      dkgl_buffer_wait_cpu(pbo);
      base = (const uint8_t *)dkgl_cpu(&pbo->mem) + (uintptr_t)pixels;
    } else {
      base = pixels;
    }
    base += src_offset;
    const uint32_t total = dst_pitch * rows * (uint32_t)d;
    DkglRing staging;
    if (!dkgl_ring_alloc(total, 64, &staging))
      return;
    uint8_t *out = staging.cpu;
    bool ok = true;
    for (int slice = 0; slice < d; slice++) {
      const uint8_t *s = base + (size_t)slice * src_slice;
      if (direct && src_pitch == dst_pitch) {
        memcpy(out, s, (size_t)dst_pitch * rows);
        out += (size_t)dst_pitch * rows;
        continue;
      }
      for (uint32_t row = 0; row < rows; row++) {
        if (direct)
          memcpy(out, s, row_bytes);
        else
          ok &= convert_row(f, format, type, s, out, (uint32_t)w);
        s += src_pitch;
        out += dst_pitch;
      }
    }
    if (!ok && debug_rate_limit("dkgl convert", "", 16))
      debugPrintf("dkgl: unhandled upload conversion internal=0x%x "
                  "format=0x%x type=0x%x\n",
                  f->internal, format, type);
    src.addr = staging.gpu;
    src.rowLength = dst_pitch;
    src.imageHeight = dst_pitch * rows;
  }

  /* partial updates of 1- and 2-byte textures (TextMesh Pro adding a glyph
   * to a dynamic font atlas), for checking what the game sends */
  if (!compressed && f->bytes < 4 && (x || y || (uint32_t)w != tex->width) &&
      debug_rate_limit("dkgl small sub-upload", "", 24))
    debugPrintf("dkgl: sub-upload tex %u (%ux%u fmt %x) level %d at %d,%d %dx%d from fmt %x type %x "
                "bpp %u row_length %d skip %d,%d align %d pbo %d direct %d\n",
                tex->name, tex->width, tex->height, f->internal, level, x, y, w, h, format, type,
                dkgl_client_bpp(format, type), ctx->unpack_row_length, ctx->unpack_skip_pixels,
                ctx->unpack_skip_rows, ctx->unpack_alignment, pbo != NULL, direct);

  DkImageView view;
  dkImageViewDefaults(&view, &tex->image);
  view.mipLevelOffset = (uint8_t)level;
  DkImageRect rect = {(uint32_t)x, (uint32_t)y, (uint32_t)z,
                      (uint32_t)w, (uint32_t)h, (uint32_t)d};
  dkgl_before_copy();
  dkCmdBufCopyBufferToImage(dkgl_cmd(), &src, &view, &rect, 0);
  dkg.stat_uploads++;
  dkg.stat_upload_bytes += (uint64_t)row_bytes * rows * (uint32_t)d;
  if (!(tex->level_defined & (1u << level))) {
    tex->level_defined |= 1u << level;
    tex->view_dirty = true;
    dkgl_texture_epoch++;
  }
}

/* --------------------------------------------------------- entry points */

void glGenTextures(GLsizei n, GLuint *textures) {
  DKGL_LOCK();
  for (GLsizei i = 0; i < n; i++) {
    textures[i] = dkgl_name_gen(DKGL_OBJ_TEXTURE);
    dkgl_texture(textures[i], true);
  }
  DKGL_UNLOCK();
}

void dkgl_fbo_detach_texture(DkglContext *ctx, GLuint name);

void glDeleteTextures(GLsizei n, const GLuint *textures) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  for (GLsizei i = 0; i < n; i++) {
    DkglTexture *tex = dkgl_texture(textures[i], false);
    if (!tex)
      continue;
    if (ctx) {
      for (int u = 0; u < DKGL_TEXTURE_UNITS; u++)
        for (int t = 0; t < DKGL_TEX_TARGETS; t++)
          if (ctx->textures[u][t] == textures[i])
            ctx->textures[u][t] = 0;
      for (int u = 0; u < DKGL_IMAGE_UNITS; u++)
        if (ctx->images[u].texture == textures[i])
          ctx->images[u].texture = 0;
      dkgl_fbo_detach_texture(ctx, textures[i]);
      ctx->dirty |= DKGL_DIRTY_TEXTURES | DKGL_DIRTY_IMAGES;
    }
    tex_release_storage(tex);
    dkgl_name_free(DKGL_OBJ_TEXTURE, textures[i]);
    free(tex);
  }
  dkgl_texture_epoch++;
  DKGL_UNLOCK();
}

GLboolean glIsTexture(GLuint texture) {
  DKGL_LOCK();
  DkglTexture *tex = dkgl_texture(texture, false);
  GLboolean result = tex && tex->target ? GL_TRUE : GL_FALSE;
  DKGL_UNLOCK();
  return result;
}

void glActiveTexture(GLenum texture) {
  DkglContext *ctx = dkgl_ctx();
  if (!ctx)
    return;
  if (texture < GL_TEXTURE0 || texture >= GL_TEXTURE0 + DKGL_TEXTURE_UNITS) {
    DKGL_ERROR(ctx, GL_INVALID_ENUM);
    return;
  }
  ctx->active_texture = texture - GL_TEXTURE0;
}

void glBindTexture(GLenum target, GLuint texture) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  int index = dkgl_target_index(target);
  if (!ctx || index < 0) {
    if (ctx)
      DKGL_ERROR(ctx, GL_INVALID_ENUM);
    DKGL_UNLOCK();
    return;
  }
  if (texture) {
    DkglTexture *tex = dkgl_texture(texture, true);
    if (tex && !tex->target)
      tex->target = target == GL_TEXTURE_EXTERNAL_OES ? GL_TEXTURE_2D : target;
  }
  if (ctx->textures[ctx->active_texture][index] != texture) {
    ctx->textures[ctx->active_texture][index] = texture;
    ctx->dirty |= DKGL_DIRTY_TEXTURES;
  }
  DKGL_UNLOCK();
}

static bool tex_param(DkglContext *ctx, DkglTexture *tex, GLenum pname,
                      const GLint *ip, const GLfloat *fp) {
  const GLint iv = ip ? ip[0] : (GLint)fp[0];
  const GLfloat fv = fp ? fp[0] : (GLfloat)ip[0];
  switch (pname) {
  case GL_TEXTURE_MIN_FILTER:
    tex->sampler.min_filter = (GLenum)iv;
    tex->sampler_dirty = true;
    break;
  case GL_TEXTURE_MAG_FILTER:
    tex->sampler.mag_filter = (GLenum)iv;
    tex->sampler_dirty = true;
    break;
  case GL_TEXTURE_WRAP_S:
    tex->sampler.wrap_s = (GLenum)iv;
    tex->sampler_dirty = true;
    break;
  case GL_TEXTURE_WRAP_T:
    tex->sampler.wrap_t = (GLenum)iv;
    tex->sampler_dirty = true;
    break;
  case GL_TEXTURE_WRAP_R:
    tex->sampler.wrap_r = (GLenum)iv;
    tex->sampler_dirty = true;
    break;
  case GL_TEXTURE_MIN_LOD:
    tex->sampler.min_lod = fv;
    tex->sampler_dirty = true;
    break;
  case GL_TEXTURE_MAX_LOD:
    tex->sampler.max_lod = fv;
    tex->sampler_dirty = true;
    break;
  case GL_TEXTURE_COMPARE_MODE:
    tex->sampler.compare_mode = (GLenum)iv;
    tex->sampler_dirty = true;
    break;
  case GL_TEXTURE_COMPARE_FUNC:
    tex->sampler.compare_func = (GLenum)iv;
    tex->sampler_dirty = true;
    break;
  case GL_TEXTURE_MAX_ANISOTROPY_EXT:
    tex->sampler.max_aniso = fv;
    tex->sampler_dirty = true;
    break;
  case GL_TEXTURE_BORDER_COLOR:
    for (int i = 0; i < 4; i++)
      tex->sampler.border[i] = fp ? fp[i] : (GLfloat)ip[i];
    tex->sampler_dirty = true;
    break;
  case GL_TEXTURE_BASE_LEVEL:
    tex->base_level = iv;
    tex->view_dirty = true;
    break;
  case GL_TEXTURE_MAX_LEVEL:
    tex->max_level = iv;
    tex->view_dirty = true;
    break;
  case GL_TEXTURE_SWIZZLE_R:
  case GL_TEXTURE_SWIZZLE_G:
  case GL_TEXTURE_SWIZZLE_B:
  case GL_TEXTURE_SWIZZLE_A:
    tex->swizzle[pname - GL_TEXTURE_SWIZZLE_R] = (GLenum)iv;
    tex->view_dirty = true;
    break;
  case GL_DEPTH_STENCIL_TEXTURE_MODE:
    tex->ds_mode = (GLenum)iv;
    tex->view_dirty = true;
    break;
  default:
    DKGL_ERROR(ctx, GL_INVALID_ENUM);
    return false;
  }
  ctx->dirty |= DKGL_DIRTY_TEXTURES;
  dkgl_texture_epoch++;
  return true;
}

static void tex_parameter(GLenum target, GLenum pname, const GLint *ip,
                          const GLfloat *fp) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglTexture *tex = ctx ? dkgl_bound_texture(ctx, target) : NULL;
  if (tex)
    tex_param(ctx, tex, pname, ip, fp);
  else if (ctx)
    DKGL_ERROR(ctx, GL_INVALID_OPERATION);
  DKGL_UNLOCK();
}

/* Scalar forms: GL_TEXTURE_BORDER_COLOR takes four values, so it is only
 * valid through the vector forms; the value is padded so nothing reads past
 * it. */
static bool scalar_pname_ok(GLenum pname) {
  if (pname != GL_TEXTURE_BORDER_COLOR)
    return true;
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  if (ctx)
    DKGL_ERROR(ctx, GL_INVALID_ENUM);
  DKGL_UNLOCK();
  return false;
}

void glTexParameteri(GLenum target, GLenum pname, GLint param) {
  const GLint v[4] = {param, 0, 0, 0};
  if (scalar_pname_ok(pname))
    tex_parameter(target, pname, v, NULL);
}
void glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
  const GLfloat v[4] = {param, 0.0f, 0.0f, 0.0f};
  if (scalar_pname_ok(pname))
    tex_parameter(target, pname, NULL, v);
}
void glTexParameteriv(GLenum target, GLenum pname, const GLint *params) {
  tex_parameter(target, pname, params, NULL);
}
void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params) {
  tex_parameter(target, pname, NULL, params);
}

static bool tex_get(DkglTexture *tex, GLenum pname, GLfloat *out) {
  switch (pname) {
  case GL_TEXTURE_MIN_FILTER:
    *out = (GLfloat)tex->sampler.min_filter;
    return true;
  case GL_TEXTURE_MAG_FILTER:
    *out = (GLfloat)tex->sampler.mag_filter;
    return true;
  case GL_TEXTURE_WRAP_S:
    *out = (GLfloat)tex->sampler.wrap_s;
    return true;
  case GL_TEXTURE_WRAP_T:
    *out = (GLfloat)tex->sampler.wrap_t;
    return true;
  case GL_TEXTURE_WRAP_R:
    *out = (GLfloat)tex->sampler.wrap_r;
    return true;
  case GL_TEXTURE_MIN_LOD:
    *out = tex->sampler.min_lod;
    return true;
  case GL_TEXTURE_MAX_LOD:
    *out = tex->sampler.max_lod;
    return true;
  case GL_TEXTURE_COMPARE_MODE:
    *out = (GLfloat)tex->sampler.compare_mode;
    return true;
  case GL_TEXTURE_COMPARE_FUNC:
    *out = (GLfloat)tex->sampler.compare_func;
    return true;
  case GL_TEXTURE_MAX_ANISOTROPY_EXT:
    *out = tex->sampler.max_aniso > 0.0f ? tex->sampler.max_aniso : 1.0f;
    return true;
  case GL_TEXTURE_BASE_LEVEL:
    *out = (GLfloat)tex->base_level;
    return true;
  case GL_TEXTURE_MAX_LEVEL:
    *out = (GLfloat)tex->max_level;
    return true;
  case GL_TEXTURE_IMMUTABLE_FORMAT:
    *out = tex->immutable ? 1.0f : 0.0f;
    return true;
  case GL_TEXTURE_IMMUTABLE_LEVELS:
    *out = tex->immutable ? (GLfloat)tex->levels : 0.0f;
    return true;
  case GL_TEXTURE_SWIZZLE_R:
  case GL_TEXTURE_SWIZZLE_G:
  case GL_TEXTURE_SWIZZLE_B:
  case GL_TEXTURE_SWIZZLE_A:
    *out = (GLfloat)tex->swizzle[pname - GL_TEXTURE_SWIZZLE_R];
    return true;
  case GL_DEPTH_STENCIL_TEXTURE_MODE:
    *out = (GLfloat)tex->ds_mode;
    return true;
  default:
    return false;
  }
}

void glGetTexParameteriv(GLenum target, GLenum pname, GLint *params) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglTexture *tex = ctx ? dkgl_bound_texture(ctx, target) : NULL;
  GLfloat v = 0;
  if (tex && tex_get(tex, pname, &v))
    params[0] = (GLint)v;
  else if (ctx)
    DKGL_ERROR(ctx, GL_INVALID_ENUM);
  DKGL_UNLOCK();
}

void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglTexture *tex = ctx ? dkgl_bound_texture(ctx, target) : NULL;
  if (!tex || !tex_get(tex, pname, params)) {
    if (ctx)
      DKGL_ERROR(ctx, GL_INVALID_ENUM);
  }
  DKGL_UNLOCK();
}

static int face_layer(GLenum target) {
  if (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
      target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)
    return (int)(target - GL_TEXTURE_CUBE_MAP_POSITIVE_X);
  return 0;
}

/* glTexImage*D: (re)specifies one level of a mutable texture. */
static void tex_image(GLenum target, GLint level, GLenum internalformat,
                      GLsizei w, GLsizei h, GLsizei d, GLenum format,
                      GLenum type, const void *pixels, bool compressed,
                      GLsizei image_size) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglTexture *tex = ctx ? dkgl_bound_texture(ctx, target) : NULL;
  if (!tex || level < 0 || level > 15 || w < 0 || h < 0 || d < 0) {
    if (ctx)
      DKGL_ERROR(ctx, tex ? GL_INVALID_VALUE : GL_INVALID_OPERATION);
    DKGL_UNLOCK();
    return;
  }
  if (tex->immutable) {
    DKGL_ERROR(ctx, GL_INVALID_OPERATION);
    DKGL_UNLOCK();
    return;
  }
  const DkglFormat *fmt = dkgl_format(internalformat);
  if (!fmt && !compressed)
    fmt = dkgl_format_unsized(internalformat, type);
  if (!fmt && !compressed)
    fmt = dkgl_format_unsized(format, type);
  if (!fmt) {
    debugPrintf("dkgl: unsupported texture format 0x%x/0x%x/0x%x\n",
                internalformat, format, type);
    DKGL_ERROR(ctx, GL_INVALID_ENUM);
    DKGL_UNLOCK();
    return;
  }
  const GLenum base_target =
      dkgl_target_index(target) == DKGL_TEX_CUBE ? GL_TEXTURE_CUBE_MAP : target;
  const int index = dkgl_target_index(target);
  uint32_t bw = (uint32_t)(w ? w : 1) << level;
  uint32_t bh = (uint32_t)(h ? h : 1) << level;
  uint32_t bd = index == DKGL_TEX_3D ? (uint32_t)(d ? d : 1) << level
                                     : (uint32_t)(d ? d : 1);
  if (index == DKGL_TEX_CUBE)
    bd = 6;
  const bool same_base = level_fits(tex, fmt, index, level, (uint32_t)w,
                                    (uint32_t)h, (uint32_t)d);
  if (!same_base) {
    /* The whole mip chain at once, as GL drivers allocate it: an engine
     * that uploads level by level (Unity's ES 2.0 renderer) then never
     * grows the image. Growing copies the defined levels with the copy
     * engine, which takes a compressed image's rectangle in texels as if
     * they were blocks: every mipmapped ETC/ASTC texture came out as noise.
     * Only the defined levels are sampled (tex_view_slot). */
    uint32_t levels = full_levels(bw, bh, index == DKGL_TEX_3D ? bd : 1);
    if ((uint32_t)level + 1 > levels)
      levels = (uint32_t)level + 1;
    if (!tex_allocate(tex, base_target, fmt, bw, bh, bd, levels, 0)) {
      DKGL_ERROR(ctx, GL_OUT_OF_MEMORY);
      DKGL_UNLOCK();
      return;
    }
  } else if ((uint32_t)level >= tex->levels) {
    bw = tex->width;
    bh = tex->height;
    bd = tex->depth;
    uint32_t levels = full_levels(bw, bh, index == DKGL_TEX_3D ? bd : 1);
    if ((uint32_t)level + 1 > levels)
      levels = (uint32_t)level + 1;
    tex_allocate(tex, base_target, fmt, bw, bh, bd, levels, tex->levels);
  }
  if (!(tex->level_defined & (1u << level))) {
    tex->level_defined |= 1u << level;
    tex->view_dirty = true; /* the sampled mip range grows */
    dkgl_texture_epoch++;
  }
  if (pixels || ctx->pixel_unpack)
    dkgl_upload_pixels(ctx, tex, level, 0, 0, face_layer(target), w, h,
                       index == DKGL_TEX_CUBE ? 1 : d, format, type, pixels,
                       compressed, image_size);
  ctx->dirty |= DKGL_DIRTY_TEXTURES;
  DKGL_UNLOCK();
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat,
                  GLsizei width, GLsizei height, GLint border, GLenum format,
                  GLenum type, const void *pixels) {
  (void)border;
  tex_image(target, level, (GLenum)internalformat, width, height, 1, format,
            type, pixels, false, 0);
}

void glTexImage3D(GLenum target, GLint level, GLint internalformat,
                  GLsizei width, GLsizei height, GLsizei depth, GLint border,
                  GLenum format, GLenum type, const void *pixels) {
  (void)border;
  tex_image(target, level, (GLenum)internalformat, width, height, depth,
            format, type, pixels, false, 0);
}

void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat,
                            GLsizei width, GLsizei height, GLint border,
                            GLsizei imageSize, const void *data) {
  (void)border;
  tex_image(target, level, internalformat, width, height, 1, 0, 0, data, true,
            imageSize);
}

void glCompressedTexImage3D(GLenum target, GLint level, GLenum internalformat,
                            GLsizei width, GLsizei height, GLsizei depth,
                            GLint border, GLsizei imageSize, const void *data) {
  (void)border;
  tex_image(target, level, internalformat, width, height, depth, 0, 0, data,
            true, imageSize);
}

static void tex_sub_image(GLenum target, GLint level, GLint x, GLint y,
                          GLint z, GLsizei w, GLsizei h, GLsizei d,
                          GLenum format, GLenum type, const void *pixels,
                          bool compressed, GLsizei image_size) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglTexture *tex = ctx ? dkgl_bound_texture(ctx, target) : NULL;
  if (!tex || !tex->has_storage || level < 0 || (uint32_t)level >= tex->levels) {
    if (ctx)
      DKGL_ERROR(ctx, GL_INVALID_OPERATION);
    DKGL_UNLOCK();
    return;
  }
  const int index = dkgl_target_index(target);
  dkgl_upload_pixels(ctx, tex, level, x, y,
                     index == DKGL_TEX_CUBE ? face_layer(target) : z, w, h, d,
                     format, type, pixels, compressed, image_size);
  DKGL_UNLOCK();
}

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                     GLsizei width, GLsizei height, GLenum format, GLenum type,
                     const void *pixels) {
  tex_sub_image(target, level, xoffset, yoffset, 0, width, height, 1, format,
                type, pixels, false, 0);
}

void glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                     GLint zoffset, GLsizei width, GLsizei height,
                     GLsizei depth, GLenum format, GLenum type,
                     const void *pixels) {
  tex_sub_image(target, level, xoffset, yoffset, zoffset, width, height, depth,
                format, type, pixels, false, 0);
}

void glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                               GLint yoffset, GLsizei width, GLsizei height,
                               GLenum format, GLsizei imageSize,
                               const void *data) {
  tex_sub_image(target, level, xoffset, yoffset, 0, width, height, 1, format,
                0, data, true, imageSize);
}

void glCompressedTexSubImage3D(GLenum target, GLint level, GLint xoffset,
                               GLint yoffset, GLint zoffset, GLsizei width,
                               GLsizei height, GLsizei depth, GLenum format,
                               GLsizei imageSize, const void *data) {
  tex_sub_image(target, level, xoffset, yoffset, zoffset, width, height, depth,
                format, 0, data, true, imageSize);
}

static void tex_storage(GLenum target, GLsizei levels, GLenum internalformat,
                        GLsizei w, GLsizei h, GLsizei d) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglTexture *tex = ctx ? dkgl_bound_texture(ctx, target) : NULL;
  const DkglFormat *fmt = dkgl_format(internalformat);
  if (!tex || tex->immutable || !fmt || levels < 1 || w < 1 || h < 1 || d < 1) {
    if (!fmt)
      debugPrintf("dkgl: unsupported storage format 0x%x\n", internalformat);
    if (ctx)
      DKGL_ERROR(ctx, !fmt ? GL_INVALID_ENUM : GL_INVALID_OPERATION);
    DKGL_UNLOCK();
    return;
  }
  if (tex_allocate(tex, target, fmt, (uint32_t)w, (uint32_t)h, (uint32_t)d,
                   (uint32_t)levels, 0)) {
    tex->immutable = true;
    tex->level_defined = (1u << levels) - 1;
  } else {
    DKGL_ERROR(ctx, GL_OUT_OF_MEMORY);
  }
  ctx->dirty |= DKGL_DIRTY_TEXTURES;
  DKGL_UNLOCK();
}

void glTexStorage2D(GLenum target, GLsizei levels, GLenum internalformat,
                    GLsizei width, GLsizei height) {
  tex_storage(target, levels, internalformat, width, height,
              target == GL_TEXTURE_CUBE_MAP ? 6 : 1);
}

void glTexStorage3D(GLenum target, GLsizei levels, GLenum internalformat,
                    GLsizei width, GLsizei height, GLsizei depth) {
  tex_storage(target, levels, internalformat, width, height, depth);
}

void glTexStorage2DMultisample(GLenum target, GLsizei samples,
                               GLenum internalformat, GLsizei width,
                               GLsizei height, GLboolean fixedsamplelocations) {
  (void)samples;
  (void)fixedsamplelocations;
  dkgl_log_once("glTexStorage2DMultisample: single-sampled storage");
  tex_storage(GL_TEXTURE_2D, 1, internalformat, width, height, 1);
  (void)target;
}

void glGenerateMipmap(GLenum target) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglTexture *tex = ctx ? dkgl_bound_texture(ctx, target) : NULL;
  if (!tex || !tex->has_storage || (tex->fmt->flags & DKGL_FMT_COMPRESSED)) {
    if (ctx)
      DKGL_ERROR(ctx, GL_INVALID_OPERATION);
    DKGL_UNLOCK();
    return;
  }
  const int index = dkgl_target_index(target);
  if (!tex->immutable) {
    uint32_t want = full_levels(tex->width, tex->height,
                                index == DKGL_TEX_3D ? tex->depth : 1);
    if (tex->levels < want)
      tex_allocate(tex, tex->target, tex->fmt, tex->width, tex->height,
                   tex->depth, want, tex->levels);
  }
  if (!(tex->fmt->flags & DKGL_FMT_2D_ENGINE) || index == DKGL_TEX_3D) {
    dkgl_log_once("glGenerateMipmap: format/target not blittable");
    DKGL_UNLOCK();
    return;
  }
  dkgl_texture_prepare_transfer(tex);
  /* Rendering into level 0 must finish before it is read. */
  dkgl_rt_barrier();
  dkgl_before_copy();
  DkCmdBuf cmd = dkgl_cmd();
  const uint32_t layers = tex->depth;
  for (uint32_t level = 1; level < tex->levels; level++) {
    DkImageView src, dst;
    dkImageViewDefaults(&src, &tex->image);
    dkImageViewDefaults(&dst, &tex->image);
    src.mipLevelOffset = (uint8_t)(level - 1);
    dst.mipLevelOffset = (uint8_t)level;
    uint32_t sw = tex->width >> (level - 1), sh = tex->height >> (level - 1);
    uint32_t dw = tex->width >> level, dh = tex->height >> level;
    DkImageRect sr = {0, 0, 0, sw ? sw : 1, sh ? sh : 1, layers};
    DkImageRect dr = {0, 0, 0, dw ? dw : 1, dh ? dh : 1, layers};
    dkCmdBufBlitImage(cmd, &src, &sr, &dst, &dr, DkBlitFlag_FilterLinear, 0);
    /* each level reads the previous one */
    dkCmdBufBarrier(cmd, DkBarrier_Full, 0);
  }
  tex->level_defined = (1u << tex->levels) - 1;
  dkgl_texture_epoch++;
  DKGL_UNLOCK();
}

void glTexBufferRange(GLenum target, GLenum internalformat, GLuint buffer,
                      GLintptr offset, GLsizeiptr size) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglTexture *tex = ctx ? dkgl_bound_texture(ctx, target) : NULL;
  const DkglFormat *fmt = dkgl_format(internalformat);
  if (!tex || !fmt) {
    if (ctx)
      DKGL_ERROR(ctx, GL_INVALID_OPERATION);
    DKGL_UNLOCK();
    return;
  }
  tex->fmt = fmt;
  tex->internal_format = internalformat;
  tex->tbo_buffer = buffer;
  tex->tbo_offset = (uint32_t)offset;
  tex->tbo_size = (uint32_t)size;
  tex->tbo_version = 0xffffffffu;
  tex->view_dirty = true;
  tex->generation++;
  ctx->dirty |= DKGL_DIRTY_TEXTURES | DKGL_DIRTY_IMAGES;
  dkgl_texture_epoch++;
  DKGL_UNLOCK();
}

void glTexBuffer(GLenum target, GLenum internalformat, GLuint buffer) {
  glTexBufferRange(target, internalformat, buffer, 0, 0);
}

void glTexBufferEXT(GLenum target, GLenum internalformat, GLuint buffer) {
  glTexBufferRange(target, internalformat, buffer, 0, 0);
}

void glTexBufferRangeEXT(GLenum target, GLenum internalformat, GLuint buffer,
                         GLintptr offset, GLsizeiptr size) {
  glTexBufferRange(target, internalformat, buffer, offset, size);
}

void glBindImageTexture(GLuint unit, GLuint texture, GLint level,
                        GLboolean layered, GLint layer, GLenum access,
                        GLenum format) {
  DkglContext *ctx = dkgl_ctx();
  if (!ctx)
    return;
  if (unit >= DKGL_IMAGE_UNITS) {
    DKGL_ERROR(ctx, GL_INVALID_VALUE);
    return;
  }
  ctx->images[unit].texture = texture;
  ctx->images[unit].level = level;
  ctx->images[unit].layered = layered;
  ctx->images[unit].layer = layer;
  ctx->images[unit].access = access;
  ctx->images[unit].format = format;
  ctx->dirty |= DKGL_DIRTY_IMAGES;
}

/* glCopyImageSubData between textures and/or renderbuffers. */
static bool copy_image_view(GLuint name, GLenum target, GLint level, GLint z,
                            DkImageView *view, GLint *z_out) {
  *z_out = 0;
  if (target == GL_RENDERBUFFER) {
    DkglRenderbuffer *rb = dkgl_renderbuffer(name, false);
    if (!rb || !rb->has_storage)
      return false;
    dkImageViewDefaults(view, &rb->image);
    return true;
  }
  DkglTexture *tex = dkgl_texture(name, false);
  if (!tex || !tex->has_storage || level < 0 || (uint32_t)level >= tex->levels)
    return false;
  dkgl_texture_prepare_transfer(tex);
  dkImageViewDefaults(view, &tex->image);
  view->mipLevelOffset = (uint8_t)level;
  *z_out = z;
  return true;
}

void glCopyImageSubData(GLuint srcName, GLenum srcTarget, GLint srcLevel,
                        GLint srcX, GLint srcY, GLint srcZ, GLuint dstName,
                        GLenum dstTarget, GLint dstLevel, GLint dstX,
                        GLint dstY, GLint dstZ, GLsizei srcWidth,
                        GLsizei srcHeight, GLsizei srcDepth) {
  DKGL_LOCK();
  DkImageView src, dst;
  GLint sz, dz;
  if (copy_image_view(srcName, srcTarget, srcLevel, srcZ, &src, &sz) &&
      copy_image_view(dstName, dstTarget, dstLevel, dstZ, &dst, &dz) &&
      srcWidth > 0 && srcHeight > 0 && srcDepth > 0) {
    DkImageRect sr = {(uint32_t)srcX, (uint32_t)srcY, (uint32_t)sz,
                      (uint32_t)srcWidth, (uint32_t)srcHeight, (uint32_t)srcDepth};
    DkImageRect dr = {(uint32_t)dstX, (uint32_t)dstY, (uint32_t)dz,
                      (uint32_t)srcWidth, (uint32_t)srcHeight, (uint32_t)srcDepth};
    if (dkgl_trace_on)
      debugPrintf("trace copyimage %u(%d,%d) -> %u(%d,%d) %dx%d\n", srcName,
                  srcX, srcY, dstName, dstX, dstY, srcWidth, srcHeight);
    dkgl_rt_barrier();
    dkgl_before_copy();
    dkCmdBufCopyImage(dkgl_cmd(), &src, &sr, &dst, &dr, 0);
  } else if (dkgl_ctx()) {
    DKGL_ERROR(dkgl_ctx(), GL_INVALID_VALUE);
  }
  DKGL_UNLOCK();
}

void glCopyImageSubDataEXT(GLuint srcName, GLenum srcTarget, GLint srcLevel,
                           GLint srcX, GLint srcY, GLint srcZ, GLuint dstName,
                           GLenum dstTarget, GLint dstLevel, GLint dstX,
                           GLint dstY, GLint dstZ, GLsizei srcWidth,
                           GLsizei srcHeight, GLsizei srcDepth) {
  glCopyImageSubData(srcName, srcTarget, srcLevel, srcX, srcY, srcZ, dstName,
                     dstTarget, dstLevel, dstX, dstY, dstZ, srcWidth, srcHeight,
                     srcDepth);
}

/* ------------------------------------------------------- sampler objects */

void glGenSamplers(GLsizei count, GLuint *samplers) {
  DKGL_LOCK();
  for (GLsizei i = 0; i < count; i++) {
    GLuint name = dkgl_name_gen(DKGL_OBJ_SAMPLER);
    DkglSampler *s = calloc(1, sizeof(*s));
    if (s) {
      s->name = name;
      sampler_defaults(&s->state);
      s->dirty = true;
      dkgl_name_set(DKGL_OBJ_SAMPLER, name, s);
    }
    samplers[i] = name;
  }
  DKGL_UNLOCK();
}

void glDeleteSamplers(GLsizei count, const GLuint *samplers) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  for (GLsizei i = 0; i < count; i++) {
    DkglSampler *s = sampler_object(samplers[i]);
    if (!s)
      continue;
    if (ctx)
      for (int u = 0; u < DKGL_TEXTURE_UNITS; u++)
        if (ctx->samplers[u] == samplers[i])
          ctx->samplers[u] = 0;
    dkgl_name_free(DKGL_OBJ_SAMPLER, samplers[i]);
    free(s);
  }
  if (ctx)
    ctx->dirty |= DKGL_DIRTY_TEXTURES;
  dkgl_texture_epoch++;
  DKGL_UNLOCK();
}

GLboolean glIsSampler(GLuint sampler) {
  DKGL_LOCK();
  GLboolean r = sampler_object(sampler) ? GL_TRUE : GL_FALSE;
  DKGL_UNLOCK();
  return r;
}

void glBindSampler(GLuint unit, GLuint sampler) {
  DkglContext *ctx = dkgl_ctx();
  if (!ctx)
    return;
  if (unit >= DKGL_TEXTURE_UNITS) {
    DKGL_ERROR(ctx, GL_INVALID_VALUE);
    return;
  }
  if (ctx->samplers[unit] != sampler) {
    ctx->samplers[unit] = sampler;
    ctx->dirty |= DKGL_DIRTY_TEXTURES;
  }
}

static void sampler_parameter(GLuint sampler, GLenum pname, const GLint *ip,
                              const GLfloat *fp) {
  DKGL_LOCK();
  DkglSampler *s = sampler_object(sampler);
  DkglContext *ctx = dkgl_ctx();
  if (!s) {
    if (ctx)
      DKGL_ERROR(ctx, GL_INVALID_OPERATION);
    DKGL_UNLOCK();
    return;
  }
  const GLint iv = ip ? ip[0] : (GLint)fp[0];
  const GLfloat fv = fp ? fp[0] : (GLfloat)ip[0];
  switch (pname) {
  case GL_TEXTURE_MIN_FILTER:
    s->state.min_filter = (GLenum)iv;
    break;
  case GL_TEXTURE_MAG_FILTER:
    s->state.mag_filter = (GLenum)iv;
    break;
  case GL_TEXTURE_WRAP_S:
    s->state.wrap_s = (GLenum)iv;
    break;
  case GL_TEXTURE_WRAP_T:
    s->state.wrap_t = (GLenum)iv;
    break;
  case GL_TEXTURE_WRAP_R:
    s->state.wrap_r = (GLenum)iv;
    break;
  case GL_TEXTURE_MIN_LOD:
    s->state.min_lod = fv;
    break;
  case GL_TEXTURE_MAX_LOD:
    s->state.max_lod = fv;
    break;
  case GL_TEXTURE_COMPARE_MODE:
    s->state.compare_mode = (GLenum)iv;
    break;
  case GL_TEXTURE_COMPARE_FUNC:
    s->state.compare_func = (GLenum)iv;
    break;
  case GL_TEXTURE_MAX_ANISOTROPY_EXT:
    s->state.max_aniso = fv;
    break;
  case GL_TEXTURE_BORDER_COLOR:
    for (int i = 0; i < 4; i++)
      s->state.border[i] = fp ? fp[i] : (GLfloat)ip[i];
    break;
  default:
    if (ctx)
      DKGL_ERROR(ctx, GL_INVALID_ENUM);
    DKGL_UNLOCK();
    return;
  }
  s->dirty = true;
  if (ctx)
    ctx->dirty |= DKGL_DIRTY_TEXTURES;
  dkgl_texture_epoch++;
  DKGL_UNLOCK();
}

void glSamplerParameteri(GLuint sampler, GLenum pname, GLint param) {
  const GLint v[4] = {param, 0, 0, 0};
  if (scalar_pname_ok(pname))
    sampler_parameter(sampler, pname, v, NULL);
}
void glSamplerParameterf(GLuint sampler, GLenum pname, GLfloat param) {
  const GLfloat v[4] = {param, 0.0f, 0.0f, 0.0f};
  if (scalar_pname_ok(pname))
    sampler_parameter(sampler, pname, NULL, v);
}
void glSamplerParameteriv(GLuint sampler, GLenum pname, const GLint *param) {
  sampler_parameter(sampler, pname, param, NULL);
}
void glSamplerParameterfv(GLuint sampler, GLenum pname, const GLfloat *param) {
  sampler_parameter(sampler, pname, NULL, param);
}

static bool sampler_get(const DkglSamplerState *s, GLenum pname, GLfloat *out) {
  switch (pname) {
  case GL_TEXTURE_MIN_FILTER:
    *out = (GLfloat)s->min_filter;
    return true;
  case GL_TEXTURE_MAG_FILTER:
    *out = (GLfloat)s->mag_filter;
    return true;
  case GL_TEXTURE_WRAP_S:
    *out = (GLfloat)s->wrap_s;
    return true;
  case GL_TEXTURE_WRAP_T:
    *out = (GLfloat)s->wrap_t;
    return true;
  case GL_TEXTURE_WRAP_R:
    *out = (GLfloat)s->wrap_r;
    return true;
  case GL_TEXTURE_MIN_LOD:
    *out = s->min_lod;
    return true;
  case GL_TEXTURE_MAX_LOD:
    *out = s->max_lod;
    return true;
  case GL_TEXTURE_COMPARE_MODE:
    *out = (GLfloat)s->compare_mode;
    return true;
  case GL_TEXTURE_COMPARE_FUNC:
    *out = (GLfloat)s->compare_func;
    return true;
  case GL_TEXTURE_MAX_ANISOTROPY_EXT:
    *out = s->max_aniso > 0.0f ? s->max_aniso : 1.0f;
    return true;
  default:
    return false;
  }
}

void glGetSamplerParameteriv(GLuint sampler, GLenum pname, GLint *params) {
  DKGL_LOCK();
  DkglSampler *s = sampler_object(sampler);
  GLfloat v = 0;
  if (s && sampler_get(&s->state, pname, &v))
    params[0] = (GLint)v;
  else if (dkgl_ctx())
    DKGL_ERROR(dkgl_ctx(), GL_INVALID_OPERATION);
  DKGL_UNLOCK();
}

void glGetSamplerParameterfv(GLuint sampler, GLenum pname, GLfloat *params) {
  DKGL_LOCK();
  DkglSampler *s = sampler_object(sampler);
  if (!s || !sampler_get(&s->state, pname, params)) {
    if (dkgl_ctx())
      DKGL_ERROR(dkgl_ctx(), GL_INVALID_OPERATION);
  }
  DKGL_UNLOCK();
}

/* Used by glCopyTexImage2D (dkgl_fbo.c) to (re)create level storage. */
bool dkgl_texture_define_level(DkglTexture *tex, GLenum target, GLint level,
                               const DkglFormat *fmt, uint32_t w, uint32_t h);
bool dkgl_texture_define_level(DkglTexture *tex, GLenum target, GLint level,
                               const DkglFormat *fmt, uint32_t w, uint32_t h) {
  const int index = dkgl_target_index(target);
  const GLenum base_target = index == DKGL_TEX_CUBE ? GL_TEXTURE_CUBE_MAP : target;
  uint32_t bw = w << level, bh = h << level;
  uint32_t bd = index == DKGL_TEX_CUBE ? 6 : 1;
  if (!level_fits(tex, fmt, index, level, w, h, 1)) {
    uint32_t levels = full_levels(bw, bh, 1); /* the whole chain: see tex_image */
    if ((uint32_t)level + 1 > levels)
      levels = (uint32_t)level + 1;
    if (!tex_allocate(tex, base_target, fmt, bw, bh, bd, levels, 0))
      return false;
  } else if ((uint32_t)level >= tex->levels) {
    bw = tex->width;
    bh = tex->height;
    if (!tex_allocate(tex, base_target, fmt, bw, bh, bd,
                      full_levels(bw, bh, 1), tex->levels))
      return false;
  }
  if (!(tex->level_defined & (1u << level))) {
    tex->level_defined |= 1u << level;
    tex->view_dirty = true;
    dkgl_texture_epoch++;
  }
  if (!tex->target)
    tex->target = base_target;
  return true;
}
