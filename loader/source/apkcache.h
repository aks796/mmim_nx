/* apkcache.h -- the APK's game data kept in RAM (apkcache.c)
 *
 * Unity reads its data (assets/bin/Data/data.unity3d, the .resource files)
 * straight out of the APK: it fopen()s the APK itself and freads at
 * offsets (log_fs shows it). On the Switch
 * each of those reads is a filesystem request to the SD card, and a
 * mid-match first load (a projectile, a sound) waited on them on the main
 * thread: 50-70% of every long frame in the MMIM HW log was NativeFile::read
 * waiting on the SD. The stored data entries are read into memory once, by a
 * low-priority thread at start-up; from then on a read of the APK that falls
 * inside one is a memcpy (fread_fake, and apkfile_read for the loader's
 * own reads). Until a range is filled, reads go to the SD as before.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef APKCACHE_H
#define APKCACHE_H

#include <stddef.h>
#include <stdint.h>

// start filling the cache from the open APK (apkfile.c); config apk_cache 0
// turns it off
void apkcache_start(void);

// fopen()/fclose() of a real file: is it the APK? (path through fix_path)
void apkcache_fopened(void *file, const char *path);
void apkcache_fclosed(void *file);
int apkcache_is_file(void *file);

// a read of len bytes at off of the APK: 1 = copied from memory into buf
int apkcache_read(uint64_t off, void *buf, size_t len);

// for the [perf] line: reads of the APK served from memory and from the SD
// since the last call
void apkcache_report(char *out, size_t size);

#endif
