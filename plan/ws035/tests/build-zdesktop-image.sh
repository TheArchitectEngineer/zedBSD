#!/bin/sh
# Builds the zdesktop guest image for the Venus tests (plan/ws035/tests/config-amd64-zdesktop.mk): the guest harness's
# files (SSH keys, net.conf) and the wallpaper.
# ws136-p001 (2026-10-04): the image is a config.mk build plus files of the tree (plan/tools/guest/test-image.sh).
# The fonts come with the compositor's package (userland/desktop/fonts/); the wallpaper is
# userland/desktop/keiland/wallpapers/Birch-Lake.png.
#
#   plan/ws035/tests/build-zdesktop-image.sh [BUILD]     (default build/ws035-sq)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:-build/ws035-sq}
exec plan/tools/guest/test-image.sh plan/ws035/tests/config-amd64-zdesktop.mk "$build" \
	--file /usr/share/keiland/wallpaper.png=userland/desktop/keiland/wallpapers/Birch-Lake.png
