/* dkgl_shader.c -- shaders, programs, uniforms and the DKSH cache
 *
 * glCompileShader rewrites the GLSL (dkgl_glsl.c), hashes the result and
 * looks it up in memory, then in shadercache/dkgl.pack, before running uam.
 * A compiled stage (DkglStageBinary) is loaded into code memory once and
 * shared by every program that links it. A stage missing from both caches is
 * compiled by uam on a worker thread; draws that need it are skipped until
 * it lands, so a new effect costs a few frames without it instead of a
 * 10-80 ms hitch on the render thread. Uniform arrays live in CPU shadow
 * blocks per program stage and are copied to the per-submission ring when a
 * draw needs them after a change.
 */
#include "dkgl.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "error.h"
#include "uam_bridge.h"
#include "util.h"

DkCmdBuf dkgl_cmd(void);

#define DKGL_PACK_PATH dkgl_path("shadercache/dkgl.pack")
#define DKGL_PACK_MAGIC 0x4b50474bu /* "KGPK" */
#define DKGL_ENTRY_MAGIC 0x4853444bu /* "KDSH" */
/* Bump when the rewriter or the uam patches change generated code, or the
 * pack's layout (2: entries carry a checksum of their DKSH). */
#define DKGL_SHADER_VERSION 2u

static Mutex compile_mutex;

/* ------------------------------------------------------------ binaries */

#define BINARY_BUCKETS 4096

enum { BIN_READY, BIN_PENDING, BIN_FAILED };

typedef struct BinaryNode {
  DkglStageBinary bin;
  DkShader dk;
  DkglAlloc code;
  bool loaded;
  int state; /* BIN_*, written by the compile worker with release order */
  struct BinaryNode *next;
} BinaryNode;

static BinaryNode *binaries[BINARY_BUCKETS];
static uint64_t binary_uids; /* (compile_mutex) */

static BinaryNode *binary_find(uint64_t hash) {
  for (BinaryNode *n = binaries[hash & (BINARY_BUCKETS - 1)]; n; n = n->next)
    if (n->bin.hash == hash)
      return n;
  return NULL;
}

static BinaryNode *node_of(DkglStageBinary *bin) { return (BinaryNode *)bin; }

/* Binaries are shared between the compile path (compile_mutex, GL lock not
 * held) and the GL objects (GL lock held). Lock order: GL lock, then
 * compile_mutex. */
static void binary_ref(DkglStageBinary *bin) {
  mutexLock(&compile_mutex);
  bin->refs++;
  mutexUnlock(&compile_mutex);
}

static void binary_release(DkglStageBinary *bin) {
  if (!bin)
    return;
  mutexLock(&compile_mutex);
  const bool last = --bin->refs == 0;
  BinaryNode *node = node_of(bin);
  if (last) {
    BinaryNode **pp = &binaries[bin->hash & (BINARY_BUCKETS - 1)];
    while (*pp && *pp != node)
      pp = &(*pp)->next;
    if (*pp)
      *pp = node->next;
  }
  mutexUnlock(&compile_mutex);
  if (!last)
    return;
  dkgl_free_deferred(&node->code);
  free(bin->dksh);
  free(bin->trace_source);
  if (bin->refl) {
    free(bin->refl->source);
    free(bin->refl);
  }
  free(node);
}

/* Binaries whose last reference the compile worker held (GL had dropped the
 * program meanwhile). Freeing one touches GL-lock state, and the worker
 * taking the GL lock for it stalled behind a draw waiting under that lock
 * for the worker's next shader: 2 s hitches at a match's start in MMIM
 * ("draw waited 2000 ms" for a 130 ms compile). The worker queues them here
 * instead and the GL side frees them. (compile_mutex) */
#define WORKER_RELEASED_MAX 64
static BinaryNode *worker_released[WORKER_RELEASED_MAX];
static int n_worker_released;

/* GL lock held */
static void drain_worker_releases(void) {
  if (!__atomic_load_n(&n_worker_released, __ATOMIC_ACQUIRE))
    return;
  BinaryNode *list[WORKER_RELEASED_MAX];
  mutexLock(&compile_mutex);
  const int n = n_worker_released;
  memcpy(list, worker_released, (size_t)n * sizeof(*list));
  __atomic_store_n(&n_worker_released, 0, __ATOMIC_RELEASE);
  mutexUnlock(&compile_mutex);
  for (int i = 0; i < n; i++)
    binary_release(&list[i]->bin);
}

/* Copies the code section into code memory and builds the DkShader. */
static bool binary_load(BinaryNode *node) {
  if (node->loaded)
    return true;
  if (__atomic_load_n(&node->state, __ATOMIC_ACQUIRE) != BIN_READY)
    return false;
  const uint32_t *hdr = node->bin.dksh;
  if (!hdr || node->bin.dksh_size < 24 || hdr[0] != 0x48534b44u) /* "DKSH" */
    return false;
  const uint32_t control_sz = hdr[2], code_sz = hdr[3];
  if (control_sz + code_sz > node->bin.dksh_size)
    return false;
  if (!dkgl_alloc(DKGL_HEAP_CODE, code_sz + DK_SHADER_CODE_UNUSABLE_SIZE,
                  DK_SHADER_CODE_ALIGNMENT, &node->code))
    return false;
  memcpy(dkgl_cpu(&node->code), (const uint8_t *)node->bin.dksh + control_sz,
         code_sz);
  DkShaderMaker maker;
  dkShaderMakerDefaults(&maker, node->code.block, node->code.offset);
  maker.control = node->bin.dksh;
  dkShaderInitialize(&node->dk, &maker);
  node->loaded = dkShaderIsValid(&node->dk);
  return node->loaded;
}

const DkShader *dkgl_binary_shader(DkglStageBinary *bin) {
  BinaryNode *node = node_of(bin);
  return binary_load(node) ? &node->dk : NULL;
}

/* For a draw: a stage still on the compile worker is waited for (up to
 * max_ms) instead of the draw being dropped, as a driver that compiles at
 * the link would have stalled; once per shader per install, the cache has
 * it after. The worker publishes the state before taking the GL lock, so
 * this may wait under it. */
const DkShader *dkgl_binary_shader_wait(DkglStageBinary *bin, uint32_t max_ms) {
  BinaryNode *node = node_of(bin);
  drain_worker_releases();
  if (binary_load(node))
    return &node->dk;
  const uint64_t t0 = armGetSystemTick();
  while (__atomic_load_n(&node->state, __ATOMIC_ACQUIRE) == BIN_PENDING &&
         armTicksToNs(armGetSystemTick() - t0) < (uint64_t)max_ms * 1000000ull)
    svcSleepThread(200000);
  const uint64_t waited = armTicksToNs(armGetSystemTick() - t0) / 1000000ull;
  static uint32_t waits;
  if (++waits <= 16 || (waits % 64) == 0)
    debugPrintf("dkgl: draw waited %llu ms for shader %016llx to compile (%u waits)\n",
                (unsigned long long)waited, (unsigned long long)node->bin.hash, waits);
  return binary_load(node) ? &node->dk : NULL;
}

/* ---------------------------------------------------------- disk cache */

typedef struct {
  uint64_t hash;
  uint32_t offset; /* of the DKSH bytes */
  uint32_t size;
  uint32_t sum; /* pack_sum of them */
} PackEntry;

static PackEntry *pack_index;
static uint32_t pack_count, pack_cap;
static FILE *pack_file;
static bool pack_ready;
/* The whole pack, read once when it opens: a cache hit is a copy, not a
 * read of the SD (mmim_nx HW, 2026-11-16: a match's first shaders, all in
 * the pack, took 26-85 ms each, the GL thread waiting in the fs) */
static uint8_t *pack_mem;
static long pack_mem_size;
/* for the [perf] line (dkgl_shader_stats) */
static unsigned st_memory, st_hits, st_compiled;
static uint64_t st_rewrite_ticks, st_lookup_ticks;

void dkgl_shader_stats(unsigned *memory, unsigned *hits, unsigned *compiled, uint64_t *rewrite_ticks,
                       uint64_t *lookup_ticks) {
  *memory = st_memory, *hits = st_hits, *compiled = st_compiled;
  *rewrite_ticks = st_rewrite_ticks, *lookup_ticks = st_lookup_ticks;
  st_memory = st_hits = st_compiled = 0;
  st_rewrite_ticks = st_lookup_ticks = 0;
}

static void pack_add_index(uint64_t hash, uint32_t offset, uint32_t size, uint32_t sum) {
  if (pack_count == pack_cap) {
    pack_cap = pack_cap ? pack_cap * 2 : 1024;
    pack_index = realloc(pack_index, pack_cap * sizeof(*pack_index));
  }
  pack_index[pack_count].hash = hash;
  pack_index[pack_count].offset = offset;
  pack_index[pack_count].size = size;
  pack_index[pack_count].sum = sum;
  pack_count++;
}

/* FNV-1a over an entry's DKSH: a torn or padded entry fails it. */
static uint32_t pack_sum(const void *data, size_t size) {
  const uint8_t *p = data;
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < size; i++)
    h = (h ^ p[i]) * 16777619u;
  return h;
}

static void pack_open(void) {
  pack_ready = true;
  (void)mkdir(dkgl_path("shadercache"), 0777);
  pack_file = fopen(DKGL_PACK_PATH, "r+b");
  uint32_t header[2] = {0, 0};
  if (pack_file && (fread(header, 1, 8, pack_file) != 8 ||
                    header[0] != DKGL_PACK_MAGIC ||
                    header[1] != DKGL_SHADER_VERSION)) {
    fclose(pack_file);
    pack_file = NULL;
  }
  if (!pack_file) {
    pack_file = fopen(DKGL_PACK_PATH, "w+b");
    if (!pack_file) {
      debugPrintf("dkgl: shader cache %s unavailable\n", DKGL_PACK_PATH);
      return;
    }
    header[0] = DKGL_PACK_MAGIC;
    header[1] = DKGL_SHADER_VERSION;
    fwrite(header, 1, 8, pack_file);
    fflush(pack_file);
    debugPrintf("dkgl: shader cache created\n");
    return;
  }
  /* An entry must end inside the file: seeking past the end succeeds, and
   * the truncate below would then pad a torn entry out with zeros (a
   * shader whose code tail, EXIT included, reads as zeros). Ryujinx loses
   * the last unflushed write when it is killed. */
  fseek(pack_file, 0, SEEK_END);
  const long file_size = ftell(pack_file);
  fseek(pack_file, 8, SEEK_SET);
  long valid_end = 8;
  for (;;) {
    uint32_t entry[5];
    if (fread(entry, 1, sizeof(entry), pack_file) != sizeof(entry))
      break;
    if (entry[0] != DKGL_ENTRY_MAGIC || entry[3] == 0 || entry[3] > (8u << 20))
      break;
    const long data = valid_end + (long)sizeof(entry);
    if (data + (long)entry[3] > file_size ||
        fseek(pack_file, (long)entry[3], SEEK_CUR) != 0)
      break;
    uint64_t hash = (uint64_t)entry[1] | ((uint64_t)entry[2] << 32);
    pack_add_index(hash, (uint32_t)data, entry[3], entry[4]);
    valid_end = data + (long)entry[3];
  }
  /* Drop a torn tail from an interrupted write. */
  fseek(pack_file, 0, SEEK_END);
  if (ftell(pack_file) != valid_end) {
    fflush(pack_file);
    if (ftruncate(fileno(pack_file), valid_end) != 0)
      debugPrintf("dkgl: shader cache truncate failed\n");
  }
  pack_mem = malloc((size_t)valid_end);
  if (pack_mem) {
    fseek(pack_file, 0, SEEK_SET);
    if (fread(pack_mem, 1, (size_t)valid_end, pack_file) == (size_t)valid_end) {
      pack_mem_size = valid_end;
    } else {
      free(pack_mem);
      pack_mem = NULL;
    }
  }
  fseek(pack_file, valid_end, SEEK_SET);
  debugPrintf("dkgl: shader cache loaded %u entries (%ld KiB)%s\n", pack_count,
              valid_end >> 10, pack_mem ? ", in memory" : "");
}

