#!/usr/bin/env python3
"""Scrub the build machine's home folder out of a WebGL build, in place.

The IL2CPP metadata in the WebGL data file (Build/*.data.unityweb, gzip)
carries [CallerFilePath] strings with the developer's home path
(/Users/<name>/), as the APKs do (scrub_apk_paths.py). Each gzip file that
contains it is unpacked, gets the same-length replacement /Users/xxxx.../,
and is packed again WITH the header Unity writes: FNAME "build.data" and
FCOMMENT "UnityWeb Compressed Content (gzip)". The loader's decompression
fallback (used when the server sends the file without Content-Encoding,
as itch.io does) checks for that comment (hasUnityMarker); a plain gzip
header made the page fail to load.

usage: tools/scrub_web_data.py <WebGL output folder> [...]
exit 0 = no file in the build has the home path left
"""
import gzip
import os
import struct
import sys
import time
import zlib

COMMENT = b"UnityWeb Compressed Content (gzip)"


def unity_gzip(data: bytes, name: bytes) -> bytes:
    deflate = zlib.compressobj(9, zlib.DEFLATED, -15)
    body = deflate.compress(data) + deflate.flush()
    header = b"\x1f\x8b\x08" + bytes([0x08 | 0x10]) + struct.pack("<I", int(time.time())) + b"\x00\x03"
    header += name + b"\x00" + COMMENT + b"\x00"
    return header + body + struct.pack("<II", zlib.crc32(data) & 0xFFFFFFFF, len(data) & 0xFFFFFFFF)


def has_marker(blob: bytes) -> bool:
    if len(blob) < 10 or blob[:2] != b"\x1f\x8b":
        return False
    flags, at = blob[3], 10
    if flags & 0x04:
        at += 2 + struct.unpack("<H", blob[at:at + 2])[0]
    if flags & 0x08:
        at = blob.index(b"\x00", at) + 1
    if flags & 0x10:
        end = blob.index(b"\x00", at)
        return blob[at:end] == COMMENT
    return False


def scrub(folder: str) -> int:
    home = os.path.expanduser("~").rstrip("/").encode() + b"/"
    parts = home.split(b"/")
    fake = b"/".join(parts[:-2] + [b"x" * len(parts[-2]), b""])
    assert len(fake) == len(home) and fake != home
    left = 0
    for root, _, files in os.walk(folder):
        for f in files:
            path = os.path.join(root, f)
            raw = open(path, "rb").read()
            packed = raw[:2] == b"\x1f\x8b"
            data = gzip.decompress(raw) if packed else raw
            n = data.count(home)
            if not n:
                continue
            if not packed:
                open(path, "wb").write(data.replace(home, fake))
                print(f"{path}: {n} home path(s) replaced")
                continue
            blob = unity_gzip(data.replace(home, fake), b"build.data")
            assert has_marker(blob) and gzip.decompress(blob).count(home) == 0
            open(path, "wb").write(blob)
            print(f"{path}: {n} home path(s) replaced, packed again with Unity's header")
    for root, _, files in os.walk(folder):
        for f in files:
            path = os.path.join(root, f)
            raw = open(path, "rb").read()
            data = gzip.decompress(raw) if raw[:2] == b"\x1f\x8b" else raw
            if home in data:
                print(f"{path}: still has the home path")
                left += 1
            if raw[:2] == b"\x1f\x8b" and not has_marker(raw):
                print(f"{path}: gzip without Unity's marker (the loader's fallback will not take it)")
    print(f"{folder}: {'clean' if not left else str(left) + ' file(s) left'}")
    return 1 if left else 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(max(scrub(a) for a in sys.argv[1:]))
