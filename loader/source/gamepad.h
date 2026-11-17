/* gamepad.h -- playing with a controller (gamepad.c)
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef GAMEPAD_H
#define GAMEPAD_H

#include <switch.h>

// the controller this frame (main.c: the console's, with scripted input)
typedef struct {
  u64 held, down;
  int lx, ly, rx, ry;   // sticks, -32767..32767 (y up)
} PadInput;

// one frame: fingers for the game (input.c) from the controller; w x h is
// the window; touchscreen_used hides the pointer
void gamepad_update(const PadInput *pad, int w, int h, int touchscreen_used);
// the game's C# is up (main.c, once game_patch_il2cpp has run): the
// controller may ask it things from then on
void gamepad_il2cpp_up(void);
// the console's controller applet for exactly 'players' players (names and
// RGB colours per player, either may be NULL); blocks until it closes, then
// the controllers are announced to Unity again in slot order. Returns the
// player count (0 when cancelled).
int gamepad_pick_controllers(int players, const char *const *names, const u8 (*rgb)[3]);
// the same for min..max players (the console asks who joins)
int gamepad_pick_controllers_range(int min, int max, const char *const *names, const u8 (*rgb)[3]);
// the highest player slot with a controller (or scripted input) now, 0 when none
int gamepad_connected(void);
// how many gamepads Unity sees: 1 in menus (player 1's, the first connected
// slot), one per player in local play (game_patch.c sets it)
void gamepad_set_players(int players);
int gamepad_players(void);
// Rumble on a player's controller (0 = player 1, whose controller plays the
// menus and online matches): one of the effects in gamepad.c (1 thud, 2 hit,
// 3 death, 4 quake, 5 big thud, 6 power-up, 7 small power-up, 8 Mega
// Mushroom, 9 1-Up) at a strength 0..1; config rumble 0 = off.
void gamepad_rumble_effect(int player, int effect, float strength);
// 1 while the title screen is up: any button on player 1 is A
void gamepad_title_any(int on);
// 1 once after player 1 pressed B (game_patch.c: back in menus and pause)
int gamepad_take_back_request(void);
// 1 once after a player held L+R (SL+SR on a single Joy-Con): show the
// controller applet (game_patch.c, on the main thread)
int gamepad_take_screen_request(void);
// the Local Play room is up (game_patch.c): - and ZL+ZR there ask for the
// controller screen too
void gamepad_set_local_room(int on);
// the Joy-Cons attached to the console are the only controller (handheld
// play, nothing else connected): the controller screen's multi-player mode
// does not take them
int gamepad_handheld_only(void);
// the ring around the button the controller is on, for drawing (egl_shim.c):
// 1 when it is to be drawn, with its rectangle in window pixels
int gamepad_ring(float *x0, float *y0, float *x1, float *y1);
// the pointer, for drawing (egl_shim.c): 1 when it is to be drawn
int gamepad_pointer(float *x, float *y);

#endif