static void *pack_lookup(uint64_t hash, size_t *size) {
  if (!pack_ready)
    pack_open();
  if (!pack_file)
    return NULL;
  for (uint32_t i = pack_count; i-- > 0;) {
    if (pack_index[i].hash != hash)
      continue;
    void *data = malloc(pack_index[i].size);
    if (!data)
      return NULL;
    size_t got;
    if (pack_mem && (long)pack_index[i].offset + (long)pack_index[i].size <= pack_mem_size) {
      memcpy(data, pack_mem + pack_index[i].offset, pack_index[i].size);
      got = pack_index[i].size;
    } else {
      long here = ftell(pack_file);
      fseek(pack_file, (long)pack_index[i].offset, SEEK_SET);
      got = fread(data, 1, pack_index[i].size, pack_file);
      fseek(pack_file, here, SEEK_SET);
    }
    if (got != pack_index[i].size || pack_sum(data, got) != pack_index[i].sum) {
      debugPrintf("dkgl: shader cache entry %016llx is damaged: compiled again\n",
                  (unsigned long long)hash);
      pack_index[i].hash = 0;
      free(data);
      return NULL;
    }
    *size = got;
    return data;
  }
  return NULL;
}

static void pack_store(uint64_t hash, const void *dksh, size_t size) {
  if (!pack_file || !size)
    return;
  fseek(pack_file, 0, SEEK_END);
  long at = ftell(pack_file);
  const uint32_t sum = pack_sum(dksh, size);
  uint32_t entry[5] = {DKGL_ENTRY_MAGIC, (uint32_t)hash, (uint32_t)(hash >> 32),
                       (uint32_t)size, sum};
  if (fwrite(entry, 1, sizeof(entry), pack_file) != sizeof(entry) ||
      fwrite(dksh, 1, size, pack_file) != size || fflush(pack_file) != 0) {
    debugPrintf("dkgl: shader cache write failed\n");
    clearerr(pack_file);
    return;
  }
  fsync(fileno(pack_file)); /* fflush leaves it in the file system's buffers */
  pack_add_index(hash, (uint32_t)(at + (long)sizeof(entry)), (uint32_t)size, sum);
}

/* ------------------------------------------------------------- compile */

static uint64_t hash_source(const char *src, size_t len, int stage) {
  uint64_t h = 1469598103934665603ull ^ ((uint64_t)DKGL_SHADER_VERSION << 8) ^
               (uint64_t)stage;
  for (size_t i = 0; i < len; i++)
    h = (h ^ (uint8_t)src[i]) * 1099511628211ull;
  return h;
}

static int uam_stage(int stage) {
  return stage == DKGL_GLSL_VERTEX     ? UAM_STAGE_VERTEX
         : stage == DKGL_GLSL_FRAGMENT ? UAM_STAGE_FRAGMENT
                                       : UAM_STAGE_COMPUTE;
}

static void dump_failure(const char *original, const char *rewritten,
                         uint64_t hash, const char *log) {
  static int dumps;
  if (dumps >= 24)
    return;
  dumps++;
  char name[96], path[700];
  snprintf(name, sizeof(name), "dkgl-fail-%016llx.txt", (unsigned long long)hash);
  snprintf(path, sizeof(path), "%s", dkgl_path(name));
  FILE *f = fopen(path, "wb");
  if (!f)
    return;
  fprintf(f, "== log\n%s\n== rewritten\n%s\n== original\n%s\n", log ? log : "",
          rewritten ? rewritten : "", original ? original : "");
  fclose(f);
  debugPrintf("dkgl: shader failure written to %s\n", path);
}

typedef struct CompileJob {
  BinaryNode *node;
  char *rewritten; /* GLSL handed to uam */
  char *original;  /* UE's source, for dkgl-fail-*.txt */
  int flags;       /* UAM_FLAG_* */
  uint64_t key;    /* dkgl.pack key */
  struct CompileJob *next;
} CompileJob;

/* Particle shaders that render wrong on the console only (the emulator,
 * which ignores Maxwell's scheduling bits, draws them right), by
 * rewritten-source hash: UE's GPU particle system came out as white vertical
 * streaks (fixed by this in r62/r63 on hardware) and CPU sprite particles
 * (boost smoke) as black blocks. These cheap shaders are compiled with uam's
 * conservative scheduling (uam patch 0004), cached under a salted key.
 * dkgl_safe_sched_off in the runtime folder turns this off. */
static const uint64_t conservative_hashes[] = {
    0xd8d695af8520930eull, /* simulation VS */
    0xf552d4b82020ce4cull, /* simulation FS */
    0x77c4910e3965734dull, /* clear-tiles FS */
    0x44b615c17dc27098ull, /* injection VS */
    0x6cda8cc8f126c720ull, /* injection FS */
    0x100c2f62a71ebc97ull, /* sprite VS */
    0x166f818b8d18ea66ull, /* sprite FS */
    /* CPU sprite particles: the sprite VS and every FS seen with it */
    0x5db30af933b79406ull,
    0x95374723acd8d522ull, /* boost smoke */
    0x5276ad4b26cb87a7ull,
    0xb07c9e08b2fb5201ull,
    0x1f69e8a8735ad021ull,
    0x1faf527d763096caull,
    0xa028712d38fbaf53ull,
    0xe07eaf50adaa7ff7ull,
    0x62de7bf160528b38ull,
};
#define CONSERVATIVE_SALT 0x5afe5c4ed0c0ffeeull

static bool sched_conservative(uint64_t hash) {
  static int enabled = -1;
  if (enabled < 0) {
    FILE *f = fopen(dkgl_path("dkgl_safe_sched_off"), "rb");
    enabled = f == NULL;
    if (f)
      fclose(f);
    debugPrintf("dkgl: conservative scheduling for %u particle shaders %s\n",
                (unsigned)(sizeof(conservative_hashes) / sizeof(conservative_hashes[0])),
                enabled ? "on" : "off (dkgl_safe_sched_off)");
  }
  if (!enabled)
    return false;
  for (size_t i = 0; i < sizeof(conservative_hashes) / sizeof(conservative_hashes[0]); i++)
    if (conservative_hashes[i] == hash)
      return true;
  return false;
}

static Mutex job_mutex;
static CondVar job_cv;
static CompileJob *job_head, *job_tail;
static int job_queued;

static void compile_enqueue(CompileJob *job);

/* Compiles one stage (a fragment stage's inputs at the locations vary gives,
 * when not NULL). Returns a referenced binary or NULL with *log set. */
static DkglStageBinary *compile_stage_linked(const char *source, int stage, const DkglGlslLink *vary,
                                             char **log) {
  *log = NULL;
  DkglGlslResult *refl = calloc(1, sizeof(*refl));
  if (!refl)
    return NULL;
  const uint64_t t_rewrite = armGetSystemTick();
  const bool rewritten = vary ? dkgl_glsl_rewrite_linked(source, stage, vary, refl) : dkgl_glsl_rewrite(source, stage, refl);
  st_rewrite_ticks += armGetSystemTick() - t_rewrite;
  if (!rewritten) {
    char msg[320];
    snprintf(msg, sizeof(msg), "dkgl rewrite: %s\n", refl->error);
    *log = strdup(msg);
    dump_failure(source, NULL, 0, msg);
    free(refl);
    return NULL;
  }
  const uint64_t hash = hash_source(refl->source, refl->source_len, stage);

  mutexLock(&compile_mutex);
  BinaryNode *node = binary_find(hash);
  if (node) {
    st_memory++;
    node->bin.refs++;
    mutexUnlock(&compile_mutex);
    dkgl_glsl_free(refl);
    free(refl);
    return &node->bin;
  }
  size_t size = 0;
  const bool conservative = sched_conservative(hash);
  const uint64_t key = conservative ? hash ^ CONSERVATIVE_SALT : hash;
  const uint64_t t_lookup = armGetSystemTick();
  void *dksh = pack_lookup(key, &size);
  st_lookup_ticks += armGetSystemTick() - t_lookup;
  if (dksh)
    st_hits++;
  else
    st_compiled++;
  if (!dksh) {
    node = calloc(1, sizeof(*node));
    CompileJob *job = calloc(1, sizeof(*job));
    if (!node || !job) {
      free(node);
      free(job);
      mutexUnlock(&compile_mutex);
      dkgl_glsl_free(refl);
      free(refl);
      *log = strdup("dkgl: out of memory\n");
      return NULL;
    }
    node->bin.refs = 2; /* the caller's and the job's */
    node->bin.uid = ++binary_uids;
    node->bin.hash = hash;
    node->bin.stage = stage;
    node->bin.refl = refl;
    node->state = BIN_PENDING;
    job->node = node;
    job->flags = conservative ? UAM_FLAG_CONSERVATIVE_SCHED : 0;
    job->key = key;
    job->rewritten = refl->source;
    job->original = strdup(source);
    refl->source = NULL;
    node->next = binaries[hash & (BINARY_BUCKETS - 1)];
    binaries[hash & (BINARY_BUCKETS - 1)] = node;
    mutexUnlock(&compile_mutex);
    compile_enqueue(job);
    return &node->bin;
  }
  node = calloc(1, sizeof(*node));
  node->bin.refs = 1;
  node->bin.uid = ++binary_uids;
  node->bin.hash = hash;
  node->bin.stage = stage;
  node->bin.refl = refl;
  node->bin.dksh = dksh;
  node->bin.dksh_size = size;
  if (dkgl_trace_enabled) /* traced draws write it out by hash */
    node->bin.trace_source = refl->source;
  else
    free(refl->source); /* keep the reflection only */
  refl->source = NULL;
  node->next = binaries[hash & (BINARY_BUCKETS - 1)];
  binaries[hash & (BINARY_BUCKETS - 1)] = node;
  dkg.stat_cache_hits++;
  mutexUnlock(&compile_mutex);
  return &node->bin;
}

static DkglStageBinary *compile_stage(const char *source, int stage, char **log) {
  return compile_stage_linked(source, stage, NULL, log);
}

/* ------------------------------------------------------- compile worker */

