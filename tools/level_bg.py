#!/usr/bin/env python3
"""Draw a level's background as its scene lays it out, from the game's own
textures (for icons and art; not a game renderer).

Takes every active SpriteRenderer on the Background sorting layer or under an
object named *Background* in a level scene, with its sprite (rect, pivot,
borders from the texture's .meta), draw mode (simple / sliced / tiled),
world position and scale through its parents, colour and sorting, and draws
them in Unity's order (sorting layer, order in layer, farther z first) over
the main camera's clear colour. Layers repeat horizontally (BackgroundLoop
clones them across the view at runtime). Point sampling, as the game.

usage (tools/venv for Pillow):
  tools/venv/bin/python tools/level_bg.py <Scene.unity> --dump
  tools/venv/bin/python tools/level_bg.py <Scene.unity> out.png x0 y0 x1 y1 px_per_unit
"""
import glob
import os
import re
import sys

from PIL import Image

PROJECT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mmim_project")
LAYER_RANK = {-1013106893: 0, 0: 1, -755315459: 2, -705177277: 3}  # Background, Default, Particle, Nametag

_guid_cache = None


def guid_index():
    global _guid_cache
    if _guid_cache is None:
        _guid_cache = {}
        for meta in glob.glob(os.path.join(PROJECT, "Assets", "**", "*.png.meta"), recursive=True):
            with open(meta, errors="replace") as f:
                m = re.search(r"^guid: (\w+)", f.read(), re.M)
            if m:
                _guid_cache[m.group(1)] = meta[:-5]
    return _guid_cache


def f3(s, key):
    m = re.search(key + r": \{x: ([^,}]+), y: ([^,}]+)(?:, z: ([^,}]+))?", s)
    return tuple(float(v) for v in m.groups() if v is not None) if m else None


def sprite_info(png, file_id):
    """(rect x,y,w,h from the bottom, pivot, border l,b,r,t, ppu) of a sprite"""
    meta = open(png + ".meta", errors="replace").read()
    ppu = float(re.search(r"spritePixelsToUnits: ([\d.]+)", meta).group(1))
    im = Image.open(png)
    W, H = im.size
    mode = int(re.search(r"spriteMode: (\d+)", meta).group(1))
    if mode == 2:
        for block in re.split(r"\n    - serializedVersion: 2\n", meta):
            if re.search(r"internalID: %s\b" % file_id, block):
                r = re.search(r"rect:\s+serializedVersion: 2\s+x: ([\d.]+)\s+y: ([\d.]+)\s+width: ([\d.]+)\s+height: ([\d.]+)", block)
                rect = tuple(float(v) for v in r.groups())
                pv = f3(block, "pivot")
                al = int(re.search(r"alignment: (\d+)", block).group(1))
                bd = re.search(r"border: \{x: ([\d.]+), y: ([\d.]+), z: ([\d.]+), w: ([\d.]+)\}", block)
                return rect, pivot_of(al, pv), tuple(float(v) for v in bd.groups()), ppu
        # internalIDToNameTable style (older metas): fall through to the whole texture
    pv = f3(meta, "spritePivot") or (0.5, 0.5)
    al = int(re.search(r"alignment: (\d+)", meta).group(1))
    bd = re.search(r"spriteBorder: \{x: ([\d.]+), y: ([\d.]+), z: ([\d.]+), w: ([\d.]+)\}", meta)
    return (0.0, 0.0, float(W), float(H)), pivot_of(al, pv), tuple(float(v) for v in bd.groups()), ppu


def pivot_of(alignment, custom):
    table = {0: (0.5, 0.5), 1: (0, 1), 2: (0.5, 1), 3: (1, 1), 4: (0, 0.5), 5: (1, 0.5),
             6: (0, 0), 7: (0.5, 0), 8: (1, 0)}
    return table.get(alignment, custom or (0.5, 0.5))


