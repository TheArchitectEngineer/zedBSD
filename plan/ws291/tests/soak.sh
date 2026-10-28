#!/bin/sh
# ws291: the power firmware's soak on the RX 5600 XT machine, from the host at the top of the tree, under the power
# guard (guard.sh): for MINUTES, mview's scene of COPIES copies for LOAD seconds (game mode, mailbox), then IDLE seconds
# with nothing running, again and again; each round's time and the guard's power line go to OUT/soak.log.  Prints the
# rounds and whether the guard tripped (it powers zedBSD off and stops the soak).
#   usage: plan/ws291/tests/soak.sh OUT [MINUTES [LOAD [IDLE [COPIES]]]]   (default 25 5 25 1500)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: soak.sh OUT [MINUTES [LOAD [IDLE [COPIES]]]]}
minutes=${2:-25}
load=${3:-5}
idle=${4:-25}
copies=${5:-1500}
m=plan/ws291/tests/measure.sh
mkdir -p "$out"
if [ -z "${SOAK_GUARDED:-}" ]; then
	SOAK_GUARDED=1 exec sh plan/ws291/tests/guard.sh "$out/soak" sh "$0" "$out" "$minutes" "$load" "$idle" "$copies"
fi
end=$(($(date +%s) + minutes * 60))
round=0
: > "$out/soak.log"
while [ "$(date +%s)" -lt $end ]; do
	round=$((round + 1))
	timeout 300 sh "$m" mview "$out/round" --spin="$load" --content=game --present=mailbox --images=4 --copies="$copies" > "$out/round.out" 2>&1
	fps=$(sed -n 's/.*MVIEW SPIN.* fps=\([0-9.]*\).*/\1/p' "$out/round.out" | tail -1)
	echo "$(date +%H:%M:%S) round $round fps ${fps:-none}" >> "$out/soak.log"
	sleep "$idle"
done
echo "soak: $round rounds" | tee -a "$out/soak.log"
