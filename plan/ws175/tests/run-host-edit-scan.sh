#!/bin/sh
# ws175-p002a, p003a, p003b, p006, p007, p004: builds libpdf (with libz-compat, libjpeg-compat and libtruetype), host-edit-scan,
# host-edit-change, host-edit-image, host-edit-intake, host-edit-blank and host-edit-text-change (edit-text.pdf) with the host's C compiler (plain, ASan and UBSan), writes edit-images.pdf (make-edit-samples.py) and
# runs the tests with each build; the files host-edit-change, host-edit-image, host-edit-intake and host-edit-blank save are checked with qpdf --check.
#   sh plan/ws175/tests/run-host-edit-scan.sh [OUTPUT]   (default build/ws175-host)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws175-host}
cc=${CC:-cc}
mkdir -p "$out/include"
ln -sfn "$(pwd)/include/libc/compat" "$out/include/compat"
ln -sf "$(pwd)/include/libc/pdf.h" "$out/include/pdf.h"
ln -sf "$(pwd)/include/libc/sha2.h" "$out/include/sha2.h"
ln -sf "$(pwd)/include/libc/md5.h" "$out/include/md5.h"
ln -sf "$(pwd)/include/libc/sha1.h" "$out/include/sha1.h"
ln -sf "$(pwd)/userland/desktop/keiland/truetype.h" "$out/include/truetype.h"
python3 plan/ws175/tests/make-edit-samples.py "$out" >/dev/null
# ws175-p005: the replacement fonts as the desktop installs them, in a folder of the test's own.
mkdir -p "$out/fonts"
ln -sf "$(pwd)/userland/desktop/fonts/Mahora-Regular.ttf" "$out/fonts/keiland.ttf"
ln -sf "$(pwd)/userland/desktop/fonts/Mahora-Mono.ttf" "$out/fonts/keiland-mono.ttf"
ln -sf "$(pwd)/userland/desktop/fonts/JetBrainsMono-Regular.ttf" "$out/fonts/keiland-fallback-mono.ttf"
ln -sf "$(pwd)/userland/desktop/fonts/DroidSansFallbackFull.ttf" "$out/fonts/keiland-fallback.ttf"
convert -size 8x4 gradient:blue-green -quality 90 "$out/insert.jpg"
libpdf="userland/base/libpdf/writer.c userland/base/libpdf/update.c userland/base/libpdf/outline.c userland/base/libpdf/object.c
	userland/base/libpdf/reader.c userland/base/libpdf/filter.c userland/base/libpdf/ccitt.c userland/base/libpdf/crypt.c
	userland/base/libpdf/image.c userland/base/libpdf/display.c userland/base/libpdf/content.c userland/base/libpdf/editor.c userland/base/libpdf/tounicode.c userland/base/libpdf/intake.c userland/base/libpdf/replace.c userland/base/libpdf/embed.c userland/base/libpdf/subset.c
	userland/base/libpdf/stroke.c userland/base/libpdf/raster.c userland/base/libpdf/font.c userland/base/libpdf/encoding.c
	userland/base/libpdf/shading.c userland/base/libpdf/charstrings.c userland/base/libpdf/type1.c userland/base/libpdf/cff.c
	userland/base/libpdf/cffdata.c"
