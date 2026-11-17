/* dkgl_device.c -- deko3d device, memory, submission and object names
 *
 * Memory: three heaps (data, image, shader code) of large deko3d blocks with
 * a segregated-fit sub-allocator, a ring for per-submission scratch data and
 * a pool of command-memory chunks. Everything the GPU may still read is freed
 * only after the submission that last used it has completed (deferred
 * frees keyed by submission serial).
 */
#include "dkgl.h"

#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "error.h"
#include "util.h"

DkglGlobals dkg;
__thread DkglContext *dkgl_tls_ctx;

/* -------------------------------------------------------------- logging */

void dkgl_log_once(const char *what) {
  static const char *seen[256];
  static int count;
  for (int i = 0; i < count; i++)
    if (seen[i] == what || !strcmp(seen[i], what))
      return;
  if (count < 256)
    seen[count++] = what;
  debugPrintf("dkgl: %s\n", what);
}

static void dk_debug(void *user, const char *context, DkResult result,
                     const char *message) {
  (void)user;
  debugPrintf("deko3d: %s result=%d %s\n", context ? context : "?",
              (int)result, message ? message : "");
}

/* ------------------------------------------------------------- heaps */

#define DKGL_SEG_BUCKETS 32
#define DKGL_MIN_ALLOC 256u

typedef struct Seg {
  uint32_t offset, size;
  struct Seg *prev, *next;   /* address order inside the arena */
  struct Seg *fprev, *fnext; /* free list */
  struct Arena *arena;
  uint8_t free;
  uint8_t bucket;
} Seg;

typedef struct Arena {
  DkMemBlock block;
  uint32_t size;
  uint32_t used;
  bool dedicated;
  Seg *first;
  struct Arena *next;
} Arena;

typedef struct {
  uint32_t flags;
  uint32_t arena_size;
  Arena *arenas;
  Seg *buckets[DKGL_SEG_BUCKETS];
  uint64_t reserved, used;
  uint32_t arena_count;
} Heap;

static Heap heaps[DKGL_HEAP_COUNT];

