/* game_patch.c -- what the port changes in the game's libraries
 *
 * Mario vs. Luigi: the frame rate. Unity on Android holds a game that never
 * sets Application.targetFrameRate to 30 fps; the console's display runs at
 * 60 Hz, so config fps (60) is applied once the C# runtime is up, and again
 * a few times early on in case the game sets its own.
 *
 * Split screen (MvL Accurate: MainMenuManager.OpenLocalPlay, the "Split
 * Screen Versus" button): the console's controller applet for two players,
 * Mario red and Luigi green as in NSMBW, shown on the UI thread; the
 * controllers are then announced to Unity in slot order (gamepad.c), so P1's
 * controller is Mario's. MMIM asks from its own C# (LocalPlay.Native: one
 * gamepad per player, the controller screen for its Local Play, players in
 * red and green as its split screen's outlines). Menus follow player 1 alone (MainMenuManager.Start,
 * OpenMainMenu, OpenTitleScreen); Split Screen Versus gives each player a
 * gamepad. Holding L+R opens the controller applet anywhere. The load hook
 * stays so libunity.so changes can go in unity_patch.c.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <switch.h>

#include "config.h"
#include "gamepad.h"
#include "il2cpp.h"
#include "jni.h"
#include "log.h"
#include "so_util.h"

void prof_match(int on); // prof.c

void unity_patch_loaded(so_module *m); // unity_patch.c
void trace_init(void);                 // trace.c

static void loaded(so_module *m) {
  if (!strcmp(m->soname, "libunity.so"))
    unity_patch_loaded(m);
}

void game_patch_init(void) {
  so_set_load_hook(loaded);
}

static void frame_rate(int quiet) {
  static void *set;
  if (!set)
    set = il2cpp_find_method(il2cpp_find_class("UnityEngine.CoreModule.dll", "UnityEngine", "Application"),
                             "set_targetFrameRate", 1);
  if (!set) {
    if (!quiet)
      log_printf("[game] Application.set_targetFrameRate not found");
    return;
  }
  int v = config.fps > 0 ? config.fps : 60;
  void *args[] = { &v };
  char exc[96] = "";
  il2cpp_invoke(set, NULL, args, exc, sizeof exc);
  if (!quiet)
    log_printf("[game] frame rate %d%s%s", v, exc[0] ? ": " : "", exc);
}

// --- split screen: the controller applet -------------------------------------------------

void watchdog_pause(int paused);

static const char *const s_names[] = { "Mario", "Luigi" };
// the players' colours, NSMBW's: red, green, yellow, blue (LocalPlay.GlowColor in MMIM)
static const u8 s_colors[][3] = { { 0xE6, 0x00, 0x12 }, { 0x00, 0xA8, 0x3C }, { 0xFF, 0xC8, 0x00 }, { 0x00, 0x78, 0xF0 } };

// runs at the end of a frame on the main thread (java_android_later): the
// game waits while the applet is up, as a console game does
static void split_screen_applet(void *arg) {
  (void)arg;
  watchdog_pause(1);
  gamepad_pick_controllers(2, s_names, s_colors);
  watchdog_pause(0);
}

// a player held L+R: the controller screen for the players there are now
// (one in menus: handheld, a pair or one controller)
static void controller_screen(void *arg) {
  (void)arg;
  const int n = gamepad_players();
  watchdog_pause(1);
#ifdef MVL_GAME_MMIM
  gamepad_pick_controllers(n, NULL, n >= 2 ? s_colors : NULL); // players pick their characters
#else
  gamepad_pick_controllers(n, n == 2 ? s_names : NULL, n == 2 ? s_colors : NULL);
#endif
  watchdog_pause(0);
}

static void select_first(void *arg);

// MMIM's Local Play (SwitchPort/LocalPlay.cs): the console's controller
// screen asks who plays (2 to 4 players, NSMBW's colours); the count it
// confirms (0: closed with B) goes back to LocalPlay.Picked
static int s_local_max = 4; // most players
static int s_local_room;    // the local room's screen is up: L+R lets players join or leave

static void local_picked(int n) {
  static void *picked;
  if (!picked) {
    void *klass = il2cpp_find_class(NULL, "", "LocalPlay");
    picked = klass ? il2cpp_find_method(klass, "Picked", 1) : NULL;
  }
  if (!picked) {
    log_printf("[game] LocalPlay.Picked not found");
    return;
  }
  void *args[] = { &n };
  char exc[96] = "";
  il2cpp_invoke(picked, NULL, args, exc, sizeof exc);
  log_printf("[game] Local Play: %d player(s)%s%s", n, exc[0] ? ": " : "", exc);
}

// at the end of a frame on the main thread (java_android_later): the game
// waits while the applet is up
// arg non-NULL: Local Play pressed on the main menu (else - / ZL+ZR / L+R
// in the local room)
static void local_play_applet(void *arg) {
  const int pressed_local_play = arg != NULL;
  int n;
  if (config.controller_applet && pressed_local_play && gamepad_handheld_only()) {
    // handheld with the Joy-Cons attached and nothing else connected (the
    // user, 2026-11-16: "it should be able to work with the two joycons
    // attached"): the controller screen's multi-player mode will not take
    // them (only its one-player mode does), so Local Play starts for one
    // player at once; - or ZL+ZR in the room still opens the screen for more
    n = 1;
    log_printf("[game] Local Play: handheld, Joy-Cons attached, no other controller: 1 player, no controller screen");
  } else if (config.controller_applet) {
    watchdog_pause(1);
    n = gamepad_pick_controllers_range(1, s_local_max, NULL, s_colors); // one is enough (practice, as online)
    watchdog_pause(0);
  } else {
    // no controller screen (config): the controllers there are now
    n = gamepad_connected();
    n = n < 1 ? 1 : n > s_local_max ? s_local_max : n;
    log_printf("[game] Local Play: controller screen off in config, %d player(s) by the controllers there", n);
  }
  local_picked(n);
}

// LocalPlay.Native(int op, int arg): 1 = announce arg gamepads (1: menus
// follow player 1), 2 = the controller screen for 1..arg players, 3 = the
// local room's screen is up (arg 1) or not (0), 4 = a split match runs (1)
// or ended (0): prof.c's 10 s windows meanwhile
static void on_local_play(uint64_t *frame) {
  const int op = (int)(int32_t)frame[2], arg = (int)(int32_t)frame[3];
  if (op == 1) {
    gamepad_set_players(arg);
  } else if (op == 2) {
    s_local_max = arg < 2 ? 2 : arg > 4 ? 4 : arg;
    log_printf("[game] Local Play: controller screen for 1-%d players", s_local_max);
    java_android_later(local_play_applet, (void *)1); // Local Play pressed
  } else if (op == 3) {
    s_local_room = arg != 0;
    gamepad_set_local_room(s_local_room); // - and ZL+ZR open the controller screen there
  } else if (op == 4) {
    prof_match(arg != 0);
  }
}

static void hook_local_play(void) {
  void *klass = il2cpp_find_class(NULL, "", "LocalPlay");
  void *m = klass ? il2cpp_find_method(klass, "Native", 2) : NULL;
  const uintptr_t code = m ? il2cpp_method_code(m) : 0;
  log_printf("[game] LocalPlay.Native %s", code && hook_arm64_pre(code, on_local_play) == 0 ? "hooked: local play"
                                           : klass ? "not hooked" : "not in this build");
}

static void on_open_local_play(uint64_t *frame) {
  (void)frame;
  gamepad_set_players(2); // Mario's controller and Luigi's (Gamepad.all[0], [1])
  java_android_later(select_first, NULL);
  if (!config.controller_applet) {
    log_printf("[game] Split Screen Versus (controller applet off in config)");
    return;
  }
  log_printf("[game] Split Screen Versus: controller applet");
  java_android_later(split_screen_applet, NULL);
}

// --- the title's prompt: "Press any button..." -------------------------------------------
//
// The user (2026-11-04): the title should say what a console game says. MMIM's
// own prompt is "Press any button..." (a TextMeshPro text in the menu scene);
// MvL Accurate's says "Click to Continue...", so it gets MMIM's words once the
// menu scene has started. Both titles are a selected UI button that only
// Submit (A) presses, so while the title is up any button on player 1 is A
// (gamepad_title_any) and the prompt is true.

static int u16_starts(const void *str, const char *ascii) {
  if (!str)
    return 0;
  const int32_t len = *(const int32_t *)((const char *)str + 0x10);
  const uint16_t *c = (const uint16_t *)((const char *)str + 0x14);
  int i = 0;
  for (; ascii[i]; i++)
    if (i >= len || c[i] != (uint8_t)ascii[i])
      return 0;
  return 1;
}

// --- the About screen: the Switch port's line ---------------------------------------------
//
// The user (2026-11-17): put the port in the game's About. HDErick's About is one
// TMP text: "MOD created by", then "Mod Contributors" with one "Name (role)" line
// each. The port's line goes after Mark19's Android one, in the same form (at the
// end if that line is not there). The text sits in the menu scene from the start
// (its panel inactive), so the title pass below finds it.

static const char k_port_credit[] = "aks796 (Nintendo Switch port)";

// an IL2CPP string (UTF-16) as UTF-8; -1 if it does not fit
static int u16_to_utf8(const void *str, char *out, int cap) {
  const int32_t len = *(const int32_t *)((const char *)str + 0x10);
  const uint16_t *c = (const uint16_t *)((const char *)str + 0x14);
  int o = 0;
  for (int i = 0; i < len; i++) {
    uint32_t cp = c[i];
    if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < len && c[i + 1] >= 0xDC00 && c[i + 1] < 0xE000)
      cp = 0x10000 + ((cp - 0xD800) << 10) + (c[++i] - 0xDC00);
    if (o + 4 >= cap)
      return -1;
    if (cp < 0x80) {
      out[o++] = (char)cp;
    } else if (cp < 0x800) {
      out[o++] = (char)(0xC0 | (cp >> 6));
      out[o++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out[o++] = (char)(0xE0 | (cp >> 12));
      out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
      out[o++] = (char)(0x80 | (cp & 0x3F));
    } else {
      out[o++] = (char)(0xF0 | (cp >> 18));
      out[o++] = (char)(0x80 | ((cp >> 12) & 0x3F));
      out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
      out[o++] = (char)(0x80 | (cp & 0x3F));
    }
  }
  out[o] = 0;
  return o;
}

// 1 if this text is the About and got the port's line
static int about_credit(void *item, void *text, void *set_text) {
  static char buf[16384], out[16384 + 96];
  if (!text || u16_to_utf8(text, buf, sizeof buf) < 0 || !strstr(buf, "Mod Contributors") ||
      strstr(buf, k_port_credit))
    return 0;
  const char *after = strstr(buf, "(Android support)");
  const char *eol = after ? strchr(after, '\n') : NULL;
  if (eol)
    snprintf(out, sizeof out, "%.*s\n%s%s", (int)(eol - buf), buf, k_port_credit, eol);
  else
    snprintf(out, sizeof out, "%s\n%s", buf, k_port_credit);
  void *sargs[] = { il2cpp_string(out) };
  char exc[96] = "";
  il2cpp_invoke(set_text, item, sargs, exc, sizeof exc);
  log_printf("[game] About: \"%s\" %s%s", k_port_credit, eol ? "after Mark19's Android line" : "at the end",
             exc[0] ? exc : "");
  return 1;
}

static void title_prompt(void *arg) {
  (void)arg;
  static void *find, *get_text, *set_text, *type;
  if (!find) {
    void *tmp = il2cpp_find_class("Unity.TextMeshPro.dll", "TMPro", "TMP_Text");
    void *obj = il2cpp_find_class("UnityEngine.CoreModule.dll", "UnityEngine", "Object");
    find = obj ? il2cpp_find_method(obj, "FindObjectsOfType", 2) : NULL;
    get_text = tmp ? il2cpp_find_method(tmp, "get_text", 0) : NULL;
    set_text = tmp ? il2cpp_find_method(tmp, "set_text", 1) : NULL;
    type = tmp ? il2cpp_keep(il2cpp_type_object(tmp)) : NULL;
    if (!find || !get_text || !set_text || !type) {
      log_printf("[game] title prompt: TMP_Text / FindObjectsOfType not found");
      find = NULL;
      return;
    }
  }
  bool inactive = true;
  void *args[] = { type, &inactive };
  char exc[96] = "";
  void *arr = il2cpp_invoke(find, NULL, args, exc, sizeof exc);
  if (!arr || exc[0]) {
    log_printf("[game] title prompt: FindObjectsOfType failed %s", exc);
    return;
  }
  const size_t n = *(const size_t *)((const char *)arr + 0x18);
  void **items = (void **)((char *)arr + 0x20);
  int changed = 0;
  for (size_t i = 0; i < n && i < 4096; i++) {
    if (!items[i])
      continue;
    void *text = il2cpp_invoke(get_text, items[i], NULL, NULL, 0);
    if (about_credit(items[i], text, set_text))
      continue;
    if (u16_starts(text, "Click to Continue")) {
      void *sargs[] = { il2cpp_string("Press any button...") };
      il2cpp_invoke(set_text, items[i], sargs, NULL, 0);
      changed++;
    }
  }
  if (changed)
    log_printf("[game] title prompt: Press any button... (%d text%s)", changed, changed == 1 ? "" : "s");
}

// --- B goes back ------------------------------------------------------------------------
//
// The user (HW rounds 1-3): B should go back. The game's menus only take
// clicks on their Back buttons, and B closes nothing. On player 1's B, at the
// end of the frame on the main thread:
//   1. in a match, paused: the quit prompt closes (HostQuitCancel), else the
//      pause menu (Pause toggles it);
//   2. in the menus: the Back button on screen is pressed -- an active,
//      interactable Button whose click plays BackSound (each of the game's
//      back buttons does), the one nearest the selected object when there
//      are several (a prompt over its panel);
//   3. on "Select a Game", which has none: the title.

static void *m_find_objs, *m_op_implicit, *t_button, *m_get_onclick, *m_invoke,
    *m_interactable, *m_es_current, *m_es_selected, *m_go_transform, *m_comp_transform, *m_parent,
    *m_go_active, *c_mmm, *c_gm, *m_pause, *m_quit_cancel, *m_open_title;

static int u16_eq(const void *str, const char *ascii) {
  return str && u16_starts(str, ascii) && *(const int32_t *)((const char *)str + 0x10) == (int32_t)strlen(ascii);
}

static int boxed_bool(void *boxed) {
  return boxed ? *(const uint8_t *)((const char *)boxed + 0x10) != 0 : 0;
}

static int boxed_int(void *boxed) {
  return boxed ? *(const int32_t *)((const char *)boxed + 0x10) : 0;
}

static int alive(void *obj) { // UnityEngine.Object's == null
  if (!obj)
    return 0;
  void *a[] = { obj };
  return boxed_bool(il2cpp_invoke(m_op_implicit, NULL, a, NULL, 0));
}

/* whether a UnityEvent's persistent (Inspector) calls include method:
 * UnityEventBase.m_PersistentCalls.m_Calls[i].m_MethodName (the getters are
 * stripped from this build; the serialized fields are not) */
