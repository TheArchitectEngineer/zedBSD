#!/bin/sh
# BUG-177: Japanese input in a title bar's search field (zdesktop's own text field) on the IME guest
# (plan/ws095/tests/build-ime-image.sh; plan/ws095/tests/ime-guest.sh start).  The compositor under test
# (BUILD/bin/wayland) is copied in; zdesktop --glass at 1280x800 starts /usr/libexec/keiland-ime, and
# /bin/titlebar-probe --mode=controls shows a file manager's controls with a search field (id 5), as Files does.
#  1. A click on the search field gives it the keyboard ("ZWL TITLEBAR focus ... id=5 edit=0") and the input method
#     is activated for it ("ZWL IME activate field").
#  2. Direct input: "ab" reaches the field (probe: event=text id=5 text=ab).
#  3. Alt+Space chooses Japanese; "kanji" is a preedit in the field ("ZWL TITLEBAR ime ... preedit=かんじ";
#     preedit.png), Space converts it (preedit=漢字), Enter commits it (probe: text=ab漢字); a second Enter ends the
#     editing with the text (probe: event=done id=5 how=0 text=ab漢字) and the input method is deactivated.
#  4. Alt+Space back to direct; the compositor stays up, with no ERROR in its log.
#   plan/ws127/tests/bug177-guest.sh BUILD [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws095-run}"
export GUEST_RUNTIME
build=${1:?usage: bug177-guest.sh BUILD [OUTDIR]}
out=${2:-build/ws127-bug177}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
put() { timeout 90 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@" >/dev/null; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 0.7; }
. plan/tools/guest/zwl-clients.sh
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[t]itlebar-probe|[k]eiland-ime" | awk "{print \$1}"); do kill -CONT $p 2>/dev/null; kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[t]itlebar-probe|[k]eiland-ime" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
status=0

# Fails the run unless a guest file has a line matching a pattern within a few seconds (matched on the host, whose
# grep knows UTF-8; a Japanese pattern does not survive the guest's shell).
expect_log() {
	tries=0
	n=0
	while [ $tries -lt 5 ]; do
		guest "cat $1" > "$out/.log" 2>/dev/null
		n=$(LC_ALL=C.UTF-8 grep -cE "$2" "$out/.log")
		case "$n" in ''|*[!0-9]*) n=0;; esac
		[ "$n" -gt 0 ] && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "$n" -gt 0 ]; then echo "log: $2 ok"; else echo "log: $2 MISSING"; status=1; fi
}

# The centre of a control (client, place, ID) as zdesktop last logged it: "x y".
control() {
	guest "grep 'ZWL TITLEBAR control client=$(zwl_app_client $1) .* where=$2 id=$3 ' /tmp/zdesktop.log | tail -1" |
	    sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p' |
	    { read x y w h; echo "$(( ${x:-0} + ${w:-0} / 2 )) $(( ${y:-0} + ${h:-0} / 2 ))"; }
}

# The compositor under test (it starts the input method), and the probe.
guest "$stop_all" >/dev/null
put "$build/bin/wayland" /bin/wayland
guest 'chmod 755 /bin/wayland; export XDG_RUNTIME_DIR=/tmp HOME=/root; rm -f /tmp/wayland-0 /root/.config/kei/ime/ja-user.dict
picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --timeout=600 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4
/bin/titlebar-probe --show=Files --mode=controls --seconds=400 > /tmp/probe.log 2>&1 </dev/null & sleep 5; echo started' >/dev/null
zwl_app_clients
expect_log /tmp/zdesktop.log 'KEI-IME READY'
expect_log /tmp/probe.log 'TITLEBARPROBE show ready mode=controls'

# 1. The search field takes the keyboard; the input method serves it.
set -- $(control 1 floating 5)
pointer move $(($1 - 2)) "$2" sleep 150 move "$1" "$2" sleep 300 down sleep 60 up sleep 700
expect_log /tmp/zdesktop.log "ZWL TITLEBAR focus client=$zc1 surface=[0-9]+ id=5 edit=0"
expect_log /tmp/zdesktop.log "ZWL IME activate field client=$zc1 "

# 2. Direct input.
keys 'ab'
expect_log /tmp/probe.log 'TITLEBARPROBE event=text id=5 text=ab$'

# 3. Japanese: a preedit, a conversion, a commit, the end of the editing.
keys '<alt-spc>'
expect_log /tmp/zdesktop.log 'ZWL IME language=ja'
keys 'kanji'
expect_log /tmp/zdesktop.log 'ZWL TITLEBAR ime commit= preedit=かんじ text=ab$'
pointer move 1270 790 sleep 500
check "$out/preedit.png" >/dev/null
keys ' '
expect_log /tmp/zdesktop.log 'ZWL TITLEBAR ime commit= preedit=漢字 text=ab$'
keys '\n'
expect_log /tmp/probe.log 'TITLEBARPROBE event=text id=5 text=ab漢字$'
keys '\n'
expect_log /tmp/probe.log 'TITLEBARPROBE event=done id=5 how=0 text=ab漢字$'
expect_log /tmp/zdesktop.log 'ZWL IME deactivate'

# 4. Back to direct input; up, without errors.
keys '<alt-spc>'
running=$(guest 'ps -A -o args | grep -cE "[w]ayland( |$)"' | tail -1)
if [ "$running" = "1" ]; then echo "alive: ok"; else echo "alive: FAILED"; status=1; fi
guest 'grep -E "ZWL IME|ZWL TITLEBAR (focus|ime)|KEI-IME|ERROR" /tmp/zdesktop.log' > "$out/log.txt"
if grep -q ERROR "$out/log.txt"; then echo "no-error: FAILED"; status=1; else echo "no-error: ok"; fi
guest "$stop_all" >/dev/null

echo "bug177-guest: status $status (outputs in $out; preedit.png for the eye)"
exit $status
