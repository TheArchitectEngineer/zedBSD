#!/bin/sh
# ws309: on QEMU's Venus guest, the font thread: the fallback and the emoji fonts read by it from the look's start, a
# title's Japanese characters drawn from the fallback font and its emoji decoded by it.  The guest is started from IMAGE
# in OUT/runtime; WAYLAND is put in as /bin/wayland, LIBKEILAND as /lib/libkeiland.so (by a rename) and PROBE as
# /bin/titlebar-probe (built from the same tree as the library); a compositor of
# its own (--testing --glass) shows /bin/titlebar-probe with a title of Japanese and an emoji, the screen is captured
# (OUT/title.png, for the eye) and the compositor's KWL lines are collected.  The guest is ended by QEMU's monitor.
# Prints "font-guest: PASS" when font is in KWL THREADS, both fonts were opened (errno=0, the fallback's faces= line),
# U+65E5 was drawn from a face other than the first, U+1F600 was asked of the font thread (color=1 pending=1) and
# none of its colour glyphs failed.
#   usage: plan/ws309/tests/font-guest.sh OUT IMAGE WAYLAND LIBKEILAND PROBE
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: font-guest.sh OUT IMAGE WAYLAND LIBKEILAND PROBE}
image=${2:?}
wl=${3:?}
lib=${4:?}
probe=${5:?}
top=$(pwd)
mkdir -p "$out"
GUEST_RUNTIME=$top/$out/runtime
export GUEST_RUNTIME
g() { timeout "${G_TIMEOUT:-300}" python3 -I plan/tools/guest/guest.py "$@"; }
failed=0
timeout 300 sh plan/ws035/tests/zdesktop-guest.sh start "$image" > "$out/start.log" 2>&1
g wait > "$out/wait.log" 2>&1 || { echo "font-guest: the guest did not answer"; failed=1; }
sleep 30
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[t]itlebar-probe" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[t]itlebar-probe" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
if [ $failed = 0 ]; then
	g run "$stop_all" > /dev/null 2>&1
	g put "$wl" /bin/wayland > "$out/put.log" 2>&1 || failed=1
	g put "$lib" /tmp/libkeiland.so > "$out/put-lib.log" 2>&1 || failed=1
	g put "$probe" /bin/titlebar-probe > "$out/put-probe.log" 2>&1 || failed=1
	G_TIMEOUT=120 g run 'chmod 755 /bin/wayland /bin/titlebar-probe; cp /tmp/libkeiland.so /lib/libkeiland.so.new && mv /lib/libkeiland.so.new /lib/libkeiland.so; export XDG_RUNTIME_DIR=/tmp HOME=/tmp/p007dev; mkdir -p $HOME; rm -f /tmp/wayland-0; /bin/wayland --testing --glass --timeout=300 --width=1280 --height=800 > /tmp/font.log 2>&1 </dev/null & sleep 5; /bin/titlebar-probe --show="日本語のタイトル 😀 試験" --seconds=60 > /tmp/probe.log 2>&1 </dev/null & sleep 6; echo started' > "$out/guest-start.out" 2>&1
	python3 -I plan/ws035/tests/zdesktop-check.py "$out/title.png" --runtime "$GUEST_RUNTIME" > "$out/check.log" 2>&1
	G_TIMEOUT=60 g run 'echo "== compositor"; grep -E "KWL (READY|THREADS|GLASS (worker|fallback font|no fallback font|emoji font|glyph codepoint=U\+(65E5|1F600))|FAILED)" /tmp/font.log; echo "== probe"; grep TITLEBARPROBE /tmp/probe.log | head -3' > "$out/guest.out" 2>&1
	g run "$stop_all" > /dev/null 2>&1
fi
python3 -I - "$GUEST_RUNTIME/qmp.sock" <<'PY' > "$out/quit.log" 2>&1
import socket, sys
s = socket.socket(socket.AF_UNIX)
s.settimeout(10)
s.connect(sys.argv[1])
s.recv(4096)
s.sendall(b'{"execute":"qmp_capabilities"}\n')
s.recv(4096)
s.sendall(b'{"execute":"quit"}\n')
print(s.recv(4096))
PY
cat "$out/guest-start.out" "$out/guest.out"
python3 -I - "$out/guest.out" <<'PY'
import re, sys
text = open(sys.argv[1], errors="replace").read().splitlines()
ok = True
def fail(why):
	global ok
	ok = False
	print("font-guest: " + why)
if not any(re.search(r"KWL THREADS .*\bfont\b", line) for line in text):
	fail("no font in KWL THREADS")
if not any(re.match(r"KWL GLASS fallback font: path=\S+ faces=\d+", line) for line in text):
	fail("the fallback font was not opened")
if not any(re.match(r"KWL GLASS emoji font: path=\S+ errno=0", line) for line in text):
	fail("the emoji font was not opened")
if not any(re.match(r"KWL GLASS glyph codepoint=U\+65E5 size=\d+ face=[12] ", line) for line in text):
	fail("U+65E5 was not drawn from the fallback font")
if not any(re.match(r"KWL GLASS glyph codepoint=U\+1F600 size=\d+ face=emoji cell=\d+ color=1 pending=1", line) for line in text):
	fail("U+1F600 was not asked of the font thread")
if any(re.match(r"KWL GLASS glyph codepoint=U\+1F600 cell=\d+ color=1 errno=", line) for line in text):
	fail("U+1F600 could not be decoded")
print("font-guest: " + ("PASS" if ok else "FAIL"))
PY
