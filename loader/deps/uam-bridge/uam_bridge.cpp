// uam_bridge.cpp -- C entry point into libuamcore, built inside the uam library
// so the wrapper only needs this header and -luamcore.
#include "uam_bridge.h"
#include "compiler_iface.h"

#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

static std::mutex compile_lock;
static char *log_buffer;
/* Read by the GM107 emitter (uam patch 0004); set only under compile_lock. */
extern "C" int uam_conservative_sched;
int uam_conservative_sched;
static size_t log_capacity;
static size_t log_used;

extern "C" void uam_log_printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  if (log_buffer && log_used + 1 < log_capacity) {
    const int written = vsnprintf(log_buffer + log_used,
                                  log_capacity - log_used, fmt, ap);
    if (written > 0) {
      log_used += (size_t)written;
      if (log_used >= log_capacity)
        log_used = log_capacity - 1;
    }
  }
  va_end(ap);
}

extern "C" int uam_bridge_compile(int stage, const char *glsl,
                                  void **out_dksh, size_t *out_size,
                                  char *log, size_t log_size) {
  return uam_bridge_compile2(stage, glsl, 0, out_dksh, out_size, log, log_size);
}

extern "C" int uam_bridge_compile2(int stage, const char *glsl, int flags,
                                   void **out_dksh, size_t *out_size,
                                   char *log, size_t log_size) {
  std::lock_guard<std::mutex> guard(compile_lock);
  uam_conservative_sched = (flags & UAM_FLAG_CONSERVATIVE_SCHED) != 0;
  *out_dksh = nullptr;
  *out_size = 0;
  log_buffer = log;
  log_capacity = log ? log_size : 0;
  log_used = 0;
  if (log && log_size)
    log[0] = '\0';

  int ok = 0;
  {
    DekoCompiler compiler(static_cast<pipeline_stage>(stage));
    if (compiler.CompileGlsl(glsl)) {
      std::vector<uint8_t> bytes;
      compiler.OutputDkshToMemory(bytes);
      if (!bytes.empty()) {
        void *copy = std::malloc(bytes.size());
        if (copy) {
          std::memcpy(copy, bytes.data(), bytes.size());
          *out_dksh = copy;
          *out_size = bytes.size();
          ok = 1;
        }
      }
    }
  }
  log_buffer = nullptr;
  log_capacity = 0;
  uam_conservative_sched = 0;
  return ok;
}