def parse(scene_path):
    text = open(scene_path, encoding="utf-8", errors="replace").read()
    objs = {}
    for d in re.split(r"\n--- !u!", text):
        m = re.match(r"(\d+) &(-?\d+)", d)
        if m:
            objs[m.group(2)] = (int(m.group(1)), d)

    def go_of(d):
        m = re.search(r"m_GameObject: \{fileID: (-?\d+)\}", d)
        return m.group(1) if m else None

    tr_of_go, name_of, active = {}, {}, {}
    for oid, (t, d) in objs.items():
        if t == 1:
            name_of[oid] = (re.search(r"m_Name: (.*)", d) or [None, "?"])[1]
            active[oid] = "m_IsActive: 1" in d
        elif t in (4, 224):
            tr_of_go[go_of(d)] = oid

    def chain(tr_id):
        """transforms from this one up to the root"""
        out = []
        while tr_id and tr_id in objs and tr_id != "0":
            d = objs[tr_id][1]
            out.append((tr_id, d))
            tr_id = re.search(r"m_Father: \{fileID: (-?\d+)\}", d).group(1)
        return out

    def world(tr_id):
        px, py, pz, sx, sy = 0.0, 0.0, 0.0, 1.0, 1.0
        names, all_active = [], True
        for tid, d in reversed(chain(tr_id)):
            lp = f3(d, "m_LocalPosition") or (0, 0, 0)
            ls = f3(d, "m_LocalScale") or (1, 1, 1)
            px, py, pz = px + lp[0] * sx, py + lp[1] * sy, pz + lp[2]
            sx, sy = sx * ls[0], sy * ls[1]
            g = go_of(d)
            names.append(name_of.get(g, "?"))
            all_active = all_active and active.get(g, True)
        return (px, py, pz), (sx, sy), names, all_active

    clear = (0, 0, 0)
    for oid, (t, d) in objs.items():
        if t == 20 and "m_ClearFlags: 2" in d:
            c = re.search(r"m_BackGroundColor: \{r: ([\d.]+), g: ([\d.]+), b: ([\d.]+)", d)
            if c:
                clear = tuple(round(float(v) * 255) for v in c.groups())

    guids = guid_index()
    layers = []
    for oid, (t, d) in objs.items():
        if t != 212 or "m_Enabled: 1" not in d:
            continue
        sp = re.search(r"m_Sprite: \{fileID: (-?\d+), guid: (\w+)", d)
        if not sp or sp.group(2) not in guids:
            continue
        sl = int(re.search(r"m_SortingLayerID: (-?\d+)", d).group(1))
        order = int(re.search(r"m_SortingOrder: (-?\d+)", d).group(1))
        go = go_of(d)
        pos, scl, names, on = world(tr_of_go.get(go))
        if not on:
            continue
        in_bg = any("background" in n.lower() or n.lower().startswith("bg") for n in names)
        if LAYER_RANK.get(sl, 1) != 0 and not in_bg:
            continue
        png = guids[sp.group(2)]
        rect, pivot, border, ppu = sprite_info(png, sp.group(1))
        size = f3(d, "m_Size")
        col = re.search(r"m_Color: \{r: ([\d.]+), g: ([\d.]+), b: ([\d.]+), a: ([\d.]+)\}", d)
        layers.append(dict(
            name="/".join(names), png=png, rect=rect, pivot=pivot, border=border, ppu=ppu,
            draw=int(re.search(r"m_DrawMode: (\d+)", d).group(1)), size=size,
            pos=pos, scale=scl, color=tuple(float(v) for v in col.groups()) if col else (1, 1, 1, 1),
            flip=("m_FlipX: 1" in d, "m_FlipY: 1" in d), rank=LAYER_RANK.get(sl, 1), order=order))
    layers.sort(key=lambda L: (L["rank"], L["order"], -L["pos"][2]))
    return clear, layers


def sprite_image(L):
    im = Image.open(L["png"]).convert("RGBA")
    x, y, w, h = L["rect"]
    im = im.crop((round(x), round(im.height - y - h), round(x + w), round(im.height - y)))
    if L["flip"][0]:
        im = im.transpose(Image.FLIP_LEFT_RIGHT)
    if L["flip"][1]:
        im = im.transpose(Image.FLIP_TOP_BOTTOM)
    r, g, b, a = L["color"]
    if (r, g, b, a) != (1, 1, 1, 1):
        ch = im.split()
        im = Image.merge("RGBA", [ch[0].point(lambda v: round(v * r)), ch[1].point(lambda v: round(v * g)),
                                  ch[2].point(lambda v: round(v * b)), ch[3].point(lambda v: round(v * a))])
    return im


