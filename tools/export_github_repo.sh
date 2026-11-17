#!/bin/sh
# export_github_repo.sh -- refresh github_repo/ (published as the port's
# GitHub repository) from the working folders.
#
# github_repo/.git (the published history) always stays: only the other
# top-level entries are deleted and written again. What goes in:
#   loader/  mvl_nx: Makefile, LICENSE, .gitignore, the icon, source/,
#            tools/, deps/ (renderer library sources, no build output)
#   unity/   the port's own Unity files: Assets/Scripts/SwitchPort (minus
#            Unity's own Il2CppSetOptionAttribute.cs), Assets/Editor/
#            MvlBuild.cs, Assets/WebGLTemplates/MMIM. MMIM's own scripts
#            and assets are MiiBumm's and never go in.
#   tools/   the release helpers (scrub, sign, icon, this script)
#   + tools/github/: README.md, THIRD_PARTY_NOTICES.md, LICENSE (GPL-3.0),
#     LICENSE-MIT, gitignore (as .gitignore), docs/ (the README's screenshots)
# Ends with a scan: no home path, user name or e-mail address anywhere.
#
# usage: tools/export_github_repo.sh
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/github_repo"
L="$ROOT/mvl_nx"
P="$ROOT/mmim_project/Assets"
mkdir -p "$OUT"
find "$OUT" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +

mkdir -p "$OUT/loader"
cp -p "$L/Makefile" "$L/LICENSE" "$L/.gitignore" "$L/mmim_nx.jpg" "$OUT/loader/"
rsync -a --exclude='.DS_Store' "$L/source" "$L/tools" "$OUT/loader/"
rsync -a --exclude='.DS_Store' --exclude='/out' --exclude='/src/' \
      --exclude='/build-*/' --exclude='/build-venv/' "$L/deps/" "$OUT/loader/deps/"

mkdir -p "$OUT/unity/Assets/Scripts" "$OUT/unity/Assets/Editor" "$OUT/unity/Assets/WebGLTemplates"
rsync -a --exclude='.DS_Store' --exclude='Il2CppSetOptionAttribute.cs*' \
      "$P/Scripts/SwitchPort" "$OUT/unity/Assets/Scripts/"
cp -p "$P/Scripts/SwitchPort.meta" "$OUT/unity/Assets/Scripts/"
cp -p "$P/Editor/MvlBuild.cs" "$P/Editor/MvlBuild.cs.meta" "$OUT/unity/Assets/Editor/"
rsync -a --exclude='.DS_Store' "$P/WebGLTemplates/MMIM" "$P/WebGLTemplates/MMIM.meta" \
      "$OUT/unity/Assets/WebGLTemplates/"

mkdir -p "$OUT/tools"
for f in scrub_apk_paths.py scrub_web_data.py sign_apk.sh make_nro_icon.py level_bg.py export_github_repo.sh; do
  cp -p "$ROOT/tools/$f" "$OUT/tools/"
done

G="$ROOT/tools/github"
cp -p "$G/README.md" "$G/THIRD_PARTY_NOTICES.md" "$G/LICENSE" "$G/LICENSE-MIT" "$OUT/"
cp -p "$G/gitignore" "$OUT/.gitignore"
rsync -a --exclude='.DS_Store' "$G/docs" "$OUT/"

# nothing personal: the home path, the account name, the e-mail address
user=$(basename "$HOME")
email=$(git config --global user.email 2>/dev/null || true)
name=$(git config --global user.name 2>/dev/null || true)
if LC_ALL=C grep -rliI --exclude-dir=.git -e "$HOME" -e "$user" ${email:+-e "$email"} ${name:+-e "$name"} "$OUT"; then
  echo "export: personal data found in the files above" >&2
  exit 1
fi
echo "exported: $OUT ($(find "$OUT" -type f ! -path '*/.git/*' | wc -l | tr -d ' ') files)"
