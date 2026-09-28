#!/usr/bin/env python3
# ws035-p109: puts a guest screenshot beside the Kei boot splash (both 800 pixels wide), and under them the
# screenshot's mark (a square at the bottom left, 4x) beside the splash's mark, to compare their tone.
#
#   plan/ws035/tests/p109/compare.py SHOT.png OUT.png [MARK_X MARK_Y MARK_SIZE]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import os
import sys

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../.."))
SPLASH = os.path.join(ROOT, "userland/desktop/artwork/kei-boot-splash.png")
WIDTH = 800


def main() -> int:
	shot = Image.open(sys.argv[1]).convert("RGB")
	out = sys.argv[2]
	mark_x, mark_y, mark_size = (24, 728, 48)
	if len(sys.argv) > 5:
		mark_x, mark_y, mark_size = (int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]))
	splash = Image.open(SPLASH).convert("RGB")
	left = shot.resize((WIDTH, shot.height * WIDTH // shot.width), Image.BILINEAR)
	right = splash.resize((WIDTH, splash.height * WIDTH // splash.width), Image.BILINEAR)
	zoom = 4 * mark_size
	ours = shot.crop((mark_x, mark_y, mark_x + mark_size, mark_y + mark_size)).resize((zoom, zoom), Image.LANCZOS)
	theirs = splash.crop((736, 246, 964, 474)).resize((zoom, zoom), Image.LANCZOS)
	top = max(left.height, right.height)
	both = Image.new("RGB", (WIDTH * 2 + 16, top + zoom + 16), (255, 255, 255))
	both.paste(left, (0, 0))
	both.paste(right, (WIDTH + 16, 0))
	both.paste(ours, (WIDTH - zoom, top + 16))
	both.paste(theirs, (WIDTH + 16, top + 16))
	both.save(out)
	print(f"compare: {out} (left: the screenshot, right: the splash)")
	return 0


if __name__ == "__main__":
	sys.exit(main())
