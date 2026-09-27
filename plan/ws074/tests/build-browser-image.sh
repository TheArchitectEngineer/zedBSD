#!/bin/sh
# ws074: builds the zdesktop guest image with zdesktop-browser (plan/ws074/tests/config-amd64-browser.mk),
# the guest harness's files, the fonts and the wallpaper, like plan/tools/files/build-files-image.sh.
# The fonts and the wallpaper are not in git: build/ws035-fonts (Inter, JetBrains Mono, Droid Sans
# Fallback) and build/ws035-wallpaper; a worktree links them from the main checkout's build/.
# The browser's test pages (plan/ws074/tests/pages/) go to /usr/share/zdesktop-browser-tests/.
#
#   plan/ws074/tests/build-browser-image.sh [BUILD]     (default build/amd64)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:-build/amd64}
extra=$(python3 plan/tools/guest/guest.py extra-files | sed -n "s/^ZEDBSD_TEST_EXTRA_FILES='\(.*\)'$/\1/p")
[ -n "$extra" ] || { echo "build-browser-image: no guest files (plan/tools/guest/guest.py keys?)"; exit 1; }
fonts=build/ws035-fonts
[ -f $fonts/Inter.ttf ] && extra="$extra --file /usr/share/fonts/zdesktop.ttf=$fonts/Inter.ttf"
[ -f $fonts/OFL.txt ] && extra="$extra --file /usr/share/fonts/zdesktop-OFL.txt=$fonts/OFL.txt"
[ -f $fonts/JetBrainsMono-Regular.ttf ] && extra="$extra --file /usr/share/fonts/zdesktop-mono.ttf=$fonts/JetBrainsMono-Regular.ttf"
[ -f $fonts/JetBrainsMono-OFL.txt ] && extra="$extra --file /usr/share/fonts/zdesktop-mono-OFL.txt=$fonts/JetBrainsMono-OFL.txt"
[ -f $fonts/DroidSansFallbackFull.ttf ] && extra="$extra --file /usr/share/fonts/zdesktop-fallback.ttf=$fonts/DroidSansFallbackFull.ttf"
[ -f $fonts/DroidSansFallback-LICENSE.txt ] && extra="$extra --file /usr/share/fonts/zdesktop-fallback-LICENSE.txt=$fonts/DroidSansFallback-LICENSE.txt"
[ -f build/ws035-wallpaper/wallpaper.ppm ] && extra="$extra --file /usr/share/zdesktop/wallpaper.ppm=build/ws035-wallpaper/wallpaper.ppm"
if [ -d plan/ws074/tests/pages ]; then
	for page in plan/ws074/tests/pages/*; do
		[ -f "$page" ] && extra="$extra --file /usr/share/zdesktop-browser-tests/$(basename "$page")=$page"
	done
fi
exec make -j"$(nproc)" ZEDBSD_CONFIG=plan/ws074/tests/config-amd64-browser.mk BUILD="$build" \
    "ZEDBSD_TEST_EXTRA_FILES=$extra" disk-image
