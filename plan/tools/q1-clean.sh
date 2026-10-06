#!/bin/sh
# Q1's cleanup of the work that host test scripts leave in a worktree's build/ (2026-10-06 user: "rmはQ1で実行する
# パイプラインにしてください"; subagents run no rm).  Removes build/tmp, the build/*.run.* directories that their
# fixed name no longer points at, the build/*.old.* directories, and what plan/tools/files/host-clean.sh removes.
#
#   sh plan/tools/q1-clean.sh WORKTREE
#   sh plan/tools/q1-clean.sh --tmp     (boot-test.sh's work directories under ${TMPDIR:-/tmp} that no QEMU uses)
#
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
if [ "${1:-}" = --tmp ]; then
	for dir in "${TMPDIR:-/tmp}"/boot-test.*; do
		[ -d "$dir" ] || continue
		if pgrep -f -- "$dir" >/dev/null 2>&1; then
			continue
		fi
		rm -rf -- "$dir"
	done
	for dir in /dev/shm/zedbsd-host-model.*; do
		[ -d "$dir" ] && rm -rf -- "$dir"
	done
	exit 0
fi
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
for dir in "$build"/ws089-host/wallpaper.* "$build"/ws071-host/volume.* "$build"/ws131-p020/model-*; do
	[ -d "$dir" ] && rm -rf -- "$dir"
done
sh "$(dirname -- "$0")/files/host-clean.sh" "$root"
