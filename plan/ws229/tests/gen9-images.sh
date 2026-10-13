#!/bin/sh
# ws229-p005: builds the Gen9 bare-metal USB images (plan/ws229/tests/config-amd64-gen9.mk), one per i915 test
# scenario, in the one standard way (plan/tools/guest/test-image.sh: the config and the tree, nothing of an older
# build).  Each lands in OUT/SCENARIO/hdd-image.img; plan/ws229/tests/gen9-hw.md says how the user runs them.
#
#   plan/ws229/tests/gen9-images.sh [SCENARIO...]     default: eu vkcs vkx vke1 vke2 vkc; OUT default build/ws229-gen9
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=${OUT:-build/ws229-gen9}
[ $# -gt 0 ] || set -- eu vkcs vkx vke1 vke2 vkc
mkdir -p "$out"
status=0
for s in "$@"; do
	case $s in
	eu|vkcs|vkx|vke1|vke2|vkc) ;;
	*) echo "gen9-images: $s is not a Gen9 scenario (eu vkcs vkx vke1 vke2 vkc)"; status=1; continue ;;
	esac
	if timeout 7200 plan/tools/guest/test-image.sh --no-harness plan/ws229/tests/config-amd64-gen9.mk "$out/$s" \
	    "GEN9_SCENARIO=$s" ZEDBSD_TEST_IMAGE_TAG=gen9 disk-image > "$out/build-$s.log" 2>&1; then
		echo "gen9-images: $s $out/$s/hdd-image.img ($(sha256sum "$out/$s/hdd-image.img" | cut -c1-16))"
	else
		echo "gen9-images: $s FAILED ($out/build-$s.log)"; grep -E ' error: |Error [0-9]' "$out/build-$s.log" | head -5
		status=1
	fi
done
exit $status
