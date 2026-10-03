#!/bin/sh
# ws079-p014: Notes writing on another program's PDF, on the Venus guest (zdesktop --glass at 1280x800).
#   install    this worktree's Notes, libpdf and PDF Viewer into the running guest, and the test PDFs: foreign.pdf
#              (qpdf's rewrite of host-pdf-update's hand-made PDF: Flate content, an image, a page turned a quarter,
#              a page without content, an attached file), signed.pdf and encrypted.pdf (run-pdf-update.sh makes them)
#   annotate   PDF Viewer shows foreign.pdf; its Annotate in Notes (Ctrl+E) starts Notes on it: the pages are the
#              background (NOTES OPEN kind=foreign, NOTES BACKGROUND); pen strokes on page 1, on the turned page 2
#              and on a page added after it (Ctrl+N); Ctrl+S; Ctrl+W
#   check      the saved file on the host: it starts with foreign.pdf's bytes unchanged, qpdf --check, four pages,
#              kei-notes.bin, pdftoppm's pages against libpdf's (host-pdf-render compare)
#   viewer     PDF Viewer shows the saved file: the drawing over the original
#   reopen     Annotate in Notes again: the strokes are editable (NOTES OPEN kind=annotated, the eraser removes one,
#              Ctrl+Z puts it back); Ctrl+S saves the same size (the revision is replaced, not piled up)
#   refuse     Notes on signed.pdf (from PDF Viewer's Annotate) and on encrypted.pdf: refused with the message
# Pictures: OUTDIR/*.png, and with PREFIX also PREFIX*.png.  Program logs (NOTES ..., PDFVIEWER ...) are read over SSH.
#
#   GUEST_RUNTIME=$PWD/build/p014/run plan/ws035/tests/zdesktop-guest.sh start IMAGE (the notes test image, ws136-p003)
#   plan/ws079/tests/notes-p014.sh OUTDIR PREFIX install annotate check viewer reopen refuse
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/p014/run}"
export GUEST_RUNTIME
bin=${BIN:-build/amd64}
host=build/ws079-p014-host
out=$1
prefix=$2
shift 2
mkdir -p "$out"
status=0
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
put() { timeout 120 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1 </dev/null || { echo "put $1: FAILED"; status=1; }; }
get() { timeout 120 python3 plan/tools/guest/guest.py get "$1" "$2" >/dev/null 2>&1 </dev/null || { echo "get $1: FAILED"; status=1; }; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@" >/dev/null; sleep 0.8; }
shot() {
	sleep 0.8
	python3 plan/ws035/tests/zdesktop-check.py "$out/$1" --runtime "$GUEST_RUNTIME" >/dev/null
	[ -n "$prefix" ] && cp "$out/$1" "$prefix$1"
	echo "shot $1"
}
# Fails the run unless a log has a line matching a pattern (within a few seconds).
expect_log() {
	tries=0
	found=0
	while [ $tries -lt 15 ]; do
		found=$(guest "grep -cE '$2' $1" | tail -1)
		[ "${found:-0}" -gt 0 ] 2>/dev/null && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "${found:-0}" -gt 0 ] 2>/dev/null; then
		echo "log: $2 ok"
	else
		echo "log: $2 MISSING"
		status=1
	fi
}
stop_apps='for p in $(ps -A -o pid,args | grep -E "[p]dfviewer|[n]otes( |$)" | awk "{print \$1}"); do kill $p; done; sleep 1'
viewer() {
	guest "$stop_apps" >/dev/null
	guest "export XDG_RUNTIME_DIR=/tmp; /bin/pdfviewer --width=1180 --height=700 $2 $1 > /tmp/pv.log 2>&1 </dev/null & sleep 6; echo started" >/dev/null
	expect_log /tmp/pv.log 'PDFVIEWER READY'
}
# The page's place in the fullscreen Notes: x y width height, from the last NOTES LAYOUT line of the full screen.
page_place() {
	guest "grep 'NOTES LAYOUT window=1280x800' /tmp/pv.log | tail -1" | sed -n 's/.*page=\([0-9-]*\),\([0-9-]*\),\([0-9]*\),\([0-9]*\) .*/\1 \2 \3 \4/p' | tail -1
}
# A click on a toolbar button of an action, from the last NOTES BUTTONS line of the full screen's width.
press() {
	place=$(guest "grep 'NOTES BUTTONS' /tmp/pv.log | tail -1" | tr ' ' '\n' | sed -n "s/^$1:\([0-9]*\),\([0-9]*\),\([0-9]*\),\([0-9]*\)$/\1 \2 \3 \4/p" |
	    head -1 | awk '{print int($1 + $3 / 2), int($2 + $4 / 2)}')
	[ -n "$place" ] || { echo "button $1: MISSING"; status=1; return; }
	set -- $place
	pointer move "$1" "$2" sleep 100 down sleep 60 up sleep 300 >/dev/null
}
# Draws a wave across the page shown, at a share of its height, with the pointer.
wave() {
	place=$(page_place)
	[ -n "$place" ] || { echo "layout: MISSING"; status=1; return; }
	set -- $place $1
	python3 - "$@" > "$out/wave.args" <<'EOF'
import math, sys
x, y, w, h, share = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), float(sys.argv[5])
left, right, top = x + w * 0.12, x + w * 0.88, y + h * share
args = []
for step in range(0, 41):
    px = left + (right - left) * step / 40
    py = top + h * 0.035 * math.sin(step / 40 * 4 * math.pi)
    args += ["move", "%d" % px, "%d" % py, "sleep", "25"]
    if step == 0:
        args += ["down", "sleep", "40"]
