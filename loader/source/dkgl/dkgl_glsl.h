/* dkgl_glsl.h -- GLSL ES rewriter for uam (deko3d's compiler)
 *
 * uam needs explicit bindings for every UBO and sampler and rejects uniforms
 * outside blocks. UE 4.27's GLES shaders declare their packed uniform arrays
 * as loose uniforms and leave sampler bindings to glUniform1i, so each stage
 * is rewritten before compiling and the rewrite reports where everything went.
 * Pure C with no deko3d/libnx dependency, so the host tests run it too.
 */
#ifndef DKGL_GLSL_H
#define DKGL_GLSL_H

#include <stddef.h>
#include <stdint.h>

#define DKGL_GLSL_NAME 64
#define DKGL_GLSL_MAX_UNIFORMS 64
#define DKGL_GLSL_MAX_BLOCKS 16
#define DKGL_GLSL_MAX_MEMBERS 64 /* uniform block members reflected (mvl_nx) */
#define DKGL_GLSL_MAX_SAMPLERS 32
#define DKGL_GLSL_MAX_IMAGES 8
#define DKGL_GLSL_MAX_INPUTS 32

enum { DKGL_GLSL_VERTEX = 0, DKGL_GLSL_FRAGMENT = 1, DKGL_GLSL_COMPUTE = 2 };
enum { DKGL_BASE_FLOAT = 0, DKGL_BASE_INT, DKGL_BASE_UINT, DKGL_BASE_BOOL };

/* Texture binding points a sampler or image reads from. */
enum {
  DKGL_TEX_2D = 0,
  DKGL_TEX_3D,
  DKGL_TEX_CUBE,
  DKGL_TEX_2D_ARRAY,
  DKGL_TEX_BUFFER,
  DKGL_TEX_CUBE_ARRAY,
  DKGL_TEX_2D_MS,
  DKGL_TEX_2D_MS_ARRAY,
  DKGL_TEX_TARGETS
};

typedef struct {
  char name[DKGL_GLSL_NAME];
  uint32_t gl_type;    /* GL_FLOAT_VEC4, GL_INT_VEC4, GL_FLOAT_MAT4, ... */
  uint8_t base;        /* DKGL_BASE_* */
  uint8_t columns;     /* 1 unless a matrix */
  uint8_t rows;        /* components per column */
  uint8_t binding;     /* UBO binding inside the stage */
  uint32_t offset;     /* byte offset in that block (std140) */
  uint32_t array_size; /* 1 when not an array */
  uint32_t stride;     /* std140 bytes per element (0 when not an array) */
  uint32_t size;       /* std140 bytes the declaration occupies */
} DkglGlslUniform;

typedef struct {
  char name[DKGL_GLSL_NAME];
  uint32_t gl_type;    /* GL_SAMPLER_2D, GL_IMAGE_2D, ... */
  uint8_t binding;     /* first deko3d texture/image binding */
  uint8_t target;      /* DKGL_TEX_* */
  uint8_t shadow;
  uint8_t base;        /* sampled/stored component type */
  int16_t gl_location; /* layout(location = n) as written, -1 none (mvl_nx:
                          Unity's UNITY_LOCATION on ES 3.1+) */
  uint32_t array_size;
} DkglGlslSampler;

typedef struct {
  char name[DKGL_GLSL_NAME];
  uint8_t binding;     /* deko3d uniform buffer binding */
  int8_t gl_binding;   /* layout(binding = n) as the shader wrote it, -1 none:
                          the block's initial GL binding point (mvl_nx) */
  uint32_t size;       /* 0 when unknown (explicit blocks are not laid out) */
} DkglGlslBlock;

/* A member of an explicit uniform block, std140 (mvl_nx: Unity lays out its
 * per-object UnityPerDraw / UnityPerMaterial buffers from these, through
 * glGetActiveUniform + glGetActiveUniformsiv, as GL lists block members among
 * the active uniforms). hlslcc's Xhlslcc_Unused* padding is left out. */
typedef struct {
  char name[DKGL_GLSL_NAME];
  uint8_t block;        /* index into blocks[] */
  uint32_t gl_type;
  uint32_t array_size;  /* 1 when not an array */
  uint32_t offset;
  uint32_t array_stride; /* 0 when not an array */
  uint32_t matrix_stride; /* 0 when not a matrix */
} DkglGlslMember;

