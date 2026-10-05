#!/bin/sh
# ws101-p009: builds the guest image of the OpenGL ES 3.1 compute test (plan/ws101/tests/gles/config-amd64-compute.mk)
# with the guest harness's files and the wallpaper.
# ws136-p001 (2026-10-04): the image is a config.mk build plus files of the tree (plan/tools/guest/test-image.sh).
# The fonts come with the compositor's package (userland/desktop/fonts/); the wallpaper is
# userland/desktop/keiland/wallpapers/Birch-Lake.png.
#
#   plan/ws101/tests/gles/build-image.sh [BUILD]     (default build/ws101-p009-img)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../../.."
build=${1:-build/ws101-p009-img}
exec plan/tools/guest/test-image.sh plan/ws101/tests/gles/config-amd64-compute.mk "$build" \
	--file /usr/share/keiland/wallpaper.png=userland/desktop/keiland/wallpapers/Birch-Lake.png