def layer_pixels(L):
    """the layer at its draw size, in sprite pixels (before world scale)"""
    im = sprite_image(L)
    w, h = im.size
    if L["draw"] == 0 or not L["size"]:
        return im
    tw, th = max(1, round(L["size"][0] * L["ppu"])), max(1, round(L["size"][1] * L["ppu"]))
    if L["draw"] == 1:  # sliced: corners keep, edges and centre stretch
        bl, bb, br, bt = (round(v) for v in L["border"])
        out = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
        xs = [(0, bl, 0, bl), (bl, w - br, bl, tw - br), (w - br, w, tw - br, tw)]
        ys = [(0, bt, 0, bt), (bt, h - bb, bt, th - bb), (h - bb, h, th - bb, th)]
        for sx0, sx1, dx0, dx1 in xs:
            for sy0, sy1, dy0, dy1 in ys:
                if sx1 > sx0 and sy1 > sy0 and dx1 > dx0 and dy1 > dy0:
                    out.paste(im.crop((sx0, sy0, sx1, sy1)).resize((dx1 - dx0, dy1 - dy0), Image.NEAREST), (dx0, dy0))
        return out
    # tiled, as Unity tiles a sprite with borders: corners once, the edges
    # repeated along their length, the centre repeated both ways, each from
    # its bottom-left (without borders: the whole sprite, from the bottom-left).
    # Night Light's hills (border top 163 of 185) are their snowy caps once at
    # the top and the plain bottom strip repeated under them down to the
    # ground; tiling the whole sprite drew a capless copy floating in the sky.
    bl, bb, br, bt = (round(v) for v in L["border"])
    out = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
    cols = [(0, bl, 0, bl, False), (bl, w - br, bl, tw - br, True), (w - br, w, tw - br, tw, False)]
    rows = [(0, bt, 0, bt, False), (bt, h - bb, bt, th - bb, True), (h - bb, h, th - bb, th, False)]
    for sx0, sx1, dx0, dx1, tile_x in cols:
        for sy0, sy1, dy0, dy1, tile_y in rows:
            if sx1 <= sx0 or sy1 <= sy0 or dx1 <= dx0 or dy1 <= dy0:
                continue
            part = im.crop((sx0, sy0, sx1, sy1))
            pw, ph = part.size
            region = Image.new("RGBA", (dx1 - dx0, dy1 - dy0), (0, 0, 0, 0))
            xs = range(0, dx1 - dx0, pw) if tile_x else [0]
            ys = range(dy1 - dy0 - ph, -ph, -ph) if tile_y else [0]
            for x in xs:
                for y in ys:
                    region.paste(part, (x, y))
            out.paste(region, (dx0, dy0))
    return out


def render(scene_path, x0, y0, x1, y1, ppu_out):
    clear, layers = parse(scene_path)
    W, H = round((x1 - x0) * ppu_out), round((y1 - y0) * ppu_out)
    out = Image.new("RGBA", (W, H), clear + (255,))
    for L in layers:
        pix = layer_pixels(L)
        sx, sy = L["scale"]
        ww, wh = pix.width / L["ppu"] * abs(sx), pix.height / L["ppu"] * abs(sy)
        left = L["pos"][0] - L["pivot"][0] * ww
        top = L["pos"][1] + (1 - L["pivot"][1]) * wh
        dw, dh = max(1, round(ww * ppu_out)), max(1, round(wh * ppu_out))
        img = pix.resize((dw, dh), Image.NEAREST)
        # BackgroundLoop: the Backgrounds layers repeat across the view;
        # anything else (Night Light's moon) is drawn once, where it is
        if L["name"].split("/")[0].lower().startswith("background"):
            k0 = int((x0 - left) // ww) - 1
            k1 = int((x1 - left) // ww) + 1
        else:
            k0 = k1 = 0
        base = round((left - x0) * ppu_out)
        for k in range(k0, k1 + 1):
            px = base + k * dw  # whole-pixel steps: no seams between copies
            py = round((y1 - top) * ppu_out)
            if px > W or px + dw < 0:
                continue
            out.alpha_composite(img, (px, py)) if px >= 0 and py >= 0 else _paste_clipped(out, img, px, py)
    return out


def _paste_clipped(out, img, px, py):
    cx0, cy0 = max(0, -px), max(0, -py)
    if cx0 >= img.width or cy0 >= img.height:
        return  # wholly off the view
    crop = img.crop((cx0, cy0, img.width, img.height))
    if crop.width > 0 and crop.height > 0:
        out.alpha_composite(crop, (px + cx0, py + cy0))


if __name__ == "__main__":
    scene = sys.argv[1]
    if len(sys.argv) > 2 and sys.argv[2] == "--dump":
        clear, layers = parse(scene)
        print("clear", clear)
        for L in layers:
            print(f"{L['rank']} {L['order']:>4} z{L['pos'][2]:>7.1f} {os.path.basename(L['png'])[:28]:28} draw{L['draw']} "
                  f"pos({L['pos'][0]:.2f},{L['pos'][1]:.2f}) scale({L['scale'][0]:.2f},{L['scale'][1]:.2f}) "
                  f"size{L['size']} a{L['color'][3]:.2f} {L['name'][-50:]}")
    else:
        x0, y0, x1, y1, ppu = (float(v) for v in sys.argv[3:8])
        render(scene, x0, y0, x1, y1, ppu).convert("RGB").save(sys.argv[2])
