/* dkgl.h -- GLES 3.2 / EGL 1.4 subset on deko3d (internal header)
 *
 * The public surface is the standard GL/EGL entry points (same prototypes as
 * the Khronos headers), so imports.c can point UE's imports straight at them.
 * Everything here is internal. See DEKO3D_BACKEND.md for the design.
 */
#ifndef DKGL_H
#define DKGL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <switch.h>
#include <deko3d.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>

#include "dkgl_glsl.h"

/* ------------------------------------------------------------ limits */

#define DKGL_TEXTURE_UNITS 48 /* = GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS */
#define DKGL_IMAGE_UNITS 8
#define DKGL_UBO_BINDINGS 36
#define DKGL_ATTRIBS 16
#define DKGL_VBO_BINDINGS 16
#define DKGL_DRAW_BUFFERS 8
#define DKGL_IMAGE_DESCRIPTORS 65536
#define DKGL_SAMPLER_DESCRIPTORS 4096
#define DKGL_STAGES 2 /* vertex/compute slot 0, fragment slot 1 */

/* ------------------------------------------------------------ memory */

enum { DKGL_HEAP_DATA, DKGL_HEAP_IMAGE, DKGL_HEAP_CODE, DKGL_HEAP_COUNT };

typedef struct {
  DkMemBlock block;
  uint32_t offset;
  uint32_t size;
  void *node; /* allocator bookkeeping */
  uint8_t heap;
} DkglAlloc;

static inline DkGpuAddr dkgl_gpu(const DkglAlloc *a) {
  return dkMemBlockGetGpuAddr(a->block) + a->offset;
}
static inline void *dkgl_cpu(const DkglAlloc *a) {
  return (uint8_t *)dkMemBlockGetCpuAddr(a->block) + a->offset;
}

bool dkgl_alloc(int heap, uint32_t size, uint32_t align, DkglAlloc *out);
/* Frees once the GPU has finished everything recorded so far. */
void dkgl_free_deferred(DkglAlloc *a);

typedef struct {
  DkGpuAddr gpu;
  void *cpu;
} DkglRing;

/* Per-submission scratch (uniform snapshots, staged uploads, client
 * arrays). Valid until the GPU finishes the current submission. */
bool dkgl_ring_alloc(uint32_t size, uint32_t align, DkglRing *out);

/* Descriptor slots. Slots are freed after the current submission. */
int dkgl_image_slot_alloc(void);
void dkgl_image_slot_free(int slot);
void dkgl_write_image_descriptor(int slot, const DkImageView *view,
                                 bool load_store);
uint32_t dkgl_sampler_slot(const DkSampler *sampler);

/* ------------------------------------------------------------ submission */

typedef struct {
  bool initialized;
  DkDevice device;
  DkQueue queue;
  DkCmdBuf cmd;
  RMutex lock;
  uint64_t serial;    /* serial of the list being recorded */
  uint64_t completed; /* every serial <= this has finished on the GPU */
  /* barrier bookkeeping */
  bool work_3d_since_barrier; /* draws/clears recorded after the last barrier */
  bool copies_pending;        /* copy-engine work not yet fenced from 3D */
  bool rt_written;            /* the bound targets received rendering */
  uint32_t draws_since_flush;
  /* frame statistics (reset every report) */
  uint32_t stat_draws, stat_dispatches, stat_flushes, stat_uploads;
  uint32_t stat_copies_barriers, stat_rt_barriers, stat_compiles;
  uint32_t stat_cache_hits, stat_renames;
  uint64_t stat_upload_bytes, stat_uniform_bytes, stat_compile_ticks;
  uint64_t stat_wait_ticks;
  uint64_t stat_acquire_ticks; /* CPU blocked in dkSwapchainAcquireImage */
} DkglGlobals;

extern DkglGlobals dkg;

#define DKGL_LOCK() rmutexLock(&dkg.lock)
#define DKGL_UNLOCK() rmutexUnlock(&dkg.lock)

void dkgl_device_init(void);
DkCmdBuf dkgl_cmd(void);      /* the open command list (begins one if needed) */
void dkgl_flush(void);         /* submit what is recorded */
void dkgl_finish(void);        /* submit and wait for the GPU */
void dkgl_retire(void);        /* reclaim finished submissions */
bool dkgl_serial_done(uint64_t serial);
void dkgl_wait_serial(uint64_t serial);
/* Fence the most recent submission for a sync object / query. */
uint64_t dkgl_signal_point(void);

