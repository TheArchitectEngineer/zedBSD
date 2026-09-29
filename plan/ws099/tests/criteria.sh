#!/bin/sh
# ws099-p001: checks the compositor's criteria (plan/ws099/ws.md, C1..C10) on the Venus guest of the criteria image
# (plan/ws099/tests/build-criteria-image.sh), each test on a guest started afresh.  The thresholds are the variables
# below (the criteria are still the user's draft); the tests read them from the environment.
#
#   plan/ws099/tests/criteria.sh [IMAGE] [OUTDIR] [CRITERIA...]
#     IMAGE     default build/ws099-criteria.img
#     OUTDIR    default build/ws099-criteria (results.txt: one line per test; the tests' outputs and pictures)
#     CRITERIA  default all: C1 C2 C3 C4 C5 C6 C7 C8 C9 C10 (C9 runs the regression list C9_TESTS)
# C6 is the machine's (plan/ws075/tests/hdmi/measure-apps.sh on the 5330): it is listed as not run here.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."

# The thresholds (the criteria's numbers; QEMU's where the criterion's is the machine's).
export C5_WINDOWS=${C5_WINDOWS:-10}
export C5_ROUNDS=${C5_ROUNDS:-3}
export C5_FIRST_FRAME_MS=${C5_FIRST_FRAME_MS:-100}
export C5_GAP_MS=${C5_GAP_MS:-150}
export C7_MIN_CONTRAST=${C7_MIN_CONTRAST:-4.5}
export C10_SECONDS=${C10_SECONDS:-3600}
export C10_MAX_ERRORS=${C10_MAX_ERRORS:-0}
C1_CYCLES=${C1_CYCLES:-2}
# The zdesktop regressions of C9 (plan/ws035/tests/zdesktop-NAME.sh) and the output size each is written for.
C9_TESTS=${C9_TESTS:-"p052 p053 p072 p076 p126 p128 p134 p137 p138"}

image=${1:-build/ws099-criteria.img}
out=${2:-build/ws099-criteria}
[ $# -ge 2 ] && shift 2 || shift $#
criteria=${*:-"C1 C2 C3 C4 C5 C6 C7 C8 C9 C10"}
mkdir -p "$out"
results=$out/results.txt
GUEST_RUNTIME=$(pwd)/build/ws035-sq-run
export GUEST_RUNTIME

# Starts the guest afresh from the image at a size (1280x800 or 1920x1280), and waits for its boot.
start_guest() {
	sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
	if [ "$1" = 1920x1280 ]; then
		VENUS_SIZE=1920x1280 timeout 180 sh plan/ws035/tests/zdesktop-guest.sh start "$image" >/dev/null 2>&1
	else
		env -u VENUS_SIZE timeout 180 sh plan/ws035/tests/zdesktop-guest.sh start "$image" >/dev/null 2>&1
	fi
	sleep 40
}

# Runs one test on a fresh guest: CRITERION NAME SIZE COMMAND...; records its verdict, time and RESULT line.
run() {
	criterion=$1 name=$2 size=$3
	shift 3
	start_guest "$size"
	began=$(date +%s)
	if [ "$size" = 1920x1280 ]; then
		VENUS_SIZE=1920x1280 timeout 5400 "$@" > "$out/$name.log" 2>&1
	else
		env -u VENUS_SIZE timeout 5400 "$@" > "$out/$name.log" 2>&1
	fi
	code=$?
	verdict=FAIL
	[ $code -eq 0 ] && verdict=PASS
	detail=$(grep -E 'RESULT|: PASS|: FAIL|status=' "$out/$name.log" | tail -2 | tr '\n' ' ')
	echo "$criterion $name $verdict seconds=$(($(date +%s) - began)) $detail" | tee -a "$results"
}

: > "$results"
for criterion in $criteria; do
	case $criterion in
	C1)
		# Login and Log Out (black pictures fail the run), then the boot and Shut Down (ws099-p004; starts its own guest).
		run C1 p126 1280x800 sh plan/ws035/tests/zdesktop-p126.sh "$out/c1" "$C1_CYCLES" --no-black
		began=$(date +%s)
		sh plan/ws099/tests/c1-boot-shutdown.sh "$image" "$out/c1-boot" > "$out/c1-boot-shutdown.log" 2>&1
		code=$?
		verdict=FAIL
		[ $code -eq 0 ] && verdict=PASS
		echo "C1 c1-boot-shutdown $verdict seconds=$(($(date +%s) - began)) $(grep -E 'RESULT' "$out/c1-boot-shutdown.log" | tail -1)" | tee -a "$results"
		;;
	C2) run C2 c2-geometry 1920x1280 sh plan/ws099/tests/c2-geometry.sh "$out/c2" ;;
	C3) run C3 p138 1280x800 sh plan/ws035/tests/zdesktop-p138.sh "$out/c3" ;;
	C4) run C4 p137 1280x800 sh plan/ws035/tests/zdesktop-p137.sh "$out/c4" ;;
	C5) run C5 c5-transitions 1280x800 sh plan/ws099/tests/c5-transitions.sh "$out/c5" ;;
	C6) echo "C6 measure-apps NOT-RUN the machine's measure (plan/ws075/tests/hdmi/measure-apps.sh on the 5330)" | tee -a "$results" ;;
	C7) run C7 c7-contrast 1280x800 sh plan/ws099/tests/c7-contrast.sh "$out/c7" ;;
	C8) run C8 p134 1280x800 sh plan/ws035/tests/zdesktop-p134.sh "$out/c8" ;;
	C9)
		for test in $C9_TESTS; do
			size=1280x800
			[ "$test" = p128 ] && size=1920x1280
			run C9 "$test" "$size" sh "plan/ws035/tests/zdesktop-$test.sh" "$out/c9-$test"
		done
		;;
	C10) run C10 c10-soak 1280x800 sh plan/ws099/tests/c10-soak.sh "$out/c10" ;;
	*) echo "unknown criterion $criterion" ;;
	esac
done
sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
echo "criteria done: $results"
