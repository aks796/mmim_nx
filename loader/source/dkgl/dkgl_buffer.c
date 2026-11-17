/* dkgl_buffer.c -- buffer objects
 *
 * Buffer memory is CPU-mapped (uncached) GPU memory. A CPU write goes
 * straight in when no recorded or in-flight GPU work uses the buffer.
 * Otherwise the storage is renamed (glBufferData, invalidating maps: UE's
 * streaming pattern is WRITE|INVALIDATE_BUFFER|UNSYNCHRONIZED) or the data
 * is staged and copied in-stream behind a barrier (glBufferSubData, partial
 * maps), which keeps GL's ordering for earlier draws.
 */
#include "dkgl.h"

#include <stdlib.h>

#include "util.h"

DkCmdBuf dkgl_cmd(void);

DkglBuffer *dkgl_buffer(GLuint name, bool create) {
  if (!name)
    return NULL;
  DkglBuffer *buf = dkgl_name_get(DKGL_OBJ_BUFFER, name);
  if (buf || !create)
    return buf;
  buf = calloc(1, sizeof(*buf));
  if (!buf)
    return NULL;
  buf->name = name;
  buf->usage = GL_STATIC_DRAW;
  dkgl_name_set(DKGL_OBJ_BUFFER, name, buf);
  return buf;
}

GLuint *dkgl_buffer_target(DkglContext *ctx, GLenum target) {
  switch (target) {
  case GL_ARRAY_BUFFER:
    return &ctx->array_buffer;
  case GL_ELEMENT_ARRAY_BUFFER:
    return &ctx->vao->element_buffer;
  case GL_COPY_READ_BUFFER:
    return &ctx->copy_read;
  case GL_COPY_WRITE_BUFFER:
    return &ctx->copy_write;
  case GL_PIXEL_PACK_BUFFER:
    return &ctx->pixel_pack;
  case GL_PIXEL_UNPACK_BUFFER:
    return &ctx->pixel_unpack;
  case GL_UNIFORM_BUFFER:
    return &ctx->generic_uniform;
  case GL_TEXTURE_BUFFER:
    return &ctx->generic_texture_buffer;
  case GL_DRAW_INDIRECT_BUFFER:
    return &ctx->draw_indirect;
  case GL_DISPATCH_INDIRECT_BUFFER:
    return &ctx->dispatch_indirect;
  case GL_SHADER_STORAGE_BUFFER:
    return &ctx->generic_storage;
  case GL_ATOMIC_COUNTER_BUFFER:
    return &ctx->generic_atomic;
  default:
    return NULL;
  }
}

DkglBuffer *dkgl_bound_buffer(DkglContext *ctx, GLenum target) {
  GLuint *slot = dkgl_buffer_target(ctx, target);
  return slot ? dkgl_buffer(*slot, false) : NULL;
}

void dkgl_buffer_used(DkglBuffer *buf) { buf->last_use = dkg.serial; }

static bool buffer_busy(DkglBuffer *buf) {
  if (!buf->last_use)
    return false;
  if (buf->last_use >= dkg.serial)
    return true;
  return !dkgl_serial_done(buf->last_use);
}

bool dkgl_buffer_wait_cpu(DkglBuffer *buf) {
  if (!buffer_busy(buf))
    return true;
  dkgl_wait_serial(buf->last_use);
  return true;
}

static bool buffer_realloc(DkglBuffer *buf, uint32_t size) {
  DkglAlloc mem;
  memset(&mem, 0, sizeof(mem));
  /* +256: a uniform block is bound rounded up to 256 bytes
   * (dkgl_program_bind_uniforms), and Unity on ES 3.1 binds its last
   * per-draw slot up to the buffer's end (688 bytes at 512 of 1200) */
  if (size && !dkgl_alloc(DKGL_HEAP_DATA, size + 256, 256, &mem))
    return false;
  dkgl_free_deferred(&buf->mem);
  buf->mem = mem;
  buf->size = size;
  buf->version++;
  buf->last_use = 0;
  return true;
}

/* Writes into a range of the buffer as GL would at this point of the
 * command stream. */
