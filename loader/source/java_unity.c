/* java_unity.c -- Unity's own Java layer: com.unity3d.player.*
 *
 * UnityPlayer (the object the engine's natives get as `thiz`, and
 * UnityPlayer.currentActivity), ReflectionHelper (how C#'s AndroidJavaObject
 * finds Java methods, fields and constructors, and makes AndroidJavaProxy
 * objects), and the small helpers the engine asks about the device.
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
#include "jni.h"
#include "log.h"
#include "net_shim.h"
#include "paths.h"

static JObj *s_player, *s_surface;

JObj *jni_unity_player(void) { return s_player; }
JObj *jni_surface(void) { return s_surface; }

static const char *S(JObj *o) {
  const char *s = jni_utf(o);
  return s ? s : "";
}

JH(ret_true) { JUNUSED; return jv_z(1); }
JH(ret_false) { JUNUSED; return jv_z(0); }
JH(ret_null) { JUNUSED; return jv_l(NULL); }
JH(ret_void) { JUNUSED; return jv_v(); }
JH(ret_zero) { JUNUSED; return jv_j(0); }
JH(ret_self) { JUNUSED; return jv_l(thiz); }

// --- ReflectionHelper ------------------------------------------------------------------

static JClass *cls_arg(JObj *o) {
  return o && jni_valid(o) && o->kind == JK_CLASS ? (JClass *)o : NULL;
}

JH(rh_getMethodID) {
  JUNUSED;
  JClass *c = cls_arg(a[0].l);
  if (!c)
    return jv_l(NULL);
  return jv_l(jni_reflect_method(c, S(a[1].l), S(a[2].l), a[3].i != 0));
}

JH(rh_getConstructorID) {
  JUNUSED;
  JClass *c = cls_arg(a[0].l);
  if (!c)
    return jv_l(NULL);
  // the signature arrives as the arguments only: "(Ljava/lang/String;)V"
  char sig[512];
  snprintf(sig, sizeof sig, "%s", S(a[1].l));
  return jv_l(jni_reflect_method(c, "<init>", sig, 0));
}

JH(rh_getFieldID) {
  JUNUSED;
  JClass *c = cls_arg(a[0].l);
  if (!c)
    return jv_l(NULL);
  return jv_l(jni_reflect_field(c, S(a[1].l), S(a[2].l), a[3].i != 0));
}

JH(rh_getFieldSignature) {
  JUNUSED;
  const char *sig = jni_reflect_sig(a[0].l);
  return jv_l(jni_str(sig ? sig : "Ljava/lang/Object;"));
}

JH(rh_newProxyInstance) {
  JUNUSED;
  // (UnityPlayer, long, Class) or (UnityPlayer, long, Class[])
  JClass *iface = NULL;
  JObj *c = a[2].l;
  if (c && jni_valid(c) && c->kind == JK_CLASS) {
    iface = (JClass *)c;
  } else {
    JArr *arr = jni_arr(c);
    if (arr && arr->len > 0)
      iface = cls_arg(((JObj **)arr->data)[0]);
  }
  JObj *p = jni_new_proxy(a[1].j, iface, 0);
  if (config.log_jni)
    log_printf("[jni] new proxy for %s", iface ? iface->name : "?");
  return jv_l(p);
}

// bitter.jnibridge.JNIBridge.newInterfaceProxy(long, Class[]): the engine's
// own Java proxies (Runnables for runOnUiThread, listeners)
JH(br_newInterfaceProxy) {
  JUNUSED;
  JArr *arr = jni_arr(a[1].l);
  JClass *iface = arr && arr->len > 0 ? cls_arg(((JObj **)arr->data)[0]) : NULL;
  return jv_l(jni_new_proxy(a[0].j, iface, 1));
}

JH(rh_createInvocationError) {
  JUNUSED;
  return jv_l(jni_new_throwable("java/lang/Error", "invocation error"));
}

// --- UnityPlayer -------------------------------------------------------------------------

JH(up_getView) { JUNUSED; return jv_l(s_player); }
JH(up_getLaunchURL) { JUNUSED; return jv_l(NULL); }
JH(up_getSplashMode) { JUNUSED; return jv_i(2); }
JH(up_getNetworkProxySettings) { JUNUSED; return jv_l(NULL); }
JH(up_getClipboardText) { JUNUSED; return jv_l(jni_str("")); }
JH(up_getKeyboardLayout) { JUNUSED; return jv_l(NULL); }
JH(up_getUnityPlayer) { JUNUSED; return jv_l(s_player); }

// the soft keyboard: shown by the engine for InputFields
void unity_soft_keyboard(const char *initial, int type, int multiline, int secure, const char *placeholder,
                         int limit);

JH(up_showSoftInput) {
  JUNUSED;
  // showSoftInput(String text, int type, boolean correction, boolean multiline,
  //               boolean secure, boolean alert, String placeholder, int limit, ...)
  const char *init = m->nargs > 0 && m->argt[0] == 'L' ? S(a[0].l) : "";
  const int type = m->nargs > 1 ? a[1].i : 0;
  const int multi = m->nargs > 3 ? a[3].i : 0;
  const int secure = m->nargs > 4 ? a[4].i : 0;
  const char *ph = m->nargs > 6 && m->argt[6] == 'L' ? S(a[6].l) : "";
  const int limit = m->nargs > 7 && m->argt[7] == 'I' ? a[7].i : 0;
  if (!config.keyboard)
    return jv_v();
  unity_soft_keyboard(init, type, multi, secure, ph, limit);
  return jv_v();
}

// --- small device helpers -------------------------------------------------------------------

// Application.internetReachability: 2 (reachable via LAN/WiFi) while the
// console is online, else 0. Unity 6 asks UnityPlayer itself (older Unity a
// NetworkConnectivity object); unanswered it was 0, "no internet", and
// Photon (ConnectionHandler.IsNetworkReachableUnity) and the backend's
// update loops stayed offline (HW round 3).
JH(nc_getNetworkConnectivity) { JUNUSED; return jv_i(net_is_available() ? 2 : 0); }
JH(mw_isInMultiWindowMode) { JUNUSED; return jv_z(0); }
JH(perm_has) { JUNUSED; return jv_z(1); }

// UnityPermissions.requestUserPermissions(activity, permissions, callbacks):
// every permission is granted (as hasUserAuthorizedPermission says), the
// answer coming later through the callbacks, as Android's does
typedef struct {
  JObj *callbacks;
  JObj *perms[8];
  int n;
} PermAnswer;

static void perm_answer(void *arg) {
  PermAnswer *pa = arg;
  for (int i = 0; i < pa->n; i++) {
    log_printf("[unity] permission %s: granted", jni_utf(pa->perms[i]));
    jni_proxy_invoke_as(pa->callbacks, "com/unity3d/player/IPermissionRequestCallbacks", "onPermissionGranted",
                        "(Ljava/lang/String;)V", &pa->perms[i], 1);
  }
  free(pa);
}

JH(perm_request) {
  JUNUSED;
  JObj *arr = m->nargs > 1 ? a[1].l : NULL, *cb = m->nargs > 2 ? a[2].l : NULL;
  if (!arr || !cb)
    return jv_v();
  PermAnswer *pa = calloc(1, sizeof *pa);
  pa->callbacks = cb;
  const JArr *ja = jni_arr(arr);
  for (int i = 0; ja && i < ja->len && pa->n < 8; i++)
    pa->perms[pa->n++] = ((JObj **)ja->data)[i];
  java_android_later(perm_answer, pa);
  return jv_v();
}
JH(surface_isValid) { JUNUSED; return jv_z(1); }

void java_unity_send(const char *obj, const char *method, const char *msg) {
  static void (*send)(void *env, void *cls, JObj *obj, JObj *method, JObj *msg);
  if (!send)
    send = jni_native("com/unity3d/player/UnityPlayer", "nativeUnitySendMessage");
  if (!send) {
    log_printf("[unity] UnityPlayer.nativeUnitySendMessage is not registered: %s.%s(%.80s) dropped", obj, method,
               msg);
    return;
  }
  send(jni_env(), jni_class("com/unity3d/player/UnityPlayer"), jni_str(obj), jni_str(method),
       jni_byte_array(msg, (int)strlen(msg)));
}


// --- boot.config, read through Java ------------------------------------------------------------
//
// Unity 2022 reads bin/Data/boot.config with AssetManager.open and a Scanner
// (next() with delimiter \z: the whole text), answered from the APK (from
// mkt_nx; Unity 6 reads it natively, so pg3d_nx had dropped this).

JH(assets_open) {
  JUNUSED;
  const char *path = m->nargs > 0 ? jni_utf(a[0].l) : NULL;
  if (!path || strcmp(path, "bin/Data/boot.config") != 0) {
    jni_throw("java/io/FileNotFoundException", path ? path : "");
    return jv_l(NULL);
  }
  const ApkEntry *e = apkfile_find("assets/bin/Data/boot.config");
  size_t n = 0;
  char *data = e ? apkfile_load(e, &n) : NULL;
  char *text = malloc(n + 2);
  if (text) {
    if (data)
      memcpy(text, data, n);
    if (n && text[n - 1] != '\n')
      text[n++] = '\n';
    text[n] = 0;
  }
  free(data);
  log_printf("[unity] boot.config: %zu B from the APK", n);
  JObj *in = jni_new("java/io/InputStream");
  jni_set_field(in, "text", jv_l(jni_str(text ? text : "")));
  free(text);
  return jv_l(in);
}

JH(scanner_init) {
  JUNUSED;
  int found;
  const jvalue t = m->nargs > 0 && a[0].l ? jni_get_field(a[0].l, "text", &found) : jv_l(NULL);
  jni_set_field(thiz, "text", t);
  return jv_v();
}

JH(scanner_self) {
  JUNUSED;
  return jv_l(thiz);
}

JH(scanner_next) {
  JUNUSED;
  int found;
  const jvalue t = jni_get_field(thiz, "text", &found);
  if (!t.l) {
    jni_throw("java/util/NoSuchElementException", "");
    return jv_l(NULL);
  }
  jni_set_field(thiz, "text", jv_l(NULL)); // read once (\z)
  return t;
}

JH(stream_close) {
  JUNUSED;
  return jv_v();
}

void java_unity_init(void) {
  jni_method("android/content/res/AssetManager", "open", "(Ljava/lang/String;)Ljava/io/InputStream;", assets_open);
  jni_method("java/util/Scanner", "<init>", NULL, scanner_init);
  jni_method("java/util/Scanner", "useDelimiter", NULL, scanner_self);
  jni_method("java/util/Scanner", "next", NULL, scanner_next);
  jni_method("java/util/Scanner", "close", NULL, stream_close);
  jni_method("java/io/InputStream", "close", NULL, stream_close);
  jni_set_super("com/unity3d/player/UnityPlayer", "android/widget/FrameLayout");
  jni_set_super("android/widget/FrameLayout", "android/view/ViewGroup");
  jni_set_super("android/view/ViewGroup", "android/view/View");
  jni_set_super("com/unity3d/player/UnityPlayerForActivityOrService", "com/unity3d/player/UnityPlayer");

  s_player = jni_new("com/unity3d/player/UnityPlayer");
  s_surface = jni_new("android/view/Surface");

  jni_static_field("com/unity3d/player/UnityPlayer", "currentActivity", jv_l(jni_activity()));
  jni_static_field("com/unity3d/player/UnityPlayer", "currentContext", jv_l(jni_activity()));
  jni_set_field(jni_activity(), "mUnityPlayer", jv_l(s_player));

  const char *RH = "com/unity3d/player/ReflectionHelper";
  jni_method(RH, "getMethodID", NULL, rh_getMethodID);
  jni_method(RH, "getConstructorID", NULL, rh_getConstructorID);
  jni_method(RH, "getFieldID", NULL, rh_getFieldID);
  jni_method(RH, "getFieldSignature", NULL, rh_getFieldSignature);
  jni_method(RH, "newProxyInstance", NULL, rh_newProxyInstance);
  jni_method(RH, "createInvocationError", NULL, rh_createInvocationError);
  jni_method(RH, "beginProxyCall", NULL, ret_true);
  jni_method(RH, "endProxyCall", NULL, ret_void);
  jni_method(RH, "endUnityLaunch", NULL, ret_void);
  jni_method("bitter/jnibridge/JNIBridge", "newInterfaceProxy", NULL, br_newInterfaceProxy);
  jni_method("bitter/jnibridge/JNIBridge", "disableInterfaceProxy", NULL, ret_void);

  const char *UP = "com/unity3d/player/UnityPlayer";
  jni_method(UP, "getView", NULL, up_getView);
  jni_method(UP, "getFrameLayout", NULL, up_getView);
  jni_method(UP, "getLaunchURL", NULL, up_getLaunchURL);
  jni_method(UP, "getSplashMode", NULL, up_getSplashMode);
  jni_method(UP, "getNetworkProxySettings", NULL, up_getNetworkProxySettings);
  jni_method(UP, "getClipboardText", NULL, up_getClipboardText);
  jni_method(UP, "setClipboardText", NULL, ret_void);
  jni_method(UP, "getKeyboardLayout", NULL, up_getKeyboardLayout);
  jni_method(UP, "isUaaLUseCase", NULL, ret_false);
  jni_method(UP, "isFinishing", NULL, ret_false);
  jni_method(UP, "showSoftInput", NULL, up_showSoftInput);
  jni_method(UP, "hideSoftInput", NULL, ret_void);
  jni_method(UP, "setSoftInputStr", NULL, ret_void);
  jni_method(UP, "setCharacterLimit", NULL, ret_void);
  jni_method(UP, "setHideInputField", NULL, ret_void);
  jni_method(UP, "setSelection", NULL, ret_void);
  jni_method(UP, "reportSoftInputArea", NULL, ret_void);
  jni_method(UP, "reportSoftInputIsVisible", NULL, ret_void);
  jni_method(UP, "reportSoftInputSelection", NULL, ret_void);
  jni_method(UP, "reportSoftInputStr", NULL, ret_void);
  jni_method(UP, "addPhoneCallListener", NULL, ret_void);
  jni_method(UP, "executeGLThreadJobs", NULL, ret_void);
  jni_method(UP, "invokeOnMainThread", NULL, ret_void);
  jni_method(UP, "requestUserAuthorization", NULL, ret_void);
  jni_method(UP, "getHapticsEnabled", NULL, ret_false);
  jni_method(UP, "hidePreservedContent", NULL, ret_void);
  jni_method(UP, "setupUnityToBeAbleToRun", NULL, ret_void);
  jni_method(UP, "getUnityPlayer", NULL, up_getUnityPlayer);
  jni_method(UP, "skipPermissionsDialog", NULL, ret_true);
  jni_method(UP, "checkPermission", NULL, perm_has);
  jni_method(UP, "pauseJavaAndCallUnloadCallback", NULL, ret_void);
  jni_method(UP, "unload", NULL, ret_void);
  jni_method(UP, "quit", NULL, ret_void);
  jni_method(UP, "setScreenKeepOn", NULL, ret_void);
  jni_method(UP, "getAudioVolume", NULL, (JHandler)ret_zero);
  jni_method(UP, "loadDynamicLibrary", NULL, ret_true);

  jni_method(UP, "getNetworkConnectivity", NULL, nc_getNetworkConnectivity);
  jni_method("com/unity3d/player/NetworkConnectivity", "getNetworkConnectivity", NULL,
             nc_getNetworkConnectivity);
  jni_method("com/unity3d/player/NetworkConnectivityNougat", "getNetworkConnectivity", NULL,
             nc_getNetworkConnectivity);
  jni_method("com/unity3d/player/MultiWindowSupport", "isInMultiWindowMode", NULL, mw_isInMultiWindowMode);
  jni_method("com/unity3d/player/MultiWindowSupport", "getAllowResizableWindow", NULL, ret_false);
  jni_method("com/unity3d/player/UnityPermissions", "hasUserAuthorizedPermission", NULL, perm_has);
  jni_method("com/unity3d/player/UnityPermissions", "requestUserPermissions", NULL, perm_request);
  jni_method("com/unity3d/player/HFPStatus", "getHFPStat", NULL, ret_false);
  jni_method("com/unity3d/player/AudioVolumeHandler", "getAudioVolume", NULL, (JHandler)ret_zero);
  jni_method("com/unity3d/player/OrientationLockListener", "getOrientationLock", NULL, (JHandler)ret_zero);
  jni_method("android/view/Surface", "isValid", NULL, surface_isValid);
  jni_method("android/view/Surface", "release", NULL, ret_void);
}