static void *c_ueb, *c_pcg, *c_pc;
static int calls_method(void *ev, const char *method) {
  const int o_group = il2cpp_field_offset(c_ueb, "m_PersistentCalls");
  const int o_calls = il2cpp_field_offset(c_pcg, "m_Calls");
  const int o_name = il2cpp_field_offset(c_pc, "m_MethodName");
  if (!ev || o_group <= 0 || o_calls <= 0 || o_name <= 0)
    return 0;
  void *group = *(void **)((char *)ev + o_group);
  void *list = group ? *(void **)((char *)group + o_calls) : NULL;
  void *lk = list ? il2cpp_object_class(list) : NULL;
  const int o_items = lk ? il2cpp_field_offset(lk, "_items") : -1;
  const int o_size = lk ? il2cpp_field_offset(lk, "_size") : -1;
  if (o_items <= 0 || o_size <= 0)
    return 0;
  void *items = *(void **)((char *)list + o_items);
  const int size = *(const int32_t *)((char *)list + o_size);
  for (int i = 0; items && i < size && i < 32; i++) {
    void *call = ((void **)((char *)items + 0x20))[i];
    if (call && u16_eq(*(void **)((char *)call + o_name), method))
      return 1;
  }
  return 0;
}

static int back_init(void) {
  static int state; // 0 not yet, 1 ok, -1 missing
  if (state)
    return state > 0;
  void *obj = il2cpp_find_class("UnityEngine.CoreModule.dll", "UnityEngine", "Object");
  void *go = il2cpp_find_class("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject");
  void *comp = il2cpp_find_class("UnityEngine.CoreModule.dll", "UnityEngine", "Component");
  void *tr = il2cpp_find_class("UnityEngine.CoreModule.dll", "UnityEngine", "Transform");
  void *ueb = il2cpp_find_class("UnityEngine.CoreModule.dll", "UnityEngine.Events", "UnityEventBase");
  void *ue = il2cpp_find_class("UnityEngine.CoreModule.dll", "UnityEngine.Events", "UnityEvent");
  c_ueb = ueb;
  c_pcg = il2cpp_find_class("UnityEngine.CoreModule.dll", "UnityEngine.Events", "PersistentCallGroup");
  c_pc = il2cpp_find_class("UnityEngine.CoreModule.dll", "UnityEngine.Events", "PersistentCall");
  void *btn = il2cpp_find_class("UnityEngine.UI.dll", "UnityEngine.UI", "Button");
  void *sel = il2cpp_find_class("UnityEngine.UI.dll", "UnityEngine.UI", "Selectable");
  void *es = il2cpp_find_class("UnityEngine.UI.dll", "UnityEngine.EventSystems", "EventSystem");
  c_mmm = il2cpp_find_class(NULL, "", "MainMenuManager");
  c_gm = il2cpp_find_class(NULL, "", "GameManager");
  m_find_objs = obj ? il2cpp_find_method(obj, "FindObjectsOfType", 2) : NULL;
  m_op_implicit = obj ? il2cpp_find_method(obj, "op_Implicit", 1) : NULL;
  m_get_onclick = btn ? il2cpp_find_method(btn, "get_onClick", 0) : NULL;
  m_invoke = ue ? il2cpp_find_method(ue, "Invoke", 0) : NULL;
  m_interactable = sel ? il2cpp_find_method(sel, "IsInteractable", 0) : NULL;
  m_es_current = es ? il2cpp_find_method(es, "get_current", 0) : NULL;
  m_es_selected = es ? il2cpp_find_method(es, "get_currentSelectedGameObject", 0) : NULL;
  m_go_transform = go ? il2cpp_find_method(go, "get_transform", 0) : NULL;
  m_comp_transform = comp ? il2cpp_find_method(comp, "get_transform", 0) : NULL;
  m_parent = tr ? il2cpp_find_method(tr, "get_parent", 0) : NULL;
  m_go_active = go ? il2cpp_find_method(go, "get_activeSelf", 0) : NULL;
  m_pause = c_gm ? il2cpp_find_method(c_gm, "Pause", 0) : NULL;
  m_quit_cancel = c_gm ? il2cpp_find_method(c_gm, "HostQuitCancel", 0) : NULL;
  m_open_title = c_mmm ? il2cpp_find_method(c_mmm, "OpenTitleScreen", 0) : NULL;
  t_button = btn ? il2cpp_keep(il2cpp_type_object(btn)) : NULL;
  const int ok = m_find_objs && m_op_implicit && m_get_onclick && c_ueb && c_pcg && c_pc && m_invoke &&
                 m_interactable && m_comp_transform && m_parent && m_go_active && t_button;
  state = ok ? 1 : -1;
  if (!ok)
    log_printf("[game] B back: Unity UI methods not found (find %d implicit %d onClick %d events %d/%d/%d "
               "invoke %d interactable %d transform %d parent %d active %d type %d)",
               !!m_find_objs, !!m_op_implicit, !!m_get_onclick, !!c_ueb, !!c_pcg, !!c_pc, !!m_invoke, !!m_interactable,
               !!m_comp_transform, !!m_parent, !!m_go_active, !!t_button);
  return ok;
}