static uint32_t align_u32(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

static int bucket_of(uint32_t size) {
  int b = 31 - __builtin_clz(size | 1);
  return b < DKGL_SEG_BUCKETS ? b : DKGL_SEG_BUCKETS - 1;
}

static void free_list_insert(Heap *h, Seg *s) {
  s->bucket = (uint8_t)bucket_of(s->size);
  s->fprev = NULL;
  s->fnext = h->buckets[s->bucket];
  if (s->fnext)
    s->fnext->fprev = s;
  h->buckets[s->bucket] = s;
}

static void free_list_remove(Heap *h, Seg *s) {
  if (s->fprev)
    s->fprev->fnext = s->fnext;
  else
    h->buckets[s->bucket] = s->fnext;
  if (s->fnext)
    s->fnext->fprev = s->fprev;
  s->fprev = s->fnext = NULL;
}

static Arena *arena_create(Heap *h, uint32_t size, bool dedicated) {
  DkMemBlockMaker maker;
  dkMemBlockMakerDefaults(&maker, dkg.device, align_u32(size, DK_MEMBLOCK_ALIGNMENT));
  maker.flags = h->flags;
  DkMemBlock block = dkMemBlockCreate(&maker);
  if (!block)
    return NULL;
  Arena *a = calloc(1, sizeof(*a));
  Seg *s = calloc(1, sizeof(*s));
  if (!a || !s) {
    free(a);
    free(s);
    dkMemBlockDestroy(block);
    return NULL;
  }
  a->block = block;
  a->size = maker.size;
  a->dedicated = dedicated;
  a->first = s;
  a->next = h->arenas;
  h->arenas = a;
  h->reserved += a->size;
  h->arena_count++;
  s->offset = 0;
  s->size = a->size;
  s->arena = a;
  s->free = 1;
  free_list_insert(h, s);
  return a;
}

static void arena_destroy(Heap *h, Arena *a) {
  Arena **pp = &h->arenas;
  while (*pp && *pp != a)
    pp = &(*pp)->next;
  if (*pp)
    *pp = a->next;
  Seg *s = a->first;
  while (s) {
    Seg *next = s->next;
    if (s->free)
      free_list_remove(h, s);
    free(s);
    s = next;
  }
  h->reserved -= a->size;
  h->arena_count--;
  dkMemBlockDestroy(a->block);
  free(a);
}

static Seg *heap_find(Heap *h, uint32_t size, uint32_t align, uint32_t *start) {
  for (int b = bucket_of(size); b < DKGL_SEG_BUCKETS; b++) {
    for (Seg *s = h->buckets[b]; s; s = s->fnext) {
      uint32_t aligned = align_u32(s->offset, align);
      if (aligned >= s->offset && aligned - s->offset + (uint64_t)size <= s->size) {
        *start = aligned;
        return s;
      }
    }
  }
  return NULL;
}

bool dkgl_alloc(int heap, uint32_t size, uint32_t align, DkglAlloc *out) {
  Heap *h = &heaps[heap];
  memset(out, 0, sizeof(*out));
  if (!size)
    size = 1;
  if (align < DKGL_MIN_ALLOC)
    align = DKGL_MIN_ALLOC;
  size = align_u32(size, DKGL_MIN_ALLOC);
  uint32_t start = 0;
  Seg *s = NULL;
  if (size > h->arena_size / 2) {
    Arena *a = arena_create(h, align_u32(size, align), true);
    if (!a) {
      debugPrintf("dkgl: out of GPU memory (heap %d, %u bytes dedicated)\n",
                  heap, size);
      return false;
    }
    s = a->first;
    start = 0;
  } else {
    s = heap_find(h, size, align, &start);
    if (!s) {
      Arena *a = arena_create(h, h->arena_size, false);
      if (!a) {
        debugPrintf("dkgl: out of GPU memory (heap %d, %u bytes, %llu MiB "
                    "reserved)\n",
                    heap, size, (unsigned long long)(h->reserved >> 20));
        return false;
      }
      s = heap_find(h, size, align, &start);
      if (!s)
        return false;
    }
  }
  free_list_remove(h, s);
  /* split off the alignment padding */
  if (start > s->offset) {
    Seg *pad = calloc(1, sizeof(*pad));
    if (!pad) {
      free_list_insert(h, s);
      return false;
    }
    *pad = *s;
    pad->size = start - s->offset;
    pad->next = s;
    if (s->prev)
      s->prev->next = pad;
    else
      s->arena->first = pad;
    s->prev = pad;
    s->offset = start;
    s->size -= pad->size;
    pad->free = 1;
    free_list_insert(h, pad);
  }
  /* split off the tail */
  if (s->size > size && s->size - size >= DKGL_MIN_ALLOC) {
    Seg *tail = calloc(1, sizeof(*tail));
    if (tail) {
      tail->offset = s->offset + size;
      tail->size = s->size - size;
      tail->arena = s->arena;
      tail->prev = s;
      tail->next = s->next;
      if (s->next)
        s->next->prev = tail;
      s->next = tail;
      s->size = size;
      tail->free = 1;
      free_list_insert(h, tail);
    }
  }
  s->free = 0;
  s->arena->used += s->size;
  h->used += s->size;
  out->block = s->arena->block;
  out->offset = s->offset;
  out->size = s->size;
  out->node = s;
  out->heap = (uint8_t)heap;
  return true;
}

static void heap_free_now(DkglAlloc *a) {
  if (!a->node)
    return;
  Heap *h = &heaps[a->heap];
  Seg *s = a->node;
  Arena *arena = s->arena;
  s->free = 1;
  arena->used -= s->size;
  h->used -= s->size;
  if (s->prev && s->prev->free) {
    Seg *p = s->prev;
    free_list_remove(h, p);
    p->size += s->size;
    p->next = s->next;
    if (s->next)
      s->next->prev = p;
    free(s);
    s = p;
  }
  if (s->next && s->next->free) {
    Seg *n = s->next;
    free_list_remove(h, n);
    s->size += n->size;
    s->next = n->next;
    if (n->next)
      n->next->prev = s;
    free(n);
  }
  free_list_insert(h, s);
  /* Return empty arenas to the heap, keeping one shared arena warm. */
  if (arena->used == 0 && (arena->dedicated || h->arena_count > 1)) {
    bool other_shared = arena->dedicated;
    for (Arena *o = h->arenas; o && !other_shared; o = o->next)
      if (o != arena && !o->dedicated)
        other_shared = true;
    if (other_shared)
      arena_destroy(h, arena);
  }
  memset(a, 0, sizeof(*a));
}

/* ------------------------------------------------------ deferred frees */

enum { DEFER_ALLOC, DEFER_IMAGE_SLOT };

typedef struct {
  uint64_t serial;
  uint8_t kind;
  union {
    DkglAlloc alloc;
    int slot;
  } u;
} Deferred;

static Deferred *deferred;
static size_t deferred_head, deferred_tail, deferred_cap;

static void defer_push(const Deferred *d) {
  if (deferred_tail - deferred_head == deferred_cap) {
    size_t cap = deferred_cap ? deferred_cap * 2 : 1024;
    Deferred *n = malloc(cap * sizeof(*n));
    if (!n) {
      debugPrintf("dkgl: deferred free queue allocation failed\n");
      return;
    }
    for (size_t i = 0; i < deferred_tail - deferred_head; i++)
      n[i] = deferred[(deferred_head + i) % (deferred_cap ? deferred_cap : 1)];
    deferred_tail -= deferred_head;
    deferred_head = 0;
    free(deferred);
    deferred = n;
    deferred_cap = cap;
  }
  deferred[deferred_tail % deferred_cap] = *d;
  deferred_tail++;
}

void dkgl_free_deferred(DkglAlloc *a) {
  if (!a->node)
    return;
  Deferred d;
  d.serial = dkg.serial;
  d.kind = DEFER_ALLOC;
  d.u.alloc = *a;
  defer_push(&d);
  memset(a, 0, sizeof(*a));
}

/* ----------------------------------------------------- descriptor pools */

static DkMemBlock desc_block;
static DkImageDescriptor *image_descs;
static DkSamplerDescriptor *sampler_descs;
static int *image_free_slots;
static int image_free_count;

int dkgl_image_slot_alloc(void) {
  if (!image_free_count) {
    dkgl_retire();
    if (!image_free_count) {
      dkgl_log_once("image descriptor pool exhausted");
      return 0;
    }
  }
  return image_free_slots[--image_free_count];
}

void dkgl_image_slot_free(int slot) {
  if (slot <= 0)
    return;
  Deferred d;
  d.serial = dkg.serial;
  d.kind = DEFER_IMAGE_SLOT;
  d.u.slot = slot;
  defer_push(&d);
}

void dkgl_write_image_descriptor(int slot, const DkImageView *view,
                                 bool load_store) {
  dkImageDescriptorInitialize(&image_descs[slot], view, load_store, false);
}

typedef struct {
  DkSampler key;
  uint32_t slot;
  bool used;
} SamplerEntry;

#define SAMPLER_TABLE 8192
static SamplerEntry *sampler_table;
static uint32_t sampler_count;

uint32_t dkgl_sampler_slot(const DkSampler *sampler) {
  const uint8_t *p = (const uint8_t *)sampler;
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < sizeof(DkSampler); i++)
    h = (h ^ p[i]) * 16777619u;
  for (uint32_t probe = 0; probe < SAMPLER_TABLE; probe++) {
    SamplerEntry *e = &sampler_table[(h + probe) & (SAMPLER_TABLE - 1)];
    if (!e->used) {
      if (sampler_count >= DKGL_SAMPLER_DESCRIPTORS) {
        dkgl_log_once("sampler descriptor pool exhausted");
        return 0;
      }
      e->used = true;
      e->key = *sampler;
      e->slot = sampler_count++;
      dkSamplerDescriptorInitialize(&sampler_descs[e->slot], sampler);
      return e->slot;
    }
    if (!memcmp(&e->key, sampler, sizeof(DkSampler)))
      return e->slot;
  }
  return 0;
}