static void *compile_worker(void *arg) {
  (void)arg;
  static char uam_log[4096];
  static uint32_t compiled;
  for (;;) {
    mutexLock(&job_mutex);
    while (!job_head)
      condvarWait(&job_cv, &job_mutex);
    CompileJob *job = job_head;
    job_head = job->next;
    if (!job_head)
      job_tail = NULL;
    job_queued--;
    mutexUnlock(&job_mutex);

    BinaryNode *node = job->node;
    const int stage = node->bin.stage;
    const uint64_t hash = node->bin.hash;
    void *dksh = NULL;
    size_t size = 0;
    uam_log[0] = '\0';
    const uint64_t t0 = armGetSystemTick();
    const bool ok = uam_bridge_compile2(uam_stage(stage), job->rewritten,
                                        job->flags, &dksh, &size, uam_log,
                                        sizeof(uam_log));
    const uint64_t ticks = armGetSystemTick() - t0;

    mutexLock(&compile_mutex);
    if (ok) {
      pack_store(job->key, dksh, size);
      node->bin.dksh = dksh;
      node->bin.dksh_size = size;
      if (dkgl_trace_enabled) {
        node->bin.trace_source = job->rewritten;
        job->rewritten = NULL;
      }
      dkg.stat_compiles++;
      dkg.stat_compile_ticks += ticks;
      __atomic_store_n(&node->state, BIN_READY, __ATOMIC_RELEASE);
    } else {
      dump_failure(job->original, job->rewritten, hash, uam_log);
      __atomic_store_n(&node->state, BIN_FAILED, __ATOMIC_RELEASE);
    }
    mutexUnlock(&compile_mutex);
    if (!ok)
      debugPrintf("dkgl: compile failed stage=%d hash=%016llx\n", stage,
                  (unsigned long long)hash);
    else if (++compiled <= 16 || (compiled % 256) == 0 || job->flags)
      debugPrintf("dkgl: compiled stage=%d hash=%016llx dksh=%zu in %llu us "
                  "(total %u, %d queued)%s%s%s\n",
                  stage, (unsigned long long)hash, size,
                  (unsigned long long)(ticks * 1000000ull / armGetSystemTickFreq()),
                  compiled, job_queued,
                  job->flags ? " conservative scheduling" : "",
                  uam_log[0] ? " warnings: " : "", uam_log);
    free(job->rewritten);
    free(job->original);
    free(job);
    /* The job's reference. If GL already dropped the others the node goes
     * now, and freeing loaded code touches GL-lock state; otherwise no GL
     * lock (a link waiting under it for this worker's next job would
     * otherwise hold the worker off). */
    mutexLock(&compile_mutex);
    const bool last = node->bin.refs <= 1;
    bool queued = false;
    if (!last)
      node->bin.refs--;
    else if (n_worker_released < WORKER_RELEASED_MAX) {
      worker_released[n_worker_released] = node; /* freed by the GL side */
      __atomic_store_n(&n_worker_released, n_worker_released + 1, __ATOMIC_RELEASE);
      queued = true;
    }
    mutexUnlock(&compile_mutex);
    if (last && !queued) {
      DKGL_LOCK();
      binary_release(&node->bin);
      DKGL_UNLOCK();
    }
  }
  return NULL;
}

static void compile_enqueue(CompileJob *job) {
  mutexLock(&job_mutex);
  if (job_tail)
    job_tail->next = job;
  else
    job_head = job;
  job_tail = job;
  job_queued++;
  condvarWakeOne(&job_cv);
  mutexUnlock(&job_mutex);
}

void dkgl_shaders_init(void) {
  /* Unity's ES 1.00 shaders: every loose uniform of a stage in one block */
  dkgl_glsl_loose_blocks = 1;
  mutexInit(&compile_mutex);
  mutexInit(&job_mutex);
  condvarInit(&job_cv);
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 2 * 1024 * 1024); /* like UE's threads */
  pthread_t thread;
  const int rc = pthread_create(&thread, &attr, compile_worker, NULL);
  pthread_attr_destroy(&attr);
  if (rc)
    fatal_error("dkgl: could not start the shader compile thread (%d)", rc);
}

/* ------------------------------------------------------------- shaders */

/* Shaders and programs share names; a program's type field is always 0. */
DkglShader *dkgl_shader(GLuint name) {
  void *obj = dkgl_name_get(DKGL_OBJ_SHADER_PROGRAM, name);
  return obj && ((DkglShader *)obj)->type ? obj : NULL;
}

DkglProgram *dkgl_program(GLuint name) {
  void *obj = dkgl_name_get(DKGL_OBJ_SHADER_PROGRAM, name);
  return obj && !((DkglShader *)obj)->type ? obj : NULL;
}

GLuint glCreateShader(GLenum type) {
  if (type != GL_VERTEX_SHADER && type != GL_FRAGMENT_SHADER &&
      type != GL_COMPUTE_SHADER) {
    if (dkgl_ctx())
      DKGL_ERROR(dkgl_ctx(), GL_INVALID_ENUM);
    return 0;
  }
  DKGL_LOCK();
  DkglShader *sh = calloc(1, sizeof(*sh));
  GLuint name = 0;
  if (sh) {
    name = dkgl_name_gen(DKGL_OBJ_SHADER_PROGRAM);
    sh->name = name;
    sh->type = type;
    dkgl_name_set(DKGL_OBJ_SHADER_PROGRAM, name, sh);
  }
  DKGL_UNLOCK();
  return name;
}

static void shader_free(DkglShader *sh) {
  binary_release(sh->binary);
  free(sh->source);
  free(sh->info_log);
  dkgl_name_free(DKGL_OBJ_SHADER_PROGRAM, sh->name);
  free(sh);
}

void glDeleteShader(GLuint shader) {
  if (!shader)
    return;
  DKGL_LOCK();
  DkglShader *sh = dkgl_shader(shader);
  if (sh) {
    if (sh->attached)
      sh->delete_pending = true;
    else
      shader_free(sh);
  }
  DKGL_UNLOCK();
}

GLboolean glIsShader(GLuint shader) {
  DKGL_LOCK();
  GLboolean r = dkgl_shader(shader) ? GL_TRUE : GL_FALSE;
  DKGL_UNLOCK();
  return r;
}

void glShaderSource(GLuint shader, GLsizei count, const GLchar *const *string,
                    const GLint *length) {
  DKGL_LOCK();
  DkglShader *sh = dkgl_shader(shader);
  if (!sh) {
    if (dkgl_ctx())
      DKGL_ERROR(dkgl_ctx(), GL_INVALID_VALUE);
    DKGL_UNLOCK();
    return;
  }
  size_t total = 0;
  for (GLsizei i = 0; i < count; i++)
    if (string[i])
      total += length && length[i] >= 0 ? (size_t)length[i] : strlen(string[i]);
  char *src = malloc(total + 1);
  if (src) {
    size_t at = 0;
    for (GLsizei i = 0; i < count; i++) {
      if (!string[i])
        continue;
      size_t n = length && length[i] >= 0 ? (size_t)length[i] : strlen(string[i]);
      memcpy(src + at, string[i], n);
      at += n;
    }
    src[at] = '\0';
  }
  free(sh->source);
  sh->source = src;
  DKGL_UNLOCK();
}

void glCompileShader(GLuint shader) {
  DKGL_LOCK();
  DkglShader *sh = dkgl_shader(shader);
  if (!sh || !sh->source) {
    if (sh) {
      sh->compiled = true;
      sh->compile_status = false;
    } else if (dkgl_ctx()) {
      DKGL_ERROR(dkgl_ctx(), GL_INVALID_VALUE);
    }
    DKGL_UNLOCK();
    return;
  }
  char *source = strdup(sh->source);
  const int stage = sh->type == GL_VERTEX_SHADER     ? DKGL_GLSL_VERTEX
                    : sh->type == GL_FRAGMENT_SHADER ? DKGL_GLSL_FRAGMENT
                                                     : DKGL_GLSL_COMPUTE;
  DKGL_UNLOCK();

#ifdef DEBUG_LOG
  static volatile uint32_t shown;
  if (__atomic_add_fetch(&shown, 1, __ATOMIC_RELAXED) <= 2) {
    /* UE adds its own prologue at run time; show it once per stage kind. */
    char head[640];
    snprintf(head, sizeof(head), "%s", source ? source : "");
    debugPrintf("dkgl: first shader source head (stage %d):\n%s\n", stage, head);
  }
#endif

  if (stage != DKGL_GLSL_COMPUTE && dkgl_glsl_is_es1(source)) {
    /* GLSL ES 1.00: its interface (attribute and varying locations) is
     * known at the link, which compiles it; here only a trial translation */
    char err[256] = "";
    DkglGlslLink none, vary;
    memset(&none, 0, sizeof(none));
    char *es3 = dkgl_glsl_es1(source, stage, &none, &vary, err, sizeof(err));
    DkglGlslResult *probe = es3 ? calloc(1, sizeof(*probe)) : NULL;
    bool ok = probe && dkgl_glsl_rewrite(es3, stage, probe);
    if (probe && !ok)
      snprintf(err, sizeof(err), "dkgl rewrite: %s", probe->error);
    if (ok)
      dkgl_glsl_free(probe);
    free(probe);
    free(es3);
    free(source);
    DKGL_LOCK();
    sh = dkgl_shader(shader);
    if (sh) {
      binary_release(sh->binary);
      sh->binary = NULL;
      sh->es1 = true;
      sh->compiled = true;
      sh->compile_status = ok;
      free(sh->info_log);
      sh->info_log = err[0] ? strdup(err) : NULL;
    }
    DKGL_UNLOCK();
    if (!ok)
      debugPrintf("dkgl: GLSL ES 1.00 shader %u does not translate: %s\n", shader, err);
    return;
  }

  char *log = NULL;
  DkglStageBinary *bin = source ? compile_stage(source, stage, &log) : NULL;
  free(source);

  DKGL_LOCK();
  sh = dkgl_shader(shader);
  if (sh) {
    binary_release(sh->binary);
    sh->binary = bin;
    sh->es1 = false;
    sh->compiled = true;
    sh->compile_status = bin != NULL;
    free(sh->info_log);
    sh->info_log = log;
  } else {
    binary_release(bin);
    free(log);
  }
  DKGL_UNLOCK();
}

void glGetShaderiv(GLuint shader, GLenum pname, GLint *params) {
  DKGL_LOCK();
  DkglShader *sh = dkgl_shader(shader);
  if (!sh) {
    if (dkgl_ctx())
      DKGL_ERROR(dkgl_ctx(), GL_INVALID_VALUE);
    DKGL_UNLOCK();
    return;
  }
  switch (pname) {
  case GL_SHADER_TYPE:
    *params = (GLint)sh->type;
    break;
  case GL_DELETE_STATUS:
    *params = sh->delete_pending;
    break;
  case GL_COMPILE_STATUS:
    *params = sh->compile_status;
    break;
  case GL_INFO_LOG_LENGTH:
    *params = sh->info_log ? (GLint)strlen(sh->info_log) + 1 : 0;
    break;
  case GL_SHADER_SOURCE_LENGTH:
    *params = sh->source ? (GLint)strlen(sh->source) + 1 : 0;
    break;
  default:
    DKGL_ERROR(dkgl_ctx(), GL_INVALID_ENUM);
    break;
  }
  DKGL_UNLOCK();
}

