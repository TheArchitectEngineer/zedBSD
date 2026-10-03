#!/bin/sh
# WS070: builds the lean zdesktop guest image of the System Menu and Titlebar tests
# (plan/tools/titlebar/config-amd64-menu.mk) with the guest harness's files and the wallpaper.
# ws136-p001 (2026-10-04): the image is a config.mk build plus files of the tree (plan/tools/guest/test-image.sh).
# The fonts come with the compositor's package (userland/desktop/fonts/); the wallpaper is
# userland/desktop/keiland/wallpapers/Birch-Lake.ppm.
#
#   plan/tools/titlebar/build-menu-image.sh [BUILD]     (default build/amd64: the external packages link against build/amd64/dynamic)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:-build/amd64}
exec plan/tools/guest/test-image.sh plan/tools/titlebar/config-amd64-menu.mk "$build" \
	--file /usr/share/keiland/wallpaper.ppm=userland/desktop/keiland/wallpapers/Birch-Lake.ppm
