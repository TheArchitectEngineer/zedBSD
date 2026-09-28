#!/bin/sh
# ws079-p008: the demo check of PDF Viewer and Notes on real-world PDFs, on the Venus guest (zdesktop --glass at
# 1280x800), with this worktree's pdfviewer, notes, libpdf and libtruetype copied into a copy of main's zdesktop image.
#   start IMAGE  boots the guest and waits for SSH
#   install      copies the programs, the libraries and the documents in, starts the compositor
#   quilt        quilt.pdf (pdfTeX, embedded Type 1 Computer Modern): page 1, zoomed, and page 3
#   faq          debian-faq.pdf (xdvipdfmx: CFF and CIDFontType2, cross-reference and object streams): page 3, zoomed
#   refcard      txirefcard.pdf (CFF Type1C, a Type 3 font): page 1, zoomed
#   programs     programs.pdf (make-text-pdfs.py: Type 1, CFF, OpenType CFF, CID-keyed CFF across and down)
#   encrypted    programs.pdf encrypted with AES-256 and an empty user password opens; one with a user password is
#                refused with "it is protected by a password"
#   notice       gnus-logo.pdf (a CCITTFax image libpdf leaves out): "Some content could not be shown"
#   annotate     quilt.pdf in PDF Viewer, Annotate in Notes (Ctrl+E): the page is Notes' background, pen and marker
#                strokes, Ctrl+S; the saved file is fetched and drawn by pdftoppm on the host
#   refuse       Annotate on the encrypted programs.pdf: Notes refuses it (NOTES OPEN failed error=25, EACCES on the guest; the notice)
#   stop         stops the guest
# Pictures: OUTDIR/*.png, and with PREFIX also PREFIX*.png.  Program logs (PDFVIEWER ..., NOTES ...) are read over SSH;
# nothing reads the console.
#
#   GUEST_RUNTIME=$PWD/build/ws079-p008-run/rt BIN=build/ws079-p007-amd64 \
#       plan/ws079/tests/pdf-demo-guest.sh OUTDIR PREFIX start install quilt faq refcard programs encrypted notice annotate refuse stop
# The documents come from build/ws079-p007-host (run-pdf-text.sh: programs.pdf, crypt/, real/).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws079-p008-run/rt}"
export GUEST_RUNTIME
bin=${BIN:-build/ws079-p007-amd64}
host=${HOST_OUT:-build/ws079-p007-host}
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
	sleep 1
	python3 plan/ws035/tests/zdesktop-check.py "$out/$1" --runtime "$GUEST_RUNTIME" >/dev/null
	[ -n "$prefix" ] && cp "$out/$1" "$prefix$1"
	echo "shot $1"
}
# Fails the run unless a log has a line matching a pattern (within a few seconds).
expect_log() {
	tries=0
	found=0
	while [ $tries -lt 20 ]; do
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
	guest "export XDG_RUNTIME_DIR=/tmp; /bin/pdfviewer --width=1180 --height=740 $2 $1 > /tmp/pv.log 2>&1 </dev/null & sleep 6; echo started" >/dev/null
	expect_log /tmp/pv.log 'PDFVIEWER READY'
	pointer move 640 420 sleep 300 >/dev/null
}
pages() {
	guest "grep -E 'PDFVIEWER (OPEN|PAGE|RASTER|NOTICE|MESSAGE)' /tmp/pv.log | head -${1:-6}"
}
# The page's place in the fullscreen Notes: x y width height, from the last NOTES LAYOUT line of the full screen.
page_place() {
	guest "grep 'NOTES LAYOUT window=1280x800' /tmp/pv.log | tail -1" | sed -n 's/.*page=\([0-9-]*\),\([0-9-]*\),\([0-9]*\),\([0-9]*\) .*/\1 \2 \3 \4/p' | tail -1
}
# Draws a wave across the page shown, at a share of its height, with the pointer.
wave() {
	place=$(page_place)
	[ -n "$place" ] || { echo "layout: MISSING"; status=1; return; }
	set -- $place $1
	python3 - "$@" > "$out/wave.args" <<'EOF'
import math, sys
x, y, w, h, share = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), float(sys.argv[5])
left, right, top = x + w * 0.15, x + w * 0.85, y + h * share
args = []
for step in range(0, 41):
    px = left + (right - left) * step / 40
    py = top + h * 0.02 * math.sin(step / 40 * 4 * math.pi)
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
	start)
		plan/ws035/tests/zdesktop-guest.sh start "${IMAGE:-build/ws079-p008-run/hdd-image.img}"
		timeout 300 python3 plan/tools/guest/guest.py wait --timeout 240 || status=1
		;;
	install)
		put "$bin/bin/pdfviewer" /tmp/pdfviewer
		put "$bin/bin/notes" /tmp/notes-program
		put "$bin/dynamic/libpdf.so" /tmp/libpdf.so
		put "$bin/dynamic/libtruetype.so" /tmp/libtruetype.so
		guest 'rm -rf /tmp/demo /root/.local/share/keiland/notes; mkdir -p /tmp/demo' >/dev/null
		put "$host/real/quilt.pdf" /tmp/demo/quilt.pdf
		put "$host/real/debian-faq.pdf" /tmp/demo/debian-faq.pdf
		put "$host/real/txirefcard.pdf" /tmp/demo/txirefcard.pdf
		put "$host/real/gnus-logo.pdf" /tmp/demo/gnus-logo.pdf
		put "$host/programs.pdf" /tmp/demo/programs.pdf
		put "$host/crypt/programs-aes-256.pdf" /tmp/demo/encrypted.pdf
		put "$host/crypt/password.pdf" /tmp/demo/password.pdf
		guest 'for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[p]dfviewer|[n]otes( |$)" | awk "{print \$1}"); do kill $p; done; sleep 1
