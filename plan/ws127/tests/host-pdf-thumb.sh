#!/bin/sh
# ws127-p004: builds a host libpdf.so (as plan/ws079/tests/run-pdf-render.sh builds libpdf, here -fPIC and shared, with
# libz-compat, libjpeg-compat, libtruetype and the C library's digests) and host-pdf-thumb.c against files' host objects
# (plan/tools/files/host-build.sh) and runs it with the library found by dlopen.
#   sh plan/ws127/tests/host-pdf-thumb.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
sh plan/tools/files/host-build.sh >/dev/null
out=build/ws127-pdf-thumb
cc=${CC:-cc}
mkdir -p "$out/include" "$out/lib"
for header in pdf.h sha2.h md5.h sha1.h; do ln -sf "$(pwd)/include/libc/$header" "$out/include/$header"; done
ln -sfn "$(pwd)/include/libc/compat" "$out/include/compat"
mkdir -p "$out/include/truetype"
ln -sf "$(pwd)/userland/desktop/include/truetype/truetype.h" "$out/include/truetype/truetype.h"
python3 plan/ws127/tests/make-pdf.py "$out/good.pdf" >/dev/null
flags="-std=gnu11 -O1 -g -fPIC -w -D_DEFAULT_SOURCE -I$out/include"
sources="userland/base/libpdf/*.c src/libc/openbsd-sha2.c src/libc/openbsd-digest.c userland/base/libz-compat/*.c
	userland/base/libjpeg-compat/*.c userland/desktop/libtruetype/*.c"
objects=
for file in $sources; do
	object="$out/lib/$(echo "$file" | tr '/.' '__').o"
	"$cc" $flags -I"$(dirname "$file")" -c "$file" -o "$object"
	objects="$objects $object"
done
"$cc" -shared -o "$out/lib/libpdf.so" $objects -lm
host=build/ws071-host
files_objects=$(ls "$host"/obj/*.o | grep -v -e '/host-model.o$' -e '/host-render.o$' -e '/host-glass.o$')
"$cc" -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter -D_GNU_SOURCE -I"$host/include" -Iuserland/desktop/files -I. \
	plan/ws127/tests/host-pdf-thumb.c $files_objects -lm -ldl -o "$out/host-pdf-thumb"
temporary=$(mktemp -d)
status=0
LD_LIBRARY_PATH="$out/lib" timeout 120 "$out/host-pdf-thumb" "$out/good.pdf" "$temporary" || status=1
rm -rf "$temporary"
exit $status
