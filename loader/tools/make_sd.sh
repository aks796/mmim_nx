#!/bin/sh
# Stage the SD card layout of the port and zip it:
#
#   out/SD_CARD/switch/mvl_nx/mvl_nx.nro
#
# That is all: the .nro sets the game up from the player's APK dropped next
# to it. An APK in local_apk/ (git-ignored: the developer's own copy) goes in
# next to the .nro too, for a card that is ready to play -- a package with it
# must never be published.
#
# usage: tools/make_sd.sh
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
OUT="$HERE/out"
DEST="$OUT/SD_CARD/switch/mvl_nx"
# only what this script makes: other things under out/SD_CARD (a debug/ copy
# from the console) stay
rm -rf "$DEST" "$OUT/SD_CARD.zip"
mkdir -p "$DEST"
cp "$HERE/mvl_nx.nro" "$DEST/"
for apk in "$HERE"/local_apk/*.apk; do
  [ -f "$apk" ] && cp "$apk" "$DEST/" && echo "with your APK: $(basename "$apk")"
done
(cd "$OUT" && zip -qr SD_CARD.zip SD_CARD/switch)
echo "zipped: $OUT/SD_CARD.zip"
ls -la "$OUT/SD_CARD.zip"
