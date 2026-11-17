#!/usr/bin/env python3
"""The NRO icon (256x256 JPEG for hbmenu): MMIM's logo over one of the
game's map backgrounds, drawn from the game's own textures as the level's
scene lays them out.

Presets:
  nightlight (default) -- CustomNightLight: night sky, the moon (with its
      8-bit MiiBumm), snowy pines; 1 icon px per sprite px (tools/level_bg.py)
  grass -- DefaultGrassLevel: the pink/yellow hills (the first icon,
      2 icon px per sprite px; render() below)

usage: tools/venv/bin/python tools/make_nro_icon.py <logo.png|webp> <out.jpg> [preview.png] [--preset name]
"""
import sys
from PIL import Image

TEX = "mmim_project/Assets/Texture2D/"
PPU = 100.0  # the sprites' pixels per unit


def sprite(path, x, y, w, h):
    """a sprite rect given Unity-style (from the bottom) as an RGBA image"""
    im = Image.open(TEX + path).convert("RGBA")
    return im.crop((x, im.height - y - h, x + w, im.height - y))


def render(cx, cy, units_w, units_h, px_per_unit):
    """the level's background over [cx +- w/2, cy +- h/2] in world units"""
    W, H = round(units_w * px_per_unit), round(units_h * px_per_unit)
    out = Image.new("RGBA", (W, H), (0, 0, 0, 255))
    x0, y1 = cx - units_w / 2, cy + units_h / 2

    def to_px(wx, wy):
        return round((wx - x0) * px_per_unit), round((y1 - wy) * px_per_unit)

    def paste_tiled(img, scale, left, top, right, bottom, alpha=1.0):
        """img (sprite pixels) tiled from the rect's left edge, scaled by
        scale world units per sprite px / PPU, clipped to the rect"""
        tw = img.width / PPU * scale * px_per_unit
        th = img.height / PPU * scale * px_per_unit
        tile = img.resize((max(1, round(tw)), max(1, round(th))), Image.NEAREST)
        if alpha < 1.0:
            a = tile.getchannel("A").point(lambda v: round(v * alpha))
            tile.putalpha(a)
        L, T = to_px(left, top)
        R, B = to_px(right, bottom)
        layer = Image.new("RGBA", out.size, (0, 0, 0, 0))
        x = L
        while x < R:
            layer.paste(tile, (x, T))
            x += tile.width
        mask = Image.new("L", out.size, 0)
        mask.paste(255, (max(L, 0), max(T, 0), min(R, W), min(B, H)))
        clipped = Image.new("RGBA", out.size, (0, 0, 0, 0))
        clipped.paste(layer, (0, 0), mask)
        out.alpha_composite(clipped)

    # Background: grass-sky_0 (rect y 130, 256x158) sliced, border bottom 157:
    # the hills keep their size, the top row stretches up; pos y 3.5, size
    # 2.56 x 3.62, scale 3 -> world y -1.93 .. 8.93, hills 4.71 tall
    hills = sprite("grass-sky.png", 0, 130, 256, 158)
    bottom = 3.5 - 3.62 * 3 / 2
    sky_top = hills.crop((0, 0, 256, 1)).resize((256, 400), Image.NEAREST)
    paste_tiled(sky_top, 3, x0 - 8, y1 + 1, cx + units_w, bottom + 1.57 * 3 - 0.01)
    # tiles centred on x 0 as BackgroundLoop lays them (7.68 apart)
    start = -7.68 / 2 - 7.68 * 4
    paste_tiled(hills, 3, start, bottom + 1.58 * 3, start + 7.68 * 9, bottom)
    # SmallClouds: clouds.png, pos y 2.91, size 5.12 x 0.86, scale 1, 27%
    clouds = Image.open(TEX + "clouds.png").convert("RGBA")
    paste_tiled(clouds, 1, -2.56 - 5.12 * 2, 2.91 + 0.44, 2.56 + 5.12 * 2, 2.91 - 0.44, 0.2706)
    # BigClouds: pos (0.34, 4.31), size 5.12 x 1, scale 2, 84%
    paste_tiled(clouds, 2, 0.34 - 5.12 - 10.24 * 2, 4.31 + 0.88, 0.34 + 5.12 + 10.24 * 2, 4.31 - 0.88, 0.8353)
    # Foreground: grass-sky_1 (rect y 0, 256x127) tiled, pos y -0.85,
    # size 5.12 x 1.27, scale 3
    bushes = sprite("grass-sky.png", 0, 0, 256, 127)
    fg_top = -0.85 + 1.27 * 3 / 2
    paste_tiled(bushes, 3, start, fg_top, start + 7.68 * 9, fg_top - 1.27 * 3)
    return out


# (scene, centre x, centre y, side in world units, logo drop in icon px)
PRESETS = {
    # the user's pick (2026-11-16): the diamond columns and stars over the
    # logo, the snowy pines and domes around and below it
    "nightlight": ("mmim_project/Assets/Scenes/Levels/CustomNightLight.unity", 0.0, 4.6, 8.0, 8),
}


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    preset = "nightlight"
    if "--preset" in sys.argv:
        preset = sys.argv[sys.argv.index("--preset") + 1]
        args.remove(preset)
    logo_path, out_path = args[0], args[1]
    preview = args[2] if len(args) > 2 else None
    S = 256
    if preset == "grass":
        # 1 sprite pixel = 2 icon pixels (whole pixels: the 3x-scaled art at
        # 66.7 px/unit), 3.84 x 3.84 units: sky and a big cloud on top, the
        # pink/yellow hill tops either side of the logo, the bushes along the
        # bottom
        ppu = 200.0 / 3.0
        bg = render(cx=0.62, cy=2.2, units_w=S / ppu, units_h=S / ppu, px_per_unit=ppu)
        drop = 8
    else:
        import importlib.util
        import os
        spec = importlib.util.spec_from_file_location(
            "level_bg", os.path.join(os.path.dirname(os.path.abspath(__file__)), "level_bg.py"))
        level_bg = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(level_bg)
        scene, cx, cy, side, drop = PRESETS[preset]
        bg = level_bg.render(scene, cx - side / 2, cy - side / 2, cx + side / 2, cy + side / 2, S / side)
    bg = bg.resize((S, S), Image.NEAREST)

    # the logo as it is (its own black extrusion separates it from the art)
    logo = Image.open(logo_path).convert("RGBA")
    lw = 236
    lh = round(logo.height * lw / logo.width)
    logo = logo.resize((lw, lh), Image.LANCZOS)
    img = bg.copy()
    img.alpha_composite(logo, ((S - lw) // 2, (S - lh) // 2 + drop))

    rgb = img.convert("RGB")
    rgb.save(out_path, "JPEG", quality=95, subsampling=0, progressive=False, optimize=False)
    if preview:
        rgb.save(preview)
    print(out_path, rgb.size, preset)


if __name__ == "__main__":
    main()
