#!/bin/sh
# ws100: builds the volume guest image (plan/ws100/tests/config-amd64-volume.mk, the criteria config with the
# generated wallpapers and audiod-feedback) with the guest harness's files, the wallpaper and the sample home maker.
# audiod-feedback is compiled from plan/ws100/tests/audiod-feedback.c into BUILD/tests/ first.
# ws136-p001 (2026-10-04): the image is a config.mk build plus files of the tree (plan/tools/guest/test-image.sh).
# The fonts come with the compositor's package (userland/desktop/fonts/); the wallpaper is
# userland/desktop/wallpapers/Birch-Lake.png.
#
#   plan/ws100/tests/build-volume-image.sh [BUILD]     (default build/amd64)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:-build/amd64}
sh plan/ws100/tests/build-audiod-feedback.sh "$build"
exec plan/tools/guest/test-image.sh plan/ws100/tests/config-amd64-volume.mk "$build" \
	--file /usr/share/keiland/wallpaper.png=userland/desktop/wallpapers/Birch-Lake.png \
	--file /usr/share/files-tests/make-home.sh=plan/tools/files/make-home.sh