/* Barrier state machine around the copy engine and render targets. */
void dkgl_before_copy(void);    /* about to record copy-engine work */
void dkgl_before_3d(void);      /* about to record a draw/clear/dispatch */
void dkgl_rt_barrier(void);     /* render targets change or get sampled */
void dkgl_note_draw(void);      /* periodic mid-frame submission */

/* ------------------------------------------------------------ formats */

enum {
  DKGL_FMT_DEPTH = 1 << 0,
  DKGL_FMT_STENCIL = 1 << 1,
  DKGL_FMT_COMPRESSED = 1 << 2,
  DKGL_FMT_INTEGER = 1 << 3,
  DKGL_FMT_RENDER = 1 << 4,
  DKGL_FMT_RGB_TO_RGBX = 1 << 5, /* 3-component client data padded to 4 */
  DKGL_FMT_TO_RGBA8 = 1 << 6,    /* packed 16-bit client data expanded */
  DKGL_FMT_SIGNED = 1 << 7,
  DKGL_FMT_FLOAT = 1 << 8,
  DKGL_FMT_2D_ENGINE = 1 << 9,
};

typedef struct {
  GLenum internal;
  DkImageFormat dk;
  uint8_t bytes;   /* per texel, or per block when compressed */
  uint8_t bw, bh;  /* block size */
  uint16_t flags;
  GLenum format;   /* GL client format that matches without conversion */
  GLenum type;
  uint8_t swizzle[4]; /* DkImageSwizzle; 0 = identity */
} DkglFormat;

const DkglFormat *dkgl_format(GLenum internal);
const DkglFormat *dkgl_format_unsized(GLenum format, GLenum type);
uint32_t dkgl_client_bpp(GLenum format, GLenum type);

/* ------------------------------------------------------------ objects */

typedef struct {
  GLenum min_filter, mag_filter;
  GLenum wrap_s, wrap_t, wrap_r;
  GLenum compare_mode, compare_func;
  float min_lod, max_lod, max_aniso;
  float border[4];
} DkglSamplerState;

typedef struct DkglBuffer {
  GLuint name;
  uint32_t size;
  GLenum usage;
  bool immutable;
  bool ubo_seen; /* bound as a uniform block once (mvl_nx trace) */
  GLbitfield storage_flags;
  DkglAlloc mem;
  uint64_t last_use;
  uint32_t version; /* bumps whenever the storage moves */
  /* glMapBufferRange */
  bool mapped;
  GLbitfield map_access;
  uint32_t map_offset, map_length;
  void *map_ptr;
  void *map_staging; /* malloc'd shadow for staged write maps */
} DkglBuffer;

typedef struct DkglTexture {
  GLuint name;
  GLenum target;
  bool has_storage;
  bool immutable;
  GLenum internal_format;
  const DkglFormat *fmt;
  uint8_t swizzle_fmt[4];
  uint32_t width, height, depth; /* depth = layers (x6 for cube arrays) */
  uint32_t levels;               /* allocated mip levels */
  uint32_t level_defined;        /* bit mask of specified levels (mutable) */
  DkImageLayout layout;
  DkImage image;
  DkglAlloc mem;
  uint32_t generation;           /* bumps on (re)allocation */
  /* texture buffer */
  GLuint tbo_buffer;
  uint32_t tbo_offset, tbo_size, tbo_version;
  /* parameters */
  DkglSamplerState sampler;
  GLint base_level, max_level;
  GLenum swizzle[4];
  GLenum ds_mode;
  /* descriptors */
  int view_slot;          /* -1 until built */
  uint32_t view_key;      /* generation/params the slot was built from */
  bool view_dirty;
  uint32_t sampler_slot;  /* own-parameter sampler (valid when !sampler_dirty) */
  bool sampler_dirty;
  uint32_t sampler_gen;   /* dkgl_sampler_policy_gen the slot was built for */
  int store_slot;         /* image load/store descriptor */
  bool compressed;        /* colour target with DkImageFlags_HwCompression */
  bool rt_checked;        /* compression decided at its first colour bind */
  bool want_compression;  /* tex_allocate: allocate compressed */
  uint32_t store_key;
  uint64_t last_use;
} DkglTexture;

typedef struct DkglSampler {
  GLuint name;
  DkglSamplerState state;
  uint32_t slot;
  bool dirty;
  uint32_t gen; /* dkgl_sampler_policy_gen the slot was built for */
} DkglSampler;