static void *field_obj(void *obj, void *klass, const char *name) {
  const int off = il2cpp_field_offset(klass, name);
  return obj && off > 0 ? *(void **)((char *)obj + off) : NULL;
}

// GameManager.Instance is a property over a private static _instance
static void *game_manager(void) {
  void *gm = NULL;
  if (c_gm && il2cpp_static_get(c_gm, "_instance", &gm) != 0)
    il2cpp_static_get(c_gm, "Instance", &gm);
  return alive(gm) ? gm : NULL;
}

static int go_active(void *go) {
  return alive(go) && boxed_bool(il2cpp_invoke(m_go_active, go, NULL, NULL, 0));
}

static void menu_back(void *arg) {
  (void)arg;
  if (!back_init())
    return;
  // 1. a match, paused
  void *gm = game_manager();
  if (gm) {
    const int off = il2cpp_field_offset(c_gm, "paused");
    if (off > 0 && *((uint8_t *)gm + off)) {
      void *exit_ui = field_obj(gm, c_gm, "hostExitUI");
      if (go_active(exit_ui) && m_quit_cancel)
        il2cpp_invoke(m_quit_cancel, gm, NULL, NULL, 0);
      else if (m_pause)
        il2cpp_invoke(m_pause, gm, NULL, NULL, 0);
      log_printf("[game] B: %s", go_active(exit_ui) ? "quit prompt closed" : "pause menu closed");
    }
    return; // in a match B is the players' own
  }
  // 2. the Back button on screen
  bool inactive = false;
  void *args[] = { t_button, &inactive };
  void *arr = il2cpp_invoke(m_find_objs, NULL, args, NULL, 0);
  void *selected = NULL;
  if (m_es_current && m_es_selected) {
    void *es = il2cpp_invoke(m_es_current, NULL, NULL, NULL, 0);
    selected = alive(es) ? il2cpp_invoke(m_es_selected, es, NULL, NULL, 0) : NULL;
  }
  void *sel_chain[64];
  int nsel = 0;
  for (void *t = alive(selected) && m_go_transform ? il2cpp_invoke(m_go_transform, selected, NULL, NULL, 0) : NULL;
       t && nsel < 64; t = il2cpp_invoke(m_parent, t, NULL, NULL, 0))
    sel_chain[nsel++] = t;
  void *best = NULL;
  int best_score = 1 << 30;
  const size_t n = arr ? *(const size_t *)((const char *)arr + 0x18) : 0;
  void **items = arr ? (void **)((char *)arr + 0x20) : NULL;
  for (size_t i = 0; i < n && i < 512; i++) {
    void *b = items[i];
    if (!alive(b) || !boxed_bool(il2cpp_invoke(m_interactable, b, NULL, NULL, 0)))
      continue;
    void *ev = il2cpp_invoke(m_get_onclick, b, NULL, NULL, 0);
    const int is_back = calls_method(ev, "BackSound");
    if (!is_back)
      continue;
    // nearest the selection: fewest steps up to an ancestor of it
    int score = 1000;
    int steps = 0;
    for (void *t = il2cpp_invoke(m_comp_transform, b, NULL, NULL, 0); t && steps < 64;
         t = il2cpp_invoke(m_parent, t, NULL, NULL, 0), steps++) {
      int k = 0;
      while (k < nsel && sel_chain[k] != t)
        k++;
      if (k < nsel) {
        score = steps + k;
        break;
      }
    }
    if (score < best_score) {
      best_score = score;
      best = ev;
    }
  }
  if (best) {
    il2cpp_invoke(m_invoke, best, NULL, NULL, 0);
    log_printf("[game] B: Back pressed");
    return;
  }
  // 3. "Select a Game": the title
  void *mmm = NULL;
  if (c_mmm && m_open_title && il2cpp_static_get(c_mmm, "Instance", &mmm) == 0 && alive(mmm) &&
      go_active(field_obj(mmm, c_mmm, "mainMenu")) && !go_active(field_obj(mmm, c_mmm, "title"))) {
    il2cpp_invoke(m_open_title, mmm, NULL, NULL, 0);
    log_printf("[game] B: title");
  }
}

