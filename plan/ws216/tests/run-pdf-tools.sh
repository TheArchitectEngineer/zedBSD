#!/bin/sh
# ws216-p002: builds libpdf and host-pdf-tools.c with the host's C compiler (plain, ASan and UBSan), with main
# f9077504e's outline.c linked under other names as the pen's golden outline, and runs the acceptance of
# plan/ws216/phase002/phase.md section 4: the outlines (A2, A3), the writer's states, Flate and grains with the
# reader's hashes (A4, A7), libpdf, pdftoppm and Ghostscript against each other (A5), the grain's place on new,
# taller and turned pages (A6), the list's counts on the page of 200 pencil strokes (A8), the clean copy (A9),
# the seam's hair lines at 600 dpi (A10).  The other libpdf host tests (A11) are run by the caller.
#   sh plan/ws216/tests/run-pdf-tools.sh
# Output: build/ws216-p002-host/ in the tree the script is in (files are overwritten, nothing is removed).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/ws216-p002-host
cc=${CC:-cc}
golden=f9077504e
mkdir -p "$out/include/truetype"
ln -sfn "$(pwd)/include/libc/compat" "$out/include/compat"
for header in pdf.h sha2.h md5.h sha1.h; do
	ln -sf "$(pwd)/include/libc/$header" "$out/include/$header"
