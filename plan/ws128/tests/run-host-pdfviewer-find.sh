#!/bin/sh
# ws128-p004: builds PDF Viewer's core with find.c and the whole of libpdf (C89 -pedantic, as the zedBSD build) for the
# host (plain, ASan+UBSan), writes WS175's edit-basic.pdf (plan/ws175/tests/make-edit-samples.py) and runs
# host-pdfviewer-find on it; the frames with the marks are written as PNG.
#   sh plan/ws128/tests/run-host-pdfviewer-find.sh [OUTPUT]   (default build/ws128-pdfviewer-find)
# Each run gets a new directory behind OUTPUT (plan/tools/fresh-out.sh); nothing is removed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws128-pdfviewer-find}
cc=${CC:-cc}
font=userland/desktop/fonts/Mahora-Regular.ttf
. plan/tools/fresh-out.sh
fresh_out "$out"
mkdir -p "$out/include"
ln -sfn "$(pwd)/include/libc/compat" "$out/include/compat"
for header in pdf.h sha2.h md5.h sha1.h; do
	ln -sf "$(pwd)/include/libc/$header" "$out/include/$header"
done
mkdir -p "$out/include/truetype"
ln -sf "$(pwd)/userland/desktop/include/truetype/truetype.h" "$out/include/truetype/truetype.h"
python3 plan/ws175/tests/make-edit-samples.py "$out/samples" >/dev/null
libpdf=$(ls userland/base/libpdf/*.c)
viewer="userland/desktop/pdfviewer/view.c userland/desktop/pdfviewer/draw.c userland/desktop/pdfviewer/document.c
	userland/desktop/pdfviewer/canvas.c userland/desktop/pdfviewer/text.c userland/desktop/pdfviewer/find.c"
status=0
for variant in plain asan; do
	flags="-std=c89 -pedantic -O1 -g -Wall -Wextra -Werror -Wno-long-long -Wno-overlength-strings -D_DEFAULT_SOURCE -I. -I$out/include"
	if [ "$variant" = asan ]; then
		flags="$flags -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"
	fi
	loose=$(echo "$flags" | sed 's/-std=c89 -pedantic/-std=gnu11/; s/-Werror//')
	objects="$out/sha2-$variant.o $out/digest-$variant.o"
	"$cc" $loose -c src/libc/openbsd-sha2.c -o "$out/sha2-$variant.o"
	"$cc" $loose -w -c src/libc/openbsd-digest.c -o "$out/digest-$variant.o"
	for file in userland/base/libz-compat/*.c userland/base/libjpeg-compat/*.c userland/desktop/libtruetype/*.c; do
		object="$out/$(basename "$(dirname "$file")")-$(basename "$file" .c)-$variant.o"
		"$cc" $loose -w -I"$(dirname "$file")" -c "$file" -o "$object"
		objects="$objects $object"
	done
	# shellcheck disable=SC2086
	"$cc" $flags $libpdf $viewer plan/ws128/tests/host-pdfviewer-find.c $objects -lm -o "$out/host-pdfviewer-find-$variant"
	mkdir -p "$out/frames-$variant"
	if "$out/host-pdfviewer-find-$variant" "$font" "$out/samples/edit-basic.pdf" "$out/frames-$variant" > "$out/find-$variant.log" 2>&1; then
		echo "host-pdfviewer-find $variant: $(grep '^host-pdfviewer-find' "$out/find-$variant.log")"
	else
		grep -E "^(FAILED|host-pdfviewer-find)" "$out/find-$variant.log" || tail -5 "$out/find-$variant.log"
		status=1
	fi
done
for picture in "$out"/frames-plain/*.ppm; do
	convert "$picture" "${picture%.ppm}.png"
done
[ $status -eq 0 ] && echo "run-host-pdfviewer-find: PASS" || echo "run-host-pdfviewer-find: FAIL"
exit $status
