/* default_window.c -- libnx's default window, with its display handed out
 *
 * libnx opens the "Default" display for the default window (NWindow) and
 * keeps it to itself; vi allows one OpenDisplay("Default") per process, so a
 * second open for the display's vsync event fails (0x1272, also on
 * hardware). This file takes the place of libnx's own default_window.o
 * (the same code; it is linked first, so libnx's is never pulled in) and
 * adds nwindowGetDefaultDisplay() for vsync.c.
 *
 * Copyright 2017-2024 libnx authors (ISC). The accessor as in libnx32.
 */

#include <switch.h>

static ViDisplay g_viDisplay;
static ViLayer g_viLayer;
static NWindow g_defaultWin;

NWindow *nwindowGetDefault(void) {
  return &g_defaultWin;
}

ViDisplay *nwindowGetDefaultDisplay(void) {
  return g_viDisplay.initialized ? &g_viDisplay : NULL;
}

void __nx_win_init(void) {
  Result rc = viInitialize(ViServiceType_Default);
  if (R_SUCCEEDED(rc)) {
    rc = viOpenDefaultDisplay(&g_viDisplay);
    if (R_SUCCEEDED(rc)) {
      rc = viCreateLayer(&g_viDisplay, &g_viLayer);
      if (R_SUCCEEDED(rc)) {
        rc = viSetLayerScalingMode(&g_viLayer, ViScalingMode_FitToLayer);
        if (R_SUCCEEDED(rc)) {
          rc = nwindowCreateFromLayer(&g_defaultWin, &g_viLayer);
          if (R_SUCCEEDED(rc))
            nwindowSetDimensions(&g_defaultWin, 1280, 720);
        }
        if (R_FAILED(rc))
          viCloseLayer(&g_viLayer);
      }
      if (R_FAILED(rc))
        viCloseDisplay(&g_viDisplay);
    }
    if (R_FAILED(rc))
      viExit();
  }
  if (R_FAILED(rc))
    diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_BadGfxInit));
}

void __nx_win_exit(void) {
  nwindowClose(&g_defaultWin);
  viCloseLayer(&g_viLayer);
  viCloseDisplay(&g_viDisplay);
  viExit();
}
