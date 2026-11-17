/* test_script.c -- debug: scripted input (config test_script)
 *
 * For checks without hands on the console (Ryujinx): the file named by
 * test_script (in the port's folder) lists timed inputs, one per line,
 * seconds since the main loop started:
 *
 *     35 tap 545 361      a finger down at (x, y) in 1280x720 panel pixels,
 *                         held for 6 frames
 *     38 drag 640 600 640 200 0.5   a finger dragged from (x, y) to
 *                         (x2, y2) over 0.5 seconds, then lifted
 *     40 key back         Android BACK pressed and released
 *     50 pause 8          focus lost for 8 seconds, as with HOME (the game
 *                         pauses and resumes)
 *     60 stick 1 0 2.5    the left stick at (1, 0) for 2.5 seconds (x right,
 *                         y up, -1..1); "rstick" the right one
 *     62 press A          a controller button pressed for 3 frames (A B X Y
 *                         L R ZL ZR PLUS MINUS UP DOWN LEFT RIGHT)
 *     64 hold ZR 1.5      held for 1.5 seconds
 *     0 kbd Racer 2       the next soft keyboard answers "Racer 2" at once
 *                         (no console keyboard: Ryujinx's needs a person)
 *     70 p2 press A       "p2 " before stick / rstick / press / hold: player
 *                         2's controller (its slot counts as connected while
 *                         scripted, so local play has a second gamepad)
 *
 * Lines starting with # are comments. Each input is logged when it fires.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "config.h"
#include "log.h"
#include "paths.h"

typedef struct {
  float at;
  int key;     // 1: BACK; 2: pause for x seconds; 3: stick (x, y, for dur);
               // 4: right stick; 5: button for dur; 0: tap (a drag when
               // dur > 0: to x2, y2)
  float x, y, dur, x2, y2;
  u64 button;
  int player;  // controller input: 0 player 1, 1 player 2 .. 3 player 4
} Step;

static Step s_steps[256];
static Step s_pads[64];     // controller input: any number at once, by time
static int s_npads;
static char s_kbd[64];      // the next soft keyboard's answer ("" none)

static u64 button_of(const char *n) {
  static const struct { const char *name; u64 b; } k[] = {
    { "A", HidNpadButton_A }, { "B", HidNpadButton_B }, { "X", HidNpadButton_X }, { "Y", HidNpadButton_Y },
    { "L", HidNpadButton_L }, { "R", HidNpadButton_R }, { "ZL", HidNpadButton_ZL }, { "ZR", HidNpadButton_ZR },
    { "PLUS", HidNpadButton_Plus }, { "MINUS", HidNpadButton_Minus }, { "UP", HidNpadButton_Up },
    { "DOWN", HidNpadButton_Down }, { "LEFT", HidNpadButton_Left }, { "RIGHT", HidNpadButton_Right },
  };
  for (size_t i = 0; i < sizeof k / sizeof *k; i++)
    if (!strcmp(n, k[i].name))
      return k[i].b;
  return 0;
}
static int s_n, s_next, s_hold;
static u64 s_t0;
static float s_resume_at = -1; // a scripted pause ends then

void test_script_init(void) {
  if (!config.test_script[0])
    return;
  char path[600];
  snprintf(path, sizeof path, "%s/%s", path_base(), config.test_script);
  FILE *f = fopen(path, "r");
  if (!f) {
    log_printf("[script] cannot open %s", path);
    return;
  }
  char raw[160], line[160];
  while (s_n < 256 && fgets(raw, sizeof raw, f)) {
    Step st = { 0 };
    char what[16];
    snprintf(line, sizeof line, "%s", raw);
    if (line[0] == '#' || sscanf(line, "%f %15s", &st.at, what) != 2)
      continue;
    if (what[0] == 'p' && what[1] >= '2' && what[1] <= '4' && !what[2]) {
      // "<t> p2 <input...>" (p3, p4): the same input on that player's controller
      const int player = what[1] - '1';
      const char *rest = strstr(raw, what) + 2;
      snprintf(line, sizeof line, "%f%s", (double)st.at, rest);
      if (sscanf(line, "%f %15s", &st.at, what) != 2)
        continue;
      st.player = player;
    }
    if (!strcmp(what, "tap") && sscanf(line, "%*f %*s %f %f", &st.x, &st.y) == 2)
      s_steps[s_n++] = st;
    else if (!strcmp(what, "drag") &&
             sscanf(line, "%*f %*s %f %f %f %f %f", &st.x, &st.y, &st.x2, &st.y2, &st.dur) == 5 && st.dur > 0)
      s_steps[s_n++] = st;
    else if (!strcmp(what, "kbd")) {
      const char *t = strstr(line, "kbd") + 3;
      t += strspn(t, " \t");
      snprintf(s_kbd, sizeof s_kbd, "%.*s", (int)strcspn(t, "\r\n"), t);
    } else if (!strcmp(what, "key"))
      st.key = 1, s_steps[s_n++] = st;
    else if (!strcmp(what, "pause") && sscanf(line, "%*f %*s %f", &st.x) == 1)
      st.key = 2, s_steps[s_n++] = st;
    else if ((!strcmp(what, "stick") || !strcmp(what, "rstick")) &&
             sscanf(line, "%*f %*s %f %f %f", &st.x, &st.y, &st.dur) == 3)
      st.key = what[0] == 'r' ? 4 : 3, s_pads[s_npads++ % 64] = st;
    else if (!strcmp(what, "press") || !strcmp(what, "hold")) {
      char name[16];
      st.dur = 0.05f;
      if (sscanf(line, "%*f %*s %15s %f", name, &st.dur) >= 1 && (st.button = button_of(name)))
        st.key = 5, s_pads[s_npads++ % 64] = st;
    }
  }
  fclose(f);
  s_t0 = armGetSystemTick();
  log_printf("[script] %d inputs from %s", s_n, path);
}

static float now_s(void) {
  return (float)armTicksToNs(armGetSystemTick() - s_t0) / 1e9f;
}

// a player's scripted controller now: buttons held and sticks (0 when none)
void test_script_pad_player(int player, u64 *held, int *lx, int *ly, int *rx, int *ry) {
  if (!s_npads)
    return;
  const float now = now_s();
  for (int i = 0; i < s_npads && i < 64; i++) {
    const Step *p = &s_pads[i];
    if (p->player != player || now < p->at || now >= p->at + p->dur)
      continue;
    if (p->key == 5)
      *held |= p->button;
    else if (p->key == 3)
      *lx = (int)(p->x * 32767), *ly = (int)(p->y * 32767);
    else
      *rx = (int)(p->x * 32767), *ry = (int)(p->y * 32767);
  }
}

void test_script_pad(u64 *held, int *lx, int *ly, int *rx, int *ry) {
  test_script_pad_player(0, held, lx, ly, rx, ry);
}

// 1 when the script has input for that player
int test_script_uses_player(int player) {
  for (int i = 0; i < s_npads && i < 64; i++)
    if (s_pads[i].player == player)
      return 1;
  return 0;
}

// the scripted answer to a soft keyboard (once): 1 with it in out
int test_script_keyboard(char *out, size_t n) {
  if (!s_kbd[0])
    return 0;
  snprintf(out, n, "%s", s_kbd);
  log_printf("[script] %.1f s: keyboard answered \"%s\"", now_s(), s_kbd);
  s_kbd[0] = '\0';
  return 1;
}

// a scripted focus change: 0 to lose focus, 1 to get it back, -1 none
int test_script_focus(void) {
  if (s_resume_at >= 0) {
    if (now_s() < s_resume_at)
      return -1;
    s_resume_at = -1;
    log_printf("[script] %.1f s: focus back", now_s());
    return 1;
  }
  if (s_next >= s_n || s_steps[s_next].key != 2 || now_s() < s_steps[s_next].at)
    return -1;
  const Step *st = &s_steps[s_next++];
  s_resume_at = now_s() + st->x;
  log_printf("[script] %.1f s: focus lost for %.0f s", now_s(), (double)st->x);
  return 0;
}

// this frame's scripted finger (1 when one is down), in panel pixels;
// *back gets 1 on the frame BACK is pressed, 2 on the one it is released
static float s_drag_t0;

int test_script_poll(float *x, float *y, int *back) {
  *back = 0;
  if (s_hold > 0) {
    const Step *st = &s_steps[s_next - 1];
    if (st->key) {
      *back = 2;
      s_hold = 0;
      return 0;
    }
    if (st->dur > 0) { // a drag: along the line, then lifted
      const float f = (now_s() - s_drag_t0) / st->dur;
      if (f >= 1) {
        s_hold = 0;
        return 0;
      }
      *x = st->x + (st->x2 - st->x) * f;
      *y = st->y + (st->y2 - st->y) * f;
      return 1;
    }
    *x = st->x;
    *y = st->y;
    s_hold--;
    return 1;
  }
  if (s_next >= s_n || s_steps[s_next].key == 2)
    return 0;
  const float now = now_s();
  if (now < s_steps[s_next].at)
    return 0;
  const Step *st = &s_steps[s_next++];
  if (st->key) {
    log_printf("[script] %.1f s: BACK", now);
    *back = 1;
    s_hold = 1;
    return 0;
  }
  if (st->dur > 0)
    log_printf("[script] %.1f s: drag %.0f,%.0f -> %.0f,%.0f", now, st->x, st->y, st->x2, st->y2);
  else
    log_printf("[script] %.1f s: tap %.0f,%.0f", now, st->x, st->y);
  *x = st->x;
  *y = st->y;
  s_hold = 5;
  s_drag_t0 = now;
  return 1;
}
