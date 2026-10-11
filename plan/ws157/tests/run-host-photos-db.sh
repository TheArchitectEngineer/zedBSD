#!/bin/sh
# ws157-p004: builds and runs the host test of Photos' library, database and import (host-photos-db.c with
# userland/desktop/photos/exif.c, library.c, db.c and import.c, and the C library's SHA-256) under ASan and UBSan, on
# the folders make-photos.py writes into a fresh folder each run (under OUTPUT's folder; Q1's cleaning removes the old
# ones).
#   sh plan/ws157/tests/run-host-photos-db.sh [OUTPUT [LINUX_BUILD]]   (default build/ws157/host-photos-db; LINUX_BUILD
#   is a keiland-linux.mk build with keiland-preview)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws157/host-photos-db}
dir=$(dirname -- "$out")
mkdir -p "$dir/inc/keiland"
# ws157-p013: import.c asks keiland-preview --info (the Linux build's, LINUX_BUILD) for sizes and EXIF dates; the child
# starts without an environment, so a wrapper gives it the build's libraries.
linux=$(realpath "${2:-build/ws157-q937-linux}")
mkdir -p "$dir/libexec"
printf '#!/bin/sh\nLD_LIBRARY_PATH=%s exec %s "$@"\n' "$linux/lib" "$linux/libexec/keiland-preview" >"$dir/libexec/keiland-preview"
chmod 0755 "$dir/libexec/keiland-preview"
libexec=$(realpath "$dir/libexec")
cp userland/desktop/include/keiland/keiland.h "$dir/inc/keiland/keiland.h"
cp include/libc/sha2.h "$dir/inc/sha2.h"
${CC:-cc} -std=gnu99 -D_GNU_SOURCE -O1 -g -w -c src/libc/openbsd-sha2.c -I"$dir/inc" -o "$dir/sha2.o"
${CC:-cc} -std=gnu99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror \
	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
	-I. -I"$dir/inc" -Iuserland/desktop/photos "-DKEILAND_LIBEXECDIR=\"$libexec\"" plan/ws157/tests/host-photos-db.c \
	userland/desktop/photos/exif.c userland/desktop/photos/library.c userland/desktop/photos/db.c \
	userland/desktop/photos/import.c userland/desktop/preview/client.c userland/desktop/preview/linux/spawn.c "$dir/sha2.o" -o "$out"
run=$(mktemp -d "$(pwd)/$dir/photos-db.XXXXXX")
python3 plan/ws157/tests/make-photos.py "$run/Pictures"
python3 plan/ws157/tests/make-photos.py --other "$run/Other"
TZ=UTC timeout 60 "$out" "$run/Pictures" "$run/Other" "$run/Library"
