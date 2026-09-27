#!/bin/sh
# ws074: takes a reference screenshot of one page with the host's headless Chromium.
#
#   sh plan/ws074/tests/chrome-shot.sh PAGE.html OUT.png [WIDTH HEIGHT]
#
# The page is shown at a device scale of 1 with no scroll bars, in a viewport of
# WIDTH x HEIGHT (default 800 x 600), and only the fonts listed by
# plan/ws074/tests/fonts.conf are visible to it, so the reference and zdesktop-browser
# draw with the same faces.  Chromium is the Debian package (sudo apt-get install chromium).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
root=$(pwd)
page=$1
out=$2
width=${3:-800}
height=${4:-600}
case $page in
/*) ;;
*) page=$root/$page ;;
esac
case $out in
/*) ;;
*) out=$root/$out ;;
esac
profile=$root/build/ws074-chrome/profile
mkdir -p "$profile" "$(dirname -- "$out")"
fonts=
if [ -f "$root/build/ws074-chrome/fonts.conf" ]; then
	fonts=$root/build/ws074-chrome/fonts.conf
fi
if [ -n "$fonts" ]; then
	FONTCONFIG_FILE=$fonts
	export FONTCONFIG_FILE
fi
timeout 120 chromium --headless --no-sandbox --disable-gpu --hide-scrollbars \
	--force-device-scale-factor=1 --user-data-dir="$profile" \
	--window-size="$width,$height" --screenshot="$out" "file://$page" >/dev/null 2>&1
test -s "$out"
echo "$out"
