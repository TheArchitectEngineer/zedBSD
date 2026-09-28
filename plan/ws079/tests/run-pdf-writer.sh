#!/bin/sh
# ws079-p004: builds libpdf's writer with the host's C compiler (plain, ASan and UBSan), writes a
# document of pressure strokes with host-pdf-writer and validates it with qpdf --check, pdfinfo and the attachment list.
#   sh plan/ws079/tests/run-pdf-writer.sh
# Output: build/ws079-host/ (the public header is linked into build/ws079-host/include, since the
# host's C library headers must not be replaced by include/libc).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/ws079-host
cc=${CC:-cc}
mkdir -p "$out/include"
convert -size 64x48 gradient:red-yellow -quality 90 "$out/test.jpg"
ln -sf "$(pwd)/include/libc/pdf.h" "$out/include/pdf.h"
for variant in plain asan ubsan; do
	flags="-std=c89 -pedantic -O1 -g -Wall -Wextra -Werror -D_DEFAULT_SOURCE -I$out/include"
	if [ "$variant" = asan ]; then
		flags="$flags -fsanitize=address -fno-omit-frame-pointer"
	fi
	if [ "$variant" = ubsan ]; then
		flags="$flags -fsanitize=undefined,float-cast-overflow -fno-sanitize-recover=all"
	fi
	"$cc" $flags userland/base/libpdf/writer.c userland/base/libpdf/outline.c plan/ws079/tests/host-pdf-writer.c -lm \
	    -o "$out/host-pdf-writer-$variant"
	"$out/host-pdf-writer-$variant" "$out/writer-$variant.pdf" "$out/test.jpg"
done
cmp "$out/writer-plain.pdf" "$out/writer-asan.pdf"
cmp "$out/writer-plain.pdf" "$out/writer-ubsan.pdf"
qpdf --check "$out/writer-plain.pdf"
pdfinfo "$out/writer-plain.pdf"
qpdf --list-attachments "$out/writer-plain.pdf"
qpdf --show-attachment=zedbsd-notes.bin "$out/writer-plain.pdf" | od -An -tx1
if command -v pdftoppm >/dev/null 2>&1; then
	pdftoppm -r 72 -png -f 1 -l 1 -singlefile "$out/writer-plain.pdf" "$out/page1"
	pdftoppm -r 72 -png -f 2 -l 2 -singlefile "$out/writer-plain.pdf" "$out/page2"
fi
# The trailer's identifier and the information dictionary's dates.
grep -a '/ID \[' "$out/writer-plain.pdf"
grep -a '/CreationDate' "$out/writer-plain.pdf"
echo "run-pdf-writer: ok"
