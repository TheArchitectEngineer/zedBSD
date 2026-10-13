#!/bin/sh
# ws216-p003: builds Notes' model, edit data, journal and PDF (with shape.c) and the whole of libpdf with the host's C
# compiler (plain, ASan and UBSan) into host-notes-tools, and the Notes of main d3a486f37 (before the drawing tools)
# into host-notes-old, and runs the acceptance of plan/ws216/phase003/phase.md section 8:
#   B2, B5 to B8   host-notes-tools run (the edit data 3.0, the journal 3, the eraser, the pens, the PDF)
#   B3             the old Notes' 1.1 and 2.0 notebooks and its journal 2 read by the new one, the same edit data
#                  bytes and page content written for notebooks without the tools, no /Filter in their content
#   B4             the old Notes opens a 3.0 notebook as another program's PDF and writes a pen on it; the new opens it
#   B8             qpdf --check, the content compressed only in 3.0, the clean copy drawn like the notebook
#   B9             the pencil's grain at 1536 dpi with pdftoppm on a new page, an A4 page and a turned smaller page
#   sh plan/ws216/tests/run-notes-tools.sh
# Output: build/ws216-p003-host (a new run each time behind the name; nothing is removed, Q1 cleans the old runs).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/ws216-p003-host
cc=${CC:-cc}
old=d3a486f37
. plan/tools/fresh-out.sh
fresh_out "$out"
# The journals live under XDG_DATA_HOME, which Notes takes only when it is absolute (else the user's own folder).
out=$(cd "$out" && pwd -P)
mkdir -p "$out/include/truetype" "$out/old"
ln -sfn "$(pwd)/include/libc/compat" "$out/include/compat"
for header in pdf.h sha2.h md5.h sha1.h; do
	ln -sf "$(pwd)/include/libc/$header" "$out/include/$header"
done
ln -sf "$(pwd)/userland/desktop/include/truetype/truetype.h" "$out/include/truetype/truetype.h"
# The Notes before the drawing tools.
for file in notes.h document.c edit.c encode.c journal.c save.c; do
	git show "$old:userland/desktop/notes/$file" > "$out/old/$file"
done
notes="userland/desktop/notes/document.c userland/desktop/notes/edit.c userland/desktop/notes/encode.c
	userland/desktop/notes/journal.c userland/desktop/notes/save.c userland/desktop/notes/shape.c"
