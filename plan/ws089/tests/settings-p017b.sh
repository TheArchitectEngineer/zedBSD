#!/bin/sh
# ws089-p017b: the applications in the dark appearance.  On the Venus guest of config-amd64-dark-apps.mk (built from the
# commit under test), zdesktop --glass at 1280x800 on the default wallpaper, HOME=/root.  For each application (Text
# Editor, Notes, PDF Viewer, Image Viewer (a bundled wallpaper: the image has no /usr/share/keiland/wallpaper.png, the
# desktop draws its built-in one), Phone, Calendar, Mailer, Files, Settings), one at a time:
#  1. started in the light appearance: light-NAME.png (the look as before);
#  2. "keiland-settings set appearance.dark 1" while it runs: its log says "APPEARANCE appearance=1" (the kl_app
#     programs, Phone, Calendar and Mailer, log nothing of their own: the picture shows it), and dark-NAME.png shows it
#     drawn again dark (dark ground and cards, light text; a document's pages and pictures as they are);
#  3. "keiland-settings reset appearance.dark".
# No ERROR in zdesktop's log.  PASS: the last line "settings-p017b: status 0", and the pictures (to Q1; the eye judges
# them: nothing light left on a dark ground, nothing unreadable).
#
#   plan/ws089/tests/settings-guest.sh start IMAGE      (the guest must be up)
#   plan/ws089/tests/settings-p017b.sh [OUTDIR]          (default build/ws089-shots/p017b)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws089-run}"
export GUEST_RUNTIME
out=${1:-build/ws089-shots/p017b}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
programs='textedit|notes|pdfviewer|imageview|phone|calendar|mailer|files|settings'
# ps prints the pid first, so the program is matched on the second field (T1-198: a pattern anchored at the line's
# start never matched, the applications were not ended and their windows piled up).
stop_all="service stop greeter >/dev/null 2>&1; for p in \$(ps -A -o pid,args | awk '\$2 ~ /^\/bin\/(wayland|$programs)\$/ {print \$1}'); do kill \$p; done; sleep 1"
ends="for p in \$(ps -A -o pid,args | awk '\$2 ~ /^\/bin\/($programs)\$/ {print \$1}'); do kill \$p; done; sleep 1; echo ok"
env_line='export XDG_RUNTIME_DIR=/tmp HOME=/root WAYLAND_DISPLAY=wayland-0'
status=0
. plan/ws089/tests/settings-wait.sh
shot() {
	pointer move 5 790 sleep 600 >/dev/null
	check "$out/$1" >/dev/null
	echo "shot: $out/$1"
}
tool() { guest "$env_line; /bin/keiland-settings --timeout-ms=3000 $1 2>&1" | grep KEILAND-SETTINGS; }

# The desktop, light; a picture and a document for the viewers.
wait_guest
guest "$stop_all" >/dev/null
guest "$env_line; rm -f /tmp/wayland-0
/bin/wayland --testing --timeout=1500 --width=1280 --height=800 --glass --wallpaper=/usr/share/keiland/wallpaper.png > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started" >/dev/null
wait_desktop
tool "reset appearance.dark"
guest 'printf "The dark appearance.\nA second line.\n" > /root/dark.txt; ls /usr/share/doc/*.pdf /usr/share/keiland/*.pdf 2>/dev/null | head -1 > /tmp/pdf-path' >/dev/null
pdf=$(guest 'cat /tmp/pdf-path' | tail -1)

# Each application: light, dark while it runs, light again.
for name in textedit notes pdfviewer imageview phone calendar mailer files settings; do
	case $name in
	textedit) args="/root/dark.txt" ;;
	pdfviewer) args="$pdf" ;;
	imageview) args="/usr/share/keiland/wallpapers/Lagoon.png" ;;
	*) args="" ;;
	esac
	guest "$ends" >/dev/null
	guest "$env_line; /bin/$name $args > /tmp/app-$name.log 2>&1 </dev/null & sleep 7; echo started" >/dev/null
	shot "light-$name.png"
	tool "set appearance.dark 1"
	sleep 3
	shot "dark-$name.png"
	case $name in
	phone|calendar|mailer) ;;
	*)
		if guest "grep -c 'APPEARANCE appearance=1' /tmp/app-$name.log" | tail -1 | grep -qv '^0$'; then
			echo "$name: told dark ok"
		else
			echo "$name: APPEARANCE appearance=1 MISSING"
			status=1
		fi
		;;
	esac
	tool "reset appearance.dark"
	sleep 2
done

# No ERROR.
if guest 'grep -c ERROR /tmp/zdesktop.log' | tail -1 | grep -qx 0; then echo "no-error: ok"; else echo "no-error: FAILED"; status=1; fi
guest "$stop_all" >/dev/null
echo "settings-p017b: status $status (pictures in $out)"
exit $status