// --- menus open on their first item, not on Back -----------------------------------------
//
// The user (HW round 3): Local Play (and other panels) opened with the Back
// button selected. After a menu opens (its Open* method, hooked), when the
// selected object is a back button, the panel's top-left interactable item
// nearest to it is selected instead.

static void *t_selectable, *m_comp_go, *m_position, *m_es_select, *c_button;

static void select_first(void *arg) {
  (void)arg;
  if (!back_init())
    return;
  if (!t_selectable) {
    void *sel = il2cpp_find_class("UnityEngine.UI.dll", "UnityEngine.UI", "Selectable");
    void *comp = il2cpp_find_class("UnityEngine.CoreModule.dll", "UnityEngine", "Component");
    void *tr = il2cpp_find_class("UnityEngine.CoreModule.dll", "UnityEngine", "Transform");
    void *es = il2cpp_find_class("UnityEngine.UI.dll", "UnityEngine.EventSystems", "EventSystem");
    c_button = il2cpp_find_class("UnityEngine.UI.dll", "UnityEngine.UI", "Button");
    m_comp_go = comp ? il2cpp_find_method(comp, "get_gameObject", 0) : NULL;
    m_position = tr ? il2cpp_find_method(tr, "get_position", 0) : NULL;
    m_es_select = es ? il2cpp_find_method(es, "SetSelectedGameObject", 1) : NULL;
    t_selectable = sel ? il2cpp_keep(il2cpp_type_object(sel)) : NULL;
    if (!t_selectable || !m_comp_go || !m_position || !m_es_select || !m_es_current || !m_es_selected ||
        !m_go_transform) {
      log_printf("[game] first item: Unity UI methods not found");
      t_selectable = NULL;
      return;
    }
  }
  void *es = il2cpp_invoke(m_es_current, NULL, NULL, NULL, 0);
  void *selected = alive(es) ? il2cpp_invoke(m_es_selected, es, NULL, NULL, 0) : NULL;
  if (!alive(selected))
    return;
  // is the selection a back button?
  void *back = NULL;
  {
    void *tr = il2cpp_invoke(m_go_transform, selected, NULL, NULL, 0);
    bool inactive = false;
    void *args[] = { t_button, &inactive };
    void *arr = il2cpp_invoke(m_find_objs, NULL, args, NULL, 0);
    const size_t n = arr ? *(const size_t *)((const char *)arr + 0x18) : 0;
    void **items = arr ? (void **)((char *)arr + 0x20) : NULL;
    for (size_t i = 0; i < n && i < 512 && !back; i++)
      if (alive(items[i]) && il2cpp_invoke(m_comp_transform, items[i], NULL, NULL, 0) == tr &&
          calls_method(il2cpp_invoke(m_get_onclick, items[i], NULL, NULL, 0), "BackSound"))
        back = items[i];
  }
  if (!back)
    return;
  void *chain[64];
  int nchain = 0;
  for (void *t = il2cpp_invoke(m_comp_transform, back, NULL, NULL, 0); t && nchain < 64;
       t = il2cpp_invoke(m_parent, t, NULL, NULL, 0))
    chain[nchain++] = t;
  bool inactive = false;
  void *args[] = { t_selectable, &inactive };
  void *arr = il2cpp_invoke(m_find_objs, NULL, args, NULL, 0);
  const size_t n = arr ? *(const size_t *)((const char *)arr + 0x18) : 0;
  void **items = arr ? (void **)((char *)arr + 0x20) : NULL;
  void *best = NULL;
  int best_score = 1 << 30;
  float best_x = 0, best_y = 0;
  for (size_t i = 0; i < n && i < 512; i++) {
    void *c = items[i];
    if (c == back || !alive(c) || !boxed_bool(il2cpp_invoke(m_interactable, c, NULL, NULL, 0)))
      continue;
    if (il2cpp_object_class(c) == c_button &&
        calls_method(il2cpp_invoke(m_get_onclick, c, NULL, NULL, 0), "BackSound"))
      continue;
    void *t = il2cpp_invoke(m_comp_transform, c, NULL, NULL, 0);
    int score = -1, steps = 0;
    for (void *u = t; u && steps < 64 && score < 0; u = il2cpp_invoke(m_parent, u, NULL, NULL, 0), steps++)
      for (int k = 1; k < nchain && score < 0; k++) // k >= 1: a common ancestor, not the button
        if (chain[k] == u)
          score = steps + k;
    if (score < 0)
      continue;
    void *pos = il2cpp_invoke(m_position, t, NULL, NULL, 0);
    const float x = pos ? *(const float *)((char *)pos + 0x10) : 0, y = pos ? *(const float *)((char *)pos + 0x14) : 0;
    // nearest panel first, then top (y up), then left
    if (!best || score < best_score ||
        (score == best_score && (y > best_y + 1.0f || (y > best_y - 1.0f && x < best_x)))) {
      best = c;
      best_score = score;
      best_x = x, best_y = y;
    }
  }
  if (!best)
    return;
  void *go = il2cpp_invoke(m_comp_go, best, NULL, NULL, 0);
  void *sa[] = { go };
  il2cpp_invoke(m_es_select, es, sa, NULL, 0);
  log_printf("[game] menu opened on its first item, not Back");
}