static void copy_string(const char *src, GLsizei bufSize, GLsizei *length,
                        GLchar *out) {
  GLsizei n = 0;
  if (bufSize > 0 && out) {
    if (src) {
      n = (GLsizei)strlen(src);
      if (n > bufSize - 1)
        n = bufSize - 1;
      memcpy(out, src, (size_t)n);
    }
    out[n] = '\0';
  }
  if (length)
    *length = n;
}

void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei *length,
                        GLchar *infoLog) {
  DKGL_LOCK();
  DkglShader *sh = dkgl_shader(shader);
  copy_string(sh ? sh->info_log : NULL, bufSize, length, infoLog);
  DKGL_UNLOCK();
}

void glGetShaderSource(GLuint shader, GLsizei bufSize, GLsizei *length,
                       GLchar *source) {
  DKGL_LOCK();
  DkglShader *sh = dkgl_shader(shader);
  copy_string(sh ? sh->source : NULL, bufSize, length, source);
  DKGL_UNLOCK();
}

void glShaderBinary(GLsizei count, const GLuint *shaders, GLenum binaryformat,
                    const void *binary, GLsizei length) {
  (void)count;
  (void)shaders;
  (void)binaryformat;
  (void)binary;
  (void)length;
  if (dkgl_ctx())
    DKGL_ERROR(dkgl_ctx(), GL_INVALID_ENUM);
}

void glReleaseShaderCompiler(void) {}

void glGetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype,
                                GLint *range, GLint *precision) {
  (void)shadertype;
  if (precisiontype == GL_LOW_INT || precisiontype == GL_MEDIUM_INT ||
      precisiontype == GL_HIGH_INT) {
    range[0] = 31;
    range[1] = 30;
    *precision = 0;
  } else {
    range[0] = 127;
    range[1] = 127;
    *precision = 23;
  }
}

/* ------------------------------------------------------------ programs */

GLuint glCreateProgram(void) {
  DKGL_LOCK();
  DkglProgram *prog = calloc(1, sizeof(*prog));
  GLuint name = 0;
  if (prog) {
    name = dkgl_name_gen(DKGL_OBJ_SHADER_PROGRAM);
    prog->name = name;
    dkgl_name_set(DKGL_OBJ_SHADER_PROGRAM, name, prog);
  }
  DKGL_UNLOCK();
  return name;
}

static void program_unlink(DkglProgram *prog) {
  for (int s = 0; s < DKGL_STAGES; s++) {
    binary_release(prog->bin[s]);
    prog->bin[s] = NULL;
    for (int b = 0; b < 16; b++) {
      free(prog->ubo[s][b].shadow);
      memset(&prog->ubo[s][b], 0, sizeof(prog->ubo[s][b]));
    }
    prog->num_ubo[s] = 0;
    prog->num_tex[s] = prog->num_img[s] = 0;
  }
  free(prog->slots);
  prog->slots = NULL;
  prog->num_slots = 0;
  prog->input_mask = 0;
  for (int i = 0; i < 32; i++)
    prog->attr_index[i] = (uint8_t)i;
  prog->link_status = false;
  prog->blk_ready = false;
}

/* The program-wide uniform block list (see DkglProgram.blk_index). */
static void program_blocks(DkglProgram *prog) {
  if (prog->blk_ready)
    return;
  const char *names[16];
  int n = 0;
  for (int s = 0; s < DKGL_STAGES; s++) {
    for (int k = 0; k < 16; k++)
      prog->blk_index[s][k] = -1;
    if (!prog->bin[s])
      continue;
    const DkglGlslResult *r = prog->bin[s]->refl;
    for (int k = 0; k < r->num_blocks && k < 16; k++) {
      int i = 0;
      while (i < n && strcmp(names[i], r->blocks[k].name))
        i++;
      if (i == n && n < 16) {
        names[n++] = r->blocks[k].name;
        /* GL: a block's binding point starts as its layout(binding = n),
         * else 0. Unity on ES 3.1+ declares them (UNITY_BINDING) and reads
         * them back (GL_UNIFORM_BLOCK_BINDING) instead of setting them. */
        prog->ubo_block_binding[i] = r->blocks[k].gl_binding >= 0 ? (uint32_t)r->blocks[k].gl_binding : 0;
      }
      prog->blk_index[s][k] = (int8_t)(i < 16 ? i : -1);
    }
  }
  prog->num_blocks = (uint8_t)n;
  /* members, each name once (UnityPerDraw is in both stages) */
  int nm = 0;
  for (int s = 0; s < DKGL_STAGES; s++) {
    if (!prog->bin[s])
      continue;
    const DkglGlslResult *r = prog->bin[s]->refl;
    for (int k = 0; k < r->num_members; k++) {
      const DkglGlslMember *m = &r->members[k];
      const int blk = m->block < 16 ? prog->blk_index[s][m->block] : -1;
      if (blk < 0)
        continue;
      int i = 0;
      while (i < nm && strcmp(prog->mem[i]->name, m->name))
        i++;
      if (i == nm && nm < DKGL_GLSL_MAX_MEMBERS) {
        prog->mem[nm] = m;
        prog->mem_block[nm] = (int8_t)blk;
        nm++;
      }
    }
  }
  prog->num_mem = (uint8_t)nm;
  prog->blk_ready = true;
}

/* stage block of program block 'index' (first stage that has it) */
static const DkglGlslBlock *program_block(DkglProgram *prog, GLuint index, int *stage_mask) {
  const DkglGlslBlock *found = NULL;
  if (stage_mask)
    *stage_mask = 0;
  program_blocks(prog);
  for (int s = 0; s < DKGL_STAGES; s++) {
    if (!prog->bin[s])
      continue;
    const DkglGlslResult *r = prog->bin[s]->refl;
    for (int k = 0; k < r->num_blocks && k < 16; k++)
      if (prog->blk_index[s][k] == (int)index) {
        if (!found || r->blocks[k].size > found->size)
          found = &r->blocks[k];
        if (stage_mask)
          *stage_mask |= 1 << s;
      }
  }
  return found;
}

void dkgl_program_release(DkglProgram *prog) {
  program_unlink(prog);
  for (int i = 0; i < prog->num_shaders; i++) {
    DkglShader *sh = dkgl_shader(prog->shaders[i]);
    if (sh && sh->attached && --sh->attached == 0 && sh->delete_pending)
      shader_free(sh);
  }
  free(prog->info_log);
  dkgl_name_free(DKGL_OBJ_SHADER_PROGRAM, prog->name);
  free(prog);
}

void glDeleteProgram(GLuint program) {
  if (!program)
    return;
  DKGL_LOCK();
  DkglProgram *prog = dkgl_program(program);
  DkglContext *ctx = dkgl_ctx();
  if (prog) {
    if (ctx && ctx->program == program)
      prog->delete_pending = true;
    else
      dkgl_program_release(prog);
  }
  DKGL_UNLOCK();
}

GLboolean glIsProgram(GLuint program) {
  DKGL_LOCK();
  GLboolean r = dkgl_program(program) ? GL_TRUE : GL_FALSE;
  DKGL_UNLOCK();
  return r;
}

void glAttachShader(GLuint program, GLuint shader) {
  DKGL_LOCK();
  DkglProgram *prog = dkgl_program(program);
  DkglShader *sh = dkgl_shader(shader);
  if (!prog || !sh || prog->num_shaders >= 4) {
    if (dkgl_ctx())
      DKGL_ERROR(dkgl_ctx(), GL_INVALID_OPERATION);
    DKGL_UNLOCK();
    return;
  }
  for (int i = 0; i < prog->num_shaders; i++)
    if (prog->shaders[i] == shader) {
      DKGL_UNLOCK();
      return;
    }
  prog->shaders[prog->num_shaders++] = shader;
  sh->attached++;
  DKGL_UNLOCK();
}

void glDetachShader(GLuint program, GLuint shader) {
  DKGL_LOCK();
  DkglProgram *prog = dkgl_program(program);
  if (prog) {
    for (int i = 0; i < prog->num_shaders; i++) {
      if (prog->shaders[i] != shader)
        continue;
      prog->shaders[i] = prog->shaders[--prog->num_shaders];
      DkglShader *sh = dkgl_shader(shader);
      if (sh && sh->attached && --sh->attached == 0 && sh->delete_pending)
        shader_free(sh);
      break;
    }
  }
  DKGL_UNLOCK();
}

void glGetAttachedShaders(GLuint program, GLsizei maxCount, GLsizei *count,
                          GLuint *shaders) {
  DKGL_LOCK();
  DkglProgram *prog = dkgl_program(program);
  GLsizei n = 0;
  if (prog)
    for (int i = 0; i < prog->num_shaders && n < maxCount; i++)
      shaders[n++] = prog->shaders[i];
  if (count)
    *count = n;
  DKGL_UNLOCK();
}

void glBindAttribLocation(GLuint program, GLuint index, const GLchar *name) {
  DKGL_LOCK();
  DkglProgram *prog = dkgl_program(program);
  if (prog && name) {
    int slot = -1;
    for (int i = 0; i < prog->num_attrib_binds; i++)
      if (!strcmp(prog->attrib_binds[i].name, name))
        slot = i;
    if (slot < 0 && prog->num_attrib_binds < 16)
      slot = prog->num_attrib_binds++;
    if (slot >= 0) {
      snprintf(prog->attrib_binds[slot].name, DKGL_GLSL_NAME, "%s", name);
      prog->attrib_binds[slot].index = index;
    }
    static int logged;
    if (logged < 24) {
      logged++;
      debugPrintf("dkgl: glBindAttribLocation(%u, %u, %s)\n", program, index, name);
    }
  }
  DKGL_UNLOCK();
}

static int find_slot(DkglProgram *prog, const char *name, size_t len) {
  for (int i = 0; i < prog->num_slots; i++)
    if (strlen(prog->slots[i].name) == len &&
        !memcmp(prog->slots[i].name, name, len))
      return i;
  return -1;
}

static int add_slot(DkglProgram *prog, const char *name, int cap) {
  int i = find_slot(prog, name, strlen(name));
  if (i >= 0)
    return i;
  if (prog->num_slots >= cap)
    return -1;
  i = prog->num_slots++;
  DkglUniformSlot *s = &prog->slots[i];
  memset(s, 0, sizeof(*s));
  snprintf(s->name, sizeof(s->name), "%s", name);
  for (int st = 0; st < DKGL_STAGES; st++)
    s->at[st].stage = -1;
  s->explicit_loc = -1;
  return i;
}

static bool link_stages(DkglProgram *prog, DkglStageBinary **stages,
                        bool compute, char *log, size_t log_size);

/* A GLSL ES 1.00 pair: translated with the link's attribute bindings and
 * the vertex stage's varyings, then compiled (or found in the cache) like
 * any ES 3.x stage. The binaries come back referenced. */
