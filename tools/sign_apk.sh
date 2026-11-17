#!/bin/sh
# sign_apk.sh -- re-sign an APK after tools/scrub_apk_paths.py.
#
# The scrub puts entries back with Info-ZIP's zip, which drops the APK's v2
# signature block and breaks the v1 digests: a phone refuses to install it
# ("package appears to be invalid"); the Switch's mmim_nx loader never checks
# a signature, so only the phone APK needs this. It signs with the key Unity
# signs with, this Mac's Android debug key (SHA-256 41:4D:BB:...:CA:12:A3).
# Keep ~/.android/debug.keystore backed up: a phone only installs an update
# over the app when it is signed by the same key.
#
# usage: tools/sign_apk.sh <apk>   (MMIM_KEYSTORE / MMIM_KEY_ALIAS /
#        MMIM_KS_PASS / MMIM_KEY_PASS pick another key)
set -e
apk="$1"
[ -f "$apk" ] || { echo "usage: $0 <apk>"; exit 2; }
bt=$(ls -d "$HOME/Library/Android/sdk/build-tools/"* | tail -1)
ks="${MMIM_KEYSTORE:-$HOME/.android/debug.keystore}"
tmp="$apk.signing"
"$bt/zipalign" -p -f 4 "$apk" "$tmp"
"$bt/apksigner" sign --ks "$ks" --ks-key-alias "${MMIM_KEY_ALIAS:-androiddebugkey}" \
    --ks-pass pass:"${MMIM_KS_PASS:-android}" --key-pass pass:"${MMIM_KEY_PASS:-android}" "$tmp"
"$bt/zipalign" -c -p 4 "$tmp"
"$bt/apksigner" verify "$tmp"
mv "$tmp" "$apk"
rm -f "$tmp.idsig"
echo "$apk: signed"
