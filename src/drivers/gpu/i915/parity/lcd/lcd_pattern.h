/*
 * WS031 Linux-parity — the known test image for the first panel light-up (LCD-B).
 * zedBSD project code.  A pure function of (x, y, width, height, test id): the same
 * function fills the scanout buffer, checks a read-back, and draws the reference
 * picture on the host for comparison with a photograph of the panel.
 *
 * The picture is deliberately asymmetric, so a mirrored, rotated, shifted or stale
 * image is recognisable by eye:
 *   - a 16-pixel white frame on the very edge of the active area (cropping / offset)
 *   - corner blocks: top-left RED, top-right GREEN, bottom-left BLUE, bottom-right YELLOW
 *   - a large white letter "F" left of the centre (unchanged under no flip or rotation)
 *   - the test id as three seven-segment digits right of the centre (not a stale image)
 *   - eight colour bars along the bottom band, a grey ramp along the top band
 *   - a dark blue background (so "panel lit" differs from "panel off")
 */
#ifndef PARITY_LCD_PATTERN_H
#define PARITY_LCD_PATTERN_H

#include <stdint.h>

/* XRGB8888: 0x00RRGGBB */
uint32_t parity_lcd_pattern_pixel(uint32_t x, uint32_t y, uint32_t width, uint32_t height, unsigned test_id);
/* Fills `pixels` (pitch in bytes) and returns the FNV-1a 64 hash of the visible pixels, row by row. */
uint64_t parity_lcd_pattern_fill(uint32_t *pixels, uint32_t pitch, uint32_t width, uint32_t height, unsigned test_id);
/* Re-computes every pixel; returns the number of mismatches and the first one. */
uint32_t parity_lcd_pattern_verify(const uint32_t *pixels, uint32_t pitch, uint32_t width, uint32_t height,
	unsigned test_id, uint32_t *first_x, uint32_t *first_y);

#endif /* PARITY_LCD_PATTERN_H */
