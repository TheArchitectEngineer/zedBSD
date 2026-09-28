#!/bin/sh
# ws079-p006: builds PDF Viewer's core (view, frame, document cache, chooser, canvas, text) with libpdf and
# libtruetype for the host (plain and ASan), and runs host-pdfviewer on the Notes-like document of
# run-pdf-render.sh: the scroll mode, the page mode's swipe and keys, the zoom, the chooser.  The frames are
# written to build/ws079-p006-host/viewer-*/ as PPM and converted to PNG.
#   sh plan/ws079/tests/run-pdfviewer-host.sh
# Needs: build/ws035-fonts/Inter.ttf, and build/ws079-p006-host/notes.pdf (run-pdf-render.sh makes it).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/ws079-p006-host
cc=${CC:-cc}
font=build/ws035-fonts/Inter.ttf
[ -f "$out/notes.pdf" ] || { echo "run-pdfviewer-host: run run-pdf-render.sh first"; exit 1; }
mkdir -p "$out/include"
ln -sfn "$(pwd)/include/libc/compat" "$out/include/compat"
ln -sf "$(pwd)/include/libc/pdf.h" "$out/include/pdf.h"
ln -sf "$(pwd)/include/libc/sha2.h" "$out/include/sha2.h"
ln -sf "$(pwd)/include/libc/truetype.h" "$out/include/truetype.h"
libpdf="userland/base/libpdf/writer.c userland/base/libpdf/outline.c userland/base/libpdf/object.c
	userland/base/libpdf/reader.c userland/base/libpdf/filter.c userland/base/libpdf/image.c
	userland/base/libpdf/display.c userland/base/libpdf/content.c userland/base/libpdf/stroke.c
	userland/base/libpdf/raster.c"
viewer="userland/desktop/pdfviewer/view.c userland/desktop/pdfviewer/draw.c userland/desktop/pdfviewer/document.c
	userland/desktop/pdfviewer/chooser.c userland/desktop/pdfviewer/canvas.c userland/desktop/pdfviewer/text.c"
status=0
for variant in plain asan; do
	flags="-std=c89 -pedantic -O1 -g -Wall -Wextra -Werror -D_DEFAULT_SOURCE -I$out/include"
	if [ "$variant" = asan ]; then
		flags="$flags -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"
	fi
	loose=$(echo "$flags" | sed 's/-std=c89 -pedantic/-std=gnu11/; s/-Werror//')
	objects="$out/sha2-$variant.o"
	"$cc" $loose -c src/libc/openbsd-sha2.c -o "$out/sha2-$variant.o"
	for file in userland/base/libz-compat/*.c userland/base/libjpeg-compat/*.c userland/desktop/libtruetype/*.c; do
		object="$out/$(basename "$(dirname "$file")")-$(basename "$file" .c)-$variant.o"
		"$cc" $loose -w -I"$(dirname "$file")" -c "$file" -o "$object"
		objects="$objects $object"
	done
	"$cc" $flags $libpdf $viewer plan/ws079/tests/host-pdfviewer.c $objects -lm -o "$out/host-pdfviewer-$variant"
	mkdir -p "$out/viewer-$variant"
	"$out/host-pdfviewer-$variant" "$font" "$out/notes.pdf" "$out/viewer-$variant" > "$out/viewer-$variant.log" 2>&1 || status=1
	grep -E "^(ok|FAILED|host-pdfviewer)" "$out/viewer-$variant.log"
done
for picture in "$out"/viewer-plain/*.ppm; do
	convert "$picture" "${picture%.ppm}.png"
done
[ "$status" = 0 ] && echo "run-pdfviewer-host: ok" || echo "run-pdfviewer-host: FAILED"
exit "$status"
