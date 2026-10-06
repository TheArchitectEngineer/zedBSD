#!/bin/sh
# ws089-p017a: the dark appearance.  On the Venus guest of the Settings image (build-settings-image.sh BUILD, built from
# the commit under test: zdesktop, libwayland-client, libkeiland, Settings, Files and keiland-settings come with it),
# zdesktop --glass at 1280x800 on the default wallpaper, HOME=/root.
#  1. Light (appearance.dark reset first): Settings on Home; light-settings.png.  Its text and the system bar's clock
#     keep a contrast of C7_MIN_CONTRAST (4.5) or more (plan/ws089/tests/c7-either.py, the regions of
#     plan/ws099/tests/c7-contrast.sh); the default look is unchanged (the user's eye on the picture against T1's
#     earlier C7 pictures).
#  2. "keiland-settings set appearance.dark 1" (as the Appearance page's switch does): zdesktop logs "KWL PREFERENCES
#     key=appearance.dark applied value=1" and "KWL THEME appearance=1"; Settings logs "ZSETTINGS APPEARANCE
#     appearance=1" and draws again; dark-settings.png: the glass, the cards and the system bar dark, the text light,
#     every region's contrast 4.5 or more.
#  3. Files started in the dark: its log says "APPEARANCE appearance=1" at once; dark-files.png; its regions 4.5 or more.
#  4. zdesktop started again (the setting is kept in desktop.conf): Settings starts dark ("APPEARANCE appearance=1"
#     before its first frame); restart-settings.png.
#  5. "keiland-settings reset appearance.dark": "KWL THEME appearance=0", Settings "APPEARANCE appearance=0";
#     light-again.png looks like light-settings.png.
#  6. No ERROR in zdesktop's log.
# PASS: the last line "settings-p017: status 0" and the pictures as above (to Q1).
#
#   plan/ws089/tests/settings-guest.sh start IMAGE      (the guest must be up)
#   plan/ws089/tests/settings-p017.sh [OUTDIR]           (default build/ws089-shots/p017)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws089-run}"
export GUEST_RUNTIME
out=${1:-build/ws089-shots/p017}
mkdir -p "$out"
C7_MIN_CONTRAST=${C7_MIN_CONTRAST:-4.5}
# The regions (x0 y0 x1 y1 at 1280x800) of plan/ws099/tests/c7-contrast.sh: the clock, Settings' Home, Files' Home.
C7_CLOCK=${C7_CLOCK:-"clock 1130 4 1272 30"}
C7_SETTINGS=${C7_SETTINGS:-"s-title 338 130 466 160 s-subtitle 338 166 592 186 s-section 340 216 436 233 s-side-wifi 100 115 143 133 s-side-appearance 100 309 194 327 s-bar-title 94 59 155 77"}
C7_FILES=${C7_FILES:-"f-heading 326 324 394 341 f-group 96 404 160 416 f-side-recents 124 432 182 446 f-bar-home 290 59 335 77"}
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[s]ettings|[f]iles" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[s]ettings|[f]iles" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
ends='for p in $(ps -A -o pid,args | awk "\$2 ~ /^\/bin\/(settings|files)\$/ {print \$1}"); do kill $p; done; sleep 1; echo ok'
env_line='export XDG_RUNTIME_DIR=/tmp HOME=/root WAYLAND_DISPLAY=wayland-0'
start_desktop="$env_line; rm -f /tmp/wayland-0
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass --wallpaper=/usr/share/keiland/wallpaper.png > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started"
status=0
. plan/ws089/tests/settings-wait.sh
shot() {
	pointer move 5 790 sleep 600 >/dev/null
	check "$out/$1" >/dev/null
	echo "shot: $out/$1"
}
expect() {
	# expect NAME FILE PATTERN: the guest's FILE has a line matching PATTERN (grep -E).
	if guest "grep -cE '$3' $2" | tail -1 | grep -qv '^0$'; then echo "$1: ok"; else echo "$1: FAILED"; status=1; fi
}
contrast() {
	# contrast PICTURE REGIONS...: every region's contrast is C7_MIN_CONTRAST or more.
	picture=$1
	shift
	lines=$(python3 plan/ws089/tests/c7-either.py "$picture" "$@")
	echo "$lines"
	for ratio in $(echo "$lines" | sed -n 's/.*contrast=\([0-9.]*\).*/\1/p'); do
		if ! python3 -c "import sys; sys.exit(0 if $ratio >= $C7_MIN_CONTRAST else 1)"; then
			echo "contrast $picture: FAILED ($ratio)"
			status=1
		fi
	done
}
settings_tool() {
	guest "$env_line; /bin/keiland-settings --timeout-ms=3000 $1 2>&1" | grep KEILAND-SETTINGS
}

# The desktop, light.
wait_guest
guest "$stop_all" >/dev/null
guest "$start_desktop" >/dev/null
wait_desktop
settings_tool "reset appearance.dark"

# 1. Light.
guest "$env_line; /bin/settings --timeout-s=600 > /tmp/s.log 2>&1 </dev/null & sleep 6; echo started" >/dev/null
expect settings-ready /tmp/s.log 'ZSETTINGS READY'
expect settings-light /tmp/s.log 'ZSETTINGS APPEARANCE appearance=0'
shot light-settings.png
contrast "$out/light-settings.png" $C7_CLOCK $C7_SETTINGS

# 2. Dark, set as the Appearance page sets it.
settings_tool "set appearance.dark 1"
sleep 2
expect applied /tmp/zdesktop.log 'KWL PREFERENCES key=appearance.dark applied value=1'
expect told /tmp/zdesktop.log 'KWL THEME appearance=1'
expect settings-dark /tmp/s.log 'ZSETTINGS APPEARANCE appearance=1'
shot dark-settings.png
contrast "$out/dark-settings.png" $C7_CLOCK $C7_SETTINGS

# 3. Files in the dark.
guest "$ends" >/dev/null
guest "$env_line; /bin/files --timeout-s=200 > /tmp/f.log 2>&1 </dev/null & sleep 8; echo started" >/dev/null
expect files-dark /tmp/f.log 'APPEARANCE appearance=1'
shot dark-files.png
contrast "$out/dark-files.png" $C7_FILES

# 4. zdesktop again: the dark appearance is kept.
guest "$stop_all" >/dev/null
guest "$start_desktop" >/dev/null
wait_desktop
expect kept /tmp/zdesktop.log 'KWL PREFERENCES key=appearance.dark applied value=1'
guest "$env_line; /bin/settings --timeout-s=600 > /tmp/s.log 2>&1 </dev/null & sleep 6; echo started" >/dev/null
expect restart-dark /tmp/s.log 'ZSETTINGS APPEARANCE appearance=1'
shot restart-settings.png
contrast "$out/restart-settings.png" $C7_CLOCK $C7_SETTINGS

# 5. Back to light.
settings_tool "reset appearance.dark"
sleep 2
expect told-light /tmp/zdesktop.log 'KWL THEME appearance=0'
expect settings-light-again /tmp/s.log 'ZSETTINGS APPEARANCE appearance=0'
shot light-again.png
contrast "$out/light-again.png" $C7_CLOCK $C7_SETTINGS

# 6. No ERROR.
if guest 'grep -c ERROR /tmp/zdesktop.log' | tail -1 | grep -qx 0; then echo "no-error: ok"; else echo "no-error: FAILED"; status=1; fi
guest "$stop_all" >/dev/null

echo "settings-p017: status $status (pictures in $out)"
exit $status
