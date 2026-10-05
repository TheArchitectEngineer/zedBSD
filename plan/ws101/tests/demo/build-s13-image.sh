#!/bin/sh
# ws101-p015: builds the demonstration image for the 5330's passthrough (as plan/ws075/demo/build-demo-image.sh BUILD
# passthrough does: the wallpapers, App Home's apps.conf, the guest harness's key) from plan/ws101/tests/demo/config.mk,
# with /bin/noct taken from NOCT (an accelerator-enabled build's bin/noct, made with ZEDBSD_NOCT_ACCEL := y where the
# toolchain rule allows it) instead of building Noct here.
# ws136-p001 (2026-10-04): the image is a config.mk build plus files of the tree (plan/tools/guest/test-image.sh).
# The fonts come with the compositor's package (userland/desktop/fonts/); the wallpaper is
# userland/desktop/keiland/wallpapers/Birch-Lake.png.
# NOCT has no default: the old one was another checkout's build (ws136-p001 leaves this as residual work).
#
#   NOCT=PATH plan/ws101/tests/demo/build-s13-image.sh [BUILD]      (default build/ws101-p015-demo)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../../.."
build=${1:-build/ws101-p015-demo}
noct=${NOCT:?build-s13-image: NOCT names an accelerator-enabled noct}
[ -x "$noct" ] || { echo "build-s13-image: no accelerator-enabled noct at $noct"; exit 1; }
key=plan/tmp/guest/id_ed25519.pub
[ -f "$key" ] || python3 plan/tools/guest/guest.py extra-files >/dev/null
plan/tools/guest/test-image.sh --no-harness plan/ws101/tests/demo/config.mk "$build" \
	--file /etc/keiland/apps.conf=plan/ws035/demo/apps.conf \
	--file /bin/noct=$noct --mode /bin/noct=0755 \
	--file /usr/share/keiland/wallpaper.png=userland/desktop/keiland/wallpapers/Birch-Lake.png \
	--file /root/.ssh/authorized_keys=$key --mode /root/.ssh/authorized_keys=0600 --mode /root/.ssh=0700 \
	I915_TEST_VBT=y ZEDBSD_TEST_IMAGE_TAG=ws101-demo
echo "s13 demo image: $build/hdd-image.img"
