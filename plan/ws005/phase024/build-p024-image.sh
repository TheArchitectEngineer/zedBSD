#!/bin/sh
# ws005-p024: the AX211 desktop image for the 5330's passthrough (plan/ws004/tests/config-ax211-desktop.mk, which
# asks for the generated wallpapers) without the autologin, with the p024 watcher service and its rc.conf.
# The base system's accounts are the demonstration's (root/root, kei/kei), so no accounts are put in.
# ws136-p001 (2026-10-04): the image is a config.mk build plus files of the tree (plan/tools/guest/test-image.sh).
# The fonts come with the compositor's package (userland/desktop/fonts/); the wallpaper is
# userland/desktop/keiland/wallpapers/Birch-Lake.png.
#
#   sh plan/ws005/phase024/build-p024-image.sh [BUILD]        (default build/p1-wdesk24)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:-build/p1-wdesk24}
exec plan/tools/guest/test-image.sh --no-harness plan/ws004/tests/config-ax211-desktop.mk "$build" \
	--file /etc/keiland/apps.conf=plan/ws035/demo/apps.conf \
	--file /usr/share/keiland/wallpaper.png=userland/desktop/keiland/wallpapers/Birch-Lake.png \
	--file /etc/keiland/autologin=plan/ws005/phase024/image/autologin-none \
	--file /etc/rc.conf=plan/ws005/phase024/image/rc.conf \
	--file /etc/service.d/p024watch=plan/ws005/phase024/image/p024watch.service \
	--file /etc/p024-watch.sh=plan/ws005/phase024/image/p024-watch.sh \
	--mode /etc/p024-watch.sh=0755 \
	I915_TEST_VBT=y \
	ZEDBSD_TEST_IMAGE_TAG=demo-hdmi
