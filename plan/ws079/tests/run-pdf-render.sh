#!/bin/sh
# ws079-p006: builds libpdf (with libz-compat and libjpeg-compat) and host-pdf-render with the host's C compiler
# (plain, ASan and UBSan), writes a Notes-like document through the writer and a hand-built document of the
# stage-1 operators, renders both with libpdf and with pdftoppm (poppler) and compares every page within a
# tolerance, checks that qpdf's Flate-compressed copies render to the same pixels, and runs the corruption loop.
#   sh plan/ws079/tests/run-pdf-render.sh [FUZZ_ITERATIONS]
# Output: build/ws079-p006-host/ (pictures: notes-*, ops-* from libpdf, poppler-notes-*, poppler-ops-* from pdftoppm).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/ws079-p006-host
cc=${CC:-cc}
iterations=${1:-3000}
mkdir -p "$out/include"
ln -sfn "$(pwd)/include/libc/compat" "$out/include/compat"
ln -sf "$(pwd)/include/libc/pdf.h" "$out/include/pdf.h"
ln -sf "$(pwd)/include/libc/sha2.h" "$out/include/sha2.h"
# ws079-p008: the security handler (crypt.c) uses the C library's MD5, which openbsd-digest.c has with SHA-1.
ln -sf "$(pwd)/include/libc/md5.h" "$out/include/md5.h"
ln -sf "$(pwd)/include/libc/sha1.h" "$out/include/sha1.h"
mkdir -p "$out/include/truetype"
ln -sf "$(pwd)/userland/desktop/include/truetype/truetype.h" "$out/include/truetype/truetype.h"
convert -size 64x48 gradient:red-yellow -quality 90 "$out/test.jpg"
libpdf="userland/base/libpdf/writer.c userland/base/libpdf/outline.c userland/base/libpdf/object.c
	userland/base/libpdf/reader.c userland/base/libpdf/filter.c userland/base/libpdf/ccitt.c userland/base/libpdf/crypt.c userland/base/libpdf/image.c
	userland/base/libpdf/display.c userland/base/libpdf/content.c userland/base/libpdf/stroke.c
	userland/base/libpdf/raster.c userland/base/libpdf/font.c userland/base/libpdf/tounicode.c userland/base/libpdf/encoding.c
	userland/base/libpdf/shading.c
	userland/base/libpdf/charstrings.c userland/base/libpdf/type1.c userland/base/libpdf/cff.c userland/base/libpdf/cffdata.c"
status=0
for variant in plain asan ubsan; do
	flags="-std=c89 -pedantic -O1 -g -Wall -Wextra -Werror -D_DEFAULT_SOURCE -I$out/include"
	if [ "$variant" = asan ]; then
		flags="$flags -fsanitize=address -fno-omit-frame-pointer"
	fi
	if [ "$variant" = ubsan ]; then
		flags="$flags -fsanitize=undefined,float-cast-overflow -fno-sanitize-recover=all"
	fi
	loose=$(echo "$flags" | sed 's/-std=c89 -pedantic/-std=gnu11/; s/-Werror//')
	# The C library's SHA-256 and the compat libraries are not C89; they are compiled as they are for the host.
	objects=
	"$cc" $loose -c src/libc/openbsd-sha2.c -o "$out/sha2-$variant.o"
	"$cc" $loose -w -c src/libc/openbsd-digest.c -o "$out/digest-$variant.o"
	objects="$objects $out/digest-$variant.o"
	objects="$objects $out/sha2-$variant.o"
	for file in userland/base/libz-compat/*.c userland/base/libjpeg-compat/*.c; do
		object="$out/$(basename "$(dirname "$file")")-$(basename "$file" .c)-$variant.o"
		"$cc" $loose -w -I"$(dirname "$file")" -c "$file" -o "$object"
		objects="$objects $object"
	done
	# libtruetype (ws079-p007: libpdf draws text with it) is C11 with float; compiled on its own.
	for file in userland/desktop/libtruetype/*.c; do
		object="$out/truetype-$(basename "$file" .c)-$variant.o"
		"$cc" $loose -Werror -c "$file" -o "$object"
		objects="$objects $object"
	done
	"$cc" $flags -Wno-overlength-strings $libpdf plan/ws079/tests/host-pdf-render.c $objects -lm -o "$out/host-pdf-render-$variant"
done
# The documents.
"$out/host-pdf-render-plain" notes "$out/notes.pdf" "$out/test.jpg"
"$out/host-pdf-render-plain" ops "$out/ops.pdf"
qpdf --check "$out/notes.pdf" > "$out/qpdf-notes.txt" || { echo "qpdf: notes.pdf"; status=1; }
qpdf --check "$out/ops.pdf" > "$out/qpdf-ops.txt" || { echo "qpdf: ops.pdf"; status=1; }
# libpdf and poppler at two resolutions; every variant renders the same pixels.
for dpi in 72 150; do
	for doc in notes ops; do
		for variant in plain asan ubsan; do
			"$out/host-pdf-render-$variant" render "$out/$doc.pdf" "$out/$doc-$dpi-$variant" "$dpi" > "$out/$doc-$dpi-$variant.log"
		done
		cat "$out/$doc-$dpi-plain.log"
		pdftoppm -cropbox -r "$dpi" "$out/$doc.pdf" "$out/poppler-$doc-$dpi"
		for page in 1 2 3; do
			cmp "$out/$doc-$dpi-plain-$page.ppm" "$out/$doc-$dpi-asan-$page.ppm"
			cmp "$out/$doc-$dpi-plain-$page.ppm" "$out/$doc-$dpi-ubsan-$page.ppm"
			poppler=$(ls "$out"/poppler-$doc-$dpi-*$page.ppm | head -1)
			"$out/host-pdf-render-plain" compare "$out/$doc-$dpi-plain-$page.ppm" "$poppler" || status=1
		done
	done
done
# qpdf's Flate-compressed copies render to the same pixels.
for doc in notes ops; do
	qpdf --compress-streams=y --recompress-flate --object-streams=disable "$out/$doc.pdf" "$out/$doc-flate.pdf"
	"$out/host-pdf-render-plain" render "$out/$doc-flate.pdf" "$out/$doc-flate" 72 > "$out/$doc-flate.log"
	for page in 1 2 3; do
		cmp "$out/$doc-72-plain-$page.ppm" "$out/$doc-flate-$page.ppm" || status=1
	done
	echo "flate $doc: same pixels"
done
# The corruption loops.
for variant in plain asan ubsan; do
	n=$iterations
	[ "$variant" = plain ] || n=$((iterations / 3))
	/usr/bin/time -f "fuzz $variant: %e s, %M KiB" "$out/host-pdf-render-$variant" fuzz "$out/notes.pdf" "$n"
done
[ "$status" = 0 ] && echo "run-pdf-render: ok" || echo "run-pdf-render: FAILED"
exit "$status"
