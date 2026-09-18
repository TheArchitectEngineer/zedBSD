/*
 * WS031 host tool/test: renders the LCD-B test image (the same lcd_pattern.c the kernel builds) to a PPM
 * and checks its structural properties.   cc -o t lcd-pattern-host.c <lcd dir>/lcd_pattern.c -I<lcd dir>
 *   ./t <id> <out.ppm>
 */
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "lcd_pattern.h"

int main(int argc, char **argv)
{
	const uint32_t w = 1920, h = 1080, pitch = 7680;
	unsigned id = argc > 1 ? (unsigned)atoi(argv[1]) : 110u;
	uint32_t *px = calloc((size_t)pitch * h, 1), x, y, fx = 0, fy = 0, bad;
	uint64_t hash, hash2;
	unsigned fails = 0;
	FILE *f;

	if (!px) return 2;
	hash = parity_lcd_pattern_fill(px, pitch, w, h, id);
	bad = parity_lcd_pattern_verify(px, pitch, w, h, id, &fx, &fy);
	printf("id=%u fnv1a64=%016" PRIx64 " verify_bad=%u\n", id, hash, bad);
	if (bad) fails++;
	/* corners and frame */
	if (px[0] != 0x00ffffffu || px[(h - 1) * (pitch / 4) + (w - 1)] != 0x00ffffffu) { puts("FAIL frame"); fails++; }
	if (px[20 * (pitch / 4) + 20] != 0x00ff0000u) { puts("FAIL top-left red"); fails++; }
	if (px[20 * (pitch / 4) + (w - 21)] != 0x0000ff00u) { puts("FAIL top-right green"); fails++; }
	if (px[(h - 21) * (pitch / 4) + 20] != 0x000000ffu) { puts("FAIL bottom-left blue"); fails++; }
	if (px[(h - 21) * (pitch / 4) + (w - 21)] != 0x00ffff00u) { puts("FAIL bottom-right yellow"); fails++; }
	/* not symmetric under a horizontal or vertical mirror, nor a 180 degree turn */
	{
		unsigned mh = 0, mv = 0, mr = 0;
		for (y = 0; y < h; y += 7) for (x = 0; x < w; x += 7) {
			uint32_t v = px[y * (pitch / 4) + x];
			if (v != px[y * (pitch / 4) + (w - 1 - x)]) mh++;
			if (v != px[(h - 1 - y) * (pitch / 4) + x]) mv++;
			if (v != px[(h - 1 - y) * (pitch / 4) + (w - 1 - x)]) mr++;
		}
		printf("mirror differences (sampled): horizontal=%u vertical=%u rotate180=%u\n", mh, mv, mr);
		if (mh < 1000 || mv < 1000 || mr < 1000) { puts("FAIL not asymmetric enough"); fails++; }
	}
	/* a different id is a different image; a one-pixel change is detected */
	hash2 = parity_lcd_pattern_fill(px, pitch, w, h, id + 1);
	if (hash2 == hash) { puts("FAIL id does not change the image"); fails++; }
	(void)parity_lcd_pattern_fill(px, pitch, w, h, id);
	px[500 * (pitch / 4) + 700] ^= 1u;
	bad = parity_lcd_pattern_verify(px, pitch, w, h, id, &fx, &fy);
	if (bad != 1 || fx != 700 || fy != 500) { puts("FAIL single-pixel change not located"); fails++; }
	px[500 * (pitch / 4) + 700] ^= 1u;
	if (argc > 2 && (f = fopen(argv[2], "wb")) != NULL) {
		fprintf(f, "P6\n%u %u\n255\n", w, h);
		for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
			uint32_t v = px[y * (pitch / 4) + x];
			fputc((v >> 16) & 255, f); fputc((v >> 8) & 255, f); fputc(v & 255, f);
		}
		fclose(f);
	}
	free(px);
	printf("lcd_pattern_host: %u failures\n", fails);
	return fails != 0;
}
