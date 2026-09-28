#!/bin/sh
# ws079-p005: builds Notes' model, edit data, journal and PDF saving with the host's C compiler
# (plain, ASan and UBSan) against libpdf's writer and reader, runs host-notes (which also opens the saved PDF
# again), and checks the saved PDF with qpdf --check, the attachment list and pdftoppm (the page 1 picture is
# build/ws079-p005-host/page1.png).
#   sh plan/ws079/tests/run-notes-host.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/ws079-p005-host
cc=${CC:-cc}
rm -rf "$out"
mkdir -p "$out/include"
ln -sf "$(pwd)/include/libc/pdf.h" "$out/include/pdf.h"
ln -sf "$(pwd)/include/libc/sha2.h" "$out/include/sha2.h"
sources="userland/desktop/notes/document.c userland/desktop/notes/encode.c userland/desktop/notes/journal.c
	userland/desktop/notes/save.c userland/base/libpdf/writer.c userland/base/libpdf/outline.c
	userland/base/libpdf/object.c userland/base/libpdf/reader.c plan/ws079/tests/host-notes.c"
for variant in plain asan ubsan; do
	flags="-std=c99 -pedantic -O1 -g -Wall -Wextra -Werror -D_DEFAULT_SOURCE -I$out/include -Iuserland/desktop/notes"
	if [ "$variant" = asan ]; then
		flags="$flags -fsanitize=address -fno-omit-frame-pointer"
	fi
	if [ "$variant" = ubsan ]; then
		flags="$flags -fsanitize=undefined,float-cast-overflow -fno-sanitize-recover=all"
	fi
	# The C library's SHA-256 (the writer's and the reader's content hashes), compiled for the host.
	"$cc" $(echo "$flags" | sed 's/-std=c99 -pedantic/-std=gnu99/') -c src/libc/openbsd-sha2.c -o "$out/sha2-$variant.o"
	"$cc" $flags $sources "$out/sha2-$variant.o" -lm -o "$out/host-notes-$variant"
	timeout 60 "$out/host-notes-$variant" "$out/notes-$variant.pdf" "$out/scratch-$variant.pdf"
done
cmp "$out/notes-plain.pdf.bin" "$out/notes-asan.pdf.bin"
cmp "$out/notes-plain.pdf.bin" "$out/notes-ubsan.pdf.bin"
qpdf --check "$out/notes-plain.pdf"
pdfinfo "$out/notes-plain.pdf"
qpdf --list-attachments "$out/notes-plain.pdf"
qpdf --show-attachment=kei-notes.bin "$out/notes-plain.pdf" > "$out/attached.bin"
# The attached edit data is the document encoded at the save (with the pages' content hashes): same size, magic ZNOT.
[ "$(head -c 4 "$out/attached.bin")" = ZNOT ]
[ "$(wc -c < "$out/attached.bin")" = "$(wc -c < "$out/notes-plain.pdf.bin")" ]
pdftoppm -r 60 -png -f 1 -l 1 -singlefile "$out/notes-plain.pdf" "$out/page1"
pdftoppm -r 60 -png -f 2 -l 2 -singlefile "$out/notes-plain.pdf" "$out/page2"
echo "run-notes-host: ok"
