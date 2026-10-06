#!/bin/sh
# ws089-p027: builds and runs the host test of About's version name (se_about_pretty_name of
# userland/desktop/settings/about.c) under the sanitizers.
#   sh plan/ws089/tests/host-about.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws089-host-about}
mkdir -p "$out/include"
for header in truetype/truetype.h keiland/keiland.h; do
	mkdir -p "$out/include/$(dirname "$header")"
	ln -sf "$(pwd)/userland/desktop/include/$header" "$out/include/$header"
done
${CC:-cc} -O1 -g -std=gnu89 -Wall -Wextra -Werror -Wno-unused-parameter -D_GNU_SOURCE -fsanitize=address,undefined \
    -I"$out/include" -Iuserland/desktop/settings -I. \
    userland/desktop/settings/about.c plan/ws089/tests/host-about.c -o "$out/host-about"
exec "$out/host-about" "$out"
