#!/bin/sh
# Q1's cleanup of the work that host test scripts leave in a worktree's build/ (2026-10-06 user: "rmはQ1で実行する
# パイプラインにしてください"; subagents run no rm).  Removes build/tmp, the build/*.run.* directories that their
# fixed name no longer points at, the build/*.old.* directories, and what plan/tools/files/host-clean.sh removes.
#
#   sh plan/tools/q1-clean.sh WORKTREE
#
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
[ $# -eq 1 ] || { echo "usage: q1-clean.sh WORKTREE" >&2; exit 2; }
root=$(cd -- "$1" && pwd)
build=$root/build
[ -d "$build" ] || exit 0
rm -rf -- "$build/tmp"
for dir in "$build"/*.run.* "$build"/*/*.run.*; do
	[ -d "$dir" ] || continue
	name=${dir%.run.*}
	if [ -L "$name" ] && [ "$(readlink -- "$name")" = "$(basename -- "$dir")" ]; then
		continue
	fi
	rm -rf -- "$dir"
done
for dir in "$build"/*.old.* "$build"/*/*.old.*; do
	[ -d "$dir" ] && rm -rf -- "$dir"
done
for dir in "$build"/ws089-host/wallpaper.*; do
	[ -d "$dir" ] && rm -rf -- "$dir"
done
sh "$(dirname -- "$0")/files/host-clean.sh" "$root"
