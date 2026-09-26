#!/bin/sh
# ws070: compares plan/tools/style-check.py's findings for files before (at a
# git revision) and now, so that a change to a legacy file can be shown not
# to add findings.  A file new since the revision is counted from zero.
#
#   plan/ws070/tests/style-compare.sh REVISION FILE...
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
revision=$1
shift
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
status=0
for file in "$@"; do
	before=0
	suffix=${file##*.}
	if git cat-file -e "$revision:$file" 2>/dev/null; then
		git show "$revision:$file" > "$scratch/before.$suffix"
		before=$(python3 plan/tools/style-check.py "$scratch/before.$suffix" | wc -l)
	fi
	now=$(python3 plan/tools/style-check.py "$file" | wc -l)
	mark=ok
	if [ "$now" -gt "$before" ]; then
		mark=WORSE
		status=1
	fi
	echo "$file before=$before now=$now $mark"
done
exit $status
