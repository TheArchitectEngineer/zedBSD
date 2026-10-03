#!/bin/sh
# ws073-p047 (BUG-052): builds src/kern/tmpfs-pages.c for the host with stub physical pages and runs its test.
# Last line: tmpfs-pages-host: PASS.  ASAN=1 adds AddressSanitizer and UndefinedBehaviorSanitizer.
#   sh plan/ws073/tests/tmpfs-pages-host.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/ws073-host/tmpfs-pages
rm -rf "$out"
mkdir -p "$out"
cc=${CC:-cc}
san=
if [ "${ASAN:-}" = 1 ]; then
	san="-fsanitize=address,undefined -fno-omit-frame-pointer"
fi
flags="-std=gnu89 -O1 -g -Wall -Wextra -Werror -DHAL_ARCH_AMD64 -DKERN_USER_ABI_LP64 -I. -Iinclude -Isrc $san"
"$cc" $flags -c src/kern/tmpfs-pages.c -o "$out/tmpfs-pages.o"
"$cc" -std=gnu99 -O1 -g -w -DHAL_ARCH_AMD64 -DKERN_USER_ABI_LP64 -I. -Iinclude -Isrc $san \
    -c plan/ws073/tests/tmpfs-pages-host.c -o "$out/main.o"
"$cc" $san -o "$out/tmpfs-pages-host" "$out"/*.o
exec "$out/tmpfs-pages-host"
