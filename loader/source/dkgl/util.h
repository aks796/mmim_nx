/* util.h -- what dkgl asks of the port (its util.h in RL Sideswipe, where
 * dkgl comes from): logging and where its files go. dkgl's sources find
 * this one before the port's own util.h, which it includes. */
#ifndef DKGL_PORT_UTIL_H
#define DKGL_PORT_UTIL_H

#include "../util.h"

// into debug.log (dkgl's lines end in \n; log_printf adds its own)
void dkgl_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define debugPrintf dkgl_log

// "<internal>/<name>": dkgl's shader cache and debug files (a few rotating
// buffers)
const char *dkgl_path(const char *name);

// RL Sideswipe's: debug.log onto the card now (before a fatal error); the
// busiest threads with dkgl's stats (the port's [perf] lines have them)
void log_flush(void);
#define debug_log_flush log_flush
static inline void thread_registry_report_cpu(void) {}
// 1 for the first budget messages of a kind (key_a.key_b), then 0
int debug_rate_limit(const char *key_a, const char *key_b, unsigned int budget);

#endif
