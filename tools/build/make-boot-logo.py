#!/usr/bin/env python3
"""Draws the zedBSD boot logo as a binary PPM (ws035-p096).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

The logo is made here from shapes, so no picture from elsewhere is in the
tree: a rounded square in a blue gradient with a white Z, and the word
zedBSD beside it in strokes of a small geometric font of its own.  The
background is one plain colour; the UEFI loader fills the screen with the
colour of the top-left pixel and draws the logo in the middle.  The output
is the same for the same script.

    make-boot-logo.py OUTPUT.ppm
"""
import math
import sys

WIDTH = 600
HEIGHT = 200
BACKGROUND = (12, 18, 34)
WHITE = (255, 255, 255)
LIGHT = (140, 186, 255)

# The mark: a rounded square with a vertical gradient, and its place.
MARK_SIZE = 124
MARK_RADIUS = 28
MARK_X = 30
MARK_Y = (HEIGHT - MARK_SIZE) // 2
MARK_TOP = (70, 132, 246)
MARK_BOTTOM = (36, 196, 226)

# The word: stroke width, the height of capitals and of lower case, the baseline, and the first letter's left.
STROKE = 11.0
CAP = 80.0
XHEIGHT = 55.0
BASELINE = HEIGHT / 2 + CAP / 2
WORD_X = MARK_X + MARK_SIZE + 36


def arc(cx, cy, rx, ry, start, end, steps=40):
	"""A polyline along an ellipse from start to end degrees (0 is right, 90 is up)."""
	points = []
	for index in range(steps + 1):
		angle = math.radians(start + (end - start) * index / steps)
		points.append((cx + rx * math.cos(angle), cy - ry * math.sin(angle)))
	return points


def letter(name, x):
	"""The strokes (polylines) of one letter at x, and its advance."""
	top = BASELINE - CAP
	xtop = BASELINE - XHEIGHT
	half = STROKE / 2
	if name == 'z':
		w = 43.0
		return [[(x + half, xtop + half), (x + w - half, xtop + half), (x + half, BASELINE - half),
		         (x + w - half, BASELINE - half)]], w + 15
	if name == 'e':
		r = XHEIGHT / 2 - half
		cx = x + r + half
		cy = xtop + XHEIGHT / 2
		return [[(cx - r, cy), (cx + r, cy)] + arc(cx, cy, r, r, 0, 318)], 2 * r + STROKE + 15
	if name == 'd':
		r = XHEIGHT / 2 - half
		cx = x + r + half
		cy = xtop + XHEIGHT / 2
		stem = cx + r
		return [arc(cx, cy, r, r, 0, 360, 60), [(stem, top + half), (stem, BASELINE - half)]], 2 * r + STROKE + 18
	if name == 'B':
		w = 45.0
		mid = top + CAP * 0.47
		upper = (mid - top - half) / 2
		lower = (BASELINE - half - mid) / 2
		stroke = [(x + half, BASELINE - half), (x + half, top + half), (x + w - upper - half - 6, top + half)]
		stroke += arc(x + w - upper - half - 6, top + half + upper, upper, upper, 90, -90)[1:]
		stroke += [(x + half, mid), (x + w - lower - half, mid)]
		stroke += arc(x + w - lower - half, mid + lower, lower, lower, 90, -90)[1:]
		stroke += [(x + half, BASELINE - half)]
		return [stroke], w + 17
	if name == 'S':
		w = 47.0
		r = (CAP - STROKE) / 4
		cx = x + w / 2
		upper = arc(cx, top + half + r, w / 2 - half, r, 20, 270, 36)
		lower = arc(cx, BASELINE - half - r, w / 2 - half, r, 90, -160, 36)
		return [upper + lower], w + 17
	if name == 'D':
		w = 52.0
		r = CAP / 2 - half
		stroke = [(x + half, top + half), (x + w - r - half, top + half)]
		stroke += arc(x + w - r - half, top + CAP / 2, r, r, 90, -90)[1:]
		stroke += [(x + half, BASELINE - half), (x + half, top + half)]
		return [stroke], w + 17
	raise ValueError(name)


