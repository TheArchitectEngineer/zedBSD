#!/bin/sh
# ws309-p016: the compositor under logind in the Debian 13 gdm guest (plan/tools/keiland-linux, WS105): the seat's
# callbacks answered without waiting (kl_backend_seat_device_let_go, kl_backend_seat_paused_done) and the inputs'
# TakeDevice answered on the bus.  The guest GUEST_DIR (build-guest.sh ... gdm) is started with its own overlay in
# OUT/run, STAGE (make keiland-linux-install keiland-linux-install-session DESTDIR=STAGE) is installed, gdm logs kei in
# to Keiland; then a uinput keyboard (UINPUT, uinput-key.c built static) comes, types and goes, and the seat is paused
# and resumed twice (logind's SwitchTo, then chvt).  The compositor's lines come from the guest's journal.
# Prints "seat-guest: PASS" when: the seat is logind's; the inputs were found (2 or more KWL INPUT lines before the
# keyboard); the keyboard was found and then gone (KWL SEAT pause ... type=gone); every "KWL SEAT paused" was followed
# by "KWL SEAT paused_done" and the seat resumed twice; the compositor's pid is the same after; no KWL FAILED.
# The PNGs (session, vt, back, back2) are for a person to look at.
#   usage: plan/ws309/tests/seat-linux/seat-guest.sh OUT GUEST_DIR STAGE UINPUT
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: seat-guest.sh OUT GUEST_DIR STAGE UINPUT}
guest_dir=${2:?}
stage=${3:?}
uinput=${4:?}
top=$(pwd)
mkdir -p "$out"
out=$(cd "$out" && pwd)
GUEST_DIR=$(cd "$guest_dir" && pwd)
GUEST_RUN=$out/run
SSH_PORT=${SSH_PORT:-2327}
export GUEST_DIR GUEST_RUN SSH_PORT
g() { timeout "${G_TIMEOUT:-150}" sh "$top/plan/tools/keiland-linux/guest.sh" "$@"; }
failed=0

G_TIMEOUT=250 g start > "$out/start.log" 2>&1 || failed=1
if [ $failed = 0 ]; then
	timeout 240 sh plan/tools/keiland-linux/install-guest.sh "$stage" > "$out/install.log" 2>&1 || failed=1
	g put "$uinput" /tmp/uinput-key >> "$out/install.log" 2>&1 || failed=1
	g ssh 'chmod 755 /tmp/uinput-key; modprobe uinput' >> "$out/install.log" 2>&1 || failed=1
	g gdm-setup > "$out/gdm.log" 2>&1 || failed=1
fi
if [ $failed = 0 ]; then
	sleep 45
	g screenshot "$out/session.png" > /dev/null 2>&1
	g ssh 'pidof wayland' > "$out/pid1.txt" 2>&1
	g ssh 'loginctl show-session $(loginctl list-sessions --no-legend | awk "\$3==\"kei\"{print \$1}") -p VTNr --value' > "$out/vt.txt" 2>&1
	vt=$(head -n 1 "$out/vt.txt" | tr -dc 0-9)
	[ -n "$vt" ] || vt=1
	g ssh 'journalctl -b -o cat --no-pager | grep -c "^KWL INPUT device="' > "$out/inputs-before.txt" 2>&1

	# The keyboard comes, types three times and goes.
	G_TIMEOUT=60 g ssh '/tmp/uinput-key 5 3' > "$out/uinput.log" 2>&1
	sleep 3

	# logind's pause (SwitchTo) and the way back, then the forced one (chvt).
	g ssh 'busctl call org.freedesktop.login1 /org/freedesktop/login1/seat/seat0 org.freedesktop.login1.Seat SwitchTo u 3' > "$out/vt-switch.log" 2>&1
	sleep 6
	g screenshot "$out/vt.png" > /dev/null 2>&1
	g ssh "busctl call org.freedesktop.login1 /org/freedesktop/login1/seat/seat0 org.freedesktop.login1.Seat SwitchTo u $vt" >> "$out/vt-switch.log" 2>&1
	sleep 8
	g screenshot "$out/back.png" > /dev/null 2>&1
	g ssh 'chvt 3' >> "$out/vt-switch.log" 2>&1
	sleep 6
	g ssh "chvt $vt" >> "$out/vt-switch.log" 2>&1
	sleep 8
	g screenshot "$out/back2.png" > /dev/null 2>&1
	g key a > /dev/null 2>&1
	sleep 2
	g ssh 'pidof wayland' > "$out/pid2.txt" 2>&1
	G_TIMEOUT=60 g ssh 'journalctl -b -o cat --no-pager' > "$out/journal.txt" 2>&1
fi
G_TIMEOUT=90 g stop > "$out/stop.log" 2>&1

python3 -I - "$out" "$failed" <<'PY'
import re, sys
out = sys.argv[1]
ok = sys.argv[2] == "0"
def read(name):
	try:
		return open(out + "/" + name, errors="replace").read()
	except OSError:
		return ""
journal = read("journal.txt")
lines = [l for l in journal.splitlines() if l.startswith("KWL ")]
open(out + "/kwl.txt", "w").write("\n".join(lines) + "\n")
def check(passed, what):
	global ok
	print(("ok: " if passed else "FAIL: ") + what)
	if not passed:
		ok = False
check(any(l.startswith("KWL SEAT logind session=") for l in lines), "the seat is logind's")
before = read("inputs-before.txt").strip()
inputs = [l for l in lines if l.startswith("KWL INPUT device=")]
check(before.isdigit() and int(before) >= 2, "inputs found through TakeDevice's answers before the keyboard (%s)" % before)
check(before.isdigit() and len(inputs) > int(before), "the uinput keyboard found (%d KWL INPUT lines in all)" % len(inputs))
gone = [l for l in lines if l.startswith("KWL SEAT pause ") and "type=gone" in l]
check(len(gone) >= 1, "the keyboard gone (%d)" % len(gone))
paused = [i for i, l in enumerate(lines) if l == "KWL SEAT paused"]
done = [i for i, l in enumerate(lines) if l == "KWL SEAT paused_done"]
resumed = [l for l in lines if l == "KWL SEAT resumed"]
check(len(paused) >= 2 and all(any(d > p for d in done) for p in paused), "every pause answered with paused_done (%d paused, %d done)" % (len(paused), len(done)))
check(len(resumed) >= 2, "resumed twice (%d)" % len(resumed))
pid1 = read("pid1.txt").strip()
pid2 = read("pid2.txt").strip()
check(pid1 != "" and pid1 == pid2, "the same compositor after (%s, %s)" % (pid1, pid2))
failed = [l for l in lines if l.startswith("KWL FAILED")]
check(not failed, "no KWL FAILED (%s)" % (failed[:1],))
print("seat-guest: " + ("PASS" if ok else "FAIL"))
PY
