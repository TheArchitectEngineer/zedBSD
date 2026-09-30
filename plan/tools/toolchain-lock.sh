#!/bin/sh
# Locks or unlocks the shared toolchain trees of the main checkout, so that nothing but the main agent
# (with the lock taken off on purpose) can write into them.  Directories are made read-only, files are left
# alone: a package build that hard-links the LLVM source into its own copy (cp -al) and patches the copy still
# works, because the copy's directories are its own (cp -a copies the read-only mode, so the package rule makes
# them writable, BUG-126), but nothing can create, rename or delete files inside
# the shared trees, which is what a patch or an install into them would do (the 2026-09-28 incident).
#
#   plan/tools/toolchain-lock.sh lock|unlock|status
#
# Trees: build/llvm (the installed toolchain, followed through its link), build/llvm-source,
# build/llvm-build, build/NoctLang.  Unlock only for a toolchain change the main agent has approved, and lock
# again right after (AGENTS.md, 禁止と承認).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../.."
case "${1:-}" in
lock)
	mode=a-w
	;;
unlock)
	mode=u+w
	;;
status)
	mode=
	;;
*)
	echo "usage: $0 lock|unlock|status" >&2
	exit 2
	;;
esac
for tree in build/llvm build/llvm-source build/llvm-build build/NoctLang; do
	if [ ! -e "$tree" ]; then
		continue
	fi
	real=$(realpath "$tree")
	if [ -n "$mode" ]; then
		find "$real" -type d -exec chmod "$mode" {} +
	fi
	writable=$(find "$real" -type d -perm -u+w | wc -l)
	echo "$tree -> $real: writable directories $writable"
done
