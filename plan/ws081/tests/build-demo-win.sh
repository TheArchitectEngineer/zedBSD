#!/bin/sh
# ws081: builds the demonstration image of the window tests (plan/ws081/tests/config-amd64-demo-win.mk, which asks for
# the generated wallpapers and puts in touchlog), with App Home's list, the wallpaper and the guest harness's public
# key.  touchlog is compiled from plan/ws081/tests/touchlog.c into BUILD/tests/ first (build-touchlog.sh).
# ws136-p001 (2026-10-04): the image is a config.mk build plus files of the tree (plan/tools/guest/test-image.sh).
# The fonts come with the compositor's package (userland/desktop/fonts/); the wallpaper is
# userland/desktop/keiland/wallpapers/Birch-Lake.ppm.
#
#   plan/ws081/tests/build-demo-win.sh [BUILD]     (default build/ws081-demo-win; the image is BUILD/hdd-image.img)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:-build/ws081-demo-win}
key=plan/tmp/guest/id_ed25519.pub
[ -f "$key" ] || python3 plan/tools/guest/guest.py extra-files >/dev/null
TOUCHLOG_CONFIG=plan/ws081/tests/config-amd64-demo-win.mk sh plan/ws081/tests/build-touchlog.sh "$build"
plan/tools/guest/test-image.sh --no-harness plan/ws081/tests/config-amd64-demo-win.mk "$build" \
	--file /etc/keiland/apps.conf=plan/ws035/demo/apps.conf \
	--file /usr/share/keiland/wallpaper.ppm=userland/desktop/keiland/wallpapers/Birch-Lake.ppm \
	--file /root/.ssh/authorized_keys=$key --mode /root/.ssh/authorized_keys=0600 --mode /root/.ssh=0700 \
	ZEDBSD_TEST_IMAGE_TAG=demo-win
echo "demo image: $build/hdd-image.img"