static const char *buffer_write(DkglBuffer *buf, uint32_t offset, uint32_t size,
                                const void *data) {
  if (!size || !buf->mem.node)
    return "nothing";
  if (!buffer_busy(buf)) {
    memcpy((uint8_t *)dkgl_cpu(&buf->mem) + offset, data, size);
    return "direct";
  }
  if (offset == 0 && size == buf->size && !buf->immutable) {
    if (buffer_realloc(buf, size)) {
      memcpy(dkgl_cpu(&buf->mem), data, size);
      dkg.stat_renames++;
      return "renamed";
    }
  }
  DkglRing staging;
  if (!dkgl_ring_alloc(size, 16, &staging))
    return "DROPPED (no ring)";
  memcpy(staging.cpu, data, size);
  dkgl_before_copy();
  dkCmdBufCopyBuffer(dkgl_cmd(), staging.gpu, dkgl_gpu(&buf->mem) + offset,
                     size);
  dkgl_buffer_used(buf);
  return "staged copy";
}

/* mvl_nx (HW round 2): how Unity fills its uniform buffers. The first 60
 * calls on GL_UNIFORM_BUFFER, with the path each write took. */
static void ubo_log(const char *fn, GLenum target, const DkglBuffer *buf, long off, long size, unsigned flags,
                    const char *path) {
  static int n;
  if (dkgl_trace_on) {
    debugPrintf("trace buf %s target %x buf %u (%u bytes%s) off %ld size %ld flags 0x%x: %s\n", fn, target,
                buf ? buf->name : 0, buf ? buf->size : 0, buf && buf->immutable ? ", immutable" : "", off, size,
                flags, path);
    return;
  }
  if (!buf || !(buf->ubo_seen || buf->size == 688 || target == GL_UNIFORM_BUFFER) || n >= 24)
    return;
  n++;
  debugPrintf("dkgl: ubo %s target %x buf %u (%u bytes%s) off %ld size %ld flags 0x%x: %s\n", fn, target, buf ? buf->name : 0,
              buf ? buf->size : 0, buf && buf->immutable ? ", immutable" : "", off, size, flags, path);
}

void glGenBuffers(GLsizei n, GLuint *buffers) {
  DKGL_LOCK();
  for (GLsizei i = 0; i < n; i++) {
    buffers[i] = dkgl_name_gen(DKGL_OBJ_BUFFER);
    dkgl_buffer(buffers[i], true);
  }
  DKGL_UNLOCK();
}

void glDeleteBuffers(GLsizei n, const GLuint *buffers) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  for (GLsizei i = 0; i < n; i++) {
    DkglBuffer *buf = dkgl_buffer(buffers[i], false);
    if (!buf)
      continue;
    if (ctx) {
      GLuint *slots[] = {&ctx->array_buffer, &ctx->copy_read, &ctx->copy_write,
                         &ctx->pixel_pack, &ctx->pixel_unpack,
                         &ctx->generic_uniform, &ctx->generic_texture_buffer,
                         &ctx->draw_indirect, &ctx->dispatch_indirect,
                         &ctx->generic_storage, &ctx->generic_atomic,
                         &ctx->vao->element_buffer};
      for (size_t s = 0; s < sizeof(slots) / sizeof(slots[0]); s++)
        if (*slots[s] == buffers[i])
          *slots[s] = 0;
      for (int b = 0; b < DKGL_UBO_BINDINGS; b++)
        if (ctx->ubo_bindings[b].buffer == buffers[i])
          ctx->ubo_bindings[b].buffer = 0;
      for (int b = 0; b < DKGL_VBO_BINDINGS; b++)
        if (ctx->vao->bindings[b].buffer == buffers[i])
          ctx->vao->bindings[b].buffer = 0;
      ctx->dirty |= DKGL_DIRTY_VERTEX;
    }
    dkgl_draw_forget_buffer(buffers[i]);
    free(buf->map_staging);
    dkgl_free_deferred(&buf->mem);
    dkgl_name_free(DKGL_OBJ_BUFFER, buffers[i]);
    free(buf);
  }
  DKGL_UNLOCK();
}

GLboolean glIsBuffer(GLuint buffer) {
  DKGL_LOCK();
  GLboolean r = dkgl_buffer(buffer, false) ? GL_TRUE : GL_FALSE;
  DKGL_UNLOCK();
  return r;
}

void glBindBuffer(GLenum target, GLuint buffer) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  GLuint *slot = ctx ? dkgl_buffer_target(ctx, target) : NULL;
  if (!slot) {
    if (ctx)
      DKGL_ERROR(ctx, GL_INVALID_ENUM);
    DKGL_UNLOCK();
    return;
  }
  if (buffer)
    dkgl_buffer(buffer, true);
  *slot = buffer;
  if (target == GL_ELEMENT_ARRAY_BUFFER)
    ctx->dirty |= DKGL_DIRTY_VERTEX;
  DKGL_UNLOCK();
}

