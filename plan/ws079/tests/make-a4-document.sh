#!/bin/sh
# ws079-p016: writes the A4 document of ten pages the page turns are timed on: the first ten pages of the host's
# quilt manual (/usr/share/doc/quilt/quilt.pdf, pdfTeX with embedded Type 1 fonts), fitted to A4 by Ghostscript.
#   plan/ws079/tests/make-a4-document.sh OUT.pdf
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
out=${1:?usage: make-a4-document.sh OUT.pdf}
source=/usr/share/doc/quilt/quilt.pdf
[ -f "$source" ] || { echo "make-a4-document: no $source"; exit 1; }
gs -q -dBATCH -dNOPAUSE -dSAFER -sDEVICE=pdfwrite -sPAPERSIZE=a4 -dFIXEDMEDIA -dPDFFitPage \
    -dFirstPage=1 -dLastPage=10 -sOutputFile="$out" "$source"
pdfinfo "$out" | grep -E '^(Pages|Page size):'
