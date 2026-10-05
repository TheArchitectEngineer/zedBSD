#!/bin/sh
# ws102-p018: the clipboard's history (userland/desktop/wayland/clipboard.c) on the Venus guest of the inset image
# (plan/ws102/tests/build-inset-image.sh: data-probe, Text Editor).  zdesktop --glass at 1280x800; the keyboard's
# history tab is ws102-p016's, so the shortcuts that stand in for it are pressed: Super+Alt+H logs the history
# (ZWL CLIP history count=N index:length:checksum ..., newest first; the log never has the text), Super+Alt+1 ... 0
# pastes an item.
#  1. Three applications copy in turn: data-probe a ("alpha one", key s), Text Editor (its text "editor text", select
#     all and copy), data-probe c ("gamma three").  The history is c, the editor's, a.
#  2. Super+Alt+2 pastes the editor's text into data-probe c: it is the selection (zdesktop's own), c receives it
#     (DATAPROBE received ... text=editor text), and it comes first.
#  3. A secret copy (data-probe --secret, which offers x-kde-passwordManagerHint) is not kept (ZWL CLIP skip
#     reason=secret); its text is nowhere in zdesktop's log.
#  4. A second Text Editor copies "item 1" ... "item 12": the history keeps ten, "item 12" first and "item 3" last.
# The lock screen's clearing is clip-lock.sh's (a login session).
# Prints "clip-guest: PASS" or "clip-guest: FAIL".
#
#   plan/ws102/tests/clip-guest.sh IMAGE OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
image=$1
out=$2
mkdir -p "$out"
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws102-p018-run}"
export GUEST_RUNTIME
status=0
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@" >/dev/null; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@" >/dev/null; sleep 0.5; }
shot() { pointer move 1275 400 sleep 300; python3 plan/ws035/tests/zdesktop-check.py "$out/$1" --runtime "$GUEST_RUNTIME" >/dev/null; echo "shot $out/$1"; }
count() { guest "grep -cE '$1' /tmp/zdesktop.log" | tail -1; }
sum() { python3 -c 'import sys
s = 2166136261
for b in sys.argv[1].encode():
    s = ((s ^ b) * 16777619) & 0xffffffff
print("%08x" % s)' "$1"; }

# Waits (up to 10 s) for more than N lines of a guest file matching a pattern.
expect_more() {
	tries=0
	while [ $tries -lt 10 ]; do
		found=$(guest "grep -cE '$2' $1" | tail -1)
		[ "${found:-0}" -gt "$3" ] 2>/dev/null && { echo "log: $2 ok"; return 0; }
		tries=$((tries + 1))
		sleep 1
	done
	echo "log: $2 MISSING"
	status=1
	return 1
}

# The last history line (Super+Alt+H), compared with the checksums expected, newest first.
history_is() {
	before=$(count 'ZWL CLIP history count=')
	keys '<super-alt-h>'
	expect_more /tmp/zdesktop.log 'ZWL CLIP history count=' "$before" >/dev/null
	line=$(guest "grep 'ZWL CLIP history count=' /tmp/zdesktop.log | tail -1" | tail -1)
	got=$(echo "$line" | sed 's/.*history //' | tr ' ' '\n' | sed -n 's/^count=\([0-9]*\)$/count=\1/p; s/^[0-9]*:[0-9]*:\([0-9a-f]*\)$/\1/p' | tr '\n' ' ')
	want="$*"
	if [ "$got" = "$want " ]; then
		echo "history $1 ... ok"
	else
		echo "history: got '$got' want '$want' FAIL"
		status=1
	fi
}

# The guest and zdesktop.
sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
timeout 180 sh plan/ws035/tests/zdesktop-guest.sh start "$image" >/dev/null 2>&1
tries=0
until guest 'echo up' | grep -q '^up$' || [ $tries -ge 30 ]; do tries=$((tries + 1)); sleep 5; done
guest 'service stop greeter >/dev/null 2>&1; printf "editor text" > /root/e.txt; export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null &
i=0; while ! grep -q "ZWL OSK zone" /tmp/zdesktop.log && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done; sleep 1; echo started' >/dev/null
start() { guest "export XDG_RUNTIME_DIR=/tmp HOME=/root; $1 > $2 2>&1 </dev/null & sleep 5; echo started" >/dev/null; }

# 1. Three applications copy in turn.
start '/bin/data-probe --token=a --text="alpha one" --color=3a6ea5 --timeout-s=600' /tmp/pa.log
keys 's'
expect_more /tmp/zdesktop.log 'ZWL CLIP add length=9 ' 0
start '/bin/textedit --timeout-s=600 /root/e.txt' /tmp/te.log
keys '<super-alt-a>'
keys '<super-alt-c>'
expect_more /tmp/zdesktop.log 'ZWL CLIP add length=11 ' 0
start '/bin/data-probe --token=c --text="gamma three" --color=a53a6e --timeout-s=600' /tmp/pc.log
keys 's'
expect_more /tmp/zdesktop.log 'ZWL CLIP add length=11 sum='"$(sum 'gamma three')" 0
history_is count=3 "$(sum 'gamma three')" "$(sum 'editor text')" "$(sum 'alpha one')"
shot three.png

# 2. The editor's text pasted into c from the history.
keys '<super-alt-2>'
expect_more /tmp/zdesktop.log 'ZWL CLIP paste index=1 length=11' 0
expect_more /tmp/pc.log 'DATAPROBE received bytes=11 text=editor text' 0
history_is count=3 "$(sum 'editor text')" "$(sum 'gamma three')" "$(sum 'alpha one')"
shot pasted.png

# 3. A secret copy is not kept.
start '/bin/data-probe --token=s --secret --text=hunter2secret --color=6e6e6e --timeout-s=600' /tmp/ps.log
keys 's'
expect_more /tmp/zdesktop.log 'ZWL CLIP skip reason=secret' 0
history_is count=3 "$(sum 'editor text')" "$(sum 'gamma three')" "$(sum 'alpha one')"
[ "$(count 'hunter2')" = 0 ] && echo "no secret in the log ok" || { echo "secret in the log FAIL"; status=1; }
guest "for p in \$(ps -A -o pid,args | grep '[t]oken=s' | awk '{print \$1}'); do kill \$p; done" >/dev/null
sleep 2

# 4. Twelve copies from a new Text Editor (on top, with the keyboard): ten kept.
guest ': > /root/f.txt' >/dev/null
start '/bin/textedit --timeout-s=600 /root/f.txt' /tmp/tf.log
n=1
while [ $n -le 12 ]; do
	keys '<super-alt-a>'
	keys "item $n"
	keys '<super-alt-a>'
	keys '<super-alt-c>'
	n=$((n + 1))
done
sleep 1
want=""
n=12
while [ $n -ge 3 ]; do want="$want $(sum "item $n")"; n=$((n - 1)); done
history_is count=10 $want
shot ten.png

# Nothing failed.
errors=$(count 'ZWL ERROR|protocol error')
[ "${errors:-1}" = 0 ] && echo "no errors ok" || { echo "errors: $errors FAIL"; status=1; }
guest 'grep -E "ZWL (CLIP|DATA selection|EDIT action|FOCUS|ERROR)" /tmp/zdesktop.log' > "$out/zdesktop.log"
sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
[ $status -eq 0 ] && echo "clip-guest: PASS" || echo "clip-guest: FAIL"
exit $status
