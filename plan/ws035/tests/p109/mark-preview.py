#!/usr/bin/env python3
# ws035-p109: renders the Kei mark's layers on the host (plan/ws035/tests/p107/mark-host.c), tints them with the
# colours of userland/desktop/files/ui-home.c (fm_mark_draw; the compositor's glass.c uses the same values), puts
# the result over a piece of the splash's sky, and writes it beside the splash's own mark for comparison.
#
#   plan/ws035/tests/p109/mark-preview.py OUT.png [PIXELS]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import os
import subprocess
import sys

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../.."))
SPLASH = os.path.join(ROOT, "userland/desktop/artwork/kei-boot-splash.png")

# (colour, alpha) per layer, in the order of enum keiland_mark_layer; keep in step with fm_mark_draw.
COLOURS = [
	(0xa9c3f6, 175), (0x7fa2f0, 90), (0xa3d8fa, 170), (0x3a86f5, 170),
	(0x2f7cf3, 200), (0xffffff, 170), (0xffffff, 60),
]


def main() -> int:
	out = sys.argv[1]
	pixels = int(sys.argv[2]) if len(sys.argv) > 2 else 240
	work = os.path.dirname(os.path.abspath(out))
	tool = os.path.join(work, "mark-host")
	subprocess.run(["cc", "-O2", "-I" + ROOT, "-o", tool, os.path.join(ROOT, "plan/ws035/tests/p107/mark-host.c"),
		os.path.join(ROOT, "userland/desktop/artwork/mark.c"), "-lm"], check=True)
	pgm = os.path.join(work, "mark-layers.pgm")
	subprocess.run([tool, pgm, str(pixels)], check=True)
	layers = Image.open(pgm)
	count = layers.width // pixels
	if count != len(COLOURS):
		print(f"mark-preview: {count} layers, {len(COLOURS)} colours")
		return 1
	splash = Image.open(SPLASH).convert("RGB")
	# The splash's mark (about 745..955 x 253..467 of 1672x941) and a piece of sky left of it as the background.
	theirs = splash.crop((730, 245, 970, 485)).resize((pixels, pixels), Image.BILINEAR)
	canvas = splash.crop((470, 245, 710, 485)).resize((pixels, pixels), Image.BILINEAR)
	for index, (colour, alpha) in enumerate(COLOURS):
		cover = layers.crop((index * pixels, 0, (index + 1) * pixels, pixels))
		mask = cover.point(lambda value, a=alpha: value * a // 255)
		tint = Image.new("RGB", (pixels, pixels), ((colour >> 16) & 255, (colour >> 8) & 255, colour & 255))
		canvas = Image.composite(tint, canvas, mask)
	both = Image.new("RGB", (pixels * 2, pixels))
	both.paste(canvas, (0, 0))
	both.paste(theirs, (pixels, 0))
	both.save(out)
	print(f"mark-preview: {out} (left: drawn, right: splash)")
	return 0


if __name__ == "__main__":
	sys.exit(main())
