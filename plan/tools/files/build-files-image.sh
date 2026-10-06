#!/bin/sh
# ws071: builds the lean zdesktop guest image (plan/tools/files/config-amd64-files.mk) with the guest harness's
# files, the wallpaper and the tests' sample home maker, like plan/tools/titlebar/build-menu-image.sh.
# ws136-p001 (2026-10-04): the image is a config.mk build plus files of the tree (plan/tools/guest/test-image.sh).
# The fonts come with the compositor's package (userland/desktop/fonts/); the wallpaper is
# userland/desktop/wallpapers/Birch-Lake.png.
#
#   plan/tools/files/build-files-image.sh [BUILD]     (default build/amd64)
#   FILES_CONFIG=plan/tools/files/config-amd64-files-ime.mk plan/tools/files/build-files-image.sh BUILD
#                                                     (the same with the input method, BUG-146)
#   FILES_EXTRA='--file DEST=SOURCE ...'              (files another test adds, e.g. plan/ws134/tests/build-monitor-image.sh)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:-build/amd64}
# FILES_EXTRA is a list of --file and --mode pairs, split into words.
# shellcheck disable=SC2086
exec plan/tools/guest/test-image.sh "${FILES_CONFIG:-plan/tools/files/config-amd64-files.mk}" "$build" \
	--file /usr/share/keiland/wallpaper.png=userland/desktop/wallpapers/Birch-Lake.png \
	--file /usr/share/files-tests/make-home.sh=plan/tools/files/make-home.sh \
	${FILES_EXTRA:-}
