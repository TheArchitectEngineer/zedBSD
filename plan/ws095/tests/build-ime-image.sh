#!/bin/sh
# ws095: builds the input method's guest image (plan/ws095/tests/config-amd64-ime.mk) with the guest harness's files,
# the wallpaper and App Home's list of the demonstration.
# ws136-p001 (2026-10-04): the image is a config.mk build plus files of the tree (plan/tools/guest/test-image.sh).
# The fonts come with the compositor's package (userland/desktop/fonts/); the wallpaper is
# userland/desktop/keiland/wallpapers/Birch-Lake.ppm.
#
#   plan/ws095/tests/build-ime-image.sh [BUILD [DISTDIR]]   (default build/ws095/img; DISTDIR: where the dictionary's
#                                                             archive is fetched to, default the shared build/distfiles)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:-build/ws095/img}
distdir=${2:-}
set -- --file /usr/share/keiland/wallpaper.ppm=userland/desktop/keiland/wallpapers/Birch-Lake.ppm --file /etc/keiland/apps.conf=plan/ws035/demo/apps.conf
[ -n "$distdir" ] && set -- "$@" "ZEDBSD_EXTERNAL_DISTDIR=$distdir"
exec plan/tools/guest/test-image.sh plan/ws095/tests/config-amd64-ime.mk "$build" "$@"
