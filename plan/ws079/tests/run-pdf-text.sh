#!/bin/sh
# ws079-p007: stage 2 of libpdf's reader on the host: text (embedded TrueType, Type0/CIDFontType2, the standard 14
# substitutes), shadings, the ASCII85/LZW/RunLength filters, inline images, cross-reference and object streams and
# the repair of a broken xref, compared with pdftoppm (poppler).
#
#   sh plan/ws079/tests/run-pdf-text.sh [FUZZ_ITERATIONS]
#
# 1. Builds host-pdf-render with libpdf, libz-compat, libjpeg-compat and libtruetype (plain, ASan, UBSan).  The
#    substitute fonts (keiland*.ttf) are the host's Liberation fonts, linked into OUT/fonts.
# 2. make-text-pdfs.py writes the test documents (text-simple, text-cid, text-std14, shading, filters).
# 3. Each page at 100 dpi: the three builds give the same pixels, and libpdf is within the tolerance of pdftoppm
#    after a 5x5 binomial blur, a Gaussian of sigma 1 (host-pdf-render blurcompare): embedded fonts mean 2.5 / 0.6 % of pixels past 64,
#    substituted fonts (other shapes in the same places) and enlarged images (poppler rounds an image out to whole
#    pixels, which moves the edges of square pixels by one) 4.0 / 1.5 %.
# 4. qpdf's object-stream copy of each document and a copy with a broken startxref (the reader repairs it)
#    render to the same pixels as the original.
# 5. The real documents under /usr/share/doc (gunzipped into OUT/real) open and every page renders in the three
#    builds without a sanitizer report; chosen pages are compared with pdftoppm at 80 dpi.
# 6. The corruption loop (host-pdf-render fuzzdoc) over each test document's uncompressed copy and its
#    object-stream copy, in the ASan and the UBSan builds.
#
# Output: build/ws079-p007-host/ (cmp-*.png: libpdf left, poppler right).  Needs python3 with fontTools and PIL,
# poppler-utils, qpdf and ImageMagick.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/ws079-p007-host
cc=${CC:-cc}
iterations=${1:-300}
mkdir -p "$out/include" "$out/fonts" "$out/real"
ln -sfn "$(pwd)/include/libc/compat" "$out/include/compat"
ln -sf "$(pwd)/include/libc/pdf.h" "$out/include/pdf.h"
ln -sf "$(pwd)/include/libc/sha2.h" "$out/include/sha2.h"
ln -sf "$(pwd)/include/libc/truetype.h" "$out/include/truetype.h"