def cover(image, strokes, color, width):
	"""Paints round-capped strokes of a width, antialiased over one pixel."""
	half = width / 2
	for points in strokes:
		for (ax, ay), (bx, by) in zip(points, points[1:]):
			left = int(min(ax, bx) - half - 2)
			right = int(max(ax, bx) + half + 2)
			top = int(min(ay, by) - half - 2)
			bottom = int(max(ay, by) + half + 2)
			dx = bx - ax
			dy = by - ay
			length = dx * dx + dy * dy
			for y in range(max(top, 0), min(bottom, HEIGHT)):
				for x in range(max(left, 0), min(right, WIDTH)):
					px = x + 0.5 - ax
					py = y + 0.5 - ay
					t = 0.0
					if length > 0:
						t = max(0.0, min(1.0, (px * dx + py * dy) / length))
					distance = math.hypot(px - t * dx, py - t * dy)
					alpha = max(0.0, min(1.0, half + 0.5 - distance))
					if alpha > image[y][x][3]:
						image[y][x][3] = alpha
						image[y][x][4] = color


def rounded_alpha(x, y, left, top, size, radius):
	"""How much of a pixel a rounded square covers."""
	cx = min(max(x + 0.5, left + radius), left + size - radius)
	cy = min(max(y + 0.5, top + radius), top + size - radius)
	distance = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
	return max(0.0, min(1.0, radius + 0.5 - distance))


def mix(a, b, t):
	return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def main():
	if len(sys.argv) != 2:
		sys.exit("usage: make-boot-logo.py OUTPUT.ppm")

	# The background, and a coverage layer for the white strokes (alpha, colour).
	pixels = [[list(BACKGROUND) for _ in range(WIDTH)] for _ in range(HEIGHT)]
	strokes = [[[0, 0, 0, 0.0, WHITE] for _ in range(WIDTH)] for _ in range(HEIGHT)]

	# The mark's rounded square with its gradient, and a soft shadow under it.
	for y in range(MARK_Y - 4, MARK_Y + MARK_SIZE + 14):
		for x in range(MARK_X - 12, MARK_X + MARK_SIZE + 12):
			shadow = rounded_alpha(x, y - 8, MARK_X - 4, MARK_Y - 4, MARK_SIZE + 8, MARK_RADIUS + 4) * 0.35
			if shadow > 0:
				pixels[y][x] = list(mix(pixels[y][x], (0, 0, 0), shadow))
			alpha = rounded_alpha(x, y, MARK_X, MARK_Y, MARK_SIZE, MARK_RADIUS)
			if alpha > 0:
				shade = mix(MARK_TOP, MARK_BOTTOM, (y - MARK_Y) / MARK_SIZE)
				pixels[y][x] = list(mix(pixels[y][x], shade, alpha))

	# The Z in the mark.
	inset = 35.0
	z = [[(MARK_X + inset, MARK_Y + inset), (MARK_X + MARK_SIZE - inset, MARK_Y + inset),
	      (MARK_X + inset, MARK_Y + MARK_SIZE - inset), (MARK_X + MARK_SIZE - inset, MARK_Y + MARK_SIZE - inset)]]
	cover(strokes, z, WHITE, 14.0)

	# The word: zed in white, BSD in light blue.
	x = float(WORD_X)
	for name in "zedBSD":
		lines, advance = letter(name, x)
		color = WHITE
		if name.isupper():
			color = LIGHT
		cover(strokes, lines, color, STROKE)
		x += advance

	# The strokes over the background.
	with open(sys.argv[1], "wb") as output:
		output.write(b"P6\n%d %d\n255\n" % (WIDTH, HEIGHT))
		row = bytearray()
		for y in range(HEIGHT):
			for x in range(WIDTH):
				base = pixels[y][x]
				alpha = strokes[y][x][3]
				color = strokes[y][x][4]
				row += bytes(mix(base, color, alpha))
		output.write(bytes(row))


if __name__ == "__main__":
	main()
