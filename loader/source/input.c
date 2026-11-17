/* input.c -- touch and keys into Unity as Android MotionEvents and KeyEvents
 *
 * Unity reads an event back through JNI during nativeInjectEvent (action,
 * pointers, coordinates, source, times). One MotionEvent and one KeyEvent
 * object are reused for every event sent; Unity 2022 keeps a touch for
 * later with MotionEvent.obtain(event), which gets a copy of its own
 * (recycled copies are reused).
 *
 * Per frame the caller reports every finger (input_touch) and then calls
 * input_flush, which turns the changes into the Android sequence: DOWN for
 * the first finger, POINTER_DOWN/POINTER_UP for the others, MOVE for motion,
 * UP when the last one lifts.
 *
 * Controllers (input_pad) are Android gamepads: InputDevice ids 2..5 with
 * SOURCE_GAMEPAD | SOURCE_JOYSTICK, buttons as KeyEvents (by label: Switch A
 * is KEYCODE_BUTTON_A, so A confirms and B goes back in Unity's UI, as on the
 * console), sticks / D-pad (hat) / triggers as joystick MotionEvent axes.
 * Unity's InputDeviceListener hears of controllers coming and going.
 *
 * The soft keyboard Unity opens for input fields is the console's (swkbd);
 * the text goes back through nativeSetInputString / nativeSoftInputClosed.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "config.h"
#include "jni.h"
#include "libc_shim.h"
#include "log.h"
#include "unity.h"

#define MAX_PTR 10

typedef struct {
  int down;
  float x, y;
} Ptr;

static Ptr s_cur[MAX_PTR], s_new[MAX_PTR];
static JObj *s_motion, *s_key, *s_device;
static int64_t s_down_time;

// a MotionEvent's content: pointer ids and positions in index order
#define NAXES 24 // AXIS_X 0 .. AXIS_BRAKE 23

typedef struct {
  int action;
  int count;
  int id[MAX_PTR];
  float x[MAX_PTR], y[MAX_PTR];
  int64_t time, down_time;
  int source, device;  // SOURCE_TOUCHSCREEN / device 1, or a controller's
  float axis[NAXES];   // joystick events
} MotionData;

#define SRC_TOUCH    0x1002
#define SRC_KEYBOARD 0x101
#define SRC_GAMEPAD  0x401
#define SRC_JOYSTICK 0x1000010
#define TOUCH_DEVICE 1
#define PAD_DEVICE0  2

// an InputDevice (JObj.native)
typedef struct {
  int id, sources, keyboard_type, controller_number;
  const char *name, *descriptor;
  JObj *ranges;  // java.util.List of MotionRange, NULL for the touchscreen
} DevInfo;

// a MotionRange (JObj.native)
typedef struct {
  int axis, source;
  float min, max, flat, fuzz;
} RangeInfo;

#define MAX_PADS 4
static JObj *s_pad_dev[MAX_PADS];
static int s_pad_on[MAX_PADS];
static JObj *s_dev_listener;

// the event being sent (s_motion's); copies made by MotionEvent.obtain
// carry their own (JObj.native), and recycled ones are kept for reuse
static MotionData ev;
static JObj *s_free_motion[64];
static int s_nfree_motion;

static const MotionData *md(const JObj *o) {
  return o && o->native ? (const MotionData *)o->native : &ev;
}

static struct {
  int action, code, repeat;
  int64_t time, down_time;
  int source, device;
} kev;

// SystemClock.uptimeMillis's clock, as Android's event times
static int64_t now_ms(void) {
  return android_monotonic_ns() / 1000000;
}

// --- MotionEvent ---------------------------------------------------------------------

static int idx_arg(const MotionData *e, const JMethod *m, const jvalue *a) {
  const int i = m->nargs >= 1 && m->argt[m->nargs - 1] == 'I' ? a[m->nargs - 1].i : 0;
  return i >= 0 && i < e->count ? i : 0;
}

JH(me_getAction) { JUNUSED; return jv_i(md(thiz)->action); }
JH(me_getActionMasked) { JUNUSED; return jv_i(md(thiz)->action & 0xff); }
JH(me_getActionIndex) { JUNUSED; return jv_i((md(thiz)->action >> 8) & 0xff); }
JH(me_getPointerCount) { JUNUSED; return jv_i(md(thiz)->count); }
JH(me_getPointerId) { JUNUSED; const MotionData *e = md(thiz); return jv_i(e->id[idx_arg(e, m, a)]); }
JH(me_findPointerIndex) {
  JUNUSED;
  const MotionData *e = md(thiz);
  for (int i = 0; i < e->count; i++)
    if (e->id[i] == a[0].i)
      return jv_i(i);
  return jv_i(-1);
}
JH(me_getX) { JUNUSED; const MotionData *e = md(thiz); return jv_f(e->x[idx_arg(e, m, a)]); }
JH(me_getY) { JUNUSED; const MotionData *e = md(thiz); return jv_f(e->y[idx_arg(e, m, a)]); }
JH(me_getAxisValue) {
  JUNUSED;
  // (axis) or (axis, pointerIndex): AXIS_X 0, AXIS_Y 1, PRESSURE 2, SIZE 3
  const MotionData *e = md(thiz);
  const int axis = a[0].i;
  if (e->source == SRC_JOYSTICK)
    return jv_f(axis >= 0 && axis < NAXES ? e->axis[axis] : 0.0f);
  const int i = m->nargs > 1 && a[1].i >= 0 && a[1].i < e->count ? a[1].i : 0;
  switch (axis) {
    case 0: return jv_f(e->x[i]);
    case 1: return jv_f(e->y[i]);
    case 2: return jv_f(1.0f);
    case 3: return jv_f(0.1f);
    default: return jv_f(0.0f);
  }
}
JH(me_getEventTime) { JUNUSED; return jv_j(md(thiz)->time); }
JH(me_getEventTimeNanos) { JUNUSED; return jv_j(md(thiz)->time * 1000000ll); }
JH(me_getDownTime) { JUNUSED; return jv_j(md(thiz)->down_time); }
// MotionEvent.obtain(MotionEvent): a copy of its content, kept by Unity
// to read later (Unity 2022 queues the copy)
JH(me_obtain) {
  JUNUSED;
  JObj *o = s_nfree_motion ? s_free_motion[--s_nfree_motion] : jni_new("android/view/MotionEvent");
  if (!o->native)
    o->native = malloc(sizeof(MotionData));
  memcpy(o->native, md(m->nargs ? a[0].l : NULL), sizeof(MotionData));
  return jv_l(o);
}
// recycle(): a copy goes back for the next obtain
JH(me_recycle) {
  JUNUSED;
  if (thiz && thiz != s_motion && thiz->native && s_nfree_motion < 64)
    s_free_motion[s_nfree_motion++] = thiz;
  return jv_v();
}
static JObj *device_obj(int id);
JH(me_getSource) { JUNUSED; const MotionData *e = md(thiz); return jv_i(e->source ? e->source : SRC_TOUCH); }
JH(me_getDeviceId) { JUNUSED; const MotionData *e = md(thiz); return jv_i(e->device ? e->device : TOUCH_DEVICE); }
JH(me_getToolType) { JUNUSED; return jv_i(md(thiz)->source == SRC_JOYSTICK ? 0 : 1); } // UNKNOWN / FINGER
JH(me_getPressure) { JUNUSED; return jv_f(1.0f); }
JH(me_getSize) { JUNUSED; return jv_f(0.1f); }
JH(me_getTouchMajor) { JUNUSED; return jv_f(20.0f); }
JH(me_zero_i) { JUNUSED; return jv_i(0); }
JH(me_zero_f) { JUNUSED; return jv_f(0.0f); }
JH(me_zero_j) { JUNUSED; return jv_j(0); }
JH(me_isFromSource) {
  JUNUSED;
  const MotionData *e = md(thiz);
  const int src = e->source ? e->source : SRC_TOUCH;
  return jv_z((a[0].i & src) == a[0].i);
}
JH(me_getDevice) { JUNUSED; const MotionData *e = md(thiz); return jv_l(device_obj(e->device ? e->device : TOUCH_DEVICE)); }

// --- KeyEvent --------------------------------------------------------------------------

JH(ke_getAction) { JUNUSED; return jv_i(kev.action); }
JH(ke_getKeyCode) { JUNUSED; return jv_i(kev.code); }
JH(ke_getRepeatCount) { JUNUSED; return jv_i(kev.repeat); }
JH(ke_getEventTime) { JUNUSED; return jv_j(kev.time); }
JH(ke_getDownTime) { JUNUSED; return jv_j(kev.down_time); }
JH(ke_getSource) { JUNUSED; return jv_i(kev.source); }
JH(ke_getDeviceId) { JUNUSED; return jv_i(kev.device); }
JH(ke_getDevice) { JUNUSED; return jv_l(device_obj(kev.device)); }
JH(ke_getUnicodeChar) { JUNUSED; return jv_i(0); }
JH(ke_getCharacters) { JUNUSED; return jv_l(NULL); }
JH(ke_isFromSource) { JUNUSED; return jv_z((a[0].i & kev.source) == a[0].i); }

// --- InputEvent: methods Unity looks up on the base class -----------------------------
// (KeyEvent or MotionEvent decided by the object itself)
static int is_key(JObj *o) { return o && jni_instance_of(o, "android/view/KeyEvent"); }
JH(ie_getDevice) { return is_key(thiz) ? ke_getDevice(thiz, m, a) : me_getDevice(thiz, m, a); }
JH(ie_getDeviceId) { return is_key(thiz) ? ke_getDeviceId(thiz, m, a) : me_getDeviceId(thiz, m, a); }
JH(ie_getSource) { return is_key(thiz) ? ke_getSource(thiz, m, a) : me_getSource(thiz, m, a); }
JH(ie_isFromSource) { return is_key(thiz) ? ke_isFromSource(thiz, m, a) : me_isFromSource(thiz, m, a); }
JH(ie_getEventTime) { return is_key(thiz) ? ke_getEventTime(thiz, m, a) : me_getEventTime(thiz, m, a); }

// --- InputDevice -------------------------------------------------------------------------

static const DevInfo *dinfo(JObj *o) {
  static const DevInfo none = { 0 };
  return o && o->native ? (const DevInfo *)o->native : &none;
}

static JObj *device_obj(int id) {
  if (id == TOUCH_DEVICE)
    return s_device;
  const int i = id - PAD_DEVICE0;
  return i >= 0 && i < MAX_PADS && s_pad_on[i] ? s_pad_dev[i] : NULL;
}

JH(dev_get) { JUNUSED; return jv_l(device_obj(m->nargs ? a[0].i : TOUCH_DEVICE)); }
JH(dev_getName) { JUNUSED; return jv_l(jni_str(dinfo(thiz)->name ? dinfo(thiz)->name : "")); }
JH(dev_getSources) { JUNUSED; return jv_i(dinfo(thiz)->sources); }
JH(dev_getId) { JUNUSED; return jv_i(dinfo(thiz)->id); }
JH(dev_getIds) {
  JUNUSED;
  int ids[1 + MAX_PADS], n = 0;
  ids[n++] = TOUCH_DEVICE;
  for (int i = 0; i < MAX_PADS; i++)
    if (s_pad_on[i])
      ids[n++] = PAD_DEVICE0 + i;
  JObj *arr = jni_array('I', n, NULL);
  memcpy(jni_arr(arr)->data, ids, n * sizeof(int32_t));
  return jv_l(arr);
}
JH(dev_isVirtual) { JUNUSED; return jv_z(0); }
JH(dev_isExternal) { JUNUSED; return jv_z(dinfo(thiz)->id >= PAD_DEVICE0); }
JH(dev_getMotionRanges) { JUNUSED; return jv_l(dinfo(thiz)->ranges); }
// getMotionRange(axis) or (axis, source)
JH(dev_getMotionRange) {
  JUNUSED;
  JObj *list = dinfo(thiz)->ranges;
  if (!list)
    return jv_l(NULL);
  for (int i = 0, n = jni_list_size(list); i < n; i++) {
    JObj *r = jni_list_get(list, i);
    const RangeInfo *ri = r->native;
    if (ri->axis == a[0].i && (m->nargs < 2 || (a[1].i & ri->source) == a[1].i))
      return jv_l(r);
  }
  return jv_l(NULL);
}
JH(dev_getDescriptor) { JUNUSED; return jv_l(jni_str(dinfo(thiz)->descriptor ? dinfo(thiz)->descriptor : "")); }
JH(dev_getKeyboardType) { JUNUSED; return jv_i(dinfo(thiz)->keyboard_type); }
JH(dev_getControllerNumber) { JUNUSED; return jv_i(dinfo(thiz)->controller_number); }
JH(dev_zero) { JUNUSED; return jv_i(0); }
// hasKeys(int...): a controller has every gamepad key it is asked about
JH(dev_hasKeys) {
  JUNUSED;
  JObj *in = m->nargs ? a[0].l : NULL;
  const int n = in ? jni_arr(in)->len : 0;
  JObj *out = jni_array('Z', n, NULL);
  const int pad = dinfo(thiz)->id >= PAD_DEVICE0;
  for (int i = 0; i < n; i++) {
    const int k = ((int32_t *)jni_arr(in)->data)[i];
    ((uint8_t *)jni_arr(out)->data)[i] = pad && ((k >= 96 && k <= 110) || (k >= 19 && k <= 23));
  }
  return jv_l(out);
}
// registerInputDeviceListener(listener, handler): kept, to report controllers
JH(dev_registerListener) {
  JUNUSED;
  s_dev_listener = m->nargs ? a[0].l : NULL;
  log_printf("[input] InputDeviceListener registered (%s)", s_dev_listener ? jni_class_name(s_dev_listener) : "null");
  return jv_v();
}

// MotionRange
JH(mr_getAxis) { JUNUSED; return jv_i(((RangeInfo *)thiz->native)->axis); }
JH(mr_getSource) { JUNUSED; return jv_i(((RangeInfo *)thiz->native)->source); }
JH(mr_getMin) { JUNUSED; return jv_f(((RangeInfo *)thiz->native)->min); }
JH(mr_getMax) { JUNUSED; return jv_f(((RangeInfo *)thiz->native)->max); }
JH(mr_getRange) { JUNUSED; const RangeInfo *r = thiz->native; return jv_f(r->max - r->min); }
JH(mr_getFlat) { JUNUSED; return jv_f(((RangeInfo *)thiz->native)->flat); }
JH(mr_getFuzz) { JUNUSED; return jv_f(((RangeInfo *)thiz->native)->fuzz); }
JH(mr_getResolution) { JUNUSED; return jv_f(0.0f); }
JH(mr_isFromSource) { JUNUSED; const RangeInfo *r = thiz->native; return jv_z((a[0].i & r->source) == a[0].i); }

static JObj *range_new(int axis, float min, float max, float flat) {
  JObj *o = jni_new("android/view/InputDevice$MotionRange");
  RangeInfo *r = calloc(1, sizeof *r);
  r->axis = axis, r->source = SRC_JOYSTICK, r->min = min, r->max = max, r->flat = flat;
  o->native = r;
  return o;
}

static void pads_init(void) {
  // AXIS_X/Y left stick, Z/RZ right stick, HAT_X/Y D-pad, L/RTRIGGER, BRAKE/GAS
  static const int stick_axes[] = { 0, 1, 11, 14, 15, 16 };
  static const int trigger_axes[] = { 17, 18, 22, 23 };
  static char names[MAX_PADS][32], descs[MAX_PADS][32];
  for (int i = 0; i < MAX_PADS; i++) {
    JObj *o = jni_new("android/view/InputDevice");
    DevInfo *d = calloc(1, sizeof *d);
    snprintf(names[i], sizeof names[i], "Switch Controller %d", i + 1);
    snprintf(descs[i], sizeof descs[i], "nx-pad-%d", i + 1);
    d->id = PAD_DEVICE0 + i;
    d->sources = SRC_GAMEPAD | SRC_JOYSTICK;
    d->keyboard_type = 1; // KEYBOARD_TYPE_NON_ALPHABETIC
    d->controller_number = i + 1;
    d->name = names[i];
    d->descriptor = descs[i];
    d->ranges = jni_list_new();
    for (unsigned k = 0; k < sizeof stick_axes / sizeof *stick_axes; k++)
      jni_list_add(d->ranges, range_new(stick_axes[k], -1.0f, 1.0f, k < 4 ? 0.1f : 0.0f));
    for (unsigned k = 0; k < sizeof trigger_axes / sizeof *trigger_axes; k++)
      jni_list_add(d->ranges, range_new(trigger_axes[k], 0.0f, 1.0f, 0.0f));
    o->native = d;
    s_pad_dev[i] = o;
  }
}

// --- sending ------------------------------------------------------------------------------

static void send_motion(int action) {
  ev.source = SRC_TOUCH;
  ev.device = TOUCH_DEVICE;
  ev.action = action;
  ev.time = now_ms();
  ev.down_time = s_down_time;
  unity_inject(s_motion);
}

static void build_pointers(const Ptr *set) {
  ev.count = 0;
  for (int i = 0; i < MAX_PTR; i++) {
    if (!set[i].down)
      continue;
    ev.id[ev.count] = i;
    ev.x[ev.count] = set[i].x;
    ev.y[ev.count] = set[i].y;
    ev.count++;
  }
}

static int index_of(int id) {
  for (int i = 0; i < ev.count; i++)
    if (ev.id[i] == id)
      return i;
  return 0;
}

// per pointer id: the caller's finger it is, -1 when free. Android gives a
// new finger the lowest id not in use (the first finger is always 0), and
// Unity drops every touch with an id above 0 while the game has multi-touch
// off; the console's finger ids keep counting up.
static int s_finger_of[MAX_PTR] = { [0 ... MAX_PTR - 1] = -1 };

void input_touch(int finger, int down, float x, float y) {
  if (!down)
    return; // callers report the fingers that are down
  int id = -1;
  for (int i = 0; i < MAX_PTR && id < 0; i++)
    if (s_finger_of[i] == finger)
      id = i;
  for (int i = 0; i < MAX_PTR && id < 0; i++)
    if (s_finger_of[i] < 0) {
      id = i;
      s_finger_of[i] = finger;
    }
  if (id < 0)
    return;
  s_new[id].down = 1;
  s_new[id].x = x;
  s_new[id].y = y;
}

void input_flush(void) {
  // lifts first (with the pointer still in the list), then new fingers, then moves
  for (int id = 0; id < MAX_PTR; id++) {
    if (s_cur[id].down && !s_new[id].down) {
      build_pointers(s_cur);
      int n = 0;
      for (int k = 0; k < MAX_PTR; k++)
        n += s_cur[k].down;
      send_motion(n == 1 ? 1 : (6 | index_of(id) << 8)); // UP / POINTER_UP
      s_cur[id].down = 0;
    }
    if (!s_new[id].down)
      s_finger_of[id] = -1; // lifted (or never came down): the id is free again
  }
  for (int id = 0; id < MAX_PTR; id++) {
    if (!s_cur[id].down && s_new[id].down) {
      int n = 0;
      for (int k = 0; k < MAX_PTR; k++)
        n += s_cur[k].down;
      s_cur[id] = s_new[id];
      build_pointers(s_cur);
      if (n == 0)
        s_down_time = now_ms();
      send_motion(n == 0 ? 0 : (5 | index_of(id) << 8)); // DOWN / POINTER_DOWN
    }
  }
  int moved = 0;
  for (int id = 0; id < MAX_PTR; id++) {
    if (s_cur[id].down && s_new[id].down &&
        (s_cur[id].x != s_new[id].x || s_cur[id].y != s_new[id].y)) {
      s_cur[id] = s_new[id];
      moved = 1;
    }
  }
  if (moved) {
    build_pointers(s_cur);
    send_motion(2); // MOVE
  }
  // the next frame starts from "nothing reported": callers report every frame
  memset(s_new, 0, sizeof s_new);
}

static void send_key(int code, int down, int source, int device) {
  const int64_t t = now_ms();
  kev.source = source;
  kev.device = device;
  kev.code = code;
  kev.action = down ? 0 : 1; // ACTION_DOWN / ACTION_UP
  kev.repeat = 0;
  kev.time = t;
  if (down)
    kev.down_time = t;
  unity_inject(s_key);
}

void input_key(int code, int down) {
  send_key(code, down, SRC_KEYBOARD, TOUCH_DEVICE);
}

// --- controllers ---------------------------------------------------------------------------

// Switch button (HidNpadButton bit) -> Android keycode, by label
static const struct { u64 bit; int code; } k_pad_keys[] = {
  { HidNpadButton_A, 96 },      // KEYCODE_BUTTON_A
  { HidNpadButton_B, 97 },      // KEYCODE_BUTTON_B
  { HidNpadButton_X, 99 },      // KEYCODE_BUTTON_X
  { HidNpadButton_Y, 100 },     // KEYCODE_BUTTON_Y
  { HidNpadButton_L, 102 },     // KEYCODE_BUTTON_L1
  { HidNpadButton_R, 103 },     // KEYCODE_BUTTON_R1
  { HidNpadButton_ZL, 104 },    // KEYCODE_BUTTON_L2
  { HidNpadButton_ZR, 105 },    // KEYCODE_BUTTON_R2
  { HidNpadButton_StickL, 106 },// KEYCODE_BUTTON_THUMBL
  { HidNpadButton_StickR, 107 },// KEYCODE_BUTTON_THUMBR
  { HidNpadButton_Plus, 108 },  // KEYCODE_BUTTON_START
  { HidNpadButton_Minus, 109 }, // KEYCODE_BUTTON_SELECT
};

static void dev_listener(const char *method, int id) {
  if (!s_dev_listener)
    return;
  JObj *box = jni_box('I', (jvalue){ .i = id });
  if (jni_is_proxy(s_dev_listener))
    jni_proxy_invoke_as(s_dev_listener, "android/hardware/input/InputManager$InputDeviceListener", method, "(I)V",
                        &box, 1);
}

void input_pad(int i, int connected, uint64_t buttons, float lx, float ly, float rx, float ry) {
  static u64 s_btn[MAX_PADS];
  static u64 s_held_at_connect[MAX_PADS];
  static float s_ax[MAX_PADS][NAXES];
  if (i < 0 || i >= MAX_PADS)
    return;
  const int id = PAD_DEVICE0 + i;
  // a controller announced again (gamepad.c: local play starting or
  // ending, the controller screen) while a button is held: that hold is
  // not a new press. MMIM's B in the local room ended local play, the
  // gamepads came back with B still down, and the main menu took it as a
  // second Back (the title screen; Ryubing, 2026-11-12)
  s_held_at_connect[i] &= buttons;
  if (connected && !s_pad_on[i])
    s_held_at_connect[i] = buttons;
  buttons &= ~s_held_at_connect[i];
  if (connected != s_pad_on[i]) {
    s_pad_on[i] = connected;
    log_printf("[input] controller %d %s (device %d)", i + 1, connected ? "connected" : "gone", id);
    dev_listener(connected ? "onInputDeviceAdded" : "onInputDeviceRemoved", id);
    if (!connected) {
      for (unsigned k = 0; k < sizeof k_pad_keys / sizeof *k_pad_keys; k++)
        if (s_btn[i] & k_pad_keys[k].bit)
          send_key(k_pad_keys[k].code, 0, SRC_GAMEPAD, id);
      s_btn[i] = 0;
      memset(s_ax[i], 0, sizeof s_ax[i]);
    }
  }
  if (!connected)
    return;
  const u64 changed = buttons ^ s_btn[i];
  for (unsigned k = 0; k < sizeof k_pad_keys / sizeof *k_pad_keys; k++)
    if (changed & k_pad_keys[k].bit)
      send_key(k_pad_keys[k].code, (buttons & k_pad_keys[k].bit) != 0, SRC_GAMEPAD, id);
  s_btn[i] = buttons;

  float ax[NAXES] = { 0 };
  ax[0] = lx, ax[1] = -ly;   // Android: +Y is down
  ax[11] = rx, ax[14] = -ry;
  ax[15] = (buttons & HidNpadButton_Right ? 1.0f : 0.0f) - (buttons & HidNpadButton_Left ? 1.0f : 0.0f);
  ax[16] = (buttons & HidNpadButton_Down ? 1.0f : 0.0f) - (buttons & HidNpadButton_Up ? 1.0f : 0.0f);
  ax[17] = ax[23] = buttons & HidNpadButton_ZL ? 1.0f : 0.0f;  // LTRIGGER, BRAKE
  ax[18] = ax[22] = buttons & HidNpadButton_ZR ? 1.0f : 0.0f;  // RTRIGGER, GAS
  if (memcmp(ax, s_ax[i], sizeof ax) != 0) {
    memcpy(s_ax[i], ax, sizeof ax);
    MotionData saved = ev;
    memset(&ev, 0, sizeof ev);
    ev.count = 1;
    ev.x[0] = ax[0], ev.y[0] = ax[1];
    ev.source = SRC_JOYSTICK;
    ev.device = id;
    memcpy(ev.axis, ax, sizeof ax);
    ev.action = 2; // ACTION_MOVE
    ev.time = ev.down_time = now_ms();
    unity_inject(s_motion);
    ev = saved;
  }
}

// --- the soft keyboard -------------------------------------------------------------------

int test_script_keyboard(char *out, size_t n);

typedef struct {
  char initial[512], placeholder[128];
  int type, secure, limit;
} KbdReq;

// two frames after the text: the keyboard closes
static void keyboard_close(void *arg) {
  (void)arg;
  void (*closed)(void *, void *) = unity_native("nativeSoftInputClosed");
  void (*visible)(void *, void *, uint8_t) = unity_native("nativeSetKeyboardIsVisible");
  if (closed)
    closed(jni_env(), jni_unity_player());
  if (visible)
    visible(jni_env(), jni_unity_player(), 0);
}

static void keyboard_close_later(void *arg) {
  (void)arg;
  java_android_later(keyboard_close, NULL);
}

// on the frame after the engine asked: answered inside showSoftInput, the
// engine sets the keyboard visible again after and the field stays in
// editing until the next touch (Android answers from its UI thread, later)
static void keyboard_show(void *arg) {
  KbdReq *rq = arg;
  SwkbdConfig kbd;
  char out[512] = "";
  Result rc = 0;
  if (test_script_keyboard(out, sizeof out))
    goto answered;
  if (R_FAILED(swkbdCreate(&kbd, 0))) {
    free(rq);
    return;
  }
  swkbdConfigMakePresetDefault(&kbd);
  if (rq->initial[0])
    swkbdConfigSetInitialText(&kbd, rq->initial);
  if (rq->placeholder[0])
    swkbdConfigSetGuideText(&kbd, rq->placeholder);
  if (rq->secure)
    swkbdConfigMakePresetPassword(&kbd);
  if (rq->limit > 0 && rq->limit < (int)sizeof out)
    swkbdConfigSetStringLenMax(&kbd, (u32)rq->limit);
  rc = swkbdShow(&kbd, out, sizeof out);
  swkbdClose(&kbd);
answered:
  free(rq);
  // Unity 6 registers these on UnityPlayerForActivityOrService
  void (*set_str)(void *, void *, void *) = unity_native("nativeSetInputString");
  void (*closed)(void *, void *) = unity_native("nativeSoftInputClosed");
  void (*canceled)(void *, void *) = unity_native("nativeSoftInputCanceled");
  void (*visible)(void *, void *, uint8_t) = unity_native("nativeSetKeyboardIsVisible");
  if (!set_str || !closed)
    log_printf("[input] the engine has no nativeSetInputString / nativeSoftInputClosed: the text is lost");
  // as Unity 6's own keyboard dialog reports it: shown and the text now;
  // closed and hidden on a later frame, so the game's input field reads the
  // text while the keyboard is still up (NGUI copies it then)
  if (visible)
    visible(jni_env(), jni_unity_player(), 1);
  if (R_SUCCEEDED(rc)) {
    if (set_str)
      set_str(jni_env(), jni_unity_player(), jni_str(out));
  } else if (canceled) {
    canceled(jni_env(), jni_unity_player());
  }
  java_android_later(keyboard_close_later, NULL);
}

void unity_soft_keyboard(const char *initial, int type, int multiline, int secure,
                         const char *placeholder, int limit) {
  (void)multiline;
  log_printf("[input] keyboard requested (type %d, limit %d)", type, limit);
  KbdReq *rq = calloc(1, sizeof *rq);
  if (!rq)
    return;
  snprintf(rq->initial, sizeof rq->initial, "%s", initial ? initial : "");
  snprintf(rq->placeholder, sizeof rq->placeholder, "%s", placeholder ? placeholder : "");
  rq->type = type;
  rq->secure = secure;
  rq->limit = limit;
  java_android_later(keyboard_show, rq);
}

void input_init(void) {
  jni_set_super("android/view/MotionEvent", "android/view/InputEvent");
  jni_set_super("android/view/KeyEvent", "android/view/InputEvent");
  s_motion = jni_new("android/view/MotionEvent");
  s_key = jni_new("android/view/KeyEvent");
  s_device = jni_new("android/view/InputDevice");
  static DevInfo touch = { .id = TOUCH_DEVICE, .sources = SRC_TOUCH | SRC_KEYBOARD, .name = "Switch touchscreen",
                           .descriptor = "nx-touch" };
  s_device->native = &touch;
  pads_init();

  const char *M = "android/view/MotionEvent";
  jni_method(M, "getAction", NULL, me_getAction);
  jni_method(M, "getActionMasked", NULL, me_getActionMasked);
  jni_method(M, "getActionIndex", NULL, me_getActionIndex);
  jni_method(M, "getPointerCount", NULL, me_getPointerCount);
  jni_method(M, "getPointerId", NULL, me_getPointerId);
  jni_method(M, "findPointerIndex", NULL, me_findPointerIndex);
  jni_method(M, "getX", NULL, me_getX);
  jni_method(M, "getY", NULL, me_getY);
  jni_method(M, "getRawX", NULL, me_getX);
  jni_method(M, "getRawY", NULL, me_getY);
  jni_method(M, "getAxisValue", NULL, me_getAxisValue);
  jni_method(M, "getEventTime", NULL, me_getEventTime);
  jni_method(M, "getEventTimeNanos", NULL, me_getEventTimeNanos);
  jni_method(M, "getDownTime", NULL, me_getDownTime);
  jni_method(M, "getSource", NULL, me_getSource);
  jni_method(M, "getDeviceId", NULL, me_getDeviceId);
  jni_method(M, "getToolType", NULL, me_getToolType);
  jni_method(M, "getPressure", NULL, me_getPressure);
  jni_method(M, "getSize", NULL, me_getSize);
  jni_method(M, "getTouchMajor", NULL, me_getTouchMajor);
  jni_method(M, "getTouchMinor", NULL, me_getTouchMajor);
  jni_method(M, "getToolMajor", NULL, me_getTouchMajor);
  jni_method(M, "getToolMinor", NULL, me_getTouchMajor);
  jni_method(M, "getOrientation", NULL, me_zero_f);
  jni_method(M, "getHistorySize", NULL, me_zero_i);
  jni_method(M, "getHistoricalX", NULL, me_getX);
  jni_method(M, "getHistoricalY", NULL, me_getY);
  jni_method(M, "getHistoricalEventTime", NULL, me_getEventTime);
  jni_method(M, "getMetaState", NULL, me_zero_i);
  jni_method(M, "getButtonState", NULL, me_zero_i);
  jni_method(M, "getFlags", NULL, me_zero_i);
  jni_method(M, "getEdgeFlags", NULL, me_zero_i);
  jni_method(M, "getDisplayId", NULL, me_zero_i);
  jni_method(M, "getXPrecision", NULL, me_zero_f);
  jni_method(M, "getYPrecision", NULL, me_zero_f);
  jni_method(M, "isFromSource", NULL, me_isFromSource);
  jni_method(M, "getDevice", NULL, me_getDevice);
  jni_method(M, "getClassification", NULL, me_zero_i);
  jni_method(M, "obtain", "(Landroid/view/MotionEvent;)Landroid/view/MotionEvent;", me_obtain);
  jni_method(M, "recycle", NULL, me_recycle);

  const char *K = "android/view/KeyEvent";
  jni_method(K, "getAction", NULL, ke_getAction);
  jni_method(K, "getKeyCode", NULL, ke_getKeyCode);
  jni_method(K, "getRepeatCount", NULL, ke_getRepeatCount);
  jni_method(K, "getEventTime", NULL, ke_getEventTime);
  jni_method(K, "getDownTime", NULL, ke_getDownTime);
  jni_method(K, "getSource", NULL, ke_getSource);
  jni_method(K, "getDeviceId", NULL, ke_getDeviceId);
  jni_method(K, "getMetaState", NULL, me_zero_i);
  jni_method(K, "getFlags", NULL, me_zero_i);
  jni_method(K, "getScanCode", NULL, me_zero_i);
  jni_method(K, "getUnicodeChar", NULL, ke_getUnicodeChar);
  jni_method(K, "getCharacters", NULL, ke_getCharacters);
  jni_method(K, "isFromSource", NULL, ke_isFromSource);
  jni_method(K, "getDevice", NULL, ke_getDevice);
  jni_method(K, "isLongPress", NULL, me_zero_i);
  jni_method(K, "getDisplayLabel", NULL, me_zero_i);

  const char *IE = "android/view/InputEvent";
  jni_method(IE, "getDevice", NULL, ie_getDevice);
  jni_method(IE, "getDeviceId", NULL, ie_getDeviceId);
  jni_method(IE, "getSource", NULL, ie_getSource);
  jni_method(IE, "isFromSource", NULL, ie_isFromSource);
  jni_method(IE, "getEventTime", NULL, ie_getEventTime);

  const char *D = "android/view/InputDevice";
  jni_method(D, "getDevice", NULL, dev_get);
  jni_method(D, "getDeviceIds", NULL, dev_getIds);
  jni_method(D, "getName", NULL, dev_getName);
  jni_method(D, "getSources", NULL, dev_getSources);
  jni_method(D, "getId", NULL, dev_getId);
  jni_method(D, "isVirtual", NULL, dev_isVirtual);
  jni_method(D, "isExternal", NULL, dev_isExternal);
  jni_method(D, "getMotionRange", NULL, dev_getMotionRange);
  jni_method(D, "getMotionRanges", NULL, dev_getMotionRanges);
  jni_method(D, "getDescriptor", NULL, dev_getDescriptor);
  jni_method(D, "getKeyboardType", NULL, dev_getKeyboardType);
  jni_method(D, "getControllerNumber", NULL, dev_getControllerNumber);
  jni_method(D, "getVendorId", NULL, dev_zero);
  jni_method(D, "getProductId", NULL, dev_zero);
  jni_method(D, "hasKeys", NULL, dev_hasKeys);
  const char *R = "android/view/InputDevice$MotionRange";
  jni_method(R, "getAxis", NULL, mr_getAxis);
  jni_method(R, "getSource", NULL, mr_getSource);
  jni_method(R, "getMin", NULL, mr_getMin);
  jni_method(R, "getMax", NULL, mr_getMax);
  jni_method(R, "getRange", NULL, mr_getRange);
  jni_method(R, "getFlat", NULL, mr_getFlat);
  jni_method(R, "getFuzz", NULL, mr_getFuzz);
  jni_method(R, "getResolution", NULL, mr_getResolution);
  jni_method(R, "isFromSource", NULL, mr_isFromSource);
  jni_method("android/hardware/input/InputManager", "getInputDeviceIds", NULL, dev_getIds);
  jni_method("android/hardware/input/InputManager", "getInputDevice", NULL, dev_get);
  jni_method("android/hardware/input/InputManager", "registerInputDeviceListener", NULL, dev_registerListener);
}
