#!/bin/sh
# ws157-p002: builds and runs the host test of Photos' library (host-photos-library.c with
# userland/desktop/photos/exif.c, library.c and store.c) under ASan and UBSan, on the folder make-photos.py writes
# into a fresh folder each run (under OUTPUT's folder; Q1's cleaning removes the old ones).
#   sh plan/ws157/tests/run-host-photos-library.sh [OUTPUT]   (default build/ws157/host-photos-library)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws157/host-photos-library}
dir=$(dirname -- "$out")
mkdir -p "$dir/inc/keiland"
cp userland/desktop/include/keiland/keiland.h "$dir/inc/keiland/keiland.h"
${CC:-cc} -std=gnu99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror \
	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
	-I. -I"$dir/inc" -Iuserland/desktop/photos plan/ws157/tests/host-photos-library.c \
	userland/desktop/photos/exif.c userland/desktop/photos/library.c userland/desktop/photos/store.c -o "$out"
run=$(mktemp -d "$(pwd)/$dir/photos-library.XXXXXX")
python3 plan/ws157/tests/make-photos.py "$run/Pictures"
mkdir -p "$run/config/keiland"
TZ=UTC XDG_CONFIG_HOME="$run/config" timeout 60 "$out" "$run/Pictures" "$run/config"
