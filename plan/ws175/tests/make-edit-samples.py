#!/usr/bin/env python3
"""make-edit-samples.py: the PDFs of WS175's host tests (ws175-p002a: the images and graphics of the scan).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    make-edit-samples.py FOLDER

writes FOLDER/edit-images.pdf, pages of 200 x 100 points (and edit-text.pdf, text_objects below):

  1. an image XObject drawn through cm; a form XObject (whose own image is not the page's); an inline image; a Q
     without its q at the top level; a q left open at the end
  2. /Rotate 90, an image through cm
  3. two content streams, the second with a filter the reader does not read (/JBIG2Decode): the page is not editable
  4. an image, then q nested 70 deep (past the interpreter's 63) and an image inside: the second is left out
  5. an image named /KeiIm0, as a revision Notes saved before would name it: an update's names take another prefix
  6. text in three fonts for the characters of the codes (p002b): Helvetica in WinAnsiEncoding (F1), Helvetica with a
     /ToUnicode CMap that makes <41> a Z (F2), and the host's DejaVu Sans whole as a CIDFontType2 in Identity-H without
     /ToUnicode (F3, its codes the glyphs)
  7. lines of text (p002b), Helvetica with every width 500: "Hello" and "World" in two text objects on one baseline (one
     line), a TJ with a kerning and a Tj after it (one line), "Before" and "After" with a colour set between (two lines),
     a clipping text object (Tr 7, no line) and an invisible one (Tr 3)

The bytes are written by hand (the cross-reference table computed here), so that the content is exactly the test's.
"""
import sys
from pathlib import Path

DEJAVU = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")

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
		b"<< /Type /Pages /Kids [3 0 R 8 0 R 10 0 R 13 0 R 15 0 R 17 0 R 26 0 R] /Count 7 >>",
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
		b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] /Resources << /XObject << /KeiIm0 5 0 R >> >> /Contents 16 0 R >>",
		stream(b"<< >>", b"q 20 0 0 20 30 30 cm /KeiIm0 Do Q\n"),
		b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] /Resources << /Font << /F1 19 0 R /F2 20 0 R /F3 22 0 R >> >> /Contents 18 0 R >>",
		stream(b"<< >>", b"BT /F1 12 Tf 10 80 Td (AB) Tj ET BT /F2 12 Tf 10 60 Td (AB) Tj ET BT /F3 12 Tf 10 40 Td <0024> Tj ET\n"),
		b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>",
		b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding /ToUnicode 21 0 R >>",
		stream(b"<< >>", b"/CIDInit /ProcSet findresource begin 12 dict begin begincmap 1 begincodespacerange <00> <FF> endcodespacerange 1 beginbfchar <41> <005A> endbfchar endcmap end end\n"),
		b"<< /Type /Font /Subtype /Type0 /BaseFont /DejaVuSans /Encoding /Identity-H /DescendantFonts [23 0 R] >>",
		b"<< /Type /Font /Subtype /CIDFontType2 /BaseFont /DejaVuSans /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /FontDescriptor 24 0 R /CIDToGIDMap /Identity /DW 600 >>",
		b"<< /Type /FontDescriptor /FontName /DejaVuSans /Flags 32 /FontBBox [0 -200 1000 900] /ItalicAngle 0 /Ascent 900 /Descent -200 /CapHeight 700 /StemV 80 /FontFile2 25 0 R >>",
		stream(b"<< >>", DEJAVU.read_bytes()),
		b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 200] /Resources << /Font << /F1 28 0 R >> >> /Contents 27 0 R >>",
		stream(b"<< >>", b"BT /F1 10 Tf 10 180 Td (Hello) Tj ET\nBT /F1 10 Tf 37.8 180 Td (World) Tj ET\n"
			b"BT /F1 10 Tf 10 160 Td [(Ke) -80 (rned)] TJ ( text) Tj ET\n"
			b"BT /F1 10 Tf 10 140 Td (Before) Tj ET 0 0 1 rg BT /F1 10 Tf 45 140 Td (After) Tj ET\n"
			b"BT 7 Tr /F1 10 Tf 10 120 Td (Clip) Tj ET\nBT 3 Tr /F1 10 Tf 10 100 Td (Hidden) Tj ET\n"),
		b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding /FirstChar 32 /LastChar 126 /Widths ["
			+ b" 500" * 95 + b"] >>",
	]
	write(folder / "edit-images.pdf", objects)
	print(f"wrote {folder / 'edit-images.pdf'}")
	write(folder / "edit-text.pdf", text_objects())
	print(f"wrote {folder / 'edit-text.pdf'}")
	return 0


