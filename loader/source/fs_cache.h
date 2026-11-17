/* fs_cache.h -- stat()/access() answers kept in memory (see fs_cache.c)
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#pragma once

#include <sys/stat.h>

// stat(2) of a device path (already through fix_path), from memory when
// nothing has changed on the SD since; newlib errno on failure
int fs_cache_stat(const char *path, struct stat *st);

// something on the SD may have changed (created, written, renamed, deleted):
// every remembered answer is dropped
void fs_cache_changed(void);

// a file descriptor opened for writing (writes to it change what stat says)
void fs_cache_note_writer(int fd);
// a write to fd: forgets if fd is one of those; a close also forgets fd
void fs_cache_wrote(int fd);
void fs_cache_closed(int fd);

// mkdir(2) of the port's own directories: skipped when it is already there
void fs_mkdir(const char *path);

// per-[perf]-period numbers: checks made, of them answered from memory, and
// the path checked most often (static buffer)
void fs_cache_stats(unsigned *checks, unsigned *hits, const char **top, unsigned *top_n);
