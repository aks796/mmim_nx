# Third-party notices

The code written for this port is copyright (c) 2026 aks796:
- **`loader/`:** GPL-3.0 ([LICENSE](LICENSE)).
- **`unity/` and `tools/`:** MIT ([LICENSE-MIT](LICENSE-MIT)).

The parts below come from other projects and keep their own licences.

## The game

MiiBumm's Mega Item Mod (MiiBumm) and NSMB Mario vs. Luigi (ipodtouch0218) are not part of this repository. The release
packages carry MMIM's game as built for Android and the web: it is MiiBumm's work, distributed as MMIM itself is. The
characters, sprites, sounds and music belong to Nintendo.

## Switch loader (`loader/`)

| Code | From | Licence |
|---|---|---|
| The ELF loader, its JNI/libc layers and the files that say so in their header | so-loader by Andy Nguyen (TheFloW) and fgsfds | MIT: `loader/LICENSE` |
| `source/pthr.c`, `source/pthr.h` | gm666q's [lswtcs-vita](https://github.com/gm666q/lswtcs-vita) (`reimpl/pthr.c`), through earlier Switch ports | MIT |
| One accessor in `source/default_window.c` | [libnx](https://github.com/switchbrew/libnx) | ISC |
| `source/saltynx.c` | StevensND's [nfsmw-nx](https://github.com/StevensND/nfsmw-nx) SaltyNX template (`extras/saltynx-template`), ported to C | GPL-3.0 |

Because `saltynx.c` is GPL-3.0, the loader as a whole, and the `mmim_nx.nro` built from it, is distributed under the
GPL-3.0. The MIT-licensed files can still be reused under the MIT licence.

## Renderer libraries (`loader/deps/`)

These are statically linked into `mmim_nx.nro` and rebuilt from this folder by `build_deps.sh`.

| Code | From | Licence |
|---|---|---|
| `deps/deko3d/` | [deko3d](https://github.com/devkitPro/deko3d) 0.5.0 by devkitPro (fincs), with patches listed in `deps/deko3d/PATCHES.md` | zlib (`deps/deko3d/LICENSE`) |
| uam, fetched by `build_deps.sh` at a pinned commit and patched by `deps/uam-patches/` | [uam](https://github.com/devkitPro/uam) by devkitPro; its `mesa-imported/` part from Mesa | zlib; Mesa's part MIT |
| deko3d patch 1 (separate alpha destination blend factor) and `deps/uam-patches/0003-switchwakerhd-gm107-scheduling-fixes.patch` | The SwitchWakerHD port | MPL-2.0 |

The MPL-2.0 covers the changes those two patches make. The files they touch stay under the MPL-2.0 for those changes,
and their source is this folder.

## Web page (`unity/Assets/WebGLTemplates/MMIM`)

The page's loading code follows Unity's WebGL template, with the variables Unity fills in at build time.