static void on_menu_opened(uint64_t *frame) {
  (void)frame;
#ifndef MVL_GAME_MMIM // MMIM's C# keeps its own selection (ControllerMenuSupport)
  java_android_later(select_first, NULL); // after the method has selected its item
#endif
}

// the main menu again (MainMenuManager.Start, OpenMainMenu): menus follow
// player 1, and the title is gone unless Start opens it again
static void on_main_menu(uint64_t *frame) {
  (void)frame;
  gamepad_set_players(1);
  gamepad_title_any(0);
#ifndef MVL_GAME_MMIM
  java_android_later(title_prompt, NULL); // after this frame: the scene is up
#endif
}

// the title (OpenTitleScreen, also from Start): any button continues
static void on_title(uint64_t *frame) {
  (void)frame;
  gamepad_set_players(1);
#ifndef MVL_GAME_MMIM // MMIM's C# takes any button on its title and keeps its own text
  gamepad_title_any(1);
  java_android_later(title_prompt, NULL);
#endif
}

static void hook(void *klass, const char *name, void (*fn)(uint64_t *)) {
  void *m = klass ? il2cpp_find_method(klass, name, 0) : NULL;
  const uintptr_t code = m ? il2cpp_method_code(m) : 0;
  if (code) {
    const uint32_t *w = (const uint32_t *)code;
    log_printf("[game] MainMenuManager.%s at %p: %08x %08x %08x %08x %08x %08x", name, (void *)code, w[0], w[1], w[2],
               w[3], w[4], w[5]);
  }
  if (code && hook_arm64_pre(code, fn) == 0)
    log_printf("[game] MainMenuManager.%s hooked", name);
  else
    log_printf("[game] MainMenuManager.%s %s", name, klass ? "not hooked" : "not found");
}

