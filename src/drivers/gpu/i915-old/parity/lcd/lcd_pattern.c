/*
 * WS031 Linux-parity — the known test image for LCD-B (see lcd_pattern.h).
 * zedBSD project code; no dependencies, so the host builds the same file.
 */
#include "lcd_pattern.h"

#define C_BG      0x00102040u
#define C_WHITE   0x00ffffffu
#define C_RED     0x00ff0000u
#define C_GREEN   0x0000ff00u
#define C_BLUE    0x000000ffu
#define C_YELLOW  0x00ffff00u

#ifndef PARITY_LCD_PATTERN_SOLID
static int in_rect(uint32_t x, uint32_t y, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h)
{
	return x >= x0 && x < x0 + w && y >= y0 && y < y0 + h;
}

/* seven segments a..g = bits 0..6, for the digits 0..9 */
static const uint8_t seven_seg[10] = { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };

/* one digit in a box of w x h at (x0, y0); `t` = segment thickness */
static int in_digit(uint32_t x, uint32_t y, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, uint32_t t, unsigned digit)
{
	uint8_t s = seven_seg[digit % 10u];
	uint32_t half = h / 2u;

	if ((s & 0x01u) && in_rect(x, y, x0, y0, w, t)) return 1;                           /* a: top */
	if ((s & 0x02u) && in_rect(x, y, x0 + w - t, y0, t, half)) return 1;                /* b: upper right */
	if ((s & 0x04u) && in_rect(x, y, x0 + w - t, y0 + half, t, h - half)) return 1;     /* c: lower right */
	if ((s & 0x08u) && in_rect(x, y, x0, y0 + h - t, w, t)) return 1;                   /* d: bottom */
	if ((s & 0x10u) && in_rect(x, y, x0, y0 + half, t, h - half)) return 1;             /* e: lower left */
	if ((s & 0x20u) && in_rect(x, y, x0, y0, t, half)) return 1;                        /* f: upper left */
	if ((s & 0x40u) && in_rect(x, y, x0, y0 + half - t / 2u, w, t)) return 1;           /* g: middle */
	return 0;
}

#endif

uint32_t parity_lcd_pattern_pixel(uint32_t x, uint32_t y, uint32_t width, uint32_t height, unsigned test_id)
{
#ifdef PARITY_LCD_PATTERN_SOLID
	/* E-126 diagnostic: one flat colour instead of the picture, to tell a scanout that reads the
	   wrong memory from one that reads ours but lays it out wrongly. */
	(void)width; (void)height; (void)test_id;
	if (x < width && y < height)
		return (uint32_t)(PARITY_LCD_PATTERN_SOLID);
	return 0u;
#else
	uint32_t frame = 16u, corner_w = width / 8u, corner_h = height / 6u;
	uint32_t band_h = height / 12u, fx, fy, fw, fh, ft, dw, dh, dt, dx, dy, i;

	if (x >= width || y >= height)
		return 0u;
	/* the frame on the very edge of the active area */
	if (x < frame || y < frame || x >= width - frame || y >= height - frame)
		return C_WHITE;
	/* corner blocks */
	if (in_rect(x, y, frame, frame, corner_w, corner_h)) return C_RED;
	if (in_rect(x, y, width - frame - corner_w, frame, corner_w, corner_h)) return C_GREEN;
	if (in_rect(x, y, frame, height - frame - corner_h, corner_w, corner_h)) return C_BLUE;
	if (in_rect(x, y, width - frame - corner_w, height - frame - corner_h, corner_w, corner_h)) return C_YELLOW;
	/* grey ramp along the top band, between the corner blocks */
	if (y >= frame && y < frame + band_h && x >= frame + corner_w && x < width - frame - corner_w) {
		uint32_t span = width - 2u * (frame + corner_w);
		uint32_t g = ((x - frame - corner_w) * 255u) / (span - 1u);

		return (g << 16) | (g << 8) | g;
	}
	/* eight colour bars along the bottom band, between the corner blocks */
	if (y >= height - frame - band_h && y < height - frame && x >= frame + corner_w && x < width - frame - corner_w) {
		static const uint32_t bars[8] = { 0x00ffffffu, 0x00ffff00u, 0x0000ffffu, 0x0000ff00u,
						  0x00ff00ffu, 0x00ff0000u, 0x000000ffu, 0x00000000u };
		uint32_t span = width - 2u * (frame + corner_w);

		return bars[((x - frame - corner_w) * 8u) / span];
	}
	/* the letter F, left of the centre */
	fh = height / 2u; fw = fh / 2u; ft = fh / 8u;
	fx = width / 4u - fw / 2u; fy = height / 4u;
	if (in_rect(x, y, fx, fy, ft, fh) ||                          /* stem */
	    in_rect(x, y, fx, fy, fw, ft) ||                          /* top bar */
	    in_rect(x, y, fx, fy + fh / 2u - ft, (fw * 3u) / 4u, ft)) /* middle bar */
		return C_WHITE;
	/* the test id: three seven-segment digits, right of the centre */
	dh = height / 3u; dw = dh / 2u; dt = dh / 10u;
	dx = width / 2u + width / 16u; dy = height / 3u;
	for (i = 0u; i < 3u; i++) {
		unsigned digit = i == 0u ? (test_id / 100u) % 10u : i == 1u ? (test_id / 10u) % 10u : test_id % 10u;

		if (in_digit(x, y, dx + i * (dw + dw / 2u), dy, dw, dh, dt, digit))
			return C_YELLOW;
	}
	return C_BG;
#endif
}

uint64_t parity_lcd_pattern_fill(uint32_t *pixels, uint32_t pitch, uint32_t width, uint32_t height, unsigned test_id)
{
	uint64_t hash = 0xcbf29ce484222325ull;
	uint32_t x, y;
	unsigned b;

	for (y = 0u; y < height; y++) {
		uint32_t *row = (uint32_t *)((uint8_t *)pixels + (uint64_t)y * pitch);

		for (x = 0u; x < width; x++) {
			uint32_t v = parity_lcd_pattern_pixel(x, y, width, height, test_id);

			row[x] = v;
			for (b = 0u; b < 4u; b++) {
				hash ^= (v >> (8u * b)) & 0xffu;
				hash *= 0x100000001b3ull;
			}
		}
	}
	return hash;
}

uint32_t parity_lcd_pattern_verify(const uint32_t *pixels, uint32_t pitch, uint32_t width, uint32_t height,
	unsigned test_id, uint32_t *first_x, uint32_t *first_y)
{
	uint32_t x, y, bad = 0u;

	for (y = 0u; y < height; y++) {
		const uint32_t *row = (const uint32_t *)((const uint8_t *)pixels + (uint64_t)y * pitch);

		for (x = 0u; x < width; x++) {
			if (row[x] != parity_lcd_pattern_pixel(x, y, width, height, test_id)) {
				if (bad == 0u) {
					if (first_x != 0) *first_x = x;
					if (first_y != 0) *first_y = y;
				}
				bad++;
			}
		}
	}
	return bad;
}
