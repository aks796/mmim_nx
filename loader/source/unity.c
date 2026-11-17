/* unity.c -- loading and driving the Unity engine (see unity.h)
 *
 * On Android (Unity 6000.3), UnityPlayer's constructor loads the natives
 * through libmain's NativeLoader (libunity.so, then libil2cpp.so, each
 * followed by its JNI_OnLoad) and calls initJni(context, contextType,
 * launchArguments). UnityPlayerForActivityOrService then runs the UnityMain
 * thread: when the surface exists, nativeRecreateGfxState(0, surface) and
 * nativeSendSurfaceChangedEvent; onResume nativeResume, the window focus
 * nativeFocusChanged(true); nativeUnityPlayerSetRunning(true) lets frames
 * run. UnityMain loops: nativeRender() once per frame, false meaning
 * "finish". Our main thread is UnityMain.
 *
 * libunity registers its natives with RegisterNatives in JNI_OnLoad, spread
 * over UnityPlayer, UnityPlayerForActivityOrService and
 * UnityPlayerForGameActivity; jni.c records them by class and name and this
 * file looks each up in all three (the receiver is the one UnityPlayer
 * object the Java side has, jni_unity_player()).
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "config.h"
#include "error.h"
#include "jni.h"
#include "log.h"
#include "paths.h"
#include "so_util.h"
#include "unity.h"

so_module *g_unity;
so_module *g_il2cpp;

static const char *const UP_CLASSES[] = {
  "com/unity3d/player/UnityPlayer",
  "com/unity3d/player/UnityPlayerForActivityOrService",
  "com/unity3d/player/UnityPlayerForGameActivity",
};

static struct {
  // Unity 6: (Context, int contextType, String launchArguments)
  void (*initJni)(void *env, void *thiz, void *ctx, int32_t type, void *args);
  void (*setRunning)(void *env, void *thiz, uint8_t running);
  void (*recreateGfxState)(void *env, void *thiz, int32_t which, void *surface);
  void (*sendSurfaceChanged)(void *env, void *thiz);
  uint8_t (*render)(void *env, void *thiz);
  void (*resume)(void *env, void *thiz);
  uint8_t (*pause)(void *env, void *thiz);
  void (*focusChanged)(void *env, void *thiz, uint8_t focused);
  uint8_t (*injectEvent)(void *env, void *thiz, void *event, int32_t display);
  void (*lowMemory)(void *env, void *thiz);
  void (*applicationUnload)(void *env, void *thiz);
  uint8_t (*done)(void *env, void *thiz);
  void (*setLaunchURL)(void *env, void *thiz, void *url);
  void (*orientationChanged)(void *env, void *thiz, int32_t a, int32_t b);
  void (*muteMasterAudio)(void *env, void *thiz, uint8_t mute);
} up;

static void call_onload(so_module *m) {
  int32_t (*onload)(void *vm, void *reserved) = (void *)so_sym(m, "JNI_OnLoad");
  if (!onload)
    return;
  m->jni_loaded = 1;
  const int32_t v = onload(jni_vm(), NULL);
  log_printf("[unity] %s JNI_OnLoad -> 0x%x", m->soname, v);
  if (v < 0)
    fatal_error("%s refused the Java VM (JNI_OnLoad returned %d).", m->soname, v);
}

static so_module *load_lib(const char *name) {
  const u64 t0 = armGetSystemTick();
  so_module *m = so_load(name);
  if (!m)
    fatal_error("Could not load %s:\n%s", name, so_last_error());
  log_printf("[unity] loaded %s at %p (%llu ms)", name, (void *)m->base,
             (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000ull));
  so_run_inits(m);
  log_printf("[unity] %s constructors done", name);
  call_onload(m);
  return m;
}

void *unity_native(const char *name) {
  for (unsigned i = 0; i < sizeof(UP_CLASSES) / sizeof(*UP_CLASSES); i++) {
    void *fn = jni_native(UP_CLASSES[i], name);
    if (fn)
      return fn;
  }
  return NULL;
}

#define NATIVE(field, name)                                       \
  do {                                                            \
    up.field = unity_native(name);                                 \
    if (!up.field)                                                \
      log_printf("[unity] UnityPlayer.%s was not registered", name); \
  } while (0)

// debug: a trap at each listed libunity.so offset (Ryujinx prints the guest
// stack trace on one), for bisecting a crash the emulator cannot report
static void plant_traps(void) {
  const char *p = config.brk_unity;
  while (*p) {
    char *end;
    const unsigned long off = strtoul(p, &end, 16);
    if (end == p)
      break;
    const uint32_t brk = 0x00000077u; // udf #0x77: Ryujinx reports its address
    const int r = so_write_code((void *)(g_unity->base + off), &brk, 4);
    log_printf("[unity] trap at libunity.so+0x%lx: write %d, reads back %08x", off, r,
               *(volatile uint32_t *)(g_unity->base + off));
    p = end;
    while (*p == ',' || *p == ' ')
      p++;
  }
}

void unity_load(void) {
  g_unity = load_lib("libunity.so");
  plant_traps();
  g_il2cpp = load_lib("libil2cpp.so");

  NATIVE(initJni, "initJni");
  NATIVE(setRunning, "nativeUnityPlayerSetRunning");
  NATIVE(recreateGfxState, "nativeRecreateGfxState");
  NATIVE(sendSurfaceChanged, "nativeSendSurfaceChangedEvent");
  NATIVE(render, "nativeRender");
  NATIVE(resume, "nativeResume");
  NATIVE(pause, "nativePause");
  NATIVE(focusChanged, "nativeFocusChanged");
  NATIVE(injectEvent, "nativeInjectEvent");
  NATIVE(lowMemory, "nativeLowMemory");
  NATIVE(applicationUnload, "nativeApplicationUnload");
  NATIVE(done, "nativeDone");
  NATIVE(setLaunchURL, "nativeSetLaunchURL");
  NATIVE(orientationChanged, "nativeOrientationChanged");
  NATIVE(muteMasterAudio, "nativeMuteMasterAudio");
  if (!up.initJni || !up.render || !up.recreateGfxState)
    fatal_error("libunity.so did not register its UnityPlayer natives.\n"
                "This port needs the Unity 6000.3 build of the game.");
}

void unity_start(void) {
  void *env = jni_env();
  void *thiz = jni_unity_player();
  log_printf("[unity] initJni");
  up.initJni(env, thiz, jni_activity(), 0 /* an Activity */, jni_str(""));
  log_printf("[unity] nativeRecreateGfxState(0, surface)");
  up.recreateGfxState(env, thiz, 0, jni_surface());
  if (up.sendSurfaceChanged)
    up.sendSurfaceChanged(env, thiz);
  if (up.setLaunchURL)
    up.setLaunchURL(env, thiz, NULL);
  if (up.orientationChanged)
    up.orientationChanged(env, thiz, 2, 0); // Configuration.ORIENTATION_LANDSCAPE
  if (up.setRunning)
    up.setRunning(env, thiz, 1);
  log_printf("[unity] nativeResume");
  if (up.resume)
    up.resume(env, thiz);
  log_printf("[unity] nativeFocusChanged(true)");
  if (up.focusChanged)
    up.focusChanged(env, thiz, 1);
}

int unity_frame(void) {
  java_android_frame();
  return up.render(jni_env(), jni_unity_player()) != 0;
}

void unity_pause(int paused) {
  if (paused) {
    if (up.focusChanged)
      up.focusChanged(jni_env(), jni_unity_player(), 0);
    if (up.pause)
      up.pause(jni_env(), jni_unity_player());
  } else {
    if (up.resume)
      up.resume(jni_env(), jni_unity_player());
    if (up.focusChanged)
      up.focusChanged(jni_env(), jni_unity_player(), 1);
  }
}

void unity_focus(int focused) {
  if (up.focusChanged)
    up.focusChanged(jni_env(), jni_unity_player(), focused ? 1 : 0);
}

void unity_quit(void) {
  if (up.pause)
    up.pause(jni_env(), jni_unity_player());
  if (up.done)
    up.done(jni_env(), jni_unity_player());
}

uint8_t unity_inject(void *event) {
  if (!up.injectEvent)
    return 0;
  return up.injectEvent(jni_env(), jni_unity_player(), event, 0);
}