static bool link_es1(DkglProgram *prog, DkglShader *vs, DkglShader *fs,
                     DkglStageBinary **out, char *log, size_t log_size) {
  DkglGlslLink attrs, vary;
  memset(&attrs, 0, sizeof(attrs));
  for (int i = 0; i < prog->num_attrib_binds && i < 16; i++) {
    snprintf(attrs.attrib_name[i], DKGL_GLSL_NAME, "%s", prog->attrib_binds[i].name);
    attrs.attrib_index[i] = (int)prog->attrib_binds[i].index;
    attrs.num_attribs++;
  }
  char err[256] = "";
  char *v3 = dkgl_glsl_es1(vs->source, DKGL_GLSL_VERTEX, &attrs, &vary, err, sizeof(err));
  char *f3 = v3 ? dkgl_glsl_es1(fs->source, DKGL_GLSL_FRAGMENT, &vary, NULL, err, sizeof(err)) : NULL;
  if (!v3 || !f3) {
    snprintf(log, log_size, "GLSL ES 1.00 translation: %s\n", err);
    free(v3);
    return false;
  }
  char *clog = NULL;
  out[0] = compile_stage(v3, DKGL_GLSL_VERTEX, &clog);
  if (out[0]) {
    free(clog);
    clog = NULL;
    out[1] = compile_stage(f3, DKGL_GLSL_FRAGMENT, &clog);
  }
  free(v3);
  free(f3);
  if (!out[0] || !out[1]) {
    snprintf(log, log_size, "%s", clog ? clog : "compile failed\n");
    free(clog);
    binary_release(out[0]);
    out[0] = out[1] = NULL;
    return false;
  }
  free(clog);
  /* Compiled here, as a driver compiling at the link would (Unity links
   * while it loads): its draws never find the code missing. */
  const uint64_t t0 = armGetSystemTick();
  for (int s = 0; s < 2; s++)
    while (__atomic_load_n(&node_of(out[s])->state, __ATOMIC_ACQUIRE) == BIN_PENDING &&
           armTicksToNs(armGetSystemTick() - t0) < 10000000000ull)
      svcSleepThread(100000);
  return true;
}

static bool program_link(DkglProgram *prog, char *log, size_t log_size) {
  DkglStageBinary *stages[DKGL_STAGES] = {NULL, NULL};
  bool compute = false;
  DkglShader *es1[2] = {NULL, NULL};
  for (int i = 0; i < prog->num_shaders; i++) {
    DkglShader *sh = dkgl_shader(prog->shaders[i]);
    if (sh && sh->es1)
      es1[sh->type == GL_VERTEX_SHADER ? 0 : 1] = sh;
  }
  if (es1[0] || es1[1]) {
    if (!es1[0] || !es1[1] || !es1[0]->compile_status || !es1[1]->compile_status) {
      snprintf(log, log_size, "a GLSL ES 1.00 program needs both stages compiled\n");
      return false;
    }
    DkglStageBinary *own[2] = {NULL, NULL};
    if (!link_es1(prog, es1[0], es1[1], own, log, log_size))
      return false;
    const bool ok = link_stages(prog, own, false, log, log_size);
    /* the code into GPU memory now, while the engine loads, rather than at
     * the first draw in the middle of a frame */
    if (ok) {
      binary_load(node_of(own[0]));
      binary_load(node_of(own[1]));
    }
    binary_release(own[0]); /* the program holds its own references */
    binary_release(own[1]);
    return ok;
  }
  for (int i = 0; i < prog->num_shaders; i++) {
    DkglShader *sh = dkgl_shader(prog->shaders[i]);
    if (!sh)
      continue;
    if (!sh->compile_status || !sh->binary) {
      snprintf(log, log_size, "attached shader %u is not compiled\n", sh->name);
      return false;
    }
    if (sh->type == GL_COMPUTE_SHADER) {
      stages[0] = sh->binary;
      compute = true;
    } else if (sh->type == GL_VERTEX_SHADER) {
      stages[0] = sh->binary;
    } else {
      stages[1] = sh->binary;
    }
  }
  if (!stages[0] || (!compute && !stages[1]) || (compute && stages[1])) {
    snprintf(log, log_size, "program needs a vertex+fragment or compute shader\n");
    return false;
  }
  /* GLSL ES 3 links varyings by name; the rewriter numbered each stage's in
   * declaration order. When the fragment stage reads a subset (Unity's
   * PlayerShader: vs_INTERP0, 1, 3, 7 of the vertex stage's 0..8), its
   * inputs are re-numbered after the vertex stage's outputs in a variant of
   * its own (HW round 3: the players' textures came out scrambled). */
  DkglStageBinary *own_fs = NULL;
  if (!compute) {
    const DkglGlslResult *vr = stages[0]->refl, *fr = stages[1]->refl;
    bool differ = false;
    for (int i = 0; vr && fr && i < fr->num_varyings && !differ; i++) {
      int at = -1;
      for (int j = 0; j < vr->num_varyings && at < 0; j++)
        if (!strcmp(vr->varyings[j].name, fr->varyings[i].name))
          at = vr->varyings[j].location;
      if (at >= 0 && at != fr->varyings[i].location)
        differ = true;
      for (int j = 0; at < 0 && j < vr->num_varyings; j++)
        if (vr->varyings[j].location == fr->varyings[i].location)
          differ = true; /* reads another name's output */
    }
    if (differ) {
      DkglShader *fsh = NULL;
      for (int i = 0; i < prog->num_shaders && !fsh; i++) {
        DkglShader *sh = dkgl_shader(prog->shaders[i]);
        if (sh && sh->type == GL_FRAGMENT_SHADER)
          fsh = sh;
      }
      DkglGlslLink *vary = calloc(1, sizeof(*vary));
      if (fsh && fsh->source && vary) {
        for (int j = 0; j < vr->num_varyings && j < DKGL_GLSL_MAX_VARYINGS; j++) {
          snprintf(vary->varying_name[j], sizeof(vary->varying_name[j]), "%s", vr->varyings[j].name);
          vary->varying_location[j] = vr->varyings[j].location;
          vary->num_varyings = j + 1;
        }
        char *clog = NULL;
        own_fs = compile_stage_linked(fsh->source, DKGL_GLSL_FRAGMENT, vary, &clog);
        if (!own_fs) {
          snprintf(log, log_size, "%s", clog ? clog : "fragment varyings: compile failed\n");
          free(clog);
          free(vary);
          return false;
        }
        free(clog);
        stages[1] = own_fs;
        static int logged;
        if (logged++ < 16)
          debugPrintf("dkgl: program %u: fragment inputs follow the vertex outputs by name\n", prog->name);
      }
      free(vary);
    }
  }
  const bool ok = link_stages(prog, stages, compute, log, log_size);
  if (own_fs) {
    if (ok)
      binary_load(node_of(own_fs));
    binary_release(own_fs); /* the program holds its own reference */
  }
  return ok;
}

static bool link_stages(DkglProgram *prog, DkglStageBinary **stages,
                        bool compute, char *log, size_t log_size) {
  /* Linking needs only the reflection. A stage still compiling on the
   * worker links fine and its code is loaded at the first draw that finds
   * it ready; only a failed compile fails the link. */
  for (int s = 0; s < DKGL_STAGES; s++)
    if (stages[s] &&
        __atomic_load_n(&node_of(stages[s])->state, __ATOMIC_ACQUIRE) == BIN_FAILED) {
      snprintf(log, log_size, "stage %d failed to compile\n", s);
      return false;
    }

  int cap = 0;
  for (int s = 0; s < DKGL_STAGES; s++)
    if (stages[s])
      cap += stages[s]->refl->num_uniforms + stages[s]->refl->num_samplers +
             stages[s]->refl->num_images;
  prog->slots = calloc(cap ? (size_t)cap : 1, sizeof(DkglUniformSlot));
  if (!prog->slots) {
    snprintf(log, log_size, "out of memory\n");
    return false;
  }
  prog->compute = compute;
  for (int s = 0; s < DKGL_STAGES; s++) {
    DkglStageBinary *bin = stages[s];
    if (!bin)
      continue;
    binary_ref(bin);
    prog->bin[s] = bin;
    const DkglGlslResult *r = bin->refl;
    prog->num_ubo[s] = r->num_ubos;
    for (int b = 0; b < r->num_ubos; b++) {
      prog->ubo[s][b].size = r->ubo_size[b];
      prog->ubo[s][b].shadow = calloc(1, r->ubo_size[b] ? r->ubo_size[b] : 16);
      prog->ubo[s][b].dirty = true;
    }
    for (int u = 0; u < r->num_uniforms; u++) {
      const DkglGlslUniform *un = &r->uniforms[u];
      int i = add_slot(prog, un->name, cap);
      if (i < 0)
        continue;
      DkglUniformSlot *slot = &prog->slots[i];
      slot->kind = DKGL_SLOT_UNIFORM;
      slot->gl_type = un->gl_type;
      slot->base = un->base;
      slot->columns = un->columns;
      slot->rows = un->rows;
      slot->array_size = un->array_size;
      slot->at[s].stage = (int8_t)s;
      slot->at[s].binding = un->binding;
      slot->at[s].offset = un->offset;
      slot->at[s].stride = un->stride;
      slot->at[s].size = un->size;
    }
    for (int k = 0; k < r->num_samplers + r->num_images; k++) {
      const bool image = k >= r->num_samplers;
      const DkglGlslSampler *sm =
          image ? &r->images[k - r->num_samplers] : &r->samplers[k];
      int i = add_slot(prog, sm->name, cap);
      if (i < 0)
        continue;
      DkglUniformSlot *slot = &prog->slots[i];
      slot->kind = image ? DKGL_SLOT_IMAGE : DKGL_SLOT_SAMPLER;
      slot->gl_type = sm->gl_type;
      slot->array_size = sm->array_size;
      if (sm->gl_location >= 0)
        slot->explicit_loc = sm->gl_location;
      slot->at[s].stage = (int8_t)s;
      slot->at[s].binding = sm->binding;
      for (uint32_t e = 0; e < sm->array_size && e < 8; e++) {
        if (image)
          slot->units[e] = (uint8_t)(sm->binding + e);
        const uint32_t b = sm->binding + e;
        if (image && b < 8) {
          prog->img[s][b].slot = (int16_t)i;
          prog->img[s][b].element = (uint8_t)e;
          prog->img[s][b].target = sm->target;
          if ((int)b + 1 > prog->num_img[s])
            prog->num_img[s] = (int)b + 1;
        } else if (!image && b < 32) {
          prog->tex[s][b].slot = (int16_t)i;
          prog->tex[s][b].element = (uint8_t)e;
          prog->tex[s][b].target = sm->target;
          if ((int)b + 1 > prog->num_tex[s])
            prog->num_tex[s] = (int)b + 1;
        }
      }
    }
    if (s == 0 && !compute) {
      for (int a = 0; a < r->num_inputs; a++) {
        const DkglGlslInput *in = &r->inputs[a];
        int bound = -1;
        for (int i = 0; i < prog->num_attrib_binds; i++)
          if (!strcmp(prog->attrib_binds[i].name, in->name))
            bound = (int)prog->attrib_binds[i].index;
        for (int k = 0; k < in->slots; k++)
          if (in->location + k >= 0 && in->location + k < 32) {
            const int loc = in->location + k;
            prog->input_mask |= 1u << loc;
            prog->input_base[loc] = in->base;
            if (bound >= 0 && bound + k < DKGL_ATTRIBS)
              prog->attr_index[loc] = (uint8_t)(bound + k);
          }
      }
    }
  }
  /* unused texture binding entries point at no slot */
  for (int s = 0; s < DKGL_STAGES; s++) {
    for (int b = 0; b < prog->num_tex[s]; b++)
      if (!prog->bin[s] || b >= prog->num_tex[s])
        prog->tex[s][b].slot = -1;
  }
  prog->version++;
  return true;
}