/* ------------------------------------------------------------- ring */

#define RING_SIZE (64u << 20)

static DkMemBlock ring_block;
static uint8_t *ring_cpu;
static DkGpuAddr ring_gpu;
static uint32_t ring_head; /* next free byte */
static uint32_t ring_tail; /* oldest byte still in use by the GPU */
static bool ring_full_wrap; /* head has wrapped behind tail */

/* ------------------------------------------------------ command memory */

#define CHUNK_SIZE (128u << 10)
#define CHUNK_BLOCK (4u << 20)

typedef struct {
  DkMemBlock block;
  uint32_t offset;
  uint64_t serial;
} Chunk;

/* The command buffer keeps appending into its current chunk across
 * dkCmdBufFinishList (dkCmdBufClear would rewind it over lists the GPU has
 * not run yet). A chunk is retired when deko3d asks for the next one: the
 * list being recorded at that moment is its last user. The tail of every
 * chunk is kept out of deko3d's reach as a guard. */
#define CHUNK_GUARD 256u

static Chunk *chunks;
static int chunk_count, chunk_cap;
static int *chunk_free;
static int chunk_free_count;
static int chunk_active = -1;
static void pending_chunk_push(int c);

static int chunk_new(void) {
  if (chunk_free_count)
    return chunk_free[--chunk_free_count];
  DkMemBlockMaker maker;
  dkMemBlockMakerDefaults(&maker, dkg.device, CHUNK_BLOCK);
  maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
  DkMemBlock block = dkMemBlockCreate(&maker);
  if (!block)
    return -1;
  const int per_block = CHUNK_BLOCK / CHUNK_SIZE;
  if (chunk_count + per_block > chunk_cap) {
    int cap = chunk_cap ? chunk_cap * 2 : 64;
    while (cap < chunk_count + per_block)
      cap *= 2;
    chunks = realloc(chunks, (size_t)cap * sizeof(*chunks));
    chunk_free = realloc(chunk_free, (size_t)cap * sizeof(int));
    chunk_cap = cap;
  }
  for (int i = 0; i < per_block; i++) {
    chunks[chunk_count + i].block = block;
    chunks[chunk_count + i].offset = (uint32_t)i * CHUNK_SIZE;
    chunks[chunk_count + i].serial = 0;
  }
  for (int i = per_block - 1; i >= 1; i--)
    chunk_free[chunk_free_count++] = chunk_count + i;
  int first = chunk_count;
  chunk_count += per_block;
  return first;
}

