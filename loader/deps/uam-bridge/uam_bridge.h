/* uam_bridge.h -- C entry point into libuamcore (deko3d's GLSL compiler) */
#ifndef UAM_BRIDGE_H
#define UAM_BRIDGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  UAM_STAGE_VERTEX = 0,
  UAM_STAGE_FRAGMENT = 4,
  UAM_STAGE_COMPUTE = 5,
};

/* Compiles one GLSL stage to a DKSH image. On success returns 1 and a
 * malloc'd *out_dksh of *out_size bytes (free() it). Compiler messages
 * (errors and warnings) are written NUL-terminated to log (may be NULL).
 * Calls are serialised internally: Mesa's GLSL front end is not reentrant. */
int uam_bridge_compile(int stage, const char *glsl, void **out_dksh,
                       size_t *out_size, char *log, size_t log_size);

/* As above with flags. UAM_FLAG_CONSERVATIVE_SCHED (uam patch 0004) makes
 * every instruction stall 15 cycles and wait on all scoreboard barriers:
 * slow, for shaders the regular scheduling breaks on the console. */
enum { UAM_FLAG_CONSERVATIVE_SCHED = 1 };
int uam_bridge_compile2(int stage, const char *glsl, int flags,
                        void **out_dksh, size_t *out_size, char *log,
                        size_t log_size);

#ifdef __cplusplus
}
#endif

#endif
