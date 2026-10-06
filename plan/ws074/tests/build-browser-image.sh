#!/bin/sh
# ws074: builds the browser guest image (plan/ws074/tests/config-amd64-browser.mk) with the guest harness's files, the
# wallpaper, the sample home maker, the test pages (plan/ws074/tests/pages/) and the image test's pictures, which
# plan/ws074/tests/make-test-images.py draws into BUILD/browser-images from the tree.
# ws136-p001 (2026-10-04): the image is a config.mk build plus files of the tree (plan/tools/guest/test-image.sh).
# The fonts come with the compositor's package (userland/desktop/fonts/); the wallpaper is
# userland/desktop/wallpapers/Birch-Lake.png.
#
#   plan/ws074/tests/build-browser-image.sh [BUILD]     (default build/amd64)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:-build/amd64}
set --
for page in plan/ws074/tests/pages/*; do
	[ -f "$page" ] && set -- "$@" --file "/usr/share/browser-tests/$(basename "$page")=$page"
done
python3 plan/ws074/tests/make-test-images.py "$build/browser-images" >/dev/null
for picture in "$build"/browser-images/*; do
	[ -f "$picture" ] && set -- "$@" --file "/usr/share/browser-images/$(basename "$picture")=$picture"
done
exec plan/tools/guest/test-image.sh plan/ws074/tests/config-amd64-browser.mk "$build" \
	--file /usr/share/keiland/wallpaper.png=userland/desktop/wallpapers/Birch-Lake.png \
	--file /usr/share/files-tests/make-home.sh=plan/tools/files/make-home.sh \
	"$@"