static void cmd_add_mem(void *user, DkCmdBuf cmdbuf, size_t min_size) {
  (void)user;
  if (min_size > CHUNK_SIZE - CHUNK_GUARD)
    fatal_error("dkgl: command request of %zu bytes exceeds a chunk", min_size);
  if (chunk_active >= 0) {
    chunks[chunk_active].serial = dkg.serial;
    pending_chunk_push(chunk_active);
  }
  int c = chunk_new();
  if (c < 0)
    fatal_error("dkgl: command memory exhausted");
  chunk_active = c;
  dkCmdBufAddMemory(cmdbuf, chunks[c].block, chunks[c].offset,
                    CHUNK_SIZE - CHUNK_GUARD);
}

/* ---------------------------------------------------------- submissions */

#define MAX_INFLIGHT 32

typedef struct {
  uint64_t serial;
  DkFence fence;
  /* Ring head at the previous submit. When the ring runs out halfway through
   * a draw's validation, its data was allocated before the forced submit but
   * is read by the next list, so a retiring list frees only up to where the
   * list before it ended. */
  uint32_t ring_end;
} Submission;

static uint32_t ring_last_submit_head;

static Submission inflight[MAX_INFLIGHT];
static int inflight_head, inflight_count;
static bool list_open;

/* In-use ring data is [tail, head) or, once wrapped, [tail, end) + [0, head).
 * Submissions retire in order, so the tail moves to the retired list's head. */
static void ring_retire_to(uint32_t end) {
  ring_tail = end;
  if (ring_full_wrap && ring_tail <= ring_head)
    ring_full_wrap = false;
}

