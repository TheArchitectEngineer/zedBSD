#!/bin/sh
# ws071: runs files-render (host-render.c) over the sample home and turns the pictures it drew into PNG.
#
#   sh plan/tools/files/host-run.sh [--fresh] OPTION-OR-ACTION...
#
# HOME is build/ws071-host/home (made by make-home.sh; --fresh makes it again and empties the clipboard).  The fallback
# font is Droid Sans Fallback when the host has it (for Japanese names).  Every draw=NAME.ppm
# action is also written as NAME.png.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
root=$(pwd)
home=$root/build/ws071-host/home
if [ "${1:-}" = --fresh ]; then
	shift
	rm -rf "$home"
	rm -f "$root/build/ws071-host/files.clipboard"
fi
[ -d "$home" ] || sh plan/tools/files/make-home.sh "$home" >/dev/null
fallback=
[ -f /usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf ] && fallback=--fallback=/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf
HOME=$home XDG_RUNTIME_DIR=$root/build/ws071-host XDG_DATA_HOME=$home/.local/share XDG_CONFIG_HOME=$home/.config \
    "$root/build/ws071-host/files-render" $fallback "$@"
for argument in "$@"; do
	case $argument in
	draw=*.ppm)
		picture=${argument#draw=}
		python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" "$picture" "${picture%.ppm}.png"
		;;
	esac
done
