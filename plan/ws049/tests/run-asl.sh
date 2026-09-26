#!/bin/sh
# WS049: compiles the ASL tests and runs each MAIN in aml-host and in acpiexec.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
#   plan/ws049/tests/run-asl.sh [NAME...]
#
# Each plan/ws049/tests/asl/NAME.asl defines \MAIN, which returns 0 when
# every check holds or the number of the first failing check.  The test is
# compiled with iasl -oa (no constant folding) into build/ws049/asl/, run
# by aml-host --main, and run by acpiexec as the reference.  A test passes
# when both return 0.  Needs acpica-tools (iasl, acpiexec) on the host.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
out="$repo/build/ws049/asl"
mkdir -p "$out"
host="$repo/build/ws049/host/aml-host"
if [ "$#" -eq 0 ]; then
	set -- $(cd "$repo/plan/ws049/tests/asl" && ls *.asl | sed 's/\.asl$//')
fi
pass=0
fail=0
for name in "$@"; do
	source="$repo/plan/ws049/tests/asl/$name.asl"
	if ! iasl -oa -vi -p "$out/$name" "$source" > "$out/$name.iasl.log" 2>&1; then
		echo "$name: iasl failed, see $out/$name.iasl.log"
		fail=$((fail + 1))
		continue
	fi
	extra=""
	if [ -f "$repo/plan/ws049/tests/asl/$name.args" ]; then
		extra=$(cat "$repo/plan/ws049/tests/asl/$name.args")
	fi
	ours=$("$host" --quiet --main $extra "$out/$name.aml" 2>&1 | tail -1 || true)
	oracle=$(acpiexec -b "evaluate MAIN" "$out/$name.aml" 2>&1 |
		sed -n 's/.*\[Integer\] = \([0-9A-F]*\).*/\1/p' | head -1)
	case "$oracle" in
	0000000000000000|00000000) oracle_result="passed" ;;
	"") oracle_result="no result" ;;
	*) oracle_result="returned 0x$oracle" ;;
	esac
	if [ "$ours" = "MAIN passed" ] && [ "$oracle_result" = "passed" ]; then
		echo "$name: passed (aml-host and acpiexec)"
		pass=$((pass + 1))
	else
		echo "$name: aml-host: $ours; acpiexec: $oracle_result"
		fail=$((fail + 1))
	fi
done
echo "asl: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
