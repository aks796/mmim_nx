/* dkgl_glsl.c -- GLSL ES rewriter for uam (see dkgl_glsl.h)
 *
 * Only global declarations are touched. The scanner walks the source keeping
 * comments and preprocessor lines verbatim, splits global scope into
 * statements, and rewrites:
 *   - #version 310 es / 300 es        -> #version 320 es
 *   - loose uniforms                  -> one std140 block each, own binding
 *   - samplers without a binding      -> layout(binding=N), declaration order
 *   - samplerExternalOES              -> sampler2D (its #extension dropped)
 *   - inputs/outputs without location -> sequential locations
 * Function bodies are copied without being parsed.
 *
 * GLSL ES 1.00 is translated to ES 3.00 first (dkgl_glsl_es1, at the end).
 */
#include "dkgl_glsl.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- types */

enum { KIND_VALUE, KIND_SAMPLER, KIND_IMAGE, KIND_ATOMIC };

typedef struct {
  const char *name;
  uint32_t gl_type;
  uint8_t kind;
  uint8_t base;
  uint8_t columns;
  uint8_t rows;
  uint8_t target;
  uint8_t shadow;
} TypeInfo;

static const TypeInfo type_table[] = {
    {"float", 0x1406, KIND_VALUE, DKGL_BASE_FLOAT, 1, 1, 0, 0},
    {"vec2", 0x8B50, KIND_VALUE, DKGL_BASE_FLOAT, 1, 2, 0, 0},
    {"vec3", 0x8B51, KIND_VALUE, DKGL_BASE_FLOAT, 1, 3, 0, 0},
    {"vec4", 0x8B52, KIND_VALUE, DKGL_BASE_FLOAT, 1, 4, 0, 0},
    {"int", 0x1404, KIND_VALUE, DKGL_BASE_INT, 1, 1, 0, 0},
    {"ivec2", 0x8B53, KIND_VALUE, DKGL_BASE_INT, 1, 2, 0, 0},
    {"ivec3", 0x8B54, KIND_VALUE, DKGL_BASE_INT, 1, 3, 0, 0},
    {"ivec4", 0x8B55, KIND_VALUE, DKGL_BASE_INT, 1, 4, 0, 0},
    {"uint", 0x1405, KIND_VALUE, DKGL_BASE_UINT, 1, 1, 0, 0},
    {"uvec2", 0x8DC6, KIND_VALUE, DKGL_BASE_UINT, 1, 2, 0, 0},
    {"uvec3", 0x8DC7, KIND_VALUE, DKGL_BASE_UINT, 1, 3, 0, 0},
    {"uvec4", 0x8DC8, KIND_VALUE, DKGL_BASE_UINT, 1, 4, 0, 0},
    {"bool", 0x8B56, KIND_VALUE, DKGL_BASE_BOOL, 1, 1, 0, 0},
    {"bvec2", 0x8B57, KIND_VALUE, DKGL_BASE_BOOL, 1, 2, 0, 0},
    {"bvec3", 0x8B58, KIND_VALUE, DKGL_BASE_BOOL, 1, 3, 0, 0},
    {"bvec4", 0x8B59, KIND_VALUE, DKGL_BASE_BOOL, 1, 4, 0, 0},
    {"mat2", 0x8B5A, KIND_VALUE, DKGL_BASE_FLOAT, 2, 2, 0, 0},
    {"mat3", 0x8B5B, KIND_VALUE, DKGL_BASE_FLOAT, 3, 3, 0, 0},
    {"mat4", 0x8B5C, KIND_VALUE, DKGL_BASE_FLOAT, 4, 4, 0, 0},
    {"mat2x2", 0x8B5A, KIND_VALUE, DKGL_BASE_FLOAT, 2, 2, 0, 0},
    {"mat3x3", 0x8B5B, KIND_VALUE, DKGL_BASE_FLOAT, 3, 3, 0, 0},
    {"mat4x4", 0x8B5C, KIND_VALUE, DKGL_BASE_FLOAT, 4, 4, 0, 0},
    {"mat2x3", 0x8B65, KIND_VALUE, DKGL_BASE_FLOAT, 2, 3, 0, 0},
    {"mat2x4", 0x8B66, KIND_VALUE, DKGL_BASE_FLOAT, 2, 4, 0, 0},
    {"mat3x2", 0x8B67, KIND_VALUE, DKGL_BASE_FLOAT, 3, 2, 0, 0},
    {"mat3x4", 0x8B68, KIND_VALUE, DKGL_BASE_FLOAT, 3, 4, 0, 0},
    {"mat4x2", 0x8B69, KIND_VALUE, DKGL_BASE_FLOAT, 4, 2, 0, 0},
    {"mat4x3", 0x8B6A, KIND_VALUE, DKGL_BASE_FLOAT, 4, 3, 0, 0},
    {"sampler2D", 0x8B5E, KIND_SAMPLER, DKGL_BASE_FLOAT, 1, 4, DKGL_TEX_2D, 0},
    {"sampler3D", 0x8B5F, KIND_SAMPLER, DKGL_BASE_FLOAT, 1, 4, DKGL_TEX_3D, 0},
    {"samplerCube", 0x8B60, KIND_SAMPLER, DKGL_BASE_FLOAT, 1, 4, DKGL_TEX_CUBE, 0},
    {"sampler2DShadow", 0x8B62, KIND_SAMPLER, DKGL_BASE_FLOAT, 1, 1, DKGL_TEX_2D, 1},
    {"sampler2DArray", 0x8DC1, KIND_SAMPLER, DKGL_BASE_FLOAT, 1, 4, DKGL_TEX_2D_ARRAY, 0},
    {"sampler2DArrayShadow", 0x8DC4, KIND_SAMPLER, DKGL_BASE_FLOAT, 1, 1, DKGL_TEX_2D_ARRAY, 1},
    {"samplerCubeShadow", 0x8DC5, KIND_SAMPLER, DKGL_BASE_FLOAT, 1, 1, DKGL_TEX_CUBE, 1},
    {"samplerBuffer", 0x8DC2, KIND_SAMPLER, DKGL_BASE_FLOAT, 1, 4, DKGL_TEX_BUFFER, 0},
    {"samplerCubeArray", 0x900C, KIND_SAMPLER, DKGL_BASE_FLOAT, 1, 4, DKGL_TEX_CUBE_ARRAY, 0},
    {"samplerCubeArrayShadow", 0x900D, KIND_SAMPLER, DKGL_BASE_FLOAT, 1, 1, DKGL_TEX_CUBE_ARRAY, 1},
    {"sampler2DMS", 0x9108, KIND_SAMPLER, DKGL_BASE_FLOAT, 1, 4, DKGL_TEX_2D_MS, 0},
    {"sampler2DMSArray", 0x910B, KIND_SAMPLER, DKGL_BASE_FLOAT, 1, 4, DKGL_TEX_2D_MS_ARRAY, 0},
    {"samplerExternalOES", 0x8D66, KIND_SAMPLER, DKGL_BASE_FLOAT, 1, 4, DKGL_TEX_2D, 0},
    {"isampler2D", 0x8DCA, KIND_SAMPLER, DKGL_BASE_INT, 1, 4, DKGL_TEX_2D, 0},
    {"isampler3D", 0x8DCB, KIND_SAMPLER, DKGL_BASE_INT, 1, 4, DKGL_TEX_3D, 0},
    {"isamplerCube", 0x8DCC, KIND_SAMPLER, DKGL_BASE_INT, 1, 4, DKGL_TEX_CUBE, 0},
    {"isampler2DArray", 0x8DCF, KIND_SAMPLER, DKGL_BASE_INT, 1, 4, DKGL_TEX_2D_ARRAY, 0},
    {"isamplerBuffer", 0x8DD0, KIND_SAMPLER, DKGL_BASE_INT, 1, 4, DKGL_TEX_BUFFER, 0},
    {"isamplerCubeArray", 0x900E, KIND_SAMPLER, DKGL_BASE_INT, 1, 4, DKGL_TEX_CUBE_ARRAY, 0},
    {"isampler2DMS", 0x9109, KIND_SAMPLER, DKGL_BASE_INT, 1, 4, DKGL_TEX_2D_MS, 0},
    {"usampler2D", 0x8DD2, KIND_SAMPLER, DKGL_BASE_UINT, 1, 4, DKGL_TEX_2D, 0},
    {"usampler3D", 0x8DD3, KIND_SAMPLER, DKGL_BASE_UINT, 1, 4, DKGL_TEX_3D, 0},
    {"usamplerCube", 0x8DD4, KIND_SAMPLER, DKGL_BASE_UINT, 1, 4, DKGL_TEX_CUBE, 0},
    {"usampler2DArray", 0x8DD7, KIND_SAMPLER, DKGL_BASE_UINT, 1, 4, DKGL_TEX_2D_ARRAY, 0},
    {"usamplerBuffer", 0x8DD8, KIND_SAMPLER, DKGL_BASE_UINT, 1, 4, DKGL_TEX_BUFFER, 0},
    {"usamplerCubeArray", 0x900F, KIND_SAMPLER, DKGL_BASE_UINT, 1, 4, DKGL_TEX_CUBE_ARRAY, 0},
    {"usampler2DMS", 0x910A, KIND_SAMPLER, DKGL_BASE_UINT, 1, 4, DKGL_TEX_2D_MS, 0},
    {"image2D", 0x904D, KIND_IMAGE, DKGL_BASE_FLOAT, 1, 4, DKGL_TEX_2D, 0},
    {"image3D", 0x904E, KIND_IMAGE, DKGL_BASE_FLOAT, 1, 4, DKGL_TEX_3D, 0},
    {"imageCube", 0x9050, KIND_IMAGE, DKGL_BASE_FLOAT, 1, 4, DKGL_TEX_CUBE, 0},
    {"imageBuffer", 0x9051, KIND_IMAGE, DKGL_BASE_FLOAT, 1, 4, DKGL_TEX_BUFFER, 0},
    {"image2DArray", 0x9053, KIND_IMAGE, DKGL_BASE_FLOAT, 1, 4, DKGL_TEX_2D_ARRAY, 0},
    {"imageCubeArray", 0x9054, KIND_IMAGE, DKGL_BASE_FLOAT, 1, 4, DKGL_TEX_CUBE_ARRAY, 0},
    {"iimage2D", 0x9058, KIND_IMAGE, DKGL_BASE_INT, 1, 4, DKGL_TEX_2D, 0},
    {"iimage3D", 0x9059, KIND_IMAGE, DKGL_BASE_INT, 1, 4, DKGL_TEX_3D, 0},
    {"iimageCube", 0x905B, KIND_IMAGE, DKGL_BASE_INT, 1, 4, DKGL_TEX_CUBE, 0},
    {"iimageBuffer", 0x905C, KIND_IMAGE, DKGL_BASE_INT, 1, 4, DKGL_TEX_BUFFER, 0},
    {"iimage2DArray", 0x905E, KIND_IMAGE, DKGL_BASE_INT, 1, 4, DKGL_TEX_2D_ARRAY, 0},
    {"uimage2D", 0x9063, KIND_IMAGE, DKGL_BASE_UINT, 1, 4, DKGL_TEX_2D, 0},
    {"uimage3D", 0x9064, KIND_IMAGE, DKGL_BASE_UINT, 1, 4, DKGL_TEX_3D, 0},
    {"uimageCube", 0x9066, KIND_IMAGE, DKGL_BASE_UINT, 1, 4, DKGL_TEX_CUBE, 0},
    {"uimageBuffer", 0x9067, KIND_IMAGE, DKGL_BASE_UINT, 1, 4, DKGL_TEX_BUFFER, 0},
    {"uimage2DArray", 0x9069, KIND_IMAGE, DKGL_BASE_UINT, 1, 4, DKGL_TEX_2D_ARRAY, 0},
    {"atomic_uint", 0x92DB, KIND_ATOMIC, DKGL_BASE_UINT, 1, 1, 0, 0},
};

