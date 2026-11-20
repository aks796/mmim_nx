#!/usr/bin/env python3
"""Make the project's texture import settings match the official MMIM build.

The project was rebuilt from MMIM's PC build, and two import settings came
back different from what MMIM ships (MiiBumm, 2026-11-18: "every sprite seems
to be weirdly crusty", "every item reserve sprite is stretched or too big"):

  * compression: 234 textures MMIM ships uncompressed (RGBA32/RGB24/Alpha8,
    point-filtered pixel art) were imported "Compressed HQ", so Android
    (ASTC/ETC2) and the browser (DXT) drew them with lossy block artifacts.
    Those get textureCompression 0 (Uncompressed), as MMIM.
  * sprite rects: 22 sprites (the reserve box icons and the menu header) had
    their rect cropped to their visible pixels, where MMIM's are whole cells
    (each reserve icon a 64x64 cell): the reserve box stretched every icon to
    fill it. Those get MMIM's rect, pivot and border; their mesh (outline
    and vertices, taken from MMIM's own data) is moved with the rect, so the
    shapes stay exactly as they were.
  * audio load type: 126 clips back to Decompress On Load (see audio()).

Read-only unless --apply. Prints what it would change.

usage: tools/venv/bin/python tools/match_pc_import.py <official build>/"MiiBumm's Mega Item Mod_Data" mmim_project/Assets [--apply]
           [--measured <tools/clip_kbps.py JSON of a built APK>]
"""
import glob
import os
import re
import sys

import UnityPy

# Unity TextureFormat codes that are not block-compressed
UNCOMPRESSED = {1, 2, 3, 4, 5, 7, 9, 13, 14, 62, 63}
NUM = r"(-?[\d.eE+-]+)"


def fmt(v):
    """a float as Unity's YAML writes it (no trailing .0)"""
    v = round(float(v), 6)
    return str(int(v)) if v == int(v) else repr(v)


def official(data):
    tex, spr = {}, {}
    for path in sorted(glob.glob(os.path.join(data, "*.assets"))):
        for obj in UnityPy.load(path).objects:
            if obj.type.name == "Texture2D":
                t = obj.read()
                code = t.m_TextureFormat if isinstance(t.m_TextureFormat, int) else int(t.m_TextureFormat.value)
                tex.setdefault(t.m_Name, code)
            elif obj.type.name == "Sprite":
                s = obj.read()
                if not (s.m_RD.texture and s.m_RD.texture.path_id):
                    continue
                t = s.m_RD.texture.read()
                r, b, tr = s.m_Rect, s.m_Border, s.m_RD.textureRect
                spr.setdefault((t.m_Name, s.m_Name), dict(rect=[r.x, r.y, r.width, r.height],
                                                 pivot=[s.m_Pivot.x, s.m_Pivot.y], border=[b.x, b.y, b.z, b.w],
                                                 tight=[tr.x - r.x, tr.y - r.y, tr.width, tr.height],
                                                 size=[t.m_Width, t.m_Height]))
    return tex, spr


def close(a, b, tol=0.01):
    return all(abs(x - y) <= tol for x, y in zip(a, b))


def fix_sprite_block(lines, start, end, pc, log):
    """rect, pivot, border to MMIM's; outline/physicsShape (about the rect's
    centre) and vertices (from its corner) moved by as much as the rect moved"""
    txt = "\n".join(lines[start:end])
    m = re.search(r"rect:\s*\n\s*serializedVersion: 2\s*\n\s*x: " + NUM + r"\s*\n\s*y: " + NUM +
                  r"\s*\n\s*width: " + NUM + r"\s*\n\s*height: " + NUM, txt)
    pv = re.search(r"pivot: \{x: " + NUM + ", y: " + NUM + r"\}", txt)
    bd = re.search(r"border: \{x: " + NUM + ", y: " + NUM + ", z: " + NUM + ", w: " + NUM + r"\}", txt)
    old = [float(v) for v in m.groups()]
    ours = dict(rect=old, pivot=[float(v) for v in pv.groups()], border=[float(v) for v in bd.groups()])
    if all(close(ours[k], pc[k]) for k in ("rect", "pivot", "border")):
        return False
    new = pc["rect"]
    corner = (old[0] - new[0], old[1] - new[1])                    # vertices: from the corner
    centre = (old[0] + old[2] / 2 - (new[0] + new[2] / 2),          # outline: about the centre
              old[1] + old[3] / 2 - (new[1] + new[3] / 2))
    section = None
    point = re.compile(r"^(\s*(?:- )*- )\{x: " + NUM + ", y: " + NUM + r"\}\s*$")
    for i in range(start, end):
        line = lines[i]
        key = re.match(r"^      (\w+):", line)
        if key:
            section = key.group(1)
        if re.match(r"^        x: ", line) and section == "rect":
            lines[i] = "        x: " + fmt(new[0])
        elif re.match(r"^        y: ", line) and section == "rect":
            lines[i] = "        y: " + fmt(new[1])
        elif re.match(r"^        width: ", line) and section == "rect":
            lines[i] = "        width: " + fmt(new[2])
        elif re.match(r"^        height: ", line) and section == "rect":
            lines[i] = "        height: " + fmt(new[3])
        elif line.startswith("      alignment: "):
            lines[i] = "      alignment: 9"
        elif line.startswith("      pivot: "):
            lines[i] = "      pivot: {x: %s, y: %s}" % (fmt(pc["pivot"][0]), fmt(pc["pivot"][1]))
        elif line.startswith("      border: "):
            lines[i] = "      border: {x: %s, y: %s, z: %s, w: %s}" % tuple(fmt(v) for v in pc["border"])
        else:
            p = point.match(line)
            if p and section in ("outline", "physicsShape", "vertices"):
                dx, dy = corner if section == "vertices" else centre
                lines[i] = "%s{x: %s, y: %s}" % (p.group(1), fmt(float(p.group(2)) + dx), fmt(float(p.group(3)) + dy))
    log.append(f"  sprite rect {[round(v, 3) for v in old]} -> {new}, pivot -> {pc['pivot']}, border -> {pc['border']}")
    return True


