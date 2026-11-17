/* config.h -- global configuration and config file handling.
 *
 * Mario vs. Luigi (MvLO forks: Android arm64, Unity 2022 IL2CPP) on the Switch.
 * Bring-up runs HDErick's MvL Accurate 3.2 APK; the MMIM build comes next. File paths are resolved at runtime from the .nro location (paths.h).
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef __CONFIG_H__
#define __CONFIG_H__

#define CONFIG_NAME "config.txt"

// The game's Android identity, as its own code and the server expect it.
#ifdef MVL_GAME_MMIM
// MiiBumm's Mega Item Mod, built from mmim_project (MvlBuild.Android)
#define GAME_PACKAGE      "com.miibumm.mmim"
#define GAME_VERSION_NAME "MMIM_1.0.0"
#define GAME_VERSION_CODE 1
#define GAME_TITLE        "MiiBumm's Mega Item Mod"
#define PORT_FOLDER       "/switch/mmim_nx"
#else
// HDErick's MvL Accurate 3.2 APK, the loader's test game
#define GAME_PACKAGE      "com.Wiiucoop.MarioVsLuigiAccurate"
#define GAME_VERSION_NAME "3.2-Accuracy"
#define GAME_VERSION_CODE 1
#define GAME_TITLE        "Mario vs. Luigi"
#define PORT_FOLDER       "/switch/mvl_nx"
#endif

// android.os.Build.VERSION.SDK_INT reported through the fake JNI (Unity 2022.2
// targets 33).
#define ANDROID_SDK_INT 33

// actual render/surface size (picked at runtime from docked state)
extern int screen_width;
extern int screen_height;

typedef struct {
  int screen_width;     // -1 = auto (1280x720 handheld, 1920x1080 docked)
  int screen_height;
  int vsync;            // 1 = 60 fps cap through the swap interval
  int fps;              // the frame rate the game is held to (Application.targetFrameRate)
  float look_speed;     // unused (from pg3d_nx)
  int invert_y;         // right stick up looks down
  int touch;            // 1 = the touchscreen works too
  int controller_applet;// 1 = Split Screen shows the console's controller applet (off: emulator tests)
  int keyboard;         // 1 = text fields open the Switch keyboard
  int rumble;           // 1 = the controller rumbles (gamepad_rumble)
  int log_all;          // every Android log line into debug.log
  int log_jni;          // every JNI method lookup and call into debug.log
  int log_fs;           // every file open/stat into debug.log
  int log_net;          // every connection the game makes
  int log_diag;         // the game's input and screens, through C# hooks
  int il2cpp_dump;      // write internal/il2cpp_dump.txt once Unity is up
  int profile;          // [prof] lines: where the main and render threads spend their time (prof.c)
  int hitch_log;        // 1 = frames over 50 ms log where the threads were (prof.c)
  int apk_cache;        // 1 = the APK's game data is kept in RAM (apkcache.c)
  int log_ubo;          // debug: Unity's uniform buffer calls (egl_shim.c)
  char language[16];    // Java Locale: "en", "ja", "fr", ...
  char country[16];     // Java Locale country: "US", "JP", ...
  char capture_at[96];  // debug: seconds at which to save internal/cap_<s>.bmp
  char capture_fbos[64];// debug: also these GL framebuffers (cap_<s>_fb<n>.bmp)
  char dump_threads_at[96]; // debug: seconds at which to log where every thread is
  char test_script[160];// debug: input script file, "" = none
  char trace[512];      // debug: C# methods logged on entry (trace.c)
  char trace_nre[32];   // debug: "auto" or the libil2cpp offset of Exception::RaiseNullReferenceException() (trace.c)
  char brk_unity[160];  // debug: hex offsets in libunity.so to trap at (emulator bisecting)
} Config;

extern Config config;

int read_config(const char *file);
int write_config(const char *file);

#endif
