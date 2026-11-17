/* il2cpp.c -- the game's C# through the IL2CPP runtime API (see il2cpp.h)
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "apkfile.h"
#include "config.h"
#include "il2cpp.h"
#include "log.h"
#include "paths.h"
#include "unity.h"

static struct {
  void *(*domain_get)(void);
  void **(*domain_get_assemblies)(void *domain, size_t *size);
  void *(*assembly_get_image)(void *assembly);
  const char *(*image_get_name)(void *image);
  size_t (*image_get_class_count)(void *image);
  void *(*image_get_class)(void *image, size_t index);
  void *(*class_from_name)(void *image, const char *ns, const char *name);
  const char *(*class_get_name)(void *klass);
  const char *(*class_get_namespace)(void *klass);
  void *(*class_get_parent)(void *klass);
  void *(*class_get_declaring_type)(void *klass);
  void *(*class_get_nested_types)(void *klass, void **iter);
  void *(*class_get_methods)(void *klass, void **iter);
  void *(*class_get_fields)(void *klass, void **iter);
  void *(*class_get_method_from_name)(void *klass, const char *name, int argc);
  void *(*class_get_field_from_name)(void *klass, const char *name);
  int (*class_is_enum)(void *klass);
  int (*class_is_valuetype)(void *klass);
  const char *(*method_get_name)(void *method);
  uint32_t (*method_get_param_count)(void *method);
  void *(*method_get_param)(void *method, uint32_t index);
  const char *(*method_get_param_name)(void *method, uint32_t index);
  void *(*method_get_return_type)(void *method);
  uint32_t (*method_get_flags)(void *method, uint32_t *iflags);
  char *(*type_get_name)(void *type);
  const char *(*field_get_name)(void *field);
  size_t (*field_get_offset)(void *field);
  void *(*field_get_type)(void *field);
  int (*field_get_flags)(void *field);
  void (*free)(void *p);
  void *(*thread_attach)(void *domain);
  void *(*object_get_class)(void *obj);
  void *(*array_new_specific)(void *array_class, uintptr_t length);
  void *(*runtime_invoke)(void *method, void *obj, void **params, void **exc);
  void *(*string_new)(const char *utf8);
  void *(*object_new)(void *klass);
  uint32_t (*gchandle_new)(void *obj, int pinned);
  void (*gchandle_free)(uint32_t handle);
  void *(*array_new)(void *element_class, uintptr_t length);
  void (*field_static_get_value)(void *field, void *value);
  void (*field_static_set_value)(void *field, void *value);
  void *(*class_get_type)(void *klass);
  void *(*type_get_object)(void *type);
} api;

static int s_ok;

#define RES(f, n) api.f = (void *)so_sym(g_il2cpp, "il2cpp_" n)

int il2cpp_api_init(void) {
  RES(domain_get, "domain_get");
  RES(domain_get_assemblies, "domain_get_assemblies");
  RES(assembly_get_image, "assembly_get_image");
  RES(image_get_name, "image_get_name");
  RES(image_get_class_count, "image_get_class_count");
  RES(image_get_class, "image_get_class");
  RES(class_from_name, "class_from_name");
  RES(class_get_name, "class_get_name");
  RES(class_get_namespace, "class_get_namespace");
  RES(class_get_parent, "class_get_parent");
  RES(class_get_declaring_type, "class_get_declaring_type");
  RES(class_get_nested_types, "class_get_nested_types");
  RES(class_get_methods, "class_get_methods");
  RES(class_get_fields, "class_get_fields");
  RES(class_get_method_from_name, "class_get_method_from_name");
  RES(class_get_field_from_name, "class_get_field_from_name");
  RES(class_is_enum, "class_is_enum");
  RES(class_is_valuetype, "class_is_valuetype");
  RES(method_get_name, "method_get_name");
  RES(method_get_param_count, "method_get_param_count");
  RES(method_get_param, "method_get_param");
  RES(method_get_param_name, "method_get_param_name");
  RES(method_get_return_type, "method_get_return_type");
  RES(method_get_flags, "method_get_flags");
  RES(type_get_name, "type_get_name");
  RES(field_get_name, "field_get_name");
  RES(field_get_offset, "field_get_offset");
  RES(field_get_type, "field_get_type");
  RES(field_get_flags, "field_get_flags");
  RES(free, "free");
  RES(thread_attach, "thread_attach");
  RES(object_get_class, "object_get_class");
  RES(array_new_specific, "array_new_specific");
  RES(runtime_invoke, "runtime_invoke");
  RES(string_new, "string_new");
  RES(object_new, "object_new");
  RES(gchandle_new, "gchandle_new");
  RES(gchandle_free, "gchandle_free");
  RES(array_new, "array_new");
  RES(field_static_get_value, "field_static_get_value");
  RES(field_static_set_value, "field_static_set_value");
  RES(class_get_type, "class_get_type");
  RES(type_get_object, "type_get_object");
  s_ok = api.domain_get && api.domain_get_assemblies && api.assembly_get_image &&
         api.image_get_name && api.class_from_name && api.class_get_method_from_name &&
         api.method_get_name;
  log_printf("[il2cpp] runtime API %s", s_ok ? "found" : "MISSING");
  return s_ok;
}

static void *image_by_name(const char *name) {
  void *dom = api.domain_get();
  if (!dom)
    return NULL;
  size_t n = 0;
  void **as = api.domain_get_assemblies(dom, &n);
  for (size_t i = 0; as && i < n; i++) {
    void *img = api.assembly_get_image(as[i]);
    const char *in = img ? api.image_get_name(img) : NULL;
    if (in && !strcmp(in, name))
      return img;
  }
  return NULL;
}

int il2cpp_ready(void) {
  if (!s_ok)
    return 0;
  return image_by_name("Assembly-CSharp.dll") != NULL;
}

void *il2cpp_find_class(const char *image, const char *ns, const char *name) {
  if (!s_ok)
    return NULL;
  // "Outer/Inner": the outer class, then its nested types by name
  const char *slash = strchr(name, '/');
  char outer[256];
  snprintf(outer, sizeof outer, "%.*s", slash ? (int)(slash - name) : (int)strlen(name), name);
  void *k = NULL;
  if (image) {
    void *img = image_by_name(image);
    if (!img)
      return NULL;
    k = api.class_from_name(img, ns, outer);
  } else {
    // any assembly
    void *dom = api.domain_get();
    size_t n = 0;
    void **as = dom ? api.domain_get_assemblies(dom, &n) : NULL;
    for (size_t i = 0; !k && as && i < n; i++) {
      void *img = api.assembly_get_image(as[i]);
      k = img ? api.class_from_name(img, ns, outer) : NULL;
    }
  }
  while (k && slash) {
    const char *part = slash + 1;
    slash = strchr(part, '/');
    const size_t len = slash ? (size_t)(slash - part) : strlen(part);
    void *it = NULL, *n, *found = NULL;
    while (!found && api.class_get_nested_types && (n = api.class_get_nested_types(k, &it)) != NULL) {
      const char *nn = api.class_get_name(n);
      if (nn && strlen(nn) == len && !strncmp(nn, part, len))
        found = n;
    }
    k = found;
  }
  return k;
}

int il2cpp_image_classes(const char *image, void **out, int max) {
  void *img = s_ok ? image_by_name(image) : NULL;
  if (!img || !api.image_get_class_count)
    return 0;
  const size_t cc = api.image_get_class_count(img);
  int n = 0;
  for (size_t i = 0; i < cc && n < max; i++) {
    void *k = api.image_get_class(img, i);
    if (k)
      out[n++] = k;
  }
  return n;
}

const char *il2cpp_class_namespace(void *klass) {
  return s_ok && klass ? api.class_get_namespace(klass) : "";
}

int il2cpp_nested(void *klass, void **out, int max) {
  int n = 0;
  void *it = NULL, *k;
  while (s_ok && klass && api.class_get_nested_types && n < max &&
         (k = api.class_get_nested_types(klass, &it)) != NULL)
    out[n++] = k;
  return n;
}

const char *il2cpp_class_name(void *klass) {
  return s_ok && klass ? api.class_get_name(klass) : "?";
}

int il2cpp_method_codes(void *klass, const char *name, uintptr_t *out, int max) {
  if (!s_ok || !klass)
    return 0;
  int n = 0;
  void *it = NULL, *m;
  while (n < max && (m = api.class_get_methods(klass, &it)) != NULL) {
    const char *mn = api.method_get_name(m);
    const uintptr_t code = il2cpp_method_code(m);
    if (mn && code && !strcmp(mn, name))
      out[n++] = code;
  }
  return n;
}

int il2cpp_methods_named(void *klass, const char *name, void **out, int max) {
  if (!s_ok || !klass)
    return 0;
  int n = 0;
  void *it = NULL, *m;
  while (n < max && (m = api.class_get_methods(klass, &it)) != NULL) {
    const char *mn = api.method_get_name(m);
    if (mn && !strcmp(mn, name))
      out[n++] = m;
  }
  return n;
}

static void type_name(void *type, char *out, size_t n);

int il2cpp_method_is_static(void *method) {
  uint32_t ifl = 0;
  return method && api.method_get_flags && (api.method_get_flags(method, &ifl) & 0x10);
}

int il2cpp_method_params(void *method) {
  return method && api.method_get_param_count ? (int)api.method_get_param_count(method) : 0;
}

void il2cpp_method_param_type(void *method, int i, char *out, size_t n) {
  type_name(method && api.method_get_param ? api.method_get_param(method, (uint32_t)i) : NULL, out, n);
}

void *il2cpp_find_method(void *klass, const char *name, int argc) {
  if (!s_ok || !klass)
    return NULL;
  return api.class_get_method_from_name(klass, name, argc);
}

uintptr_t il2cpp_method_code(void *method) {
  return method ? *(uintptr_t *)method : 0; // MethodInfo.methodPointer is first
}

uintptr_t il2cpp_code_of(const char *image, const char *ns, const char *cls, const char *method,
                         int argc) {
  void *k = il2cpp_find_class(image, ns, cls);
  void *m = k ? il2cpp_find_method(k, method, argc) : NULL;
  const uintptr_t code = il2cpp_method_code(m);
  if (!code)
    log_printf("[il2cpp] %s.%s::%s(%d) not found (%s)", ns, cls, method, argc,
               !k ? "no class" : !m ? "no method" : "no code");
  return code;
}

const char *il2cpp_utf8(const void *str, char *out, size_t n) {
  // Il2CppString: object header (16 bytes), int32 length, UTF-16 chars
  if (!n)
    return out;
  size_t o = 0;
  if (str) {
    const int32_t len = *(const int32_t *)((const uint8_t *)str + 0x10);
    const uint16_t *c = (const uint16_t *)((const uint8_t *)str + 0x14);
    for (int32_t i = 0; i < len && o + 4 < n; i++) {
      const uint32_t u = c[i];
      if (u < 0x80) {
        out[o++] = (char)u;
      } else if (u < 0x800) {
        out[o++] = (char)(0xc0 | (u >> 6));
        out[o++] = (char)(0x80 | (u & 0x3f));
      } else {
        out[o++] = (char)(0xe0 | (u >> 12));
        out[o++] = (char)(0x80 | ((u >> 6) & 0x3f));
        out[o++] = (char)(0x80 | (u & 0x3f));
      }
    }
  }
  out[o] = 0;
  return out;
}

void *il2cpp_find_class_with(const char *image, const char *const *fields) {
  void *img = s_ok ? image_by_name(image) : NULL;
  if (!img || !api.image_get_class_count || !api.class_get_field_from_name)
    return NULL;
  const size_t cc = api.image_get_class_count(img);
  void *found = NULL;
  int hits = 0;
  for (size_t i = 0; i < cc; i++) {
    void *k = api.image_get_class(img, i);
    if (!k)
      continue;
    int all = 1;
    for (int f = 0; fields[f] && all; f++)
      all = api.class_get_field_from_name(k, fields[f]) != NULL;
    if (all) {
      found = k;
      hits++;
    }
  }
  if (hits != 1) {
    log_printf("[il2cpp] a class with fields %s, %s...: %d found", fields[0], fields[1] ? fields[1] : "", hits);
    return NULL;
  }
  return found;
}

int il2cpp_field_offset(void *klass, const char *name) {
  if (!s_ok || !klass || !api.class_get_field_from_name)
    return -1;
  void *f = api.class_get_field_from_name(klass, name);
  return f ? (int)api.field_get_offset(f) : -1;
}

// --- the dump -------------------------------------------------------------------------------

static void type_name(void *type, char *out, size_t n) {
  char *s = type && api.type_get_name ? api.type_get_name(type) : NULL;
  snprintf(out, n, "%s", s ? s : "?");
  if (s && api.free)
    api.free(s);
}

static void dump_class(FILE *f, void *k) {
  const char *ns = api.class_get_namespace(k);
  const char *nm = api.class_get_name(k);
  void *outer = api.class_get_declaring_type ? api.class_get_declaring_type(k) : NULL;
  void *parent = api.class_get_parent ? api.class_get_parent(k) : NULL;
  fprintf(f, "\nclass %s%s%s%s%s", ns && ns[0] ? ns : "", ns && ns[0] ? "." : "",
          outer ? api.class_get_name(outer) : "", outer ? "/" : "", nm);
  if (parent)
    fprintf(f, " : %s", api.class_get_name(parent));
  fputc('\n', f);
  void *it = NULL, *fld;
  while (api.class_get_fields && (fld = api.class_get_fields(k, &it)) != NULL) {
    char tn[256];
    type_name(api.field_get_type(fld), tn, sizeof tn);
    const int flags = api.field_get_flags ? api.field_get_flags(fld) : 0;
    fprintf(f, "  field 0x%zx %s%s %s\n", api.field_get_offset(fld), (flags & 0x10) ? "static " : "",
            tn, api.field_get_name(fld));
  }
  it = NULL;
  void *m;
  while ((m = api.class_get_methods(k, &it)) != NULL) {
    const uintptr_t code = il2cpp_method_code(m);
    char rt[256];
    type_name(api.method_get_return_type(m), rt, sizeof rt);
    uint32_t ifl = 0;
    const uint32_t flags = api.method_get_flags ? api.method_get_flags(m, &ifl) : 0;
    fprintf(f, "  method %s%s %s(", (flags & 0x10) ? "static " : "", rt, api.method_get_name(m));
    const uint32_t pc = api.method_get_param_count(m);
    for (uint32_t i = 0; i < pc; i++) {
      char pt[256];
      type_name(api.method_get_param(m, i), pt, sizeof pt);
      const char *pn = api.method_get_param_name ? api.method_get_param_name(m, i) : NULL;
      fprintf(f, "%s%s %s", i ? ", " : "", pt, pn ? pn : "");
    }
    if (code && code >= g_il2cpp->base && code < g_il2cpp->base + g_il2cpp->size)
      fprintf(f, ") ; rva 0x%lx\n", (unsigned long)(code - g_il2cpp->base));
    else
      fprintf(f, ") ; %s\n", code ? "outside libil2cpp" : "no code");
  }
}

void il2cpp_dump_once(void) {
  if (!s_ok || !config.il2cpp_dump)
    return;
  char path[600], stamp[128];
  snprintf(path, sizeof path, "%s/il2cpp_dump.txt", path_internal());
  snprintf(stamp, sizeof stamp, "# apk %llu", (unsigned long long)apkfile_size());
  FILE *f = fopen(path, "r");
  if (f) {
    char line[128] = "";
    fgets(line, sizeof line, f);
    fclose(f);
    if (!strncmp(line, stamp, strlen(stamp)))
      return;
  }
  void *dom = api.domain_get();
  if (!dom)
    return;
  const u64 t0 = armGetSystemTick();
  char tmp[620];
  snprintf(tmp, sizeof tmp, "%s.part", path);
  f = fopen(tmp, "w");
  if (!f)
    return;
  setvbuf(f, NULL, _IOFBF, 1 << 20);
  fprintf(f, "%s\n# every class of the game's assemblies; rva = offset in libil2cpp.so\n", stamp);
  size_t n = 0;
  void **as = api.domain_get_assemblies(dom, &n);
  size_t classes = 0;
  for (size_t i = 0; as && i < n; i++) {
    void *img = api.assembly_get_image(as[i]);
    if (!img)
      continue;
    const size_t cc = api.image_get_class_count ? api.image_get_class_count(img) : 0;
    fprintf(f, "\n\n==== %s (%zu classes)\n", api.image_get_name(img), cc);
    for (size_t c = 0; c < cc; c++) {
      void *k = api.image_get_class(img, c);
      if (k) {
        dump_class(f, k);
        classes++;
      }
    }
  }
  fclose(f);
  remove(path);
  rename(tmp, path);
  log_printf("[il2cpp] dumped %zu classes of %zu assemblies in %llu ms", classes, n,
             (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000ull));
}

void *il2cpp_object_class(void *obj) {
  return s_ok && obj && api.object_get_class ? api.object_get_class(obj) : NULL;
}

void *il2cpp_string(const char *utf8) {
  return api.string_new && utf8 ? api.string_new(utf8) : NULL;
}

void *il2cpp_new(void *klass) {
  return s_ok && klass && api.object_new ? api.object_new(klass) : NULL;
}

void *il2cpp_keep(void *obj) {
  if (obj && api.gchandle_new)
    api.gchandle_new(obj, 0);
  return obj;
}

uint32_t il2cpp_hold(void *obj) {
  return obj && api.gchandle_new ? api.gchandle_new(obj, 0) : 0;
}

void il2cpp_release(uint32_t handle) {
  if (handle && api.gchandle_free)
    api.gchandle_free(handle);
}

void *il2cpp_new_array(void *element_class, size_t length) {
  return s_ok && element_class && api.array_new ? api.array_new(element_class, (uintptr_t)length) : NULL;
}

void *il2cpp_type_object(void *klass) {
  void *t = s_ok && klass && api.class_get_type ? api.class_get_type(klass) : NULL;
  return t && api.type_get_object ? api.type_get_object(t) : NULL;
}

int il2cpp_static_get(void *klass, const char *name, void *out) {
  void *f = s_ok && klass && api.class_get_field_from_name ? api.class_get_field_from_name(klass, name) : NULL;
  if (!f || !api.field_static_get_value)
    return -1;
  api.field_static_get_value(f, out);
  return 0;
}

int il2cpp_static_set(void *klass, const char *name, void *value) {
  void *f = s_ok && klass && api.class_get_field_from_name ? api.class_get_field_from_name(klass, name) : NULL;
  if (!f || !api.field_static_set_value)
    return -1;
  api.field_static_set_value(f, value);
  return 0;
}

void *il2cpp_array_like(void *array, size_t length) {
  void *k = il2cpp_object_class(array);
  return k && api.array_new_specific ? api.array_new_specific(k, (uintptr_t)length) : NULL;
}

void *il2cpp_invoke(void *method, void *obj, void **params, char *exc_name, size_t n) {
  if (exc_name && n)
    exc_name[0] = '\0';
  if (!s_ok || !method || !api.runtime_invoke)
    return NULL;
  void *exc = NULL;
  void *r = api.runtime_invoke(method, obj, params, &exc);
  if (exc && exc_name && n)
    snprintf(exc_name, n, "%s", il2cpp_class_name(il2cpp_object_class(exc)));
  return exc ? NULL : r;
}
