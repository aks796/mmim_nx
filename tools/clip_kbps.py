#!/usr/bin/env python3
"""Bitrate of every AudioClip in a Unity build, written as JSON
{key: [kbps, load type, compression format, channels, frequency, length s, bytes]},
key = the clip's Resources path ("sound/music/castle"), else "name:<clip name>".

The build is a Data folder of .assets files (PC MMIM), an APK, or a WebGL
.data(.unityweb) file. tools/match_pc_import.py --measured takes an APK's
file to calibrate each clip's quality against MMIM's.

usage: tools/venv/bin/python tools/clip_kbps.py <Data dir | .apk | .unityweb> <out.json>
"""
import glob
import gzip
import json
import os
import shutil
import sys
import tempfile
import zipfile

import UnityPy


def resource_paths(env):
    """(serialized file name, path id) -> the object's Resources path
    (lower case, no extension: "sound/character/mario/jump"), from the
    build's ResourceManager"""
    out = {}
    for o in env.objects:
        if o.type.name != "ResourceManager":
            continue
        ext = [os.path.basename(e.path).lower() for e in o.assets_file.externals]
        for path, ptr in o.read_typetree()["m_Container"]:
            fid = ptr["m_FileID"]
            name = o.assets_file.name.lower() if fid == 0 else ext[fid - 1]
            out[(name, ptr["m_PathID"])] = path
    return out


def scan(env, res):
    """keys: a clip's Resources path, else "name:<clip name>" (clips that
    only scenes use; names repeat, every character has its own jump)"""
    paths = resource_paths(env)
    for obj in env.objects:
        if obj.type.name != "AudioClip":
            continue
        a = obj.read()
        size = a.m_Resource.m_Size if a.m_Resource else 0
        kbps = size * 8 / a.m_Length / 1000 if a.m_Length > 0 else 0
        key = paths.get((os.path.basename(obj.assets_file.name).lower(), obj.path_id)) or "name:" + a.m_Name
        res.setdefault(key, [round(kbps, 1), int(a.m_LoadType), int(a.m_CompressionFormat),
                             a.m_Channels, a.m_Frequency, round(a.m_Length, 3), size])


def main():
    src, out = sys.argv[1:3]
    res = {}
    if os.path.isdir(src):
        scan(UnityPy.load(src), res)
    elif src.endswith(".apk"):
        tmp = tempfile.mkdtemp(dir=os.path.dirname(os.path.abspath(out)))
        try:
            with zipfile.ZipFile(src) as z:
                z.extractall(tmp, [n for n in z.namelist() if n.startswith("assets/bin/Data/")])
            scan(UnityPy.load(os.path.join(tmp, "assets", "bin", "Data")), res)
        finally:
            shutil.rmtree(tmp)
    else:
        data = open(src, "rb").read()
        if data[:2] == b"\x1f\x8b":
            data = gzip.decompress(data)
        scan(UnityPy.load(data), res)
    json.dump(res, open(out, "w"), indent=0)
    print(f"{len(res)} clips, {sum(v[6] for v in res.values()) / 1e6:.1f} MB of audio")


if __name__ == "__main__":
    main()