void glLinkProgram(GLuint program) {
  DKGL_LOCK();
  DkglProgram *prog = dkgl_program(program);
  if (!prog) {
    if (dkgl_ctx())
      DKGL_ERROR(dkgl_ctx(), GL_INVALID_VALUE);
    DKGL_UNLOCK();
    return;
  }
  program_unlink(prog);
  /* texture bindings default to "no sampler" until the stages fill them */
  for (int s = 0; s < DKGL_STAGES; s++) {
    for (int b = 0; b < 32; b++)
      prog->tex[s][b].slot = -1;
    for (int b = 0; b < 8; b++)
      prog->img[s][b].slot = -1;
  }
  char log[256] = "";
  prog->link_status = program_link(prog, log, sizeof(log));
  free(prog->info_log);
  prog->info_log = log[0] ? strdup(log) : NULL;
  if (!prog->link_status) {
    debugPrintf("dkgl: link failed program=%u: %s", program, log);
    program_unlink(prog);
  }
  DkglContext *ctx = dkgl_ctx();
  if (ctx && ctx->program == program)
    ctx->dirty |= DKGL_DIRTY_PROGRAM | DKGL_DIRTY_TEXTURES |
                  DKGL_DIRTY_IMAGES | DKGL_DIRTY_VERTEX;
  DKGL_UNLOCK();
}

void glUseProgram(GLuint program) {
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  if (ctx && ctx->program != program) {
    DkglProgram *old = dkgl_program(ctx->program);
    if (old && old->delete_pending)
      dkgl_program_release(old);
    ctx->program = program;
    ctx->dirty |= DKGL_DIRTY_PROGRAM | DKGL_DIRTY_TEXTURES |
                  DKGL_DIRTY_IMAGES | DKGL_DIRTY_VERTEX;
  }
  DKGL_UNLOCK();
}

void glValidateProgram(GLuint program) {
  DKGL_LOCK();
  DkglProgram *prog = dkgl_program(program);
  if (prog)
    prog->validate_status = prog->link_status;
  DKGL_UNLOCK();
}

void glProgramParameteri(GLuint program, GLenum pname, GLint value) {
  (void)program;
  (void)pname;
  (void)value;
}

void glGetProgramBinary(GLuint program, GLsizei bufSize, GLsizei *length,
                        GLenum *binaryFormat, void *binary) {
  (void)program;
  (void)bufSize;
  (void)binary;
  if (length)
    *length = 0;
  if (binaryFormat)
    *binaryFormat = 0;
  if (dkgl_ctx())
    DKGL_ERROR(dkgl_ctx(), GL_INVALID_OPERATION);
}

void glProgramBinary(GLuint program, GLenum binaryFormat, const void *binary,
                     GLsizei length) {
  (void)binaryFormat;
  (void)binary;
  (void)length;
  DKGL_LOCK();
  DkglProgram *prog = dkgl_program(program);
  if (prog) {
    program_unlink(prog);
    free(prog->info_log);
    prog->info_log = strdup("program binaries are not supported\n");
  }
  if (dkgl_ctx())
    DKGL_ERROR(dkgl_ctx(), GL_INVALID_ENUM);
  DKGL_UNLOCK();
}

static size_t max_name_length(DkglProgram *prog, int kind) {
  size_t m = 0;
  for (int i = 0; i < prog->num_slots; i++) {
    if (kind >= 0 && prog->slots[i].kind != kind)
      continue;
    size_t n = strlen(prog->slots[i].name) + 1 +
               (prog->slots[i].array_size > 1 ? 3 : 0);
    if (n > m)
      m = n;
  }
  return m;
}

void glGetProgramiv(GLuint program, GLenum pname, GLint *params) {
  DKGL_LOCK();
  DkglProgram *prog = dkgl_program(program);
  if (!prog) {
    if (dkgl_ctx())
      DKGL_ERROR(dkgl_ctx(), GL_INVALID_VALUE);
    DKGL_UNLOCK();
    return;
  }
  const DkglGlslResult *vs = prog->bin[0] ? prog->bin[0]->refl : NULL;
  switch (pname) {
  case GL_LINK_STATUS:
    *params = prog->link_status;
    break;
  case GL_VALIDATE_STATUS:
    *params = prog->validate_status;
    break;
  case GL_DELETE_STATUS:
    *params = prog->delete_pending;
    break;
  case GL_INFO_LOG_LENGTH:
    *params = prog->info_log ? (GLint)strlen(prog->info_log) + 1 : 0;
    break;
  case GL_ATTACHED_SHADERS:
    *params = prog->num_shaders;
    break;
  case GL_ACTIVE_UNIFORMS:
    program_blocks(prog);
    *params = prog->num_slots + prog->num_mem;
    break;
  case GL_ACTIVE_UNIFORM_MAX_LENGTH: {
    program_blocks(prog);
    GLint m = (GLint)max_name_length(prog, -1);
    for (int i = 0; i < prog->num_mem; i++)
      if ((GLint)strlen(prog->mem[i]->name) + 4 > m)
        m = (GLint)strlen(prog->mem[i]->name) + 4;
    *params = m;
    break;
  }
  case GL_ACTIVE_ATTRIBUTES:
    *params = vs && !prog->compute ? vs->num_inputs : 0;
    break;
  case GL_ACTIVE_ATTRIBUTE_MAX_LENGTH: {
    GLint m = 0;
    for (int i = 0; vs && i < vs->num_inputs; i++)
      if ((GLint)strlen(vs->inputs[i].name) + 1 > m)
        m = (GLint)strlen(vs->inputs[i].name) + 1;
    *params = m;
    break;
  }
  case GL_ACTIVE_UNIFORM_BLOCKS:
    program_blocks(prog);
    *params = prog->num_blocks;
    break;
  case GL_ACTIVE_UNIFORM_BLOCK_MAX_NAME_LENGTH:
    *params = DKGL_GLSL_NAME;
    break;
  case GL_PROGRAM_BINARY_LENGTH:
  case GL_TRANSFORM_FEEDBACK_VARYINGS:
  case GL_TRANSFORM_FEEDBACK_VARYING_MAX_LENGTH:
  case GL_PROGRAM_SEPARABLE:
  case GL_PROGRAM_BINARY_RETRIEVABLE_HINT:
    *params = 0;
    break;
  case GL_TRANSFORM_FEEDBACK_BUFFER_MODE:
    *params = GL_INTERLEAVED_ATTRIBS;
    break;
  case GL_COMPUTE_WORK_GROUP_SIZE:
    for (int i = 0; i < 3; i++)
      params[i] = prog->compute && vs ? vs->local_size[i] : 0;
    break;
  default:
    DKGL_ERROR(dkgl_ctx(), GL_INVALID_ENUM);
    break;
  }
  DKGL_UNLOCK();
}

void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei *length,
                         GLchar *infoLog) {
  DKGL_LOCK();
  DkglProgram *prog = dkgl_program(program);
  copy_string(prog ? prog->info_log : NULL, bufSize, length, infoLog);
  DKGL_UNLOCK();
}

GLint glGetAttribLocation(GLuint program, const GLchar *name) {
  DKGL_LOCK();
  DkglProgram *prog = dkgl_program(program);
  GLint loc = -1;
  if (prog && prog->link_status && prog->bin[0] && !prog->compute && name) {
    const DkglGlslResult *r = prog->bin[0]->refl;
    for (int i = 0; i < r->num_inputs; i++)
      if (!strcmp(r->inputs[i].name, name) && r->inputs[i].location >= 0 &&
          r->inputs[i].location < 32)
        loc = prog->attr_index[r->inputs[i].location]; /* the bound index, as GL reports it */
  }
  DKGL_UNLOCK();
  return loc;
}

void glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize,
                       GLsizei *length, GLint *size, GLenum *type,
                       GLchar *name) {
  DKGL_LOCK();
  DkglProgram *prog = dkgl_program(program);
  const DkglGlslResult *r = prog && prog->bin[0] && !prog->compute
                                ? prog->bin[0]->refl
                                : NULL;
  if (!r || index >= (GLuint)r->num_inputs) {
    if (dkgl_ctx())
      DKGL_ERROR(dkgl_ctx(), GL_INVALID_VALUE);
    DKGL_UNLOCK();
    return;
  }
  copy_string(r->inputs[index].name, bufSize, length, name);
  if (size)
    *size = 1;
  if (type)
    *type = r->inputs[index].gl_type;
  DKGL_UNLOCK();
}

GLint glGetUniformLocation(GLuint program, const GLchar *name) {
  DKGL_LOCK();
  DkglProgram *prog = dkgl_program(program);
  GLint loc = -1;
  if (prog && prog->link_status && name) {
    size_t len = strlen(name);
    uint32_t element = 0;
    const char *bracket = strchr(name, '[');
    if (bracket) {
      element = (uint32_t)strtoul(bracket + 1, NULL, 10);
      len = (size_t)(bracket - name);
    }
    int i = find_slot(prog, name, len);
    if (i >= 0 && element < prog->slots[i].array_size && element < 4096)
      loc = prog->slots[i].explicit_loc >= 0 ? prog->slots[i].explicit_loc + (GLint)element
                                             : (GLint)(((uint32_t)(i + 1) << 12) | element);
  }
  {
    /* mvl_nx (HW round 2): does Unity set UnityPerDraw's members as plain
     * uniforms? */
    static int n;
    if (name && n < 24 && (strstr(name, "ObjectToWorld") || strstr(name, "_BaseColor"))) {
      n++;
      debugPrintf("dkgl: glGetUniformLocation(%u, %s) -> %d\n", program, name, loc);
    }
  }
  DKGL_UNLOCK();
  return loc;
}

void glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize,
                        GLsizei *length, GLint *size, GLenum *type,
                        GLchar *name) {
  DKGL_LOCK();
  DkglProgram *prog = dkgl_program(program);
  if (prog)
    program_blocks(prog);
  if (prog && index >= (GLuint)prog->num_slots && index < (GLuint)(prog->num_slots + prog->num_mem)) {
    /* a uniform block member (mvl_nx) */
    const DkglGlslMember *m = prog->mem[index - (GLuint)prog->num_slots];
    {
      static int n;
      if (n < 60 && (strstr(m->name, "Color") || strstr(m->name, "Powerup") || strstr(m->name, "Eye"))) {
        n++;
        debugPrintf("dkgl: glGetActiveUniform(%u, #%u) -> %s (block %d, %d slots + %d members)\n", program, index,
                    m->name, prog->mem_block[index - (GLuint)prog->num_slots], prog->num_slots, prog->num_mem);
      }
    }
    char tmp[DKGL_GLSL_NAME + 4];
    snprintf(tmp, sizeof(tmp), "%s%s", m->name, m->array_stride ? "[0]" : "");
    copy_string(tmp, bufSize, length, name);
    if (size)
      *size = (GLint)m->array_size;
    if (type)
      *type = m->gl_type;
    DKGL_UNLOCK();
    return;
  }
  if (!prog || index >= (GLuint)prog->num_slots) {
    if (dkgl_ctx())
      DKGL_ERROR(dkgl_ctx(), GL_INVALID_VALUE);
    DKGL_UNLOCK();
    return;
  }
  const DkglUniformSlot *s = &prog->slots[index];
  char tmp[DKGL_GLSL_NAME + 4];
  snprintf(tmp, sizeof(tmp), "%s%s", s->name, s->array_size > 1 ? "[0]" : "");
  copy_string(tmp, bufSize, length, name);
  if (size)
    *size = (GLint)s->array_size;
  if (type)
    *type = s->gl_type;
  DKGL_UNLOCK();
}

