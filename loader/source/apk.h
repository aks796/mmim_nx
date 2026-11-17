/* apk.h -- set the game up from the player's own APK (apk.c)
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef __APK_H__
#define __APK_H__

typedef struct {
  char package[128];  // from the APK's manifest
  int version_code;
  int extracted;      // the libraries were extracted this launch
} GameInfo;

// Finds the APK next to the .nro, opens it (apkfile.c), extracts the native
// libraries when they are missing or the APK changed. 0 = ready, -1 = not
// (the player has been told why).
int apk_setup(GameInfo *info);

#endif