static const TypeInfo *find_type(const char *name, size_t len) {
  for (size_t i = 0; i < sizeof(type_table) / sizeof(type_table[0]); i++)
    if (strlen(type_table[i].name) == len &&
        !memcmp(type_table[i].name, name, len))
      return &type_table[i];
  return NULL;
}

/* -------------------------------------------------------------- output */

typedef struct {
  char *data;
  size_t len;
  size_t cap;
  int failed;
} Buf;

static void buf_reserve(Buf *b, size_t extra) {
  if (b->failed || b->len + extra + 1 <= b->cap)
    return;
  size_t cap = b->cap ? b->cap : 4096;
  while (cap < b->len + extra + 1)
    cap *= 2;
  char *data = realloc(b->data, cap);
  if (!data) {
    b->failed = 1;
    return;
  }
  b->data = data;
  b->cap = cap;
}

static void buf_put(Buf *b, const char *s, size_t n) {
  buf_reserve(b, n);
  if (b->failed)
    return;
  memcpy(b->data + b->len, s, n);
  b->len += n;
  b->data[b->len] = '\0';
}

static void buf_printf(Buf *b, const char *fmt, ...) {
  char tmp[512];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  if (n < 0)
    return;
  if ((size_t)n >= sizeof(tmp))
    n = sizeof(tmp) - 1;
  buf_put(b, tmp, (size_t)n);
}

/* ------------------------------------------------------------ scanning */

typedef struct {
  const char *s;
  size_t len;
} Tok;

#define MAX_TOKS 96

typedef struct {
  size_t begin;      /* source offset of the statement */
  size_t end;        /* one past its final ';' */
  int kind;          /* STMT_* */
  /* parsed declaration */
  Tok layout;        /* contents between layout( ) */
  int has_layout;
  int is_uniform, is_in, is_out, is_buffer, has_location, has_binding;
  Tok qualifiers[8]; /* precision, memory, interpolation, kept verbatim */
  int num_qualifiers;
  Tok type;
  const TypeInfo *info;
  int num_decls;
  Tok decl_name[8];
  Tok decl_array[8]; /* text between [ ], len 0 when not an array */
  Tok block_name;    /* uniform blocks */
} Stmt;

enum { STMT_OTHER, STMT_DECL, STMT_BLOCK };

static int is_ident_start(char c) { return isalpha((unsigned char)c) || c == '_'; }
static int is_ident_char(char c) { return isalnum((unsigned char)c) || c == '_'; }

static int tok_is(Tok t, const char *s) {
  return t.len == strlen(s) && !memcmp(t.s, s, t.len);
}

/* Skips whitespace and comments. */
static size_t skip_space(const char *src, size_t i, size_t n) {
  for (;;) {
    while (i < n && isspace((unsigned char)src[i]))
      i++;
    if (i + 1 < n && src[i] == '/' && src[i + 1] == '/') {
      while (i < n && src[i] != '\n')
        i++;
      continue;
    }
    if (i + 1 < n && src[i] == '/' && src[i + 1] == '*') {
      i += 2;
      while (i + 1 < n && !(src[i] == '*' && src[i + 1] == '/'))
        i++;
      i = i + 2 <= n ? i + 2 : n;
      continue;
    }
    return i;
  }
}

static int tokenize(const char *src, size_t begin, size_t end, Tok *toks,
                    int max) {
  int count = 0;
  size_t i = begin;
  while (i < end && count < max) {
    i = skip_space(src, i, end);
    if (i >= end)
      break;
    size_t start = i;
    if (is_ident_start(src[i])) {
      while (i < end && is_ident_char(src[i]))
        i++;
    } else if (isdigit((unsigned char)src[i])) {
      while (i < end && (is_ident_char(src[i]) || src[i] == '.'))
        i++;
    } else {
      i++;
    }
    toks[count].s = src + start;
    toks[count].len = i - start;
    count++;
  }
  return count;
}

static int parse_int(Tok t, int *value) {
  if (!t.len || t.len > 10)
    return 0;
  char tmp[16];
  memcpy(tmp, t.s, t.len);
  tmp[t.len] = '\0';
  char *endp = NULL;
  long v = strtol(tmp, &endp, 0);
  if (!endp || *endp)
    return 0;
  *value = (int)v;
  return 1;
}

/* Finds key=value inside a layout() argument list. */
static int layout_int(Tok layout, const char *key, int *value) {
  Tok toks[32];
  int n = tokenize(layout.s, 0, layout.len, toks, 32);
  for (int i = 0; i + 2 < n; i++)
    if (tok_is(toks[i], key) && tok_is(toks[i + 1], "="))
      return parse_int(toks[i + 2], value);
  return 0;
}

static int is_qualifier(Tok t) {
  static const char *const words[] = {
      "highp",  "mediump",  "lowp",     "flat",     "smooth",
      "noperspective", "centroid", "sample", "invariant", "precise",
      "coherent", "volatile", "restrict", "readonly", "writeonly",
      "patch",  "const"};
  for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++)
    if (tok_is(t, words[i]))
      return 1;
  return 0;
}

/* Parses one global statement. Anything unexpected leaves it STMT_OTHER,
 * which copies it unchanged. */