def fix_single(lines, name, pc, png, log):
    """a single-image sprite (spriteMode 1): MMIM's pivot, and its mesh
    (outline and physics shape about the rect's centre, vertices from its
    corner) moved so that the area it draws is MMIM's (the tight rect): the
    export had both wrong for 15 sprites (2026-11-20: tailslash's mesh
    covered rows 17-46 where its crescent is at 37-64, so the whoosh drew a
    sliver; ovhills' pivot was 1.555 instead of 0.5)"""
    from PIL import Image
    w, h = Image.open(png).size
    if [w, h] != pc["size"] or pc["rect"][2:] != [w, h]:
        return False  # another texture of the same name, or not the whole texture
    txt = "\n".join(lines)
    pv = re.search(r"\n  spritePivot: \{x: " + NUM + ", y: " + NUM + r"\}", txt)
    if not pv:
        return False
    pivot = [float(v) for v in pv.groups()]
    pts, section = [], None
    point = re.compile(r"^(\s*(?:- )*- )\{x: " + NUM + ", y: " + NUM + r"\}\s*$")
    start = next((i for i, l in enumerate(lines) if l.startswith("  spriteSheet:")), None)
    end = next((i for i in range(start + 1, len(lines)) if re.match(r"^  \w", lines[i])), len(lines)) if start is not None else None
    if start is not None:
        for i in range(start, end):
            key = re.match(r"^    (\w+):", lines[i])
            if key:
                section = key.group(1)
            p = point.match(lines[i])
            if p and section == "outline":
                pts.append((float(p.group(2)), float(p.group(3))))
    if pts:
        ours = [min(x for x, _ in pts) + w / 2, min(y for _, y in pts) + h / 2]
        dx, dy = pc["tight"][0] - ours[0], pc["tight"][1] - ours[1]
    else:
        dx = dy = 0.0  # a full-rect sprite: only the pivot
    piv_off = max(abs(pivot[0] - pc["pivot"][0]) * w, abs(pivot[1] - pc["pivot"][1]) * h)
    if piv_off <= 0.5 and abs(dx) <= 1 and abs(dy) <= 1:
        return False
    for i, line in enumerate(lines):
        if line.startswith("  alignment: "):
            lines[i] = "  alignment: 9"
        elif line.startswith("  spritePivot: "):
            lines[i] = "  spritePivot: {x: %s, y: %s}" % (fmt(pc["pivot"][0]), fmt(pc["pivot"][1]))
    if start is not None and (abs(dx) > 1 or abs(dy) > 1):
        for i in range(start, end):
            key = re.match(r"^    (\w+):", lines[i])
            if key:
                section = key.group(1)
            p = point.match(lines[i])
            if p and section in ("outline", "physicsShape", "vertices"):
                lines[i] = "%s{x: %s, y: %s}" % (p.group(1), fmt(float(p.group(2)) + dx), fmt(float(p.group(3)) + dy))
    log.append(f"  single sprite: pivot {pivot} -> {[round(v, 3) for v in pc['pivot']]} ({piv_off:.1f} px), "
               f"mesh moved ({dx:.1f}, {dy:.1f}) px to MMIM's area")
    return True


