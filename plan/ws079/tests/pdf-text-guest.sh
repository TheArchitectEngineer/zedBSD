#!/bin/sh
# ws079-p007: text and shadings in PDF Viewer on the Venus guest (a copy of the zdesktop image booted by
# plan/ws035/tests/zdesktop-guest.sh, with this worktree's pdfviewer, libpdf and libtruetype copied in).
# zdesktop --glass at 1280x800.  The substitute fonts are the image's own (/usr/share/fonts/keiland.ttf is
# Inter, keiland-mono.ttf JetBrains Mono).
#   start IMAGE    boots the guest and waits for SSH
#   install        copies the binaries, the libraries and the documents in, starts the compositor
#   simple         text-simple.pdf: embedded simple TrueType fonts, the text operators (simple*.png)
#   cid            text-cid.pdf: Type0 Identity-H/V (cid*.png)
#   std14          text-std14.pdf: the standard 14 fonts through the substitutes, zoomed (std14*.png)
#   shading        shading.pdf: axial and radial shadings and shading patterns (shading.png)
#   faq            debian-faq.pdf (xdvipdfmx: cross-reference stream, object streams, CIDFontType2), page 3
#                  zoomed and page 12 (faq*.png)
#   quilt          quilt.pdf (pdfTeX: Type 1 fonts drawn through the substitutes), page 1 (quilt*.png)
#   stop           stops the guest
# The documents come from build/ws079-p007-host (run-pdf-text.sh) and DOCS (the host's /usr/share/doc PDFs).
# The steps read the program's own log lines (PDFVIEWER ...) through SSH; nothing reads the console.
#   GUEST_RUNTIME=... BIN=... DOCS=... plan/ws079/tests/pdf-text-guest.sh OUTDIR PREFIX STEP...
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws079-p007-run/rt}"
export GUEST_RUNTIME
bin=${BIN:-build/ws079-p007-amd64}
host=${HOST_OUT:-build/ws079-p007-host}
docs=${DOCS:-$host/real-docs}
out=$1
prefix=$2
shift 2
mkdir -p "$out"
status=0
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
put() { timeout 120 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1 </dev/null || { echo "put $1: FAILED"; status=1; }; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@" >/dev/null; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
shot() {
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
stop_viewer='for p in $(ps -A -o pid,args | grep -E "[p]dfviewer" | awk "{print \$1}"); do kill $p; done; sleep 1'
viewer() {
	guest "$stop_viewer" >/dev/null
	guest "export XDG_RUNTIME_DIR=/tmp; /bin/pdfviewer --width=1180 --height=740 $2 $1 > /tmp/pv.log 2>&1 </dev/null & sleep 6; echo started" >/dev/null
	expect_log /tmp/pv.log 'PDFVIEWER READY'
	expect_log /tmp/pv.log 'PDFVIEWER OPEN'
	pointer move 640 420 sleep 300 >/dev/null
}
pages() {
	guest "grep -E 'PDFVIEWER (OPEN|PAGE|RASTER)' /tmp/pv.log | head -${1:-6}"
}

for step in "$@"; do
	case "$step" in
	start)
		plan/ws035/tests/zdesktop-guest.sh start "${IMAGE:-build/ws079-p007-run/hdd-image.img}"
		timeout 300 python3 plan/tools/guest/guest.py wait --timeout 240 || status=1
		;;
	install)
		put "$bin/bin/pdfviewer" /tmp/pdfviewer
		put "$bin/dynamic/libpdf.so" /tmp/libpdf.so
		put "$bin/dynamic/libtruetype.so" /tmp/libtruetype.so
		put "$host/text-simple.pdf" /tmp/text-simple.pdf
		put "$host/text-cid.pdf" /tmp/text-cid.pdf
		put "$host/text-std14.pdf" /tmp/text-std14.pdf
		put "$host/shading.pdf" /tmp/shading.pdf
		put "$docs/debian-faq.pdf" /tmp/debian-faq.pdf
		put "$docs/quilt.pdf" /tmp/quilt.pdf
		guest 'for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[p]dfviewer" | awk "{print \$1}"); do kill $p; done; sleep 1
cp /tmp/pdfviewer /bin/pdfviewer && cp /tmp/libpdf.so /lib/libpdf.so && cp /tmp/libtruetype.so /lib/libtruetype.so &&
chmod 0755 /bin/pdfviewer && chmod 0644 /lib/libpdf.so /lib/libtruetype.so && ls -l /usr/share/fonts && echo installed'
		guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --timeout=1800 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 7; echo started'
		;;
	simple)
		viewer /tmp/text-simple.pdf --mode=page
		shot simple.png
		keys '<ctrl-equal>' '<ctrl-equal>' '<ctrl-equal>'
		sleep 2
		shot simple-zoom.png
		pages 4
		;;
	cid)
		viewer /tmp/text-cid.pdf --mode=page
		shot cid.png
		pages 4
		;;
	std14)
		viewer /tmp/text-std14.pdf --mode=page
		shot std14.png
		keys '<ctrl-equal>' '<ctrl-equal>' '<ctrl-equal>'
		sleep 2
		shot std14-zoom.png
		pages 4
		;;
	shading)
		viewer /tmp/shading.pdf --mode=page
		shot shading.png
		pages 4
		;;
	faq)
		viewer /tmp/debian-faq.pdf --mode=page
		keys '<pgdn>'
		sleep 1
		keys '<pgdn>'
		sleep 2
		shot faq-3.png
		keys '<ctrl-equal>' '<ctrl-equal>' '<ctrl-equal>'
		sleep 2
		shot faq-3-zoom.png
		keys '<ctrl-0>'
		sleep 1
		keys '<pgdn>' '<pgdn>' '<pgdn>' '<pgdn>' '<pgdn>' '<pgdn>' '<pgdn>' '<pgdn>' '<pgdn>'
		sleep 3
		shot faq-12.png
		pages 20
		;;
	quilt)
		viewer /tmp/quilt.pdf --mode=page
		shot quilt.png
		keys '<ctrl-equal>' '<ctrl-equal>' '<ctrl-equal>'
		sleep 2
		shot quilt-zoom.png
		pages 4
		;;
	stop)
		guest "$stop_viewer" >/dev/null
		timeout 60 python3 plan/tools/guest/guest.py stop
		;;
	*)
		echo "unknown step: $step"
		status=1
		;;
	esac
done
[ "$status" = 0 ] && echo "pdf-text-guest: ok" || echo "pdf-text-guest: FAILED"
exit "$status"
