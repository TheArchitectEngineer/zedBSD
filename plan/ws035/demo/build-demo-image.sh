#!/bin/sh
# Builds the zdesktop demo image for a real amd64 machine with an Intel GPU (i915): zdesktop --glass starts at
# boot on the machine's own display, and App Home (the launcher at the top left, or a drag from the
# top-left corner; its list is plan/ws035/demo/apps.conf) starts Files, the terminal, the browser (when its
# start page is there), the model viewer, and the X11 terminal and Gears (xserver starts with the
# first of them).  The session's home is /root with the usual folders (run-zdesktop.sh).  The fonts and the
# wallpaper, kept out of git, are put in from build/ws035-fonts/ and build/ws035-wallpaper/ when they are there.
#
#   plan/ws035/demo/build-demo-image.sh [BUILD]     (default build/zdesktop-demo)
#
# The image is BUILD/hdd-image.img; write it to a USB stick and boot the machine from it (UEFI).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:-build/zdesktop-demo}
demo=plan/ws035/demo
extra="--file /etc/service.d/zdesktop=$demo/zdesktop --file /etc/keiland/run-zdesktop.sh=$demo/run-zdesktop.sh --file /etc/keiland/apps.conf=$demo/apps.conf"
[ -f build/ws035-fonts/Inter.ttf ] && extra="$extra --file /usr/share/fonts/keiland.ttf=build/ws035-fonts/Inter.ttf"
[ -f build/ws035-fonts/OFL.txt ] && extra="$extra --file /usr/share/fonts/keiland-OFL.txt=build/ws035-fonts/OFL.txt"
[ -f build/ws035-fonts/JetBrainsMono-Regular.ttf ] && extra="$extra --file /usr/share/fonts/keiland-mono.ttf=build/ws035-fonts/JetBrainsMono-Regular.ttf"
[ -f build/ws035-fonts/JetBrainsMono-OFL.txt ] && extra="$extra --file /usr/share/fonts/keiland-mono-OFL.txt=build/ws035-fonts/JetBrainsMono-OFL.txt"
[ -f build/ws035-wallpaper/wallpaper-1080.ppm ] && extra="$extra --file /usr/share/keiland/wallpaper.ppm=build/ws035-wallpaper/wallpaper-1080.ppm"
make -j"$(nproc)" ZEDBSD_CONFIG=plan/ws031/tests/config-zdesktop-hw.mk BUILD="$build" \
    "ZEDBSD_TEST_RC_CONF=$demo/rc.conf" "ZEDBSD_TEST_EXTRA_FILES=$extra" ZEDBSD_TEST_IMAGE_TAG=zdesktop-demo disk-image
echo "zdesktop demo image: $build/hdd-image.img"
