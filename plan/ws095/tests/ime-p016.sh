#!/bin/sh
# ws095-p016: the input method's language is kept for each application (the user's request of 2026-10-04), on the Venus
# guest (the lean image, build-ime-image.sh).  zdesktop --glass at 1280x800 starts /usr/libexec/keiland-ime; the test
# client ime-probe (--app-id) stands for applications.  Judged by zdesktop's log (ZWL IME app ... and ZWL IME language=)
# and the probes' logs, never the guest's console:
#  1. Probe A (app probe-a) starts with the desktop's language (direct, from=inherited); Alt+Space: Japanese.
#  2. Probe B (app probe-b) starts with the desktop's (direct, from=inherited), not A's.
#  3. B goes: the keyboard is A's again and so is Japanese (from=remembered).
#  4. Probe C of the same application (app probe-a) keeps Japanese without a new choice (no new ZWL IME app line for
#     probe-a), and "kanji" composes there (a preedit).
#  5. A and C go: the desktop's language comes back (direct, from=desktop); Alt+Space on the desktop: Japanese.
#  6. Probe D (app probe-d) starts with the desktop's language now (ja, from=inherited), and "kanji" composes there.
# PASS: the last line "ime-p016: status=0".
#
#   plan/ws095/tests/ime-guest.sh start     (the guest must be up)
#   plan/ws095/tests/ime-p016.sh [OUTDIR]   (default build/ws095-shots/p016)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws095-run}"
export GUEST_RUNTIME
out=${1:-build/ws095-shots/p016}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 0.7; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[i]me-probe|[k]eiland-ime" | awk "{print \$1}"); do kill -CONT $p 2>/dev/null; kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[i]me-probe|[k]eiland-ime" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
start_desktop='export XDG_RUNTIME_DIR=/tmp HOME=/root; rm -f /tmp/wayland-0 /root/.config/kei/ime/ja-user.dict; picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
/bin/wayland --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started'
status=0

# Counts the lines of a guest file that match a pattern (read over SSH and matched on the host, whose grep knows
# UTF-8; a Japanese pattern does not survive the guest's shell).
count() {
	guest "cat $1" > "$out/.count" 2>/dev/null
	n=$(LC_ALL=C.UTF-8 grep -cE "$2" "$out/.count")
	case "$n" in ''|*[!0-9]*) n=0;; esac
	echo "$n"
}

# Fails the run unless a log has at least N lines matching a pattern (within a few seconds).
expect_log() {
	want=${3:-1}
	tries=0
	found=0
	while [ $tries -lt 10 ]; do
		found=$(count "$1" "$2")
		[ "$found" -ge "$want" ] && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "$found" -ge "$want" ]; then
		echo "log: $2 ok ($found)"
	else
		echo "log: $2 MISSING ($found of $want)"
		status=1
	fi
}

# Fails the run if a log has a line matching a pattern.
expect_none() {
	found=$(count "$1" "$2")
	if [ "$found" -eq 0 ]; then
		echo "log: no $2 ok"
	else
		echo "log: $2 UNEXPECTED ($found)"
		status=1
	fi
}

# A picture of the screen.
shot() {
	check "$out/$1" >/dev/null
	echo "shot: $out/$1"
}

# Starts a probe (its log at $1), with more options after.
start_probe() {
	log=$1; shift
	guest "export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/ime-probe --log=$log $* > /dev/null 2>&1 </dev/null & echo \$! > $log.pid; sleep 3; echo started" >/dev/null
	expect_log "$log" 'PROBE READY'
	expect_log "$log" 'PROBE ENTER'
}

# 1. The desktop, then probe A with the desktop's language, then Japanese.
guest "$stop_all" >/dev/null
guest "$start_desktop" >/dev/null
expect_log /tmp/zdesktop.log 'ZWL IME started pid='
expect_log /tmp/zdesktop.log 'KEI-IME READY languages=2'
expect_log /tmp/zdesktop.log 'ZWL IME language=direct'
start_probe /tmp/a.log --app-id=probe-a
expect_log /tmp/zdesktop.log 'ZWL IME app key=app:probe-a language=direct from=inherited'
keys '<alt-spc>'
expect_log /tmp/zdesktop.log 'ZWL IME language=ja'

# 2. Probe B starts with the desktop's language, not A's.
start_probe /tmp/b.log --app-id=probe-b
expect_log /tmp/zdesktop.log 'ZWL IME app key=app:probe-b language=direct from=inherited'
expect_log /tmp/zdesktop.log 'ZWL IME language=direct' 2
shot b-direct.png

# 3. B goes: A's Japanese comes back.
guest 'kill $(cat /tmp/b.log.pid); sleep 2' >/dev/null
expect_log /tmp/zdesktop.log 'ZWL IME app key=app:probe-a language=ja from=remembered'
expect_log /tmp/zdesktop.log 'ZWL IME language=ja' 2
shot a-ja.png

# 4. A second window of the same application keeps Japanese and composes.
start_probe /tmp/c.log --app-id=probe-a
lines=$(count /tmp/zdesktop.log 'ZWL IME app key=app:probe-a ')
[ "$lines" -eq 2 ] && echo "same application: no new choice ok" || { echo "same application: $lines choices for probe-a, not 2 FAIL"; status=1; }
keys 'kanji'
expect_log /tmp/c.log 'preedit=かんじ '
keys '<esc>'

# 5. A and C go: the desktop's language; Alt+Space on the desktop.
guest 'kill $(cat /tmp/c.log.pid) $(cat /tmp/a.log.pid); sleep 2' >/dev/null
expect_log /tmp/zdesktop.log 'ZWL IME app key=desktop language=direct from=desktop'
expect_log /tmp/zdesktop.log 'ZWL IME language=direct' 3
keys '<alt-spc>'
expect_log /tmp/zdesktop.log 'ZWL IME language=ja' 3

# 6. Probe D starts with the desktop's Japanese.
start_probe /tmp/d.log --app-id=probe-d
expect_log /tmp/zdesktop.log 'ZWL IME app key=app:probe-d language=ja from=inherited'
keys 'kanji'
expect_log /tmp/d.log 'preedit=かんじ '
shot d-ja.png

# zdesktop saw no protocol error.
expect_none /tmp/zdesktop.log 'ZWL ERROR'
guest "grep -E 'ZWL IME|KEI-IME|ZWL ERROR' /tmp/zdesktop.log" > "$out/zdesktop-ime.txt"
guest "$stop_all" >/dev/null
echo "ime-p016: status=$status"
exit $status
