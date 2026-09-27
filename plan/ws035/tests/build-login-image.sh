#!/bin/sh
# ws035-p094〜: builds the lean zdesktop guest image with the graphical login's test greeter
# (plan/ws035/tests/config-amd64-login.mk), like plan/tools/files/build-files-image.sh (the guest harness's
# files, the fonts and the wallpaper, which are not in git).
#
#   plan/ws035/tests/build-login-image.sh [BUILD]     (default build/amd64)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:-build/amd64}
extra=$(python3 plan/tools/guest/guest.py extra-files | sed -n "s/^ZEDBSD_TEST_EXTRA_FILES='\(.*\)'$/\1/p")
[ -n "$extra" ] || { echo "build-login-image: no guest files (plan/tools/guest/guest.py keys?)"; exit 1; }
[ -f build/ws035-fonts/Inter.ttf ] && extra="$extra --file /usr/share/fonts/zdesktop.ttf=build/ws035-fonts/Inter.ttf"
[ -f build/ws035-fonts/OFL.txt ] && extra="$extra --file /usr/share/fonts/zdesktop-OFL.txt=build/ws035-fonts/OFL.txt"
[ -f build/ws035-fonts/JetBrainsMono-Regular.ttf ] && extra="$extra --file /usr/share/fonts/zdesktop-mono.ttf=build/ws035-fonts/JetBrainsMono-Regular.ttf"
[ -f build/ws035-fonts/JetBrainsMono-OFL.txt ] && extra="$extra --file /usr/share/fonts/zdesktop-mono-OFL.txt=build/ws035-fonts/JetBrainsMono-OFL.txt"
[ -f build/ws035-wallpaper/wallpaper.ppm ] && extra="$extra --file /usr/share/zdesktop/wallpaper.ppm=build/ws035-wallpaper/wallpaper.ppm"
extra="$extra --file /usr/share/zdesktop-files-tests/make-home.sh=plan/tools/files/make-home.sh"
exec make -j"$(nproc)" ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-login.mk BUILD="$build" \
    "ZEDBSD_TEST_EXTRA_FILES=$extra" disk-image