def text_objects() -> list[bytes]:
	"""edit-text.pdf (ws175-p004): one page of 300 x 200 whose lines rewriting moves, deletes and gives new words.

	F1 is the host's DejaVu Sans whole as a simple TrueType font in WinAnsiEncoding (embedded: new words in it), F2
	Helvetica (not embedded: new words need a replacement font), F3 DejaVu Sans as a CIDFontType2 in Identity-H. The
	first text object moves by Td, T* and TD (whose -14 sets the leading the later text objects' T* and ' use, [H1]);
	the third has a ' and a " (which sets Tw 2 and Tc 1); then an invisible one (Tr 3); "Marked" in marked content whose
	inline properties have /ActualText (Old), and "Named" in one whose named properties (/P0) have /ActualText (Older)
	(design.md [M10][N15]).
	"""
	content = (b"BT /F1 10 Tf 12 TL 10 180 Td (Line one) Tj T* (Line two) Tj 0 -14 TD (Line three) Tj ET\n"
		b"BT /F1 10 Tf 10 120 Td T* (After TL) Tj ET\n"
		b"BT /F1 10 Tf 10 90 Td (Quote) Tj (Next) ' 2 1 (Dq) \" ET\n"
		b"BT /F2 10 Tf 10 50 Td (Helvetica) Tj ET\n"
		b"BT /F3 10 Tf 10 30 Td <0024> Tj ET\n"
		b"BT 3 Tr /F1 10 Tf 150 30 Td (Hidden) Tj ET\n"
		b"/Span << /ActualText (Old) >> BDC BT 0 Tr /F1 10 Tf 150 180 Td (Marked) Tj ET EMC\n"
		b"/Span /P0 BDC BT /F1 10 Tf 150 160 Td (Named) Tj ET EMC\n")
	descriptor = (b"<< /Type /FontDescriptor /FontName /DejaVuSans /Flags 32 /FontBBox [0 -200 1000 900] /ItalicAngle 0 /Ascent 900"
		b" /Descent -200 /CapHeight 700 /StemV 80 /FontFile2 9 0 R >>")
	return [
		b"<< /Type /Catalog /Pages 2 0 R >>",
		b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
		b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 200] /Resources << /Font << /F1 5 0 R /F2 7 0 R /F3 10 0 R >>"
			b" /Properties << /P0 << /ActualText (Older) /Lang (en) >> >> >>"
			b" /Contents 4 0 R >>",
		stream(b"<< >>", content),
		b"<< /Type /Font /Subtype /TrueType /BaseFont /DejaVuSans /Encoding /WinAnsiEncoding /FirstChar 32 /LastChar 126 /Widths ["
			+ b" 600" * 95 + b"] /FontDescriptor 6 0 R >>",
		descriptor.replace(b"9 0 R", b"8 0 R"),
		b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>",
		stream(b"<< >>", DEJAVU.read_bytes()),
		descriptor.replace(b"9 0 R", b"8 0 R"),
		b"<< /Type /Font /Subtype /Type0 /BaseFont /DejaVuSans /Encoding /Identity-H /DescendantFonts [11 0 R] >>",
		b"<< /Type /Font /Subtype /CIDFontType2 /BaseFont /DejaVuSans /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity)"
			b" /Supplement 0 >> /FontDescriptor 9 0 R /CIDToGIDMap /Identity /DW 600 >>",
	]


if __name__ == "__main__":
	sys.exit(main())
