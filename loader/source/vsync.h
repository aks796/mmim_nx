/* vsync.h -- the display's vertical blank, as the frame clock (vsync.c)
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef VSYNC_H
#define VSYNC_H

#include <stdint.h>

// Start the vsync thread (once; vsync_listen starts it too).
void vsync_start(void);
// fn(time, arg) on every vsync, on the vsync thread: be quick (wake a
// waiter). time: android_monotonic_ns() at the vsync.
void vsync_listen(void (*fn)(int64_t ns, void *arg), void *arg);
// the last vsync's time, and how many there have been
int64_t vsync_last_ns(void);
uint64_t vsync_count(void);

#endif
