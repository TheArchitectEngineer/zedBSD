#!/bin/sh
# ws045: runs the GNU cases util-diff.py --gnu --export wrote, on the guest,
# with the utilities of build-guest-utils.sh first on PATH, and compares
# each with GNU's output recorded on the host.  (The same as
# plan/tools/sh/guest-diff.sh, with the utilities' directory on PATH and
# the environment of util-diff.py.)
#   [DUMP=1] guest-diff.sh CASES_DIR BIN_DIR
# Each case is GG/NNNN.sh (the code) and GG/NNNN.exp (the status, the name,
# then the output).  Prints FAIL and the name for each difference, then PASS.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
cases=$1
bin=$2
work_base=/tmp/ws045-work.$$
total=0
passed=0
# POSIXLY_CORRECT, when it is set, goes into each case's environment too.
posix=
if [ -n "${POSIXLY_CORRECT-}" ]; then
	posix=POSIXLY_CORRECT=1
fi
for code in "$cases"/*/*.sh; do
	case=${code%.sh}
	total=$((total + 1))
	# A directory of its own for each case.
	work=$work_base.$total
	mkdir -p "$work"
	cd "$work" || exit 1
	env -i PATH="$bin:/bin:/usr/bin" HOME="$work" LC_ALL=C TZ=UTC $posix \
	    timeout 10 /bin/sh "$code" > /tmp/ws045-out 2>/dev/null </dev/null
	status=$?
	cd /
	rm -rf "$work"
	{ read expected; read name; cat > /tmp/ws045-want; } < "$case.exp"
	if cmp -s /tmp/ws045-out /tmp/ws045-want && [ "$status" = "$expected" ]; then
		passed=$((passed + 1))
	else
		echo "FAIL $name (status $status, expected $expected)"
		if [ -n "${DUMP-}" ]; then
			echo "--- got"; cat /tmp/ws045-out
			echo "--- want"; cat /tmp/ws045-want
		fi
	fi
done
echo "PASS $passed/$total"
