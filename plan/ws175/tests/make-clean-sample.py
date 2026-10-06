#!/usr/bin/env python3
"""make-clean-sample.py: the PDF of ws175-p009's host test (Save Clean Copy).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    make-clean-sample.py OUT

writes OUT, two pages that inherit their box and resources from the page tree node (200 x 100 points; the second
page has its own 300 x 150 box and a link to the first):

  1. image ImA (its bytes QZXQZXQZXQZX, which the test looks for in the files), image ImB, text in F1
  2. text in F1, the ExtGState G1

The resources also hold the font F9, which no page uses.  The test puts the objects into object streams with qpdf.
"""
import sys
from pathlib import Path


def stream(dictionary: bytes, data: bytes) -> bytes:
	"""A stream object's body: its dictionary with its length, and its data."""
	return dictionary[:-2] + b" /Length %d >>\nstream\n" % len(data) + data + b"\nendstream"


def main() -> int:
	if len(sys.argv) != 2:
		print(__doc__, file=sys.stderr)
		return 2
	image = b"<< /Type /XObject /Subtype /Image /Width 2 /Height 2 /ColorSpace /DeviceRGB /BitsPerComponent 8 >>"
	objects = [
		b"<< /Type /Catalog /Pages 2 0 R >>",
		b"<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 /MediaBox [0 0 200 100] /Resources 5 0 R >>",
		b"<< /Type /Page /Parent 2 0 R /Contents 6 0 R >>",
		b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 150] /Contents 7 0 R /Annots [14 0 R] >>",
		b"<< /XObject << /ImA 8 0 R /ImB 9 0 R >> /Font << /F1 10 0 R /F9 11 0 R >> /ExtGState << /G1 12 0 R >> >>",
		stream(b"<< >>", b"q 50 0 0 40 10 20 cm /ImA Do Q\nq 50 0 0 40 100 20 cm /ImB Do Q\nBT /F1 12 Tf 10 80 Td (Page one) Tj ET\n"),
		stream(b"<< >>", b"/G1 gs BT /F1 12 Tf 10 80 Td (Page two) Tj ET\n"),
		stream(image, b"QZXQZXQZXQZX"),
		stream(image, b"\x00\xff\x00\x00\xff\x00\x00\x00\xff\x00\x00\xff"),
		b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>",
		b"<< /Type /Font /Subtype /Type1 /BaseFont /Courier /Encoding /WinAnsiEncoding >>",
		b"<< /Type /ExtGState /ca 0.5 >>",
		b"<< /Title (Clean sample) >>",
		b"<< /Type /Annot /Subtype /Link /Rect [10 10 60 30] /Border [0 0 0] /Dest [3 0 R /Fit] >>",
	]
	out = bytearray(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
	offsets = []
	for number, body in enumerate(objects, start=1):
		offsets.append(len(out))
		out += b"%d 0 obj\n" % number + body + b"\nendobj\n"
	table = len(out)
	out += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objects) + 1)
	for offset in offsets:
		out += b"%010d 00000 n \n" % offset
	out += b"trailer\n<< /Size %d /Root 1 0 R /Info 13 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objects) + 1, table)
	Path(sys.argv[1]).write_bytes(bytes(out))
	return 0


if __name__ == "__main__":
	sys.exit(main())