args += ["up", "sleep", "500"]
print(" ".join(args))
EOF
	pointer $(cat "$out/wave.args") >/dev/null
}

for step in "$@"; do
	case "$step" in
	install)
		put "$bin/bin/notes" /tmp/notes-program
		put "$bin/dynamic/libpdf.so" /tmp/libpdf.so
		put "$bin/bin/pdfviewer" /tmp/pdfviewer
		put "$host/base-qpdf.pdf" /tmp/foreign.pdf
		put "$host/refusals/plain/signed.pdf" /tmp/signed.pdf
		put "$host/refusals/plain/encrypted.pdf" /tmp/encrypted.pdf
		guest 'for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[p]dfviewer|[n]otes( |$)" | awk "{print \$1}"); do kill $p; done; sleep 1
cp /tmp/notes-program /bin/notes && cp /tmp/libpdf.so /lib/libpdf.so && cp /tmp/pdfviewer /bin/pdfviewer && chmod 0755 /bin/notes /bin/pdfviewer &&
chmod 0644 /lib/libpdf.so && rm -rf /tmp/p014 /root/.local/share/keiland/notes && mkdir -p /tmp/p014 &&
cp /tmp/foreign.pdf /tmp/signed.pdf /tmp/encrypted.pdf /tmp/p014/ && echo installed' | tail -1
		guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
