#!/bin/sh
# ws291: one round of the RX 5600 XT machine's checks of a kernel, from the host at the top of the tree: the kernel put
# in (measure.sh deploy), the draw shapes (nggshape), mview's still views of 1 and 6500 copies against Linux's, the
# bench list's runs, and the hang check; each step's output in OUT/<step>.out, the summary printed.
#   usage: plan/ws291/tests/round.sh OUT VMUNIX [STEP...]   (steps: deploy nggshape shot bench hang; default all)
# The programs copied in come from USB_MVIEW, USB_NGGSHAPE and USB_VKHANG (see measure.sh).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: round.sh OUT VMUNIX [STEP...]}
kernel=${2:?usage: round.sh OUT VMUNIX [STEP...]}
shift 2
steps=${*:-deploy nggshape shot bench hang}
m=plan/ws291/tests/measure.sh
mkdir -p "$out"
# The steps run under the power guard (guard.sh): a hung firmware, a thermal fault or a hot GPU stops the load and
# powers zedBSD off.
if [ -z "${ROUND_GUARDED:-}" ]; then
	ROUND_GUARDED=1 exec sh plan/ws291/tests/guard.sh "$out/round" sh "$0" "$out" "$kernel" $steps
fi
for step in $steps; do
	echo "== $step $(date +%H:%M:%S)"
	case $step in
	deploy)
		timeout 1500 sh "$m" deploy --kernel "$kernel" > "$out/deploy.out" 2>&1 || { tail -3 "$out/deploy.out"; echo "deploy failed"; exit 1; }
		tail -2 "$out/deploy.out" ;;
	nggshape)
		timeout 900 sh "$m" nggshape "$out/nggshape" > "$out/nggshape.out" 2>&1
		cut -c1-160 "$out/nggshape.out" ;;
	shot)
		for copies in 1 6500; do
			timeout 900 sh "$m" shot "$out/shot-$copies" "$copies" > "$out/shot-$copies.out" 2>&1
			echo "copies $copies:"; tail -4 "$out/shot-$copies.out"
		done ;;
	bench)
		timeout 3600 sh "$m" bench zedbsd "$out/bench" > "$out/bench.out" 2>&1
		cat "$out/bench/table.txt" 2>/dev/null || tail -5 "$out/bench.out" ;;
	hang)
		timeout 900 sh "$m" hang "$out/hang" > "$out/hang.out" 2>&1
		cut -c1-160 "$out/hang.out" | tail -12 ;;
	*)
		echo "no step $step" ;;
	esac
done
echo "== end $(date +%H:%M:%S)"
