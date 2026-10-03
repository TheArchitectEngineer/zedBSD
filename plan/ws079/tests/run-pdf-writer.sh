#!/bin/sh
# ws079-p004: builds libpdf's writer and pdf_outline_stroke() with the host's C compiler (plain, ASan and UBSan),
# writes a document of pressure strokes with host-pdf-writer and validates it with qpdf --check, pdfinfo and the
# attachment list, then checks the outline's smoothing and round joins with host-pdf-outline (sharp zigzags and
# hairpins) and renders both documents with pdftoppm.
#   sh plan/ws079/tests/run-pdf-writer.sh
# Output: build/ws079-host/ (the public header and the C library's sha2.h are linked into build/ws079-host/include,
# since the host's C library headers must not be replaced by include/libc; the C library's SHA-256 is compiled
# for the host from src/libc/openbsd-sha2.c).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/ws079-host
cc=${CC:-cc}
mkdir -p "$out/include"
convert -size 64x48 gradient:red-yellow -quality 90 "$out/test.jpg"
ln -sf "$(pwd)/include/libc/pdf.h" "$out/include/pdf.h"
ln -sf "$(pwd)/include/libc/sha2.h" "$out/include/sha2.h"
for variant in plain asan ubsan; do
	flags="-std=c89 -pedantic -O1 -g -Wall -Wextra -Werror -D_DEFAULT_SOURCE -I$out/include"
	if [ "$variant" = asan ]; then
		flags="$flags -fsanitize=address -fno-omit-frame-pointer"
	fi
	if [ "$variant" = ubsan ]; then
		flags="$flags -fsanitize=undefined,float-cast-overflow -fno-sanitize-recover=all"
	fi
	# The C library's SHA-256 uses long long constants, which C89 does not have.
	"$cc" $(echo "$flags" | sed 's/-std=c89 -pedantic/-std=gnu99/') -c src/libc/openbsd-sha2.c -o "$out/sha2-$variant.o"
	"$cc" $flags userland/base/libpdf/writer.c userland/base/libpdf/outline.c plan/ws079/tests/host-pdf-writer.c \
	    "$out/sha2-$variant.o" -lm -o "$out/host-pdf-writer-$variant"
	"$out/host-pdf-writer-$variant" "$out/writer-$variant.pdf" "$out/test.jpg"
	"$cc" $flags userland/base/libpdf/writer.c userland/base/libpdf/outline.c plan/ws079/tests/host-pdf-outline.c \
	    "$out/sha2-$variant.o" -lm -o "$out/host-pdf-outline-$variant"
	"$out/host-pdf-outline-$variant" "$out/outline-$variant.pdf"
done
cmp "$out/writer-plain.pdf" "$out/writer-asan.pdf"
cmp "$out/writer-plain.pdf" "$out/writer-ubsan.pdf"
cmp "$out/outline-plain.pdf" "$out/outline-asan.pdf"
cmp "$out/outline-plain.pdf" "$out/outline-ubsan.pdf"
qpdf --check "$out/writer-plain.pdf"
qpdf --check "$out/outline-plain.pdf"
pdfinfo "$out/writer-plain.pdf"
qpdf --list-attachments "$out/writer-plain.pdf"
qpdf --show-attachment=zedbsd-notes.bin "$out/writer-plain.pdf" | od -An -tx1
if command -v pdftoppm >/dev/null 2>&1; then
	pdftoppm -r 72 -png -f 1 -l 1 -singlefile "$out/writer-plain.pdf" "$out/page1"
	pdftoppm -r 72 -png -f 2 -l 2 -singlefile "$out/writer-plain.pdf" "$out/page2"
	pdftoppm -r 110 -png -singlefile "$out/outline-plain.pdf" "$out/outline"
fi
# The trailer's identifier and the information dictionary's dates.
grep -a '/ID \[' "$out/writer-plain.pdf"
grep -a '/CreationDate' "$out/writer-plain.pdf"
echo "run-pdf-writer: ok"
