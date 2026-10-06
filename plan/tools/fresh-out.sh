# Sourced by host test scripts: fresh_out NAME makes a new directory NAME.run.XXXXXX and points NAME (a symbolic link)
# at it, so a run starts empty without removing the last one (2026-10-06 user: deleting is Q1's step).  A NAME that is
# still a real directory from before is moved aside to NAME.old.PID.  plan/tools/q1-clean.sh (Q1) removes the runs
# NAME does not point at and the moved-aside directories.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
fresh_out() {
	fresh_name=$1
	mkdir -p "$(dirname -- "$fresh_name")"
	if [ -d "$fresh_name" ] && [ ! -L "$fresh_name" ]; then
		mv -- "$fresh_name" "$fresh_name.old.$$"
	fi
	fresh_dir=$(mktemp -d "$fresh_name.run.XXXXXX")
	ln -sfn -- "$(basename -- "$fresh_dir")" "$fresh_name"
}