def main():
    data, assets = sys.argv[1], sys.argv[2]
    apply = "--apply" in sys.argv
    measured = None
    if "--measured" in sys.argv:
        import json
        measured = json.load(open(sys.argv[sys.argv.index("--measured") + 1]))
    pc_tex, pc_spr = official(data)
    textures = sprites = 0
    for meta in sorted(glob.glob(os.path.join(assets, "**", "*.png.meta"), recursive=True)):
        name = os.path.basename(meta)[:-len(".png.meta")]
        if name not in pc_tex:
            continue
        s = open(meta, encoding="utf-8").read()
        lines = s.split("\n")
        log, changed = [], False
        if pc_tex[name] in UNCOMPRESSED:
            for i, line in enumerate(lines):
                m = re.match(r"^(\s*)(textureCompression|crunchedCompression): ([1-9])\s*$", line)
                if m:
                    lines[i] = f"{m.group(1)}{m.group(2)}: 0"
                    changed = True
            if changed:
                textures += 1
                log.append(f"  uncompressed (MMIM ships it as format {pc_tex[name]})")
        if "\n  spriteMode: 1\n" in s and (name, name) in pc_spr:
            if fix_single(lines, name, pc_spr[(name, name)], meta[:-len(".meta")], log):
                sprites += 1
                changed = True
        starts = [i for i, l in enumerate(lines) if l == "    - serializedVersion: 2"]
        for k, st in enumerate(starts):
            end = starts[k + 1] if k + 1 < len(starts) else next(
                (j for j in range(st + 1, len(lines)) if re.match(r"^  \w", lines[j])), len(lines))
            nm = re.match(r"^      name: (.+)$", lines[st + 1]) if st + 1 < len(lines) else None
            if nm and (name, nm.group(1).strip()) in pc_spr:
                if fix_sprite_block(lines, st, end, pc_spr[(name, nm.group(1).strip())], log):
                    log[-1] = log[-1].replace("  sprite", "  sprite " + nm.group(1).strip() + ":")
                    sprites += 1
                    changed = True
        if changed:
            print(os.path.relpath(meta, assets))
            print("\n".join(log[:4]) + (f"\n  ... {len(log) - 4} more" if len(log) > 4 else ""))
            if apply:
                open(meta, "w", encoding="utf-8").write("\n".join(lines))
    clips = audio(data, assets, apply, measured)
    print(f"{'changed' if apply else 'would change'}: {textures} textures to uncompressed, {sprites} sprite rects, "
          f"{clips} audio clips to MMIM's load type and bitrate")


