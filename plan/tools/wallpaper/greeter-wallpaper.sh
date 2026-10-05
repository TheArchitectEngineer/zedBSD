#!/bin/sh
# ws138-p002: the greeter shows the default wallpaper, now a PNG (/usr/share/keiland/wallpaper.png, sessiond.h
# SESSIOND_WALLPAPER).  The criteria image logs kei in at boot (/etc/keiland/autologin), so the greeter is never on
# the screen there; this run empties the autologin file (restored at the end), starts sessiond --graphical as
# plan/ws035/tests/zdesktop-p095.sh does, and checks:
#  1. /var/log/greeter.log has "ZWL STARTUP step=wallpaper" and no "ZWL GLASS no wallpaper";
#  2. greeter.png: the login screen over the birch-and-lake picture (looked at by eye).
# sessiond and the greeter are stopped afterwards and the autologin file put back.
#
#   plan/ws035/tests/zdesktop-guest.sh start BUILD/hdd-image.img     (the criteria image: login=graphical, a greeter)
#   plan/tools/wallpaper/greeter-wallpaper.sh [OUTDIR]                    (default build/wallpaper/greeter)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws035-sq-run}"
export GUEST_RUNTIME
out=${1:-build/wallpaper/greeter}
mkdir -p "$out"
status=0
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[s]essiond|[w]ayland( |$)" | awk "{print \$1}"); do kill $p; done; sleep 2'

# Nothing of the desktop running, a clean greeter log, no autologin.
guest "$stop_all
rm -f /var/log/greeter.log
[ -f /tmp/wallpaper-autologin.saved ] || cp /etc/keiland/autologin /tmp/wallpaper-autologin.saved 2>/dev/null; : > /etc/keiland/autologin; echo ready" >/dev/null

# The greeter.
guest "/sbin/sessiond --graphical </dev/null >/dev/null 2>&1 & sleep 1; echo started" >/dev/null
tries=0
while [ $tries -lt 30 ]; do
	opened=$(guest "grep -c 'ZWL GREETER open' /var/log/greeter.log" | tail -1)
	[ "${opened:-0}" -gt 0 ] 2>/dev/null && break
	tries=$((tries + 1))
	sleep 1
done
sleep 3
guest "grep -E 'ZWL STARTUP step=wallpaper|ZWL GLASS no wallpaper|ZWL GREETER open' /var/log/greeter.log" > "$out/greeter-log.txt"
cat "$out/greeter-log.txt"
grep -q 'ZWL GREETER open' "$out/greeter-log.txt" || { echo "greeter: did not open"; status=1; }
grep -q 'ZWL STARTUP step=wallpaper ' "$out/greeter-log.txt" || { echo "greeter: no wallpaper step"; status=1; }
if grep -q 'ZWL GLASS no wallpaper' "$out/greeter-log.txt"; then
	echo "greeter: the wallpaper was not read"
	status=1
fi
check "$out/greeter.png" >/dev/null
echo "picture: $out/greeter.png (the login card over the birch-and-lake picture)"

# Back as it was.
guest "$stop_all
[ -f /tmp/wallpaper-autologin.saved ] && cat /tmp/wallpaper-autologin.saved > /etc/keiland/autologin; echo restored" >/dev/null

if [ $status = 0 ]; then
	echo "greeter-wallpaper: PASS (look at the picture)"
else
	echo "greeter-wallpaper: FAIL"
fi
exit $status