void glBindBufferRange(GLenum target, GLuint index, GLuint buffer,
                       GLintptr offset, GLsizeiptr size) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  if (!ctx) {
    DKGL_UNLOCK();
    return;
  }
  if (buffer)
    dkgl_buffer(buffer, true);
  GLuint *generic = dkgl_buffer_target(ctx, target);
  if (generic)
    *generic = buffer;
  if (dkgl_trace_on)
    debugPrintf("trace bind %x point %u buf %u off %ld size %ld\n", target, index, buffer, (long)offset,
                (long)size);
  if (target == GL_UNIFORM_BUFFER && index < DKGL_UBO_BINDINGS) {
    DkglBuffer *b = buffer ? dkgl_buffer(buffer, false) : NULL;
    static int first_logged;
    if (b && !b->ubo_seen) {
      b->ubo_seen = true;
      if (first_logged++ < 24)
        debugPrintf("dkgl: ubo bound first: buf %u (%u bytes%s) at point %u off %ld size %ld\n", buffer, b->size,
                  b->immutable ? ", immutable" : "", index, (long)offset, (long)size);
    }
    ctx->ubo_bindings[index].buffer = buffer;
    ctx->ubo_bindings[index].offset = offset;
    ctx->ubo_bindings[index].size = size;
  } else if (target != GL_UNIFORM_BUFFER) {
    dkgl_log_once("glBindBufferRange: non-uniform indexed target ignored");
  }
  DKGL_UNLOCK();
}

void glBindBufferBase(GLenum target, GLuint index, GLuint buffer) {
  glBindBufferRange(target, index, buffer, 0, -1);
}

void glBufferData(GLenum target, GLsizeiptr size, const void *data,
                  GLenum usage) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglBuffer *buf = ctx ? dkgl_bound_buffer(ctx, target) : NULL;
  if (!buf || size < 0 || buf->immutable) {
    if (ctx)
      DKGL_ERROR(ctx, buf ? GL_INVALID_OPERATION : GL_INVALID_OPERATION);
    DKGL_UNLOCK();
    return;
  }
  if (buf->mapped) {
    free(buf->map_staging);
    buf->map_staging = NULL;
    buf->mapped = false;
  }
  if ((uint32_t)size != buf->size || buffer_busy(buf) || !buf->mem.node) {
    if (!buffer_realloc(buf, (uint32_t)size)) {
      DKGL_ERROR(ctx, GL_OUT_OF_MEMORY);
      DKGL_UNLOCK();
      return;
    }
  }
  buf->usage = usage;
  if (data && size)
    memcpy(dkgl_cpu(&buf->mem), data, (size_t)size);
  ubo_log("BufferData", target, buf, 0, (long)size, usage, data ? "with data" : "no data");
  ctx->dirty |= DKGL_DIRTY_VERTEX;
  DKGL_UNLOCK();
}

void glBufferStorageEXT(GLenum target, GLsizeiptr size, const void *data,
                        GLbitfield flags) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglBuffer *buf = ctx ? dkgl_bound_buffer(ctx, target) : NULL;
  if (!buf || size <= 0 || buf->immutable) {
    if (ctx)
      DKGL_ERROR(ctx, GL_INVALID_OPERATION);
    DKGL_UNLOCK();
    return;
  }
  if (!buffer_realloc(buf, (uint32_t)size)) {
    DKGL_ERROR(ctx, GL_OUT_OF_MEMORY);
    DKGL_UNLOCK();
    return;
  }
  buf->immutable = true;
  buf->storage_flags = flags;
  if (data)
    memcpy(dkgl_cpu(&buf->mem), data, (size_t)size);
  ubo_log("BufferStorage", target, buf, 0, (long)size, flags, data ? "with data" : "no data");
  ctx->dirty |= DKGL_DIRTY_VERTEX;
  DKGL_UNLOCK();
}

void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size,
                     const void *data) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglBuffer *buf = ctx ? dkgl_bound_buffer(ctx, target) : NULL;
  if (!buf || offset < 0 || size < 0 ||
      (uint64_t)offset + (uint64_t)size > buf->size) {
    static int rejected;
    if (rejected < 20) {
      rejected++;
      debugPrintf("dkgl: glBufferSubData(%x, off %ld, size %ld) rejected: bound buffer %u (%u bytes)\n", target,
                  (long)offset, (long)size, buf ? buf->name : 0, buf ? buf->size : 0);
    }
    if (ctx)
      DKGL_ERROR(ctx, GL_INVALID_VALUE);
    DKGL_UNLOCK();
    return;
  }
  const char *path = data ? buffer_write(buf, (uint32_t)offset, (uint32_t)size, data) : "no data";
  ubo_log("BufferSubData", target, buf, (long)offset, (long)size, 0, path);
  ctx->dirty |= DKGL_DIRTY_VERTEX;
  DKGL_UNLOCK();
}