static void parse_stmt(const char *src, Stmt *st) {
  Tok toks[MAX_TOKS];
  int n = tokenize(src, st->begin, st->end, toks, MAX_TOKS);
  st->kind = STMT_OTHER;
  /* a long statement is cut at MAX_TOKS: enough for a uniform block's
   * header (Unity's UnityPerDraw has ~30 members), not for a declaration */
  const int truncated = n >= MAX_TOKS;
  if (n < 2)
    return;
  int i = 0;
  for (; i < n; i++) {
    Tok t = toks[i];
    if (tok_is(t, "layout")) {
      if (i + 1 >= n || !tok_is(toks[i + 1], "("))
        return;
      int j = i + 2;
      while (j < n && !tok_is(toks[j], ")"))
        j++;
      if (j >= n)
        return;
      /* a second layout() is merged textually by keeping the last; UE never
       * emits two */
      st->has_layout = 1;
      st->layout.s = toks[i + 2].s;
      st->layout.len = (size_t)(toks[j].s - toks[i + 2].s);
      if (j == i + 2)
        st->layout.len = 0;
      i = j;
    } else if (tok_is(t, "uniform")) {
      st->is_uniform = 1;
    } else if (tok_is(t, "in") || tok_is(t, "attribute")) {
      st->is_in = 1;
    } else if (tok_is(t, "out") || tok_is(t, "varying")) {
      st->is_out = 1;
    } else if (tok_is(t, "inout")) {
      st->is_in = st->is_out = 1;
    } else if (tok_is(t, "buffer") || tok_is(t, "shared")) {
      st->is_buffer = 1;
    } else if (is_qualifier(t)) {
      if (st->num_qualifiers < 8)
        st->qualifiers[st->num_qualifiers++] = t;
    } else {
      break;
    }
  }
  if (!st->is_uniform && !st->is_in && !st->is_out)
    return;
  if (st->is_buffer || i >= n || !is_ident_start(toks[i].s[0]))
    return;
  if (st->has_layout) {
    int unused;
    st->has_location = layout_int(st->layout, "location", &unused);
    st->has_binding = layout_int(st->layout, "binding", &unused);
  }

  /* uniform Block { ... } [instance]; */
  if (i + 1 < n && tok_is(toks[i + 1], "{")) {
    if (!st->is_uniform)
      return;
    st->block_name = toks[i];
    st->kind = STMT_BLOCK;
    return;
  }

  if (truncated)
    return;
  st->type = toks[i++];
  st->info = find_type(st->type.s, st->type.len);
  while (i < n && st->num_decls < 8) {
    if (!is_ident_start(toks[i].s[0]))
      return;
    Tok name = toks[i++];
    Tok array = {NULL, 0};
    if (i < n && tok_is(toks[i], "[")) {
      int j = i + 1;
      while (j < n && !tok_is(toks[j], "]"))
        j++;
      if (j >= n)
        return;
      array.s = toks[i + 1].s;
      array.len = (size_t)(toks[j].s - toks[i + 1].s);
      i = j + 1;
    }
    if (i < n && tok_is(toks[i], "="))
      return; /* initialisers: leave alone */
    st->decl_name[st->num_decls] = name;
    st->decl_array[st->num_decls] = array;
    st->num_decls++;
    if (i < n && tok_is(toks[i], ",")) {
      i++;
      continue;
    }
    if (i < n && tok_is(toks[i], ";")) {
      st->kind = STMT_DECL;
      return;
    }
    return;
  }
}

/* 1 when a global statement that reaches '{' is a declaration (uniform or
 * interface block, struct) rather than a function definition: after an
 * optional layout() and qualifiers, its first word is a storage keyword. */
static int leading_storage(const char *src, size_t begin, size_t end) {
  Tok toks[24];
  int n = tokenize(src, begin, end, toks, 24);
  int i = 0;
  if (i < n && tok_is(toks[i], "layout")) {
    while (i < n && !tok_is(toks[i], ")"))
      i++;
    i++;
  }
  while (i < n && is_qualifier(toks[i]))
    i++;
  if (i >= n)
    return 0;
  return tok_is(toks[i], "uniform") || tok_is(toks[i], "buffer") ||
         tok_is(toks[i], "struct") || tok_is(toks[i], "in") ||
         tok_is(toks[i], "out");
}

/* Splits the source into global statements; preprocessor lines, comments
 * and function bodies are skipped (they are copied verbatim later). */
typedef struct {
  Stmt *stmts;
  int count;
  int cap;
} Scan;

static int push_stmt(Scan *sc, size_t begin, size_t end) {
  if (sc->count == sc->cap) {
    int cap = sc->cap ? sc->cap * 2 : 64;
    Stmt *s = realloc(sc->stmts, (size_t)cap * sizeof(Stmt));
    if (!s)
      return 0;
    sc->stmts = s;
    sc->cap = cap;
  }
  memset(&sc->stmts[sc->count], 0, sizeof(Stmt));
  sc->stmts[sc->count].begin = begin;
  sc->stmts[sc->count].end = end;
  sc->count++;
  return 1;
}

static int scan_source(const char *src, size_t n, Scan *sc) {
  size_t i = 0;
  int at_line_start = 1;
  size_t stmt_begin = (size_t)-1;
  int depth = 0;      /* braces */
  int paren = 0;
  int block_decl = 0; /* inside "uniform X { ... }" at global scope */
  while (i < n) {
    char c = src[i];
    if (at_line_start) {
      size_t j = i;
      while (j < n && (src[j] == ' ' || src[j] == '\t'))
        j++;
      if (j < n && src[j] == '#') {
        size_t line_end = j;
        while (line_end < n && src[line_end] != '\n') {
          if (src[line_end] == '\\' && line_end + 1 < n &&
              src[line_end + 1] == '\n')
            line_end++;
          line_end++;
        }
        i = line_end;
        continue;
      }
    }
    if (c == '\n') {
      at_line_start = 1;
      i++;
      continue;
    }
    if (c == '/' && i + 1 < n && (src[i + 1] == '/' || src[i + 1] == '*')) {
      i = skip_space(src, i, n);
      at_line_start = (i > 0 && src[i - 1] == '\n');
      continue;
    }
    at_line_start = 0;
    if (isspace((unsigned char)c)) {
      i++;
      continue;
    }
    if (depth == 0 && stmt_begin == (size_t)-1)
      stmt_begin = i;
    if (c == '(')
      paren++;
    else if (c == ')')
      paren--;
    else if (c == '{') {
      if (depth == 0 && stmt_begin != (size_t)-1)
        block_decl = leading_storage(src, stmt_begin, i);
      depth++;
    } else if (c == '}') {
      depth--;
      if (depth == 0 && !block_decl) {
        /* end of a function body: not a declaration */
        stmt_begin = (size_t)-1;
      }
      if (depth < 0)
        return 0;
    } else if (c == ';' && depth == 0 && paren == 0) {
      if (stmt_begin != (size_t)-1 && !push_stmt(sc, stmt_begin, i + 1))
        return 0;
      stmt_begin = (size_t)-1;
      block_decl = 0;
    }
    i++;
  }
  return depth == 0;
}

/* ---------------------------------------------------------- std140 */

