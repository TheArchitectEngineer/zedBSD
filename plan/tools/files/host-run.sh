#!/bin/sh
# ws071: runs files-render (host-render.c) over the sample home and turns the pictures it drew into PNG.
#
#   sh plan/tools/files/host-run.sh [--fresh] OPTION-OR-ACTION...
#
# HOME is build/ws071-host/home, or after --fresh a new build/ws071-host/run.*/home with its own clipboard (made by
# make-home.sh).  The fallback
# font is Droid Sans Fallback when the host has it (for Japanese names).  Every draw=NAME.ppm
# action is also written as NAME.png.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
root=$(pwd)
# A fresh run gets a new directory instead of removing the old one (2026-10-06 user: deleting is Q1's step, so
# this script runs none); "current" names it for the runs after it, and plan/tools/files/host-clean.sh (Q1) removes
# the old ones.
host=$root/build/ws071-host
run=$host
[ -f "$host/current" ] && run=$(cat "$host/current")
if [ "${1:-}" = --fresh ]; then
	shift
	run=$(mktemp -d "$host/run.XXXXXX")
	printf '%s\n' "$run" > "$host/current"
fi
home=$run/home
[ -d "$home" ] || sh plan/tools/files/make-home.sh "$home" >/dev/null
fallback=
fallback=--fallback=$root/userland/desktop/fonts/DroidSansFallbackFull.ttf
HOME=$home XDG_RUNTIME_DIR=$run XDG_DATA_HOME=$home/.local/share XDG_CONFIG_HOME=$home/.config \
    "$host/files-render" $fallback "$@"
for argument in "$@"; do
	case $argument in
	draw=*.ppm)
		picture=${argument#draw=}
		python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" "$picture" "${picture%.ppm}.png"
		;;
	esac
done
