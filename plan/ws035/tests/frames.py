#!/usr/bin/env python3
"""Photographs the Venus display again and again for a while (ws035-p101).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

Each picture is taken as zdesktop-check.py takes one (QEMU's VNC server on
the Venus console) and kept as OUTDIR/frame-NNN.png.  One line a picture
gives its time from the start (ms), its size, and a rough kind:
    black    almost every pixel is black (at most 0.5% are not)
    text     a black screen with light grey pixels only (the text console)
    picture  anything else (the logo, the greeter, the desktop)
The run ends after SECONDS, or earlier once the file --stop names exists.

    frames.py OUTDIR --runtime DIR --seconds 20 [--stop FILE]
"""
import argparse
import os
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "plan/ws014/tests"))
sys.path.insert(0, str(HERE))

from venus_rfb import capture  # noqa: E402
from ppm2png import read_ppm, write_png  # noqa: E402


def kind(width, height, pixels):
	"""Classifies a picture as black, text or picture (sampling every 7th pixel)."""
	count = 0
	dark = 0
	grey = 0
	for at in range(0, width * height * 3, 21):
		red = pixels[at]
		green = pixels[at + 1]
		blue = pixels[at + 2]
		count += 1
		if red < 16 and green < 16 and blue < 16:
			dark += 1
		elif abs(red - green) < 12 and abs(green - blue) < 12:
			grey += 1
	if count - dark <= count // 200:
		return "black"
	if dark + grey == count:
		return "text"
	return "picture"


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("outdir")
	parser.add_argument("--runtime", default=os.environ.get(
		"GUEST_RUNTIME", str(ROOT / "build/ws071-run")))
	parser.add_argument("--seconds", type=float, default=20.0)
	parser.add_argument("--stop", default="")
	arguments = parser.parse_args()
	out = Path(arguments.outdir)
	out.mkdir(parents=True, exist_ok=True)
	socket_path = str(Path(arguments.runtime).resolve() / "vnc.sock")
	start = time.monotonic()
	index = 0
	while time.monotonic() - start < arguments.seconds:
		if arguments.stop and os.path.exists(arguments.stop):
			break
		ppm = out / ("frame-%03d.ppm" % index)
		taken = time.monotonic() - start
		try:
			report = capture(socket_path, str(ppm), 5)
		except (OSError, ValueError, RuntimeError) as error:
			print("%6d ms  none (%s)" % (taken * 1000, error), flush=True)
			time.sleep(0.2)
			continue
		if report is None:
			continue
		width, height, pixels = read_ppm(str(ppm))
		write_png(str(out / ("frame-%03d.png" % index)), width, height, pixels)
		os.unlink(ppm)
		print("%6d ms  frame-%03d %dx%d %s" % (taken * 1000, index, width, height,
						   kind(width, height, pixels)), flush=True)
		index += 1
	return 0


if __name__ == "__main__":
	sys.exit(main())
