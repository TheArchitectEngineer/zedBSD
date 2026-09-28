#!/usr/bin/env python3
# ws081-p012: writes the touch tests' document: eight Letter pages, each with ten bands of colour and as many dark
# squares near its top as its number, so that a picture shows which page and how far down.
#   python3 plan/ws081/tests/make-touch-pdf.py OUT.pdf
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import sys

PAGES = 8

objects = {1: b'<< /Type /Catalog /Pages 2 0 R >>'}
kids = []
for index in range(PAGES):
    page = 3 + 2 * index
    content = 4 + 2 * index
    kids.append(b'%d 0 R' % page)
    body = b''
    for band in range(10):
        shade = 0.95 - 0.05 * ((band + index) % 4)
        body += b'%.2f 0.90 %.2f rg 40 %d 532 60 re f\n' % (shade, 1.0 - 0.08 * index, 40 + 70 * band)
    for square in range(index + 1):
        body += b'0.2 0.3 0.6 rg %d 730 30 30 re f\n' % (60 + 40 * square)
    objects[page] = b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents %d 0 R >>' % content
    objects[content] = b'<< /Length %d >>\nstream\n' % len(body) + body + b'endstream'
objects[2] = b'<< /Type /Pages /Kids [' + b' '.join(kids) + b'] /Count %d >>' % PAGES

out = b'%PDF-1.7\n'
offsets = {}
for number in sorted(objects):
    offsets[number] = len(out)
    out += b'%d 0 obj\n' % number + objects[number] + b'\nendobj\n'
xref = len(out)
count = max(objects) + 1
out += b'xref\n0 %d\n0000000000 65535 f \n' % count
out += b''.join(b'%010d 00000 n \n' % offsets[number] for number in range(1, count))
out += b'trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n' % (count, xref)
with open(sys.argv[1], 'wb') as file:
    file.write(out)
