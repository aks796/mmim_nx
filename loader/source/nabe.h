/* nabe.h -- the modded APK's assets/nabe.tar as a read-only file tree
 * (nabe.c)
 *
 * The tar is stored uncompressed inside the APK, and a tar member's data is
 * one contiguous byte range, so each of its ~20,000 files (the whole Nabe
 * asset cache, 2.4 GB) already sits at a fixed offset of the APK. The index
 * maps "Nabe/a00/a0058f44f_34f017f7" to that offset and size; vfs.c serves
 * the files from there. Nothing is ever extracted.
 *
 * Building the index reads every tar header once (a few seconds); it is kept
 * in internal/nabe.idx, keyed by the APK's size and the tar's position.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef NABE_H
#define NABE_H

#include <stdint.h>

typedef struct {
  const char *path;   // relative to the files dir: "Nabe/b/b6639fd23_b288565d"
  uint64_t off;       // data offset in the APK
  uint64_t size;
} NabeFile;

// Load internal/nabe.idx or build it from the APK (apkfile must be open).
// progress(permille) during a build. Returns the number of files, or -1.
int nabe_init(void (*progress)(int permille, void *arg), void *arg);

int nabe_count(void);
// rel: path relative to the files dir, no leading slash
const NabeFile *nabe_lookup(const char *rel);
// 1 when rel is a directory of the tree ("Nabe", "Nabe/a00", ...)
int nabe_is_dir(const char *rel);

// Children of a directory: calls fn(name, is_dir, arg) for each; returns the
// count, or -1 when rel is not a directory of the tree.
int nabe_list(const char *rel, void (*fn)(const char *name, int is_dir, void *arg), void *arg);

#endif
