/* gcstop.h -- IL2CPP's garbage collector stopping the world, without
 * signals (gcstop.c)
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef GCSTOP_H
#define GCSTOP_H

#include <stdint.h>
#include "so_util.h"

// After libil2cpp.so is mapped.
void gcstop_init(const so_module *il2cpp);
// pthread_kill's GC part: *handled = 1 when sig was the collector's.
int gcstop_kill(void *thread, int sig, int *handled);
void gcstop_stats(unsigned *stops, unsigned *unknown);

#endif