typedef struct DkglRenderbuffer {
  GLuint name;
  bool has_storage;
  GLenum internal_format;
  const DkglFormat *fmt;
  uint32_t width, height, samples;
  DkImageLayout layout;
  DkImage image;
  DkglAlloc mem;
  uint32_t generation;
  uint64_t last_use;
} DkglRenderbuffer;

typedef struct {
  GLenum type; /* GL_NONE, GL_TEXTURE, GL_RENDERBUFFER */
  GLuint name;
  GLint level;
  GLint layer; /* cube face / array layer / 3D slice */
} DkglAttachment;

typedef struct DkglFramebuffer {
  GLuint name;
  DkglAttachment color[DKGL_DRAW_BUFFERS];
  DkglAttachment depth, stencil;
  GLenum draw_buffers[DKGL_DRAW_BUFFERS];
  GLenum read_buffer;
  uint32_t version;
} DkglFramebuffer;

typedef struct {
  bool enabled;
  bool integer;
  bool normalized;
  uint8_t size; /* 1..4, or 5 for GL_BGRA */
  GLenum type;
  uint32_t relative_offset;
  uint8_t binding;
} DkglAttrib;

typedef struct {
  GLuint buffer;
  GLintptr offset;
  uint32_t stride; /* effective (0 when tightly packed has been resolved) */
  uint32_t divisor;
  const void *client; /* client-memory array (buffer 0) */
} DkglVertexBinding;

typedef struct DkglVao {
  GLuint name;
  DkglAttrib attribs[DKGL_ATTRIBS];
  DkglVertexBinding bindings[DKGL_VBO_BINDINGS];
  GLuint element_buffer;
} DkglVao;

typedef struct DkglStageBinary {
  uint32_t refs;
  uint64_t hash;
  uint64_t uid; /* never reused: a freed binary's memory can hold a new one */
  int stage; /* DKGL_GLSL_* */
  DkglGlslResult *refl; /* reflection; refl->source freed after compile */
  void *dksh;
  size_t dksh_size;
  char *trace_source; /* rewritten GLSL, kept only when tracing is enabled */
  bool trace_dumped;
} DkglStageBinary;

typedef struct DkglShader {
  GLuint name;
  GLenum type;
  char *source;
  bool compiled;
  bool compile_status;
  char *info_log;
  bool delete_pending;
  uint32_t attached;
  DkglStageBinary *binary;
  bool es1; /* GLSL ES 1.00: compiled at link time (dkgl_glsl_es1) */
} DkglShader;

enum { DKGL_SLOT_UNIFORM, DKGL_SLOT_SAMPLER, DKGL_SLOT_IMAGE };

typedef struct {
  int8_t stage;  /* -1 = unused */
  uint8_t binding;
  uint32_t offset;
  uint32_t stride;
  uint32_t size;
} DkglSlotTarget;

typedef struct {
  char name[DKGL_GLSL_NAME];
  uint8_t kind;
  uint8_t base;
  uint8_t columns, rows;
  uint32_t gl_type;
  uint32_t array_size;
  DkglSlotTarget at[DKGL_STAGES];
  /* samplers/images: unit per array element */
  uint8_t units[8];
  /* layout(location = n) as the shader wrote it, -1 none: then
   * glGetUniformLocation answers n (+ element) (mvl_nx) */
  int16_t explicit_loc;
} DkglUniformSlot;

typedef struct {
  uint8_t *shadow;  /* CPU copy of the block */
  uint32_t size;
  bool dirty;       /* changed since last upload */
  DkGpuAddr gpu;    /* last upload */
  uint64_t serial;  /* submission of the last upload */
} DkglUbo;