/bin/wayland --timeout=3000 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 7; echo started' | tail -1
		;;
	annotate)
		viewer /tmp/p014/foreign.pdf ""
		expect_log /tmp/pv.log 'PDFVIEWER OPEN path=/tmp/p014/foreign.pdf pages=3'
		pointer move 640 420 sleep 300 >/dev/null
		shot viewer-before.png
		keys '<ctrl-e>'
		expect_log /tmp/pv.log 'PDFVIEWER ANNOTATE program=/bin/notes path=/tmp/p014/foreign.pdf'
		expect_log /tmp/pv.log 'NOTES OPEN pages=3 strokes=0 kind=foreign path=/tmp/p014/foreign.pdf'
		expect_log /tmp/pv.log 'NOTES BACKGROUND source=0 '
		sleep 2
		shot notes-window.png
		keys '<f11>'
		expect_log /tmp/pv.log 'NOTES LAYOUT window=1280x800'
		sleep 1
		# Page 1: a black wave, a yellow marker, and a blue wave.
		wave 0.30
		expect_log /tmp/pv.log 'NOTES STROKE page=0 id=1 tool=0'
		keys 'm'
		wave 0.55
		expect_log /tmp/pv.log 'NOTES STROKE page=0 id=2 tool=1'
		keys 'p'
		press 21
		wave 0.75
		expect_log /tmp/pv.log 'NOTES STROKE page=0 id=3 tool=0'
		pointer move 1270 790 sleep 300 >/dev/null
		shot notes-page1.png
		# Page 2, turned a quarter: its own background, and a wave.
		keys '<pgdn>'
		expect_log /tmp/pv.log 'NOTES BACKGROUND source=1 '
		wave 0.20
		expect_log /tmp/pv.log 'NOTES STROKE page=1 id=4 '
		pointer move 1270 790 sleep 300 >/dev/null
		shot notes-page2.png
		# A page added after it (Notes' own), with a wave.
		keys '<ctrl-n>'
		expect_log /tmp/pv.log 'NOTES PAGE current=2 count=4 new'
		wave 0.40
		expect_log /tmp/pv.log 'NOTES STROKE page=2 id=5 '
		keys '<ctrl-s>'
		expect_log /tmp/pv.log 'NOTES SAVE reason=request pages=4 strokes=5 '
		pointer move 1270 790 sleep 300 >/dev/null
		shot notes-added.png
		keys '<ctrl-w>'
		expect_log /tmp/pv.log 'NOTES EXIT pages=4 strokes=5 dirty=0'
		;;
	check)
		get /tmp/p014/foreign.pdf "$out/annotated.pdf"
		size=$(wc -c < "$host/base-qpdf.pdf")
		if cmp -n "$size" "$host/base-qpdf.pdf" "$out/annotated.pdf" >/dev/null; then
			echo "original bytes: untouched ($size bytes of $(wc -c < "$out/annotated.pdf"))"
		else
			echo "original bytes: CHANGED"
			status=1
		fi
		qpdf --check "$out/annotated.pdf" > "$out/qpdf.txt" 2>&1 && grep -q "No syntax or stream encoding errors found" "$out/qpdf.txt" && ! grep -q WARNING "$out/qpdf.txt" &&
		    echo "qpdf: ok" || { cat "$out/qpdf.txt"; echo "qpdf: FAIL"; status=1; }
		pdfinfo "$out/annotated.pdf" | grep -E "^Pages: +4$" >/dev/null && echo "pages: 4" || { echo "pages: WRONG"; status=1; }
		qpdf --list-attachments "$out/annotated.pdf" | tee "$out/attachments.txt"
		grep -q kei-notes.bin "$out/attachments.txt" || { echo "kei-notes.bin: MISSING"; status=1; }
		pdftoppm -cropbox -r 72 "$out/annotated.pdf" "$out/poppler"
		pdftoppm -cropbox -r 72 -png "$out/annotated.pdf" "$out/annotated"
		"$host/host-pdf-render-plain" render "$out/annotated.pdf" "$out/libpdf" 72 > "$out/render.log"
		for page in 1 2 3 4; do
			"$host/host-pdf-render-plain" compare "$out/libpdf-$page.ppm" "$out/poppler-$page.ppm" || status=1
		done
		if [ -n "$prefix" ]; then
			convert "$out/annotated-1.png" "$out/annotated-2.png" "$out/annotated-3.png" "$out/annotated-4.png" -background white -gravity north +append \
			    "${prefix}pdftoppm.png"
		fi
		;;
	viewer)
		viewer /tmp/p014/foreign.pdf ""
		expect_log /tmp/pv.log 'PDFVIEWER OPEN path=/tmp/p014/foreign.pdf pages=4'
		pointer move 640 420 sleep 300 >/dev/null
		shot viewer-after.png
		pointer wheel-down sleep 150 wheel-down sleep 150 wheel-down sleep 150 wheel-down sleep 600 >/dev/null
		shot viewer-after-scroll.png
		;;
	reopen)
		first=$(wc -c < "$out/annotated.pdf" | tr -d ' ')
		viewer /tmp/p014/foreign.pdf ""
		keys '<ctrl-e>'
		expect_log /tmp/pv.log 'NOTES OPEN pages=4 strokes=5 kind=annotated path=/tmp/p014/foreign.pdf'
		sleep 2
		keys '<f11>'
		expect_log /tmp/pv.log 'NOTES LAYOUT window=1280x800'
		sleep 1
		pointer move 1270 790 sleep 300 >/dev/null
		shot notes-reopened.png
		# The strokes are editable: the eraser across the black wave removes it, undo puts it back.
		keys 'e'
		place=$(page_place)
		set -- $place
		cx=$(( $1 + $3 / 2 ))
		top=$(( $2 + $4 * 22 / 100 ))
		bottom=$(( $2 + $4 * 38 / 100 ))
		pointer move "$cx" "$top" sleep 60 down sleep 40 move "$cx" $(( (top + bottom) / 2 )) sleep 40 move "$cx" "$bottom" sleep 40 up sleep 600 >/dev/null
		expect_log /tmp/pv.log 'NOTES ERASE page=0 removed=1 strokes=2'
		pointer move 1270 790 sleep 300 >/dev/null
		shot notes-erased.png
		keys '<ctrl-z>'
		expect_log /tmp/pv.log 'NOTES UNDO page=0 strokes=3'
		keys '<ctrl-s>'
		expect_log /tmp/pv.log 'NOTES SAVE reason=request pages=4 strokes=5 '
		second=$(guest "grep 'NOTES SAVE reason=request pages=4 strokes=5' /tmp/pv.log | tail -1" | sed -n 's/.* bytes=\([0-9]*\) .*/\1/p' | tail -1)
		if [ -n "$first" ] && [ "$first" = "$second" ]; then
			echo "saved again: $second bytes, as the first save (the revision replaced)"
		else
			echo "saved again: $second bytes, first $first: DIFFERENT"
			status=1
		fi
		keys '<ctrl-w>'
		expect_log /tmp/pv.log 'NOTES EXIT pages=4 strokes=5 dirty=0'
		get /tmp/p014/foreign.pdf "$out/annotated-again.pdf"
		size=$(wc -c < "$host/base-qpdf.pdf")
		cmp -n "$size" "$host/base-qpdf.pdf" "$out/annotated-again.pdf" >/dev/null && echo "original bytes: still untouched" || { echo "original bytes: CHANGED"; status=1; }
		qpdf --check "$out/annotated-again.pdf" > "$out/qpdf-again.txt" 2>&1 && echo "qpdf again: ok" || { echo "qpdf again: FAIL"; status=1; }
		;;
	refuse)
		viewer /tmp/p014/signed.pdf ""
		expect_log /tmp/pv.log 'PDFVIEWER OPEN path=/tmp/p014/signed.pdf pages=3'
		keys '<ctrl-e>'
		expect_log /tmp/pv.log 'NOTES OPEN failed error=47 path=/tmp/p014/signed.pdf'
		sleep 2
		shot refuse-signed.png
		guest "$stop_apps" >/dev/null
		guest 'export XDG_RUNTIME_DIR=/tmp; /bin/notes /tmp/p014/encrypted.pdf > /tmp/notes-encrypted.log 2>&1 </dev/null & sleep 6; echo started' >/dev/null
		expect_log /tmp/notes-encrypted.log 'NOTES OPEN failed error=25 path=/tmp/p014/encrypted.pdf'
		shot refuse-encrypted.png
		guest "$stop_apps" >/dev/null
		;;
	esac
done

errors=$(guest "grep -c ERROR /tmp/zdesktop.log" | tail -1)
[ "${errors:-1}" = 0 ] && echo "zdesktop: no ERROR" || { echo "zdesktop: ERROR lines ($errors)"; status=1; }
guest 'cat /tmp/pv.log' > "$out/pv.log"
[ "$status" = 0 ] && echo "notes-p014: PASS" || echo "notes-p014: FAIL"
exit "$status"
