#!/bin/sh
# ws079-p015: libpdf's CCITT fax decoder (CCITTFaxDecode, Group 3 and Group 4) on the host.
#
#   sh plan/ws079/tests/run-pdf-ccitt.sh [FUZZ_ROUNDS]
#
# 1. make-ccitt-data.py makes bitmaps and codes them with ghostscript's CCITTFaxEncode (80 cases: K -1, 0 and 4, end-of-
#    line codes, byte alignment, the end-of-block code, BlackIs1, with and without /Rows), and ccitt.pdf (image
#    XObjects, a stencil mask, /Decode, inline images with /F /CCF).
# 2. host-pdf-ccitt exact (plain, ASan, UBSan): every case decodes to its bitmap bit for bit.
# 3. host-pdf-ccitt fuzz (ASan, UBSan): corrupted data and parameters decode without a fault, into whole rows.
# 4. host-pdf-render (plain, ASan, UBSan) draws ccitt.pdf: the three builds give the same pixels, no page has SKIPPED,
#    and each page is within the image tolerance of a reference after the blur of run-pdf-text.sh: page 1 (the text
#    page, one image pixel a device pixel at 150 dpi; 6.0 / 0.6 %, since libpdf's rasterizer softens the image's
#    edges where poppler draws the bits as they are, which raises the mean while hardly a pixel differs past 64)
#    and page 2 (the codings as XObjects, 100 dpi) against pdftoppm,
#    page 3 (inline images, 100 dpi) against ghostscript, since poppler leaves out its byte-aligned inline stencil
#    mask (ghostscript and libpdf draw it; the same data decodes bit for bit in step 2).
# 5. The real documents whose images are CCITT (gnus-logo.pdf, txirefcard.pdf and txirefcard-a4.pdf, which had the
#    last SKIPPED pages after ws079-p008): no page has SKIPPED; gnus-logo page 1 (image tolerance) and txirefcard
#    page 1 (the dense small text tolerance of run-pdf-text.sh, 5.0 / 0.6 %) against pdftoppm.
# 6. host-pdf-render fuzzdoc over ccitt.pdf's uncompressed copy (ASan, UBSan).
#
# Output: build/ws079-p015-host/ (cmp-*.png: libpdf left, poppler right).  Needs python3 with PIL, ghostscript,
# poppler-utils, qpdf and ImageMagick.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/ws079-p015-host
cc=${CC:-cc}
rounds=${1:-200}
mkdir -p "$out/include" "$out/fonts" "$out/real"
ln -sfn "$(pwd)/include/libc/compat" "$out/include/compat"
ln -sf "$(pwd)/include/libc/pdf.h" "$out/include/pdf.h"
ln -sf "$(pwd)/include/libc/sha2.h" "$out/include/sha2.h"
ln -sf "$(pwd)/include/libc/md5.h" "$out/include/md5.h"
ln -sf "$(pwd)/include/libc/sha1.h" "$out/include/sha1.h"
ln -sf "$(pwd)/userland/desktop/keiland/truetype.h" "$out/include/truetype.h"
liberation=/usr/share/fonts/truetype/liberation
for family in "keiland:LiberationSans" "keiland-serif:LiberationSerif" "keiland-mono:LiberationMono"; do
	name=${family%%:*}
	source=${family#*:}
	ln -sf "$liberation/$source-Regular.ttf" "$out/fonts/$name.ttf"
	ln -sf "$liberation/$source-Bold.ttf" "$out/fonts/$name-bold.ttf"
	ln -sf "$liberation/$source-Italic.ttf" "$out/fonts/$name-italic.ttf"
	ln -sf "$liberation/$source-BoldItalic.ttf" "$out/fonts/$name-bolditalic.ttf"
done
status=0

# 1. The data.
rm -rf "$out/cases"
python3 plan/ws079/tests/make-ccitt-data.py "$out"
qpdf --check "$out/ccitt.pdf" > "$out/qpdf-ccitt.txt" 2>&1 || { echo "qpdf: ccitt.pdf"; cat "$out/qpdf-ccitt.txt"; status=1; }

# The builds.
libpdf="userland/base/libpdf/writer.c userland/base/libpdf/outline.c userland/base/libpdf/object.c
	userland/base/libpdf/reader.c userland/base/libpdf/filter.c userland/base/libpdf/ccitt.c userland/base/libpdf/crypt.c
	userland/base/libpdf/image.c userland/base/libpdf/display.c userland/base/libpdf/content.c userland/base/libpdf/stroke.c
	userland/base/libpdf/raster.c userland/base/libpdf/font.c userland/base/libpdf/tounicode.c userland/base/libpdf/encoding.c
	userland/base/libpdf/shading.c
	userland/base/libpdf/charstrings.c userland/base/libpdf/type1.c userland/base/libpdf/cff.c userland/base/libpdf/cffdata.c"
for variant in plain asan ubsan; do
	flags="-std=c89 -pedantic -O1 -g -Wall -Wextra -Werror -D_DEFAULT_SOURCE -I$out/include"
	if [ "$variant" = asan ]; then
		flags="$flags -fsanitize=address -fno-omit-frame-pointer"
	fi
	if [ "$variant" = ubsan ]; then
		flags="$flags -fsanitize=undefined,float-cast-overflow -fno-sanitize-recover=all"
	fi
	loose=$(echo "$flags" | sed 's/-std=c89 -pedantic/-std=gnu11/; s/-Werror//')
	"$cc" $flags -Iuserland/base/libpdf userland/base/libpdf/ccitt.c plan/ws079/tests/host-pdf-ccitt.c -o "$out/host-pdf-ccitt-$variant"
	objects="$out/sha2-$variant.o"
	"$cc" $loose -c src/libc/openbsd-sha2.c -o "$out/sha2-$variant.o"
	"$cc" $loose -w -c src/libc/openbsd-digest.c -o "$out/digest-$variant.o"
	objects="$objects $out/digest-$variant.o"
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
	"$cc" $flags -Wno-overlength-strings "-DPDF_FONT_DIRECTORY=\"$(pwd)/$out/fonts\"" $libpdf plan/ws079/tests/host-pdf-render.c \
	    $objects -lm -o "$out/host-pdf-render-$variant"
done
render=$out/host-pdf-render-plain

# 2. Bit for bit.
for variant in plain asan ubsan; do
	"$out/host-pdf-ccitt-$variant" exact "$out" > "$out/exact-$variant.log" 2>&1 || status=1
	echo "exact $variant: $(tail -1 "$out/exact-$variant.log")"
done
grep FAILED "$out/exact-plain.log" | head -5 || true

# 3. The corruption loop of the decoder.
for variant in asan ubsan; do
	timeout 1800 "$out/host-pdf-ccitt-$variant" fuzz "$out" "$rounds" > "$out/fuzz-ccitt-$variant.log" 2>&1 ||
	    { echo "fuzz failed: $variant"; tail -20 "$out/fuzz-ccitt-$variant.log"; status=1; }
	echo "fuzz $variant: $(tail -1 "$out/fuzz-ccitt-$variant.log")"
done

# Renders a document in the three builds; the builds must give the same pixels and no page may have SKIPPED.
render_three() {
	document=$1
	prefix=$2
	dpi=$3
	rm -f "$prefix"-*.ppm
	for variant in plain asan ubsan; do
		"$out/host-pdf-render-$variant" render "$document" "$prefix-$variant" "$dpi" > "$prefix-$variant.log" ||
		    { echo "render failed: $variant $document"; status=1; }
	done
	for picture in "$prefix"-plain-*.ppm; do
		cmp "$picture" "$(echo "$picture" | sed 's/-plain-/-asan-/')" || status=1
		cmp "$picture" "$(echo "$picture" | sed 's/-plain-/-ubsan-/')" || status=1
	done
	skipped=$(grep -c "flags [13579]" "$prefix-plain.log" || true)
	pages=$(grep -c "^page" "$prefix-plain.log" || true)
	echo "$(basename "$document"): $pages pages, $skipped with SKIPPED"
	[ "$skipped" = 0 ] || status=1
}

# Compares page PAGE with a reference's (poppler or ghostscript) within a tolerance; writes cmp-NAME.png.
compare_page() {
	prefix=$1
	document=$2
	page=$3
	dpi=$4
	name=$5
	reference=${6:-poppler}
	tolerance=${7:-4.0 0.015}
	if [ "$reference" = poppler ]; then
		pdftoppm -cropbox -r "$dpi" -f "$page" -l "$page" -singlefile "$document" "$out/reference-$name"
	else
		gs -q -dNOPAUSE -dBATCH -dSAFER -sDEVICE=png16m -dUseCropBox -r"$dpi" -dTextAlphaBits=4 -dGraphicsAlphaBits=4 \
		    -dFirstPage="$page" -dLastPage="$page" -sOutputFile="$out/reference-$name.png" "$document"
		convert "$out/reference-$name.png" "$out/reference-$name.ppm"
	fi
	"$render" blurcompare "$prefix-plain-$page.ppm" "$out/reference-$name.ppm" $tolerance || status=1
	convert "$prefix-plain-$page.ppm" "$out/reference-$name.ppm" +append "$out/cmp-$name.png"
}

# 4. The document.
render_three "$out/ccitt.pdf" "$out/ccitt-100" 100
render_three "$out/ccitt.pdf" "$out/ccitt-150" 150
compare_page "$out/ccitt-150" "$out/ccitt.pdf" 1 150 ccitt-1 poppler "6.0 0.006"
compare_page "$out/ccitt-100" "$out/ccitt.pdf" 2 100 ccitt-2
compare_page "$out/ccitt-100" "$out/ccitt.pdf" 3 100 ccitt-3 ghostscript

# 5. The real documents.
[ -f /usr/share/emacs/30.1/etc/refcards/gnus-logo.pdf ] && cp /usr/share/emacs/30.1/etc/refcards/gnus-logo.pdf "$out/real/"
for file in /usr/share/doc/texinfo/txirefcard.pdf.gz /usr/share/doc/texinfo/txirefcard-a4.pdf.gz; do
	[ -f "$file" ] && zcat "$file" > "$out/real/$(basename "$file" .gz)"
done
for document in "$out"/real/*.pdf; do
	render_three "$document" "$out/real/$(basename "$document" .pdf)-80" 80
done
[ -f "$out/real/gnus-logo.pdf" ] && compare_page "$out/real/gnus-logo-80" "$out/real/gnus-logo.pdf" 1 80 gnus-logo-1
[ -f "$out/real/txirefcard.pdf" ] && compare_page "$out/real/txirefcard-80" "$out/real/txirefcard.pdf" 1 80 txirefcard-1 poppler "5.0 0.006"

# 6. The corruption loop of the document.
qpdf --stream-data=uncompress --object-streams=disable "$out/ccitt.pdf" "$out/ccitt-plain.pdf"
for variant in asan ubsan; do
	timeout 1800 "$out/host-pdf-render-$variant" fuzzdoc "$out/ccitt-plain.pdf" "$rounds" > "$out/fuzzdoc-$variant.log" 2>&1 ||
	    { echo "fuzzdoc failed: $variant"; tail -20 "$out/fuzzdoc-$variant.log"; status=1; }
	echo "fuzzdoc $variant: $(grep -h '^fuzzdoc' "$out/fuzzdoc-$variant.log" | tail -2 | tr '\n' ' ')"
done

[ "$status" = 0 ] && echo "run-pdf-ccitt: ok" || echo "run-pdf-ccitt: FAILED"
exit "$status"
