#!/bin/sh
# ws068: the Venus tests of desktop GL 3.3 (glx-p037), ETC2 (egl-p039) and their regressions (glx-p013, glx-p031,
# glx-p033, egl-p019 to egl-p030) in one run: build the lean image (build-glsl-image.sh), boot it with a Venus GPU
# (plan/ws035/tests/zdesktop-guest.sh), run each test against the same guest, stop the guest and write a summary.
# Each test's own output goes under OUTDIR/<test>/ and OUTDIR/<test>.txt; OUTDIR/summary.txt has one line per test.
#
#   plan/ws068/tests/venus-all.sh OUTDIR [TEST...]
#
# Environment: IMAGE_BUILD (default build/ws068-glsl), GUEST_RUNTIME (default build/ws068-run), VENUS_RENDERER
# (the strict-queue virglrenderer, default the main checkout's build/ws035-sq-venus/install), SKIP_IMAGE=1 to reuse
# the image already in IMAGE_BUILD.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=${1:?usage: venus-all.sh OUTDIR [TEST...]}
shift
tests=${*:-glx-p037 glx-p013 glx-p031 glx-p033 egl-p039 egl-p019 egl-p020 egl-p022 egl-p023 egl-p024 egl-p025 egl-p026 egl-p027 egl-p028 egl-p029 egl-p030}
image_build=${IMAGE_BUILD:-build/ws068-glsl}
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws068-run}"
export VENUS_RENDERER="${VENUS_RENDERER:-/home/awe/zedBSD-claude1/build/ws035-sq-venus/install}"
mkdir -p "$out"
summary=$out/summary.txt
: > "$summary"

# 1. The image, from the tree as it is.
if [ "${SKIP_IMAGE:-0}" != 1 ]; then
	git rev-parse HEAD > "$out/tree.txt"
	git status --short >> "$out/tree.txt"
	if ! timeout 5400 flock /tmp/zedbsd-image-build.lock sh plan/ws068/tests/build-glsl-image.sh "$image_build" > "$out/image.log" 2>&1; then
		echo "image: FAIL (see $out/image.log)" | tee -a "$summary"
		exit 1
	fi
	echo "image: ok $(sha256sum "$image_build/hdd-image.img" | cut -c1-16)" | tee -a "$summary"
fi

# 2. The guest, booted and answering on SSH (start only starts the emulator).
if ! timeout 400 sh plan/ws035/tests/zdesktop-guest.sh start "$image_build/hdd-image.img" > "$out/start.txt" 2>&1 ||
    ! timeout 300 python3 plan/tools/guest/guest.py wait >> "$out/start.txt" 2>&1; then
	echo "boot: FAIL (see $out/start.txt)" | tee -a "$summary"
	timeout 60 sh plan/ws035/tests/zdesktop-guest.sh stop > /dev/null 2>&1
	exit 1
fi
echo "boot: ok" | tee -a "$summary"

# 3. The tests, each with a time limit.
for t in $tests; do
	timeout 900 sh "plan/ws068/tests/$t.sh" "$out/$t" > "$out/$t.txt" 2>&1
	rc=$?
	echo "$t: rc=$rc $(tail -1 "$out/$t.txt")" | tee -a "$summary"
done

# 4. Stop the guest.
timeout 60 sh plan/ws035/tests/zdesktop-guest.sh stop > /dev/null 2>&1
fails=$(grep -cv -e ': ok' -e 'rc=0 .*: PASS$' "$summary")
echo "venus-all: $([ "$fails" -eq 0 ] && echo PASS || echo "FAIL ($fails)")" | tee -a "$summary"
