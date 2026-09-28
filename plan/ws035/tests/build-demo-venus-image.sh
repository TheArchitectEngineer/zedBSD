#!/bin/sh
# ws035-p116: builds the Venus guest image for the demonstration's walk-through (config-amd64-demo-venus.mk): the
# graphical login with Notes, App Home's demonstration list (plan/ws035/demo/apps.conf), the guest harness's files,
# and the fonts and the wallpaper, which are not in git (build/ws035-fonts/, build/ws035-wallpaper/).
# root logs in with an empty password.
#
#   plan/ws035/tests/build-demo-venus-image.sh [BUILD]     (default build/amd64)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:-build/amd64}
extra=$(python3 plan/tools/guest/guest.py extra-files | sed -n "s/^ZEDBSD_TEST_EXTRA_FILES='\(.*\)'$/\1/p")
[ -n "$extra" ] || { echo "build-demo-venus-image: no guest files (plan/tools/guest/guest.py keys?)"; exit 1; }
extra="$extra --file /etc/keiland/apps.conf=plan/ws035/demo/apps.conf"
for pair in \
	keiland.ttf=build/ws035-fonts/Inter.ttf \
	keiland-OFL.txt=build/ws035-fonts/OFL.txt \
	keiland-mono.ttf=build/ws035-fonts/JetBrainsMono-Regular.ttf \
	keiland-mono-OFL.txt=build/ws035-fonts/JetBrainsMono-OFL.txt \
	keiland-fallback.ttf=build/ws035-fonts/DroidSansFallbackFull.ttf \
	keiland-fallback-LICENSE.txt=build/ws035-fonts/DroidSansFallback-LICENSE.txt; do
	[ -f "${pair#*=}" ] && extra="$extra --file /usr/share/fonts/$pair"
done
[ -f build/ws035-wallpaper/wallpaper.ppm ] && extra="$extra --file /usr/share/keiland/wallpaper.ppm=build/ws035-wallpaper/wallpaper.ppm"
extra="$extra --file /usr/share/files-tests/make-home.sh=plan/tools/files/make-home.sh"
exec make -j"$(nproc)" ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-demo-venus.mk BUILD="$build" \
    "ZEDBSD_TEST_EXTRA_FILES=$extra" disk-image
