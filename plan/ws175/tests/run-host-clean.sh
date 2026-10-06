#!/bin/sh
# ws175-p009: builds libpdf (with libz-compat, libjpeg-compat and libtruetype) and host-clean with the host's C compiler
# (plain, ASan and UBSan), writes make-clean-sample.py's sample and puts it into object streams with qpdf, runs
# host-clean with each build, and checks the clean copy with qpdf --check and against the update it was made from:
# the same text (pdftotext) and the same pixels (pdftoppm).
#   sh plan/ws175/tests/run-host-clean.sh [OUTPUT]   (default build/ws175-host-clean)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws175-host-clean}
cc=${CC:-cc}
mkdir -p "$out/include"
ln -sfn "$(pwd)/include/libc/compat" "$out/include/compat"
ln -sf "$(pwd)/include/libc/pdf.h" "$out/include/pdf.h"
ln -sf "$(pwd)/include/libc/sha2.h" "$out/include/sha2.h"
ln -sf "$(pwd)/include/libc/md5.h" "$out/include/md5.h"
ln -sf "$(pwd)/include/libc/sha1.h" "$out/include/sha1.h"
ln -sfn "$(pwd)/userland/desktop/include/truetype" "$out/include/truetype"
python3 plan/ws175/tests/make-clean-sample.py "$out/sample-plain.pdf"
qpdf --object-streams=generate --compress-streams=n "$out/sample-plain.pdf" "$out/clean-sample.pdf"
libpdf=$(ls userland/base/libpdf/*.c)
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
	# The C library's digests and the compat libraries are not C89; they are compiled as they are for the host.
	objects=
	"$cc" $loose -c src/libc/openbsd-sha2.c -o "$out/sha2-$variant.o"
	"$cc" $loose -w -c src/libc/openbsd-digest.c -o "$out/digest-$variant.o"
	objects="$objects $out/sha2-$variant.o $out/digest-$variant.o"
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
	# shellcheck disable=SC2086
	"$cc" $flags -Wno-overlength-strings -Iuserland/base/libpdf $libpdf plan/ws175/tests/host-clean.c $objects -lm \
		-o "$out/host-clean-$variant"
	if "$out/host-clean-$variant" "$out/clean-sample.pdf" "$out/updated-$variant.pdf" "$out/clean-$variant.pdf" > "$out/clean-$variant.txt" 2>&1; then
		echo "host-clean $variant: $(tail -1 "$out/clean-$variant.txt")"
	else
		grep -v '^ok' "$out/clean-$variant.txt"
		status=1
		continue
	fi
	if ! qpdf --check "$out/clean-$variant.pdf" > "$out/qpdf-$variant.txt" 2>&1; then
		echo "qpdf --check $variant: FAIL"
		cat "$out/qpdf-$variant.txt"
		status=1
	fi
	pdftotext -layout "$out/updated-$variant.pdf" "$out/updated-$variant.txt"
	pdftotext -layout "$out/clean-$variant.pdf" "$out/clean-text-$variant.txt"
	if ! cmp -s "$out/updated-$variant.txt" "$out/clean-text-$variant.txt"; then
		echo "pdftotext $variant: the clean copy's text differs"
		status=1
	fi
	pdftoppm -r 72 "$out/updated-$variant.pdf" "$out/updated-$variant"
	pdftoppm -r 72 "$out/clean-$variant.pdf" "$out/clean-$variant"
	for page in 1 2; do
		if ! cmp -s "$out/updated-$variant-$page.ppm" "$out/clean-$variant-$page.ppm"; then
			echo "pdftoppm $variant page $page: the clean copy draws differently"
			status=1
		fi
	done
done
if [ "$status" = 0 ]; then
	echo "run-host-clean: PASS"
else
	echo "run-host-clean: FAIL"
fi
exit "$status"
