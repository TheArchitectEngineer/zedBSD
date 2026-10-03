#!/bin/sh
# Builds the zdesktop demo image for a real amd64 machine with an Intel GPU (i915): zdesktop --glass starts at
# boot on the machine's own display, and App Home (the launcher at the top left, or a drag from the
# top-left corner; its list is plan/ws035/demo/apps.conf) starts Files, the terminal, the browser (when its
# start page is there), the model viewer, and the X11 terminal and Gears (xserver starts with the
# first of them).  The session's home is /root with the usual folders (run-zdesktop.sh).
# ws136-p001 (2026-10-04): the image is a config.mk build plus files of the tree (plan/tools/guest/test-image.sh).
# The fonts come with the compositor's package (userland/desktop/fonts/); the wallpaper is
# userland/desktop/keiland/wallpapers/Birch-Lake.ppm.
#
#   plan/ws035/demo/build-demo-image.sh [BUILD]     (default build/zdesktop-demo)
#
# The image is BUILD/hdd-image.img; write it to a USB stick and boot the machine from it (UEFI).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:-build/zdesktop-demo}
demo=plan/ws035/demo
plan/tools/guest/test-image.sh --no-harness plan/ws031/tests/config-zdesktop-hw.mk "$build" \
	--file /etc/service.d/zdesktop=$demo/zdesktop \
	--file /etc/keiland/run-zdesktop.sh=$demo/run-zdesktop.sh \
	--file /etc/keiland/apps.conf=$demo/apps.conf \
	--file /usr/share/keiland/wallpaper.ppm=userland/desktop/keiland/wallpapers/Birch-Lake.ppm \
	ZEDBSD_TEST_RC_CONF=$demo/rc.conf ZEDBSD_TEST_IMAGE_TAG=zdesktop-demo
echo "zdesktop demo image: $build/hdd-image.img"