static void hook_split_screen(void) {
  void *klass = il2cpp_find_class(NULL, "", "MainMenuManager");
  hook(klass, "OpenLocalPlay", on_open_local_play);
  hook(klass, "Start", on_main_menu);
  hook(klass, "OpenMainMenu", on_main_menu);
  hook(klass, "OpenTitleScreen", on_title);
  static const char *const opens[] = { "OpenLobbyMenu", "OpenCreateLobby", "OpenOptions", "OpenControls",
                                       "OpenCredits", "OpenInLobbyMenu", "OpenPrivatePrompt", "StartGame" };
  for (unsigned i = 0; i < sizeof opens / sizeof opens[0]; i++)
    hook(klass, opens[i], on_menu_opened);
}

// MMIM's SwitchRumble.Native(int player, int effect, float strength)
// (SwitchPort/SwitchRumble.cs): an empty C# method the game calls for each
// rumble; the hook takes the arguments (x0, x1, s0) to the controller.
static void on_rumble(uint64_t *frame) {
  float strength;
  memcpy(&strength, (const uint8_t *)frame + 96, sizeof strength); // q0, saved by hook_pre.s
  gamepad_rumble_effect((int)(int32_t)frame[2], (int)(int32_t)frame[3], strength);
}

static void hook_rumble(void) {
  void *klass = il2cpp_find_class(NULL, "", "SwitchRumble");
  void *m = klass ? il2cpp_find_method(klass, "Native", 3) : NULL;
  const uintptr_t code = m ? il2cpp_method_code(m) : 0;
  log_printf("[game] SwitchRumble.Native %s", code && hook_arm64_pre(code, on_rumble) == 0 ? "hooked: rumble on"
                                              : klass ? "not hooked" : "not in this build");
}

int game_patch_il2cpp(void) {
  static int done;
  if (done || !il2cpp_ready())
    return done;
  done = 1;
  frame_rate(0);
  hook_split_screen();
  hook_rumble();
  hook_local_play();
  if (config.trace[0] || config.trace_nre[0])
    trace_init();
  return 1;
}

void game_patch_frame(unsigned frame) {
#ifndef MVL_GAME_MMIM
  if (gamepad_take_back_request() && il2cpp_ready())
    java_android_later(menu_back, NULL);
#else
  gamepad_take_back_request(); // MMIM's own C# goes back (ControllerMenuSupport)
#endif
  if (gamepad_take_screen_request())
    java_android_later(s_local_room ? local_play_applet : controller_screen, NULL);
  if (frame == 300 || frame == 900 || frame == 1800)
    frame_rate(1);
}