void *glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length,
                       GLbitfield access) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglBuffer *buf = ctx ? dkgl_bound_buffer(ctx, target) : NULL;
  if (!buf || buf->mapped || offset < 0 || length <= 0 ||
      (uint64_t)offset + (uint64_t)length > buf->size || !buf->mem.node) {
    static int rejected;
    if (rejected < 20) {
      rejected++;
      debugPrintf("dkgl: glMapBufferRange(%x, off %ld, len %ld, 0x%x) rejected: bound buffer %u (%u bytes%s)\n",
                  target, (long)offset, (long)length, access, buf ? buf->name : 0, buf ? buf->size : 0,
                  buf && buf->mapped ? ", already mapped" : "");
    }
    if (ctx)
      DKGL_ERROR(ctx, GL_INVALID_OPERATION);
    DKGL_UNLOCK();
    return NULL;
  }
  void *ptr = NULL;
  const char *path = "wait";
  buf->map_staging = NULL;
  if (access & GL_MAP_READ_BIT) {
    dkgl_buffer_wait_cpu(buf);
    ptr = (uint8_t *)dkgl_cpu(&buf->mem) + offset;
    path = "read: waited";
  } else if (!buffer_busy(buf)) {
    ptr = (uint8_t *)dkgl_cpu(&buf->mem) + offset;
    path = "idle: direct";
  } else if ((access & GL_MAP_INVALIDATE_BUFFER_BIT) && !buf->immutable) {
    if (buffer_realloc(buf, buf->size)) {
      dkg.stat_renames++;
      ctx->dirty |= DKGL_DIRTY_VERTEX;
    }
    ptr = (uint8_t *)dkgl_cpu(&buf->mem) + offset;
    path = "busy: renamed";
  } else if (access & (GL_MAP_INVALIDATE_RANGE_BIT |
                       GL_MAP_INVALIDATE_BUFFER_BIT)) {
    path = "busy: staged until unmap";
    /* Busy, and the old contents are not wanted: staged, copied in-stream
     * at the unmap, so draws recorded before keep their data. Before the
     * unsynchronised case: Unity's ES 2.0 renderer maps every dynamic
     * buffer write|invalidate-range|flush-explicit|unsynchronised at offset
     * 0 and draws from it in the same frame; written in place, the frame's
     * earlier draws (recorded, not yet run) read the later data. */
    buf->map_staging = malloc((size_t)length);
    ptr = buf->map_staging;
  } else if (access & GL_MAP_UNSYNCHRONIZED_BIT) {
    ptr = (uint8_t *)dkgl_cpu(&buf->mem) + offset;
    path = "busy: unsynchronised direct";
  } else {
    dkgl_buffer_wait_cpu(buf);
    ptr = (uint8_t *)dkgl_cpu(&buf->mem) + offset;
    path = "busy: waited";
  }
  ubo_log("MapBufferRange", target, buf, (long)offset, (long)length, access, path);
  if (ptr) {
    buf->mapped = true;
    buf->map_access = access;
    buf->map_offset = (uint32_t)offset;
    buf->map_length = (uint32_t)length;
    buf->map_ptr = ptr;
  } else {
    DKGL_ERROR(ctx, GL_OUT_OF_MEMORY);
  }
  DKGL_UNLOCK();
  return ptr;
}

void glFlushMappedBufferRange(GLenum target, GLintptr offset,
                              GLsizeiptr length) {
  (void)target;
  (void)offset;
  (void)length;
  /* Direct maps are uncached memory; staged maps are copied at unmap. */
}

GLboolean glUnmapBuffer(GLenum target) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglBuffer *buf = ctx ? dkgl_bound_buffer(ctx, target) : NULL;
  if (!buf || !buf->mapped) {
    if (ctx)
      DKGL_ERROR(ctx, GL_INVALID_OPERATION);
    DKGL_UNLOCK();
    return GL_FALSE;
  }
  if (buf->map_staging) {
    const char *path = buffer_write(buf, buf->map_offset, buf->map_length, buf->map_staging);
    ubo_log("UnmapBuffer", target, buf, buf->map_offset, buf->map_length, 0, path);
    free(buf->map_staging);
    buf->map_staging = NULL;
  }
  buf->mapped = false;
  buf->map_ptr = NULL;
  DKGL_UNLOCK();
  return GL_TRUE;
}

