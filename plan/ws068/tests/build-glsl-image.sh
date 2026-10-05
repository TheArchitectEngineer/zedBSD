#!/bin/sh
# ws068-p019: builds the guest image of the GLSL compiler's Venus tests (plan/ws068/tests/config-amd64-glsl.mk) with
# the guest harness's files and the wallpaper.
# ws136-p001 (2026-10-04): the image is a config.mk build plus files of the tree (plan/tools/guest/test-image.sh).
# The fonts come with the compositor's package (userland/desktop/fonts/); the wallpaper is
# userland/desktop/keiland/wallpapers/Birch-Lake.png.
#
#   plan/ws068/tests/build-glsl-image.sh [BUILD]     (default build/ws068-glsl)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:-build/ws068-glsl}
exec plan/tools/guest/test-image.sh plan/ws068/tests/config-amd64-glsl.mk "$build" \
	--file /usr/share/keiland/wallpaper.png=userland/desktop/keiland/wallpapers/Birch-Lake.png
