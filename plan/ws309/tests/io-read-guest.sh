#!/bin/sh
# ws309: on QEMU's Venus guest, the compositor's reads that went to the io-file thread: the language's catalogs
# (ui.language changed while the session runs), App Home's apps.conf and its programs looked up, and the printers
# opened before READY.  The guest is started from IMAGE in OUT/runtime; WAYLAND is put in as /bin/wayland, LIBKEILAND
# as /lib/libkeiland.so (by a rename, so that a program running keeps its copy) and SETTINGS as /bin/keiland-settings;
# a compositor of its own (--testing, HOME=/tmp/p005read) runs while keiland-settings sets ui.language to 1, 0, then
# 1 and 0 at once, and the compositor's own KWL lines are collected.  The guest is ended by QEMU's monitor.  Prints
# "io-read-guest: PASS" when io-file is in KWL THREADS, every language after the start came by=io-file with error=0
# in the order asked (ja, en, then en last), Home's list came by=io-file with some apps before "KWL HOME prepared",
# and the printers were prepared open and hosted before READY.
#   usage: plan/ws309/tests/io-read-guest.sh OUT IMAGE WAYLAND LIBKEILAND SETTINGS
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: io-read-guest.sh OUT IMAGE WAYLAND LIBKEILAND SETTINGS}
image=${2:?}
wl=${3:?}
lib=${4:?}
settings=${5:?}
top=$(pwd)
mkdir -p "$out"
GUEST_RUNTIME=$top/$out/runtime
export GUEST_RUNTIME
g() { timeout "${G_TIMEOUT:-300}" python3 -I plan/tools/guest/guest.py "$@"; }
failed=0
timeout 300 sh plan/ws035/tests/zdesktop-guest.sh start "$image" > "$out/start.log" 2>&1
g wait > "$out/wait.log" 2>&1 || { echo "io-read-guest: the guest did not answer"; failed=1; }
sleep 30
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
if [ $failed = 0 ]; then
	g run "$stop_all" > /dev/null 2>&1
	g put "$wl" /bin/wayland > "$out/put.log" 2>&1 || failed=1
	g put "$lib" /tmp/libkeiland.so > "$out/put-lib.log" 2>&1 || failed=1
	g put "$settings" /bin/keiland-settings > "$out/put-settings.log" 2>&1 || failed=1
	G_TIMEOUT=180 g run 'chmod 755 /bin/wayland /bin/keiland-settings; cp /tmp/libkeiland.so /lib/libkeiland.so.new && mv /lib/libkeiland.so.new /lib/libkeiland.so; export XDG_RUNTIME_DIR=/tmp HOME=/tmp/p005read; mkdir -p $HOME; rm -f /tmp/wayland-0; /bin/wayland --testing --timeout=60000 --width=1280 --height=800 > /tmp/io-read.log 2>&1 </dev/null & sleep 6; keiland-settings --timeout-ms=10000 set ui.language 1 > /tmp/settings.out 2>&1; sleep 1; keiland-settings --timeout-ms=10000 set ui.language 0 >> /tmp/settings.out 2>&1; sleep 1; keiland-settings --timeout-ms=10000 set ui.language 1 set ui.language 0 >> /tmp/settings.out 2>&1; sleep 2; cat /tmp/settings.out; echo "== compositor"; grep -E "KWL (READY|THREADS|LANGUAGE|HOME (read|prepared|skip)|PRINTERS prepared|IO)" /tmp/io-read.log' > "$out/guest.out" 2>&1
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
cat "$out/guest.out"
python3 -I - "$out/guest.out" <<'PY'
import re, sys
text = open(sys.argv[1], errors="replace").read().splitlines()
ok = True
def fail(why):
	global ok
	ok = False
	print("io-read-guest: " + why)
def index_of(pattern):
	for number, line in enumerate(text):
		if re.search(pattern, line):
			return number
	return -1
if index_of(r"KWL THREADS .*io-file") < 0:
	fail("no io-file in KWL THREADS")
ready = index_of(r"KWL READY ")
if ready < 0:
	fail("no KWL READY")
prepared = index_of(r"KWL PRINTERS prepared open=1 hosted=1 ")
if prepared < 0:
	fail("the printers were not prepared open and hosted")
elif ready >= 0 and prepared > ready:
	fail("the printers were prepared after READY")
results = [line for line in text if line.startswith("KEILAND-SETTINGS result")]
if len(results) < 4 or any("error=0" not in line for line in results):
	fail("%d settings results, 4 with error=0 wanted" % len(results))
changes = [re.search(r"language=(\w+) from=setting error=(-?\d+) by=(\S+)", line) for line in text if line.startswith("KWL LANGUAGE")]
changes = [m for m in changes if m]
later = [m for m in changes if m.group(3) == "io-file"]
if len(later) < 2:
	fail("%d language changes by=io-file, at least 2 wanted" % len(later))
if any(m.group(2) != "0" for m in later):
	fail("a language read failed")
if later and later[0].group(1) != "ja":
	fail("the first change is not ja")
if later and later[-1].group(1) != "en":
	fail("the last change is not en")
home = index_of(r"KWL HOME read apps=([1-9]\d*) by=io-file")
if home < 0:
	fail("Home's list did not come from io-file")
home_prepared = index_of(r"KWL HOME prepared names=")
if home_prepared < 0:
	fail("Home was not prepared")
elif home >= 0 and home_prepared < home:
	fail("Home was prepared before its list came")
print("io-read-guest: " + ("PASS" if ok else "FAIL"))
PY