def audio(data, assets, apply, measured=None):
    """MMIM ships its clips Decompress On Load (two Streaming). The port had
    set 126 long ones Compressed In Memory (round 17, the Switch's main-thread
    decode), where AudioSource.time is only as exact as a compressed frame:
    LoopingMusic reads and sets it at the loop points, so tracks looped off
    (MiiBumm, 2026-11-18). Back to MMIM's load type; loadInBackground stays
    on, so the decode still happens off the main thread.

    Quality: every clip came back with quality 0 (the lowest), so the music
    was ~90-100 kbps where MMIM's is 77-400 kbps (user, 2026-11-18: "Match
    MMIM's quality"). Each clip gets the quality whose Vorbis bitrate is
    closest to the clip's bitrate in MMIM's build (VORBIS_KBPS: libvorbis'
    nominal 44.1 kHz stereo rates by quality; approximate, so check the built
    sizes against MMIM's afterwards and nudge QUALITY_SCALE).

    The table runs high for this music: the first v1.0.3 phone build came
    out at 0.66x MMIM's rates (castle 63 kbps vs 102). --measured <JSON of
    an APK> calibrates each clip from that build's real bitrate
    (calibrated()) into ANDROID_TABLE, the Android-only quality (phone and
    Switch APKs) that Assets/Editor/MvlAudio.cs sets as each clip's Android
    override. The default settings stay for the browser build, whose AAC
    already comes out at MMIM's rates (median 1.25x) and whose web.data must
    stay under itch.io's file size limit. A default quality that is set
    already is left alone."""
    android = {}
    if os.path.exists(ANDROID_TABLE):
        for line in open(ANDROID_TABLE, encoding="utf-8"):
            if line.strip() and not line.startswith("#"):
                path, q = line.rstrip("\n").split("\t")
                android[path] = float(q)
    android_changes = 0
    # clips by Resources path (clip names repeat: every character has its
    # own jump), as tools/clip_kbps.py keys them
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from clip_kbps import resource_paths
    env = UnityPy.load(data)
    paths = resource_paths(env)
    pc = {}
    for obj in env.objects:
        if obj.type.name == "AudioClip":
            a = obj.read()
            size = a.m_Resource.m_Size if a.m_Resource else 0
            kbps = size * 8 / a.m_Length / 1000 if a.m_Length > 0 else 0
            key = paths.get((os.path.basename(obj.assets_file.name).lower(), obj.path_id)) or "name:" + a.m_Name
            pc.setdefault(key, (int(a.m_LoadType), quality_for(kbps, max(1, a.m_Channels), a.m_Frequency), kbps))
    n = 0
    metas = glob.glob(os.path.join(assets, "**", "*.ogg.meta"), recursive=True) + \
        glob.glob(os.path.join(assets, "**", "*.wav.meta"), recursive=True)
    for meta in sorted(metas):
        name = os.path.basename(meta).rsplit(".", 2)[0]
        rel = os.path.relpath(meta[:-len(".meta")], os.path.join(assets, "Resources"))
        key = "name:" + name if rel.startswith("..") else os.path.splitext(rel)[0].replace(os.sep, "/").lower()
        if key not in pc:
            continue
        load, quality, kbps = pc[key]
        s = open(meta, encoding="utf-8").read()
        # the import settings (defaultSettings), not the generated output cache
        head, sep, tail = s.partition("  output:")
        now = re.search(r"\n  defaultSettings:\n(?:    .*\n)*?    quality: ([\d.]+)", head)
        q_now = float(now.group(1)) if now else 0.0
        if q_now > 0:
            quality = q_now  # set already: left alone
        asset = os.path.relpath(meta[:-len(".meta")], os.path.dirname(os.path.abspath(assets)))
        if measured is not None:
            # the Android quality the measured build had: the table's, or the default
            q_android = android.get(asset, q_now or quality)
            got = measured.get(key)
            if got and got[0] > 0 and kbps > 0:
                q_new = calibrated(q_android, got[0], kbps)
                if abs(q_new - android.get(asset, -1)) > 0.001:
                    android_changes += 1
                android[asset] = q_new
        new = re.sub(r"(\n  defaultSettings:\n    loadType: )(\d)", lambda m: m.group(1) + str(load), head)
        new = re.sub(r"(\n  defaultSettings:\n(?:    .*\n)*?    quality: )([\d.]+)",
                     lambda m: m.group(1) + fmt(quality), new)
        if new != head:
            n += 1
            print(os.path.relpath(meta, assets) + f"\n  load type {load}, quality {fmt(quality)} (MMIM's {kbps:.0f} kbps)")
            if apply:
                open(meta, "w", encoding="utf-8").write(new + sep + tail)
    if measured is not None:
        print(f"Android quality: {android_changes} clips calibrated" + ("" if apply else " (not written)"))
        if apply:
            os.makedirs(os.path.dirname(ANDROID_TABLE), exist_ok=True)
            with open(ANDROID_TABLE, "w", encoding="utf-8") as f:
                f.write("# asset path<TAB>Vorbis quality for Android (phone + Switch APKs), from\n"
                        "# tools/match_pc_import.py --measured; applied by MvlAudio.ApplyAndroid\n")
                for path in sorted(android):
                    f.write(f"{path}\t{fmt(android[path])}\n")
    return n


# the Android-only qualities (relative to this file: tools/audio/)
ANDROID_TABLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "audio", "android_quality.tsv")

# libvorbis' nominal bitrate (kbps, 44.1 kHz stereo) at quality 0.0 .. 1.0
# (Unity's 0-1 quality as libvorbis -q 0..10)
VORBIS_KBPS = [64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 500]
QUALITY_SCALE = 1.0  # after a build: >1 if the clips came out smaller than MMIM's


def quality_for(kbps, channels, frequency):
    """the 0-1 quality whose nominal bitrate is MMIM's for this clip"""
    if kbps <= 0:
        return 0.7
    # the table is for 44.1 kHz stereo: scale MMIM's rate to that
    return quality_of_nominal(kbps * QUALITY_SCALE * (2 / channels) * (44100 / max(frequency, 8000)) ** 0.5)


def quality_of_nominal(k):
    """the 0-1 quality whose nominal (table) bitrate is k"""
    if k <= VORBIS_KBPS[0]:
        return 0.01
    for i in range(1, len(VORBIS_KBPS)):
        if k <= VORBIS_KBPS[i]:
            lo, hi = VORBIS_KBPS[i - 1], VORBIS_KBPS[i]
            return round(((i - 1) + (k - lo) / (hi - lo)) / 10, 2)
    return 1.0


def nominal_of_quality(q):
    """the table's bitrate at quality q"""
    x = min(max(q, 0.0), 1.0) * 10
    i = min(int(x), len(VORBIS_KBPS) - 2)
    return VORBIS_KBPS[i] + (x - i) * (VORBIS_KBPS[i + 1] - VORBIS_KBPS[i])


def calibrated(q_now, built_kbps, mmim_kbps):
    """a clip's quality from a build of it: the clip's real bitrate is taken
    to follow the table's in proportion (how much a clip compresses depends
    on the music, not the quality), so the quality that gives MMIM's rate is
    the one whose table rate is the table rate now times MMIM's / built"""
    return quality_of_nominal(nominal_of_quality(q_now) * mmim_kbps / built_kbps)


if __name__ == "__main__":
    main()
