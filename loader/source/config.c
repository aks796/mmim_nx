/* config.c -- simple configuration parser
 *
 * Copyright (C) 2021 Andy Nguyen, fgsfds
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <switch.h>

#include "config.h"

#define CONFIG_VARS \
  CONFIG_VAR_INT(screen_width); \
  CONFIG_VAR_INT(screen_height); \
  CONFIG_VAR_INT(vsync); \
  CONFIG_VAR_INT(fps); \
  CONFIG_VAR_FLOAT(look_speed); \
  CONFIG_VAR_INT(invert_y); \
  CONFIG_VAR_INT(touch); \
  CONFIG_VAR_INT(controller_applet); \
  CONFIG_VAR_INT(keyboard); \
  CONFIG_VAR_INT(rumble); \
  CONFIG_VAR_INT(log_all); \
  CONFIG_VAR_INT(log_jni); \
  CONFIG_VAR_INT(log_fs); \
  CONFIG_VAR_INT(log_net); \
  CONFIG_VAR_INT(log_diag); \
  CONFIG_VAR_INT(il2cpp_dump); \
  CONFIG_VAR_INT(profile); \
  CONFIG_VAR_INT(hitch_log); \
  CONFIG_VAR_INT(apk_cache); \
  CONFIG_VAR_INT(log_ubo); \
  CONFIG_VAR_STR(language); \
  CONFIG_VAR_STR(country); \
  CONFIG_VAR_STR(capture_at); \
  CONFIG_VAR_STR(capture_fbos); \
  CONFIG_VAR_STR(dump_threads_at); \
  CONFIG_VAR_STR(test_script); \
  CONFIG_VAR_STR(trace); \
  CONFIG_VAR_STR(trace_nre); \
  CONFIG_VAR_STR(brk_unity);

Config config;

// actual screen size that is in use right now
int screen_width = 1280;
int screen_height = 720;

static inline void parse_var(const char *name, const char *value) {
  #define CONFIG_VAR_INT(var) if (!strcmp(name, #var)) { config.var = atoi(value); return; }
  #define CONFIG_VAR_STR(var) if (!strcmp(name, #var)) { snprintf(config.var, sizeof(config.var), "%s", value); return; }
  #define CONFIG_VAR_FLOAT(var) if (!strcmp(name, #var)) { config.var = strtof(value, NULL); return; }
  CONFIG_VARS
  #undef CONFIG_VAR_INT
  #undef CONFIG_VAR_STR
  #undef CONFIG_VAR_FLOAT
}

// The console's language as a Java Locale (language, country).
static void console_locale(char *lang, size_t ll, char *country, size_t cl) {
  u64 lcode = 0;
  SetLanguage sl = SetLanguage_ENUS;
  if (R_SUCCEEDED(setInitialize())) {
    if (R_SUCCEEDED(setGetSystemLanguage(&lcode)))
      setMakeLanguage(lcode, &sl);
    setExit();
  }
  const char *l = "en", *c = "US";
  switch (sl) {
    case SetLanguage_ENGB:  l = "en"; c = "GB"; break;
    case SetLanguage_FR:    l = "fr"; c = "FR"; break;
    case SetLanguage_FRCA:  l = "fr"; c = "CA"; break;
    case SetLanguage_DE:    l = "de"; c = "DE"; break;
    case SetLanguage_IT:    l = "it"; c = "IT"; break;
    case SetLanguage_ES:    l = "es"; c = "ES"; break;
    case SetLanguage_ES419: l = "es"; c = "MX"; break;
    case SetLanguage_NL:    l = "nl"; c = "NL"; break;
    case SetLanguage_PT:    l = "pt"; c = "PT"; break;
    case SetLanguage_PTBR:  l = "pt"; c = "BR"; break;
    case SetLanguage_RU:    l = "ru"; c = "RU"; break;
    case SetLanguage_JA:    l = "ja"; c = "JP"; break;
    case SetLanguage_KO:    l = "ko"; c = "KR"; break;
    case SetLanguage_ZHCN:
    case SetLanguage_ZHHANS:l = "zh"; c = "CN"; break;
    case SetLanguage_ZHTW:
    case SetLanguage_ZHHANT:l = "zh"; c = "TW"; break;
    default: break;
  }
  snprintf(lang, ll, "%s", l);
  snprintf(country, cl, "%s", c);
}

static void set_defaults(Config *c) {
  memset(c, 0, sizeof(*c));
  c->screen_width = -1;
  c->screen_height = -1;
  c->vsync = 1;
  c->fps = 60;
  c->look_speed = 100.0f;
  c->touch = 1;
  c->controller_applet = 1;
  c->keyboard = 1;
  c->rumble = 1;
  c->apk_cache = 1; // the APK's game data read into RAM at start (apkcache.c)
  c->profile = 0; // the sampler walks other threads' stacks: off unless asked (it read an
                  // uncommitted stack page and killed Ryubing in mvl_nx's bring-up; reads
                  // now stop at the end of the stack's memory region)
#ifdef MVL_GAME_MMIM
  c->hitch_log = 1; // MMIM HW rounds: frames over 50 ms say where they went ([hitch], prof.c)
#endif
  console_locale(c->language, sizeof(c->language), c->country, sizeof(c->country));
#ifdef MVL_GAME_MMIM
  // where C# raises NullReferenceExceptions, once per place (trace.c): an
  // IL2CPP release build logs them without a stack trace
  snprintf(c->trace_nre, sizeof(c->trace_nre), "auto");
#endif
}

int read_config(const char *file) {
  char line[1024] = { 0 };

  set_defaults(&config);

  FILE *f = fopen(file, "r");
  if (f == NULL)
    return -1;

  do {
    char *name = NULL, *value = NULL, *tmp = NULL;
    if (fgets(line, sizeof(line), f) != NULL) {
      name = line;
      while (*name && isspace((int)*name)) ++name;
      if (name[0] == '#') continue; // skip comments
      for (tmp = name; *tmp && !isspace((int)*tmp); ++tmp);
      if (*tmp != 0) {
        *tmp = 0;
        for (value = tmp + 1; *value && isspace((int)*value); ++value);
        for (tmp = value + strlen(value) - 1; tmp >= value && isspace((int)*tmp); --tmp) *tmp = 0;
        parse_var(name, value);
      }
    }
  } while (!feof(f));

  fclose(f);

  return 0;
}

int write_config(const char *file) {
  FILE *f = fopen(file, "w");
  if (f == NULL)
    return -1;

  fprintf(f,
    "# mvl_nx configuration\n"
    "\n"
    "# Render size. -1 = auto: 720p handheld, 1080p docked.\n"
    "screen_width %d\n"
    "screen_height %d\n"
    "\n"
    "# 1 = cap at 60 fps (swap interval 1), 0 = uncapped.\n"
    "vsync %d\n"
    "\n"
    "# The game's language as a Java locale (default: the console's).\n"
    "language %s\n"
    "country %s\n"
    "\n"
    "# The frame rate the game is held to (Application.targetFrameRate):\n"
    "# 60, or 30 for a steadier picture on busy maps.\n"
    "fps %d\n"
    "\n"
    "# The controller. look_speed: how fast the right stick turns the view, in\n"
    "# percent (100 = default; the game's own sensitivity setting applies\n"
    "# too). invert_y 1: stick up looks down.\n"
    "look_speed %.0f\n"
    "invert_y %d\n"
    "\n"
    "# 1 = the touchscreen works as on a phone (handheld).\n"
    "touch %d\n"
    "\n"
    "# 1 = text fields (name, login, chat) open the Switch keyboard.\n"
    "keyboard %d\n"
    "\n"
    "# 1 = the controller rumbles (ground pounds, damage, quakes), 0 = never.\n"
    "rumble %d\n"
    "\n"
    "# 1 = Split Screen Versus first shows the console's controller screen\n"
    "# (Mario red, Luigi green); 0 = the controllers as they are connected.\n"
    "controller_applet %d\n",
    config.screen_width, config.screen_height, config.vsync,
    config.language, config.country, config.fps, (double)config.look_speed, config.invert_y,
    config.touch, config.keyboard, config.rumble, config.controller_applet);

  fprintf(f,
    "\n"
    "# Debug logging into debug.log (1 = on):\n"
    "#   log_all  every Android log line of the game and Unity\n"
    "#   log_jni  every Java method the game looks up and calls (a method's\n"
    "#            first 10 calls, then its 100th, 1000th, ...)\n"
    "#   log_fs   every file the game opens\n"
    "#   log_net  every connection and host name lookup\n"
    "#   log_diag the game's screens and input, through C# hooks\n"
    "#   profile  every 10 s, where the main and render threads spend their\n"
    "#            time ([prof] lines; it costs a little)\n"
    "#   hitch_log  a frame over 50 ms says where the threads were ([hitch])\n"
    "log_all %d\n"
    "log_jni %d\n"
    "log_fs %d\n"
    "log_net %d\n"
    "log_diag %d\n"
    "profile %d\n"
    "hitch_log %d\n"
    "\n",
    config.log_all, config.log_jni, config.log_fs, config.log_net, config.log_diag, config.profile, config.hitch_log);
  if (config.log_ubo)
    fprintf(f, "# debug: log the game's uniform buffer GL calls\nlog_ubo %d\n", config.log_ubo);
  if (!config.apk_cache)
    fprintf(f, "# debug: 0 = the game's data is read from the SD each time, not kept in RAM\napk_cache %d\n",
            config.apk_cache);
  if (config.il2cpp_dump)
    fprintf(f, "# debug: write internal/il2cpp_dump.txt from the running game\nil2cpp_dump %d\n",
            config.il2cpp_dump);

  // debug keys: only written when set, so a normal config stays short
  if (config.capture_at[0])
    fprintf(f, "\n# debug: save internal/cap_<seconds>.bmp at these times\ncapture_at %s\n",
            config.capture_at);
  if (config.capture_fbos[0])
    fprintf(f, "# debug: also these GL framebuffers (cap_<seconds>_fb<n>.bmp)\ncapture_fbos %s\n",
            config.capture_fbos);
  if (config.dump_threads_at[0])
    fprintf(f, "\n# debug: log where every thread is at these times (seconds)\ndump_threads_at %s\n",
            config.dump_threads_at);
  if (config.test_script[0])
    fprintf(f, "\n# debug: input script\ntest_script %s\n", config.test_script);
  if (config.trace[0])
    fprintf(f, "\n# debug: C# methods logged on entry\ntrace %s\n", config.trace);
  if (config.trace_nre[0])
    fprintf(f, "\n# debug: where NullReferenceExceptions are raised\ntrace_nre %s\n", config.trace_nre);
  if (config.brk_unity[0])
    fprintf(f, "\n# debug: trap at these libunity.so offsets\nbrk_unity %s\n", config.brk_unity);

  fclose(f);

  return 0;
}
