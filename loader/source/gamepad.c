/* gamepad.c -- the Switch controllers as Android gamepads
 *
 * Each player slot (No1..No4; handheld counts as player 1) is polled with
 * its own PadState and handed to input.c as one Android gamepad, which
 * Unity's Input System sees as a Gamepad. Mario vs. Luigi's local play takes
 * Gamepad.all[0], [1], ... so the slot order is the player order. Scripted
 * test input (test_script.c, main.c) rides on player 1.
 *
 * Menus belong to player 1, as on a console: until local play opens
 * (gamepad_set_players, game_patch.c) only one gamepad is announced, fed by
 * the first connected slot. Local play announces one per player.
 *
 * Joy-Cons, as nsmbw_nx's input_switch.cpp reads them (HW round 2,
 * 2026-11-04): a single Joy-Con held sideways (hold type horizontal) has
 * its buttons by position (the one under the right thumb is A, SL/SR are
 * L/R, its +/- is +, the stick click is -) and its one stick turned a
 * quarter turn back. A Joy-Con alone in a pair's slot (the console reports
 * it as JoyDual with one half attached) is played as a single one too.
 *
 * gamepad_pick_controllers: the console's controller applet (player colours
 * and names, NSMBW style). Afterwards every controller is announced to Unity
 * again in slot order, so Gamepad.all follows the applet's assignment.
 * Holding L+R for a second (SL+SR on a single Joy-Con: two) asks for the
 * applet from anywhere (gamepad_take_screen_request, game_patch.c).
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <switch.h>

#include <math.h>
#include <stdio.h>

#include "config.h"
#include "gamepad.h"
#include "log.h"
#include "unity.h"

#define MAX_PADS 4

void test_script_pad_player(int player, u64 *held, int *lx, int *ly, int *rx, int *ry); // test_script.c
int test_script_uses_player(int player);

static PadState s_pads[MAX_PADS];
static int s_init;
static volatile int s_reassign; // 1: announce every controller again (after the applet)
static int s_quiet_frames;
static volatile int s_players = 1;  // gamepads announced to Unity (1 = menus: player 1's only)
static int s_menu_slot = -1;        // menus: the slot that is player 1 (the first connected)
static u32 s_logged_style[MAX_PADS];
static u64 s_both_since[MAX_PADS];  // L+R held since (ns), for the controller screen
static volatile int s_screen_request;
static volatile int s_title_any;    // the title is up: any button is A (game_patch.c)
static volatile int s_back_request; // player 1 pressed B (game_patch.c: menus go back)
static u64 s_prev_held0;
static u8 s_was_on[MAX_PADS];       // local play: the player's controller was there
static u64 s_off_since[MAX_PADS];   // local play: and has been gone since (ns)
static void watch_player(int i, int on);

#define SINGLE_STYLES (HidNpadStyleTag_NpadJoyLeft | HidNpadStyleTag_NpadJoyRight)
#define MULTI_STYLES (HidNpadStyleTag_NpadFullKey | HidNpadStyleTag_NpadHandheld | HidNpadStyleTag_NpadJoyDual)

// A Joy-Con alone in a pair's slot: one connected outside the controller
// screen (the console puts it in dual mode, as half of a pair) or one whose
// partner left. Alone it can only be played sideways, so it is read as a
// single one; its other half joining makes it a pair again.
static u32 played_style(PadState *pad) {
  const u32 style = padGetStyleSet(pad);
  if (!(style & HidNpadStyleTag_NpadJoyDual) ||
      (style & (HidNpadStyleTag_NpadFullKey | HidNpadStyleTag_NpadHandheld)))
    return style;
  const u32 attr = padGetAttributes(pad);
  const int left = (attr & HidNpadAttribute_IsLeftConnected) != 0;
  const int right = (attr & HidNpadAttribute_IsRightConnected) != 0;
  if (left == right)
    return style;
  return (style & ~(u32)HidNpadStyleTag_NpadJoyDual) |
         (left ? HidNpadStyleTag_NpadJoyLeft : HidNpadStyleTag_NpadJoyRight);
}

static int is_single(u32 style) {
  return (style & SINGLE_STYLES) && !(style & MULTI_STYLES);
}

// a single Joy-Con held sideways, by position: the buttons of a Pro
// Controller in the same places (A right, B bottom, X top, Y left), SL/SR as
// L/R, the left one's - pauses as the right one's + does, the stick click is -
static u64 normalize_single_joycon(u64 held, u32 style) {
  if (!is_single(style))
    return held;
  u64 o = held & ~(u64)(HidNpadButton_A | HidNpadButton_B | HidNpadButton_X | HidNpadButton_Y | HidNpadButton_Up |
                        HidNpadButton_Down | HidNpadButton_Left | HidNpadButton_Right | HidNpadButton_L |
                        HidNpadButton_R | HidNpadButton_AnySL | HidNpadButton_AnySR | HidNpadButton_Plus |
                        HidNpadButton_Minus | HidNpadButton_StickL | HidNpadButton_StickR);
#define MAP(from, to) if (held & (from)) o |= (to)
  if (style & HidNpadStyleTag_NpadJoyLeft) {
    MAP(HidNpadButton_Down, HidNpadButton_A);
    MAP(HidNpadButton_Left, HidNpadButton_B);
    MAP(HidNpadButton_Right, HidNpadButton_X);
    MAP(HidNpadButton_Up, HidNpadButton_Y);
    MAP(HidNpadButton_LeftSL, HidNpadButton_L);
    MAP(HidNpadButton_LeftSR, HidNpadButton_R);
    MAP(HidNpadButton_Minus, HidNpadButton_Plus);
    MAP(HidNpadButton_StickL, HidNpadButton_Minus);
  } else {
    MAP(HidNpadButton_X, HidNpadButton_A);
    MAP(HidNpadButton_A, HidNpadButton_B);
    MAP(HidNpadButton_Y, HidNpadButton_X);
    MAP(HidNpadButton_B, HidNpadButton_Y);
    MAP(HidNpadButton_RightSL, HidNpadButton_L);
    MAP(HidNpadButton_RightSR, HidNpadButton_R);
    MAP(HidNpadButton_Plus, HidNpadButton_Plus);
    MAP(HidNpadButton_StickR, HidNpadButton_Minus);
  }
#undef MAP
  return o;
}

static const char *style_name(u32 style) {
  if (style & HidNpadStyleTag_NpadHandheld) return "handheld";
  if (style & HidNpadStyleTag_NpadFullKey) return "Pro Controller";
  if (style & HidNpadStyleTag_NpadJoyDual) return "Joy-Con pair";
  if (style & HidNpadStyleTag_NpadJoyLeft) return "left Joy-Con sideways";
  if (style & HidNpadStyleTag_NpadJoyRight) return "right Joy-Con sideways";
  return style ? "other controller" : "none";
}

static float stick(s32 v) {
  const float f = (float)v / 32767.0f;
  return f < -1.0f ? -1.0f : f > 1.0f ? 1.0f : f;
}

typedef struct {
  int on;
  int single; // a single Joy-Con held sideways
  u64 held;
  HidAnalogStickState ls, rs;
} Slot;

static void read_slot(int i, Slot *s) {
  PadState *p = &s_pads[i];
  padUpdate(p);
  s->on = padIsConnected(p);
  s->single = 0;
  s->held = 0;
  s->ls = (HidAnalogStickState){ 0 };
  s->rs = (HidAnalogStickState){ 0 };
  if (!s->on) {
    s_logged_style[i] = 0;
    s_both_since[i] = 0;
    return;
  }
  const u32 style = played_style(p);
  if (style != s_logged_style[i]) {
    s_logged_style[i] = style;
    log_printf("[input] slot %d: %s (style 0x%x, attributes 0x%x)", i + 1, style_name(style), padGetStyleSet(p),
               padGetAttributes(p));
  }
  s->held = normalize_single_joycon(padGetButtons(p), style);
  s->ls = padGetStickPos(p, 0);
  s->rs = padGetStickPos(p, 1);
  if (is_single(style)) {
    // one stick, the player's only one: a right Joy-Con reports it where a
    // full controller has its right stick. Held sideways the Joy-Con is
    // turned a quarter turn (the left one anticlockwise, the right one
    // clockwise) and the console reports the stick as printed, so the turn
    // is undone here, as for the buttons
    const HidAnalogStickState st = (style & HidNpadStyleTag_NpadJoyRight) ? s->rs : s->ls;
    s->ls.x = (style & HidNpadStyleTag_NpadJoyRight) ? st.y : -st.y;
    s->ls.y = (style & HidNpadStyleTag_NpadJoyRight) ? -st.x : st.x;
    s->rs = (HidAnalogStickState){ 0 };
  }
  // L+R held: the controller screen (longer on a single Joy-Con, whose SL+SR
  // are easy to press together briefly)
  if ((s->held & HidNpadButton_L) && (s->held & HidNpadButton_R)) {
    const u64 now = armTicksToNs(armGetSystemTick());
    if (!s_both_since[i])
      s_both_since[i] = now;
    else if (now - s_both_since[i] >= (is_single(style) ? 2000000000ull : 1000000000ull)) {
      s_both_since[i] = 0;
      s_screen_request = 1;
      log_printf("[input] slot %d held L+R: controller screen", i + 1);
    }
  } else {
    s_both_since[i] = 0;
  }
  s->single = is_single(style);
}

void gamepad_update(const PadInput *pad, int w, int h, int touchscreen_used) {
  (void)w, (void)h, (void)touchscreen_used;
  if (!s_init) {
    s_init = 1;
    hidSetNpadJoyHoldType(HidNpadJoyHoldType_Horizontal); // single Joy-Cons sideways
    padInitialize(&s_pads[0], HidNpadIdType_No1, HidNpadIdType_Handheld);
    for (int i = 1; i < MAX_PADS; i++)
      padInitialize(&s_pads[i], (HidNpadIdType)(HidNpadIdType_No1 + i));
  }
  // after the applet (or a change of players): every controller leaves,
  // then comes back in slot order
  if (s_reassign) {
    s_reassign = 0;
    for (int i = 0; i < MAX_PADS; i++)
      input_pad(i, 0, 0, 0, 0, 0, 0);
    s_quiet_frames = 2;
    return;
  }
  if (s_quiet_frames > 0) {
    s_quiet_frames--;
    return;
  }
  Slot slots[MAX_PADS];
  for (int i = 0; i < MAX_PADS; i++)
    read_slot(i, &slots[i]);
  const int players = s_players;
  if (players <= 1) {
    // menus: one gamepad, the first connected slot's (the console's player 1
    // normally; whoever has a controller when slot 1 is empty)
    int first = -1;
    for (int i = 0; i < MAX_PADS && first < 0; i++)
      if (slots[i].on)
        first = i;
    if (first >= 0 && first != s_menu_slot) {
      s_menu_slot = first;
      log_printf("[input] menus: slot %d", first + 1);
    }
    if (first > 0)
      slots[0] = slots[first];
  }
  for (int i = 0; i < MAX_PADS; i++) {
    Slot *s = &slots[i];
    if (i >= (players < 1 ? 1 : players))
      s->on = 0, s->held = 0;
    else if (players > 1)
      watch_player(i, s->on);
    // player 1 also carries the scripted test input (main.c: script only)
    int scripted = i == 0 && pad && (pad->held || pad->lx || pad->ly || pad->rx || pad->ry);
    if (i >= 1) {
      // players 2-4's scripted input (test_script.c "p2 ..." .. "p4 ..."): local play checks in Ryujinx
      PadInput q = { 0 };
      test_script_pad_player(i, &q.held, &q.lx, &q.ly, &q.rx, &q.ry);
      if (i < players && test_script_uses_player(i)) {
        scripted = 1;
        s->held |= q.held;
        if (!s->ls.x && !s->ls.y)
          s->ls.x = q.lx, s->ls.y = q.ly;
        if (!s->rs.x && !s->rs.y)
          s->rs.x = q.rx, s->rs.y = q.ry;
      }
    }
    if (i == 0 && pad) {
      s->held |= pad->held;
      if (!s->ls.x && !s->ls.y)
        s->ls.x = pad->lx, s->ls.y = pad->ly;
      if (!s->rs.x && !s->rs.y)
        s->rs.x = pad->rx, s->rs.y = pad->ry;
    }
    // the stick directions also press the D-pad bits libnx sets for them;
    // keep only the real D-pad
    s->held &= ~(u64)(HidNpadButton_StickLLeft | HidNpadButton_StickLRight | HidNpadButton_StickLUp |
                      HidNpadButton_StickLDown | HidNpadButton_StickRLeft | HidNpadButton_StickRRight |
                      HidNpadButton_StickRUp | HidNpadButton_StickRDown);
#ifdef MVL_GAME_MMIM
    // a full controller (Pro Controller, handheld, a Joy-Con pair): L drops
    // the reserve power-up, as - does (the user, 2026-11-16: "the L (not
    // ZL) should also drop the powerup"); ZL, R and ZR stay the power-up
    // action. A single Joy-Con sideways keeps SL/SR (its L/R) for the
    // power-up. After read_slot's L+R check (the real L) and the scripted
    // input (Ryubing tests press L as a player would)
    {
      static u8 s_l_down[MAX_PADS];
      static int s_l_logged;
      const int l = !s->single && (s->held & HidNpadButton_L);
      if (l && !s_l_down[i] && s_l_logged < 8) {
        s_l_logged++;
        log_printf("[input] player %d: L on a full controller = - (reserve item)", i + 1);
      }
      s_l_down[i] = (u8)l;
      if (l)
        s->held = (s->held & ~(u64)HidNpadButton_L) | HidNpadButton_Minus;
    }
#endif
    // the title says "Press any button...": any face, shoulder or +/- button
    // is the A that its button (the selected UI object) takes
    if (i == 0 && s_title_any &&
        (s->held & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_X | HidNpadButton_Y | HidNpadButton_L |
                    HidNpadButton_R | HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_Plus |
                    HidNpadButton_Minus)))
      s->held |= HidNpadButton_A;
    if (i == 0) {
      // B: back, as on a console (the game's menus only take clicks there)
      if ((s->held & HidNpadButton_B) && !(s_prev_held0 & HidNpadButton_B) && !s_title_any)
        s_back_request = 1;
      s_prev_held0 = s->held;
    }
    // local play: every player's gamepad stays announced while its
    // controller is away (Gamepad.all keeps the player order, the player
    // stands still; watch_player brings up the controller screen)
    const int on = s->on || scripted || (players > 1 && i < players);
    input_pad(i, on, on ? s->held : 0, on ? stick(s->ls.x) : 0, on ? stick(s->ls.y) : 0, on ? stick(s->rs.x) : 0,
              on ? stick(s->rs.y) : 0);
  }
}

void gamepad_il2cpp_up(void) {}

// local play: a player's controller gone for a moment (a Joy-Con's battery,
// out of range, docked or undocked) brings up the controller screen, as a
// console game does, so it can be connected again
static void watch_player(int i, int on) {
  if (on) {
    s_was_on[i] = 1;
    s_off_since[i] = 0;
    return;
  }
  if (!s_was_on[i])
    return;
  const u64 now = armTicksToNs(armGetSystemTick());
  if (!s_off_since[i]) {
    s_off_since[i] = now;
  } else if (now - s_off_since[i] >= 1500000000ull) {
    s_was_on[i] = 0;
    s_off_since[i] = 0;
    s_screen_request = 1;
    log_printf("[input] player %d's controller is gone: controller screen", i + 1);
  }
}

int gamepad_connected(void) {
  int n = 0;
  for (int i = 0; i < MAX_PADS; i++)
    if ((s_init && padIsConnected(&s_pads[i])) || (i > 0 && test_script_uses_player(i)))
      n = i + 1;
  return n;
}

void gamepad_set_players(int players) {
  if (players < 1)
    players = 1;
  if (players > MAX_PADS)
    players = MAX_PADS;
  if (players == s_players)
    return;
  log_printf("[input] %d player(s): %s", players, players == 1 ? "menus follow player 1" : "one gamepad each");
  s_players = players;
  s_menu_slot = -1;
  s_reassign = 1;
}

int gamepad_take_back_request(void) {
  if (!s_back_request)
    return 0;
  s_back_request = 0;
  return 1;
}

void gamepad_title_any(int on) {
  s_title_any = on;
}

int gamepad_players(void) {
  return s_players;
}

int gamepad_take_screen_request(void) {
  if (!s_screen_request)
    return 0;
  s_screen_request = 0;
  return 1;
}

int gamepad_pick_controllers(int players, const char *const *names, const u8 (*rgb)[3]) {
  return gamepad_pick_controllers_range(players, players, names, rgb);
}

int gamepad_pick_controllers_range(int min, int max, const char *const *names, const u8 (*rgb)[3]) {
  if (min < 1 || max > MAX_PADS || min > max)
    return 0;
  const int players = max;
  HidLaControllerSupportArg arg;
  hidLaCreateControllerSupportArg(&arg);
  arg.hdr.player_count_min = (s8)min;
  arg.hdr.player_count_max = (s8)max;
  arg.hdr.enable_take_over_connection = 1;
  arg.hdr.enable_left_justify = 1;
  arg.hdr.enable_permit_joy_dual = 1;
  arg.hdr.enable_single_mode = max == 1;
  arg.hdr.enable_identification_color = rgb != NULL;
  for (int i = 0; rgb && i < players; i++) {
    arg.identification_color[i].r = rgb[i][0];
    arg.identification_color[i].g = rgb[i][1];
    arg.identification_color[i].b = rgb[i][2];
    arg.identification_color[i].a = 255;
  }
  arg.enable_explain_text = names != NULL;
  for (int i = 0; names && i < players; i++)
    hidLaSetExplainText(&arg, names[i], (HidNpadIdType)(HidNpadIdType_No1 + i));
  HidLaControllerSupportResultInfo info = { 0 };
  const Result rc = hidLaShowControllerSupport(&info, &arg);
  log_printf("[input] controller applet for %d-%d player(s): rc 0x%x, %d player(s), selected %u", min, max, rc,
             info.player_count, info.selected_id);
  for (int i = 0; i < MAX_PADS; i++) {
    s_logged_style[i] = 0; // log what each slot holds now
    s_was_on[i] = 0;       // the applet may have moved them: watched again from now
    s_off_since[i] = 0;
  }
  s_menu_slot = -1;
  s_reassign = 1;
  return R_SUCCEEDED(rc) ? info.player_count : 0;
}

int gamepad_ring(float *x0, float *y0, float *x1, float *y1) {
  (void)x0, (void)y0, (void)x1, (void)y1;
  return 0;
}

int gamepad_pointer(float *x, float *y) {
  (void)x, (void)y;
  return 0;
}

// --- rumble ------------------------------------------------------------------------
//
// The game names a moment (SwitchRumble.cs in MMIM, through a hook in
// game_patch.c) and this plays it as HD rumble, shaped the way first-party
// Switch games shape theirs rather than a Wii Remote's on/off motor: each
// effect is a few strokes on the low band (impacts: 60-150 Hz) and the high
// band (crackle and sparkle: 200-900 Hz), each with an attack, an exponential
// fade, a pitch glide and sometimes a tremolo, sampled every 5 ms on a thread
// of its own (the controller takes a new value about that often). Effects
// overlap: each band takes the strongest stroke of the moment.
typedef struct {
  u16 at, len;      // ms from the effect's start, ms long
  u8 high;          // 0 low band, 1 high band
  float amp;        // peak amplitude
  u16 attack, tau;  // ms of linear rise; ms of exponential fade after it (0: none)
  float f0, f1;     // Hz at the start and the end (a glide)
  float trem;       // tremolo, Hz (0: none)
} Stroke;

typedef struct {
  const Stroke *s;
  int n;
} Effect;

#define S(...) { __VA_ARGS__ }
static const Stroke k_thud[] = {   // a ground pound's landing
  S(0, 180, 0, 1.00f, 0, 55, 120, 70, 0), S(0, 80, 1, 0.80f, 0, 22, 300, 200, 0) };
static const Stroke k_hit[] = {    // hurt / powered down: a jolt and its echo
  S(0, 130, 0, 1.00f, 0, 60, 150, 110, 0), S(0, 100, 1, 0.80f, 0, 40, 340, 300, 0),
  S(140, 130, 0, 0.70f, 0, 60, 140, 100, 0), S(140, 90, 1, 0.50f, 0, 35, 320, 300, 0) };
static const Stroke k_death[] = {  // a life lost: knocks fading out (NSMBW's shape)
  S(0, 160, 0, 1.00f, 0, 60, 110, 80, 0), S(0, 70, 1, 0.60f, 0, 25, 260, 220, 0),
  S(120, 160, 0, 0.85f, 0, 60, 105, 80, 0), S(250, 160, 0, 0.70f, 0, 60, 100, 75, 0),
  S(390, 160, 0, 0.55f, 0, 60, 95, 70, 0), S(540, 160, 0, 0.40f, 0, 60, 90, 70, 0),
  S(700, 160, 0, 0.28f, 0, 60, 85, 65, 0) };
static const Stroke k_quake[] = {  // the ground shakes (strength scales it)
  S(0, 340, 0, 1.00f, 0, 110, 85, 60, 0), S(0, 120, 1, 0.45f, 0, 40, 210, 180, 0) };
static const Stroke k_big[] = {    // a Mega Mushroom's ground pound
  S(0, 560, 0, 1.00f, 0, 170, 90, 50, 0), S(0, 160, 1, 0.65f, 0, 55, 240, 180, 0) };
// The power-up jingle (player/powerup.ogg, also the Propeller, Penguin and
// Gummy collect sounds) is three rising runs of notes, 0, 320 and 640 ms in,
// each 0.3 s and each starting higher (tops 1325, 1675, 1900 Hz): a rising
// sparkle on each run, each a little higher and stronger.
static const Stroke k_powerup[] = {
  S(0, 290, 1, 0.85f, 30, 140, 300, 650, 22),   S(0, 120, 0, 0.40f, 10, 50, 160, 190, 0),
  S(320, 290, 1, 0.90f, 30, 140, 360, 760, 22), S(320, 120, 0, 0.40f, 10, 50, 165, 195, 0),
  S(640, 300, 1, 0.95f, 30, 150, 420, 860, 22), S(640, 130, 0, 0.45f, 10, 55, 170, 200, 0) };
// the Mini Mushroom's: three quicker runs (0, 220, 420 ms), lighter and higher
static const Stroke k_small[] = {
  S(0, 190, 1, 0.70f, 20, 90, 520, 860, 26),   S(0, 80, 0, 0.25f, 8, 35, 180, 210, 0),
  S(220, 190, 1, 0.72f, 20, 90, 560, 900, 26), S(220, 80, 0, 0.25f, 8, 35, 185, 215, 0),
  S(420, 200, 1, 0.75f, 20, 95, 600, 940, 26), S(420, 80, 0, 0.28f, 8, 35, 190, 220, 0) };
// the Super Leaf's (powerup/tanookichange.ogg: a poof, then a rising run at
// 360-700 ms): a soft low puff, then a sparkle that climbs
static const Stroke k_leaf[] = {
  S(40, 170, 0, 0.60f, 10, 70, 130, 95, 0),     S(40, 90, 1, 0.45f, 10, 40, 260, 220, 0),
  S(360, 340, 1, 0.85f, 40, 160, 380, 820, 20), S(500, 100, 0, 0.30f, 10, 40, 170, 200, 0) };
// the Gold Flower's (powerup/goldcollect.ogg: two quick glints, 40 and 200 ms,
// then a long run from 540 ms up to 2800 Hz)
static const Stroke k_gold[] = {
  S(40, 110, 1, 0.75f, 12, 55, 420, 620, 0),    S(200, 110, 1, 0.80f, 12, 55, 560, 860, 0),
  S(540, 540, 1, 0.95f, 40, 260, 440, 900, 24), S(540, 130, 0, 0.45f, 10, 55, 165, 200, 0) };
static const Stroke k_mega[] = {   // growing giant, then landing on the size
  S(0, 900, 0, 0.95f, 750, 0, 60, 150, 0), S(0, 900, 1, 0.60f, 750, 0, 200, 520, 12),
  S(900, 280, 0, 1.00f, 0, 90, 95, 60, 0) };
static const Stroke k_oneup[] = {  // a 1-Up: two bright taps
  S(0, 70, 1, 0.85f, 0, 28, 460, 460, 0), S(0, 55, 0, 0.40f, 0, 20, 180, 180, 0),
  S(140, 70, 1, 0.85f, 0, 28, 620, 620, 0), S(140, 55, 0, 0.40f, 0, 20, 180, 180, 0) };
#undef S
#define E(a) { a, (int)(sizeof a / sizeof a[0]) }
static const Effect k_effects[] = {
  { NULL, 0 }, E(k_thud), E(k_hit), E(k_death), E(k_quake), E(k_big), E(k_powerup), E(k_small), E(k_mega), E(k_oneup),
  E(k_leaf), E(k_gold),
};
#undef E
#define NUM_EFFECTS ((int)(sizeof k_effects / sizeof k_effects[0]))

typedef struct {
  int effect;
  float strength;
  u64 start;   // ns
  u32 len_ms;  // how long it lasts
} Playing;

static Mutex s_rlock;
static CondVar s_rcv;
static Thread s_rthread;
static int s_rstarted;
static Playing s_playing[4];
static HidNpadIdType s_rid;  // the controller the effects go to
static u32 s_rstyle;
static int s_rcount;
static u64 s_rgen;           // bumped when the controller changes

static float stroke_amp(const Stroke *k, float t_ms, float *freq) {
  if (t_ms < k->at || t_ms >= (float)(k->at + k->len))
    return 0.f;
  const float t = t_ms - k->at;
  float a = k->amp;
  if (k->attack && t < k->attack)
    a *= t / k->attack;
  else if (k->tau)
    a *= expf(-(t - k->attack) / k->tau);
  if (k->trem > 0.f)
    a *= 0.65f + 0.35f * sinf(6.2831853f * k->trem * t / 1000.f);
  *freq = k->f0 + (k->f1 - k->f0) * (t / k->len);
  return a;
}

static void rumble_thread(void *arg) {
  (void)arg;
  HidVibrationDeviceHandle handles[2];
  int count = 0;
  u64 gen = 0;
  HidVibrationValue last = { 0 };
  int quiet = 1;
  for (;;) {
    mutexLock(&s_rlock);
    int any = 0;
    for (int i = 0; i < 4; i++)
      any |= s_playing[i].effect != 0;
    while (!any && quiet) {
      condvarWait(&s_rcv, &s_rlock);
      for (int i = 0; i < 4; i++)
        any |= s_playing[i].effect != 0;
    }
    const u64 now = armTicksToNs(armGetSystemTick());
    float lo = 0.f, hi = 0.f, flo = 160.f, fhi = 320.f;
    for (int i = 0; i < 4; i++) {
      Playing *p = &s_playing[i];
      if (!p->effect)
        continue;
      const float t = (float)((now - p->start) / 1000000ull);
      if (t >= p->len_ms) {
        p->effect = 0;
        continue;
      }
      const Effect *e = &k_effects[p->effect];
      for (int k = 0; k < e->n; k++) {
        float f = 0.f;
        const float a = stroke_amp(&e->s[k], t, &f) * p->strength;
        if (e->s[k].high ? a > hi : a > lo) {
          if (e->s[k].high)
            hi = a, fhi = f;
          else
            lo = a, flo = f;
        }
      }
    }
    if (gen != s_rgen || !count) {
      gen = s_rgen;
      count = 0;
      if (s_rcount && R_SUCCEEDED(hidInitializeVibrationDevices(handles, s_rcount, s_rid, (HidNpadStyleTag)s_rstyle)))
        count = s_rcount;
      last.amp_low = last.amp_high = -1.f; // send to the new controller
    }
    mutexUnlock(&s_rlock);
    // full strength on the low band (HW round 7: "rumble strength is
    // weak" at 0.75), the high band a little lighter so a sparkle is not a
    // shriek
    HidVibrationValue v = { 0 };
    v.amp_low = lo > 1.f ? 1.f : lo;
    v.amp_high = 0.85f * (hi > 1.f ? 1.f : hi);
    v.freq_low = flo < 40.f ? 40.f : flo > 600.f ? 600.f : flo;
    v.freq_high = fhi < 80.f ? 80.f : fhi > 1200.f ? 1200.f : fhi;
    quiet = v.amp_low < 0.005f && v.amp_high < 0.005f;
    if (quiet)
      v.amp_low = v.amp_high = 0.f, v.freq_low = 160.f, v.freq_high = 320.f;
    if (count && (v.amp_low != last.amp_low || v.amp_high != last.amp_high || v.freq_low != last.freq_low ||
                  v.freq_high != last.freq_high)) {
      HidVibrationValue values[2] = { v, v };
      if (R_SUCCEEDED(hidSendVibrationValues(handles, values, count)))
        last = v;
    }
    svcSleepThread(5000000ull); // 5 ms
  }
}

void gamepad_rumble_effect(int player, int effect, float strength) {
  static int logged;
  if (effect > 0 && effect < NUM_EFFECTS && logged < 40) {
    logged++;
    log_printf("[rumble] player %d: effect %d at %.2f%s", player + 1, effect, (double)strength,
               config.rumble ? "" : " (off: config rumble 0)");
  }
  if (!config.rumble || effect <= 0 || effect >= NUM_EFFECTS || strength <= 0.f)
    return;
  if (player < 0 || player >= MAX_PADS || !s_init)
    return;
  int slot = player;
  if (player == 0 && s_players <= 1 && s_menu_slot >= 0)
    slot = s_menu_slot; // menus and online: player 1 is the first connected
  if (!padIsConnected(&s_pads[slot]))
    return;
  const u32 attached = padGetStyleSet(&s_pads[slot]);
  HidNpadIdType id = (HidNpadIdType)(HidNpadIdType_No1 + slot);
  u32 style;
  int count;
  if (slot == 0 && (attached & HidNpadStyleTag_NpadHandheld)) {
    id = HidNpadIdType_Handheld, style = HidNpadStyleTag_NpadHandheld, count = 2;
  } else if (attached & HidNpadStyleTag_NpadFullKey) {
    style = HidNpadStyleTag_NpadFullKey, count = 2;
  } else if (attached & HidNpadStyleTag_NpadJoyDual) {
    style = HidNpadStyleTag_NpadJoyDual, count = 2;
  } else if (attached & HidNpadStyleTag_NpadJoyLeft) {
    style = HidNpadStyleTag_NpadJoyLeft, count = 1;
  } else if (attached & HidNpadStyleTag_NpadJoyRight) {
    style = HidNpadStyleTag_NpadJoyRight, count = 1;
  } else {
    return; // nothing that can rumble
  }
  if (!s_rstarted) {
    s_rstarted = 1;
    mutexInit(&s_rlock);
    condvarInit(&s_rcv);
    if (R_FAILED(threadCreate(&s_rthread, rumble_thread, NULL, NULL, 0x4000, 0x2c, -2)) ||
        R_FAILED(threadStart(&s_rthread))) {
      log_printf("[rumble] no thread: off");
      return;
    }
  }
  const Effect *e = &k_effects[effect];
  u32 len = 0;
  for (int k = 0; k < e->n; k++)
    if ((u32)(e->s[k].at + e->s[k].len) > len)
      len = e->s[k].at + e->s[k].len;
  mutexLock(&s_rlock);
  if (id != s_rid || style != s_rstyle || count != s_rcount) {
    s_rid = id, s_rstyle = style, s_rcount = count;
    s_rgen++;
  }
  // a free place, else the one nearest its end
  int at = 0;
  u64 best = ~0ull;
  const u64 now = armTicksToNs(armGetSystemTick());
  for (int i = 0; i < 4; i++) {
    if (!s_playing[i].effect) {
      at = i;
      break;
    }
    const u64 end = s_playing[i].start + (u64)s_playing[i].len_ms * 1000000ull;
    if (end < best)
      best = end, at = i;
  }
  s_playing[at].effect = effect;
  s_playing[at].strength = strength > 1.f ? 1.f : strength;
  s_playing[at].start = now;
  s_playing[at].len_ms = len;
  condvarWakeOne(&s_rcv);
  mutexUnlock(&s_rlock);
}
