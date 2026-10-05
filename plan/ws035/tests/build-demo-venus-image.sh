#!/bin/sh
# ws035-p116: builds the Venus guest image for the demonstration's walk-through (config-amd64-demo-venus.mk): the
# guest harness's files, App Home's list, the wallpaper and the sample home maker.  The base system's accounts are
# the demonstration's (root/root, kei/kei, kei logged in at boot).
# ws136-p001 (2026-10-04): the image is a config.mk build plus files of the tree (plan/tools/guest/test-image.sh).
# The fonts come with the compositor's package (userland/desktop/fonts/); the wallpaper is
# userland/desktop/keiland/wallpapers/Birch-Lake.png.
#
#   plan/ws035/tests/build-demo-venus-image.sh [BUILD]     (default build/amd64)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:-build/amd64}
exec plan/tools/guest/test-image.sh plan/ws035/tests/config-amd64-demo-venus.mk "$build" \
	--file /etc/keiland/apps.conf=plan/ws035/demo/apps.conf \
	--file /usr/share/keiland/wallpaper.png=userland/desktop/keiland/wallpapers/Birch-Lake.png \
	--file /usr/share/files-tests/make-home.sh=plan/tools/files/make-home.sh
