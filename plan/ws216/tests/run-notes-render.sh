#!/bin/sh
# ws216-p004: builds Notes' screen (render.c, geometry.c) with its model (document.c, shape.c and the rest) and the
# whole of libpdf over the model of a Vulkan device (host-vulkan-mock.c) with the host's C compiler (plain, ASan and
# UBSan) into host-notes-render, and runs the acceptance of plan/ws216/phase004/phase.md section 6 (C2 to C7):
#   sh plan/ws216/tests/run-notes-render.sh
# Then the SPIR-V: shaders/regenerate.py (which runs spirv-val on each shader) into a copy of shaders.h, which must
# equal the one in the tree (C1).
# Output: build/ws216-p004-host (a new run each time behind the name; nothing is removed, Q1 cleans the old runs).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/ws216-p004-host
cc=${CC:-cc}
. plan/tools/fresh-out.sh
fresh_out "$out"
out=$(cd "$out" && pwd -P)
mkdir -p "$out/include/truetype"
ln -sfn "$(pwd)/include/libc/compat" "$out/include/compat"
for header in pdf.h sha2.h md5.h sha1.h; do
	ln -sf "$(pwd)/include/libc/$header" "$out/include/$header"
done
ln -sf "$(pwd)/userland/desktop/include/truetype/truetype.h" "$out/include/truetype/truetype.h"
ln -sfn "$(pwd)/userland/desktop/include/keiland" "$out/include/keiland"
notes="userland/desktop/notes/document.c userland/desktop/notes/edit.c userland/desktop/notes/encode.c
	userland/desktop/notes/journal.c userland/desktop/notes/save.c userland/desktop/notes/shape.c
	userland/desktop/notes/geometry.c userland/desktop/notes/render.c"
libpdf=$(ls userland/base/libpdf/*.c)
for variant in plain asan ubsan; do
	flags="-std=c99 -pedantic -O1 -g -Wall -Wextra -Werror -Wno-overlength-strings -D_DEFAULT_SOURCE -I$out/include"
	if [ "$variant" = asan ]; then
		flags="$flags -fsanitize=address -fno-omit-frame-pointer"
	fi
	if [ "$variant" = ubsan ]; then
		flags="$flags -fsanitize=undefined,float-cast-overflow -fno-sanitize-recover=all"
	fi
	loose=$(echo "$flags" | sed 's/-std=c99 -pedantic/-std=gnu11/; s/-Werror//')
	objects=
	"$cc" $loose -c src/libc/openbsd-sha2.c -o "$out/sha2-$variant.o"
	"$cc" $loose -w -c src/libc/openbsd-digest.c -o "$out/digest-$variant.o"
	objects="$objects $out/sha2-$variant.o $out/digest-$variant.o"
	for file in userland/base/libz-compat/*.c userland/base/libjpeg-compat/*.c userland/desktop/libtruetype/*.c; do
		object="$out/$(basename "$(dirname "$file")")-$(basename "$file" .c)-$variant.o"
		"$cc" $loose -w -I"$(dirname "$file")" -c "$file" -o "$object"
		objects="$objects $object"
	done
	# shellcheck disable=SC2086
	"$cc" $flags -Iuserland/desktop/notes -Iplan/ws216/tests $notes $libpdf plan/ws216/tests/host-vulkan-mock.c \
		plan/ws216/tests/host-notes-render.c $objects -lm -o "$out/host-notes-render-$variant"
	timeout 600 "$out/host-notes-render-$variant" "$out/render-$variant.log"
done
grep -h '^NOTES' "$out/render-plain.log" | sort | uniq -c

# C1: the SPIR-V made again from the shaders is the one in the tree, and each shader passes spirv-val.
mkdir -p "$out/shaders"
cp userland/desktop/notes/shaders/*.vert userland/desktop/notes/shaders/*.frag userland/desktop/notes/shaders/regenerate.py "$out/shaders/"
python3 -I "$out/shaders/regenerate.py"
cmp "$out/shaders.h" userland/desktop/notes/shaders.h
echo "run-notes-render: ok"
