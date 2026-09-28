#!/bin/sh
# ws079-p004: builds libpdf's reader with the host's C compiler (plain, ASan and UBSan) and runs host-pdf-reader:
# the write-read round trip, an appended revision, hand-made files for each refusal and limit, and the truncation
# and corruption loops.  Then checks the round-trip file with qpdf --check and compares the reader's page content
# hashes with sha256sum of qpdf's raw stream data (the writer numbers page n's content stream 3 + 2n).
#   sh plan/ws079/tests/run-pdf-reader.sh
# Output: build/ws079-host/ (see run-pdf-writer.sh for the header links and the C library's SHA-256).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/ws079-host
cc=${CC:-cc}
mkdir -p "$out/include"
ln -sf "$(pwd)/include/libc/pdf.h" "$out/include/pdf.h"
ln -sf "$(pwd)/include/libc/sha2.h" "$out/include/sha2.h"
sources="userland/base/libpdf/writer.c userland/base/libpdf/outline.c userland/base/libpdf/object.c userland/base/libpdf/reader.c"
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
	"$cc" $flags $sources plan/ws079/tests/host-pdf-reader.c "$out/sha2-$variant.o" -lm -o "$out/host-pdf-reader-$variant"
	/usr/bin/time -f "$variant: %e s, %M KiB" "$out/host-pdf-reader-$variant" "$out/reader-$variant.pdf" \
	    > "$out/reader-$variant.log"
	cat "$out/reader-$variant.log"
done
cmp "$out/reader-plain.pdf" "$out/reader-asan.pdf"
cmp "$out/reader-plain.pdf" "$out/reader-ubsan.pdf"
qpdf --check "$out/reader-plain.pdf"
qpdf --list-attachments "$out/reader-plain.pdf"
# The reader's hashes against qpdf's view of each page's content stream.
for page in 1 2 3; do
	object=$((3 + 2 * page))
	expected=$(qpdf --show-object=$object --raw-stream-data "$out/reader-plain.pdf" | sha256sum | cut -d' ' -f1)
	got=$(grep "^page $page sha256 " "$out/reader-plain.log" | cut -d' ' -f4)
	echo "page $page: qpdf $expected reader $got"
	[ "$expected" = "$got" ]
done
echo "run-pdf-reader: ok"