/* GL's active uniform index from a name ("a", "a[0]"): slots, then block
 * members (mvl_nx) */
static GLuint active_uniform_index(DkglProgram *prog, const char *name) {
  size_t len = strlen(name);
  const char *bracket = strchr(name, '[');
  if (bracket && !strcmp(bracket, "[0]"))
    len = (size_t)(bracket - name);
  int i = find_slot(prog, name, len);
  if (i >= 0)
    return (GLuint)i;
  for (int k = 0; k < prog->num_mem; k++)
    if (strlen(prog->mem[k]->name) == len && !strncmp(prog->mem[k]->name, name, len))
      return (GLuint)(prog->num_slots + k);
  return GL_INVALID_INDEX;
}

void glGetUniformIndices(GLuint program, GLsizei count, const GLchar *const *names, GLuint *indices) {
  DKGL_LOCK();
  DkglProgram *prog = dkgl_program(program);
  if (prog)
    program_blocks(prog);
  for (GLsizei i = 0; i < count && indices; i++)
    indices[i] = prog && names && names[i] ? active_uniform_index(prog, names[i]) : GL_INVALID_INDEX;
  DKGL_UNLOCK();
}

void glGetActiveUniformsiv(GLuint program, GLsizei count, const GLuint *indices, GLenum pname, GLint *params) {
  DKGL_LOCK();
  DkglProgram *prog = dkgl_program(program);
  if (prog)
    program_blocks(prog);
  for (GLsizei i = 0; prog && indices && params && i < count; i++) {
    const GLuint idx = indices[i];
    GLint v = 0;
    if (idx < (GLuint)prog->num_slots) {
      const DkglUniformSlot *s = &prog->slots[idx];
      switch (pname) {
      case GL_UNIFORM_TYPE: v = (GLint)s->gl_type; break;
      case GL_UNIFORM_SIZE: v = (GLint)s->array_size; break;
      case GL_UNIFORM_NAME_LENGTH: v = (GLint)strlen(s->name) + (s->array_size > 1 ? 4 : 1); break;
      case GL_UNIFORM_BLOCK_INDEX:
      case GL_UNIFORM_OFFSET:
      case GL_UNIFORM_ARRAY_STRIDE:
      case GL_UNIFORM_MATRIX_STRIDE: v = -1; break;
      default: v = 0; break; /* GL_UNIFORM_IS_ROW_MAJOR */
      }
    } else if (idx < (GLuint)(prog->num_slots + prog->num_mem)) {
      const int k = (int)(idx - (GLuint)prog->num_slots);
      const DkglGlslMember *m = prog->mem[k];
      switch (pname) {
      case GL_UNIFORM_TYPE: v = (GLint)m->gl_type; break;
      case GL_UNIFORM_SIZE: v = (GLint)m->array_size; break;
      case GL_UNIFORM_NAME_LENGTH: v = (GLint)strlen(m->name) + (m->array_stride ? 4 : 1); break;
      case GL_UNIFORM_BLOCK_INDEX: v = prog->mem_block[k]; break;
      case GL_UNIFORM_OFFSET: v = (GLint)m->offset; break;
      case GL_UNIFORM_ARRAY_STRIDE: v = (GLint)m->array_stride; break;
      case GL_UNIFORM_MATRIX_STRIDE: v = (GLint)m->matrix_stride; break;
      default: v = 0; break;
      }
      {
        static int n;
        if (n < 40 && (strstr(m->name, "Color") || strstr(m->name, "Powerup") || strstr(m->name, "ObjectToWorld"))) {
          n++;
          debugPrintf("dkgl: glGetActiveUniformsiv(%u, #%u %s, 0x%x) -> %d\n", program, idx, m->name, pname, v);
        }
      }
    } else {
      if (dkgl_ctx())
        DKGL_ERROR(dkgl_ctx(), GL_INVALID_VALUE);
      break;
    }
    params[i] = v;
  }
  DKGL_UNLOCK();
}

GLuint glGetUniformBlockIndex(GLuint program, const GLchar *uniformBlockName) {
  DKGL_LOCK();
  DkglProgram *prog = dkgl_program(program);
  GLuint index = GL_INVALID_INDEX;
  if (prog) {
    program_blocks(prog);
    for (int s = 0; s < DKGL_STAGES && index == GL_INVALID_INDEX; s++) {
      if (!prog->bin[s])
        continue;
      const DkglGlslResult *r = prog->bin[s]->refl;
      for (int k = 0; k < r->num_blocks && k < 16; k++)
        if (!strcmp(r->blocks[k].name, uniformBlockName)) {
          index = (GLuint)prog->blk_index[s][k];
          break;
        }
    }
  }
  DKGL_UNLOCK();
  return index;
}

void glUniformBlockBinding(GLuint program, GLuint uniformBlockIndex,
                           GLuint uniformBlockBinding) {
  DKGL_LOCK();
  DkglProgram *prog = dkgl_program(program);
  if (prog)
    program_blocks(prog); /* its initial bindings first, so this one stays */
  if (prog && uniformBlockIndex < 16)
    prog->ubo_block_binding[uniformBlockIndex] = uniformBlockBinding;
  DKGL_UNLOCK();
}

void glGetActiveUniformBlockiv(GLuint program, GLuint uniformBlockIndex,
                               GLenum pname, GLint *params) {
  DKGL_LOCK();
  DkglProgram *prog = dkgl_program(program);
  *params = 0;
  int stages = 0;
  const DkglGlslBlock *blk = prog && uniformBlockIndex < 16 ? program_block(prog, uniformBlockIndex, &stages) : NULL;
  if (blk) {
    switch (pname) {
    case GL_UNIFORM_BLOCK_BINDING:
      *params = (GLint)prog->ubo_block_binding[uniformBlockIndex];
      break;
    case GL_UNIFORM_BLOCK_DATA_SIZE:
      *params = (GLint)blk->size;
      break;
    case GL_UNIFORM_BLOCK_NAME_LENGTH:
      *params = (GLint)strlen(blk->name) + 1;
      break;
    case GL_UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER:
      *params = (stages & 1) != 0;
      break;
    case GL_UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER:
      *params = (stages & 2) != 0;
      break;
    default: {
      static GLenum seen[8];
      static int nseen;
      int i = 0;
      while (i < nseen && seen[i] != pname)
        i++;
      if (i == nseen && nseen < 8) {
        seen[nseen++] = pname;
        debugPrintf("dkgl: glGetActiveUniformBlockiv 0x%x not handled\n", pname);
      }
      break;
    }
    }
  }
  DKGL_UNLOCK();
}

void glGetActiveUniformBlockName(GLuint program, GLuint uniformBlockIndex,
                                 GLsizei bufSize, GLsizei *length,
                                 GLchar *uniformBlockName) {
  DKGL_LOCK();
  DkglProgram *prog = dkgl_program(program);
  const DkglGlslBlock *blk = prog ? program_block(prog, uniformBlockIndex, NULL) : NULL;
  copy_string(blk ? blk->name : NULL, bufSize, length, uniformBlockName);
  DKGL_UNLOCK();
}

/* ------------------------------------------------------------ uniforms */

typedef enum { VAL_FLOAT, VAL_INT, VAL_UINT } ValKind;

/* A location as glGetUniformLocation gave it: dkgl's own ((slot + 1) << 12 |
 * element), or one the shader chose (layout(location = n), below 4096). */
static int decode_location(const DkglProgram *prog, GLint location, uint32_t *element) {
  if (location >= 4096) {
    *element = (uint32_t)location & 0xfff;
    return (location >> 12) - 1;
  }
  for (int i = 0; prog && i < prog->num_slots; i++) {
    const DkglUniformSlot *sl = &prog->slots[i];
    if (sl->explicit_loc >= 0 && location >= sl->explicit_loc &&
        (uint32_t)(location - sl->explicit_loc) < sl->array_size) {
      *element = (uint32_t)(location - sl->explicit_loc);
      return i;
    }
  }
  return -1;
}

/* Writes count values of comps components each, starting at location. */
static void set_uniform(GLint location, GLsizei count, int comps, ValKind kind,
                        const void *values, bool matrix, int mcols, int mrows,
                        GLboolean transpose) {
  if (location < 0 || count <= 0)
    return;
  DKGL_LOCK();
  DkglContext *ctx = dkgl_ctx();
  DkglProgram *prog = ctx ? dkgl_program(ctx->program) : NULL;
  uint32_t element = 0;
  const int index = decode_location(prog, location, &element);
  if (!prog || index < 0 || index >= prog->num_slots) {
    if (ctx && location >= 0)
      DKGL_ERROR(ctx, GL_INVALID_OPERATION);
    DKGL_UNLOCK();
    return;
  }
  DkglUniformSlot *slot = &prog->slots[index];
  if (slot->kind != DKGL_SLOT_UNIFORM) {
    /* sampler/image units */
    const int32_t *v = values;
    for (GLsizei i = 0; i < count && element + (uint32_t)i < 8; i++)
      slot->units[element + (uint32_t)i] = (uint8_t)v[i];
    ctx->dirty |= DKGL_DIRTY_TEXTURES | DKGL_DIRTY_IMAGES;
    DKGL_UNLOCK();
    return;
  }
  if (element >= slot->array_size) {
    DKGL_UNLOCK();
    return;
  }
  if ((uint32_t)count > slot->array_size - element)
    count = (GLsizei)(slot->array_size - element);
  for (int s = 0; s < DKGL_STAGES; s++) {
    const DkglSlotTarget *t = &slot->at[s];
    if (t->stage < 0)
      continue;
    DkglUbo *ubo = &prog->ubo[s][t->binding];
    if (!ubo->shadow)
      continue;
    const uint32_t elem_stride =
        t->stride ? t->stride : (slot->columns > 1 ? 16u * slot->columns : 16u);
    uint8_t *dst = ubo->shadow + t->offset + element * elem_stride;
    const uint8_t *src = values;
    if (!matrix) {
      for (GLsizei i = 0; i < count; i++) {
        if (dst + comps * 4 > ubo->shadow + ubo->size)
          break;
        if (kind == VAL_FLOAT || slot->base != DKGL_BASE_BOOL) {
          memcpy(dst, src + (size_t)i * comps * 4, (size_t)comps * 4);
        } else {
          for (int c = 0; c < comps; c++) {
            int32_t v;
            memcpy(&v, src + ((size_t)i * comps + c) * 4, 4);
            uint32_t b = v ? 1u : 0u;
            memcpy(dst + c * 4, &b, 4);
          }
        }
        dst += elem_stride;
      }
    } else {
      const float *m = values;
      for (GLsizei i = 0; i < count; i++) {
        for (int c = 0; c < mcols; c++) {
          float col[4];
          for (int r = 0; r < mrows; r++)
            col[r] = transpose ? m[r * mcols + c] : m[c * mrows + r];
          if (dst + c * 16 + mrows * 4 <= ubo->shadow + ubo->size)
            memcpy(dst + c * 16, col, (size_t)mrows * 4);
        }
        m += mcols * mrows;
        dst += elem_stride;
      }
    }
    ubo->dirty = true;
  }
  DKGL_UNLOCK();
}

