#!/bin/sh
# ws155-p002: builds and runs the host test of Calendar's store (host-calendar-store.c with
# userland/desktop/calendar/store.c and date.c) under ASan and UBSan, in a fresh folder each run (under OUTPUT's
# folder; Q1's cleaning removes the old ones).
#   sh plan/ws155/tests/run-host-calendar-store.sh [OUTPUT]   (default build/ws155/host-calendar-store)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws155/host-calendar-store}
dir=$(dirname -- "$out")
mkdir -p "$dir/inc/keiland"
cp userland/desktop/include/keiland/keiland.h "$dir/inc/keiland/keiland.h"
${CC:-cc} -std=gnu99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror \
	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
	-I. -I"$dir/inc" -Iuserland/desktop/calendar plan/ws155/tests/host-calendar-store.c userland/desktop/calendar/store.c \
	userland/desktop/calendar/date.c -lm -o "$out"
folder=$(mktemp -d "$dir/calendar-store.XXXXXX")
timeout 60 "$out" "$folder/Calendar"
