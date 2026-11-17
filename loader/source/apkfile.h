/* apkfile.h -- random access to the player's APK (apkfile.c)
 *
 * The APK is opened once with libnx's FsFile API, which reads at an offset
 * without a shared file position, so any thread can read any part of it at
 * any time and it costs one filesystem handle no matter how many virtual
 * files are open on it.
 *
 * Its zip central directory is parsed once into a hash table, which gives
 * every entry's data offset: a stored entry (data.unity3d, nabe.tar, the
 * _nabe_ chunks) is then just a byte range of the file.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef APKFILE_H
#define APKFILE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  const char *name;     // entry name ("assets/bin/Data/data.unity3d")
  uint64_t data_off;    // where the (compressed) data starts in the APK
  uint64_t csize;       // compressed size
  uint64_t usize;       // uncompressed size
  uint16_t method;      // 0 stored, 8 deflated
  uint32_t crc;
  uint32_t mtime_dos;   // DOS date << 16 | DOS time
} ApkEntry;

// Open the APK and read its directory. 0 on success, else -1 with
// apkfile_error() telling why.
int apkfile_open(const char *path);
void apkfile_close(void);
const char *apkfile_error(void);
const char *apkfile_path(void);
uint64_t apkfile_size(void);

// pread on the APK: bytes read, or -1. From RAM where apkcache.c holds the
// range; apkfile_read_sd always reads the SD.
long apkfile_read(uint64_t off, void *buf, size_t len);
long apkfile_read_sd(uint64_t off, void *buf, size_t len);

int apkfile_count(void);
const ApkEntry *apkfile_entry_at(int i);
const ApkEntry *apkfile_find(const char *name);

// An entry's whole uncompressed content in a malloc'd buffer (inflating a
// deflated one); NULL on failure. *size_out gets its length.
void *apkfile_load(const ApkEntry *e, size_t *size_out);

// Copy an entry to a file on the SD (inflating as needed), through a
// temporary name; progress(bytes_done) is called now and then if given.
int apkfile_extract(const ApkEntry *e, const char *dest, void (*progress)(uint64_t done, void *arg),
                    void *arg);

#endif
