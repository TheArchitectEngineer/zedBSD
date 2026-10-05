#!/bin/sh
# ws122-p004: the player with its own container reader and the libavcodec add-in (dlopen, no FFmpeg headers, not
# linked to FFmpeg), on the guest and image of videoplayer-p002.sh (config-amd64-p002.mk with sample.mp4), built from
# the commit under test.
#  1. videoplayer-p002.sh passes as before (open, play, pause, seek, end, close, the sound).
#  2. The add-in loaded the image's FFmpeg 9.0.2: "CODEC load error=0 major=63", and the reader is the player's own:
#     "OPEN ... container=mp4".  The program has no NEEDED entry of FFmpeg (readelf is not on the guest, so the
#     player's dynamic section is checked on the host by the requester; here the log is the evidence).
#  3. Without libavcodec (its library moved aside on the guest): the open fails with "NOTICE problem=1 text=Playing
#     video needs FFmpeg's libavcodec, which is not installed." and the window says so (missing.png); the library is
#     put back.
# PASS: the last line "videoplayer-p004: PASS".
#   plan/ws122/tests/videoplayer-guest.sh start IMAGE    (the guest must be up, this image)
#   plan/ws122/tests/videoplayer-p004.sh [OUTDIR]        (default build/ws122-p004)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws122-run}"
export GUEST_RUNTIME
out=${1:-build/ws122-p004}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[v]ideoplayer" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[v]ideoplayer" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
status=0

# 1. The p002 test.
if sh plan/ws122/tests/videoplayer-p002.sh "$out/p002" > "$out/p002.txt" 2>&1 && tail -1 "$out/p002.txt" | grep -q PASS; then
	echo "p002: ok"
else
	echo "p002: FAILED"; tail -20 "$out/p002.txt"; status=1
fi

# 2. The add-in and the reader (from the p002 run's log).
grep -q 'CODEC load error=0 major=63' "$out/p002/v.log" && echo "add-in: ok (major 63)" || { echo "add-in: FAILED"; status=1; }
grep -q 'OPEN path=.* container=mp4' "$out/p002/v.log" && echo "reader: ok" || { echo "reader: FAILED"; status=1; }

# 3. Without libavcodec.
guest "$stop_all" >/dev/null
guest 'mv /usr/lib/libavcodec.so.63 /usr/lib/libavcodec.so.63.aside; export XDG_RUNTIME_DIR=/tmp
rm -f /tmp/wayland-0; /bin/wayland --testing --timeout=300 --width=1280 --height=800 --glass > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4
/bin/videoplayer --timeout-s=60 /usr/share/videoplayer-tests/sample.mp4 > /tmp/v.log 2>&1 </dev/null & sleep 5; echo started' >/dev/null
found=$(guest "grep -c \"NOTICE problem=1 text=Playing video needs FFmpeg's libavcodec, which is not installed.\" /tmp/v.log" | tail -1)
[ "${found:-0}" -gt 0 ] 2>/dev/null && echo "missing: ok" || { echo "missing: FAILED"; guest 'cat /tmp/v.log' | tail -5; status=1; }
pointer move 1270 790 sleep 500 >/dev/null
check "$out/missing.png" >/dev/null
guest "$stop_all; mv /usr/lib/libavcodec.so.63.aside /usr/lib/libavcodec.so.63; ls /usr/lib/libavcodec.so.63" | tail -1

[ $status = 0 ] && echo "videoplayer-p004: PASS" || echo "videoplayer-p004: FAIL"
exit $status
