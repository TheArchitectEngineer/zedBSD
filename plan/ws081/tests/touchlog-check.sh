#!/bin/sh
# ws081-p016: checks that touchlog counts a touch screen's reports right, and runs touch-latency.py on zdesktop's
# log, on the Venus guest of the touchlog image (build-touchlog.sh BUILD image).  The touch screen is the kernel's
# test touch screen (touchinject, the USB touch screen's report path with a HID Scan Time), driven at known rates:
#  A. 60 Hz: one finger dragged for 10 seconds, a report every 16.667 ms (601 reports down, the scan time 16.67 ms
#     apart, no gap).
#  B. 90 Hz with jitter: 900 frames 11.111 ms apart, each moved by up to +-3 ms (the rate stays 90 Hz, no gap).
#  C. Three lost reports: a 60 Hz drag with three pauses of 100 ms (the finger held, nothing sent): three gaps.
#  D. zdesktop --log-frames: ten taps on the desktop and a drag; touch-latency.py gives each tap's time to the next
#     composed frame (RESULT downs=10 ...).
# Each case's SUMMARY and the checks are printed; the raw logs are kept in OUTDIR.
#   plan/ws081/tests/touchlog-check.sh [IMAGE] [OUTDIR]   (IMAGE default build/ws081-touchlog.img)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
image=${1:-build/ws081-touchlog.img}
out=${2:-build/ws081-shots/p016}
mkdir -p "$out"
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws081/touchlog-run}"
export GUEST_RUNTIME
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
put() { timeout 60 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1 </dev/null; }
status=0

# Records a verdict.
verdict() {
	if [ "$1" = ok ]; then
		echo "$2 ok"
	else
		echo "$2 FAIL"
		status=1
	fi
}

# A value KEY=... from a summary file.
value() {
	sed -n "s/.*[ ]$2=\([0-9.]*\).*/\1/p" "$1" | head -1
}

# Records a case: the touch script replayed in the background (its screen comes with it), touchlog on it for its seconds (or until the screen goes), the summary.
record() {
	name=$1; seconds=$2
	printf '%s\n' "$3" | tr '|' '\n' > "$out/$name.script"
	put "$out/$name.script" /tmp/$name.script
	guest "(timeout 60 /bin/touchinject /tmp/$name.script > /tmp/$name.replay 2>&1 </dev/null &); sleep 0.8; /usr/bin/touchlog --seconds=$seconds --raw=/tmp/$name.raw; echo replay: \$(cat /tmp/$name.replay)" > "$out/$name.sum"
	grep -E '^(SUMMARY|replay|touchlog)' "$out/$name.sum"
	guest "cat /tmp/$name.raw" > "$out/$name.raw"
}

# Tells whether a number is within a range.
within() {
	python3 -c "import sys; v=float(sys.argv[1] or 'nan'); sys.exit(0 if float(sys.argv[2]) <= v <= float(sys.argv[3]) else 1)" "$1" "$2" "$3"
}

# 0. The guest, and zdesktop with its frames logged (it reads the same touch screen as touchlog).
sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
timeout 180 sh plan/ws035/tests/zdesktop-guest.sh start "$image" >/dev/null 2>&1
sleep 20
guest 'service stop greeter >/dev/null 2>&1; export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; /bin/wayland --timeout=1200 --width=1280 --height=800 --glass --log-frames > /tmp/zdesktop.log 2>&1 </dev/null & sleep 5; echo started' >/dev/null

# A. 60 Hz, 10 seconds.
record a60 16 'size 1279 799 2 scan|wait 1500|down 1 200 400|swipe 800 0 600 16.667|up 1|wait 300'
touching=$(value "$out/a60.sum" touching); rate=$(value "$out/a60.sum" rate_hz); median=$(sed -n 's/.*scan_intervals=[0-9]* median_ms=\([0-9.]*\).*/\1/p' "$out/a60.sum"); gaps=$(value "$out/a60.sum" scan_gaps)
within "$touching" 600 602 && verdict ok "60 Hz: $touching reports down (601 sent)" || verdict no "60 Hz: $touching reports down (601 sent)"
within "$rate" 59 61 && verdict ok "60 Hz: rate $rate Hz" || verdict no "60 Hz: rate $rate Hz"
within "$median" 16.5 16.8 && verdict ok "60 Hz: scan median $median ms" || verdict no "60 Hz: scan median $median ms"
[ "$gaps" = 0 ] && verdict ok "60 Hz: no gap" || verdict no "60 Hz: gaps $gaps"