static uint32_t align_up(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

/* std140 alignment/size for one declaration of type t with count elements
 * (count 0 = not an array). */
static void std140_layout(const TypeInfo *t, uint32_t count, uint32_t *align,
                          uint32_t *size, uint32_t *stride) {
  const uint32_t comp = 4;
  uint32_t vec_align = t->rows == 1 ? comp : (t->rows == 2 ? 2 * comp : 4 * comp);
  uint32_t vec_size = t->rows * comp;
  if (t->columns > 1) {
    /* matrices: array of column vectors with vec4 alignment */
    uint32_t col = 16;
    uint32_t elem = col * t->columns;
    *align = 16;
    *stride = count ? elem : 0;
    *size = elem * (count ? count : 1);
    return;
  }
  if (count) {
    uint32_t elem = align_up(vec_size, 16);
    *align = 16;
    *stride = elem;
    *size = elem * count;
  } else {
    *align = vec_align;
    *stride = 0;
    *size = vec_size;
  }
}

/* std140 size of an explicit uniform block, from its members as written
 * (Unity: "UNITY_UNIFORM mediump vec4 name[4];", preprocessor lines between
 * them). 0 when a member's type is not known. (mvl_nx) */
static uint32_t block_std140_size(const char *src, size_t begin, size_t end, DkglGlslResult *r, int block) {
  const char *b = memchr(src + begin, '{', end - begin);
  if (!b)
    return 0;
  const char *close = src + end;
  while (close > b && *close != '}')
    close--;
  uint32_t off = 0;
  const char *p = b + 1;
  char decl[256];
  size_t dn = 0;
  int line_start = 1;
  while (p < close) {
    if (line_start) {
      const char *q = p;
      while (q < close && (*q == ' ' || *q == '\t'))
        q++;
      if (q < close && *q == '#') { /* skip the preprocessor line */
        while (p < close && *p != '\n')
          p++;
        continue;
      }
    }
    const char c = *p++;
    line_start = c == '\n';
    if (c != ';') {
      if (dn < sizeof(decl) - 1)
        decl[dn++] = c == '\n' || c == '\t' ? ' ' : c;
      continue;
    }
    decl[dn] = 0;
    dn = 0;
    /* words: qualifiers / precision / UNITY_UNIFORM, then type, name[, [N]] */
    char *w[16];
    int nw = 0;
    for (char *t = strtok(decl, " []"); t && nw < 16; t = strtok(NULL, " []"))
      w[nw++] = t;
    int i = 0;
    while (i < nw && (!strcmp(w[i], "UNITY_UNIFORM") || !strcmp(w[i], "uniform") || !strcmp(w[i], "highp") ||
                      !strcmp(w[i], "mediump") || !strcmp(w[i], "lowp") || !strcmp(w[i], "layout")))
      i++;
    if (i + 1 >= nw)
      continue;
    const TypeInfo *t = find_type(w[i], strlen(w[i]));
    if (!t || t->kind != KIND_VALUE)
      return 0;
    const uint32_t count = i + 2 < nw ? (uint32_t)atoi(w[i + 2]) : 0;
    uint32_t align, size, stride;
    std140_layout(t, count, &align, &size, &stride);
    off = align_up(off, align);
    if (r && r->num_members < DKGL_GLSL_MAX_MEMBERS && strncmp(w[i + 1], "Xhlslcc_Unused", 14) != 0) {
      DkglGlslMember *m = &r->members[r->num_members++];
      snprintf(m->name, sizeof(m->name), "%s", w[i + 1]);
      m->block = (uint8_t)block;
      m->gl_type = t->gl_type;
      m->array_size = count ? count : 1;
      m->offset = off;
      m->array_stride = count ? stride : 0;
      m->matrix_stride = t->columns > 1 ? 16 : 0;
    }
    off += size;
  }
  return align_up(off, 16);
}

/* #extension lines to remove: samplerExternalOES becomes sampler2D, and
 * texture buffers are core in ES 3.2 while uam rejects their extension names
 * with ": require" (UE may add such lines at run time). */
static int dropped_extension(Tok name) {
  static const char *const names[] = {
      "GL_OES_EGL_image_external", "GL_OES_EGL_image_external_essl3",
      "GL_EXT_texture_buffer", "GL_OES_texture_buffer"};
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    if (tok_is(name, names[i]))
      return 1;
  return 0;
}

/* ------------------------------------------------------------ rewrite */

static void copy_tok(char *dst, size_t cap, Tok t) {
  size_t n = t.len < cap - 1 ? t.len : cap - 1;
  memcpy(dst, t.s, n);
  dst[n] = '\0';
}

static void put_tok(Buf *b, Tok t) { buf_put(b, t.s, t.len); }

static void put_qualifiers(Buf *b, const Stmt *st) {
  for (int q = 0; q < st->num_qualifiers; q++) {
    put_tok(b, st->qualifiers[q]);
    buf_put(b, " ", 1);
  }
}

int dkgl_glsl_loose_blocks = DKGL_GLSL_MAX_BLOCKS;

static int fail(DkglGlslResult *r, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(r->error, sizeof(r->error), fmt, ap);
  va_end(ap);
  return 0;
}

/* Unity's GLES3 shaders (hlslcc) declare samplers and uniform blocks through
 * macros of their own, defined at the top of each shader:
 *   UNITY_LOCATION(n) uniform mediump sampler2D _MainTex;
 *   UNITY_BINDING(n) uniform UnityPerDraw { ... };
 * which the statement scanner below does not see as declarations, so uam
 * got samplers and blocks without bindings. Their uses (not the #define
 * lines) are expanded first: UNITY_LOCATION(n) to nothing (samplers get
 * bindings in declaration order like any other), UNITY_BINDING(n) to
 * layout(std140). When the shader says UNITY_SUPPORTS_UNIFORM_LOCATION 1
 * (Unity on ES 3.1+), to layout(location = n) and layout(binding = n,
 * std140): the program then answers n for the sampler's location and
 * block's binding, as Unity expects. (mvl_nx) */
/* Object-like macros whose body starts with a storage/precision qualifier
 * (Unity's internal shaders: #define ATTRIBUTE_IN in, #define VARYING_OUT out,
 * #define DECLARE_FRAG_COLOR out vec4 fragColor). The scanner needs the
 * qualifier itself to see an input/output, so their uses are replaced by the
 * body -- what the preprocessor does anyway. (mvl_nx) */
typedef struct {
  char name[48];
  char body[96];
} QualMacro;

static int qual_macros(const char *src, QualMacro *m, int max) {
  static const char *const quals[] = {"in", "out", "attribute", "varying", "uniform", "highp",
                                      "mediump", "lowp", "flat", "centroid", "layout"};
  int n = 0;
  for (const char *p = strstr(src, "#define "); p && n < max; p = strstr(p + 1, "#define ")) {
    const char *q = p + 8;
    while (*q == ' ' || *q == '\t')
      q++;
    const char *ne = q;
    while (isalnum((unsigned char)*ne) || *ne == '_')
      ne++;
    if (ne == q || *ne == '(' || (size_t)(ne - q) >= sizeof(m->name))
      continue;
    const char *b = ne;
    while (*b == ' ' || *b == '\t')
      b++;
    const char *be = strchr(b, '\n');
    if (!be)
      be = b + strlen(b);
    while (be > b && (be[-1] == ' ' || be[-1] == '\t' || be[-1] == '\r'))
      be--;
    if (be == b || (size_t)(be - b) >= sizeof(m->body))
      continue;
    int ok = 0;
    for (unsigned i = 0; i < sizeof(quals) / sizeof(*quals); i++) {
      const size_t ql = strlen(quals[i]);
      if ((size_t)(be - b) >= ql && !strncmp(b, quals[i], ql) &&
          !(isalnum((unsigned char)b[ql]) || b[ql] == '_'))
        ok = 1;
    }
    if (!ok)
      continue;
    snprintf(m[n].name, sizeof(m[n].name), "%.*s", (int)(ne - q), q);
    snprintf(m[n].body, sizeof(m[n].body), "%.*s", (int)(be - b), b);
    n++;
  }
  /* a name #defined more than once (both sides of an #if, like Unity's
   * UNITY_UNIFORM) is left to the preprocessor: which body applies is not
   * known here */
  int keep = 0;
  for (int i = 0; i < n; i++) {
    int defs = 0;
    for (const char *p = strstr(src, "#define "); p; p = strstr(p + 1, "#define ")) {
      const char *q = p + 8;
      while (*q == ' ' || *q == '\t')
        q++;
      const size_t ln = strlen(m[i].name);
      if (!strncmp(q, m[i].name, ln) && !(isalnum((unsigned char)q[ln]) || q[ln] == '_' || q[ln] == '('))
        defs++;
    }
    if (defs == 1)
      m[keep++] = m[i];
  }
  return keep;
}

static char *unity_macros_expand(const char *src) {
  QualMacro qm[16];
  const int nqm = qual_macros(src, qm, 16);
  if (!nqm && !strstr(src, "UNITY_LOCATION(") && !strstr(src, "UNITY_BINDING("))
    return NULL;
  const char *d = strstr(src, "#define UNITY_SUPPORTS_UNIFORM_LOCATION");
  const int explicit_bindings = d && atoi(d + strlen("#define UNITY_SUPPORTS_UNIFORM_LOCATION")) != 0;
  const size_t n = strlen(src);
  char *out = malloc(n * 4 + 256);
  if (!out)
    return NULL;
  size_t o = 0;
  const char *p = src;
  while (*p) {
    const char *eol = strchr(p, '\n');
    const size_t len = eol ? (size_t)(eol - p + 1) : strlen(p);
    const char *q = p;
    while (q < p + len && (*q == ' ' || *q == '\t'))
      q++;
    if (*q == '#') { /* preprocessor lines (the #defines themselves) stay */
      memcpy(out + o, p, len);
      o += len;
      p += len;
      continue;
    }
    for (const char *c = p; c < p + len;) {
      int which = 0;
      if (!strncmp(c, "UNITY_LOCATION(", 15))
        which = 1;
      else if (!strncmp(c, "UNITY_BINDING(", 14))
        which = 2;
      if (!which && nqm && (isalpha((unsigned char)*c) || *c == '_') &&
          (c == src || !(isalnum((unsigned char)c[-1]) || c[-1] == '_'))) {
        const char *we = c;
        while (we < p + len && (isalnum((unsigned char)*we) || *we == '_'))
          we++;
        int hit = -1;
        for (int i = 0; i < nqm && hit < 0; i++)
          if ((size_t)(we - c) == strlen(qm[i].name) && !strncmp(c, qm[i].name, (size_t)(we - c)))
            hit = i;
        if (hit >= 0) {
          o += (size_t)sprintf(out + o, "%s", qm[hit].body);
        } else {
          memcpy(out + o, c, (size_t)(we - c));
          o += (size_t)(we - c);
        }
        c = we;
        continue;
      }
      if (which && (c == src || !(isalnum((unsigned char)c[-1]) || c[-1] == '_'))) {
        const char *a = c + (which == 1 ? 15 : 14);
        const char *b = a;
        while (isdigit((unsigned char)*b))
          b++;
        if (b > a && *b == ')') {
          if (which == 2) {
            if (explicit_bindings)
              o += (size_t)sprintf(out + o, "layout(binding = %.*s, std140)", (int)(b - a), a);
            else
              o += (size_t)sprintf(out + o, "layout(std140)");
          } else if (explicit_bindings) {
            /* the sampler's location, which Unity on ES 3.1+ also uses as
             * its texture unit (glUniform1i(location, location)) */
            o += (size_t)sprintf(out + o, "layout(location = %.*s)", (int)(b - a), a);
          }
          c = b + 1;
          continue;
        }
      }
      out[o++] = *c++;
    }
    p += len;
  }
  out[o] = 0;
  return out;
}

static int rewrite_source(const char *source, int stage, DkglGlslResult *r);

int dkgl_glsl_rewrite(const char *source, int stage, DkglGlslResult *r) {
  char *expanded = source ? unity_macros_expand(source) : NULL;
  const int ok = rewrite_source(expanded ? expanded : source, stage, r);
  free(expanded);
  return ok;
}

static const DkglGlslLink *s_vary_map; /* (callers hold dkgl's lock) */

int dkgl_glsl_rewrite_linked(const char *source, int stage, const DkglGlslLink *vary, DkglGlslResult *r) {
  s_vary_map = vary;
  const int ok = dkgl_glsl_rewrite(source, stage, r);
  s_vary_map = NULL;
  return ok;
}

static int rewrite_source(const char *source, int stage, DkglGlslResult *r) {
  memset(r, 0, sizeof(*r));
  if (!source)
    return fail(r, "no source");
  const size_t n = strlen(source);
  Scan sc;
  memset(&sc, 0, sizeof(sc));
  if (!scan_source(source, n, &sc)) {
    free(sc.stmts);
    return fail(r, "unbalanced braces");
  }
  for (int s = 0; s < sc.count; s++)
    parse_stmt(source, &sc.stmts[s]);

  /* Plan UBO bindings. Explicitly bound blocks keep theirs. Of the free
   * bindings, unbound blocks take the last ones and each loose uniform gets
   * its own block from the first ones; if there are more loose uniforms than
   * bindings left, the tail shares one block (shared_binding). */
  int loose = 0, unbound_blocks = 0;
  int ubo_taken[DKGL_GLSL_MAX_BLOCKS] = {0};
  for (int s = 0; s < sc.count; s++) {
    Stmt *st = &sc.stmts[s];
    int v;
    if (st->kind == STMT_DECL && st->is_uniform && st->info &&
        st->info->kind == KIND_VALUE)
      loose += st->num_decls;
    else if (st->kind == STMT_DECL && st->is_uniform && !st->info)
      return free(sc.stmts), fail(r, "uniform of unknown type");
    else if (st->kind == STMT_BLOCK && st->has_binding &&
             layout_int(st->layout, "binding", &v) && v >= 0 &&
             v < DKGL_GLSL_MAX_BLOCKS)
      ubo_taken[v] = 1;
    else if (st->kind == STMT_BLOCK)
      unbound_blocks++;
  }
  int free_bindings[DKGL_GLSL_MAX_BLOCKS];
  int num_free = 0;
  for (int b = 0; b < DKGL_GLSL_MAX_BLOCKS; b++)
    if (!ubo_taken[b])
      free_bindings[num_free++] = b;
  if (unbound_blocks + (loose ? 1 : 0) > num_free)
    return free(sc.stmts), fail(r, "too many uniform blocks");
  int loose_slots = num_free - unbound_blocks;
  if (loose_slots > dkgl_glsl_loose_blocks)
    loose_slots = dkgl_glsl_loose_blocks > 0 ? dkgl_glsl_loose_blocks : 1;
  const int shared_binding =
      loose > loose_slots ? free_bindings[loose_slots - 1] : -1;
  int next_block_slot = loose_slots;

  Buf out = {0};
  size_t cursor = 0;
  int next_ubo = 0, next_texture = 0;
  int next_in_location = 0, next_out_location = 0;
  int used_in_locations[64] = {0};
  int explicit_out = 0, unlocated_out = 0;
  for (int s = 0; s < sc.count; s++) {
    Stmt *st = &sc.stmts[s];
    int v;
    if (st->kind == STMT_DECL && st->is_in && st->has_location &&
        layout_int(st->layout, "location", &v) && v >= 0 && v < 64)
      used_in_locations[v] = 1;
    if (st->kind == STMT_DECL && st->is_out && !st->is_in) {
      if (st->has_location)
        explicit_out++;
      else
        unlocated_out += st->num_decls;
    }
  }
  /* Image bindings already taken explicitly. */
  int image_taken[DKGL_GLSL_MAX_IMAGES] = {0};
  for (int s = 0; s < sc.count; s++) {
    Stmt *st = &sc.stmts[s];
    int v;
    if (st->kind == STMT_DECL && st->is_uniform && st->info &&
        st->info->kind == KIND_IMAGE && st->has_binding &&
        layout_int(st->layout, "binding", &v) && v >= 0 &&
        v < DKGL_GLSL_MAX_IMAGES)
      image_taken[v] = 1;
  }

  for (int s = 0; s < sc.count; s++) {
    Stmt *st = &sc.stmts[s];
    int rewrite = 0;

    if (st->kind == STMT_DECL && st->is_uniform && st->info &&
        st->info->kind == KIND_VALUE)
      rewrite = 1;
    else if (st->kind == STMT_DECL && st->is_uniform && st->info &&
             (st->info->kind == KIND_SAMPLER || st->info->kind == KIND_IMAGE))
      rewrite = 1;
    else if (st->kind == STMT_BLOCK && !st->has_binding)
      rewrite = 1;
    else if (st->kind == STMT_DECL && (st->is_in || st->is_out) &&
             !st->is_uniform && !st->has_location && stage != DKGL_GLSL_COMPUTE)
      rewrite = 1;

    /* varyings with their own location (kept as written) */
    if (st->kind == STMT_DECL && !st->is_uniform && st->has_location &&
        ((stage == DKGL_GLSL_VERTEX && st->is_out && !st->is_in) ||
         (stage == DKGL_GLSL_FRAGMENT && st->is_in && !st->is_out))) {
      int loc = 0;
      layout_int(st->layout, "location", &loc);
      for (int d = 0; d < st->num_decls && r->num_varyings < 32; d++) {
        copy_tok(r->varyings[r->num_varyings].name, sizeof(r->varyings[0].name), st->decl_name[d]);
        r->varyings[r->num_varyings].location = (int16_t)(loc + d);
        r->num_varyings++;
      }
    }
    /* reflection that does not need a rewrite */
    if (st->kind == STMT_DECL && st->is_in && !st->is_out &&
        stage == DKGL_GLSL_VERTEX && st->has_location) {
      int loc = 0;
      layout_int(st->layout, "location", &loc);
      for (int d = 0; d < st->num_decls; d++) {
        if (r->num_inputs >= DKGL_GLSL_MAX_INPUTS)
          break;
        DkglGlslInput *in = &r->inputs[r->num_inputs++];
        copy_tok(in->name, sizeof(in->name), st->decl_name[d]);
        in->location = loc;
        in->gl_type = st->info ? st->info->gl_type : 0;
        in->base = st->info ? st->info->base : DKGL_BASE_FLOAT;
        in->slots = st->info ? st->info->columns : 1;
        loc += in->slots;
      }
    }
    if (st->kind == STMT_BLOCK && st->has_binding &&
        r->num_blocks < DKGL_GLSL_MAX_BLOCKS) {
      int b = 0;
      layout_int(st->layout, "binding", &b);
      DkglGlslBlock *blk = &r->blocks[r->num_blocks++];
      copy_tok(blk->name, sizeof(blk->name), st->block_name);
      blk->binding = (uint8_t)b;
      blk->gl_binding = (int8_t)b;
      blk->size = block_std140_size(source, st->begin, st->end, r, r->num_blocks - 1);
    }
    if (!rewrite)
      continue;

    buf_put(&out, source + cursor, st->begin - cursor);
    cursor = st->end;

    if (st->kind == STMT_BLOCK) {
      /* layout(..., binding=K) uniform Name { ...as written... */
      const int binding = free_bindings[next_block_slot++];
      DkglGlslBlock *blk = &r->blocks[r->num_blocks++];
      copy_tok(blk->name, sizeof(blk->name), st->block_name);
      blk->binding = (uint8_t)binding;
      blk->gl_binding = -1;
      blk->size = block_std140_size(source, st->begin, st->end, r, r->num_blocks - 1);
      /* Re-emit from "uniform" onward with a binding prepended. */
      const char *u = strstr(source + st->begin, "uniform");
      if (!u || (size_t)(u - source) >= st->end) {
        free(out.data);
        free(sc.stmts);
        return fail(r, "block without uniform keyword");
      }
      if (st->has_layout && st->layout.len)
        buf_printf(&out, "layout(%.*s, binding=%d) ", (int)st->layout.len,
                   st->layout.s, binding);
      else
        buf_printf(&out, "layout(std140, binding=%d) ", binding);
      buf_put(&out, u, (size_t)(source + st->end - u));
      continue;
    }

    if (st->is_uniform && st->info->kind == KIND_VALUE) {
      for (int d = 0; d < st->num_decls; d++) {
        if (r->num_uniforms >= DKGL_GLSL_MAX_UNIFORMS) {
          free(out.data);
          free(sc.stmts);
          return fail(r, "too many uniforms");
        }
        DkglGlslUniform *u = &r->uniforms[r->num_uniforms++];
        int count = 0;
        if (st->decl_array[d].len && !parse_int(st->decl_array[d], &count)) {
          free(out.data);
          free(sc.stmts);
          return fail(r, "non-literal uniform array size");
        }
        copy_tok(u->name, sizeof(u->name), st->decl_name[d]);
        u->gl_type = st->info->gl_type;
        u->base = st->info->base;
        u->columns = st->info->columns;
        u->rows = st->info->rows;
        u->array_size = count ? (uint32_t)count : 1;
        uint32_t align, size, stride;
        std140_layout(st->info, (uint32_t)count, &align, &size, &stride);
        u->stride = stride;
        u->size = size;
        int binding;
        if (shared_binding >= 0 && next_ubo >= loose_slots - 1)
          binding = shared_binding;
        else
          binding = free_bindings[next_ubo++];
        u->binding = (uint8_t)binding;
        u->offset = align_up(r->ubo_size[binding], align);
        r->ubo_size[binding] = u->offset + size;
        if (binding + 1 > r->num_ubos)
          r->num_ubos = binding + 1;
        if (binding == shared_binding) {
          /* members of the shared block are emitted together at the end */
          continue;
        }
        buf_printf(&out, "layout(std140, binding=%d) uniform _dkU%d { ",
                   binding, binding);
        put_qualifiers(&out, st);
        put_tok(&out, st->type);
        buf_put(&out, " ", 1);
        put_tok(&out, st->decl_name[d]);
        if (st->decl_array[d].len) {
          buf_put(&out, "[", 1);
          put_tok(&out, st->decl_array[d]);
          buf_put(&out, "]", 1);
        }
        buf_put(&out, "; };", 4);
      }
      continue;
    }

    if (st->is_uniform) {
      /* sampler / image */
      const int is_image = st->info->kind == KIND_IMAGE;
      for (int d = 0; d < st->num_decls; d++) {
        int count = 1;
        if (st->decl_array[d].len && !parse_int(st->decl_array[d], &count)) {
          free(out.data);
          free(sc.stmts);
          return fail(r, "non-literal sampler array size");
        }
        int binding = 0;
        if (st->has_binding) {
          layout_int(st->layout, "binding", &binding);
          binding += d; /* "uniform image2D a, b" with one binding: rare */
        } else if (is_image) {
          while (binding < DKGL_GLSL_MAX_IMAGES && image_taken[binding])
            binding++;
          if (binding < DKGL_GLSL_MAX_IMAGES)
            image_taken[binding] = 1;
        } else {
          binding = next_texture;
        }
        if (is_image) {
          if (binding + count > DKGL_GLSL_MAX_IMAGES ||
              r->num_images >= DKGL_GLSL_MAX_IMAGES) {
            free(out.data);
            free(sc.stmts);
            return fail(r, "too many images");
          }
        } else if (binding + count > DKGL_GLSL_MAX_SAMPLERS ||
                   r->num_samplers >= DKGL_GLSL_MAX_SAMPLERS) {
          free(out.data);
          free(sc.stmts);
          return fail(r, "too many samplers");
        }
        DkglGlslSampler *smp = is_image ? &r->images[r->num_images++]
                                        : &r->samplers[r->num_samplers++];
        copy_tok(smp->name, sizeof(smp->name), st->decl_name[d]);
        smp->gl_type = st->info->gl_type;
        smp->binding = (uint8_t)binding;
        smp->target = st->info->target;
        smp->shadow = st->info->shadow;
        smp->base = st->info->base;
        smp->array_size = (uint32_t)count;
        {
          int loc = -1;
          smp->gl_location = (int16_t)(!is_image && st->has_location &&
                                               layout_int(st->layout, "location", &loc) && loc >= 0
                                           ? loc + d
                                           : -1);
        }
        if (!is_image && binding + count > next_texture)
          next_texture = binding + count;
        if (!is_image && binding + count > r->num_texture_bindings)
          r->num_texture_bindings = binding + count;

        if (st->has_layout && st->has_binding)
          buf_printf(&out, "layout(%.*s) uniform ", (int)st->layout.len,
                     st->layout.s);
        else if (!is_image && st->has_location)
          /* the location is the program's business (reflection above) */
          buf_printf(&out, "layout(binding=%d) uniform ", binding);
        else if (st->has_layout && st->layout.len)
          buf_printf(&out, "layout(%.*s, binding=%d) uniform ",
                     (int)st->layout.len, st->layout.s, binding);
        else
          buf_printf(&out, "layout(binding=%d) uniform ", binding);
        put_qualifiers(&out, st);
        if (tok_is(st->type, "samplerExternalOES"))
          buf_put(&out, "sampler2D", 9);
        else
          put_tok(&out, st->type);
        buf_put(&out, " ", 1);
        put_tok(&out, st->decl_name[d]);
        if (st->decl_array[d].len) {
          buf_put(&out, "[", 1);
          put_tok(&out, st->decl_array[d]);
          buf_put(&out, "]", 1);
        }
        buf_put(&out, ";", 1);
      }
      continue;
    }

    /* in/out without location */
    {
      const int is_input = st->is_in && !st->is_out;
      for (int d = 0; d < st->num_decls; d++) {
        int location;
        int mapped = -1;
        if (is_input && stage == DKGL_GLSL_FRAGMENT && s_vary_map) {
          for (int v = 0; v < s_vary_map->num_varyings && mapped < 0; v++)
            if (tok_is(st->decl_name[d], s_vary_map->varying_name[v]))
              mapped = s_vary_map->varying_location[v];
          if (mapped < 0) { /* nothing writes it: a location nothing uses */
            mapped = 31;
            while (mapped > 0 && used_in_locations[mapped])
              mapped--;
            for (int v = 0; v < s_vary_map->num_varyings; v++)
              if (s_vary_map->varying_location[v] == mapped && mapped > 0) {
                mapped--;
                v = -1;
              }
          }
        }
        if (mapped >= 0) {
          location = mapped;
          if (location < 64)
            used_in_locations[location] = 1;
        } else if (is_input) {
          while (next_in_location < 64 && used_in_locations[next_in_location])
            next_in_location++;
          location = next_in_location;
          int slots = st->info ? st->info->columns : 1;
          for (int k = 0; k < slots && location + k < 64; k++)
            used_in_locations[location + k] = 1;
          if (stage == DKGL_GLSL_VERTEX &&
              r->num_inputs < DKGL_GLSL_MAX_INPUTS) {
            DkglGlslInput *in = &r->inputs[r->num_inputs++];
            copy_tok(in->name, sizeof(in->name), st->decl_name[d]);
            in->location = location;
            in->gl_type = st->info ? st->info->gl_type : 0;
            in->base = st->info ? st->info->base : DKGL_BASE_FLOAT;
            in->slots = (uint8_t)slots;
          }
        } else {
          if (stage == DKGL_GLSL_FRAGMENT && unlocated_out == 1 &&
              explicit_out == 0)
            location = 0;
          else
            location = next_out_location++;
        }
        if (((stage == DKGL_GLSL_VERTEX && !is_input) || (stage == DKGL_GLSL_FRAGMENT && is_input)) &&
            r->num_varyings < 32) {
          copy_tok(r->varyings[r->num_varyings].name, sizeof(r->varyings[0].name), st->decl_name[d]);
          r->varyings[r->num_varyings].location = (int16_t)location;
          r->num_varyings++;
        }
        if (st->has_layout && st->layout.len)
          buf_printf(&out, "layout(%.*s, location=%d) ", (int)st->layout.len,
                     st->layout.s, location);
        else
          buf_printf(&out, "layout(location=%d) ", location);
        put_qualifiers(&out, st);
        buf_put(&out, is_input ? "in " : "out ", is_input ? 3 : 4);
        put_tok(&out, st->type);
        buf_put(&out, " ", 1);
        put_tok(&out, st->decl_name[d]);
        if (st->decl_array[d].len) {
          buf_put(&out, "[", 1);
          put_tok(&out, st->decl_array[d]);
          buf_put(&out, "]", 1);
        }
        buf_put(&out, ";", 1);
      }
    }
  }

  buf_put(&out, source + cursor, n - cursor);

  if (out.failed) {
    free(out.data);
    free(sc.stmts);
    return fail(r, "out of memory");
  }

  /* Second pass, line by line: the #version line, the shared overflow block
   * right after it (its member types are built in), and the dropped
   * image-external #extension lines. */
  Buf fin = {0};
  const char *o = out.data ? out.data : "";
  size_t olen = out.len;
  size_t pos = 0;
  int version_done = 0;
  while (pos < olen) {
    size_t line_end = pos;
    while (line_end < olen && o[line_end] != '\n')
      line_end++;
    size_t j = pos;
    while (j < line_end && (o[j] == ' ' || o[j] == '\t'))
      j++;
    int handled = 0;
    if (j < line_end && o[j] == '#') {
      Tok toks[6];
      int nt = tokenize(o, j + 1, line_end, toks, 6);
      if (!version_done && nt >= 1 && tok_is(toks[0], "version")) {
        int ver = 0;
        if (nt >= 2)
          parse_int(toks[1], &ver);
        int es = nt >= 3 && tok_is(toks[2], "es");
        if (es && ver >= 300)
          buf_printf(&fin, "#version 320 es");
        else
          buf_put(&fin, o + pos, line_end - pos);
        buf_put(&fin, "\n", 1);
        version_done = 1;
        handled = 1;
        if (shared_binding >= 0) {
          buf_printf(&fin, "layout(std140, binding=%d) uniform _dkU%d {",
                     shared_binding, shared_binding);
          for (int u = 0; u < r->num_uniforms; u++) {
            DkglGlslUniform *un = &r->uniforms[u];
            if (un->binding != shared_binding)
              continue;
            const TypeInfo *t = NULL;
            for (size_t k = 0; k < sizeof(type_table) / sizeof(type_table[0]);
                 k++)
              if (type_table[k].gl_type == un->gl_type &&
                  type_table[k].kind == KIND_VALUE) {
                t = &type_table[k];
                break;
              }
            if (!t)
              continue;
            if (un->stride)
              buf_printf(&fin, " highp %s %s[%u];", t->name, un->name,
                         un->array_size);
            else
              buf_printf(&fin, " highp %s %s;", t->name, un->name);
          }
          buf_put(&fin, " };\n", 4);
        }
      } else if (nt >= 2 && tok_is(toks[0], "extension") &&
                 dropped_extension(toks[1])) {
        handled = 1; /* dropped */
        if (line_end < olen)
          line_end++;
        pos = line_end;
        continue;
      }
    }
    if (!handled) {
      buf_put(&fin, o + pos, line_end - pos);
      if (line_end < olen)
        buf_put(&fin, "\n", 1);
    }
    pos = line_end < olen ? line_end + 1 : line_end;
  }
  free(out.data);
  free(sc.stmts);
  if (fin.failed)
    return free(fin.data), fail(r, "out of memory");

  /* Compute local size, for glGetProgramiv(GL_COMPUTE_WORK_GROUP_SIZE). */
  if (stage == DKGL_GLSL_COMPUTE && fin.data) {
    const char *ls = strstr(fin.data, "local_size_x");
    r->local_size[0] = r->local_size[1] = r->local_size[2] = 1;
    for (int axis = 0; axis < 3 && ls; axis++) {
      const char *key = axis == 0 ? "local_size_x" : axis == 1 ? "local_size_y"
                                                               : "local_size_z";
      const char *k = strstr(fin.data, key);
      if (k) {
        k = strchr(k, '=');
        if (k)
          r->local_size[axis] = atoi(k + 1);
      }
    }
  }

  r->source = fin.data;
  r->source_len = fin.len;
  if (!r->source)
    return fail(r, "empty output");
  for (int b = 0; b < r->num_ubos; b++)
    r->ubo_size[b] = align_up(r->ubo_size[b] ? r->ubo_size[b] : 16, 16);
  return 1;
}

void dkgl_glsl_free(DkglGlslResult *result) {
  free(result->source);
  result->source = NULL;
}

/* ------------------------------------------------------- GLSL ES 1.00 */

int dkgl_glsl_is_es1(const char *source) {
  const size_t n = source ? strlen(source) : 0;
  size_t i = 0;
  while (i < n) {
    i = skip_space(source, i, n);
    if (i >= n || source[i] != '#')
      return 1; /* code before any #version: ES 1.00 */
    Tok toks[4];
    size_t e = i;
    while (e < n && source[e] != '\n')
      e++;
    const int nt = tokenize(source, i + 1, e, toks, 4);
    if (nt >= 1 && tok_is(toks[0], "version")) {
      int v = 0;
      return nt < 2 || !parse_int(toks[1], &v) || v == 100;
    }
    i = e;
  }
  return 1;
}

/* built-in names of ES 1.00 sampling and outputs -> ES 3.00 */
static const char *const es1_renames[][2] = {
    {"texture2D", "texture"},           {"texture2DProj", "textureProj"},
    {"texture2DLod", "textureLod"},     {"texture2DProjLod", "textureProjLod"},
    {"textureCube", "texture"},         {"textureCubeLod", "textureLod"},
    {"texture2DLodEXT", "textureLod"},  {"texture2DProjLodEXT", "textureProjLod"},
    {"textureCubeLodEXT", "textureLod"}, {"texture2DGradEXT", "textureGrad"},
    {"texture2DProjGradEXT", "textureProjGrad"},
    {"textureCubeGradEXT", "textureGrad"}, {"shadow2DEXT", "texture"},
    {"shadow2DProjEXT", "textureProj"}, {"texture3D", "texture"},
    {"texture3DLod", "textureLod"},     {"texture3DProj", "textureProj"},
    {"gl_FragDepthEXT", "gl_FragDepth"}, {"gl_FragColor", "_dk_FragColor"},
    {"gl_FragData", "_dk_FragData"},
    /* every precision full: Mesa ran these ES 1.00 shaders at 32 bits; as
     * ES 3.x, uam lowers mediump to the GPU's 16-bit float instructions
     * (other results, and encodings Ryujinx's translator dies on) */
    {"mediump", "highp"}, {"lowp", "highp"},
};

/* ES 3.x built-in functions that ES 1.00 code may define for itself (Unity
 * brings its own trunc(), for one): redefining a built-in is an error in
 * ES 3.x, so such a function is renamed, calls included. */
static const char *const es3_builtins[] = {
    "trunc", "round", "roundEven", "sinh", "cosh", "tanh", "asinh", "acosh",
    "atanh", "isnan", "isinf", "modf", "fma", "frexp", "ldexp",
    "floatBitsToInt", "floatBitsToUint", "intBitsToFloat", "uintBitsToFloat",
    "packSnorm2x16", "unpackSnorm2x16", "packUnorm2x16", "unpackUnorm2x16",
    "packHalf2x16", "unpackHalf2x16", "packUnorm4x8", "unpackUnorm4x8",
    "packSnorm4x8", "unpackSnorm4x8", "outerProduct", "transpose",
    "determinant", "inverse", "texture", "textureProj", "textureLod",
    "textureGrad", "textureSize", "texelFetch", "textureOffset",
    "bitfieldExtract", "bitfieldInsert", "bitfieldReverse", "bitCount",
    "findLSB", "findMSB", "textureGather", "imageSize"};

typedef struct {
  char name[DKGL_GLSL_NAME];
} Es1Name;

static int es1_slots(const char *type, size_t len) {
  const TypeInfo *t = find_type(type, len);
  return t ? t->columns : 1;
}

char *dkgl_glsl_es1(const char *src, int stage, const DkglGlslLink *link,
                    DkglGlslLink *vout, char *err, size_t err_size) {
  const size_t n = strlen(src);
  if (vout)
    memset(vout, 0, sizeof(*vout));

  /* functions of the source's own named like ES 3.x built-ins */
  Es1Name clash[16];
  int nclash = 0;
  {
    int depth = 0;
    Tok prev = {NULL, 0};
    size_t i = 0;
    while (i < n) {
      i = skip_space(src, i, n);
      if (i >= n)
        break;
      if (src[i] == '#') { /* preprocessor line */
        while (i < n && src[i] != '\n')
          i++;
        continue;
      }
      if (src[i] == '{')
        depth++;
      else if (src[i] == '}')
        depth--;
      if (is_ident_start(src[i])) {
        size_t b = i;
        while (i < n && is_ident_char(src[i]))
          i++;
        Tok t = {src + b, i - b};
        size_t j = skip_space(src, i, n);
        if (depth == 0 && j < n && src[j] == '(' && prev.len &&
            is_ident_start(prev.s[0]) && nclash < 16)
          for (size_t k = 0; k < sizeof(es3_builtins) / sizeof(*es3_builtins); k++)
            if (tok_is(t, es3_builtins[k])) {
              int seen = 0;
              for (int c = 0; c < nclash; c++)
                seen |= tok_is(t, clash[c].name);
              if (!seen)
                copy_tok(clash[nclash++].name, DKGL_GLSL_NAME, t);
              break;
            }
        prev = t;
        continue;
      }
      prev.s = src + i;
      prev.len = 1;
      i++;
    }
  }

  /* object-like macros for the storage keywords (Unity's blit shader:
   * "#define ATTRIBUTE_IN attribute"): their uses are declarations too */
  Es1Name alias[8];
  int alias_kind[8], nalias = 0; /* 1 attribute, 2 varying */
  for (size_t i = 0; i < n && nalias < 8;) {
    size_t e = i;
    while (e < n && src[e] != '\n')
      e++;
    size_t j = i;
    while (j < e && (src[j] == ' ' || src[j] == '\t'))
      j++;
    Tok toks[4];
    if (j < e && src[j] == '#' && tokenize(src, j + 1, e, toks, 4) == 3 &&
        tok_is(toks[0], "define") &&
        (tok_is(toks[2], "attribute") || tok_is(toks[2], "varying"))) {
      copy_tok(alias[nalias].name, DKGL_GLSL_NAME, toks[1]);
      alias_kind[nalias++] = tok_is(toks[2], "attribute") ? 1 : 2;
    }
    i = e + 1;
  }

  /* gl_FragData's size: its highest constant index */
  int frag_data = 0, frag_color = 0;
  for (const char *p = src; stage == DKGL_GLSL_FRAGMENT && (p = strstr(p, "gl_FragData")); p += 11) {
    const char *q = p + 11;
    while (*q == ' ' || *q == '\t')
      q++;
    int idx = DKGL_GLSL_MAX_SAMPLERS; /* a computed index: the most */
    if (*q == '[' && isdigit((unsigned char)q[1]))
      idx = atoi(q + 1);
    if (idx > 7)
      idx = 7;
    if (idx + 1 > frag_data)
      frag_data = idx + 1;
  }
  frag_color = stage == DKGL_GLSL_FRAGMENT && strstr(src, "gl_FragColor") != NULL;

  /* attribute locations: bound ones first, the rest after */
  int used_attr[64] = {0};
  if (stage == DKGL_GLSL_VERTEX && link)
    for (int a = 0; a < link->num_attribs; a++)
      if (link->attrib_index[a] >= 0 && link->attrib_index[a] < 64)
        used_attr[link->attrib_index[a]] = 1;
  int next_attr = 0, next_vary = 0;
  if (stage == DKGL_GLSL_FRAGMENT && link)
    for (int v = 0; v < link->num_varyings; v++)
      if (link->varying_location[v] + 1 > next_vary)
        next_vary = link->varying_location[v] + 1;

  Buf out = {0};
  int depth = 0, version_done = 0;
  size_t i = 0;
  int at_line_start = 1, in_pp = 0; /* in a preprocessor line: renames only */
  while (i < n) {
    const char c = src[i];
    /* comments verbatim */
    if (c == '/' && i + 1 < n && (src[i + 1] == '/' || src[i + 1] == '*')) {
      size_t e = skip_space(src, i, n);
      /* keep only the comment, not the whitespace after it */
      size_t ce = i + 2;
      if (src[i + 1] == '/') {
        while (ce < n && src[ce] != '\n')
          ce++;
      } else {
        while (ce + 1 < n && !(src[ce] == '*' && src[ce + 1] == '/'))
          ce++;
        ce = ce + 2 <= n ? ce + 2 : n;
      }
      (void)e;
      buf_put(&out, src + i, ce - i);
      i = ce;
      continue;
    }
    if (at_line_start) {
      size_t j = i;
      while (j < n && (src[j] == ' ' || src[j] == '\t'))
        j++;
      if (j < n && src[j] == '#') {
        size_t e = j;
        while (e < n && src[e] != '\n')
          e++;
        Tok toks[3];
        const int nt = tokenize(src, j + 1, e, toks, 3);
        if (nt >= 1 && tok_is(toks[0], "version")) {
          buf_printf(&out, "#version 300 es");
          version_done = 1;
          if (frag_color)
            buf_printf(&out, "\nlayout(location=0) out highp vec4 _dk_FragColor;");
          if (frag_data)
            buf_printf(&out, "\nlayout(location=0) out highp vec4 _dk_FragData[%d];",
                       frag_data);
          i = e;
          continue;
        }
        int is_alias = 0;
        for (int a = 0; a < nalias && nt >= 2 && tok_is(toks[0], "define"); a++)
          is_alias |= tok_is(toks[1], alias[a].name);
        if (is_alias)
          buf_put(&out, "//", 2); /* its uses become declarations */
        if (nt >= 1 && tok_is(toks[0], "extension")) {
          if (nt >= 2 && tok_is(toks[1], "GL_EXT_shader_framebuffer_fetch")) {
            snprintf(err, err_size, "framebuffer fetch is not supported");
            free(out.data);
            return NULL;
          }
          buf_put(&out, "//", 2); /* dropped: built in to ES 3.x */
        }
        /* other preprocessor lines: copied, with renames (#define
         * SV_Target0 gl_FragData[0]) */
        at_line_start = 0;
        in_pp = 1;
        i = j;
        continue;
      }
    }
    if (c == '\n') {
      at_line_start = 1;
      if (!(i > 0 && src[i - 1] == '\\'))
        in_pp = 0;
    } else if (!isspace((unsigned char)c)) {
      at_line_start = 0;
    }
    if (c == '{')
      depth++;
    else if (c == '}')
      depth--;
    if (!is_ident_start(c)) {
      buf_put(&out, &c, 1);
      i++;
      continue;
    }
    size_t b = i;
    while (i < n && is_ident_char(src[i]))
      i++;
    Tok t = {src + b, i - b};

    /* attribute / varying declarations at global scope */
    int is_attr = tok_is(t, "attribute");
    int is_vary = tok_is(t, "varying");
    for (int a = 0; a < nalias; a++)
      if (tok_is(t, alias[a].name)) {
        is_attr |= alias_kind[a] == 1;
        is_vary |= alias_kind[a] == 2;
      }
    if (depth == 0 && !in_pp && (is_attr || is_vary)) {
      size_t e = i;
      while (e < n && src[e] != ';')
        e++;
      Tok toks[MAX_TOKS];
      const int nt = tokenize(src, i, e, toks, MAX_TOKS);
      int k = 0;
      Tok quals[4];
      int nq = 0;
      while (k < nt && is_qualifier(toks[k]))
        if (nq < 4 && !tok_is(toks[k], "invariant"))
          quals[nq++] = toks[k++];
        else
          k++;
      if (k >= nt) {
        snprintf(err, err_size, "bad %s declaration", is_attr ? "attribute" : "varying");
        free(out.data);
        return NULL;
      }
      Tok type = toks[k++];
      const char *dir = is_attr || stage == DKGL_GLSL_FRAGMENT ? "in" : "out";
      while (k < nt) {
        Tok name = toks[k++];
        int count = 1;
        if (k + 2 < nt + 1 && k < nt && tok_is(toks[k], "[")) {
          if (k + 1 < nt)
            parse_int(toks[k + 1], &count);
          while (k < nt && !tok_is(toks[k], "]"))
            k++;
          k++;
        }
        const int slots = es1_slots(type.s, type.len) * (count > 0 ? count : 1);
        int loc = -1;
        if (is_attr) {
          for (int a = 0; link && a < link->num_attribs; a++)
            if (tok_is(name, link->attrib_name[a]))
              loc = link->attrib_index[a];
          if (loc < 0) {
            while (next_attr < 64 && used_attr[next_attr])
              next_attr++;
            loc = next_attr;
            for (int s = 0; s < slots && loc + s < 64; s++)
              used_attr[loc + s] = 1;
          }
        } else if (stage == DKGL_GLSL_FRAGMENT) {
          for (int v = 0; link && v < link->num_varyings; v++)
            if (tok_is(name, link->varying_name[v]))
              loc = link->varying_location[v];
          if (loc < 0) {
            loc = next_vary;
            next_vary += slots;
          }
        } else {
          loc = next_vary;
          next_vary += slots;
          if (vout && vout->num_varyings < DKGL_GLSL_MAX_VARYINGS) {
            copy_tok(vout->varying_name[vout->num_varyings], DKGL_GLSL_NAME, name);
            vout->varying_location[vout->num_varyings++] = loc;
          }
        }
        buf_printf(&out, "layout(location=%d) %s ", loc, dir);
        for (int q = 0; q < nq; q++) {
          if (tok_is(quals[q], "mediump") || tok_is(quals[q], "lowp"))
            buf_printf(&out, "highp"); /* (as es1_renames) */
          else
            put_tok(&out, quals[q]);
          buf_put(&out, " ", 1);
        }
        put_tok(&out, type);
        buf_put(&out, " ", 1);
        put_tok(&out, name);
        if (count > 1)
          buf_printf(&out, "[%d]", count);
        buf_put(&out, ";", 1);
        if (k < nt && tok_is(toks[k], ","))
          k++;
        else
          break;
      }
      i = e < n ? e + 1 : n;
      continue;
    }

    int done = 0;
    for (size_t r = 0; r < sizeof(es1_renames) / sizeof(*es1_renames); r++)
      if (tok_is(t, es1_renames[r][0])) {
        buf_printf(&out, "%s", es1_renames[r][1]);
        done = 1;
        break;
      }
    for (int c2 = 0; !done && c2 < nclash; c2++)
      if (tok_is(t, clash[c2].name)) {
        size_t j = skip_space(src, i, n);
        if (j < n && src[j] == '(') {
          buf_printf(&out, "%s_dk", clash[c2].name);
          done = 1;
        }
      }
    if (!done)
      put_tok(&out, t);
  }
  if (out.failed) {
    free(out.data);
    snprintf(err, err_size, "out of memory");
    return NULL;
  }
  if (!version_done) {
    /* no #version: ES 1.00 by default */
    Buf fin = {0};
    buf_printf(&fin, "#version 300 es\n");
    if (frag_color)
      buf_printf(&fin, "layout(location=0) out highp vec4 _dk_FragColor;\n");
    if (frag_data)
      buf_printf(&fin, "layout(location=0) out highp vec4 _dk_FragData[%d];\n", frag_data);
    buf_put(&fin, out.data ? out.data : "", out.len);
    free(out.data);
    return fin.data;
  }
  return out.data;
}
