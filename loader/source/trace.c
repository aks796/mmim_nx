/* trace.c -- debug: C# methods logged on entry (config trace)
 *
 * config trace lists methods, comma separated, as Namespace.Class::Method
 * ("Game.UICurrentValueInputField::onEndEdit"; nested classes as
 * Outer/Inner); every overload is traced. Each entry is logged with its
 * arguments: strings as text, anything else as a register value. Up to
 * MAX_TRACE methods.
 *
 * config trace_nre is "auto" (found by its instructions) or the offset in
 * libil2cpp.so of il2cpp::vm::Exception::RaiseNullReferenceException()
 * (llvm-nm on the build's unstripped libil2cpp.so; 0x965a00 for MMIM build
 * 202611051000):
 * each place C# raises a NullReferenceException is logged once, as the
 * return addresses up the stack ("[nre] #3 libil2cpp.so+0x1a2b3c ..."), for
 * llvm-addr2line -f on that libil2cpp.so. An IL2CPP release build logs
 * these exceptions without a stack trace.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "il2cpp.h"
#include "log.h"
#include "so_util.h"

#define MAX_TRACE 16
#define MAX_ARGS 7

typedef struct {
  char name[200];
  int is_static, nargs;
  char is_str[MAX_ARGS];
} Traced;

static int64_t sext(uint64_t v, int bits) {
  return (int64_t)(v << (64 - bits)) >> (64 - bits);
}

static Traced s_tr[MAX_TRACE];
static int s_ntr;

static void trace_log(int i, const uint64_t *frame) {
  const Traced *t = &s_tr[i];
  char line[512];
  int o = snprintf(line, sizeof line, "[trace] %s(", t->name);
  const int first = t->is_static ? 0 : 1; // x0 is this
  for (int a = 0; a < t->nargs && a < MAX_ARGS && o < (int)sizeof line - 8; a++) {
    const uint64_t v = frame[2 + first + a];
    if (t->is_str[a]) {
      char s[128];
      o += snprintf(line + o, sizeof line - (size_t)o, "%s\"%s\"", a ? ", " : "",
                    v ? il2cpp_utf8((const void *)(uintptr_t)v, s, sizeof s) : "(null)");
    } else {
      o += snprintf(line + o, sizeof line - (size_t)o, "%s0x%llx", a ? ", " : "", (unsigned long long)v);
    }
  }
  if (o < (int)sizeof line - 2)
    snprintf(line + o, sizeof line - (size_t)o, ")");
  log_printf("%s", line);
}

#define TR(n) static void tr##n(uint64_t *f) { trace_log(n, f); }
TR(0) TR(1) TR(2) TR(3) TR(4) TR(5) TR(6) TR(7) TR(8) TR(9) TR(10) TR(11) TR(12) TR(13) TR(14) TR(15)
static void (*const s_fns[MAX_TRACE])(uint64_t *) = {
  tr0, tr1, tr2, tr3, tr4, tr5, tr6, tr7, tr8, tr9, tr10, tr11, tr12, tr13, tr14, tr15,
};

static void trace_one(const char *spec) {
  char ns[96] = "", cls[96], meth[96];
  const char *sep = strstr(spec, "::");
  if (!sep || sep == spec) {
    log_printf("[trace] %s: not Namespace.Class::Method", spec);
    return;
  }
  snprintf(meth, sizeof meth, "%s", sep + 2);
  // the namespace ends at the last '.' before the class (and before any '/')
  const char *slash = memchr(spec, '/', (size_t)(sep - spec));
  const char *dot = NULL;
  for (const char *p = spec; p < (slash ? slash : sep); p++)
    if (*p == '.')
      dot = p;
  if (dot)
    snprintf(ns, sizeof ns, "%.*s", (int)(dot - spec), spec);
  snprintf(cls, sizeof cls, "%.*s", (int)(sep - (dot ? dot + 1 : spec)), dot ? dot + 1 : spec);
  void *klass = il2cpp_find_class(NULL, ns, cls);
  void *ms[8];
  const int n = klass ? il2cpp_methods_named(klass, meth, ms, 8) : 0;
  if (!n)
    log_printf("[trace] %s: %s", spec, klass ? "no such method" : "no such class");
  for (int i = 0; i < n && s_ntr < MAX_TRACE; i++) {
    Traced *t = &s_tr[s_ntr];
    snprintf(t->name, sizeof t->name, "%s::%s", cls, meth);
    t->is_static = il2cpp_method_is_static(ms[i]);
    t->nargs = il2cpp_method_params(ms[i]);
    for (int a = 0; a < t->nargs && a < MAX_ARGS; a++) {
      char tn[96];
      il2cpp_method_param_type(ms[i], a, tn, sizeof tn);
      t->is_str[a] = !strcmp(tn, "System.String");
    }
    const uintptr_t code = il2cpp_method_code(ms[i]);
    if (code && !hook_arm64_pre(code, s_fns[s_ntr]))
      s_ntr++;
    else
      log_printf("[trace] %s: not hooked", spec);
  }
}

// The hook's frame is just below the stack pointer at entry; the codegen
// helper that calls RaiseNullReferenceException (str x30, [sp, #-16]!; bl)
// leaves the C# method's return address there, and x29 is still the C#
// method's frame record.
static void nre_log(uint64_t *frame) {
  static uint64_t seen[32];
  static int nseen;
  const uint64_t site = *(const uint64_t *)((const uint8_t *)frame + 224);
  for (int i = 0; i < nseen; i++)
    if (seen[i] == site)
      return;
  if (nseen >= 32)
    return;
  seen[nseen++] = site;
  char line[768];
  int o = snprintf(line, sizeof line, "[nre] #%d", nseen);
  uint64_t ret = site, fp = frame[0];
  for (int d = 0; d < 12 && ret && o < (int)sizeof line - 48; d++) {
    uintptr_t base = 0;
    const char *name = so_name_at_unlocked((uintptr_t)ret, &base);
    o += name ? snprintf(line + o, sizeof line - (size_t)o, " %s+0x%lx", name, (unsigned long)(ret - base))
              : snprintf(line + o, sizeof line - (size_t)o, " 0x%lx", (unsigned long)ret);
    // up the frame records: each one higher on the stack than the last
    if (!fp || (fp & 7) || fp < (uint64_t)(uintptr_t)frame || fp - (uint64_t)(uintptr_t)frame > (1u << 20))
      break;
    const uint64_t *rec = (const uint64_t *)(uintptr_t)fp;
    ret = rec[1];
    if (rec[0] <= fp)
      break;
    fp = rec[0];
  }
  log_printf("%s", line);
}

// RaiseNullReferenceException(): sub sp, sp, #32; stp xzr, x30, [sp, #8];
// mov x0, sp; str xzr, [sp]; bl (the overload taking a message, next). The
// other Raise...Exception() helpers start the same way; this one's message
// overload (str x30, [sp, #-16]!; bl Get...Exception) gets its class by the
// name "NullReferenceException", which its first instructions address with
// ADRP + ADD.
static uintptr_t bl_target(uintptr_t pc) {
  const uint32_t w = *(const uint32_t *)pc;
  if ((w & 0xfc000000u) != 0x94000000u)
    return 0;
  return pc + (uintptr_t)(sext(w & 0x3ffffff, 26) << 2);
}

static int addresses(uintptr_t fn, const uintptr_t *names, int nnames) {
  const uint32_t *w = (const uint32_t *)fn;
  for (int i = 0; i + 1 < 24; i++) {
    if ((w[i] & 0x9f000000u) != 0x90000000u) // ADRP
      continue;
    const int rd = (int)(w[i] & 0x1f);
    const int64_t imm = sext(((w[i] >> 29) & 3) | (((w[i] >> 5) & 0x7ffff) << 2), 21);
    const uintptr_t page = ((fn + 4 * (uintptr_t)i) & ~(uintptr_t)0xfff) + (uintptr_t)(imm << 12);
    for (int j = i + 1; j < i + 4 && j < 24; j++) {
      // ADD (immediate, 64-bit, no shift) rd, rd, #imm12
      if ((w[j] & 0xffc00000u) == 0x91000000u && (int)(w[j] & 0x1f) == rd && (int)((w[j] >> 5) & 0x1f) == rd) {
        const uintptr_t a = page + ((w[j] >> 10) & 0xfff);
        for (int k = 0; k < nnames; k++)
          if (a == names[k])
            return 1;
      }
    }
  }
  return 0;
}

static uintptr_t find_raise_nre(const so_module *m) {
  static const uint32_t pat[5] = { 0xd10083ffu, 0xa900fbffu, 0x910003e0u, 0xf90003ffu, 0x94000001u };
  static const char name[] = "\0NullReferenceException\0";
  uintptr_t names[8];
  int nnames = 0;
  const uint8_t *img = (const uint8_t *)m->base;
  for (size_t i = 0; i + sizeof name - 1 <= m->size && nnames < 8; i++)
    if (img[i] == 0 && img[i + 1] == 'N' && !memcmp(img + i, name, sizeof name - 1))
      names[nnames++] = m->base + i + 1;
  const uint32_t *w = (const uint32_t *)m->base;
  const size_t n = m->text_size / 4;
  uintptr_t found = 0;
  for (size_t i = 0; i + 7 <= n; i++) {
    if (w[i] != pat[0] || memcmp(&w[i], pat, sizeof pat))
      continue;
    const uintptr_t get = bl_target(m->base + 4 * (i + 6)); // after str x30
    if (!get || get < m->base || get >= m->base + m->text_size || !addresses(get, names, nnames))
      continue;
    if (found)
      return 0; // not one place: guess nothing
    found = 4 * i;
  }
  return found;
}

static void trace_nre_init(void) {
  so_module *m = so_find_loaded("libil2cpp.so");
  const uintptr_t off = !m ? 0
                        : !strcmp(config.trace_nre, "auto") ? find_raise_nre(m)
                                                             : (uintptr_t)strtoull(config.trace_nre, NULL, 16);
  if (!m || !off || off >= m->text_size) {
    log_printf("[nre] trace_nre %s: no libil2cpp.so code there", config.trace_nre);
    return;
  }
  const uint32_t *w = (const uint32_t *)(m->base + off);
  // Exception::RaiseNullReferenceException(): sub sp, sp, #32 first
  if (w[0] != 0xd10083ffu) {
    log_printf("[nre] libil2cpp.so+0x%lx is not RaiseNullReferenceException (%08x)", (unsigned long)off, w[0]);
    return;
  }
  log_printf("[nre] libil2cpp.so+0x%lx %s", (unsigned long)off,
             hook_arm64_pre(m->base + off, nre_log) ? "not hooked" : "hooked: logging where they are raised");
}

// Every managed exception IL2CPP raises (a C# throw, a bounds check, a
// cast) goes through il2cpp::vm::Exception::Raise(exception, method), which
// the exported il2cpp_raise_exception tail-calls: hooked, each place one is
// raised from is logged once with the exception's class, where the NRE log
// above only knows NullReferenceExceptions. Release builds' Unity log has no
// stack for them.
static void exc_log(uint64_t *frame) {
  static uint64_t seen[48];
  static int nseen;
  const uint64_t site = frame[1];
  for (int i = 0; i < nseen; i++)
    if (seen[i] == site)
      return;
  if (nseen >= 48)
    return;
  seen[nseen++] = site;
  void *klass = il2cpp_object_class((void *)(uintptr_t)frame[2]);
  const char *cls = klass ? il2cpp_class_name(klass) : NULL;
  char line[768];
  int o = snprintf(line, sizeof line, "[exc] #%d %s", nseen, cls ? cls : "?");
  // Raise...Exception() helpers (str x30, [sp, #-16]!; bl; bl Raise) keep
  // no frame record: the C# method's return address is just above our frame
  // (codegen's raise_index_out_of_range: one more such helper, 16 bytes up)
  for (int k = 0; k < 3; k++) {
    const uint64_t via = *(const uint64_t *)((const uint8_t *)frame + 224 + 16 * k);
    uintptr_t base = 0;
    const char *name = via ? so_name_at_unlocked((uintptr_t)via, &base) : NULL;
    if (name)
      o += snprintf(line + o, sizeof line - (size_t)o, " (from %s+0x%lx)", name, (unsigned long)(via - base));
  }
  uint64_t ret = site, fp = frame[0];
  for (int d = 0; d < 12 && ret && o < (int)sizeof line - 48; d++) {
    uintptr_t base = 0;
    const char *name = so_name_at_unlocked((uintptr_t)ret, &base);
    o += name ? snprintf(line + o, sizeof line - (size_t)o, " %s+0x%lx", name, (unsigned long)(ret - base))
              : snprintf(line + o, sizeof line - (size_t)o, " 0x%lx", (unsigned long)ret);
    if (!fp || (fp & 7) || fp < (uint64_t)(uintptr_t)frame || fp - (uint64_t)(uintptr_t)frame > (1u << 20))
      break;
    const uint64_t *rec = (const uint64_t *)(uintptr_t)fp;
    ret = rec[1];
    if (rec[0] <= fp)
      break;
    fp = rec[0];
  }
  log_printf("%s", line);
}

static void trace_exc_init(void) {
  so_module *m = so_find_loaded("libil2cpp.so");
  const uintptr_t api = m ? so_sym(m, "il2cpp_raise_exception") : 0;
  uintptr_t raise = 0;
  for (int i = 0; api && i < 6 && !raise; i++) {
    const uint32_t w = ((const uint32_t *)api)[i];
    if ((w & 0x7c000000u) == 0x14000000u) // B or BL
      raise = api + 4 * (uintptr_t)i + (uintptr_t)(sext(w & 0x3ffffff, 26) << 2);
  }
  if (!raise || raise < m->base || raise >= m->base + m->text_size) {
    log_printf("[exc] Exception::Raise not found");
    return;
  }
  log_printf("[exc] Exception::Raise at libil2cpp.so+0x%lx %s", (unsigned long)(raise - m->base),
             hook_arm64_pre(raise, exc_log) ? "not hooked" : "hooked: logging where exceptions are raised");
}

void trace_init(void) {
  if (config.trace_nre[0]) {
    trace_nre_init();
    trace_exc_init();
  }
  char list[sizeof config.trace];
  snprintf(list, sizeof list, "%s", config.trace);
  for (char *save = NULL, *tok = strtok_r(list, ", ", &save); tok; tok = strtok_r(NULL, ", ", &save))
    trace_one(tok);
  if (s_ntr)
    log_printf("[trace] %d method(s) traced", s_ntr);
}
