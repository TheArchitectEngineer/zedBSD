#!/bin/sh
# ws309-p017: the WS309 QEMU (Venus) regression on one image built from the tree
# (plan/tools/guest/test-image.sh plan/ws309/tests/config-amd64-p1.mk BUILD --file ...): each of the WS309 guest tests
# in turn with the image's own programs (BUILD/bin/wayland, BUILD/dynamic/libkeiland.so and libc.so, keiland-settings,
# titlebar-probe; keiland-system made with `make ZEDBSD_CONFIG=... BUILD=BUILD BUILD/bin/keiland-system` when missing),
# then the GPU boundary's forge and fence tests, then the threads' and passes' lines of a compositor run.  A test that
# did not get an answer from the guest (SSH) is run once more after RETRY_WAIT seconds (the host's load, 2026-10-30
# user via Q1).  OUT/summary.txt has one line per test (PASS, FAIL, or NOANSWER) and the last line is
# "regress-guest: PASS" when every test passed.
#   usage: [WAYLAND=PATH] plan/ws309/tests/regress-guest.sh OUT BUILD [TEST...]   (WAYLAND: another compositor put in)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: regress-guest.sh OUT BUILD [TEST...]}
build=${2:?}
shift 2
tests=${*:-"grave resize hand scanout rotate task io-device io-device2 io-file io-read daemon font spawn-closefrom spawn forge fence"}
retry_wait=${RETRY_WAIT:-600}
image=$build/hdd-image.img
wl=${WAYLAND:-$build/bin/wayland}
lib=$build/dynamic/libkeiland.so
libc=$build/dynamic/libc.so
mkdir -p "$out"
: > "$out/summary.txt"

# One test: its command's output in OUT/NAME.out, its last "NAME: PASS|FAIL" line read.
run_one() {
	name=$1
	shift
	timeout 1500 "$@" > "$out/$name.out" 2>&1
	if grep -q ": PASS" "$out/$name.out"; then
		echo PASS
	elif grep -qi "did not answer\|no answer\|SSH" "$out/$name.out" && ! grep -q ": FAIL" "$out/$name.out"; then
		echo NOANSWER
	else
		echo FAIL
	fi
}

for name in $tests; do
	t=$out/$name
	case $name in
	grave|resize|hand|scanout|rotate|task|io-device)
		set -- sh plan/ws309/tests/$name-guest.sh "$t" "$image" "$wl" ;;
	io-device2|daemon)
		set -- sh plan/ws309/tests/$name-guest.sh "$t" "$image" "$wl" "$lib" "$build/bin/keiland-system" ;;
	font)
		set -- sh plan/ws309/tests/font-guest.sh "$t" "$image" "$wl" "$lib" "$build/bin/titlebar-probe" ;;
	io-file)
		set -- sh plan/ws309/tests/io-file-guest.sh "$t" "$image" "$wl" "$build/bin/keiland-system" ;;
	io-read)
		set -- sh plan/ws309/tests/io-read-guest.sh "$t" "$image" "$wl" "$lib" "$build/bin/keiland-settings" "$build/bin/keiland-system" ;;
	spawn)
		set -- sh plan/ws309/tests/spawn-guest.sh "$t" "$image" "$wl" "$out/spawn-closefrom/spawn-closefrom" "$libc" ;;
	spawn-closefrom)
		set -- sh plan/ws309/tests/spawn-closefrom/run.sh "$t" "$image" ;;
	forge|fence)
		set -- sh -c "export GUEST_RUNTIME=\$(pwd)/$t-run; timeout 300 sh plan/ws035/tests/zdesktop-guest.sh start '$image' > '$t-start.log' 2>&1; timeout 300 python3 -I plan/tools/guest/guest.py wait > '$t-wait.log' 2>&1; sh plan/tools/gpu-boundary/$name-guest.sh '$t'; timeout 60 python3 -I plan/tools/guest/guest.py stop > /dev/null 2>&1" ;;
	*)
		echo "SKIP $name (unknown)" >> "$out/summary.txt"
		continue ;;
	esac
	result=$(run_one "$name" "$@")
	if [ "$result" = NOANSWER ]; then
		sleep "$retry_wait"
		result=$(run_one "$name" "$@")
	fi
	echo "$result $name" | tee -a "$out/summary.txt"
done

# Every test passed.
if grep -qv '^PASS ' "$out/summary.txt"; then
	echo "regress-guest: FAIL" | tee -a "$out/summary.txt"
	exit 1
fi
echo "regress-guest: PASS" | tee -a "$out/summary.txt"
