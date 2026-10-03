#!/bin/sh
# ws035-p013〜: builds the lean zdesktop guest image with the networkd stand-in (config-amd64-network.mk), the guest
# harness's files, the wallpaper and the sample home maker.
# ws136-p001 (2026-10-04): the image is a config.mk build plus files of the tree (plan/tools/guest/test-image.sh).
# The fonts come with the compositor's package (userland/desktop/fonts/); the wallpaper is
# userland/desktop/keiland/wallpapers/Birch-Lake.ppm.
#
#   plan/ws035/tests/build-network-image.sh [BUILD]     (default build/amd64)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:-build/amd64}
exec plan/tools/guest/test-image.sh plan/ws035/tests/config-amd64-network.mk "$build" \
	--file /usr/share/keiland/wallpaper.ppm=userland/desktop/keiland/wallpapers/Birch-Lake.ppm \
	--file /usr/share/files-tests/make-home.sh=plan/tools/files/make-home.sh
