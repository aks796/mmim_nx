/* java_game.c -- the Google pieces a Unity game asks for
 *
 * Google Play services: answered as missing, Play Games as "nobody signed
 * in", Tasks as already complete (kept from mkt_nx / pg3d_nx). Mario vs.
 * Luigi has no Java classes of its own.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "jni.h"
#include "log.h"

JH(ret_true) { JUNUSED; return jv_z(1); }
JH(ret_false) { JUNUSED; return jv_z(0); }
JH(ret_null) { JUNUSED; return jv_l(NULL); }
JH(ret_void) { JUNUSED; return jv_v(); }
JH(ret_self) { JUNUSED; return jv_l(thiz); }

// --- com.google.android.gms.tasks.Task: already complete -------------------------------
// A Task made here is done when it is handed out: a result, or an exception
// for a failed one. Listeners added to it are called at once on the caller
// (Android posts them to the main thread, which is the caller's here, as
// runOnUiThread), and every add... returns the Task.

#define TASK_CLASS "com/google/android/gms/tasks/Task"

static JObj *task_done(JObj *result, JObj *exception) {
  JObj *t = jni_new(TASK_CLASS);
  jni_set_field(t, "result", jv_l(result));
  jni_set_field(t, "exception", jv_l(exception));
  return t;
}

static JObj *task_exception(JObj *t) { return jni_get_field(t, "exception", NULL).l; }

static void task_call(JObj *listener, const char *iface, const char *method, const char *sig,
                      JObj *arg) {
  if (jni_is_proxy(listener))
    jni_proxy_invoke_as(listener, iface, method, sig, &arg, 1);
  else if (listener)
    log_printf("[game] a %s added to a Task is not called", jni_class_name(listener));
}

// addOn...Listener([Activity | Executor,] listener): the listener is the last argument
JH(task_addOnSuccessListener) {
  JUNUSED;
  if (!task_exception(thiz))
    task_call(a[m->nargs - 1].l, "com/google/android/gms/tasks/OnSuccessListener", "onSuccess",
              "(Ljava/lang/Object;)V", jni_get_field(thiz, "result", NULL).l);
  return jv_l(thiz);
}
JH(task_addOnFailureListener) {
  JUNUSED;
  JObj *e = task_exception(thiz);
  if (e)
    task_call(a[m->nargs - 1].l, "com/google/android/gms/tasks/OnFailureListener", "onFailure",
              "(Ljava/lang/Exception;)V", e);
  return jv_l(thiz);
}
JH(task_addOnCompleteListener) {
  JUNUSED;
  task_call(a[m->nargs - 1].l, "com/google/android/gms/tasks/OnCompleteListener", "onComplete",
            "(Lcom/google/android/gms/tasks/Task;)V", thiz);
  return jv_l(thiz);
}
JH(task_isSuccessful) { JUNUSED; return jv_z(task_exception(thiz) == NULL); }
JH(task_getResult) { JUNUSED; return jni_get_field(thiz, "result", NULL); }
JH(task_getException) { JUNUSED; return jv_l(task_exception(thiz)); }

// --- Google Play Games: present, nobody signed in -----------------------------------------

static JObj *s_gsi_client;

JH(pg_getGamesSignInClient) { JUNUSED; return jv_l(s_gsi_client); }
JH(gsi_authenticate) {
  JUNUSED;
  JObj *r = jni_new("com/google/android/gms/games/AuthenticationResult");
  log_printf("[game] Play Games %s(): not signed in", m->name);
  return jv_l(task_done(r, NULL));
}
JH(gms_missing) { JUNUSED; return jv_i(1); } // ConnectionResult.SERVICE_MISSING

// --- registration ---------------------------------------------------------------------------

void java_game_init(void) {
  // Google Play services: "not available" keeps every Google path off
  jni_method("com/google/android/gms/common/GoogleApiAvailability", "getInstance", NULL, ret_null);
  jni_method("com/google/android/gms/common/GoogleApiAvailability", "isGooglePlayServicesAvailable",
             NULL, gms_missing);

  const char *T = TASK_CLASS;
  jni_method(T, "addOnSuccessListener", NULL, task_addOnSuccessListener);
  jni_method(T, "addOnFailureListener", NULL, task_addOnFailureListener);
  jni_method(T, "addOnCompleteListener", NULL, task_addOnCompleteListener);
  jni_method(T, "addOnCanceledListener", NULL, ret_self);
  jni_method(T, "isComplete", NULL, ret_true);
  jni_method(T, "isCanceled", NULL, ret_false);
  jni_method(T, "isSuccessful", NULL, task_isSuccessful);
  jni_method(T, "getResult", NULL, task_getResult);
  jni_method(T, "getException", NULL, task_getException);

  const char *GSI = "com/google/android/gms/games/GamesSignInClient";
  s_gsi_client = jni_new(GSI);
  jni_method("com/google/android/gms/games/PlayGamesSdk", "initialize", NULL, ret_void);
  jni_method("com/google/android/gms/games/PlayGames", "getGamesSignInClient", NULL,
             pg_getGamesSignInClient);
  jni_method(GSI, "isAuthenticated", NULL, gsi_authenticate);
  jni_method(GSI, "signIn", NULL, gsi_authenticate);
  jni_method("com/google/android/gms/games/AuthenticationResult", "isAuthenticated", NULL, ret_false);
}