typedef struct DkglProgram {
  GLuint name;
  GLenum type; /* always 0: shaders (type != 0) share this name space */
  GLuint shaders[4];
  int num_shaders;
  bool link_status;
  bool validate_status;
  char *info_log;
  bool delete_pending;
  bool first_drawn; /* its first draw was logged */
  /* attribute bindings set before linking */
  struct {
    char name[DKGL_GLSL_NAME];
    GLuint index;
  } attrib_binds[16];
  int num_attrib_binds;
  /* linked */
  bool compute;
  DkShader dk[DKGL_STAGES];
  DkglAlloc code[DKGL_STAGES];
  DkglStageBinary *bin[DKGL_STAGES];
  DkglUbo ubo[DKGL_STAGES][16];
  int num_ubo[DKGL_STAGES];
  DkglUniformSlot *slots;
  int num_slots;
  /* texture binding -> slot/element (for the unit lookup) */
  struct {
    int16_t slot;
    uint8_t element;
    uint8_t target;
  } tex[DKGL_STAGES][32], img[DKGL_STAGES][8];
  int num_tex[DKGL_STAGES], num_img[DKGL_STAGES];
  uint32_t input_mask;          /* vertex attribute locations read */
  uint8_t input_base[32];       /* DKGL_BASE_* per location */
  /* the GL attribute index feeding each location: the glBindAttribLocation
   * made for the input's name before the link, else the location itself
   * (an ES 3.x input without layout(location) is numbered in declaration
   * order, which is not the slot Unity bound it to) */
  uint8_t attr_index[32];
  uint32_t ubo_block_binding[16]; /* program blocks (GL index): GL binding point */
  /* the program's uniform blocks as GL numbers them: each name once, vertex
   * stage first; blk_index[s][k] is stage s's block k in that list (mvl_nx) */
  int8_t blk_index[DKGL_STAGES][16];
  uint8_t num_blocks;
  bool blk_ready;
  /* the blocks' members, after the slots in GL's active uniform list
   * (each name once; mem_block: its program block) (mvl_nx) */
  const DkglGlslMember *mem[DKGL_GLSL_MAX_MEMBERS];
  int8_t mem_block[DKGL_GLSL_MAX_MEMBERS];
  uint8_t num_mem;
  uint32_t version;
  uint64_t last_use;
} DkglProgram;

typedef struct DkglQuery {
  GLuint name;
  GLenum target;
  DkglAlloc report; /* two 16-byte reports: begin, end */
  uint64_t serial;  /* submission that wrote the end report */
  bool active;
  bool pending;
} DkglQuery;

typedef struct DkglSync {
  uint32_t magic;
  uint64_t serial;
} DkglSync;

/* ------------------------------------------------------------ context */

enum {
  DKGL_DIRTY_RASTER = 1u << 0,
  DKGL_DIRTY_DEPTH_STENCIL = 1u << 1,
  DKGL_DIRTY_STENCIL_REF = 1u << 2,
  DKGL_DIRTY_BLEND = 1u << 3,
  DKGL_DIRTY_COLOR_WRITE = 1u << 4,
  DKGL_DIRTY_BLEND_CONST = 1u << 5,
  DKGL_DIRTY_VIEWPORT = 1u << 6,
  DKGL_DIRTY_SCISSOR = 1u << 7,
  DKGL_DIRTY_DEPTH_BIAS = 1u << 8,
  DKGL_DIRTY_MULTISAMPLE = 1u << 9,
  DKGL_DIRTY_PROGRAM = 1u << 10,
  DKGL_DIRTY_VERTEX = 1u << 11,
  DKGL_DIRTY_TEXTURES = 1u << 12,
  DKGL_DIRTY_IMAGES = 1u << 13,
  DKGL_DIRTY_FRAMEBUFFER = 1u << 14,
  DKGL_DIRTY_PRIM_RESTART = 1u << 15,
  DKGL_DIRTY_LINE = 1u << 16,
  DKGL_DIRTY_ALL = 0xffffffffu,
};

typedef struct DkglSurface DkglSurface;

typedef struct {
  GLenum eq_rgb, eq_a;
  GLenum src_rgb, dst_rgb, src_a, dst_a;
} DkglBlend;

typedef struct {
  GLenum func;
  GLint ref;
  GLuint func_mask;
  GLenum fail, zfail, zpass;
  GLuint write_mask;
} DkglStencilFace;

