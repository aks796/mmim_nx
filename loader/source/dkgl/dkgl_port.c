/* dkgl_port.c -- dkgl's hooks into the port (dkgl/util.h)
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "util.h"
#include "../log.h"
#include "../paths.h"

void dkgl_log(const char *fmt, ...) {
  char line[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  size_t n = strlen(line);
  while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
    line[--n] = '\0';
  log_printf("[dkgl] %s", strncmp(line, "dkgl: ", 6) ? line : line + 6);
}

const char *dkgl_path(const char *name) {
  static char bufs[4][600];
  static unsigned rr;
  char *out = bufs[rr++ & 3];
  snprintf(out, sizeof bufs[0], "%s/%s", path_internal(), name);
  return out;
}

// --- GL functions dkgl does not have ---------------------------------------------
//
// Each gets a stub of its own: x0 = its name, then missing_called() (which
// logs it at the first call and returns 0).

#include <stdint.h>
#include <stdlib.h>
#include <switch.h>
#include "../so_util.h"

static uint64_t missing_called(const char *name) {
  static const char *seen[64];
  static int n;
  for (int i = 0; i < n; i++)
    if (seen[i] == name)
      return 0;
  if (n < 64)
    seen[n++] = name;
  log_printf("[dkgl] %s called: not implemented (nothing done)", name);
  return 0;
}

void *dkgl_missing_proc(const char *name) {
  static struct {
    const char *name;
    void *stub;
  } made[256];
  static int nmade;
  static Mutex lock;
  mutexLock(&lock);
  for (int i = 0; i < nmade; i++)
    if (!strcmp(made[i].name, name)) {
      mutexUnlock(&lock);
      return made[i].stub;
    }
  void *stub = NULL;
  char *copy = strdup(name);
  if (copy && nmade < 256) {
    uint32_t code[8];
    code[0] = 0x58000000 | (4 << 5) | 0;  // ldr x0, name (+16)
    code[1] = 0x58000000 | (5 << 5) | 16; // ldr x16, missing_called (+24)
    code[2] = 0xd61f0200;                 // br x16
    code[3] = 0xd503201f;                 // nop
    const uint64_t lit[2] = { (uint64_t)(uintptr_t)copy, (uint64_t)(uintptr_t)missing_called };
    memcpy(&code[4], lit, sizeof lit);
    stub = so_alloc_code(sizeof code);
    if (stub && so_write_code(stub, code, sizeof code) == 0) {
      made[nmade].name = copy;
      made[nmade].stub = stub;
      nmade++;
    } else {
      stub = NULL;
    }
  }
  mutexUnlock(&lock);
  if (!stub)
    log_printf("[dkgl] no stub for %s", name);
  return stub;
}

int debug_rate_limit(const char *key_a, const char *key_b, unsigned int budget) {
  static struct {
    uint64_t key;
    unsigned count;
  } slots[64];
  static Mutex lock;
  uint64_t key = 1469598103934665603ull;
  for (const char *p = key_a ? key_a : ""; *p; p++)
    key = (key ^ (uint8_t)*p) * 1099511628211ull;
  key ^= 0x2e;
  for (const char *p = key_b ? key_b : ""; *p; p++)
    key = (key ^ (uint8_t)*p) * 1099511628211ull;
  mutexLock(&lock);
  unsigned count = budget + 2; // (table full: quiet)
  for (unsigned i = 0; i < 64; i++) {
    const unsigned k = (unsigned)((key + i) % 64);
    if (!slots[k].key || slots[k].key == key) {
      slots[k].key = key;
      count = ++slots[k].count;
      break;
    }
  }
  mutexUnlock(&lock);
  if (count == budget + 1)
    log_printf("[dkgl] further \"%s%s%s\" messages suppressed", key_a ? key_a : "", key_b && *key_b ? "." : "",
               key_b ? key_b : "");
  return count <= budget;
}
