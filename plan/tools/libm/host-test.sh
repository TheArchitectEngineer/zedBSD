#!/bin/sh
# Builds the libm test runner on the host against src/libc/math and runs it
# (WS076).
#
#   plan/tools/libm/host-test.sh [--count N] [NAME...]
#
# The library is compiled against zedBSD's headers, the runner against the
# host's, and they meet through errno-shim.c.  libm of the host is not
# linked, so every math call reaches the library under test.  The reference
# cases are regenerated when the generator is newer than them.
#
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
out="$root/build/ws076-libm"
count=20000
if [ "${1:-}" = "--count" ]; then
	count=$2
	shift 2
fi
cc=${HOSTCC:-clang}

mkdir -p "$out/obj"
cd "$root"

# The library, with zedBSD's headers first on the search path.
objects=""
for source in src/libc/math/*.c src/libc/fenv.c; do
	name=$(echo "$source" | tr '/' '_')
	$cc -std=c11 -O2 -fno-builtin -Wall -Wextra -Werror -Iinclude/libc \
		-DKERN_UAPI_NATIVE -Iinclude -I. -c "$source" -o "$out/obj/$name.o"
	objects="$objects $out/obj/$name.o"
done

# The runner and the errno cell, with the host's headers.
$cc -std=c11 -O2 -fno-builtin -Wall -Wextra -Werror -c \
	plan/tools/libm/libm-test.c -o "$out/obj/libm-test.o"
$cc -std=c11 -O2 -Wall -Wextra -Werror -c plan/tools/libm/errno-shim.c \
	-o "$out/obj/errno-shim.o"
# shellcheck disable=SC2086
$cc -o "$out/libm-test" "$out/obj/libm-test.o" "$out/obj/errno-shim.o" $objects

# The reference cases.
reference="$out/reference-$count.bin"
if [ ! -f "$reference" ] || [ plan/tools/libm/gen-reference.py -nt "$reference" ]; then
	python3 plan/tools/libm/gen-reference.py "$reference" --count "$count"
fi

exec "$out/libm-test" "$reference" "$@"
