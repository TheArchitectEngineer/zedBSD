#!/bin/sh
# ws291: the still view of mview drawn copy by copy (--draws=each, the big-N bench's draws) against the same view drawn
# instanced, on the running zedBSD of the gpu-perf machine, from the host at the top of the tree.  Each view is
# keiland-shot 6 s into a FIFO run of 900 frames (composed, as measure.sh shot); the two pictures are compared
# pixel by pixel (shot-diff.py), and the one copy's with the Linux reference too.
#   usage: plan/ws291/tests/each-shot.sh OUT [COPIES...]   (default 1 100)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: each-shot.sh OUT [COPIES...]}
shift
counts=${*:-1 100}
top=$(pwd)
. plan/tools/gpu-perf/conf.sh
KEY=${KEY:-$top/plan/tmp/guest/id_ed25519}
SSHOPT="-o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
mkdir -p "$out"

# The running zedBSD (measure.sh boots it from Debian when Debian runs).
ip=$(timeout 900 sh plan/tools/gpu-perf/measure.sh zedbsd 2> "$out/zedbsd.err") || { echo "each-shot: no zedBSD"; exit 1; }

# One view: the run in the session's user, the capture 6 s in, the picture and the run's lines fetched.
view() {
	name=$1
	options=$2
	timeout 120 ssh -i "$KEY" $SSHOPT "root@$ip" "sh -s" > "$out/$name.run" 2>&1 <<EOS
[ -S /run/aat-input.sock ] || { aat-input start --width 1920 --height 1080 > /dev/null 2>&1; sleep 3; }
aat-input move-to 960 600 > /dev/null 2>&1
aat-input move-to 962 602 > /dev/null 2>&1
rm -f /tmp/each-shot.png /tmp/each-shot.log
user=\$(awk -F: '\$3 == 1000 { print \$1; exit }' /etc/passwd)
su \$user -c 'env XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 /bin/mview --token=shot --frames=900 --content=none --present=fifo $options' > /tmp/each-shot.log 2>&1 < /dev/null &
sleep 6
env XDG_RUNTIME_DIR=/run/user/1000 keiland-shot /tmp/each-shot.png > /dev/null 2>&1 < /dev/null
wait
EOS
	timeout 60 ssh -i "$KEY" $SSHOPT "root@$ip" "cat /tmp/each-shot.png" > "$out/$name.png" < /dev/null
	timeout 60 ssh -i "$KEY" $SSHOPT "root@$ip" "cat /tmp/each-shot.log" > "$out/$name.log" < /dev/null
	echo "$name: $(grep -E 'MVIEW (DONE|FAILED)' "$out/$name.log")"
}

# Each count: drawn instanced and copy by copy, then compared.
for copies in $counts; do
	view "instanced-$copies" "--copies=$copies"
	view "each-$copies" "--copies=$copies --draws=each"
	echo "each-$copies against instanced-$copies: $(python3 -I plan/tools/frame-ops/shot-diff.py "$out/instanced-$copies.png" "$out/each-$copies.png")"
	if [ -f "$SHOT_REFERENCE/linux-shot-$copies.png" ]; then
		echo "each-$copies against Linux: $(python3 -I plan/tools/frame-ops/shot-diff.py "$SHOT_REFERENCE/linux-shot-$copies.png" "$out/each-$copies.png")"
	fi
done
