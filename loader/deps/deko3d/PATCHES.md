# deko3d in RL Sideswipe NX

Copied from the SwitchWakerHD port (`runtime/third_party/deko3d`), which carries the patch below.
`deps/build_deps.sh` builds it with `CMakeLists.txt` (release library only).

## Provenance (from SwitchWakerHD)

deko3d 0.5.0, https://github.com/devkitPro/deko3d at `5cc144d` (tag v0.5.0, the version of devkitPro's
`libdeko3d.a` this project linked before; LICENSE: zlib). Built by `CMakeLists.txt` (no Makefile) as the
static libraries `deko3d_release` and `deko3d_debug` (upstream's `libdeko3d` / `libdeko3dd`, same
`-DNDEBUG` / `-DDEBUG` and `-O2 -fno-rtti -fno-exceptions -std=gnu++17`). Only `include/`, `source/`,
`LICENSE` and `README.md` are kept.

`generated/` holds what upstream's Makefile writes with devkitPro's dekotools
(https://github.com/devkitPro/dekotools, `c15523c`), which the devkitA64 image does not ship:

    for d in source/maxwell/*.def; do n=$(basename $d .def)
        if [[ $n == *_3d ]]; then dekodef -h generated/$n.h -m generated/$n.mme $d
        else dekodef -h generated/$n.h $d; fi; done
    dekomme -o generated/mme_macros.h generated/engine_3d.mme $(ls source/maxwell/*.mme | sort)

Rerun it after editing a `.def` or `.mme` file (dekotools builds with meson, bison and flex, e.g. in a
`debian:bookworm` container).

Every change to upstream files is marked `SwitchWakerHD patch N`.

1. **Separate alpha destination factor** (`source/maxwell/gpu_3d_state.cpp`, `dkCmdBufBindBlendStates`):
   upstream writes `dstColorBlendFactor` into `FuncAlphaDst`, so a blend state with its own alpha
   factors blended alpha with the color's destination factor. The Picto Box writes each subject's
   number into the picture's alpha with color (SRC_ALPHA, ONE_MINUS_SRC_ALPHA) and alpha (ONE, ZERO)
   (CB_BLEND0_CONTROL 20010504): the alpha stayed 255 and Lenzo never found the subject (issue #7).
   Issue #12 (place names) was the same bug, worked around in the renderer before. Still unfixed
   upstream at `350f2b0` (2026-02).

## RL Sideswipe NX patches

2. **Vertex buffer reserve** (`source/maxwell/gpu_3d_vbo.cpp`, `dkCmdBufBindVtxBuffers`): each buffer
   writes two 3-word commands but upstream reserved 5 words per buffer, so a bind near the end of a
   command chunk spilled into memory past it (found by the nfsmw-nx port as a GPU method error).
3. **Level-0 tile height** (`source/dk_image.cpp`, `dkImageLayoutInitialize`): the picked tile height is
   shrunk with `adjustTileSize` against level 0's height, as `calcLevelOffset` and `ImageInfo` already
   do, so the texture descriptor agrees with the layout (images 6-8 block rows tall had mip 1 drawn
   over level 0; the mkw_nx and SwitchWakerHD ports hit the same bug).

4. **Command-list decompression** (`source/maxwell/gpu_3d_base.cpp`, `include/deko3d.h`): new
   `dkCmdBufDecompressImage`, the queue's present-time `decompressSurface` recorded into a command
   list. dkgl compresses colour render targets (`DkImageFlags_HwCompression`) and decompresses one
   before the copy or 2D engine reads or writes it; neither engine understands compression.