typedef struct {
  char name[DKGL_GLSL_NAME];
  int location;
  uint32_t gl_type;
  uint8_t base;
  uint8_t slots;       /* locations used (matrix columns x array) */
} DkglGlslInput;

typedef struct {
  char *source;        /* rewritten GLSL, malloc'd */
  size_t source_len;
  int num_uniforms;
  DkglGlslUniform uniforms[DKGL_GLSL_MAX_UNIFORMS];
  int num_ubos;                          /* bindings 0..num_ubos-1 used */
  uint32_t ubo_size[DKGL_GLSL_MAX_BLOCKS]; /* bytes, 16-aligned */
  int num_blocks;                        /* explicit uniform blocks */
  DkglGlslBlock blocks[DKGL_GLSL_MAX_BLOCKS];
  int num_members;                       /* their members */
  DkglGlslMember members[DKGL_GLSL_MAX_MEMBERS];
  /* varyings: the vertex stage's outputs / the fragment stage's inputs and
   * the locations they got (mvl_nx: GLSL ES 3 links them by name; the
   * program links a fragment variant whose inputs follow the vertex
   * stage's when they differ) */
  int num_varyings;
  struct {
    char name[DKGL_GLSL_NAME];
    int16_t location;
  } varyings[32];
  int num_samplers;
  DkglGlslSampler samplers[DKGL_GLSL_MAX_SAMPLERS];
  int num_texture_bindings;              /* deko3d texture bindings used */
  int num_images;
  DkglGlslSampler images[DKGL_GLSL_MAX_IMAGES];
  int num_inputs;                        /* vertex stage attributes */
  DkglGlslInput inputs[DKGL_GLSL_MAX_INPUTS];
  int local_size[3];                     /* compute */
  char error[256];
} DkglGlslResult;

/* Returns 1 on success (result->source owned by the caller, free with
 * dkgl_glsl_free), 0 with result->error set otherwise. */
int dkgl_glsl_rewrite(const char *source, int stage, DkglGlslResult *result);
void dkgl_glsl_free(DkglGlslResult *result);

/* How many UBO bindings a stage's loose uniforms may spread over (the rest
 * share the last). 1 packs them all into one block: Unity's GLSL ES 1.00
 * shaders have 10-30 loose uniforms a stage, one buffer bind beats sixteen. */
extern int dkgl_glsl_loose_blocks;

/* ------------------------------------------------ GLSL ES 1.00 shaders
 *
 * Unity's ES 2.0 renderer hands over GLSL ES 1.00 (attribute/varying,
 * gl_FragColor/gl_FragData, texture2D...). uam compiles ES 3.x, and an ES
 * 1.00 interface needs what only the link knows: attribute locations come
 * from glBindAttribLocation, varying locations must agree between the stages
 * (the vertex shader's order; the fragment shader looks its own up by name).
 * So such a stage is translated to ES 3.00 at link time, then rewritten as
 * any other. */
#define DKGL_GLSL_MAX_VARYINGS 32

typedef struct {
  /* vertex: glBindAttribLocation's bindings */
  int num_attribs;
  char attrib_name[16][DKGL_GLSL_NAME];
  int attrib_index[16];
  /* fragment (in) / vertex (out): varyings and their locations */
  int num_varyings;
  char varying_name[DKGL_GLSL_MAX_VARYINGS][DKGL_GLSL_NAME];
  int varying_location[DKGL_GLSL_MAX_VARYINGS];
} DkglGlslLink;

/* dkgl_glsl_rewrite for a fragment stage whose unlocated inputs take the
 * locations vary gives their names (the vertex stage's outputs); inputs it
 * does not name get the highest free locations (mvl_nx). */
int dkgl_glsl_rewrite_linked(const char *source, int stage, const DkglGlslLink *vary, DkglGlslResult *result);

/* 1 when source is GLSL ES 1.00 (#version 100, or no #version). */
int dkgl_glsl_is_es1(const char *source);
/* source in ES 3.00 (malloc'd), or NULL with err set. A vertex stage reads
 * link's attributes and writes its varyings into varyings_out; a fragment
 * stage reads link's varyings. */
char *dkgl_glsl_es1(const char *source, int stage, const DkglGlslLink *link,
                    DkglGlslLink *varyings_out, char *err, size_t err_size);

#endif
