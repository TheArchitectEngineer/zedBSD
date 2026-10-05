#!/usr/bin/env python3
"""samples: the AAT's sample files, made on the host and put on the target (WS173 p004).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

The image does not carry them (the test image is its config.mk and the
files copied in, 2026-10-04 user): run-aat.sh makes them under
OUTDIR/samples and puts them in /tmp/aat-samples on the target, readable
by everyone, before the scenarios run.

    sample.png    320x200, a colour gradient with a white square
    sample.jpg    the same, JPEG
    sample.pdf    two pages, "AAT page 1" and "AAT page 2"
    sample.mp4    4 s, 640x360, MPEG-4 Part 2 video and AAC sound (host ffmpeg)

    samples.py OUTDIR -- TARGET-OPTIONS
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
AAT = HERE.parent / "aat"
REMOTE = "/tmp/aat-samples"


def pictures(directory: Path) -> None:
	"""The PNG and the JPEG."""
	picture = Image.new("RGB", (320, 200))
	draw = ImageDraw.Draw(picture)
	for x in range(320):
		draw.line([(x, 0), (x, 199)], fill=(x * 255 // 319, 120, 255 - x * 255 // 319))
	draw.rectangle([130, 70, 190, 130], fill=(255, 255, 255))
	picture.save(directory / "sample.png")
	picture.save(directory / "sample.jpg", quality=90)


def pdf(directory: Path) -> None:
	"""A two-page PDF written by hand (objects, then the cross-reference table at their offsets)."""
	objects = [
		b"<< /Type /Catalog /Pages 2 0 R >>",
		b"<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >>",
	]
	for page in (1, 2):
		text = f"BT /F1 36 Tf 72 700 Td (AAT page {page}) Tj ET".encode()
		page_number = len(objects) + 1
		objects.append(
			f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents {page_number + 1} 0 R "
			f"/Resources << /Font << /F1 7 0 R >> >> >>".encode())
		objects.append(b"<< /Length %d >>\nstream\n" % len(text) + text + b"\nendstream")
	objects.append(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
	out = bytearray(b"%PDF-1.4\n")
	offsets = []
	for index, body in enumerate(objects, start=1):
		offsets.append(len(out))
		out += b"%d 0 obj\n" % index + body + b"\nendobj\n"
	table = len(out)
	out += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objects) + 1)
	for offset in offsets:
		out += b"%010d 00000 n \n" % offset
	out += b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objects) + 1, table)
	(directory / "sample.pdf").write_bytes(bytes(out))


def video(directory: Path) -> None:
	"""Four seconds of a test pattern with a tone (the host's ffmpeg, its own encoders)."""
	subprocess.run([
		"ffmpeg", "-loglevel", "error", "-y",
		"-f", "lavfi", "-i", "testsrc=size=640x360:rate=25:duration=4",
		"-f", "lavfi", "-i", "sine=frequency=440:duration=4",
		"-c:v", "mpeg4", "-q:v", "5", "-c:a", "aac", "-b:a", "96k", "-shortest",
		str(directory / "sample.mp4"),
	], check=True, timeout=120)


def main() -> int:
	"""Makes the samples and puts them on the target."""
	if len(sys.argv) < 3 or "--" not in sys.argv:
		print("usage: samples.py OUTDIR -- TARGET-OPTIONS", file=sys.stderr)
		return 2
	directory = Path(sys.argv[1]) / "samples"
	target = [word for word in sys.argv[sys.argv.index("--") + 1:]]
	directory.mkdir(parents=True, exist_ok=True)
	pictures(directory)
	pdf(directory)
	video(directory)
	aat = [sys.executable, str(AAT), *target]
	subprocess.run([*aat, "run", "--root", f"rm -rf {REMOTE}; mkdir -p {REMOTE}; chmod 1777 {REMOTE}"], check=True, timeout=120)
	for path in sorted(directory.iterdir()):
		subprocess.run([*aat, "put", str(path), f"{REMOTE}/{path.name}"], check=True, timeout=300, stdout=subprocess.DEVNULL)
	subprocess.run([*aat, "run", "--root", f"chmod 644 {REMOTE}/*; ls -l {REMOTE}"], check=True, timeout=120)
	return 0


if __name__ == "__main__":
	sys.exit(main())
