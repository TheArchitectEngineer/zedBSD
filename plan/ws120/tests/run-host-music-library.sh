#!/bin/sh
# ws120-p008: builds and runs the host test of Music's tags and collection (host-music-library.c with
# userland/desktop/music/tags.c and library.c) under ASan and UBSan, on files make-m4a.py writes into a fresh folder
# each run (under OUTPUT's folder; Q1's cleaning removes the old ones).
#   sh plan/ws120/tests/run-host-music-library.sh [OUTPUT]   (default build/ws120/host-music-library)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws120/host-music-library}
dir=$(dirname -- "$out")
mkdir -p "$dir/inc/keiland"
cp userland/desktop/include/keiland/keiland.h "$dir/inc/keiland/keiland.h"
${CC:-cc} -std=gnu99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror \
	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
	-I. -I"$dir/inc" -Iuserland/desktop/music plan/ws120/tests/host-music-library.c userland/desktop/music/tags.c \
	userland/desktop/music/library.c -o "$out"
folder=$(mktemp -d "$dir/music-library.XXXXXX")
python3 plan/ws120/tests/make-m4a.py "$folder"
timeout 60 "$out" "$folder"