status=0
for variant in plain asan ubsan; do
	flags="-std=c89 -pedantic -O1 -g -Wall -Wextra -Werror -D_DEFAULT_SOURCE -I$out/include -DPDF_EDIT_FONT_DIRECTORY=\"$out/fonts\""
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
	for test in host-edit-scan host-edit-change host-edit-image host-edit-intake host-edit-blank host-edit-text-change host-edit-insert-text host-tounicode host-font-unicode host-edit-text; do
		# shellcheck disable=SC2086
		"$cc" $flags -Wno-overlength-strings -Iuserland/base/libpdf $libpdf "plan/ws175/tests/$test.c" $objects -lm \
			-o "$out/$test-$variant"
	done
	if "$out/host-edit-scan-$variant" "$out/edit-images.pdf" > "$out/scan-$variant.txt" 2>&1; then
		echo "host-edit-scan $variant: $(tail -1 "$out/scan-$variant.txt")"
	else
		grep -v '^ok' "$out/scan-$variant.txt"
		status=1
	fi
	if "$out/host-edit-change-$variant" "$out/edit-images.pdf" "$out/edited-$variant.pdf" > "$out/change-$variant.txt" 2>&1; then
		echo "host-edit-change $variant: $(tail -1 "$out/change-$variant.txt")"
	else
		grep -v '^ok' "$out/change-$variant.txt"
		status=1
	fi
	if "$out/host-tounicode-$variant" > "$out/tounicode-$variant.txt" 2>&1; then
		echo "host-tounicode $variant: $(tail -1 "$out/tounicode-$variant.txt")"
	else
		grep -v '^ok' "$out/tounicode-$variant.txt"
		status=1
	fi
	if "$out/host-font-unicode-$variant" "$out/edit-images.pdf" /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf > "$out/font-unicode-$variant.txt" 2>&1; then
		echo "host-font-unicode $variant: $(tail -1 "$out/font-unicode-$variant.txt")"
	else
		grep -v '^ok' "$out/font-unicode-$variant.txt"
		status=1
	fi
	if "$out/host-edit-text-$variant" "$out/edit-images.pdf" > "$out/text-$variant.txt" 2>&1; then
		echo "host-edit-text $variant: $(tail -1 "$out/text-$variant.txt")"
	else
		grep -v '^ok' "$out/text-$variant.txt"
		status=1
	fi
	if "$out/host-edit-image-$variant" "$out/edit-images.pdf" "$out/insert.jpg" "$out/imaged-$variant.pdf" > "$out/image-$variant.txt" 2>&1; then
		echo "host-edit-image $variant: $(tail -1 "$out/image-$variant.txt")"
	else
		grep -v '^ok' "$out/image-$variant.txt"
		status=1
	fi
	if "$out/host-edit-intake-$variant" "$out/edit-images.pdf" "$out/insert.jpg" "$out/intake-$variant.pdf" > "$out/intake-$variant.txt" 2>&1; then
		echo "host-edit-intake $variant: $(tail -1 "$out/intake-$variant.txt")"
	else
		grep -v '^ok' "$out/intake-$variant.txt"
		status=1
	fi
	if "$out/host-edit-blank-$variant" "$out/insert.jpg" "$out/blank-$variant.pdf" > "$out/blank-$variant.txt" 2>&1; then
		echo "host-edit-blank $variant: $(tail -1 "$out/blank-$variant.txt")"
	else
		grep -v '^ok' "$out/blank-$variant.txt"
		status=1
	fi
	if "$out/host-edit-text-change-$variant" "$out/edit-text.pdf" "$out/texted-$variant.pdf" > "$out/text-change-$variant.txt" 2>&1; then
		echo "host-edit-text-change $variant: $(tail -1 "$out/text-change-$variant.txt")"
	else
		grep -v '^ok' "$out/text-change-$variant.txt"
		status=1
	fi
	if "$out/host-edit-insert-text-$variant" "$out/edit-text.pdf" "$out/inserted-$variant.pdf" "$out/blank-text-$variant.pdf" > "$out/insert-text-$variant.txt" 2>&1; then
		echo "host-edit-insert-text $variant: $(tail -1 "$out/insert-text-$variant.txt")"
	else
		grep -v '^ok' "$out/insert-text-$variant.txt"
		status=1
	fi
	words=$(pdftotext "$out/inserted-$variant.pdf" - 2>/dev/null | tr '\n' ' ')
	case "$words" in
	*Hello*"日本語"*second*) echo "pdftotext inserted-$variant.pdf: the inserted words are there" ;;
	*) echo "FAIL pdftotext inserted-$variant.pdf: $words"; status=1 ;;
	esac
	# The rewritten lines as another reader reads them.
	words=$(pdftotext "$out/texted-$variant.pdf" - 2>/dev/null | tr '\n' ' ')
	case "$words" in
	*"Changed!"*"Marker"*"Namer"*) echo "pdftotext texted-$variant.pdf: Changed!, Marker and Namer are there" ;;
	*) echo "FAIL pdftotext texted-$variant.pdf: $words"; status=1 ;;
	esac
	# The stale /ActualText went with the words it stood for (design.md [M10][N15]): no Old, no Older.
	case "$words" in
	*Old*) echo "FAIL pdftotext texted-$variant.pdf still reads an /ActualText: $words"; status=1 ;;
	*) echo "pdftotext texted-$variant.pdf: no stale /ActualText" ;;
	esac
	# The only error qpdf may find is the sample's own: page 3's stream of a filter no reader decodes here.
	for saved in edited imaged intake blank texted inserted blank-text; do
		errors=$(qpdf --check "$out/$saved-$variant.pdf" 2>&1 | grep 'ERROR' | grep -v 'page 3: content stream' || true)
		if [ -z "$errors" ]; then
			echo "qpdf --check $saved-$variant.pdf: ok (page 3's own stream only)"
		else
			echo "$errors"
			status=1
		fi
	done
done
# ws175-p005: the TrueType subset of DejaVu (composites), JetBrains Mono, Droid Sans Fallback and Mahora, read by fontTools.
"$cc" -std=c89 -pedantic -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I"$out/include" -Iuserland/base/libpdf \
	userland/base/libpdf/subset.c plan/ws175/tests/host-truetype-subset.c -o "$out/host-truetype-subset"
for pair in "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf|Héllo Ǻ" "userland/desktop/fonts/DroidSansFallbackFull.ttf|日本語テスト" \
	"userland/desktop/fonts/JetBrainsMono-Regular.ttf|Hello é" "userland/desktop/fonts/Mahora-Regular.ttf|Hello"; do
	font=${pair%%|*}
	words=${pair#*|}
	gids=$(python3 -c 'import sys
from fontTools.ttLib import TTFont
t = TTFont(sys.argv[1]); c = t.getBestCmap(); o = t.getGlyphOrder()
print(" ".join(str(o.index(c[ord(ch)])) for ch in sys.argv[2] if ord(ch) in c))' "$font" "$words")
	# shellcheck disable=SC2086
	if "$out/host-truetype-subset" "$font" "$out/subset.ttf" $gids > "$out/subset.txt" 2>&1 &&
		python3 plan/ws175/tests/check-subset.py "$font" "$out/subset.ttf" "$words" >> "$out/subset.txt" 2>&1; then
		echo "subset $(basename "$font"): $(tail -1 "$out/subset.txt")"
	else
		cat "$out/subset.txt"
		status=1
	fi
done
[ $status -eq 0 ] && echo "run-host-edit-scan: PASS" || echo "run-host-edit-scan: FAIL"
exit $status