void glCopyBufferSubData(GLenum readTarget, GLenum writeTarget,
                         GLintptr readOffset, GLintptr writeOffset,
                         GLsizeiptr size) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglBuffer *src = ctx ? dkgl_bound_buffer(ctx, readTarget) : NULL;
  DkglBuffer *dst = ctx ? dkgl_bound_buffer(ctx, writeTarget) : NULL;
  if (!src || !dst || size <= 0 || !src->mem.node || !dst->mem.node ||
      (uint64_t)readOffset + size > src->size ||
      (uint64_t)writeOffset + size > dst->size) {
    if (ctx)
      DKGL_ERROR(ctx, GL_INVALID_VALUE);
    DKGL_UNLOCK();
    return;
  }
  dkgl_before_copy();
  dkCmdBufCopyBuffer(dkgl_cmd(), dkgl_gpu(&src->mem) + readOffset,
                     dkgl_gpu(&dst->mem) + writeOffset, (uint32_t)size);
  dkgl_buffer_used(src);
  dkgl_buffer_used(dst);
  {
    static int n;
    if (n < 20 || dkgl_trace_on) {
      n++;
      debugPrintf("dkgl: copy %u+%ld -> %u+%ld (%ld bytes; targets %x -> %x)\n", src->name, (long)readOffset,
                  dst->name, (long)writeOffset, (long)size, readTarget, writeTarget);
    }
  }
  DKGL_UNLOCK();
}

static bool buffer_param(DkglBuffer *buf, GLenum pname, GLint64 *out) {
  switch (pname) {
  case GL_BUFFER_SIZE:
    *out = buf->size;
    return true;
  case GL_BUFFER_USAGE:
    *out = buf->usage;
    return true;
  case GL_BUFFER_MAPPED:
    *out = buf->mapped;
    return true;
  case GL_BUFFER_ACCESS_FLAGS:
    *out = buf->mapped ? buf->map_access : 0;
    return true;
  case GL_BUFFER_MAP_LENGTH:
    *out = buf->mapped ? buf->map_length : 0;
    return true;
  case GL_BUFFER_MAP_OFFSET:
    *out = buf->mapped ? buf->map_offset : 0;
    return true;
  case GL_BUFFER_IMMUTABLE_STORAGE_EXT:
    *out = buf->immutable;
    return true;
  case GL_BUFFER_STORAGE_FLAGS_EXT:
    *out = buf->storage_flags;
    return true;
  default:
    return false;
  }
}

void glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglBuffer *buf = ctx ? dkgl_bound_buffer(ctx, target) : NULL;
  GLint64 v = 0;
  if (buf && buffer_param(buf, pname, &v))
    params[0] = (GLint)v;
  else if (ctx)
    DKGL_ERROR(ctx, GL_INVALID_ENUM);
  DKGL_UNLOCK();
}

void glGetBufferParameteri64v(GLenum target, GLenum pname, GLint64 *params) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglBuffer *buf = ctx ? dkgl_bound_buffer(ctx, target) : NULL;
  if (!buf || !buffer_param(buf, pname, params)) {
    if (ctx)
      DKGL_ERROR(ctx, GL_INVALID_ENUM);
  }
  DKGL_UNLOCK();
}

void glGetBufferPointerv(GLenum target, GLenum pname, void **params) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglBuffer *buf = ctx ? dkgl_bound_buffer(ctx, target) : NULL;
  if (buf && pname == GL_BUFFER_MAP_POINTER)
    *params = buf->mapped ? buf->map_ptr : NULL;
  else if (ctx)
    DKGL_ERROR(ctx, GL_INVALID_ENUM);
  DKGL_UNLOCK();
}

void *glMapBufferOES(GLenum target, GLenum access) {
  (void)access;
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglBuffer *buf = ctx ? dkgl_bound_buffer(ctx, target) : NULL;
  GLsizeiptr size = buf ? (GLsizeiptr)buf->size : 0;
  DKGL_UNLOCK();
  return glMapBufferRange(target, 0, size, GL_MAP_WRITE_BIT);
}

GLboolean glUnmapBufferOES(GLenum target) { return glUnmapBuffer(target); }
