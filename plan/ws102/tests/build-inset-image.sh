#!/bin/sh
# ws102: builds the inset guest image (plan/ws102/tests/config-amd64-inset.mk) with the guest harness's files, the
# wallpaper and the tests' sample home maker.
# ws136-p001 (2026-10-04): the image is a config.mk build plus files of the tree (plan/tools/guest/test-image.sh).
# The fonts come with the compositor's package (userland/desktop/fonts/); the wallpaper is
# userland/desktop/keiland/wallpapers/Birch-Lake.png.
#
#   plan/ws102/tests/build-inset-image.sh [BUILD]     (default build/amd64)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:-build/amd64}
exec plan/tools/guest/test-image.sh plan/ws102/tests/config-amd64-inset.mk "$build" \
	--file /usr/share/keiland/wallpaper.png=userland/desktop/keiland/wallpapers/Birch-Lake.png \
	--file /usr/share/files-tests/make-home.sh=plan/tools/files/make-home.sh
