#!/usr/bin/env bash
# Builds the renderer's two Switch libraries into deps/out:
#   lib/libdeko3d.a   deko3d 0.5.0 + deps/deko3d/PATCHES.md (vendored source)
#   lib/libuamcore.a  uam (GLSL -> DKSH) at UAM_COMMIT + deps/uam-patches + uam_bridge.cpp
#
# Host requirements: devkitA64 + libnx (DEVKITPRO), cmake, ninja, flex, bison >= 3
# (Homebrew), and a python with mako (PYTHON=...; a venv is created if needed).
# uam's source comes from UAM_GIT (a local clone of github.com/devkitPro/uam) when
# set, otherwise it is cloned. Only the pinned commit is used (git archive), so
# local edits in that clone are ignored.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
UAM_COMMIT=5a5afc2bae8b55409ab36ba45be63fcb73f68993
out="$here/out"
src="$here/src"
jobs="${JOBS:-8}"

python="${PYTHON:-}"
if [ -z "$python" ]; then
  if python3 -c 'import mako' 2>/dev/null; then
    python="$(command -v python3)"
  else
    if [ ! -x "$here/build-venv/bin/python3" ]; then
      python3 -m venv "$here/build-venv"
      "$here/build-venv/bin/pip" install --quiet mako
    fi
    python="$here/build-venv/bin/python3"
  fi
fi
"$python" -c 'import mako' || { echo "python with mako required" >&2; exit 1; }

# bison runs m4; macOS only ships an xcode-select stub without the developer tools.
if [ -z "${M4:-}" ]; then
  for candidate in /opt/homebrew/opt/m4/bin/m4 /usr/local/opt/m4/bin/m4; do
    if [ -x "$candidate" ]; then export M4="$candidate"; break; fi
  done
fi

# Stamp = commit + patch contents, so editing a patch re-extracts the source.
stamp="$UAM_COMMIT $(cat "$here"/uam-patches/*.patch | shasum | cut -d' ' -f1)"
if [ ! -f "$src/uam/.stamp" ] || [ "$(cat "$src/uam/.stamp")" != "$stamp" ]; then
  rm -rf "$src/uam"
  mkdir -p "$src/uam"
  git_dir="${UAM_GIT:-}"
  if [ -z "$git_dir" ]; then
    git_dir="$src/uam-git"
    [ -d "$git_dir/.git" ] || git clone https://github.com/devkitPro/uam.git "$git_dir"
  fi
  git -C "$git_dir" archive "$UAM_COMMIT" | tar -x -C "$src/uam"
  for patch in "$here"/uam-patches/*.patch; do
    patch -d "$src/uam" -p1 --quiet < "$patch"
  done
  printf '%s' "$stamp" > "$src/uam/.stamp"
fi

toolchain="$DEVKITPRO/cmake/Switch.cmake"
common=(-G Ninja -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON)

cmake "${common[@]}" -S "$here/deko3d" -B "$here/build-deko3d" >/dev/null
cmake --build "$here/build-deko3d" -j "$jobs"

cmake "${common[@]}" -S "$here/uam-cmake" -B "$here/build-uam" \
  -DUAM_ROOT="$src/uam" -DUAM_PYTHON="$python" >/dev/null
cmake --build "$here/build-uam" -j "$jobs"

mkdir -p "$out/lib" "$out/include"
cp "$here/build-deko3d/libdeko3d.a" "$out/lib/libdeko3d.a"
cp "$here/build-deko3d/libdeko3dd.a" "$out/lib/libdeko3dd.a"
cp "$here/build-uam/libuamcore.a" "$out/lib/libuamcore.a"
cp "$here/deko3d/include/deko3d.h" "$here/deko3d/include/deko3d.hpp" "$out/include/"
cp "$here/uam-bridge/uam_bridge.h" "$out/include/"
echo "deps: $(ls -la "$out/lib" | awk 'NR>1{print $NF" "$5}' | tr '\n' ' ')"
