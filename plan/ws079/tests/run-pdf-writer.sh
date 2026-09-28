#!/bin/sh
# ws079-p004: builds libpdf's writer with the host's C compiler (plain and ASan/UBSan), writes a
# document with host-pdf-writer and validates it with qpdf --check, pdfinfo and the attachment list.
#   sh plan/ws079/tests/run-pdf-writer.sh
# Output: build/ws079-host/ (the public header is linked into build/ws079-host/include, since the
# host's C library headers must not be replaced by include/libc).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/ws079-host
cc=${CC:-cc}
mkdir -p "$out/include"
ln -sf "$(pwd)/include/libc/pdf.h" "$out/include/pdf.h"
for variant in plain asan; do
	flags="-std=c89 -pedantic -O1 -g -Wall -Wextra -Werror -D_DEFAULT_SOURCE -I$out/include"
	if [ "$variant" = asan ]; then
		flags="$flags -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined"
	fi
	"$cc" $flags userland/base/libpdf/writer.c plan/ws079/tests/host-pdf-writer.c -o "$out/host-pdf-writer-$variant"
	"$out/host-pdf-writer-$variant" "$out/writer-$variant.pdf"
done
cmp "$out/writer-plain.pdf" "$out/writer-asan.pdf"
qpdf --check "$out/writer-plain.pdf"
pdfinfo "$out/writer-plain.pdf"
qpdf --list-attachments "$out/writer-plain.pdf"
qpdf --show-attachment=zedbsd-notes.bin "$out/writer-plain.pdf" | od -An -tx1
if command -v pdftoppm >/dev/null 2>&1; then
	pdftoppm -r 36 -png -f 1 -l 1 "$out/writer-plain.pdf" "$out/page"
fi
echo "run-pdf-writer: ok"
