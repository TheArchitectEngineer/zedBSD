#!/bin/sh
# ws063-p002: prints plan/tools/style-check.py's count for each file of the
# WS060/WS062/WS063 changes, now and before the changes (a tree extracted from
# the commit before them into build/style-base).
#   sh plan/ws063/tests/style-compare.sh FILE...
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
for f in "$@"; do
	now=$(python3 plan/tools/style-check.py "$f" --summary 2>&1 | tail -1)
	if [ -f "build/style-base/$f" ]; then
		base=$(python3 plan/tools/style-check.py "build/style-base/$f" --summary 2>&1 | tail -1)
	else
		base="(new file)"
	fi
	echo "$f: now: $now | base: $base"
done
