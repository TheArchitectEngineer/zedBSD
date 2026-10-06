#!/bin/sh
# ws102-p017: the editing operations (kl_edit_v1, libkeiland's default), the keys for a window without them, and the
# previous application, on the Venus guest of the inset image (plan/ws102/tests/build-inset-image.sh: the WS079 demo
# image with Text Editor and Terminal).  zdesktop --glass at 1280x800; the tool face's buttons are not there yet
# (ws102-p016), so the shortcuts that stand in for them are pressed: Super+Alt with S (select_begin), C, V and P.
#  1. Text Editor A (a.txt: "HELLOWORLD") and B (b.txt, empty), B on top.  The previous application brings A forward
#     (KWL FOCUS previous), with the QWERTY row open (it stays open).
#  2. In A: select_begin, Right five times, copy (both through the protocol: KWL EDIT action=... via=protocol); A's
#     state showed a selection being made (flags with 0x10) and then something to paste (0x2).
#  3. The previous application again (B), paste; B saved with Ctrl+S holds "HELLO".
#  The state the buttons would show (Super+Alt+Q, kwl_edit_state; Text Editor's real state since ws102-p023): while
#  selecting 0xa3 (copy, cut, select all, select end), after the copy 0x67 (copy, cut, paste, select all, begin); Terminal 0x5 (copy, paste), wlshm 0x3f (the operations with keys).
#  4. Terminal (no protocol, a terminal): copy is Ctrl+Shift+C (via=keys ... modifiers=0x5 terminal=1), cut has no keys
#     (via=none); wlshm (no protocol): copy is Ctrl+C (modifiers=0x4 terminal=0); both keep running.
# Prints "edit-guest: PASS" or "edit-guest: FAIL".
#
#   plan/ws102/tests/edit-guest.sh IMAGE OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
image=$1
out=$2
mkdir -p "$out"
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws102-p017-run}"
export GUEST_RUNTIME
status=0
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@" >/dev/null; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@" >/dev/null; sleep 0.4; }
shot() { pointer move 1275 400 sleep 300; python3 plan/ws035/tests/zdesktop-check.py "$out/$1" --runtime "$GUEST_RUNTIME" >/dev/null; echo "shot $out/$1"; }
count() { guest "grep -cE '$1' /tmp/zdesktop.log" | tail -1; }

# Waits (up to 10 s) for more than N lines of zdesktop's log matching a pattern.
expect_more() {
	tries=0
	while [ $tries -lt 10 ]; do
		found=$(count "$1")
		[ "${found:-0}" -gt "$2" ] 2>/dev/null && { echo "log: $1 ok"; return 0; }
		tries=$((tries + 1))
		sleep 1
	done
	echo "log: $1 MISSING"
	status=1
	return 1
}

# The guest and zdesktop.
sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
timeout 180 sh plan/ws035/tests/zdesktop-guest.sh start "$image" >/dev/null 2>&1
tries=0
until guest 'echo up' | grep -q '^up$' || [ $tries -ge 30 ]; do tries=$((tries + 1)); sleep 5; done
guest 'service stop greeter >/dev/null 2>&1; printf "HELLOWORLD\n" > /root/a.txt; : > /root/b.txt; export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null &
i=0; while ! grep -q "KWL OSK zone" /tmp/zdesktop.log && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done; sleep 1; echo started' >/dev/null

# 1. A, then B on top; the QWERTY row; the previous application (A) with the row still open.
guest "export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/textedit --timeout-s=800 /root/a.txt > /tmp/a.log 2>&1 </dev/null & sleep 6; echo started" >/dev/null
guest "export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/textedit --timeout-s=800 /root/b.txt > /tmp/b.log 2>&1 </dev/null & sleep 6; echo started" >/dev/null
expect_more 'KWL EDIT create client=' 1
expect_more 'KWL EDIT state client=[0-9]+ edit=[0-9]+ actions=0xff' 1
pointer move 6 792 sleep 200 down sleep 80 move 78 721 sleep 60 move 150 650 sleep 120 up sleep 1500
expect_more 'KWL OSK open kind=qwerty' 0
closes=$(count 'KWL OSK close')
keys '<super-alt-p>'
expect_more 'KWL FOCUS previous surface=' 0
[ "$(count 'KWL OSK close')" = "$closes" ] && echo "keyboard stays open ok" || { echo "keyboard closed FAIL"; status=1; }
shot previous.png

# 2. In A: select_begin, five times Right, copy.
keys '<ctrl-home>'
keys '<super-alt-s>'
expect_more 'KWL EDIT action=select_begin via=protocol' 0
expect_more 'KWL EDIT state client=[0-9]+ edit=[0-9]+ actions=0xff flags=0x1[0-9a-f]' 0
for n in 1 2 3 4 5; do keys '<right>'; done
shot selected.png
keys '<super-alt-q>'
expect_more 'KWL EDIT enabled=0xa3 protocol=1' 0
keys '<super-alt-c>'
expect_more 'KWL EDIT action=copy via=protocol' 0
sleep 1
keys '<super-alt-q>'
expect_more 'KWL EDIT enabled=0x67 protocol=1' 0

# 3. B, paste, save.
keys '<super-alt-p>'
expect_more 'KWL FOCUS previous surface=' 1
keys '<super-alt-v>'
expect_more 'KWL EDIT action=paste via=protocol' 0
sleep 1
keys '<ctrl-s>'
sleep 2
shot pasted.png
pasted=$(guest 'cat /root/b.txt' | tail -1)
echo "b.txt: '$pasted'"
[ "$pasted" = "HELLO" ] && echo "pasted HELLO ok" || { echo "pasted FAIL"; status=1; }
guest 'grep -E "KWL EDIT state" /tmp/zdesktop.log' | tail -8

# 4. Terminal and wlshm: the keys.
pointer move 6 792 sleep 200 down sleep 80 move 78 721 sleep 60 move 150 650 sleep 120 up sleep 1000
guest "export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/terminal > /tmp/term.log 2>&1 </dev/null & sleep 6; echo started" >/dev/null
keys '<super-alt-c>'
expect_more 'KWL EDIT action=copy via=keys key=46 modifiers=0x5 terminal=1' 0
keys '<super-alt-x>'
expect_more 'KWL EDIT action=cut via=none reason=no-keys' 0
keys '<super-alt-q>'
expect_more 'KWL EDIT enabled=0x5 protocol=0' 0
guest "export XDG_RUNTIME_DIR=/tmp; /bin/wlshm --size=480x320 --frames=100000 > /tmp/wlshm.log 2>&1 </dev/null & sleep 4; echo started" >/dev/null
keys '<super-alt-c>'
expect_more 'KWL EDIT action=copy via=keys key=46 modifiers=0x4 terminal=0' 0
keys '<super-alt-q>'
expect_more 'KWL EDIT enabled=0x3f protocol=0' 0
shot keys.png
alive=$(guest 'ps -A -o args | grep -cE "[w]lshm|/bin/[t]erminal"' | tail -1)
[ "${alive:-0}" -ge 2 ] && echo "terminal and wlshm running ok" || { echo "terminal or wlshm gone FAIL ($alive)"; status=1; }

# Nothing failed.
errors=$(count 'KWL ERROR|protocol error')
[ "${errors:-1}" = 0 ] && echo "no errors ok" || { echo "errors: $errors FAIL"; status=1; }
guest 'grep -E "KWL (EDIT|FOCUS|OSK (open|close)|ERROR)" /tmp/zdesktop.log' > "$out/zdesktop.log"
sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
[ $status -eq 0 ] && echo "edit-guest: PASS" || echo "edit-guest: FAIL"
exit $status