static void retire_through(uint64_t serial) {
  if (serial <= dkg.completed)
    return;
  dkg.completed = serial;
  /* deferred frees */
  while (deferred_head != deferred_tail) {
    Deferred *d = &deferred[deferred_head % deferred_cap];
    if (d->serial > serial)
      break;
    if (d->kind == DEFER_ALLOC)
      heap_free_now(&d->u.alloc);
    else
      image_free_slots[image_free_count++] = d->u.slot;
    deferred_head++;
  }
}

static int pending_chunks_count;
static int *pending_chunks;
static int pending_chunks_cap;

static void pending_chunk_push(int c) {
  if (pending_chunks_count == pending_chunks_cap) {
    pending_chunks_cap = pending_chunks_cap ? pending_chunks_cap * 2 : 64;
    pending_chunks =
        realloc(pending_chunks, (size_t)pending_chunks_cap * sizeof(int));
  }
  pending_chunks[pending_chunks_count++] = c;
}

/* Release deko3d turns any API misuse or GPU fault into a silent abort;
 * say what happened first. */
static void check_queue(const char *where) {
  if (dkQueueIsInErrorState(dkg.queue)) {
    debug_log_flush();
    fatal_error("deko3d: the GPU queue faulted (%s, submission %llu).\n"
                "See debug.log.",
                where, (unsigned long long)dkg.serial);
  }
}

static void reclaim_chunks(void) {
  int kept = 0;
  for (int i = 0; i < pending_chunks_count; i++) {
    int c = pending_chunks[i];
    if (chunks[c].serial <= dkg.completed)
      chunk_free[chunk_free_count++] = c;
    else
      pending_chunks[kept++] = c;
  }
  pending_chunks_count = kept;
}

void dkgl_retire(void) {
  while (inflight_count) {
    Submission *s = &inflight[inflight_head];
    if (dkFenceWait(&s->fence, 0) != DkResult_Success)
      break;
    ring_retire_to(s->ring_end);
    retire_through(s->serial);
    inflight_head = (inflight_head + 1) % MAX_INFLIGHT;
    inflight_count--;
  }
  if (!inflight_count) {
    /* GPU idle: everything recorded before the open list is complete */
    if (dkg.serial > 0)
      retire_through(dkg.serial - 1);
    if (!list_open) {
      ring_head = ring_tail = ring_last_submit_head = 0;
      ring_full_wrap = false;
    }
  }
  reclaim_chunks();
}

static void wait_oldest(void) {
  if (!inflight_count)
    return;
  Submission *s = &inflight[inflight_head];
  const uint64_t t0 = armGetSystemTick();
  check_queue("wait");
  dkFenceWait(&s->fence, -1);
  dkg.stat_wait_ticks += armGetSystemTick() - t0;
  dkgl_retire();
}

DkCmdBuf dkgl_cmd(void);

DkCmdBuf dkgl_cmd(void) {
  if (!list_open) {
    list_open = true;
    /* The GPU L2 and its texture/constant/descriptor caches are not coherent
     * with CPU writes; everything this list reads from CPU-written memory was
     * written before it is submitted. */
    dkCmdBufBarrier(dkg.cmd, DkBarrier_None,
                    DkInvalidateFlags_L2Cache | DkInvalidateFlags_Image |
                        DkInvalidateFlags_Shader |
                        DkInvalidateFlags_Descriptors);
  }
  return dkg.cmd;
}

void dkgl_flush(void) {
  if (!dkg.initialized || !list_open)
    return;
  if (inflight_count == MAX_INFLIGHT)
    wait_oldest();
  check_queue("submit");
  DkCmdList list = dkCmdBufFinishList(dkg.cmd);
  dkQueueSubmitCommands(dkg.queue, list);
  Submission *s = &inflight[(inflight_head + inflight_count) % MAX_INFLIGHT];
  s->serial = dkg.serial;
  s->ring_end = ring_last_submit_head;
  ring_last_submit_head = ring_head;
  dkQueueSignalFence(dkg.queue, &s->fence, true);
  dkQueueFlush(dkg.queue);
  inflight_count++;
  list_open = false;
  dkg.serial++;
  dkg.draws_since_flush = 0;
  dkg.stat_flushes++;
  dkgl_retire();
}

