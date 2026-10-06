#!/bin/sh
# ws099-p016: the time from App Home's click on an application's icon to its window's first image and the next frame
# of zdesktop, on the Venus guest (zdesktop --glass --log-frames at 1280x800; the program and libraries of BIN put in
# first when BIN is given).  For each application, ROUNDS times: Home opened with the launcher, the icon clicked, and
# from zdesktop's log the launch's wait to the window's first image (KWL HOME launched waited_ms) and to the first
# KWL COMPOSE frame after it; the application is then ended.  Prints each round, the medians, and the imports
# zdesktop made (KWL IMPORT lines).  A round whose window did not come within App Home's wait is left out of the medians.
#   GUEST_RUNTIME=... [BIN=build/amd64] [ROUNDS=3] plan/ws099/tests/import-launch.sh OUTDIR [APP...]
#     APP  an App Home name (default: Files, "Model viewer")
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=$1
shift
[ $# -gt 0 ] || set -- Files "Model viewer"
rounds=${ROUNDS:-3}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[f]iles|[m]view|[w]lshm" | awk "{print \$1}"); do kill $p; done; sleep 1'
status=0

# The program and the libraries (tried a few times: just after the boot the greeter's compositor may still hold it).
if [ -n "${BIN:-}" ]; then
	guest "$stop_all" >/dev/null
	for program in wayland files; do
		tries=0
		until timeout 120 python3 plan/tools/guest/guest.py put "$BIN/bin/$program" "/bin/$program" >/dev/null 2>&1 </dev/null; do
			tries=$((tries + 1))
			[ $tries -lt 3 ] || { echo "put $program: FAILED"; status=1; break; }
			guest "$stop_all" >/dev/null
			sleep 3
		done
	done
	for library in $(cd "$BIN/dynamic" && ls *.so | grep -vE '^(libc|ld)\.so$'); do
		timeout 120 python3 plan/tools/guest/guest.py put "$BIN/dynamic/$library" "/lib/$library" >/dev/null 2>&1 </dev/null || { echo "put $library: FAILED"; status=1; }
	done
	guest 'chmod 755 /bin/wayland /bin/files' >/dev/null
fi

# zdesktop alone, logging its frames.
guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp HOME=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass $picture --log-frames > /tmp/zdesktop.log 2>&1 </dev/null & i=0; while ! grep -q "KWL READY" /tmp/zdesktop.log && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done; sleep 3; echo started' >/dev/null

: > "$out/import-launch.txt"
for app in "$@"; do
	round=1
	while [ $round -le $rounds ]; do
		# Home, then the icon (its centre from zdesktop's log).
		pointer move 23 17 sleep 300 down sleep 60 up sleep 1500
		set -- $(guest "grep -a 'KWL HOME icon name=\"$app\"' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\).*/\1 \2/p')
		if [ $# -ne 2 ]; then
			echo "app=\"$app\" round=$round: no icon" | tee -a "$out/import-launch.txt"
			status=1
			break
		fi
		before=$(guest "grep -ac 'KWL HOME launched ' /tmp/zdesktop.log" | tail -1)
		imports=$(guest "grep -ac 'KWL IMPORT client' /tmp/zdesktop.log" | tail -1)
		pointer move "$1" "$2" sleep 300 down sleep 60 up sleep 9000
		pointer move 1250 780 sleep 300

		# The launch's lines: the wait to the first image, and the first frame after it.
		line=$(guest "grep -a 'KWL HOME launched ' /tmp/zdesktop.log | sed -n '$((before + 1))p'")
		waited=$(printf '%s\n' "$line" | sed -n 's/.* waited_ms=\([0-9]*\).*/\1/p')
		mapped=$(printf '%s\n' "$line" | sed -n 's/.* at_ms=\([0-9]*\).*/\1/p')
		frame=-1
		if [ -n "$waited" ] && [ -n "$mapped" ]; then
			composed=$(guest "grep -a 'KWL COMPOSE frame=' /tmp/zdesktop.log | sed -n 's/.* at_ms=\([0-9]*\).*/\1/p' | awk '\$1 >= $mapped' | head -1")
			[ -n "$composed" ] && frame=$((composed - mapped + waited))
		else
			waited=-1
		fi
		after=$(guest "grep -ac 'KWL IMPORT client' /tmp/zdesktop.log" | tail -1)
		echo "app=\"$app\" round=$round waited_ms=$waited frame_ms=$frame imports=$((after - imports))" | tee -a "$out/import-launch.txt"
		[ "$round" = 1 ] && python3 plan/ws035/tests/zdesktop-shot.py --runtime "$GUEST_RUNTIME" "$out/$(echo "$app" | tr ' ' '-').png" >/dev/null 2>&1

		# The application ends (its pid from the launch's line).
		pid=$(guest "grep -a 'KWL HOME launch name=$app pid=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* pid=\([0-9]*\).*/\1/p')
		[ -n "$pid" ] && guest "kill $pid" >/dev/null
		sleep 2
		round=$((round + 1))
	done
done

# The medians of each application's rounds.
python3 - "$out/import-launch.txt" <<'PY' | tee -a "$out/import-launch.txt"
import re, statistics, sys
rows = {}
for line in open(sys.argv[1]):
	match = re.match(r'app="([^"]+)" round=\d+ waited_ms=(-?\d+) frame_ms=(-?\d+)', line)
	if match:
		rows.setdefault(match.group(1), []).append((int(match.group(2)), int(match.group(3))))
for app, values in rows.items():
	measured = [v for v in values if v[0] >= 0 and v[1] >= 0]
	waits = [v[0] for v in measured] or [-1]
	frames = [v[1] for v in measured] or [-1]
	print('RESULT app="%s" waited_ms=%d frame_ms=%d rounds=%d measured=%d' % (app, statistics.median(waits), statistics.median(frames), len(values), len(measured)))
PY
exit $status
