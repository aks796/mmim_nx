/* il2cpp.h -- the game's C# through the IL2CPP runtime API (il2cpp.c)
 *
 * Class and method names come from the running runtime: libil2cpp.so
 * exports the il2cpp_* API that Unity itself uses. Lookups by name find code addresses for patches, and
 * il2cpp_dump writes every class, field and method (with its offset in
 * libil2cpp.so) to internal/il2cpp_dump.txt for working out new ones.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef IL2CPP_H
#define IL2CPP_H

#include <stddef.h>
#include <stdint.h>

// After libil2cpp.so is loaded: resolve the API.
int il2cpp_api_init(void);
// The runtime is up once Unity has called il2cpp_init (during the first
// frames); 1 when the domain has the game's assemblies.
int il2cpp_ready(void);

// A class by assembly image ("Assembly-CSharp.dll"), namespace and name;
// a nested class as "Outer/Inner" ("SystemBtdManager/<Load>d__23"); a NULL
// image searches every assembly.
void *il2cpp_find_class(const char *image, const char *ns, const char *name);
// Every class of an assembly image (nested ones included), up to max.
int il2cpp_image_classes(const char *image, void **out, int max);
const char *il2cpp_class_namespace(void *klass);
// klass's nested classes, up to max into out; returns how many.
int il2cpp_nested(void *klass, void **out, int max);
// a class's name (no namespace)
const char *il2cpp_class_name(void *klass);
// The code of every method of klass called name (overloads included), up to
// max into out; returns how many.
int il2cpp_method_codes(void *klass, const char *name, uintptr_t *out, int max);
// Every method of klass called name (overloads included) as MethodInfos,
// up to max into out; returns how many.
int il2cpp_methods_named(void *klass, const char *name, void **out, int max);
// A method's signature: static or not, its parameter count, a parameter's
// type name ("System.String").
int il2cpp_method_is_static(void *method);
int il2cpp_method_params(void *method);
void il2cpp_method_param_type(void *method, int i, char *out, size_t n);
// A method's MethodInfo by name and parameter count (-1: any).
void *il2cpp_find_method(void *klass, const char *name, int argc);
// Its compiled code (MethodInfo->methodPointer).
uintptr_t il2cpp_method_code(void *method);
// Both at once; 0 when anything is missing (logged).
uintptr_t il2cpp_code_of(const char *image, const char *ns, const char *cls, const char *method,
                         int argc);
// A C# string (System.String*) as UTF-8 into out ("" for null); returns out.
const char *il2cpp_utf8(const void *str, char *out, size_t n);
// The one class of an image that has every field named (a NULL-terminated
// list; instance or static, its own): the game's class names are
// obfuscated per build, its field names are not. NULL when none or several.
void *il2cpp_find_class_with(const char *image, const char *const *fields);
// An instance field's byte offset, or -1.
int il2cpp_field_offset(void *klass, const char *name);

// An object's class; NULL for NULL.
void *il2cpp_object_class(void *obj);
// A new C# string (System.String*) from UTF-8; NULL for NULL.
void *il2cpp_string(const char *utf8);
// A new object of klass (fields zeroed, no constructor run).
void *il2cpp_new(void *klass);
// obj kept from the collector for good (a GC handle); returns obj.
void *il2cpp_keep(void *obj);
// obj kept from the collector until il2cpp_release(the handle); 0 for NULL.
// A pointer the port keeps across frames must be held: the collector does
// not see the port's memory, and Unity's own wrappers of components and
// GameObjects are only weakly held, so they can be collected and their
// memory reused while the port still points at them.
uint32_t il2cpp_hold(void *obj);
void il2cpp_release(uint32_t handle);
// A new array of element_class elements.
void *il2cpp_new_array(void *element_class, size_t length);
// A class's System.Type (typeof(klass)).
void *il2cpp_type_object(void *klass);
// A static field's value into out (its size); 0, or -1 when there is none.
int il2cpp_static_get(void *klass, const char *name, void *out);
// A static field set from value (its size); 0, or -1 when there is none.
int il2cpp_static_set(void *klass, const char *name, void *value);
// A new array of array's class (byte[] for a byte[]), length elements.
void *il2cpp_array_like(void *array, size_t length);
// A managed method called through the runtime, which catches what it
// throws (C code has no unwind tables for IL2CPP's C++ exceptions): its
// result (boxed for value types), or NULL with exc_name (n bytes) set to the
// exception's class when it threw. params: pointers to each argument (value
// types) or the objects themselves.
void *il2cpp_invoke(void *method, void *obj, void **params, char *exc_name, size_t n);

// Write the dump if it is not there for this APK yet.
void il2cpp_dump_once(void);

#endif
