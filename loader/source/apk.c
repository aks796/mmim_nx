/* apk.c -- set the game up from the player's own APK
 *
 * Drop the game's APK into the port folder, whatever it is called. At
 * launch this finds it, checks it is the arm64 build of GAME_PACKAGE
 * and extracts the libraries in lib/arm64-v8a/ to internal/lib/ (about
 * 100 MB, once).
 *
 * Everything else (assets/bin/Data: data.unity3d and the .resource files,
 * stored uncompressed in the APK) Unity reads straight from the APK.
 *
 * internal/apk.stamp remembers which APK the libraries came from (name and
 * size), so nothing is extracted again until it changes.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <switch.h>

#include "apk.h"
#include "apkfile.h"
#include "apkcache.h"
#include "config.h"
#include "log.h"
#include "paths.h"
#include "setup_ui.h"

#define LIB_DIR "lib/arm64-v8a/"

// libraries the port never loads (none: libunity.so names libmain.so as a
// dependency in Unity 6, though the port loads libunity.so itself, as
// libmain's NativeLoader would, and never calls libmain's JNI_OnLoad)
static const char *const skip_libs[] = {
  "",
};

static const char *const NOTE = "Setting up the game from your APK";

static int ends_with(const char *s, const char *suffix) {
  const size_t n = strlen(s), m = strlen(suffix);
  return n >= m && !strcasecmp(s + n - m, suffix);
}

// --- binary AndroidManifest.xml -------------------------------------------------

typedef struct {
  const uint8_t *d;
  size_t n;
  uint32_t count, flags, strings_start, base;
} StringPool;

static int pool_get(const StringPool *sp, uint32_t idx, char *out, size_t outlen) {
  out[0] = '\0';
  if (idx >= sp->count)
    return -1;
  const size_t offs_at = sp->base + 28 + idx * 4;
  if (offs_at + 4 > sp->n)
    return -1;
  uint32_t off;
  memcpy(&off, sp->d + offs_at, 4);
  size_t p = sp->base + sp->strings_start + off;
  if (p >= sp->n)
    return -1;
  if (sp->flags & 0x100) { // UTF-8: utf16 length, utf8 length, bytes
    p += (sp->d[p] & 0x80) ? 2 : 1;
    if (p >= sp->n)
      return -1;
    size_t len = sp->d[p++];
    if (len & 0x80)
      len = ((len & 0x7f) << 8) | sp->d[p++];
    if (p + len > sp->n)
      return -1;
    snprintf(out, outlen, "%.*s", (int)len, (const char *)sp->d + p);
  } else { // UTF-16LE, ASCII is enough here
    uint16_t len;
    memcpy(&len, sp->d + p, 2);
    p += 2;
    size_t i = 0;
    for (; i < len && i + 1 < outlen && p + 2 * i + 1 < sp->n; i++)
      out[i] = (char)sp->d[p + 2 * i];
    out[i] = '\0';
  }
  return 0;
}

static void parse_manifest(const uint8_t *d, size_t n, GameInfo *g) {
  if (n < 16)
    return;
  uint16_t type;
  memcpy(&type, d + 8, 2);
  if (type != 0x0001) // string pool first
    return;
  StringPool sp = { .d = d, .n = n, .base = 8 };
  uint32_t pool_size;
  memcpy(&pool_size, d + 12, 4);
  memcpy(&sp.count, d + 16, 4);
  memcpy(&sp.flags, d + 24, 4);
  memcpy(&sp.strings_start, d + 28, 4);

  for (size_t off = 8 + pool_size; off + 36 <= n;) {
    uint16_t ctype;
    uint32_t csize;
    memcpy(&ctype, d + off, 2);
    memcpy(&csize, d + off + 4, 4);
    if (csize < 8 || off + csize > n)
      break;
    if (ctype == 0x0102) { // start element
      uint32_t name_idx;
      uint16_t attr_count;
      memcpy(&name_idx, d + off + 20, 4);
      memcpy(&attr_count, d + off + 28, 2);
      char name[64];
      pool_get(&sp, name_idx, name, sizeof(name));
      if (!strcmp(name, "manifest")) {
        for (int i = 0; i < attr_count; i++) {
          const size_t at = off + 36 + (size_t)i * 20;
          if (at + 20 > n)
            break;
          uint32_t an, raw, data;
          memcpy(&an, d + at + 4, 4);
          memcpy(&raw, d + at + 8, 4);
          memcpy(&data, d + at + 16, 4);
          char aname[64];
          pool_get(&sp, an, aname, sizeof(aname));
          if (!strcmp(aname, "package") && raw != 0xffffffffu)
            pool_get(&sp, raw, g->package, sizeof(g->package));
          else if (!strcmp(aname, "versionCode"))
            g->version_code = (int)data;
        }
        return;
      }
    }
    off += csize;
  }
}

// --- finding the APK ----------------------------------------------------------------

#define MAX_APKS 16

typedef struct {
  char path[512];
  char name[200];
  uint64_t size;
} Cand;

static int cand_cmp(const void *a, const void *b) {
  const uint64_t x = ((const Cand *)a)->size, y = ((const Cand *)b)->size;
  return x < y ? 1 : x > y ? -1 : 0;
}

static int list_apks(Cand *out) {
  int n = 0;
  DIR *d = opendir(path_base());
  if (!d)
    return 0;
  struct dirent *e;
  while ((e = readdir(d)) != NULL && n < MAX_APKS) {
    if (e->d_name[0] == '.' || !ends_with(e->d_name, ".apk"))
      continue;
    snprintf(out[n].path, sizeof out[n].path, "%s/%s", path_base(), e->d_name);
    snprintf(out[n].name, sizeof out[n].name, "%s", e->d_name);
    struct stat st;
    out[n].size = stat(out[n].path, &st) == 0 ? (uint64_t)st.st_size : 0;
    n++;
  }
  closedir(d);
  qsort(out, n, sizeof(*out), cand_cmp); // the biggest first: the full APK
  return n;
}

typedef enum { APK_OK, APK_NOT_ZIP, APK_OTHER_GAME, APK_NO_ARM64, APK_NO_DATA } ApkCheck;

static ApkCheck check_open_apk(GameInfo *g) {
  memset(g, 0, sizeof(*g));
  const ApkEntry *m = apkfile_find("AndroidManifest.xml");
  if (!m)
    return APK_NOT_ZIP;
  size_t n = 0;
  uint8_t *xml = apkfile_load(m, &n);
  if (xml) {
    parse_manifest(xml, n, g);
    free(xml);
  }
  if (strcmp(g->package, GAME_PACKAGE) != 0)
    return APK_OTHER_GAME;
  if (!apkfile_find(LIB_DIR "libil2cpp.so") || !apkfile_find(LIB_DIR "libunity.so"))
    return APK_NO_ARM64;
  // the data as one bundle (LZ4: data.unity3d) or as files (uncompressed:
  // globalgamemanagers, level0.split0, ...)
  if ((!apkfile_find("assets/bin/Data/data.unity3d") && !apkfile_find("assets/bin/Data/globalgamemanagers")) ||
      !apkfile_find("assets/bin/Data/Managed/Metadata/global-metadata.dat"))
    return APK_NO_DATA;
  return APK_OK;
}

// --- extraction --------------------------------------------------------------------

typedef struct {
  uint64_t done_before, total;
  const char *label;
} Prog;

static void extract_progress(uint64_t done, void *arg) {
  Prog *p = arg;
  const int pm = p->total ? (int)((p->done_before + done) * 1000 / p->total) : 0;
  setup_ui_progress(NOTE, p->label, pm);
}

static int is_wanted_lib(const char *name) {
  if (strncmp(name, LIB_DIR, strlen(LIB_DIR)) != 0 || !ends_with(name, ".so"))
    return 0;
  const char *base = name + strlen(LIB_DIR);
  if (strchr(base, '/'))
    return 0;
  for (unsigned i = 0; i < sizeof(skip_libs) / sizeof(*skip_libs); i++)
    if (!strcmp(base, skip_libs[i]))
      return 0;
  return 1;
}

static void stamp_path(char *out, size_t n) {
  snprintf(out, n, "%s/apk.stamp", path_internal());
}

static void stamp_text(char *out, size_t n) {
  const char *slash = strrchr(apkfile_path(), '/');
  snprintf(out, n, "%s %llu", slash ? slash + 1 : apkfile_path(),
           (unsigned long long)apkfile_size());
}

// the libraries are in place, and from this APK
static int libs_current(void) {
  char p[600], want[400], got[400] = "";
  stamp_path(p, sizeof p);
  FILE *f = fopen(p, "r");
  if (!f)
    return 0;
  if (!fgets(got, sizeof got, f))
    got[0] = '\0';
  fclose(f);
  got[strcspn(got, "\r\n")] = '\0';
  stamp_text(want, sizeof want);
  if (strcmp(got, want) != 0)
    return 0;
  for (int i = 0; i < apkfile_count(); i++) {
    const ApkEntry *e = apkfile_entry_at(i);
    if (!e || !is_wanted_lib(e->name))
      continue;
    char lp[600];
    snprintf(lp, sizeof lp, "%s/%s", path_lib(), e->name + strlen(LIB_DIR));
    struct stat st;
    if (stat(lp, &st) != 0 || (uint64_t)st.st_size != e->usize)
      return 0;
  }
  return 1;
}

static int extract_libs(void) {
  uint64_t total = 0;
  for (int i = 0; i < apkfile_count(); i++) {
    const ApkEntry *e = apkfile_entry_at(i);
    if (e && is_wanted_lib(e->name))
      total += e->usize;
  }
  Prog pr = { 0, total, "" };
  char label[160];
  for (int i = 0; i < apkfile_count(); i++) {
    const ApkEntry *e = apkfile_entry_at(i);
    if (!e || !is_wanted_lib(e->name))
      continue;
    const char *base = e->name + strlen(LIB_DIR);
    snprintf(label, sizeof label, "Extracting %s", base);
    pr.label = label;
    char dest[600];
    snprintf(dest, sizeof dest, "%s/%s", path_lib(), base);
    log_printf("[apk] extracting %s (%llu KB)", base, (unsigned long long)(e->usize >> 10));
    if (apkfile_extract(e, dest, extract_progress, &pr) < 0)
      return -1;
    pr.done_before += e->usize;
  }
  char p[600], text[400];
  stamp_path(p, sizeof p);
  stamp_text(text, sizeof text);
  FILE *f = fopen(p, "w");
  if (f) {
    fprintf(f, "%s\n", text);
    fclose(f);
  }
  return 0;
}

// --- entry point -----------------------------------------------------------------

int apk_setup(GameInfo *info) {
  Cand c[MAX_APKS];
  const int n = list_apks(c);
  if (n == 0) {
    char msg[600];
    snprintf(msg, sizeof msg,
             "Put the game's APK (the arm64-v8a build of\n"
             GAME_PACKAGE ") into this folder:\n\n"
             "  sd:%s/\n\n"
             "Any file name ending in .apk works.",
             path_base());
    setup_ui_message("No APK found", msg);
    log_printf("[apk] no .apk in %s", path_base());
    return -1;
  }
  ApkCheck why = APK_NOT_ZIP;
  const char *chosen = NULL;
  for (int i = 0; i < n; i++) {
    setup_ui_progress(NOTE, "Reading the APK", 0);
    if (apkfile_open(c[i].path) < 0) {
      why = APK_NOT_ZIP;
      continue;
    }
    why = check_open_apk(info);
    log_printf("[apk] %s: package %s, versionCode %d -> %d", c[i].name, info->package,
               info->version_code, (int)why);
    if (why == APK_OK) {
      chosen = c[i].path;
      break;
    }
    apkfile_close();
  }
  if (!chosen) {
    const char *msg =
        why == APK_OTHER_GAME ? "The APK in this folder is not " GAME_TITLE "\n(package " GAME_PACKAGE ")."
      : why == APK_NO_ARM64   ? "This APK has no arm64-v8a libraries.\n"
                                "The 64-bit (arm64-v8a) build is needed."
      : why == APK_NO_DATA    ? "This APK has no game data\n"
                                "(assets/bin/Data). A split APK's base part\n"
                                "is needed, or the full .apk."
                              : "The .apk in this folder could not be read as an APK.";
    setup_ui_message("The APK cannot be used", msg);
    return -1;
  }
  paths_set_apk(chosen);
  if (info->version_code != GAME_VERSION_CODE)
    log_printf("[apk] versionCode %d, the port was made for %d (" GAME_VERSION_NAME ")",
               info->version_code, GAME_VERSION_CODE);

  if (!libs_current()) {
    setup_ui_progress(NOTE, "Extracting the game's libraries", 0);
    if (extract_libs() < 0) {
      setup_ui_message("Setup failed",
                       "The game's libraries could not be extracted from the APK.\n"
                       "Check that the SD card has about 150 MB free.");
      return -1;
    }
    info->extracted = 1;
  }

  apkcache_start(); // the game data into RAM, in the background
  return 0;
}
