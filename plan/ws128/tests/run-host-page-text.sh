#!/bin/sh
# ws128-p004: builds libpdf (with libz-compat and libtruetype) and host-page-text.c with the host's C compiler (plain, ASan
# and UBSan), writes the WS175 samples (plan/ws175/tests/make-edit-samples.py) and runs the test on them; libpdf's changed
# files are also compiled as C89 -pedantic.
#   sh plan/ws128/tests/run-host-page-text.sh [OUTPUT]   (default build/ws128-page-text)
# Each run gets a new directory behind OUTPUT (plan/tools/fresh-out.sh); nothing is removed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws128-page-text}
cc=${CC:-cc}
. plan/tools/fresh-out.sh
fresh_out "$out"
mkdir -p "$out/include"
ln -sfn "$(pwd)/include/libc/compat" "$out/include/compat"
for header in pdf.h sha2.h md5.h sha1.h; do
	ln -sf "$(pwd)/include/libc/$header" "$out/include/$header"
done
ln -sf "$(pwd)/userland/desktop/keiland/truetype.h" "$out/include/truetype.h"
sources=$(ls userland/base/libpdf/*.c)
status=0
# The files this Phase changed, as the zedBSD build compiles libpdf (C89).
for file in userland/base/libpdf/content.c userland/base/libpdf/editor.c; do
	"$cc" -std=c89 -pedantic -Wall -Wextra -Werror -Wno-long-long -Wno-overlength-strings -D_DEFAULT_SOURCE -I"$out/include" \
		-fsyntax-only "$file" || status=1
done
python3 plan/ws175/tests/make-edit-samples.py "$out/samples" >/dev/null
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
	for file in userland/base/libz-compat/*.c userland/base/libjpeg-compat/*.c userland/desktop/libtruetype/*.c; do
		object="$out/$(basename "$(dirname "$file")")-$(basename "$file" .c)-$variant.o"
		"$cc" $loose -w -I"$(dirname "$file")" -c "$file" -o "$object"
		objects="$objects $object"
	done
	# shellcheck disable=SC2086
	"$cc" $flags $sources plan/ws128/tests/host-page-text.c $objects -lm -o "$out/host-page-text-$variant"
	if "$out/host-page-text-$variant" "$out/samples" > "$out/page-text-$variant.txt" 2>&1; then
		echo "host-page-text $variant: $(tail -1 "$out/page-text-$variant.txt")"
	else
		grep -v '^ok' "$out/page-text-$variant.txt"
		status=1
	fi
done
[ $status -eq 0 ] && echo "run-host-page-text: PASS" || echo "run-host-page-text: FAIL"
exit $status
