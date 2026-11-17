#!/usr/bin/env python3
"""Scrub the build machine's home folder out of a built APK, in place.

IL2CPP's global-metadata.dat keeps [CallerFilePath] strings from the URP
packages (GenerateHLSL sourcePath = .../mmim_project/Library/PackageCache/...),
so every APK built here carries the developer's home path (/Users/<name>/).
Each entry that contains it gets the same-length replacement /Users/xxxx.../
(byte offsets and string lengths unchanged; the paths are editor-only data)
and is put back with its own compression method (Info-ZIP's zip replaces
only those entries; every other entry is copied as it is).

usage: tools/scrub_apk_paths.py <apk> [<apk>...]
exit 0 = the APK has no home path left
"""
import os
import subprocess
import sys
import tempfile
import zipfile


def scrub(apk: str) -> int:
    home = os.path.expanduser("~").rstrip("/").encode() + b"/"
    parts = home.split(b"/")  # b'', b'Users', b'<name>', b''
    fake = b"/".join(parts[:-2] + [b"x" * len(parts[-2]), b""])
    assert len(fake) == len(home) and fake != home
    with zipfile.ZipFile(apk) as z:
        hits = []
        for info in z.infolist():
            data = z.read(info)
            n = data.count(home)
            if n:
                hits.append((info, data.replace(home, fake), n))
    if not hits:
        print(f"{apk}: clean")
        return 0
    with tempfile.TemporaryDirectory() as tmp:
        for info, data, n in hits:
            out = os.path.join(tmp, info.filename)
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with open(out, "wb") as f:
                f.write(data)
            level = "-0" if info.compress_type == zipfile.ZIP_STORED else "-9"
            subprocess.run(["zip", "-q", "-X", level, os.path.abspath(apk), info.filename],
                           cwd=tmp, check=True)
            print(f"{apk}: {info.filename}: {n} home path(s) replaced")
    # check: nothing left, every entry still reads, methods unchanged
    with zipfile.ZipFile(apk) as z:
        bad = z.testzip()
        if bad:
            print(f"{apk}: CRC error in {bad}")
            return 1
        before = {i.filename: i.compress_type for i, _, _ in hits}
        for info in z.infolist():
            if info.filename in before and info.compress_type != before[info.filename]:
                print(f"{apk}: {info.filename} changed compression")
                return 1
            if home in z.read(info):
                print(f"{apk}: {info.filename} still has the home path")
                return 1
        signed = any(n.startswith("META-INF/") and n.endswith(".SF") for n in z.namelist())
    if signed:
        # zip dropped the v2 signature: fine for the Switch loader, refused by a phone
        print(f"{apk}: signature no longer valid -- a phone APK needs tools/sign_apk.sh {apk}")
    print(f"{apk}: clean")
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(max(scrub(a) for a in sys.argv[1:]))