old_notes="$out/old/document.c $out/old/edit.c $out/old/encode.c $out/old/journal.c $out/old/save.c"
libpdf=$(ls userland/desktop/libpdf/*.c)
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
	for file in userland/base/libz-compat/*.c userland/desktop/libjpeg-compat/*.c userland/desktop/libtruetype/*.c; do
		object="$out/$(basename "$(dirname "$file")")-$(basename "$file" .c)-$variant.o"
		"$cc" $loose -w -I"$(dirname "$file")" -c "$file" -o "$object"
		objects="$objects $object"
	done
	# shellcheck disable=SC2086
	"$cc" $flags -Iuserland/desktop/notes $notes $libpdf plan/ws216/tests/host-notes-tools.c $objects -lm -o "$out/host-notes-tools-$variant"
	if [ "$variant" = plain ]; then
		# shellcheck disable=SC2086
		"$cc" $flags -I"$out/old" $old_notes $libpdf plan/ws216/tests/host-notes-old.c $objects -lm -o "$out/host-notes-old"
	fi
done
new="$out/host-notes-tools-plain"
oldbin="$out/host-notes-old"
convert -size 8x4 xc:red "$out/insert.jpg"

# B3, B5: the old Notes' files, read by each build of the new one (each on a copy of its own).
mkdir -p "$out/oldfiles"
timeout 120 "$oldbin" make "$out/oldfiles" "$out/insert.jpg"
for variant in plain asan ubsan; do
	cp -R "$out/oldfiles" "$out/oldfiles-$variant"
	timeout 300 "$out/host-notes-tools-$variant" old "$out/oldfiles-$variant"
done

# B2, B5 to B8: the model and the formats, with each build.
for variant in plain asan ubsan; do
	timeout 600 "$out/host-notes-tools-$variant" run "$out/run-$variant"
done
cmp "$out/run-plain/t30.bin" "$out/run-asan/t30.bin"
cmp "$out/run-plain/t30.bin" "$out/run-ubsan/t30.bin"
run="$out/run-plain"

# B3: the edit data and the PDF of notebooks without the tools are written as the old Notes wrote them.
for name in old11 old20; do
	"$oldbin" reencode "$out/oldfiles/$name.bin" "$out/$name-old.bin"
	"$new" reencode "$out/oldfiles/$name.bin" "$out/$name-new.bin"
	cmp "$out/$name-old.bin" "$out/$name-new.bin"
	cmp "$out/$name-old.bin" "$out/oldfiles/$name.bin"
done
for bin in "$out/oldfiles/old11.bin" "$out/oldfiles/old20.pdf" "$run/pens.bin"; do
	name=$(basename "$(basename "$bin" .bin)" .pdf)
	"$oldbin" save "$bin" "$out/$name-old.pdf"
	"$new" save "$bin" "$out/$name-new.pdf"
	for side in old new; do
		qpdf --qdf --object-streams=disable --static-id "$out/$name-$side.pdf" - | grep -av 'Date' > "$out/$name-$side.qdf"
	done
	cmp "$out/$name-old.qdf" "$out/$name-new.qdf"
	echo "B3: $name: the new Notes writes the old Notes' PDF (dates aside)"
done

# B8: qpdf --check, and the page content compressed in 3.0 only.
for file in "$run/t30.pdf" "$run/t30-clean.pdf" "$run/grain-new.pdf" "$out/oldfiles/old11.pdf" "$out/oldfiles/old20.pdf" "$out/pens-new.pdf"; do
	qpdf --check "$file" > "$out/check.txt" 2>&1 || { cat "$out/check.txt"; exit 1; }
	grep -q "No syntax or stream encoding errors found" "$out/check.txt"
done
contents_filters() {
	qpdf --json=2 "$1" | python3 -I -c '
import json, sys
document = json.load(sys.stdin)
objects = document["qpdf"][1]
filters = []
for page in document["pages"]:
    for reference in page["contents"]:
        stream = objects["obj:" + reference]["stream"]
        filters.append(str(stream["dict"].get("/Filter", "none")))
print(" ".join(filters))'
}
[ "$(contents_filters "$run/t30.pdf")" = "/FlateDecode /FlateDecode" ]
for file in "$out/oldfiles/old11.pdf" "$out/oldfiles/old20.pdf" "$out/pens-new.pdf"; do
	case " $(contents_filters "$file") " in
	*Flate*) echo "FAIL: $file: compressed content"; exit 1 ;;
	esac
done
grep -aq '/BM /Multiply' "$run/t30.pdf" || qpdf --qdf --object-streams=disable "$run/t30.pdf" - | grep -aq '/BM /Multiply'
echo "B8: content compressed in 3.0 only, the watercolour multiplied"

# B8: the clean copy is drawn as the notebook.
pdftoppm -r 72 -f 1 -l 1 -singlefile "$run/t30.pdf" "$out/t30"
pdftoppm -r 72 -f 1 -l 1 -singlefile "$run/t30-clean.pdf" "$out/t30-clean"
"$new" ppmdiff "$out/t30.ppm" "$out/t30-clean.ppm" | tee "$out/clean-diff.txt"
grep -q "mean=0.000 apart=0.0000%" "$out/clean-diff.txt"
pdftoppm -r 100 -png "$run/t30.pdf" "$out/t30-page"

# B4: the old Notes takes a 3.0 notebook as another program's PDF and writes a pen on it.
"$oldbin" open "$run/t30.pdf" "$out/m9.pdf" | tee "$out/m9.txt"
grep -qx "opened 2" "$out/m9.txt"
"$new" m9 "$out/m9.pdf"

# B9: the grain, at 1536 dpi, in a crop well inside a pencil 300 points wide.
"$new" base "$out"
qpdf --rotate=+90:2 "$out/base.pdf" "$out/base-rot.pdf"
"$new" grain-pages "$out"
qpdf --check "$out/grain-foreign.pdf" > "$out/check.txt" 2>&1 || { cat "$out/check.txt"; exit 1; }
pdftoppm -r 1536 -f 1 -l 1 -x 3200 -y 2200 -W 300 -H 300 -singlefile "$run/grain-new.pdf" "$out/grain-new"
"$new" grain "$out/grain-new.ppm" 3200 2200 1536
for page in 1 2; do
	pdftoppm -r 1536 -f $page -l $page -x 3200 -y 2200 -W 300 -H 300 -singlefile "$out/grain-foreign.pdf" "$out/grain-foreign-$page"
	"$new" grain "$out/grain-foreign-$page.ppm" 3200 2200 1536
done
pdftoppm -r 72 -png "$out/grain-foreign.pdf" "$out/grain-foreign-page"
echo "run-notes-tools: ok"
