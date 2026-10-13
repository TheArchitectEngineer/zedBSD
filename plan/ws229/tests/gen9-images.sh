#!/bin/sh
# ws229-p005: builds the Gen9 bare-metal USB image (plan/ws229/tests/config-amd64-gen9.mk) in the one standard way
# (plan/tools/guest/test-image.sh: the config and the tree, nothing of an older build).  The one image runs every Gen9
# test in one boot (the scenario "gen9"); it lands in OUT/hdd-image.img, and plan/ws229/tests/gen9-hw.md says how the
# user runs it.
#
#   plan/ws229/tests/gen9-images.sh      OUT default build/ws229-gen9
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=${OUT:-build/ws229-gen9}
mkdir -p "$out"
if timeout 7200 plan/tools/guest/test-image.sh --no-harness plan/ws229/tests/config-amd64-gen9.mk "$out" \
    ZEDBSD_TEST_IMAGE_TAG=gen9 disk-image > "$out/build.log" 2>&1; then
	echo "gen9-images: $out/hdd-image.img ($(sha256sum "$out/hdd-image.img" | cut -c1-16))"
	exit 0
fi
echo "gen9-images: FAILED ($out/build.log)"
grep -E ' error: |Error [0-9]' "$out/build.log" | head -5
exit 1
