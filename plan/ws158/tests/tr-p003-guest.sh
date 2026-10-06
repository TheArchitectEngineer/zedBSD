#!/bin/sh
# ws158-p003: the compositor's text in Japanese, on the Venus guest of plan/ws158/tests/config-amd64-tr.mk (zdesktop
# --glass at 1280x800).  Judged by zdesktop's log and the pictures, never the guest's console:
#  1. The catalog is installed (/usr/share/keiland/locale/ja/wayland.tr); zdesktop starts in English
#     ("ZWL LANGUAGE language=en from=setting error=0"); en-bar.png.
#  2. keiland-settings set ui.language 1: "ZWL LANGUAGE language=ja from=setting error=0" without a restart;
#     ja-bar.png (the clock "10月5日(月)  14:05").
#  3. The volume's popup (its icon from "ZWL VOLUME icon"): ja-volume.png (サウンド, 消音).
#  4. The network's menu, then its details (Alt and a click): ja-network.png, ja-network-details.png (状態, IPv4 アドレス).
#  5. Wiseview (Super+Tab) with one window: ja-wiseview.png (ほかのウインドウはありません).
#  6. (The lock screen in Japanese is the AAT's desktop.language.lock-japanese: zdesktop locks only a session sessiond
#     started, which can unlock it (greeter.c kwl_lock), and this guest's zdesktop runs alone -- T1-207's MISSING line.)
#  7. ui.language 0: "ZWL LANGUAGE language=en"; no ERROR in zdesktop's log.
# PASS: the last line "tr-p003: status=0", and the pictures show the Japanese text (to Q1).
#   plan/tools/files/files-guest.sh start BUILD/hdd-image.img
#   plan/ws158/tests/tr-p003-guest.sh [OUTDIR]          (default build/ws158-shots/p003)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws158-shots/p003}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 1; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py --width 1280 --height 800 "$GUEST_RUNTIME/qmp.sock" "$@"; }
# A key held or let go through QMP (Alt for the network's details).
key() { timeout 40 python3 plan/ws049/tests/qmp-send.py "$GUEST_RUNTIME/qmp.sock" input-send-event "{\"events\":[{\"type\":\"key\",\"data\":{\"down\":$2,\"key\":{\"type\":\"qcode\",\"data\":\"$1\"}}}]}" >/dev/null 2>&1; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[k]eiland-ime|[f]iles" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
env='export XDG_RUNTIME_DIR=/tmp HOME=/root WAYLAND_DISPLAY=wayland-0'
status=0

# Counts the lines of zdesktop's log that match a pattern (on the host, whose grep knows UTF-8).
count() {
	guest "cat /tmp/zdesktop.log" > "$out/.log" 2>/dev/null
	n=$(LC_ALL=C.UTF-8 grep -cE "$1" "$out/.log")
	case "$n" in ''|*[!0-9]*) n=0;; esac
	echo "$n"
}
expect_log() {
	tries=0
	found=0
	while [ $tries -lt 10 ]; do
		found=$(count "$1")
		[ "$found" -ge 1 ] && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "$found" -ge 1 ]; then echo "log: $1 ok"; else echo "log: $1 MISSING"; status=1; fi
}
# The middle of the last "ZWL NAME icon x= y= width= height=" line.
icon() {
	guest "grep 'ZWL $1 icon x=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p'
}
click_icon() {
	set -- $(icon "$1")
	[ $# -eq 4 ] || { echo "icon $1: MISSING"; status=1; return; }
	pointer move $(( $1 + $3 / 2 )) $(( $2 + $4 / 2 )) sleep 300 down sleep 60 up sleep 1200
}

# 1. The catalog, the desktop in English.
guest "$stop_all" >/dev/null
if guest 'test -s /usr/share/keiland/locale/ja/wayland.tr && echo there' | grep -q there; then echo "catalog: ok"; else echo "catalog: MISSING"; status=1; fi
guest "$env; rm -f /tmp/wayland-0; c=/root/.config/keiland/desktop.conf; [ -f \$c ] && grep -v '^ui.language=' \$c > \$c.new; [ -f \$c.new ] && mv \$c.new \$c; picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=600 --width=1280 --height=800 --glass \$picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 5; echo started" >/dev/null
expect_log 'ZWL LANGUAGE language=en from=setting error=0'
check "$out/en-bar.png" >/dev/null

# 2. Japanese without a restart.
guest "$env; /bin/keiland-settings set ui.language 1" >/dev/null
expect_log 'ZWL LANGUAGE language=ja from=setting error=0'
sleep 1
check "$out/ja-bar.png" >/dev/null

# 3. The volume's popup.
click_icon VOLUME
expect_log 'ZWL VOLUME popup open'
check "$out/ja-volume.png" >/dev/null
keys "<esc>"

# 4. The network's menu, then its details.
click_icon NETWORK
expect_log 'ZWL NETWORK open'
check "$out/ja-network.png" >/dev/null
keys "<esc>"
set -- $(icon NETWORK)
if [ $# -eq 4 ]; then
	pointer move $(( $1 + $3 / 2 )) $(( $2 + $4 / 2 )) sleep 300 >/dev/null
	key alt true
	sleep 0.2
	pointer down sleep 60 up sleep 300 >/dev/null
	key alt false
	sleep 1.5
fi
expect_log 'ZWL NETWORK info open'
check "$out/ja-network-details.png" >/dev/null
keys "<esc>"

# 5. Wiseview with one window (Files).
guest "$env; /bin/files > /tmp/files.log 2>&1 </dev/null & sleep 4; echo started" >/dev/null
keys "<super-tab>"
expect_log 'ZWL WISEVIEW opening'
sleep 1
check "$out/ja-wiseview.png" >/dev/null
keys "<esc>"

# 6. The lock screen is the AAT's (desktop.language.lock-japanese): a zdesktop without sessiond does not lock.

# 7. Back to English; no error.
guest "$env; /bin/keiland-settings set ui.language 0" >/dev/null
guest 'cat /tmp/zdesktop.log' > "$out/.log"
n=$(grep -c 'ZWL LANGUAGE language=en from=setting error=0' "$out/.log")
[ "$n" -ge 2 ] && echo "back to English: ok" || { echo "back to English: MISSING"; status=1; }
if [ "$(count 'ERROR')" = 0 ]; then echo "no-error: ok"; else echo "no-error: FAILED"; status=1; fi
LC_ALL=C.UTF-8 grep -E 'ZWL LANGUAGE' "$out/.log"
guest "$stop_all" >/dev/null
echo "tr-p003: status=$status"
exit $status
