/* unity_patch.c -- changes to libunity.so (Unity 2022.2), made as it loads
 *
 * Nothing yet: the engine is first run as it is, and only what the logs
 * show cannot be answered from outside (JNI, the NDK, libc) is patched here.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "log.h"
#include "so_util.h"

void unity_patch_loaded(so_module *m) {
  (void)m;
}
