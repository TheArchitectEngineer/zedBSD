#!/bin/sh
# ws081-p012: builds PDF Viewer's core (view, frame, document cache, chooser, canvas, text; C89 as in
# plan/ws079/tests/run-pdfviewer-host.sh), its touch screen (touch.c) and libkeiland's motion, scroller and gestures
# for the host (plain and ASan), makes an eight-page test document, and runs host-pdftouch on it.  The frames go to
# OUTDIR/frames-*/ as PPM, and as PNG for the plain build.
#   plan/ws081/tests/run-pdftouch.sh [OUTDIR] [FONT]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws081-p012-host}
font=${2:-userland/desktop/fonts/Inter.ttf}
cc=${CC:-clang}
mkdir -p "$out/include"
ln -sfn "$root/include/libc/compat" "$out/include/compat"
for header in pdf.h sha2.h md5.h sha1.h; do
	ln -sf "$root/include/libc/$header" "$out/include/$header"
done
for header in truetype.h keiland.h; do
	ln -sf "$root/userland/desktop/keiland/$header" "$out/include/$header"
done

# The test document: eight Letter pages, each with bands of colour and as many squares as its number.
python3 "$root/plan/ws081/tests/make-touch-pdf.py" "$out/touch.pdf"

libpdf="writer.c outline.c object.c reader.c filter.c ccitt.c crypt.c image.c display.c content.c stroke.c raster.c
	font.c encoding.c shading.c charstrings.c type1.c cff.c cffdata.c"
viewer="view.c draw.c document.c chooser.c canvas.c text.c"
status=0
for variant in plain asan; do
	strict="-std=c89 -pedantic -O1 -g -Wall -Wextra -Werror -D_DEFAULT_SOURCE -I$out/include"
	modern="-std=gnu11 -O1 -g -Wall -Wextra -Werror -D_DEFAULT_SOURCE -I$out/include"
	loose="-std=gnu11 -O1 -g -w -D_DEFAULT_SOURCE -I$out/include"
	if [ "$variant" = asan ]; then
		sanitize="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"
		strict="$strict $sanitize"
		modern="$modern $sanitize"
		loose="$loose $sanitize"
	fi
	build="$out/obj-$variant"
	mkdir -p "$build"
	"$cc" $loose -c "$root/src/libc/openbsd-sha2.c" -o "$build/sha2.o"
	"$cc" $loose -c "$root/src/libc/openbsd-digest.c" -o "$build/digest.o"
	objects="$build/sha2.o $build/digest.o"
	for file in "$root"/userland/base/libz-compat/*.c "$root"/userland/base/libjpeg-compat/*.c "$root"/userland/desktop/libtruetype/*.c; do
		object="$build/$(basename "$(dirname "$file")")-$(basename "$file" .c).o"
		"$cc" $loose -I"$(dirname "$file")" -c "$file" -o "$object"
		objects="$objects $object"
	done
	for name in $libpdf; do
		"$cc" $strict -c "$root/userland/base/libpdf/$name" -o "$build/pdf-${name%.c}.o"
		objects="$objects $build/pdf-${name%.c}.o"
	done
	for name in $viewer; do
		"$cc" $strict -c "$root/userland/desktop/pdfviewer/$name" -o "$build/viewer-${name%.c}.o"
		objects="$objects $build/viewer-${name%.c}.o"
	done
	for name in motion scroll gesture; do
		"$cc" $modern -c "$root/userland/desktop/libkeiland/$name.c" -o "$build/keiland-$name.o"
		objects="$objects $build/keiland-$name.o"
	done
	"$cc" $modern -c "$root/userland/desktop/pdfviewer/touch.c" -o "$build/touch.o"
	"$cc" $modern -c "$root/plan/ws081/tests/host-pdftouch.c" -o "$build/host-pdftouch.o"
	"$cc" $modern "$build/host-pdftouch.o" "$build/touch.o" $objects -lm -o "$out/host-pdftouch-$variant"
	mkdir -p "$out/frames-$variant"
	"$out/host-pdftouch-$variant" "$font" "$out/touch.pdf" "$out/frames-$variant" > "$out/$variant.log" 2>&1 || status=1
	grep -E "^(FAIL|host-pdftouch)" "$out/$variant.log" || true
done
for picture in "$out"/frames-plain/*.ppm; do
	if [ -f "$picture" ]; then
		convert "$picture" "${picture%.ppm}.png"
	fi
done
if [ "$status" = 0 ]; then
	echo "run-pdftouch: ok"
else
	echo "run-pdftouch: FAILED"
fi
exit "$status"