bool dkgl_serial_done(uint64_t serial) {
  if (serial <= dkg.completed)
    return true;
  dkgl_retire();
  return serial <= dkg.completed;
}

void dkgl_wait_serial(uint64_t serial) {
  if (serial <= dkg.completed)
    return;
  if (serial >= dkg.serial)
    dkgl_flush();
  while (serial > dkg.completed && inflight_count)
    wait_oldest();
}

void dkgl_finish(void) {
  dkgl_flush();
  const uint64_t t0 = armGetSystemTick();
  dkQueueWaitIdle(dkg.queue);
  dkg.stat_wait_ticks += armGetSystemTick() - t0;
  dkgl_retire();
}

uint64_t dkgl_signal_point(void) {
  /* Everything recorded so far completes with the open list. */
  uint64_t serial = list_open ? dkg.serial : dkg.serial - 1;
  return serial;
}

bool dkgl_ring_alloc(uint32_t size, uint32_t align, DkglRing *out) {
  if (!size)
    size = 4;
  if (align < 16)
    align = 16;
  if (size > RING_SIZE / 4) {
    /* Too big for the ring: a heap allocation freed after this list. */
    DkglAlloc a;
    if (!dkgl_alloc(DKGL_HEAP_DATA, size, align, &a))
      return false;
    out->gpu = dkgl_gpu(&a);
    out->cpu = dkgl_cpu(&a);
    dkgl_free_deferred(&a);
    return true;
  }
  for (int attempt = 0; attempt < MAX_INFLIGHT + 4; attempt++) {
    uint32_t start = align_u32(ring_head, align);
    if (!ring_full_wrap) {
      /* [tail, head) in use: free space is [head, end) and [0, tail) */
      if ((uint64_t)start + size <= RING_SIZE) {
        ring_head = start + size;
        out->gpu = ring_gpu + start;
        out->cpu = ring_cpu + start;
        return true;
      }
      if (size <= ring_tail) {
        ring_full_wrap = true;
        ring_head = size;
        out->gpu = ring_gpu;
        out->cpu = ring_cpu;
        return true;
      }
    } else {
      /* wrapped: free space is [head, tail) */
      if ((uint64_t)start + size <= ring_tail) {
        ring_head = start + size;
        out->gpu = ring_gpu + start;
        out->cpu = ring_cpu + start;
        return true;
      }
    }
    /* Make room: submit our own allocations and wait for older ones. Once
     * nothing is in flight dkgl_retire() resets the ring to empty. */
    if (list_open)
      dkgl_flush();
    if (inflight_count)
      wait_oldest();
  }
  debugPrintf("dkgl: ring allocation of %u bytes failed\n", size);
  return false;
}

/* ----------------------------------------------------------- barriers */

void dkgl_before_copy(void) {
  DkCmdBuf cmd = dkgl_cmd();
  if (dkg.work_3d_since_barrier) {
    dkCmdBufBarrier(cmd, DkBarrier_Full, 0);
    dkg.work_3d_since_barrier = false;
    dkg.stat_copies_barriers++;
  }
  dkg.copies_pending = true;
}

void dkgl_before_3d(void) {
  DkCmdBuf cmd = dkgl_cmd();
  if (dkg.copies_pending) {
    dkCmdBufBarrier(cmd, DkBarrier_Full,
                    DkInvalidateFlags_Image | DkInvalidateFlags_Shader);
    dkg.copies_pending = false;
  }
  dkg.work_3d_since_barrier = true;
}

void dkgl_rt_barrier(void) {
  if (!dkg.rt_written)
    return;
  dkCmdBufBarrier(dkgl_cmd(), DkBarrier_Fragments, DkInvalidateFlags_Image);
  dkg.rt_written = false;
  dkg.stat_rt_barriers++;
}

void dkgl_note_draw(void) {
  dkg.stat_draws++;
  if (++dkg.draws_since_flush >= 192)
    dkgl_flush();
}

