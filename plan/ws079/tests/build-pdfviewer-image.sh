#!/bin/sh
# ws079-p006: builds the lean Venus guest image of the File Manager tests (plan/tools/files/config-amd64-files.mk,
# which has PDF Viewer and libpdf since ws079-p006) with the guest harness's files, the wallpaper, the sample home
# maker and the two test documents notes.pdf and ops.pdf.  The documents are written from the tree by libpdf's host
# test (plan/ws079/tests/run-pdf-render.sh, into build/ws079-p006-host), which this runs when they are not there.
# ws136-p001 (2026-10-04): the image is a config.mk build plus files of the tree (plan/tools/guest/test-image.sh).
# The fonts come with the compositor's package (userland/desktop/fonts/); the wallpaper is
# userland/desktop/keiland/wallpapers/Birch-Lake.png.
#
#   plan/ws079/tests/build-pdfviewer-image.sh BUILD
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:?usage: build-pdfviewer-image.sh BUILD}
pdfs=build/ws079-p006-host
if [ ! -f $pdfs/notes.pdf ] || [ ! -f $pdfs/ops.pdf ]; then
	sh plan/ws079/tests/run-pdf-render.sh 1 >/dev/null
fi
exec plan/tools/guest/test-image.sh plan/tools/files/config-amd64-files.mk "$build" \
	--file /usr/share/keiland/wallpaper.png=userland/desktop/keiland/wallpapers/Birch-Lake.png \
	--file /usr/share/files-tests/make-home.sh=plan/tools/files/make-home.sh \
	--file /usr/share/pdfviewer-tests/notes.pdf=$pdfs/notes.pdf \
	--file /usr/share/pdfviewer-tests/ops.pdf=$pdfs/ops.pdf