cp /tmp/pdfviewer /bin/pdfviewer && cp /tmp/notes-program /bin/notes && cp /tmp/libpdf.so /lib/libpdf.so && cp /tmp/libtruetype.so /lib/libtruetype.so &&
chmod 0755 /bin/pdfviewer /bin/notes && chmod 0644 /lib/libpdf.so /lib/libtruetype.so && cksum /lib/libpdf.so && echo installed' | tail -2
		guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
/bin/wayland --timeout=3600 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 7; echo started' | tail -1
		;;
	quilt)
		viewer /tmp/demo/quilt.pdf --mode=page
		expect_log /tmp/pv.log 'PDFVIEWER OPEN path=/tmp/demo/quilt.pdf pages=12'
		expect_log /tmp/pv.log 'PDFVIEWER PAGE index=0 items=[0-9]+ flags=0 '
		shot quilt.png
		keys '<ctrl-equal>' '<ctrl-equal>' '<ctrl-equal>'
		sleep 2
		shot quilt-zoom.png
		keys '<ctrl-0>' '<pgdn>' '<pgdn>'
		sleep 2
		shot quilt-3.png
		pages 8
		;;
	faq)
		viewer /tmp/demo/debian-faq.pdf --mode=page
		keys '<pgdn>' '<pgdn>'
		sleep 2
		shot faq-3.png
		keys '<ctrl-equal>' '<ctrl-equal>' '<ctrl-equal>'
		sleep 2
		shot faq-3-zoom.png
		pages 8
		;;
	refcard)
		viewer /tmp/demo/txirefcard.pdf --mode=page
		shot refcard.png
		keys '<ctrl-equal>' '<ctrl-equal>' '<ctrl-equal>' '<ctrl-equal>'
		sleep 2
		shot refcard-zoom.png
		pages 6
		;;
	programs)
		viewer /tmp/demo/programs.pdf --mode=page
		expect_log /tmp/pv.log 'PDFVIEWER PAGE index=0 items=[0-9]+ flags=0 '
		shot programs.png
		pages 4
		;;
	encrypted)
		viewer /tmp/demo/encrypted.pdf --mode=page
		expect_log /tmp/pv.log 'PDFVIEWER OPEN path=/tmp/demo/encrypted.pdf pages=1'
		shot encrypted.png
		viewer /tmp/demo/password.pdf --mode=page
		expect_log /tmp/pv.log 'PDFVIEWER MESSAGE Cannot open password.pdf: it is protected by a password.'
		shot password.png
		pages 4
		;;
	notice)
		viewer /tmp/demo/gnus-logo.pdf --mode=page
		expect_log /tmp/pv.log 'PDFVIEWER NOTICE shown flags=1'
		shot notice.png
		pages 4
		;;
	annotate)
		viewer /tmp/demo/quilt.pdf ""
		expect_log /tmp/pv.log 'PDFVIEWER OPEN path=/tmp/demo/quilt.pdf pages=12'
		keys '<ctrl-e>'
		expect_log /tmp/pv.log 'PDFVIEWER ANNOTATE program=/bin/notes path=/tmp/demo/quilt.pdf'
		expect_log /tmp/pv.log 'NOTES OPEN pages=12 strokes=0 kind=foreign path=/tmp/demo/quilt.pdf'
		expect_log /tmp/pv.log 'NOTES BACKGROUND source=0 '
		sleep 2
		shot annotate-window.png
		keys '<f11>'
		expect_log /tmp/pv.log 'NOTES LAYOUT window=1280x800'
		sleep 1
		wave 0.36
		expect_log /tmp/pv.log 'NOTES STROKE page=0 id=1 tool=0'
		keys 'm'
		wave 0.26
		expect_log /tmp/pv.log 'NOTES STROKE page=0 id=2 tool=1'
		pointer move 1270 790 sleep 300 >/dev/null
		shot annotate-page1.png
		keys '<ctrl-s>'
		expect_log /tmp/pv.log 'NOTES SAVE reason=request pages=12 strokes=2 '
		get /tmp/demo/quilt.pdf "$out/quilt-annotated.pdf"
		if [ -f "$out/quilt-annotated.pdf" ]; then
			qpdf --check "$out/quilt-annotated.pdf" > "$out/qpdf-annotated.txt" 2>&1 || { echo "qpdf: annotated FAILED"; status=1; }
			pdftoppm -r 60 -f 1 -l 1 -png -singlefile "$out/quilt-annotated.pdf" "$out/annotated-pdftoppm"
			[ -n "$prefix" ] && cp "$out/annotated-pdftoppm.png" "${prefix}annotated-pdftoppm.png"
			echo "annotated: $(wc -c < "$out/quilt-annotated.pdf") bytes, $(grep -c 'No syntax or stream encoding errors' "$out/qpdf-annotated.txt") clean"
		fi
		keys '<ctrl-w>'
		;;
	refuse)
		viewer /tmp/demo/encrypted.pdf ""
		keys '<ctrl-e>'
		expect_log /tmp/pv.log 'NOTES OPEN failed error=25'
		sleep 2
		shot refuse-encrypted.png
		guest "grep -E 'NOTES (OPEN|MESSAGE|NOTICE)' /tmp/pv.log | tail -4"
		;;
	stop)
		guest "$stop_apps" >/dev/null
		guest 'grep -c ERROR /tmp/zdesktop.log' | tail -1 | sed 's/^/zdesktop ERROR lines: /'
		timeout 60 python3 plan/tools/guest/guest.py stop
		;;
	*)
		echo "unknown step: $step"
		status=1
		;;
	esac
done
[ "$status" = 0 ] && echo "pdf-demo-guest: ok" || echo "pdf-demo-guest: FAILED"
exit "$status"
