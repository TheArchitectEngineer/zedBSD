#!/usr/bin/env python3
"""make-edit-samples.py: the PDFs of WS175's host tests (ws175-p002a: the images and graphics of the scan).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    make-edit-samples.py FOLDER

writes FOLDER/edit-images.pdf, four pages of 200 x 100 points:

  1. an image XObject drawn through cm; a form XObject (whose own image is not the page's); an inline image; a Q
     without its q at the top level; a q left open at the end
  2. /Rotate 90, an image through cm
  3. two content streams, the second with a filter the reader does not read (/JBIG2Decode): the page is not editable
  4. an image, then q nested 70 deep (past the interpreter's 63) and an image inside: the second is left out

The bytes are written by hand (the cross-reference table computed here), so that the content is exactly the test's.
"""
import sys
from pathlib import Path

IMAGE = bytes([255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255])


def stream(dictionary: bytes, data: bytes) -> bytes:
	"""A stream object's body: its dictionary with its length, and its data."""
	return dictionary[:-2] + b" /Length %d >>\nstream\n" % len(data) + data + b"\nendstream"


def write(path: Path, objects: list[bytes]) -> None:
	"""Writes a PDF of objects 1..n (object 1 the catalog) with its cross-reference table."""
	out = bytearray(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
	offsets = []
	for number, body in enumerate(objects, start=1):
		offsets.append(len(out))
		out += b"%d 0 obj\n" % number + body + b"\nendobj\n"
	table = len(out)
	out += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objects) + 1)
	for offset in offsets:
		out += b"%010d 00000 n \n" % offset
	out += b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objects) + 1, table)
	path.write_bytes(bytes(out))


def main() -> int:
	if len(sys.argv) != 2:
		print(__doc__, file=sys.stderr)
		return 2
	folder = Path(sys.argv[1])
	folder.mkdir(parents=True, exist_ok=True)
	inline = b"q 20 0 0 10 150 60 cm BI /W 2 /H 2 /CS /G /BPC 8 ID \x10\x20\x30\x40 EI Q\n"
	page1 = b"q 50 0 0 40 10 20 cm /Im1 Do Q\nq 1 0 0 1 100 0 cm /Fm1 Do Q\n" + inline + b"Q\nq\n"
	page2 = b"q 40 0 0 20 10 10 cm /Im1 Do Q\n"
	page4 = b"q 10 0 0 10 5 5 cm /Im1 Do Q\n" + b"q " * 70 + b"10 0 0 10 50 50 cm /Im1 Do " + b"Q " * 70 + b"\n"
	resources = b"<< /XObject << /Im1 5 0 R /Fm1 6 0 R >> >>"
	objects = [
		b"<< /Type /Catalog /Pages 2 0 R >>",
		b"<< /Type /Pages /Kids [3 0 R 8 0 R 10 0 R 13 0 R] /Count 4 >>",
		b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] /Resources " + resources + b" /Contents 4 0 R >>",
		stream(b"<< >>", page1),
		stream(b"<< /Type /XObject /Subtype /Image /Width 2 /Height 2 /ColorSpace /DeviceRGB /BitsPerComponent 8 >>", IMAGE),
		stream(b"<< /Type /XObject /Subtype /Form /BBox [0 0 30 30] /Resources << /XObject << /Im1 5 0 R >> >> >>",
			b"q 30 0 0 30 0 0 cm /Im1 Do Q\n"),
		b"<< >>",
		b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] /Rotate 90 /Resources " + resources + b" /Contents 9 0 R >>",
		stream(b"<< >>", page2),
		b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] /Resources " + resources + b" /Contents [11 0 R 12 0 R] >>",
		stream(b"<< >>", b"q 50 0 0 40 10 20 cm /Im1 Do Q\n"),
		stream(b"<< /Filter /JBIG2Decode >>", b"\x00\x01\x02\x03"),
		b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] /Resources " + resources + b" /Contents 14 0 R >>",
		stream(b"<< >>", page4),
	]
	write(folder / "edit-images.pdf", objects)
	print(f"wrote {folder / 'edit-images.pdf'}")
	return 0


if __name__ == "__main__":
	sys.exit(main())
