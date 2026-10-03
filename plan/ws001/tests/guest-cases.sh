#!/bin/sh
# ws001: runs on the guest the utility cases that
# plan/tools/utils/util-diff.py --export wrote, and compares each with the
# GNU reference recorded on the host (the same comparison as
# plan/tools/sh/guest-diff.sh).  The work directories are on the root file
# system, not on /tmp, because tmpfs runs out of files (BUG-029).
#   sh guest-cases.sh CASES_DIR [WORK_BASE]
# Each case is GG/NNNN.sh (the code) and GG/NNNN.exp (the status, the name,
# then the output).  Prints FAIL and the name for each difference, then
# PASS passed/total.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
cases=$1
work_base=${2:-/root/ws001-work}
mkdir -p "$work_base"
total=0
passed=0
for code in "$cases"/*/*.sh; do
	case=${code%.sh}
	total=$((total + 1))
	work=$work_base/case.$total
	mkdir -p "$work"
	cd "$work" || exit 1
	cp "$code" "$work/../case.sh"
	env -i PATH=/bin:/usr/bin HOME="$work" LC_ALL=C TZ=UTC \
	    timeout 10 /bin/sh "$work/../case.sh" > "$work_base/out" 2>/dev/null </dev/null
	status=$?
	cd /
	chmod -R u+w "$work" 2>/dev/null
	rm -rf "$work"
	{ read expected; read name; cat > "$work_base/want"; } < "$case.exp"
	if cmp -s "$work_base/out" "$work_base/want" && [ "$status" = "$expected" ]; then
		passed=$((passed + 1))
	else
		echo "FAIL $name (status $status, expected $expected)"
		if [ -n "${DUMP-}" ]; then
			echo "--- got"; cat "$work_base/out"
			echo "--- want"; cat "$work_base/want"
		fi
	fi
done
echo "PASS $passed/$total"
