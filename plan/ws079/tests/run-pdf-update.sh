#!/bin/sh
# ws079-p014: builds libpdf (with libz-compat and libjpeg-compat), host-pdf-update and host-pdf-render with the host's C
# compiler (plain, ASan and UBSan) and checks the update that adds a revision to another program's PDF:
#  1. the hand-made "foreign" PDF (host-pdf-update base) and qpdf's rewrite of it (Flate content, qpdf's own layout);
#  2. a revision added to each (host-pdf-update update: pages drawn over, added, kept; the edit data attached),
#     read back by libpdf (the original bytes untouched, pages, boxes, attachments, the /Prev link, hashes, dates),
#     qpdf --check, the attachment list, and the pages drawn by libpdf and by pdftoppm (poppler) within the tolerance of
#     ws079-p006, and the pixels of pdftoppm's pictures against the original's (the drawing where it was drawn, the
#     page's own content elsewhere, on the rotated page too);
#  3. a second revision on the updated file (host-pdf-update keep), and the refusals (host-pdf-update refusals).
#   sh plan/ws079/tests/run-pdf-update.sh
# Output: build/ws079-p014-host/ (base*.pdf, updated*.pdf, kept*.pdf, the pictures).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/ws079-p014-host
cc=${CC:-cc}
# Each run gets a new directory behind the fixed name (2026-10-06 user: deleting is Q1's step, so this script removes
# nothing; plan/tools/q1-clean.sh removes the old runs).
. plan/tools/fresh-out.sh
fresh_out "$out"
mkdir -p "$out/include" "$out/refusals"
ln -sfn "$(pwd)/include/libc/compat" "$out/include/compat"
ln -sf "$(pwd)/include/libc/pdf.h" "$out/include/pdf.h"
ln -sf "$(pwd)/include/libc/sha2.h" "$out/include/sha2.h"
# ws079-p008: the security handler (crypt.c) uses the C library's MD5, which openbsd-digest.c has with SHA-1.
ln -sf "$(pwd)/include/libc/md5.h" "$out/include/md5.h"
ln -sf "$(pwd)/include/libc/sha1.h" "$out/include/sha1.h"
ln -sf "$(pwd)/userland/desktop/keiland/truetype.h" "$out/include/truetype.h"
# ws079-p007: the page interpreter draws text (font.c, encoding.c, libtruetype) and shadings (shading.c).
libpdf="userland/base/libpdf/writer.c userland/base/libpdf/update.c userland/base/libpdf/outline.c
	userland/base/libpdf/object.c userland/base/libpdf/reader.c userland/base/libpdf/filter.c userland/base/libpdf/ccitt.c userland/base/libpdf/crypt.c
	userland/base/libpdf/image.c userland/base/libpdf/display.c userland/base/libpdf/content.c
	userland/base/libpdf/stroke.c userland/base/libpdf/raster.c userland/base/libpdf/font.c userland/base/libpdf/tounicode.c
	userland/base/libpdf/encoding.c userland/base/libpdf/shading.c
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
	for file in userland/desktop/libtruetype/*.c; do
		object="$out/truetype-$(basename "$file" .c)-$variant.o"
		"$cc" $loose -Werror -c "$file" -o "$object"
		objects="$objects $object"
	done
	"$cc" $flags -Wno-overlength-strings $libpdf plan/ws079/tests/host-pdf-update.c $objects -lm -o "$out/host-pdf-update-$variant"
	"$cc" $flags -Wno-overlength-strings $libpdf plan/ws079/tests/host-pdf-render.c $objects -lm -o "$out/host-pdf-render-$variant"
done

# 1. The foreign documents: the hand-made one and qpdf's rewrite of it with Flate content.
"$out/host-pdf-update-plain" base "$out/base.pdf"
qpdf --check "$out/base.pdf" > "$out/qpdf-base.txt" || { echo "qpdf: base.pdf"; status=1; }
qpdf --object-streams=disable --compress-streams=y --recompress-flate "$out/base.pdf" "$out/base-qpdf.pdf"
qpdf --check "$out/base-qpdf.pdf" > "$out/qpdf-base-qpdf.txt" || { echo "qpdf: base-qpdf.pdf"; status=1; }
grep -c FlateDecode "$out/base-qpdf.pdf" > /dev/null || { echo "base-qpdf.pdf has no Flate stream"; status=1; }

# 2. A revision on each, by every variant (the plain one's results are checked with the outside tools).
for base in base base-qpdf; do
	for variant in plain asan ubsan; do
		"$out/host-pdf-update-$variant" update "$out/$base.pdf" "$out/updated-$base-$variant.pdf" > "$out/update-$base-$variant.log" ||
		    { cat "$out/update-$base-$variant.log"; echo "update $base $variant FAILED"; status=1; }
	done
	cat "$out/update-$base-plain.log"
	updated="$out/updated-$base-plain.pdf"
	qpdf --check "$updated" > "$out/qpdf-updated-$base.txt" 2>&1 || { cat "$out/qpdf-updated-$base.txt"; echo "qpdf: $updated"; status=1; }
	grep -q "WARNING" "$out/qpdf-updated-$base.txt" && { echo "qpdf warned about $updated"; status=1; }
	grep -q "No syntax or stream encoding errors found" "$out/qpdf-updated-$base.txt" || { echo "qpdf: no clean result"; status=1; }
	pdfinfo "$updated" > "$out/pdfinfo-$base.txt"
	grep -E "^Pages: +5$" "$out/pdfinfo-$base.txt" > /dev/null || { echo "pdfinfo: not 5 pages"; status=1; }
	qpdf --list-attachments "$updated" > "$out/attachments-$base.txt"
	cat "$out/attachments-$base.txt"
	grep -q "kei-notes.bin" "$out/attachments-$base.txt" || { echo "no kei-notes.bin"; status=1; }
	grep -q "readme.txt" "$out/attachments-$base.txt" || { echo "readme.txt lost"; status=1; }
	# libpdf and poppler draw every page of the updated file alike.
	"$out/host-pdf-render-plain" render "$updated" "$out/libpdf-$base" 72 > "$out/render-$base.log"
	pdftoppm -cropbox -r 72 "$updated" "$out/poppler-$base"
	for page in 1 2 3 4 5; do
		"$out/host-pdf-render-plain" compare "$out/libpdf-$base-$page.ppm" "$out/poppler-$base-$page.ppm" || status=1
	done
	# poppler's pictures against the original's: the drawing where it was drawn, the page's own content elsewhere.
	pdftoppm -cropbox -r 72 -png "$out/$base.pdf" "$out/original-$base"
	pdftoppm -cropbox -r 72 -png "$updated" "$out/updated-$base"
	python3 plan/ws079/tests/update-pixels.py 72 "$out/original-$base-1.png" "$out/updated-$base-1.png" \
	    35,20:changed:0000ff 136,56:changed 350,250:same 300,120:same || status=1
	python3 plan/ws079/tests/update-pixels.py 72 "$out/original-$base-2.png" "$out/updated-$base-3.png" \
	    35,20:changed:000000 176,146:changed:1a33cc 250,330:same 150,200:same || status=1
	python3 plan/ws079/tests/update-pixels.py 72 "$out/original-$base-3.png" "$out/updated-$base-4.png" \
	    146,296:changed:000000 250,50:same || status=1
done

# 3. A second revision on the updated file, which replaces the edit data; and the refusals.
for variant in plain asan ubsan; do
	"$out/host-pdf-update-$variant" keep "$out/updated-base-plain.pdf" "$out/kept-$variant.pdf" > "$out/keep-$variant.log" ||
	    { cat "$out/keep-$variant.log"; echo "keep $variant FAILED"; status=1; }
	mkdir -p "$out/refusals/$variant"
	"$out/host-pdf-update-$variant" refusals "$out/refusals/$variant" > "$out/refusals-$variant.log" ||
	    { cat "$out/refusals-$variant.log"; echo "refusals $variant FAILED"; status=1; }
done
# ws079-p008: a document encrypted with an empty user password opens, and the update refuses it.
qpdf --encrypt --user-password= --owner-password=owner --bits=128 --use-aes=y -- "$out/base.pdf" "$out/base-encrypted.pdf"
for variant in plain asan ubsan; do
	"$out/host-pdf-update-$variant" encrypted "$out/base-encrypted.pdf" > "$out/encrypted-$variant.log" ||
	    { cat "$out/encrypted-$variant.log"; echo "encrypted $variant FAILED"; status=1; }
done
cat "$out/keep-plain.log" "$out/refusals-plain.log" "$out/encrypted-plain.log"
qpdf --check "$out/kept-plain.pdf" > "$out/qpdf-kept.txt" 2>&1 || { cat "$out/qpdf-kept.txt"; echo "qpdf: kept"; status=1; }
qpdf --list-attachments "$out/kept-plain.pdf" > "$out/attachments-kept.txt"
[ "$(grep -c 'kei-notes.bin' "$out/attachments-kept.txt")" = 1 ] || { echo "kept: not one kei-notes.bin"; status=1; }
cmp -n "$(wc -c < "$out/updated-base-plain.pdf")" "$out/updated-base-plain.pdf" "$out/kept-plain.pdf" || { echo "kept: first revision changed"; status=1; }
pdftoppm -cropbox -r 72 -png "$out/kept-plain.pdf" "$out/kept"
for page in 1 2 3 4 5; do
	compare -metric AE "$out/updated-base-$page.png" "$out/kept-$page.png" null: 2> "$out/kept-ae-$page.txt" || true
	[ "$(cat "$out/kept-ae-$page.txt")" = 0 ] || { echo "kept page $page differs"; status=1; }
done

if [ "$status" = 0 ]; then
	echo "run-pdf-update: ok"
else
	echo "run-pdf-update: FAILED"
fi
exit "$status"
