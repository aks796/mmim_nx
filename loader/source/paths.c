/* paths.c -- see paths.h.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

#include "config.h"
#include "paths.h"

static char s_base[256], s_config[300], s_internal[300], s_lib[300], s_srv[300], s_tmp[300];
static char s_data[300], s_files[300], s_cache[300], s_ext[300], s_nabe[300];
static char s_apk[512];

// strip a "device:" prefix and the trailing "/<file>" component
static void dir_of(const char *in, char *out, size_t n) {
  const char *colon = strchr(in, ':');
  snprintf(out, n, "%s", colon ? colon + 1 : in);
  char *slash = strrchr(out, '/');
  if (slash)
    *slash = '\0';
}

void paths_init(const char *argv0) {
  s_base[0] = '\0';
  if (argv0 && argv0[0])
    dir_of(argv0, s_base, sizeof s_base);
  if (!s_base[0]) {
    char cwd[256];
    if (getcwd(cwd, sizeof cwd)) {
      const char *colon = strchr(cwd, ':');
      snprintf(s_base, sizeof s_base, "%s", colon ? colon + 1 : cwd);
    }
  }
  // no argv[0] (an emulator starting the .nro directly) leaves the cwd, the
  // card root: use the default folder then
  if (!s_base[0] || !strcmp(s_base, "/"))
    snprintf(s_base, sizeof s_base, "%s", PORT_FOLDER);
  if (s_base[0] != '/') {
    char tmp[256];
    snprintf(tmp, sizeof tmp, "/%s", s_base);
    snprintf(s_base, sizeof s_base, "%s", tmp);
  }
  snprintf(s_config,   sizeof s_config,   "%s/%s", s_base, CONFIG_NAME);
  snprintf(s_internal, sizeof s_internal, "%s/internal", s_base);
  snprintf(s_lib,      sizeof s_lib,      "%s/internal/lib", s_base);
  snprintf(s_srv,      sizeof s_srv,      "%s/internal/srv", s_base);
  snprintf(s_tmp,      sizeof s_tmp,      "%s/internal/tmp", s_base);
  snprintf(s_data,     sizeof s_data,     "%s/data", s_base);
  snprintf(s_files,    sizeof s_files,    "%s/data/files", s_base);
  snprintf(s_cache,    sizeof s_cache,    "%s/data/cache", s_base);
  snprintf(s_ext,      sizeof s_ext,      "%s/ext", s_base);
  snprintf(s_nabe,     sizeof s_nabe,     "%s/data/files/Nabe", s_base);
}

const char *path_base(void)     { return s_base; }
const char *path_config(void)   { return s_config; }
const char *path_internal(void) { return s_internal; }
const char *path_lib(void)      { return s_lib; }
const char *path_srv(void)      { return s_srv; }
const char *path_tmp(void)      { return s_tmp; }
const char *path_data(void)     { return s_data; }
const char *path_files(void)    { return s_files; }
const char *path_cache(void)    { return s_cache; }
const char *path_ext(void)      { return s_ext; }
const char *path_nabe(void)     { return s_nabe; }
const char *path_apk(void)      { return s_apk; }

void paths_set_apk(const char *path) {
  snprintf(s_apk, sizeof s_apk, "%s", path ? path : "");
}

void paths_make_dirs(void) {
  const char *dirs[] = {
    s_internal, s_lib, s_srv, s_tmp, s_data, s_files, s_cache, s_ext, s_nabe,
  };
  for (unsigned i = 0; i < sizeof(dirs) / sizeof(*dirs); i++)
    mkdir(dirs[i], 0777);
  char p[340];
  snprintf(p, sizeof p, "%s/shared_prefs", s_data);
  mkdir(p, 0777);

  // /proc (path_from_android maps it to internal/proc): the protector in
  // libil2cpp stats and reads /proc/self/status, which every Android process
  // has, for a debugger (TracerPid). Pid matches getpid_fake.
  snprintf(p, sizeof p, "%s/proc", s_internal);
  mkdir(p, 0777);
  snprintf(p, sizeof p, "%s/proc/self", s_internal);
  mkdir(p, 0777);
  // /proc/cpuinfo for open() (fopen answers from memory, libc_shim.c)
  extern const char fake_cpuinfo[];
  snprintf(p, sizeof p, "%s/proc/cpuinfo", s_internal);
  FILE *f = fopen(p, "w");
  if (f) {
    fputs(fake_cpuinfo, f);
    fclose(f);
  }
  snprintf(p, sizeof p, "%s/proc/self/status", s_internal);
  f = fopen(p, "w");
  if (f) {
    fputs("Name:\tMarioVsLuigiAccurate\n"
          "Umask:\t0077\n"
          "State:\tS (sleeping)\n"
          "Tgid:\t1000\n"
          "Ngid:\t0\n"
          "Pid:\t1000\n"
          "PPid:\t600\n"
          "TracerPid:\t0\n"
          "Uid:\t10234\t10234\t10234\t10234\n"
          "Gid:\t10234\t10234\t10234\t10234\n"
          "FDSize:\t256\n"
          "Groups:\t3003 9997 20234 50234\n"
          "Threads:\t40\n"
          "SigQ:\t0/27367\n"
          "SigPnd:\t0000000000000000\n"
          "ShdPnd:\t0000000000000000\n"
          "SigBlk:\t0000000080001204\n"
          "SigIgn:\t0000000000001001\n"
          "SigCgt:\t0000006e400084f8\n"
          "CapInh:\t0000000000000000\n"
          "CapPrm:\t0000000000000000\n"
          "CapEff:\t0000000000000000\n"
          "CapBnd:\t0000000000000000\n"
          "CapAmb:\t0000000000000000\n"
          "NoNewPrivs:\t0\n"
          "Seccomp:\t2\n"
          "Cpus_allowed:\t7\n"
          "Cpus_allowed_list:\t0-2\n"
          "voluntary_ctxt_switches:\t120\n"
          "nonvoluntary_ctxt_switches:\t8\n",
          f);
    fclose(f);
  }
}

// prefix -> replacement directory
static const char *android_roots[][2] = {
  { "/data/data/" GAME_PACKAGE,                                NULL /* data */ },
  { "/data/user/0/" GAME_PACKAGE,                              NULL },
  { "/data/user_de/0/" GAME_PACKAGE,                           NULL },
  { "/storage/emulated/0/Android/data/" GAME_PACKAGE "/files", "ext" },
  { "/sdcard/Android/data/" GAME_PACKAGE "/files",             "ext" },
  { "/storage/emulated/0/Android/data/" GAME_PACKAGE,          "extroot" },
  { "/sdcard/Android/data/" GAME_PACKAGE,                      "extroot" },
  { "/storage/emulated/0/Android/obb/" GAME_PACKAGE,           "obb" },
  { "/sdcard/Android/obb/" GAME_PACKAGE,                       "obb" },
  { "/proc",                                                  "proc" },
};

const char *path_from_android(const char *path) {
  static char bufs[4][768];
  static unsigned rr;
  if (!path || path[0] != '/')
    return path;
  for (unsigned i = 0; i < sizeof(android_roots) / sizeof(*android_roots); i++) {
    const size_t n = strlen(android_roots[i][0]);
    if (strncmp(path, android_roots[i][0], n) != 0 || (path[n] && path[n] != '/'))
      continue;
    const char *kind = android_roots[i][1];
    char *out = bufs[rr++ & 3];
    if (!kind)
      snprintf(out, sizeof bufs[0], "%s%s", s_data, path + n);
    else if (!strcmp(kind, "ext"))
      snprintf(out, sizeof bufs[0], "%s%s", s_ext, path + n);
    else if (!strcmp(kind, "extroot"))
      snprintf(out, sizeof bufs[0], "%s/extroot%s", s_internal, path + n);
    else if (!strcmp(kind, "proc"))
      snprintf(out, sizeof bufs[0], "%s/proc%s", s_internal, path + n);
    else
      snprintf(out, sizeof bufs[0], "%s/obb%s", s_internal, path + n);
    return out;
  }
  return path;
}
