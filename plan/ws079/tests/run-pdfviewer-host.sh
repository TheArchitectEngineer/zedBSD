#!/bin/sh
# ws079-p006: builds PDF Viewer's core (view, frame, document cache, canvas, text; the chooser is libkeiland's since ws090-p008) with libpdf and
# libtruetype for the host (plain and ASan), and runs host-pdfviewer on the Notes-like document of
# run-pdf-render.sh: the scroll mode, the page mode's swipe and keys, the zoom, the chooser.  The frames are
# written to build/ws079-p006-host/viewer-*/ as PPM and converted to PNG.  ws079-p015: the sidebar of thumbnails and the
# password card (notes.pdf encrypted by qpdf with the user password "secret" and the owner password "owner").
#   sh plan/ws079/tests/run-pdfviewer-host.sh
# Needs: userland/desktop/fonts/Inter.ttf, and build/ws079-p006-host/notes.pdf (run-pdf-render.sh makes it).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/ws079-p006-host
cc=${CC:-cc}
font=userland/desktop/fonts/Inter.ttf
[ -f "$out/notes.pdf" ] || { echo "run-pdfviewer-host: run run-pdf-render.sh first"; exit 1; }
mkdir -p "$out/include"
ln -sfn "$(pwd)/include/libc/compat" "$out/include/compat"
ln -sf "$(pwd)/include/libc/pdf.h" "$out/include/pdf.h"
ln -sf "$(pwd)/include/libc/sha2.h" "$out/include/sha2.h"
# ws079-p008: the security handler (crypt.c) uses the C library's MD5, which openbsd-digest.c has with SHA-1.
ln -sf "$(pwd)/include/libc/md5.h" "$out/include/md5.h"
ln -sf "$(pwd)/include/libc/sha1.h" "$out/include/sha1.h"
ln -sf "$(pwd)/userland/desktop/keiland/truetype.h" "$out/include/truetype.h"
libpdf="userland/base/libpdf/writer.c userland/base/libpdf/outline.c userland/base/libpdf/object.c
	userland/base/libpdf/reader.c userland/base/libpdf/filter.c userland/base/libpdf/ccitt.c userland/base/libpdf/crypt.c userland/base/libpdf/image.c
	userland/base/libpdf/display.c userland/base/libpdf/content.c userland/base/libpdf/stroke.c
	userland/base/libpdf/raster.c userland/base/libpdf/font.c userland/base/libpdf/tounicode.c userland/base/libpdf/encoding.c
	userland/base/libpdf/shading.c
	userland/base/libpdf/charstrings.c userland/base/libpdf/type1.c userland/base/libpdf/cff.c userland/base/libpdf/cffdata.c"
viewer="userland/desktop/pdfviewer/view.c userland/desktop/pdfviewer/draw.c userland/desktop/pdfviewer/document.c
	userland/desktop/pdfviewer/canvas.c userland/desktop/pdfviewer/text.c"
# ws079-p007: a page with a JBIG2 image (libpdf leaves it out) for the notice.
python3 -c "
import sys
body = b'q 200 0 0 200 50 50 cm /Im1 Do Q'
objects = [b'<< /Type /Catalog /Pages 2 0 R >>', b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
    b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300] /Resources << /XObject << /Im1 5 0 R >> >> /Contents 4 0 R >>',
    b'<< /Length %d >>\\nstream\\n' % len(body) + body + b'\\nendstream',
    b'<< /Type /XObject /Subtype /Image /Width 8 /Height 8 /BitsPerComponent 1 /ColorSpace /DeviceGray /Filter /JBIG2Decode /Length 4 >>\\nstream\\nabcd\\nendstream']
out = b'%PDF-1.7\\n'
offsets = []
for number, text in enumerate(objects, 1):
    offsets.append(len(out))
    out += b'%d 0 obj\\n' % number + text + b'\\nendobj\\n'
xref = len(out)
out += b'xref\\n0 6\\n0000000000 65535 f \\n' + b''.join(b'%010d 00000 n \\n' % o for o in offsets)
out += b'trailer\\n<< /Size 6 /Root 1 0 R >>\\nstartxref\\n%d\\n%%%%EOF\\n' % xref
open(sys.argv[1], 'wb').write(out)
" "$out/skipped.pdf"
# ws079-p015: the document encrypted with a user and an owner password, for the password card.
qpdf --encrypt --user-password=secret --owner-password=owner --bits=256 -- "$out/notes.pdf" "$out/password.pdf"
status=0
for variant in plain asan; do
	flags="-std=c89 -pedantic -O1 -g -Wall -Wextra -Werror -D_DEFAULT_SOURCE -I$out/include"
	if [ "$variant" = asan ]; then
		flags="$flags -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"
	fi
	loose=$(echo "$flags" | sed 's/-std=c89 -pedantic/-std=gnu11/; s/-Werror//')
	objects="$out/sha2-$variant.o"
	"$cc" $loose -c src/libc/openbsd-sha2.c -o "$out/sha2-$variant.o"
	"$cc" $loose -w -c src/libc/openbsd-digest.c -o "$out/digest-$variant.o"
	objects="$objects $out/digest-$variant.o"
	for file in userland/base/libz-compat/*.c userland/base/libjpeg-compat/*.c userland/desktop/libtruetype/*.c; do
		object="$out/$(basename "$(dirname "$file")")-$(basename "$file" .c)-$variant.o"
		"$cc" $loose -w -I"$(dirname "$file")" -c "$file" -o "$object"
		objects="$objects $object"
	done
	"$cc" $flags $libpdf $viewer plan/ws079/tests/host-pdfviewer.c $objects -lm -o "$out/host-pdfviewer-$variant"
	mkdir -p "$out/viewer-$variant"
	"$out/host-pdfviewer-$variant" "$font" "$out/notes.pdf" "$out/viewer-$variant" "$out/skipped.pdf" "$out/password.pdf" \
	    > "$out/viewer-$variant.log" 2>&1 || status=1
	grep -E "^(ok|FAILED|host-pdfviewer)" "$out/viewer-$variant.log"
done
for picture in "$out"/viewer-plain/*.ppm; do
	convert "$picture" "${picture%.ppm}.png"
done
[ "$status" = 0 ] && echo "run-pdfviewer-host: ok" || echo "run-pdfviewer-host: FAILED"
exit "$status"
