#!/bin/sh
# ws005-p020: the Venus guest image with the RTL8822BU driver (plan/ws005/phase020/config-venus-rtl.mk), the guest
# harness's files, the wallpaper and App Home's list, booting graphically.
# ws136-p001 (2026-10-04): the image is a config.mk build plus files of the tree (plan/tools/guest/test-image.sh).
# The fonts come with the compositor's package (userland/desktop/fonts/); the wallpaper is
# userland/desktop/keiland/wallpapers/Birch-Lake.ppm.
#
#   sh plan/ws005/phase020/build-rtl-image.sh BUILD
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:?usage: build-rtl-image.sh BUILD}
exec plan/tools/guest/test-image.sh plan/ws005/phase020/config-venus-rtl.mk "$build" \
	--file /usr/share/keiland/wallpaper.ppm=userland/desktop/keiland/wallpapers/Birch-Lake.ppm \
	--file /etc/keiland/apps.conf=plan/ws035/demo/apps.conf \
	ZEDBSD_GRAPHICAL_BOOT=y
