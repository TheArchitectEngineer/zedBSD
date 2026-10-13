#!/bin/sh
# ws216-p005: builds the fill tool (fill.c, fill-page.c) with Notes' model (document.c and the rest), render.c, geometry.c and the
# whole of libpdf over the model of a Vulkan device (host-vulkan-mock.c) with the host's C compiler (plain, ASan and
# UBSan) into host-notes-fill, and runs the acceptance of plan/ws216/phase005/phase.md section 5 (F2 to F10):
#   sh plan/ws216/tests/run-notes-fill.sh
# Output: build/ws216-p005-host (a new run each time behind the name; nothing is removed, Q1 cleans the old runs).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/ws216-p005-host
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
	userland/desktop/notes/geometry.c userland/desktop/notes/render.c userland/desktop/notes/fill.c userland/desktop/notes/fill-page.c"
libpdf=$(ls userland/desktop/libpdf/*.c)
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
	for file in userland/base/libz-compat/*.c userland/desktop/libjpeg-compat/*.c userland/desktop/libtruetype/*.c; do
		object="$out/$(basename "$(dirname "$file")")-$(basename "$file" .c)-$variant.o"
		"$cc" $loose -w -I"$(dirname "$file")" -c "$file" -o "$object"
		objects="$objects $object"
	done
	# shellcheck disable=SC2086
	"$cc" $flags -Iuserland/desktop/notes -Iplan/ws216/tests $notes $libpdf plan/ws216/tests/host-vulkan-mock.c \
		plan/ws216/tests/host-notes-fill.c $objects -lm -o "$out/host-notes-fill-$variant"
	mkdir -p "$out/run-$variant"
	timeout 1200 "$out/host-notes-fill-$variant" "$out/fill-$variant.log" "$out/run-$variant"
done
grep -h '^NOTES' "$out/fill-plain.log" | sed 's/ ms=[0-9.]*//' | sort | uniq -c

echo "run-notes-fill: ok"
