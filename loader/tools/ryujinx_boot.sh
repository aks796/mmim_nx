#!/bin/sh
# Short boot check in Ryujinx: copy the fresh .nro into its SD card, run for
# N seconds, stop it and print the end of debug.log. Not a substitute for
# hardware -- just catches loader and start-up problems before an SD round
# trip.
#
# It runs its own Ryujinx: a copy of the app under another name and its own
# data folder (APFS clones of the real ones, ../ryujinx/), so another
# project's test script (which stops every "Ryujinx.app") and the user's own
# Ryujinx are never touched, and this one is never stopped by them. The APK
# must already be in ../ryujinx/root/sdcard/switch/mvl_nx/ (a symlink is
# fine).
#
# usage: tools/ryujinx_boot.sh [seconds]
#        GAME=mmim tools/ryujinx_boot.sh [seconds]   (mmim_nx.nro in /switch/mmim_nx)
HERE=$(cd "$(dirname "$0")/.." && pwd)
BOX=${RYU_BOX:-$HERE/../ryujinx}
RYU_APP=$BOX/RyujinxMVL.app/Contents/MacOS/Ryujinx
RYU=$BOX/root
NRO=mvl_nx
[ "$GAME" = mmim ] && NRO=mmim_nx
DEST="$RYU/sdcard/switch/$NRO"
SECS=${1:-60}

STAMP=$(date +%s)
mkdir -p "$DEST"
cp "$HERE/$NRO.nro" "$DEST/"
rm -f "$DEST/debug.log"

"$RYU_APP" -r "$RYU" "$DEST/$NRO.nro" >/dev/null 2>&1 &
PID=$!
sleep "$SECS"
kill $PID 2>/dev/null
sleep 2
kill -9 $PID 2>/dev/null

echo "=== debug.log (last 150 lines)"
tail -150 "$DEST/debug.log" 2>/dev/null
LOG=$(ls -t "$RYU/Logs" 2>/dev/null | head -1)
[ -n "$LOG" ] && [ "$(stat -f %m "$RYU/Logs/$LOG")" -ge "$STAMP" ] || LOG=""
echo "=== ryujinx ($LOG): errors and guest crashes"
[ -n "$LOG" ] && grep -aE "\|E\| HLE|Unhandled|InvalidMemoryRegion|Exception|Abort|svc.*Error|Guest stack trace" "$RYU/Logs/$LOG" | grep -v "mvk-info" | tail -40