#define UNIFORM_FN(suffix, comps, kind, ctype)                                 \
  void glUniform##suffix##v(GLint location, GLsizei count,                     \
                            const ctype *value) {                              \
    set_uniform(location, count, comps, kind, value, false, 0, 0, GL_FALSE);   \
  }
UNIFORM_FN(1f, 1, VAL_FLOAT, GLfloat)
UNIFORM_FN(2f, 2, VAL_FLOAT, GLfloat)
UNIFORM_FN(3f, 3, VAL_FLOAT, GLfloat)
UNIFORM_FN(4f, 4, VAL_FLOAT, GLfloat)
UNIFORM_FN(1i, 1, VAL_INT, GLint)
UNIFORM_FN(2i, 2, VAL_INT, GLint)
UNIFORM_FN(3i, 3, VAL_INT, GLint)
UNIFORM_FN(4i, 4, VAL_INT, GLint)
UNIFORM_FN(1ui, 1, VAL_UINT, GLuint)
UNIFORM_FN(2ui, 2, VAL_UINT, GLuint)
UNIFORM_FN(3ui, 3, VAL_UINT, GLuint)
UNIFORM_FN(4ui, 4, VAL_UINT, GLuint)
#undef UNIFORM_FN

void glUniform1f(GLint location, GLfloat v0) { glUniform1fv(location, 1, &v0); }
void glUniform2f(GLint location, GLfloat v0, GLfloat v1) {
  const GLfloat v[2] = {v0, v1};
  glUniform2fv(location, 1, v);
}
void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {
  const GLfloat v[3] = {v0, v1, v2};
  glUniform3fv(location, 1, v);
}
void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2,
                 GLfloat v3) {
  const GLfloat v[4] = {v0, v1, v2, v3};
  glUniform4fv(location, 1, v);
}
void glUniform1i(GLint location, GLint v0) { glUniform1iv(location, 1, &v0); }
void glUniform2i(GLint location, GLint v0, GLint v1) {
  const GLint v[2] = {v0, v1};
  glUniform2iv(location, 1, v);
}
void glUniform3i(GLint location, GLint v0, GLint v1, GLint v2) {
  const GLint v[3] = {v0, v1, v2};
  glUniform3iv(location, 1, v);
}
void glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3) {
  const GLint v[4] = {v0, v1, v2, v3};
  glUniform4iv(location, 1, v);
}
void glUniform1ui(GLint location, GLuint v0) { glUniform1uiv(location, 1, &v0); }
void glUniform2ui(GLint location, GLuint v0, GLuint v1) {
  const GLuint v[2] = {v0, v1};
  glUniform2uiv(location, 1, v);
}
void glUniform3ui(GLint location, GLuint v0, GLuint v1, GLuint v2) {
  const GLuint v[3] = {v0, v1, v2};
  glUniform3uiv(location, 1, v);
}
void glUniform4ui(GLint location, GLuint v0, GLuint v1, GLuint v2,
                  GLuint v3) {
  const GLuint v[4] = {v0, v1, v2, v3};
  glUniform4uiv(location, 1, v);
}

#define MATRIX_FN(suffix, cols, rows)                                          \
  void glUniformMatrix##suffix##fv(GLint location, GLsizei count,              \
                                   GLboolean transpose,                        \
                                   const GLfloat *value) {                     \
    set_uniform(location, count, 0, VAL_FLOAT, value, true, cols, rows,        \
                transpose);                                                    \
  }
MATRIX_FN(2, 2, 2)
MATRIX_FN(3, 3, 3)
MATRIX_FN(4, 4, 4)
MATRIX_FN(2x3, 2, 3)
MATRIX_FN(3x2, 3, 2)
MATRIX_FN(2x4, 2, 4)
MATRIX_FN(4x2, 4, 2)
MATRIX_FN(3x4, 3, 4)
MATRIX_FN(4x3, 4, 3)
#undef MATRIX_FN

static void get_uniform(GLuint program, GLint location, void *out, bool as_int) {
  DKGL_LOCK();
  DkglProgram *prog = dkgl_program(program);
  uint32_t element = 0;
  const int index = location >= 0 ? decode_location(prog, location, &element) : -1;
  if (!prog || location < 0 || index < 0 || index >= prog->num_slots) {
    DKGL_UNLOCK();
    return;
  }
  DkglUniformSlot *slot = &prog->slots[index];
  if (slot->kind != DKGL_SLOT_UNIFORM) {
    int32_t v = element < 8 ? slot->units[element] : 0;
    if (as_int)
      memcpy(out, &v, 4);
    else {
      float f = (float)v;
      memcpy(out, &f, 4);
    }
    DKGL_UNLOCK();
    return;
  }
  for (int s = 0; s < DKGL_STAGES; s++) {
    const DkglSlotTarget *t = &slot->at[s];
    if (t->stage < 0)
      continue;
    const DkglUbo *ubo = &prog->ubo[s][t->binding];
    const uint32_t stride = t->stride ? t->stride : 16u * slot->columns;
    const uint8_t *src = ubo->shadow + t->offset + element * stride;
    for (int c = 0; c < slot->columns; c++)
      memcpy((uint8_t *)out + (size_t)c * slot->rows * 4, src + c * 16,
             (size_t)slot->rows * 4);
    break;
  }
  DKGL_UNLOCK();
}

void glGetUniformfv(GLuint program, GLint location, GLfloat *params) {
  get_uniform(program, location, params, false);
}
void glGetUniformiv(GLuint program, GLint location, GLint *params) {
  get_uniform(program, location, params, true);
}
void glGetUniformuiv(GLuint program, GLint location, GLuint *params) {
  get_uniform(program, location, params, true);
}

/* ------------------------------------------------------- draw support */

static DkGpuAddr bound_ubo[6][16];
static uint32_t bound_ubo_size[6][16];

void dkgl_uniforms_forget(void);
void dkgl_uniforms_forget(void) { memset(bound_ubo, 0, sizeof(bound_ubo)); }

/* Copies changed uniform blocks into this submission's ring and binds them.
 * A copy made for an earlier submission may already be recycled, so each
 * submission makes its own. */
void dkgl_program_bind_uniforms(DkglContext *ctx, DkglProgram *prog,
                                bool program_changed) {
  (void)program_changed;
  DkCmdBuf cmd = dkgl_cmd();
  for (int s = 0; s < DKGL_STAGES; s++) {
    if (!prog->bin[s])
      continue;
    const DkStage stage = s == 1 ? DkStage_Fragment
                          : prog->compute ? DkStage_Compute
                                          : DkStage_Vertex;
    for (int b = 0; b < prog->num_ubo[s]; b++) {
      DkglUbo *ubo = &prog->ubo[s][b];
      if (!ubo->size)
        continue;
      if (ubo->dirty || ubo->serial != dkg.serial || !ubo->gpu) {
        DkglRing ring;
        if (!dkgl_ring_alloc(ubo->size, DK_UNIFORM_BUF_ALIGNMENT, &ring))
          continue;
        memcpy(ring.cpu, ubo->shadow, ubo->size);
        ubo->gpu = ring.gpu;
        ubo->serial = dkg.serial;
        ubo->dirty = false;
        dkg.stat_uniform_bytes += ubo->size;
      }
      if (bound_ubo[stage][b] != ubo->gpu || bound_ubo_size[stage][b] != ubo->size) {
        dkCmdBufBindUniformBuffer(cmd, stage, (uint32_t)b, ubo->gpu, ubo->size);
        bound_ubo[stage][b] = ubo->gpu;
        bound_ubo_size[stage][b] = ubo->size;
      }
    }
    /* explicit uniform blocks backed by GL buffers */
    const DkglGlslResult *r = prog->bin[s]->refl;
    program_blocks(prog);
    for (int k = 0; k < r->num_blocks && k < 16; k++) {
      if (prog->blk_index[s][k] < 0)
        continue;
      GLuint point = prog->ubo_block_binding[prog->blk_index[s][k]];
      if (point >= DKGL_UBO_BINDINGS)
        continue;
      DkglBuffer *buf = dkgl_buffer(ctx->ubo_bindings[point].buffer, false);
      if (!buf || !buf->mem.node)
        continue;
      uint32_t offset = (uint32_t)ctx->ubo_bindings[point].offset;
      uint32_t size = ctx->ubo_bindings[point].size > 0
                          ? (uint32_t)ctx->ubo_bindings[point].size
                          : buf->size - offset;
      if (size > DK_UNIFORM_BUF_MAX_SIZE)
        size = DK_UNIFORM_BUF_MAX_SIZE;
      size = (size + 255) & ~255u;
      const DkGpuAddr addr = dkgl_gpu(&buf->mem) + offset;
      const uint32_t b = r->blocks[k].binding;
      if (bound_ubo[stage][b] != addr || bound_ubo_size[stage][b] != size) {
        dkCmdBufBindUniformBuffer(cmd, stage, b, addr, size);
        bound_ubo[stage][b] = addr;
        bound_ubo_size[stage][b] = size;
      }
      dkgl_buffer_used(buf);
    }
  }
}

/* ES 3.1 program interface queries (mvl_nx). dkgl reports ES 3.0, so Unity
 * should not ask; if anything does, it hears "no resources" instead of
 * reading whatever was in its output variables (a garbage count once made
 * Unity allocate 2.7 GB). */
void glGetProgramInterfaceiv(GLuint program, GLenum programInterface, GLenum pname, GLint *params) {
  (void)program, (void)programInterface, (void)pname;
  if (params)
    params[0] = 0;
}

void glGetProgramResourceiv(GLuint program, GLenum programInterface, GLuint index, GLsizei propCount,
                            const GLenum *props, GLsizei bufSize, GLsizei *length, GLint *params) {
  (void)program, (void)programInterface, (void)index, (void)props;
  if (length)
    *length = 0;
  for (GLsizei i = 0; params && i < propCount && i < bufSize; i++)
    params[i] = 0;
}

void glGetProgramResourceName(GLuint program, GLenum programInterface, GLuint index, GLsizei bufSize,
                              GLsizei *length, GLchar *name) {
  (void)program, (void)programInterface, (void)index;
  if (length)
    *length = 0;
  if (name && bufSize > 0)
    name[0] = 0;
}

GLuint glGetProgramResourceIndex(GLuint program, GLenum programInterface, const GLchar *name) {
  (void)program, (void)programInterface, (void)name;
  return 0xFFFFFFFFu; /* GL_INVALID_INDEX */
}

GLint glGetProgramResourceLocation(GLuint program, GLenum programInterface, const GLchar *name) {
  (void)program, (void)programInterface, (void)name;
  return -1;
}