/* ------------------------------------------------------------- init */

void dkgl_device_init(void) {
  if (dkg.initialized)
    return;
  /* dkg.lock is a zero-initialised RMutex: callers may already hold it. */

  DkDeviceMaker device_maker;
  dkDeviceMakerDefaults(&device_maker);
  device_maker.cbDebug = dk_debug;
  device_maker.flags =
      DkDeviceFlags_DepthMinusOneToOne | DkDeviceFlags_OriginLowerLeft;
  dkg.device = dkDeviceCreate(&device_maker);
  if (!dkg.device)
    fatal_error("deko3d device creation failed.");

  heaps[DKGL_HEAP_DATA].flags =
      DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
  heaps[DKGL_HEAP_DATA].arena_size = 32u << 20;
  heaps[DKGL_HEAP_IMAGE].flags = DkMemBlockFlags_CpuUncached |
                                 DkMemBlockFlags_GpuCached |
                                 DkMemBlockFlags_Image;
  heaps[DKGL_HEAP_IMAGE].arena_size = 64u << 20;
  heaps[DKGL_HEAP_CODE].flags = DkMemBlockFlags_CpuUncached |
                                DkMemBlockFlags_GpuCached |
                                DkMemBlockFlags_Code;
  heaps[DKGL_HEAP_CODE].arena_size = 16u << 20;

  DkQueueMaker queue_maker;
  dkQueueMakerDefaults(&queue_maker, dkg.device);
  queue_maker.flags = DkQueueFlags_Graphics | DkQueueFlags_Compute |
                      DkQueueFlags_HighPrio;
  queue_maker.commandMemorySize = 1u << 20;
  queue_maker.flushThreshold = 256u << 10;
  queue_maker.perWarpScratchMemorySize = 8 * DK_PER_WARP_SCRATCH_MEM_ALIGNMENT;
  dkg.queue = dkQueueCreate(&queue_maker);
  if (!dkg.queue)
    fatal_error("deko3d queue creation failed.");

  DkCmdBufMaker cmd_maker;
  dkCmdBufMakerDefaults(&cmd_maker, dkg.device);
  cmd_maker.cbAddMem = cmd_add_mem;
  dkg.cmd = dkCmdBufCreate(&cmd_maker);

  /* descriptor pools */
  const uint32_t image_bytes = DKGL_IMAGE_DESCRIPTORS * sizeof(DkImageDescriptor);
  const uint32_t sampler_bytes =
      DKGL_SAMPLER_DESCRIPTORS * sizeof(DkSamplerDescriptor);
  DkMemBlockMaker desc_maker;
  dkMemBlockMakerDefaults(&desc_maker, dkg.device,
                          align_u32(image_bytes + sampler_bytes,
                                    DK_MEMBLOCK_ALIGNMENT));
  desc_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
  desc_block = dkMemBlockCreate(&desc_maker);
  if (!desc_block)
    fatal_error("deko3d descriptor pool allocation failed.");
  image_descs = dkMemBlockGetCpuAddr(desc_block);
  sampler_descs = (DkSamplerDescriptor *)((uint8_t *)image_descs + image_bytes);
  memset(image_descs, 0, image_bytes + sampler_bytes);
  image_free_slots = malloc(DKGL_IMAGE_DESCRIPTORS * sizeof(int));
  /* slot 0 is reserved for "no texture" */
  for (int i = DKGL_IMAGE_DESCRIPTORS - 1; i >= 1; i--)
    image_free_slots[image_free_count++] = i;
  sampler_table = calloc(SAMPLER_TABLE, sizeof(*sampler_table));

  DkMemBlockMaker ring_maker;
  dkMemBlockMakerDefaults(&ring_maker, dkg.device, RING_SIZE);
  ring_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
  ring_block = dkMemBlockCreate(&ring_maker);
  if (!ring_block)
    fatal_error("deko3d scratch ring allocation failed.");
  ring_cpu = dkMemBlockGetCpuAddr(ring_block);
  ring_gpu = dkMemBlockGetGpuAddr(ring_block);

  dkg.serial = 1;
  dkg.completed = 0;
  dkg.initialized = true;
  dkgl_prof_init();

  DkCmdBuf cmd = dkgl_cmd();
  const DkGpuAddr desc_gpu = dkMemBlockGetGpuAddr(desc_block);
  dkCmdBufBindImageDescriptorSet(cmd, desc_gpu, DKGL_IMAGE_DESCRIPTORS);
  dkCmdBufBindSamplerDescriptorSet(cmd, desc_gpu + image_bytes,
                                   DKGL_SAMPLER_DESCRIPTORS);
  dkgl_flush();

  debugPrintf("dkgl: deko3d device ready (GL origin/depth, %u image + %u "
              "sampler descriptors, %u MiB ring)\n",
              DKGL_IMAGE_DESCRIPTORS, DKGL_SAMPLER_DESCRIPTORS,
              RING_SIZE >> 20);
}

