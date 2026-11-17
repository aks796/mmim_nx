/* main.c -- Mario vs. Luigi (Android arm64, Unity 2022 IL2CPP) on the Switch
 *
 * Start-up:
 *   1. setup screen: find the player's APK, extract its native libraries
 *      (apk.c)
 *   2. the linker's "system": libc & co, GLES/EGL, the NDK (android_ndk.c),
 *      OpenSL ES (opensles.c); the Java world (jni.c, java_*.c)
 *   3. libunity.so + libil2cpp.so with their JNI_OnLoad (unity.c), then what
 *      UnityPlayer does on Android: initJni, the surface, resume, focus
 *   4. the frame loop: input (the controller, touch), nativeRender; Unity
 *      owns EGL and presents by itself
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <EGL/egl.h>
#include <switch.h>

#include "apkcache.h"
#include "saltynx.h"
#include "apk.h"
#include "apkfile.h"
#include "buildinfo.h"
#include "ca_store.h"
#include "config.h"
#include "gamepad.h"
#include "error.h"
#include "gcstop.h"
#include "il2cpp.h"
#include "imports.h"
#include "fs_cache.h"
#include "jni.h"
#include "log.h"
#include "net_shim.h"
#include "paths.h"
#include "pthr.h"
#include "sandbox.h"
#include "setup_ui.h"
#include "so_util.h"
#include "unity.h"
#include "vfs.h"
#include "vmem.h"

// --- heap: all of it goes to newlib; the linker takes module images from it --

void __libnx_initheap(void) {
  void *addr;
  size_t size = 0;
  size_t mem_available = 0, mem_used = 0;

  if (envHasHeapOverride()) {
    addr = envGetHeapOverrideAddr();
    size = envGetHeapOverrideSize();
  } else {
    svcGetInfo(&mem_available, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&mem_used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    if (mem_available > mem_used + 0x200000)
      size = (mem_available - mem_used - 0x200000) & ~0x1FFFFF;
    if (size == 0)
      size = 0x2000000 * 16;
    Result rc = svcSetHeapSize(&addr, size);
    if (R_FAILED(rc))
      diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed));
  }

  extern char *fake_heap_start;
  extern char *fake_heap_end;
  fake_heap_start = (char *)addr;
  fake_heap_end = (char *)addr + size;
}

// --- linker tables ----------------------------------------------------------------

extern DynLibFunction ndk_functions[];
extern size_t ndk_numfunctions;
extern DynLibFunction sles_functions[];
extern size_t sles_numfunctions;
extern DynLibFunction more_functions[];
extern size_t more_numfunctions;
extern DynLibFunction aaudio_functions[];
extern size_t aaudio_numfunctions;

void egl_shim_init(void);
void game_patch_init(void);
int game_patch_il2cpp(void);
void game_patch_frame(unsigned frame);
void watchdog_init(void);
void watchdog_beat(void);
void watchdog_pause(int paused);

void *egl_shim_gl_wrap(const char *name); // egl_shim.c: debug call logs

static uintptr_t gl_ext_resolver(const char *name) {
  void *w = egl_shim_gl_wrap(name);
  if (w)
    return (uintptr_t)w;
  if ((name[0] == 'g' && name[1] == 'l') || strncmp(name, "egl", 3) == 0)
    return (uintptr_t)eglGetProcAddress(name);
  return 0;
}

static void setup_linker(void) {
  update_imports();
  const DynLibFunction *tabs[] = { dynlib_functions, gl_functions, libc_functions, ndk_functions,
                                   sles_functions, more_functions, aaudio_functions };
  const size_t ns[] = { dynlib_numfunctions, gl_numfunctions, libc_numfunctions, ndk_numfunctions,
                        sles_numfunctions, more_numfunctions, aaudio_numfunctions };
  const unsigned ntabs = sizeof(ns) / sizeof(*ns);
  size_t n = 0;
  for (unsigned i = 0; i < ntabs; i++)
    n += ns[i];
  DynLibFunction *all = malloc(n * sizeof(*all));
  size_t k = 0;
  for (unsigned i = 0; i < ntabs; i++) {
    memcpy(all + k, tabs[i], ns[i] * sizeof(*all));
    k += ns[i];
  }
  // debug call logs (config log_ubo): the wrappers over the GL table's own
  for (size_t i = 0; config.log_ubo && i < n; i++) {
    void *w = all[i].symbol ? egl_shim_gl_wrap(all[i].symbol) : NULL;
    if (w)
      all[i].func = (uintptr_t)w;
  }
  so_set_system_symbols(all, n);
  so_set_stdcxx_symbols(dynlib_stdcxx, dynlib_numstdcxx);
  so_set_fallback(gl_ext_resolver);
  egl_shim_init();
  game_patch_init();
  so_add_search_dir(path_lib());
  // the code the port patches: the engine and the game's C# (IL2CPP).
  // Everything else maps read-execute for good (the kernel has 10
  // CodeMemory objects for the whole system: these two and the trampoline
  // pool use 3).
  so_set_patchable("libunity.so");
  so_set_patchable("libil2cpp.so");
}

// --- screen ---------------------------------------------------------------------------

static void pick_screen_size(void) {
  if (config.screen_width > 0 && config.screen_height > 0 && config.screen_width <= 1920 &&
      config.screen_height <= 1080) {
    screen_width = config.screen_width;
    screen_height = config.screen_height;
  } else if (appletGetOperationMode() == AppletOperationMode_Console) {
    screen_width = 1920;
    screen_height = 1080;
  } else {
    screen_width = 1280;
    screen_height = 720;
  }
}

void android_window_init(int w, int h);
void java_android_screen_changed(void);

// --- input ----------------------------------------------------------------------------

#define AKEYCODE_BACK 4

void test_script_init(void);
int test_script_poll(float *x, float *y, int *back);
void egl_swap_time(u64 *total, u64 *max); // egl_shim.c
void egl_shader_time(unsigned *compiles, u64 *compile_ticks, unsigned *links, u64 *link_ticks);
void egl_gpu_stats(u64 *wait_ticks, u64 *wait_max, unsigned *waits, unsigned *draws);
void egl_gpu_time(double *avg_ms, double *max_ms);
int test_script_focus(void);
void test_script_pad(u64 *held, int *lx, int *ly, int *rx, int *ry);

static void poll_input(void) {
  // scripted input (test_script.c) for player 1. Only the script: the
  // controllers are read per player in gamepad.c (this used to be every
  // controller merged, padInitializeAny, which put player 2 and a single
  // Joy-Con's unmapped buttons on player 1: HW round 2, 2026-11-04)
  static u64 s_held;
  PadInput pad = { 0 };
  test_script_pad(&pad.held, &pad.lx, &pad.ly, &pad.rx, &pad.ry);
  pad.down = pad.held & ~s_held;
  s_held = pad.held;

  // debug: scripted input (test_script.c)
  float sx_, sy_;
  int sback;
  const int scripted = test_script_poll(&sx_, &sy_, &sback);
  if (sback)
    input_key(AKEYCODE_BACK, sback == 1);
  if (scripted)
    input_touch(9, 1, sx_ * (float)screen_width / 1280.f, sy_ * (float)screen_height / 720.f);

  // the touchscreen reports in 1280x720 panel pixels
  HidTouchScreenState ts = { 0 };
  const int touched = config.touch && hidGetTouchScreenStates(&ts, 1) && ts.count > 0;
  // the controller works the touch controls (gamepad.c)
  gamepad_update(&pad, screen_width, screen_height, touched);
  if (touched) {
    const float sx = (float)screen_width / 1280.f, sy = (float)screen_height / 720.f;
    for (int i = 0; i < ts.count && i < 10; i++) {
      const int id = ts.touches[i].finger_id % 10;
      input_touch(id, 1, ts.touches[i].x * sx, ts.touches[i].y * sy);
    }
  }
  input_flush();
}

// --- HOME / sleep -----------------------------------------------------------------------

static AppletHookCookie s_hook;
static volatile int s_focus_changed;

static void applet_hook(AppletHookType type, void *param) {
  (void)param;
  if (type == AppletHookType_OnFocusState || type == AppletHookType_OnOperationMode)
    s_focus_changed = 1;
}

// --- main ----------------------------------------------------------------------------------

int main(int argc, char **argv) {
  pthr_register_main();
  paths_init(argc > 0 ? argv[0] : NULL);
  if (!sandbox_init()) {
    char msg[512];
    snprintf(msg, sizeof msg,
             "The port is running from %s,\n"
             "which is not a folder of its own.\n\n"
             "Put mvl_nx.nro in its own folder, like\n"
             PORT_FOLDER "/, together with your APK.",
             path_base()[0] ? path_base() : "/");
    setup_ui_message("Move the port into its own folder", msg);
    setup_ui_close();
    extern void NX_NORETURN __libnx_exit(int rc);
    __libnx_exit(0);
  }
  log_init(path_base());
  // the main thread runs like the game's own threads, time-sliced with them
  // (pthr.c, "scheduling"): it spins waiting for work they do
  // (UnityWebRequest.isDone), and they spin on each other
  pthr_sched_main();
  log_printf("mvl_nx %s build %s", APP_VERSION_STRING, build_stamp());
  read_config(path_config());
  write_config(path_config());
  paths_make_dirs();

  const char *kerr = so_check_kernel();
  if (kerr)
    fatal_error("%s", kerr);

  extern char *fake_heap_start, *fake_heap_end;
  const unsigned heap_mb = (unsigned)((fake_heap_end - fake_heap_start) / (1024 * 1024));
  log_printf("[main] heap %u MB", heap_mb);
  if (heap_mb < 2000)
    fatal_error("Not enough memory (%u MB).\n\n"
                "The game needs the console's full memory:\n"
                "start hbmenu by holding R while launching any\n"
                "installed game, then start this port from there.",
                heap_mb);

  GameInfo game;
  if (apk_setup(&game) != 0) {
    setup_ui_close();
    extern void NX_NORETURN __libnx_exit(int rc);
    __libnx_exit(0);
  }
  vfs_init();
  pick_screen_size();
  log_printf("[main] APK %s (%s, versionCode %d), screen %dx%d", path_apk(), game.package,
             game.version_code, screen_width, screen_height);

  // the environment an Android app process has
  setenv("HOME", path_data(), 1);
  setenv("TMPDIR", path_tmp(), 1);
  setenv("ANDROID_DATA", "/data", 1);
  setenv("ANDROID_ROOT", "/system", 1);
  vmem_init();
  net_init();
  ca_store_load(); // the roots Unity's TLS will trust, and a [tls] line about them
  setup_linker();
  pthr_install_fake_tls(); // before any game code: its stack-guard reads
  jni_init();
  input_init();
  java_android_screen_changed();

  padConfigureInput(8, HidNpadStyleSet_NpadStandard);
  hidInitializeTouchScreen();

  setup_ui_progress("Starting Mario vs. Luigi", "Loading the game's libraries", 0);
  // Android's UI thread and its looper, which Unity takes in JNI_OnLoad
  void ndk_ui_looper_start(void);
  ndk_ui_looper_start();
  unity_load();
  gcstop_init(g_il2cpp);
  il2cpp_api_init();

  // the console's setup screen hands the window to Unity's EGL for good
  setup_ui_close();
  android_window_init(screen_width, screen_height);
  unity_start();

  appletHook(&s_hook, applet_hook, NULL);
  saltynx_init(); // Status Monitor's FPS / RES (saltynx.c; nothing without SaltyNX)
  watchdog_init();
  void prof_start(void);
  prof_start();
  test_script_init();
  int focused = 1;
  unsigned frames = 0;
  u64 t_last = armGetSystemTick();
  u64 engine_ticks = 0, engine_max = 0; // in unity_frame, for [perf]
  while (appletMainLoop()) {
    const int scripted = test_script_focus();
    if (s_focus_changed || scripted >= 0) {
      s_focus_changed = 0;
      const int now = scripted >= 0 ? scripted : appletGetFocusState() == AppletFocusState_InFocus;
      if (now != focused) {
        focused = now;
        log_printf("[main] %s", focused ? "back in focus" : "lost focus (HOME/sleep)");
        watchdog_pause(!focused);
        unity_pause(!focused);
        if (!focused)
          java_prefs_flush(); // what the pause saved, on the SD before HOME can close the port
      }
    }
    if (!focused) {
      svcSleepThread(50000000ull);
      continue;
    }
    poll_input();
    const u64 f0 = armGetSystemTick();
    if (!unity_frame()) {
      log_printf("[main] the engine asked to quit");
      break;
    }
    const u64 fdt = armGetSystemTick() - f0;
    if (armTicksToNs(fdt) > 50000000ull) { // a hitch: where it went (config hitch_log)
      void prof_hitch(u64 t0, u64 t1);
      prof_hitch(f0, f0 + fdt);
    }
    engine_ticks += fdt;
    if (fdt > engine_max)
      engine_max = fdt;
    frames++;
    watchdog_beat();
    {
      void prof_frame(unsigned frames);
      prof_frame(frames);
    }
    if (frames < 600 && game_patch_il2cpp())
      gamepad_il2cpp_up();
    game_patch_frame(frames);
    if (frames == 600)
      log_boot_done();
    if (frames == 300 || (frames % 1800 == 0 && frames < 18000)) {
      if (il2cpp_ready())
        il2cpp_dump_once();
    }
    if (frames % 600 == 0) {
      const u64 now = armGetSystemTick();
      const double secs = (double)armTicksToNs(now - t_last) / 1e9;
      unsigned opens, mb;
      vfs_stats(&opens, &mb);
      size_t vres, vcom;
      vmem_stats(&vres, &vcom);
      log_printf("[main] frame %u: %.1f fps; %u asset files opened, %u MB read; %zu MB reserved, "
                 "%zu MB committed", frames, 600.0 / secs, opens, mb, vres >> 20, vcom >> 20);
      // where a frame goes: the engine's frame (its present included: the
      // wait on the GPU and the display) and the busiest threads' CPU use
      u64 swap, swap_max;
      egl_swap_time(&swap, &swap_max);
      char cpu[400];
      pthr_cpu_report(cpu, sizeof cpu, 6);
      const double ms = 1e3 / 600.0 / 19200000.0;
      unsigned compiles, links;
      u64 compile_t, link_t;
      egl_shader_time(&compiles, &compile_t, &links, &link_t);
      // where the shaders' time went: dkgl's memory, its pack, uam; the
      // GLSL rewrite and the pack lookups
      void dkgl_shader_stats(unsigned *memory, unsigned *hits, unsigned *compiled, uint64_t *rewrite_ticks,
                             uint64_t *lookup_ticks);
      unsigned sh_mem, sh_hits, sh_comp;
      uint64_t sh_rw, sh_lk;
      dkgl_shader_stats(&sh_mem, &sh_hits, &sh_comp, &sh_rw, &sh_lk);
      u64 gw, gw_max;
      unsigned waits, draws;
      egl_gpu_stats(&gw, &gw_max, &waits, &draws);
      double gpu_ms, gpu_max;
      egl_gpu_time(&gpu_ms, &gpu_max);
      char jtop[200];
      const unsigned jcalls = jni_call_report(jtop, sizeof jtop);
      unsigned checks, hits, top_n;
      const char *top;
      fs_cache_stats(&checks, &hits, &top, &top_n);
      char apkr[96];
      apkcache_report(apkr, sizeof apkr);
      net_report();
      log_printf("[perf] frame %.1f ms: engine %.1f (max %.0f), of it present %.1f (max %.0f); gpu %.1f ms (max %.1f), "
                 "gpu waits %.1f ms (max %.1f, %u), %u draws; shaders %u in %.0f ms (%u known, %u from the pack, %u compiled; "
                 "rewrite %.0f ms, pack %.0f ms), programs %u in %.0f ms; "
                 "java %u/frame (%s); file checks %u/frame, %u%% from memory "
                 "(most: %s, %u); APK reads %s; cpu %s",
                 secs * 1e3 / 600.0, (double)engine_ticks * ms, (double)engine_max * ms * 600.0,
                 (double)swap * ms, (double)swap_max * ms * 600.0, gpu_ms, gpu_max, (double)gw * ms,
                 (double)gw_max * ms * 600.0,
                 waits / 600, draws / 600, compiles, (double)compile_t * ms * 600.0, sh_mem, sh_hits, sh_comp,
                 (double)sh_rw * ms * 600.0, (double)sh_lk * ms * 600.0, links, (double)link_t * ms * 600.0,
                 jcalls / 600, jtop, checks / 600, checks ? hits * 100 / checks : 0, top_n ? top : "-", top_n,
                 apkr, cpu);
      engine_ticks = engine_max = 0;
      t_last = now;
    }
  }

  unity_quit();
  java_prefs_flush();
  net_exit();
  log_flush();
  extern void NX_NORETURN __libnx_exit(int rc);
  __libnx_exit(0);
  return 0;
}