# B. 90 Hz with +-3 ms of jitter, 10 seconds.
record b90 16 'size 1279 799 2 scan|wait 1500|down 1 200 400|swipe 800 0 900 11.111 3|up 1|wait 300'
touching=$(value "$out/b90.sum" touching); rate=$(value "$out/b90.sum" rate_hz); gaps=$(value "$out/b90.sum" scan_gaps)
within "$touching" 900 902 && verdict ok "90 Hz: $touching reports down (901 sent)" || verdict no "90 Hz: $touching reports down (901 sent)"
within "$rate" 88 92 && verdict ok "90 Hz: rate $rate Hz" || verdict no "90 Hz: rate $rate Hz"
[ "$gaps" = 0 ] && verdict ok "90 Hz jittered: no gap" || verdict no "90 Hz jittered: gaps $gaps"

# C. Three pauses of 100 ms while the finger is held.
record c3 12 'size 1279 799 2 scan|wait 1500|down 1 200 400|swipe 200 0 120 16.667|wait 100|swipe 200 0 120 16.667|wait 100|swipe 200 0 120 16.667|wait 100|swipe 200 0 120 16.667|up 1|wait 300'
gaps=$(value "$out/c3.sum" scan_gaps)
[ "$gaps" = 3 ] && verdict ok "pauses: three gaps found" || verdict no "pauses: $gaps gaps found (3 made)"

# D. zdesktop's side: ten taps and a drag, then touch-latency.py.
printf 'size 1279 799 2 scan\nwait 1500\n' > "$out/taps.script"
for tap in 1 2 3 4 5 6 7 8 9 10; do
	printf 'down 1 %d 500\nwait 80\nup 1\nwait 700\n' $((200 + tap * 60)) >> "$out/taps.script"
done
printf 'down 1 300 600\nswipe 600 0 30 16.667\nup 1\nwait 1000\n' >> "$out/taps.script"
put "$out/taps.script" /tmp/taps.script
guest 'timeout 60 /bin/touchinject /tmp/taps.script; echo replay=$?' | tail -1

# E. Terminal with a long output: a fling scrolls it and the content glides on (the inertia's frames).
guest 'export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/terminal --command="seq 1 3000; sleep 600" > /tmp/term.log 2>&1 </dev/null & sleep 5; echo started' >/dev/null
printf 'size 1279 799 2 scan\nwait 3000\ndown 1 640 300\nswipe 0 250 6 16.667\nup 1\nwait 2500\n' > "$out/fling.script"
put "$out/fling.script" /tmp/fling.script
guest 'timeout 60 /bin/touchinject /tmp/fling.script; echo replay=$?' | tail -1
guest 'cat /tmp/zdesktop.log' > "$out/zdesktop.log"
python3 plan/ws081/tests/touch-latency.py "$out/zdesktop.log" > "$out/latency.txt"
tail -3 "$out/latency.txt"
downs=$(value "$out/latency.txt" downs)
[ "${downs:-0}" -ge 12 ] && verdict ok "latency: every finger put down measured ($downs)" || verdict no "latency: every finger put down measured (${downs:-0}, at least 12)"
inertia=$(value "$out/latency.txt" inertia_max_gap_ms)
[ "${inertia:-0}" -gt 0 ] && verdict ok "inertia: the glide's frames measured (longest interval $inertia ms)" || verdict no "inertia: the glide's frames measured"
errors=$(grep -c 'ZWL ERROR' "$out/zdesktop.log")
[ "$errors" = 0 ] && verdict ok "no ZWL ERROR" || verdict no "ZWL ERROR ($errors)"

sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
[ $status -eq 0 ] && echo "touchlog-check: PASS" || echo "touchlog-check: FAIL"
exit $status