void dkgl_memory_report(char *out, size_t size);
void dkgl_memory_report(char *out, size_t size) {
  snprintf(out, size,
           "data=%llu/%llu image=%llu/%llu code=%llu/%llu MiB used/reserved "
           "chunks=%d inflight=%d",
           (unsigned long long)(heaps[0].used >> 20),
           (unsigned long long)(heaps[0].reserved >> 20),
           (unsigned long long)(heaps[1].used >> 20),
           (unsigned long long)(heaps[1].reserved >> 20),
           (unsigned long long)(heaps[2].used >> 20),
           (unsigned long long)(heaps[2].reserved >> 20), chunk_count,
           inflight_count);
}

/* ---------------------------------------------------------- name tables */

typedef struct {
  void **objects;
  uint32_t cap;
  uint32_t next;
  GLuint *free_names;
  uint32_t free_count, free_cap;
} NameTable;

static NameTable names[DKGL_OBJ_TYPES];

static bool name_grow(NameTable *t, GLuint name) {
  if (name < t->cap)
    return true;
  uint32_t cap = t->cap ? t->cap : 1024;
  while (cap <= name)
    cap *= 2;
  void **objects = realloc(t->objects, cap * sizeof(void *));
  if (!objects)
    return false;
  memset(objects + t->cap, 0, (cap - t->cap) * sizeof(void *));
  t->objects = objects;
  t->cap = cap;
  return true;
}

GLuint dkgl_name_gen(DkglObjType type) {
  NameTable *t = &names[type];
  if (t->free_count)
    return t->free_names[--t->free_count];
  if (!t->next)
    t->next = 1;
  GLuint name = t->next++;
  name_grow(t, name);
  return name;
}

void *dkgl_name_get(DkglObjType type, GLuint name) {
  NameTable *t = &names[type];
  return name && name < t->cap ? t->objects[name] : NULL;
}

void dkgl_name_set(DkglObjType type, GLuint name, void *object) {
  NameTable *t = &names[type];
  if (!name || !name_grow(t, name))
    return;
  t->objects[name] = object;
  if (name >= t->next)
    t->next = name + 1;
}

void dkgl_name_free(DkglObjType type, GLuint name) {
  NameTable *t = &names[type];
  if (!name || name >= t->cap)
    return;
  t->objects[name] = NULL;
  if (t->free_count == t->free_cap) {
    t->free_cap = t->free_cap ? t->free_cap * 2 : 256;
    t->free_names = realloc(t->free_names, t->free_cap * sizeof(GLuint));
  }
  t->free_names[t->free_count++] = name;
}

bool dkgl_name_reserved(DkglObjType type, GLuint name) {
  return dkgl_name_get(type, name) != NULL;
}

void dkgl_set_error(DkglContext *ctx, GLenum error) {
  if (ctx && ctx->error == GL_NO_ERROR)
    ctx->error = error;
#ifdef DEBUG_LOG
  if (debug_rate_limit("dkgl error", "", 64)) {
    void *caller = __builtin_return_address(0);
    debugPrintf("dkgl: GL error 0x%x (from %p)\n", error, caller);
  }
#endif
}