# The substitute fonts: sans, serif and mono in four styles, from Liberation.
liberation=/usr/share/fonts/truetype/liberation
for family in "keiland:LiberationSans" "keiland-serif:LiberationSerif" "keiland-mono:LiberationMono"; do
	name=${family%%:*}
	source=${family#*:}
	ln -sf "$liberation/$source-Regular.ttf" "$out/fonts/$name.ttf"
	ln -sf "$liberation/$source-Bold.ttf" "$out/fonts/$name-bold.ttf"
	ln -sf "$liberation/$source-Italic.ttf" "$out/fonts/$name-italic.ttf"
	ln -sf "$liberation/$source-BoldItalic.ttf" "$out/fonts/$name-bolditalic.ttf"
done

# 1. The builds.
libpdf="userland/base/libpdf/writer.c userland/base/libpdf/outline.c userland/base/libpdf/object.c
	userland/base/libpdf/reader.c userland/base/libpdf/filter.c userland/base/libpdf/image.c
	userland/base/libpdf/display.c userland/base/libpdf/content.c userland/base/libpdf/stroke.c
	userland/base/libpdf/raster.c userland/base/libpdf/font.c userland/base/libpdf/encoding.c
	userland/base/libpdf/shading.c"
for variant in plain asan ubsan; do
	flags="-std=c89 -pedantic -O1 -g -Wall -Wextra -Werror -D_DEFAULT_SOURCE -I$out/include"
	if [ "$variant" = asan ]; then
		flags="$flags -fsanitize=address -fno-omit-frame-pointer"
	fi
	if [ "$variant" = ubsan ]; then
		flags="$flags -fsanitize=undefined,float-cast-overflow -fno-sanitize-recover=all"
	fi
	loose=$(echo "$flags" | sed 's/-std=c89 -pedantic/-std=gnu11/; s/-Werror//')
	objects="$out/sha2-$variant.o"
	"$cc" $loose -c src/libc/openbsd-sha2.c -o "$out/sha2-$variant.o"
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
status=0

# Renders a document in the three builds at a resolution; the builds must give the same pixels.
render_three() {
	document=$1
	prefix=$2
	dpi=$3
	for variant in plain asan ubsan; do
		"$out/host-pdf-render-$variant" render "$document" "$prefix-$variant" "$dpi" > "$prefix-$variant.log" ||
		    { echo "render failed: $variant $document"; status=1; }
	done
	for picture in "$prefix"-plain-*.ppm; do
		cmp "$picture" "$(echo "$picture" | sed 's/-plain-/-asan-/')" || status=1
		cmp "$picture" "$(echo "$picture" | sed 's/-plain-/-ubsan-/')" || status=1
	done
}

# Compares page PAGE of a rendering with pdftoppm's, blurred, within a tolerance class; writes cmp-NAME.png.
compare_page() {
	prefix=$1
	document=$2
	page=$3
	dpi=$4
	class=$5
	name=$6
	pdftoppm -cropbox -r "$dpi" -f "$page" -l "$page" -singlefile "$document" "$out/poppler-$name"
	if [ "$class" = embedded ]; then
		tolerance="2.5 0.006"
	else
		tolerance="4.0 0.015"
	fi
	"$render" blurcompare "$prefix-plain-$page.ppm" "$out/poppler-$name.ppm" $tolerance || status=1
	convert "$prefix-plain-$page.ppm" "$out/poppler-$name.ppm" +append "$out/cmp-$name.png"
}

# 2. The test documents.
python3 plan/ws079/tests/make-text-pdfs.py "$out"

# 3. Each test document against poppler.
for entry in text-simple:embedded text-cid:embedded text-std14:substitute shading:substitute filters:images; do
	doc=${entry%%:*}
	class=${entry#*:}
	qpdf --check "$out/$doc.pdf" > "$out/qpdf-$doc.txt" 2>&1 || { echo "qpdf: $doc"; cat "$out/qpdf-$doc.txt"; status=1; }
	rm -f "$out/$doc"-100-*.ppm
	render_three "$out/$doc.pdf" "$out/$doc-100" 100
	grep -h "^page" "$out/$doc-100-plain.log" | sed "s|^|$doc: |"
	pages=$(grep -c "^page" "$out/$doc-100-plain.log")
	page=1
	while [ "$page" -le "$pages" ]; do
		compare_page "$out/$doc-100" "$out/$doc.pdf" "$page" 100 "$class" "$doc-$page"
		page=$((page + 1))
	done
done

# 4. The object-stream copies and the repaired copies draw the same pixels.
for doc in text-simple text-cid text-std14 shading filters; do
	qpdf --object-streams=generate "$out/$doc.pdf" "$out/$doc-objstm.pdf"
	qpdf --stream-data=uncompress --object-streams=disable "$out/$doc.pdf" "$out/$doc-plain.pdf"
	python3 -c "import sys; d=open(sys.argv[1],'rb').read(); i=d.rindex(b'startxref'); open(sys.argv[2],'wb').write(d[:i]+b'startxref\n1\n%%EOF\n')" \
	    "$out/$doc.pdf" "$out/$doc-broken.pdf"
	for copy in objstm broken; do
		rm -f "$out/$doc-$copy"-*.ppm
		"$render" render "$out/$doc-$copy.pdf" "$out/$doc-$copy" 100 > "$out/$doc-$copy.log"
		for picture in "$out/$doc-100-plain"-*.ppm; do
			cmp "$picture" "$out/$doc-$copy-${picture##*-}" || { echo "$copy $doc: pixels differ"; status=1; }
		done
		echo "$copy $doc: same pixels"
	done
done

# 5. The real documents.
for file in /usr/share/doc/zlib1g-dev/crc-doc.1.0.pdf.gz /usr/share/doc/fontconfig/fontconfig-user.pdf.gz \
    /usr/share/doc/texinfo/txirefcard.pdf.gz /usr/share/doc/texinfo/txirefcard-a4.pdf.gz \
    /usr/share/doc/device-tree-compiler/dtc-paper.pdf.gz /usr/share/doc/debian/FAQ/debian-faq.pdf.gz; do
	[ -f "$file" ] && zcat "$file" > "$out/real/$(basename "$file" .gz)"
done
for file in /usr/share/doc/quilt/quilt.pdf /usr/share/doc/shared-mime-info/shared-mime-info-spec.pdf \
    /usr/share/emacs/30.1/etc/refcards/gnus-logo.pdf; do
	[ -f "$file" ] && cp "$file" "$out/real/"
done
for document in "$out"/real/*.pdf; do
	name=$(basename "$document" .pdf)
	rm -f "$out/real/$name"-80-*.ppm
	start=$(date +%s.%N)
	render_three "$document" "$out/real/$name-80" 80
	end=$(date +%s.%N)
	pages=$(grep -c "^page" "$out/real/$name-80-plain.log" || true)
	skipped=$(grep -c "flags [13579]" "$out/real/$name-80-plain.log" || true)
	echo "real $name: $pages pages ($skipped with SKIPPED), three builds $(echo "$end - $start" | bc) s"
done
for entry in debian-faq:3:substitute quilt:1:substitute; do
	name=${entry%%:*}
	rest=${entry#*:}
	page=${rest%%:*}
	class=${rest#*:}
	[ -f "$out/real/$name.pdf" ] && compare_page "$out/real/$name-80" "$out/real/$name.pdf" "$page" 80 "$class" "$name-$page"
done

# 6. The corruption loops.
for doc in text-simple text-cid text-std14 shading filters; do
	for copy in plain objstm; do
		for variant in asan ubsan; do
			timeout 1800 "$out/host-pdf-render-$variant" fuzzdoc "$out/$doc-$copy.pdf" "$iterations" > "$out/fuzz-$doc-$copy-$variant.log" 2>&1 ||
			    { echo "fuzz failed: $variant $doc-$copy"; tail -20 "$out/fuzz-$doc-$copy-$variant.log"; status=1; }
			echo "fuzz $variant $doc-$copy: $(grep -h '^fuzzdoc' "$out/fuzz-$doc-$copy-$variant.log" | tail -2 | tr '\n' ' ')"
		done
	done
done

[ "$status" = 0 ] && echo "run-pdf-text: ok" || echo "run-pdf-text: FAILED"
exit "$status"
