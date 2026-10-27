#!/bin/sh
# ws291: the amd64 vmunix of a commit without the write-combining mapping (779020ee6, cfcd07066), which waits for the
# user's approval and must not run on the machine: the commit's tree exported to OUT/tree, those two commits' changes
# taken back out there alone (the branch keeps them), the kernel built there (the shared build/ trees linked in).
#   usage: plan/ws291/tests/nowc-build.sh OUT [COMMIT]   (from the top of the tree; prints the vmunix's path)
# KEEP_WC=1 keeps the write-combining mapping (approved 2026-10-28), for a diagnostic build of the branch as it is.
# PATCHES: more patches (paths) applied to the exported tree before the build (diagnostic builds).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
out=${1:?usage: nowc-build.sh OUT [COMMIT]}
commit=${2:-HEAD}
top=$(pwd)
mkdir -p "$out/tree/build"
git archive "$commit" | tar -x -C "$out/tree"
: > "$out/patch.log"
if [ "${KEEP_WC:-0}" != 1 ]; then
	git diff 779020ee6^ cfcd07066 | patch -R -p1 -d "$out/tree" >> "$out/patch.log"
fi
for extra in ${PATCHES:-}; do
	patch -p1 -d "$out/tree" < "$extra" >> "$out/patch.log"
done
for name in llvm llvm-source NoctLang sources distfiles; do
	[ -e "$out/tree/build/$name" ] || ln -s "$(readlink -f "$top/build/$name")" "$out/tree/build/$name"
done
(cd "$out/tree" && nice make -j16 ZEDBSD_CONFIG=plan/ws291/tests/config.mk BUILD=build/ws291/usb build/ws291/usb/vmunix) > "$out/vmunix.log" 2>&1
grep -ci warning "$out/vmunix.log" | sed 's/^/warnings: /'
echo "$out/tree/build/ws291/usb/vmunix $(sha256sum "$out/tree/build/ws291/usb/vmunix" | cut -c1-16)"
