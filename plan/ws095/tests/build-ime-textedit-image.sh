#!/bin/sh
# ws095: builds the input method's Text Editor guest image (plan/ws095/tests/config-amd64-ime-textedit.mk) with the
# guest harness's files, the wallpaper and the sample home maker.
# ws136-p001 (2026-10-04): the image is a config.mk build plus files of the tree (plan/tools/guest/test-image.sh).
# The fonts come with the compositor's package (userland/desktop/fonts/); the wallpaper is
# userland/desktop/keiland/wallpapers/Birch-Lake.png.
#
#   plan/ws095/tests/build-ime-textedit-image.sh [BUILD]     (default build/ws095/img-textedit)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:-build/ws095/img-textedit}
exec plan/tools/guest/test-image.sh plan/ws095/tests/config-amd64-ime-textedit.mk "$build" \
	--file /usr/share/keiland/wallpaper.png=userland/desktop/keiland/wallpapers/Birch-Lake.png \
	--file /usr/share/files-tests/make-home.sh=plan/tools/files/make-home.sh