done
ln -sf "$(pwd)/userland/desktop/include/truetype/truetype.h" "$out/include/truetype/truetype.h"
git show "$golden:userland/desktop/libpdf/outline.c" > "$out/outline-golden.c"
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
	objects=
	"$cc" $loose -c src/libc/openbsd-sha2.c -o "$out/sha2-$variant.o"
	"$cc" $loose -w -c src/libc/openbsd-digest.c -o "$out/digest-$variant.o"
	objects="$objects $out/sha2-$variant.o $out/digest-$variant.o"
	for file in userland/base/libz-compat/*.c userland/desktop/libjpeg-compat/*.c; do
		object="$out/$(basename "$(dirname "$file")")-$(basename "$file" .c)-$variant.o"
		"$cc" $loose -w -I"$(dirname "$file")" -c "$file" -o "$object"
		objects="$objects $object"
	done
	for file in userland/desktop/libtruetype/*.c; do
		object="$out/truetype-$(basename "$file" .c)-$variant.o"
		"$cc" $loose -Werror -c "$file" -o "$object"
		objects="$objects $object"
	done
	# The golden outline: main f9077504e's file with its two public names changed.
	"$cc" $flags -Dpdf_outline_stroke=old_pdf_outline_stroke -Dpdf_outline_free=old_pdf_outline_free \
	    -c "$out/outline-golden.c" -o "$out/outline-golden-$variant.o"
	"$cc" $flags -Wno-overlength-strings userland/desktop/libpdf/*.c plan/ws216/tests/host-pdf-tools.c "$out/outline-golden-$variant.o" \
	    $objects -lm -o "$out/host-pdf-tools-$variant"
done
# A2, A3: the outlines, in every variant.
for variant in plain asan ubsan; do
	"$out/host-pdf-tools-$variant" outline > "$out/outline-$variant.log" || status=1
	tail -1 "$out/outline-$variant.log"
done
cat "$out/outline-plain.log"
# The base page, turned a quarter, and the documents (A4, A7) in every variant (the same bytes but the ids and dates).
"$out/host-pdf-tools-plain" base "$out"
qpdf --rotate=+90 "$out/base.pdf" "$out/base-rot.pdf"
for variant in asan ubsan plain; do
	"$out/host-pdf-tools-$variant" write "$out" > "$out/write-$variant.log" || status=1
	tail -1 "$out/write-$variant.log"
done
cat "$out/write-plain.log"
for doc in states tools seam update; do
	qpdf --check "$out/$doc.pdf" > "$out/qpdf-$doc.txt" || { echo "qpdf: $doc.pdf"; status=1; }
done
# A9: the clean copy.
"$out/host-pdf-tools-plain" clean "$out/tools.pdf" "$out/clean.pdf" || status=1
qpdf --check "$out/clean.pdf" > "$out/qpdf-clean.txt" || { echo "qpdf: clean.pdf"; status=1; }
# A5, A6, A8, A9: libpdf, pdftoppm and Ghostscript.
for doc in tools update clean; do
	"$out/host-pdf-tools-plain" render "$out/$doc.pdf" "$out/libpdf-$doc-72" 72 || status=1
	"$out/host-pdf-tools-asan" render "$out/$doc.pdf" "$out/asan-$doc-72" 72 > "$out/asan-render-$doc.log" || status=1
	pdftoppm -r 72 "$out/$doc.pdf" "$out/poppler-$doc-72"
	gs -q -dNOPAUSE -dBATCH -dSAFER -sDEVICE=ppmraw -r72 -dGraphicsAlphaBits=4 -o "$out/gs-$doc-72-%d.ppm" "$out/$doc.pdf"
done
for page in 1 2 3; do
	"$out/host-pdf-tools-plain" compare "$out/libpdf-tools-72-$page.ppm" "$out/poppler-tools-72-$page.ppm" || status=1
	"$out/host-pdf-tools-plain" compare "$out/libpdf-tools-72-$page.ppm" "$out/gs-tools-72-$page.ppm" || status=1
	"$out/host-pdf-tools-plain" compare "$out/libpdf-tools-72-$page.ppm" "$out/libpdf-clean-72-$page.ppm" 0 0 || status=1
	"$out/host-pdf-tools-plain" compare "$out/poppler-tools-72-$page.ppm" "$out/poppler-clean-72-$page.ppm" 0 0 || status=1
done
for page in 1 2; do
	"$out/host-pdf-tools-plain" compare "$out/libpdf-update-72-$page.ppm" "$out/poppler-update-72-$page.ppm" || status=1
	"$out/host-pdf-tools-plain" compare "$out/libpdf-update-72-$page.ppm" "$out/gs-update-72-$page.ppm" || status=1
done
# A6: the grain's place: the taller page of tools.pdf, the turned page and the added page of update.pdf.
for picture in libpdf-tools-72-2 poppler-tools-72-2 gs-tools-72-2 libpdf-update-72-1 poppler-update-72-1 gs-update-72-1 \
    libpdf-update-72-2 poppler-update-72-2 gs-update-72-2; do
	"$out/host-pdf-tools-plain" grain "$out/$picture.ppm" 72 || status=1
done
# M-7: general tiling patterns (cells of fills, larger than the step and turned, nested; an uncoloured one left out).
"$out/host-pdf-tools-plain" patterns "$out/patterns.pdf"
qpdf --check "$out/patterns.pdf" > "$out/qpdf-patterns.txt" || { echo "qpdf: patterns.pdf"; status=1; }
# Ghostscript is the reference here: poppler moves each repeat to whole device pixels (its own squares drift by a
# pixel), so libpdf against poppler is only recorded within a loose bound.
dpi=216
"$out/host-pdf-tools-asan" render "$out/patterns.pdf" "$out/libpdf-patterns-$dpi" "$dpi" | tee "$out/render-patterns-$dpi.log" || status=1
grep -q "page 2: .* flags 1 " "$out/render-patterns-$dpi.log" || { echo "patterns: the uncoloured pattern is not skipped"; status=1; }
pdftoppm -r "$dpi" "$out/patterns.pdf" "$out/poppler-patterns-$dpi"
gs -q -dNOPAUSE -dBATCH -dSAFER -sDEVICE=ppmraw -r$dpi -dGraphicsAlphaBits=4 -o "$out/gs-patterns-$dpi-%d.ppm" "$out/patterns.pdf"
"$out/host-pdf-tools-plain" compare "$out/libpdf-patterns-$dpi-1.ppm" "$out/gs-patterns-$dpi-1.ppm" 3 0.02 || status=1
"$out/host-pdf-tools-plain" compare "$out/libpdf-patterns-$dpi-1.ppm" "$out/poppler-patterns-$dpi-1.ppm" 12 0.05 || status=1
convert "$out/libpdf-patterns-216-1.ppm" "$out/gs-patterns-216-1.ppm" "$out/poppler-patterns-216-1.ppm" +append "$out/patterns-216.png"
# A10: the seam at 600 dpi, the ranges against the whole stroke (counted, not judged).
pdftoppm -r 600 "$out/seam.pdf" "$out/poppler-seam-600"
"$out/host-pdf-tools-plain" render "$out/seam.pdf" "$out/libpdf-seam-600" 600 > "$out/render-seam.log"
"$out/host-pdf-tools-plain" compare "$out/poppler-seam-600-1.ppm" "$out/poppler-seam-600-2.ppm" 1000 1 || status=1
"$out/host-pdf-tools-plain" compare "$out/libpdf-seam-600-1.ppm" "$out/libpdf-seam-600-2.ppm" 1000 1 || status=1
# Pictures to look at.
for picture in libpdf-tools-72-1 poppler-tools-72-1 gs-tools-72-1 libpdf-tools-72-3 poppler-update-72-1; do
	convert "$out/$picture.ppm" "$out/$picture.png"
done
[ "$status" = 0 ] && echo "run-pdf-tools: ok" || echo "run-pdf-tools: FAILED"
exit "$status"
