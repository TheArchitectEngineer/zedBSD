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
# Each run gets a new directory behind the fixed name (2026-10-06 user: deleting is Q1's step, so this script removes
# nothing; plan/tools/q1-clean.sh removes the old runs).
. plan/tools/fresh-out.sh
fresh_out "$out"
mkdir -p "$out/include"
ln -sf "$(pwd)/include/libc/pdf.h" "$out/include/pdf.h"
ln -sf "$(pwd)/include/libc/sha2.h" "$out/include/sha2.h"
# ws079-p008: the security handler (crypt.c) uses the C library's MD5, which openbsd-digest.c has with SHA-1.
ln -sf "$(pwd)/include/libc/md5.h" "$out/include/md5.h"
ln -sf "$(pwd)/include/libc/sha1.h" "$out/include/sha1.h"
ln -sfn "$(pwd)/include/libc/compat" "$out/include/compat"
ln -sf "$(pwd)/userland/desktop/keiland/truetype.h" "$out/include/truetype.h"
# ws079-p007: the reader decodes cross-reference and object streams through filter.c and libz-compat.
# ws175-p007: the edits of the PDF's objects (edit.c) use libpdf's editor, so the whole of libpdf (with libjpeg-compat
# and libtruetype) is built.
sources="userland/desktop/notes/document.c userland/desktop/notes/edit.c userland/desktop/notes/encode.c userland/desktop/notes/journal.c
	userland/desktop/notes/save.c userland/base/libpdf/writer.c userland/base/libpdf/update.c userland/base/libpdf/outline.c
	userland/base/libpdf/object.c userland/base/libpdf/reader.c userland/base/libpdf/filter.c userland/base/libpdf/ccitt.c userland/base/libpdf/crypt.c
	userland/base/libpdf/image.c userland/base/libpdf/display.c userland/base/libpdf/content.c userland/base/libpdf/editor.c
	userland/base/libpdf/tounicode.c userland/base/libpdf/intake.c userland/base/libpdf/replace.c userland/base/libpdf/embed.c userland/base/libpdf/subset.c userland/base/libpdf/stroke.c userland/base/libpdf/raster.c
	userland/base/libpdf/font.c userland/base/libpdf/encoding.c userland/base/libpdf/shading.c userland/base/libpdf/charstrings.c
	userland/base/libpdf/type1.c userland/base/libpdf/cff.c userland/base/libpdf/cffdata.c plan/ws079/tests/host-notes.c"
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
	"$cc" $(echo "$flags" | sed 's/-std=c99 -pedantic/-std=gnu99/; s/-Werror//') -w -c src/libc/openbsd-digest.c -o "$out/digest-$variant.o"
	zlib=
	for file in userland/base/libz-compat/*.c userland/base/libjpeg-compat/*.c userland/desktop/libtruetype/*.c; do
		object="$out/$(basename "$(dirname "$file")")-$(basename "$file" .c)-$variant.o"
		"$cc" $(echo "$flags" | sed 's/-std=c99 -pedantic/-std=gnu11/; s/-Werror//') -w -I"$(dirname "$file")" -c "$file" -o "$object"
		zlib="$zlib $object"
	done
	"$cc" $flags -Wno-overlength-strings $sources "$out/sha2-$variant.o" "$out/digest-$variant.o" $zlib -lm -o "$out/host-notes-$variant"
	timeout 60 "$out/host-notes-$variant" "$out/notes-$variant.pdf" "$out/scratch-$variant.pdf"
done
# ws079-p008: a PDF encrypted with an empty user password opens in the reader, and Notes still refuses it.
qpdf --encrypt --user-password= --owner-password=owner --bits=256 -- "$out/notes-plain.pdf" "$out/notes-encrypted.pdf"
for variant in plain asan ubsan; do
	timeout 60 "$out/host-notes-$variant" encrypted "$out/notes-encrypted.pdf"
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
# ws079-p014: the notebooks saved as a revision of another program's PDF (the foreign one, the changed notebook and
# the foreign one with a third-party revision after Notes') are clean for qpdf, carry one kei-notes.bin, and draw.
for kind in foreign changed third; do
	qpdf --check "$out/scratch-plain.pdf-$kind.pdf" > "$out/qpdf-$kind.txt" 2>&1 || { cat "$out/qpdf-$kind.txt"; exit 1; }
	grep -q "No syntax or stream encoding errors found" "$out/qpdf-$kind.txt"
	! grep -q WARNING "$out/qpdf-$kind.txt"
	[ "$(qpdf --list-attachments "$out/scratch-plain.pdf-$kind.pdf" | grep -c kei-notes.bin)" = 1 ]
	pdftoppm -r 60 -png "$out/scratch-plain.pdf-$kind.pdf" "$out/$kind"
done
echo "run-notes-host: ok"