typedef struct DkglContext {
  uint32_t magic;
  struct DkglContext *share;
  DkglSurface *draw_surface;
  DkglSurface *read_surface;
  int client_version;
  GLenum error;
  uint32_t dirty;

  /* buffer bindings */
  GLuint array_buffer, copy_read, copy_write, pixel_pack, pixel_unpack;
  GLuint generic_uniform, generic_texture_buffer, draw_indirect,
      dispatch_indirect, generic_storage, generic_atomic;
  struct {
    GLuint buffer;
    GLintptr offset;
    GLsizeiptr size;
  } ubo_bindings[DKGL_UBO_BINDINGS];

  /* textures */
  GLuint active_texture;
  GLuint textures[DKGL_TEXTURE_UNITS][DKGL_TEX_TARGETS];
  GLuint samplers[DKGL_TEXTURE_UNITS];
  struct {
    GLuint texture;
    GLint level;
    GLboolean layered;
    GLint layer;
    GLenum access, format;
  } images[DKGL_IMAGE_UNITS];

  GLuint program;
  GLuint vao_name;
  DkglVao *vao;
  DkglVao default_vao;
  GLuint draw_fbo, read_fbo;
  GLuint renderbuffer;

  /* fixed function */
  GLint viewport[4];
  float depth_range[2];
  GLint scissor[4];
  bool scissor_test, cull_face, depth_test, stencil_test, polygon_offset_fill;
  bool alpha_to_coverage, sample_coverage, dither, rasterizer_discard;
  bool primitive_restart;
  uint8_t blend_enable; /* per draw buffer */
  GLenum cull_mode, front_face;
  GLenum depth_func;
  GLboolean depth_mask;
  DkglStencilFace stencil[2]; /* front, back */
  DkglBlend blend[DKGL_DRAW_BUFFERS];
  float blend_color[4];
  uint8_t color_mask[DKGL_DRAW_BUFFERS]; /* RGBA bits */
  float polygon_offset_factor, polygon_offset_units;
  float line_width;
  float sample_coverage_value;
  GLboolean sample_coverage_invert;
  float clear_color[4];
  float clear_depth;
  GLint clear_stencil;
  GLenum hints[4];

  /* pixel store */
  GLint unpack_alignment, unpack_row_length, unpack_image_height;
  GLint unpack_skip_pixels, unpack_skip_rows, unpack_skip_images;
  GLint pack_alignment, pack_row_length, pack_skip_pixels, pack_skip_rows;

  /* current generic attribute values */
  union {
    float f[4];
    int32_t i[4];
    uint32_t u[4];
  } current_attrib[DKGL_ATTRIBS];

  /* queries */
  GLuint active_query;
} DkglContext;

struct DkglSurface {
  uint32_t magic;
  bool window;
  uint32_t width, height;
  /* window */
  NWindow *nwindow;
  DkSwapchain swapchain;
  DkImage images[3];
  DkImageLayout image_layout;
  DkglAlloc image_mem[3];
  int num_images;
  int acquired; /* slot acquired for this frame, -1 if none */
  /* dkCmdBufWaitFence records a pointer that the queue reads at submit, so
   * the acquire fence lives here until the frame's lists are submitted. */
  DkFence acquire_fence;
  /* depth/stencil (and color for pbuffers) */
  DkImage depth;
  DkImageLayout depth_layout;
  DkglAlloc depth_mem;
  bool has_depth;
  DkImage color; /* pbuffer */
  DkImageLayout color_layout;
  DkglAlloc color_mem;
  bool has_color;
  int swap_interval;
  uint32_t frame;
};

extern __thread DkglContext *dkgl_tls_ctx;
static inline DkglContext *dkgl_ctx(void) { return dkgl_tls_ctx; }

void dkgl_set_error(DkglContext *ctx, GLenum error);
#define DKGL_ERROR(ctx, e) dkgl_set_error((ctx), (e))

/* Name tables (one global namespace for each object type). */
typedef enum {
  DKGL_OBJ_BUFFER,
  DKGL_OBJ_TEXTURE,
  DKGL_OBJ_SAMPLER,
  DKGL_OBJ_RENDERBUFFER,
  DKGL_OBJ_FRAMEBUFFER,
  DKGL_OBJ_VAO,
  DKGL_OBJ_SHADER_PROGRAM, /* shaders and programs share names */
  DKGL_OBJ_QUERY,
  DKGL_OBJ_TYPES
} DkglObjType;

GLuint dkgl_name_gen(DkglObjType type);
void *dkgl_name_get(DkglObjType type, GLuint name);
void dkgl_name_set(DkglObjType type, GLuint name, void *object);
void dkgl_name_free(DkglObjType type, GLuint name);
bool dkgl_name_reserved(DkglObjType type, GLuint name);

/* Lookups that create on first bind, as GLES allows. */
DkglBuffer *dkgl_buffer(GLuint name, bool create);
DkglTexture *dkgl_texture(GLuint name, bool create);
DkglRenderbuffer *dkgl_renderbuffer(GLuint name, bool create);
DkglFramebuffer *dkgl_framebuffer(GLuint name, bool create);
DkglShader *dkgl_shader(GLuint name);
DkglProgram *dkgl_program(GLuint name);

/* buffers */
GLuint *dkgl_buffer_target(DkglContext *ctx, GLenum target);
DkglBuffer *dkgl_bound_buffer(DkglContext *ctx, GLenum target);
void dkgl_buffer_used(DkglBuffer *buf);
bool dkgl_buffer_wait_cpu(DkglBuffer *buf); /* make CPU access safe */

/* textures */
int dkgl_target_index(GLenum target);
DkglTexture *dkgl_bound_texture(DkglContext *ctx, GLenum target);
DkResHandle dkgl_texture_handle(DkglContext *ctx, GLuint unit, int target,
                                bool shadow);
DkResHandle dkgl_image_handle(DkglContext *ctx, GLuint unit, int target);
bool dkgl_texture_view(DkglTexture *tex, GLint level, GLint layer,
                       DkImageView *view);
void dkgl_texture_used(DkglTexture *tex);
void dkgl_textures_init(void);
void dkgl_upload_pixels(DkglContext *ctx, DkglTexture *tex, GLint level,
                        GLint x, GLint y, GLint z, GLsizei w, GLsizei h,
                        GLsizei d, GLenum format, GLenum type,
                        const void *pixels, bool compressed,
                        GLsizei image_size);
extern uint32_t dkgl_texture_epoch; /* bumps on any texture/sampler change */
/* Wrapper filtering policy (from the config; dkgl_egl.c's A/B mode changes
 * it at run time): anisotropy floor for samplers that ask for anisotropy,
 * and whether GL_LINEAR_MIPMAP_NEAREST is upgraded to trilinear. */
extern int dkgl_policy_aniso, dkgl_policy_trilinear;
void dkgl_set_sampler_policy(int aniso, int trilinear);
/* Frame tracing (see dkgl_egl.c): enabled when dkgl_capture.txt exists,
 * on for the few frames that get traced to debug.log. */
extern bool dkgl_trace_enabled;
extern bool dkgl_trace_on;
void dkgl_capture_check(void);

/* GPU timestamps (dkgl_prof.c) */
void dkgl_prof_init(void);
void dkgl_prof_request(const char *reason); /* profile the next frame */
void dkgl_prof_frame_start(void);
void dkgl_prof_frame_end(uint32_t frame);
void dkgl_prof_acquire(bool after);
void dkgl_prof_pass(char kind, uint32_t id, uint32_t prog);
void dkgl_prof_draw(uint32_t prog, uint64_t fs);
void dkgl_prof_collect(void);
void dkgl_prof_take(uint64_t *avg_us, uint64_t *max_us);

/* Colour render-target compression (dkgl_texture.c): a texture is promoted at
 * its first colour bind if nothing but rendering has written it; the copy and
 * 2D engines see it only after dkgl_texture_prepare_transfer. */
void dkgl_texture_promote_rt(DkglTexture *tex);
void dkgl_texture_prepare_transfer(DkglTexture *tex);

/* framebuffers */
bool dkgl_bind_draw_targets(DkglContext *ctx);
void dkgl_surface_acquire(DkglSurface *surf);
bool dkgl_read_attachment(DkglContext *ctx, DkImageView *view,
                          const DkglFormat **fmt, uint32_t *w, uint32_t *h);
void dkgl_fbo_forget_targets(void);

/* programs */
void dkgl_shaders_init(void);
/* A stage's code; one still compiling is waited for, up to max_ms. */
const DkShader *dkgl_binary_shader_wait(DkglStageBinary *bin, uint32_t max_ms);
void dkgl_program_bind_uniforms(DkglContext *ctx, DkglProgram *prog,
                                bool program_changed);
void dkgl_program_release(DkglProgram *prog);

/* draw state */
void dkgl_draw_invalidate_all(void);
void dkgl_draw_forget_buffer(GLuint name);
void dkgl_query_record_begin(DkglQuery *q);
void dkgl_query_record_end(DkglQuery *q);

/* EGL */
void dkgl_egl_present_stats(void);

/* entry point table for imports.c / eglGetProcAddress */
void *dkgl_get_proc(const char *name);
/* the window is an sRGB render target (Unity linear colour space); mvl_nx */
extern bool dkgl_window_srgb;
/* Applies dkgl to every gl or egl entry of the import table. */
int dkgl_install_imports(void *table, size_t count);

/* logging */
void dkgl_log_once(const char *what);

#endif
