/*
 * Host test of the generic /dev/graphics front end (WS275 p003).
 *
 * Built with src/drivers/generic/graphics-device.c and fakes of the few
 * kernel services it calls. Checks backend selection by priority, the
 * splitting of wide rows into chunks with the pattern phase kept, and the
 * pixel packing helpers of include/kern/graphics-backend.h.
 *
 * Usage: host-graphics-device [none]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kern/graphics-backend.h"
#include "kern/graphics-device.h"
#include "kern/cdev.h"
#include "kern/lock.h"
#include "kern/uaccess.h"
#include <uapi/errno.h>

#define MODE_WIDTH 4200U
#define MODE_HEIGHT 4U

static const struct cdev_ops *registered_ops;
static int registered_count;
static uint32_t screen[MODE_HEIGHT][MODE_WIDTH];
static uint32_t expected[MODE_HEIGHT][MODE_WIDTH];
static unsigned largest_chunk;
static unsigned chunk_calls;
static int failures;

/* Fakes of the kernel services the front end calls. */
int
cdev_register(const char *name, dev_t rdev, const struct cdev_ops *ops, void *data)
{
	(void)name;
	(void)rdev;
	(void)data;
	registered_ops = ops;
	registered_count++;
	return 0;
}

int
mutex_init(struct mutex *mutex, enum lock_rank rank, const char *name)
{
	(void)mutex;
	(void)rank;
	(void)name;
	return 0;
}

void
mutex_lock(struct mutex *mutex)
{
	(void)mutex;
}

void
mutex_unlock(struct mutex *mutex)
{
	(void)mutex;
}

int
copyin(uintptr_t source, void *destination, size_t size)
{
	memcpy(destination, (const void *)source, size);
	return 0;
}

int
copyout(const void *source, uintptr_t destination, size_t size)
{
	memcpy((void *)destination, source, size);
	return 0;
}

void
kern_text_suspend(void)
{
}

void
kern_text_resume(void)
{
}

static void
check(int condition, const char *what)
{
	if (!condition) {
		printf("FAIL %s\n", what);
		failures++;
	}
}

/* The fake backend: one 32-bit color per pixel in screen[]. */
static int fake_ready(void) { return 1; }

static size_t
fake_get_modes(struct graphics_mode_info *modes, size_t capacity)
{
	if (capacity > 0) {
		modes[0].width = MODE_WIDTH;
		modes[0].height = MODE_HEIGHT;
		modes[0].bits_per_pixel = 32;
		modes[0].stride = MODE_WIDTH * 4U;
	}
	return 1;
}

static int
fake_enter(struct graphics_mode *mode)
{
	mode->width = MODE_WIDTH;
	mode->height = MODE_HEIGHT;
	mode->bits_per_pixel = 32;
	mode->stride = MODE_WIDTH * 4U;
	return 1;
}

static void fake_leave(void) { }
static int fake_fill(const struct graphics_rect *r, uint32_t c) { (void)r; (void)c; return 1; }
static int fake_line(unsigned a, unsigned b, unsigned c, unsigned d, uint32_t e) { (void)a; (void)b; (void)c; (void)d; (void)e; return 1; }
static int fake_pattern_fill(const struct graphics_rect *r, uint32_t c, uint64_t p) { (void)r; (void)c; (void)p; return 1; }
static int fake_flush(const struct graphics_rect *r, size_t n) { (void)r; (void)n; return 1; }
static int fake_get_glyph(uint32_t c, uint8_t b[32], unsigned *w, unsigned *h) { (void)c; memset(b, 0, 32); *w = 8; *h = 16; return 1; }

static int
fake_blit_row(unsigned x, unsigned y, const struct drv_graphics_row *row, uint64_t pattern, int patterned)
{
	unsigned i;
	uint32_t color;
	const uint8_t *p;

	chunk_calls++;
	if (row->width > largest_chunk)
		largest_chunk = row->width;
	if (row->format == KERN_GRAPHICS_FORMAT_RGB24)
		check(row->width * 3U <= 4096U, "RGB24 chunk fits the row buffer");
	else
		check(row->width <= 4096U, "index chunk fits the row buffer");
	for (i = 0; i < row->width; i++) {
		if (patterned && !drv_graphics_pattern_bit(pattern, row->pattern_x + i, row->pattern_y))
			continue;
		if (row->format == KERN_GRAPHICS_FORMAT_RGB24) {
			p = row->pixels + (size_t)i * 3U;
			color = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
		} else {
			color = row->pixels[i] < row->palette_size ? row->palette[row->pixels[i]] : 0xdeadU;
		}
		screen[y][x + i] = color;
	}
	return 1;
}

static const struct drv_graphics_backend backend_low = {
	"low", fake_ready, fake_get_modes, fake_enter, fake_leave, fake_fill,
	fake_line, fake_pattern_fill, fake_blit_row, fake_flush, fake_get_glyph,
};

static const struct drv_graphics_backend backend_high = {
	"high", fake_ready, fake_get_modes, fake_enter, fake_leave, fake_fill,
	fake_line, fake_pattern_fill, fake_blit_row, fake_flush, fake_get_glyph,
};

static const struct drv_graphics_backend backend_equal = {
	"equal", fake_ready, fake_get_modes, fake_enter, fake_leave, fake_fill,
	fake_line, fake_pattern_fill, fake_blit_row, fake_flush, fake_get_glyph,
};

static const struct drv_graphics_backend backend_incomplete = {
	"incomplete", fake_ready, fake_get_modes, fake_enter, fake_leave, fake_fill,
	fake_line, fake_pattern_fill, NULL, fake_flush, fake_get_glyph,
};

/* Blits one image and compares the screen with a direct reference. */
static void
blit_case(unsigned format, unsigned width, int patterned)
{
	static uint8_t source[MODE_HEIGHT][4200U * 3U + 16U];
	static uint32_t palette[256];
	struct graphics_blit request;
	unsigned x0 = 3, y0 = 0, height = 3, stride, row, col, i;
	uint64_t pattern = 0x8142241818244281ULL ^ 0x00ff00ff00ff00ffULL;
	uint32_t color;
	unsigned long ioc = patterned ? KERN_GRAPHICS_BLIT_PATTERN : KERN_GRAPHICS_BLIT;
	char what[96];
	const uint8_t *p;
	unsigned index;

	if (format == KERN_GRAPHICS_FORMAT_RGB24)
		stride = width * 3U + 5U;
	else if (format == KERN_GRAPHICS_FORMAT_INDEX8)
		stride = width + 5U;
	else
		stride = (width + 7U) / 8U + 1U;
	for (i = 0; i < 256; i++)
		palette[i] = 0x010101U * i ^ 0x400000U;
	for (row = 0; row < height; row++)
		for (i = 0; i < stride; i++)
			source[row][i] = (uint8_t)(i * 7U + row * 13U + (i >> 8) * 3U);
	memset(screen, 0, sizeof(screen));
	memset(expected, 0, sizeof(expected));
	for (row = 0; row < height; row++) {
		for (col = 0; col < width; col++) {
			if (patterned && !drv_graphics_pattern_bit(pattern, col, row))
				continue;
			if (format == KERN_GRAPHICS_FORMAT_RGB24) {
				p = &source[row][col * 3U];
				color = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
			} else if (format == KERN_GRAPHICS_FORMAT_INDEX8) {
				color = palette[source[row][col]];
			} else {
				index = (source[row][col / 8U] >> (7U - col % 8U)) & 1U;
				color = index ? 0x00ff8800U : 0x00112233U;
			}
			expected[y0 + row][x0 + col] = color;
		}
	}

	memset(&request, 0, sizeof(request));
	request.x = x0;
	request.y = y0;
	request.width = width;
	request.height = height;
	request.format = format;
	request.stride = stride;
	request.pixels = (uintptr_t)&source[0][0];
	/* Rows of source[] are sizeof(source[0]) apart; use that as stride. */
	request.stride = (uint32_t)sizeof(source[0]);
	if (format == KERN_GRAPHICS_FORMAT_INDEX8) {
		request.palette = (uintptr_t)palette;
		request.palette_count = 256;
	}
	request.foreground = 0x00ff8800U;
	request.background = 0x00112233U;
	request.pattern = pattern;
	largest_chunk = 0;
	chunk_calls = 0;
	snprintf(what, sizeof(what), "blit format %u width %u pattern %d returns 0", format, width, patterned);
	check(registered_ops->ioctl((struct file *)1, ioc, (uintptr_t)&request) == 0, what);
	snprintf(what, sizeof(what), "blit format %u width %u pattern %d pixels", format, width, patterned);
	check(memcmp(screen, expected, sizeof(screen)) == 0, what);
	snprintf(what, sizeof(what), "blit format %u width %u was split", format, width);
	if (format == KERN_GRAPHICS_FORMAT_RGB24)
		check(largest_chunk == (width < 1360U ? width : 1360U) && chunk_calls == height * ((width + 1359U) / 1360U), what);
	else
		check(largest_chunk == (width < 4096U ? width : 4096U) && chunk_calls == height * ((width + 4095U) / 4096U), what);
	printf("ok-ish format %u width %u pattern %d: %u chunks, largest %u\n", format, width, patterned, chunk_calls, largest_chunk);
}

/* Checks the packing helpers against an emulated little-endian store. */
static void
packing_cases(void)
{
	uint32_t word;
	uint8_t memory[4];

	check(drv_graphics_rgb332(0xffffffU) == 0xffU, "rgb332 white");
	check(drv_graphics_rgb332(0xff0000U) == 0xe0U, "rgb332 red");
	check(drv_graphics_rgb332(0x00ff00U) == 0x1cU, "rgb332 green");
	check(drv_graphics_rgb332(0x0000ffU) == 0x03U, "rgb332 blue");
	check(drv_graphics_rgb332(0x203040U) == 0x25U, "rgb332 0x203040");

	/* An IE mapping stores a native word as little-endian bytes. */
	word = drv_graphics_pixel_word_le(KERN_BOOT_FRAMEBUFFER_BGRX8888, 0x112233U);
	memory[0] = (uint8_t)word; memory[1] = (uint8_t)(word >> 8);
	memory[2] = (uint8_t)(word >> 16); memory[3] = (uint8_t)(word >> 24);
	check(memory[0] == 0x33 && memory[1] == 0x22 && memory[2] == 0x11 && memory[3] == 0, "BGRX8888 bytes B,G,R,X");

	word = drv_graphics_pixel_word_le(KERN_BOOT_FRAMEBUFFER_XRGB8888, 0x112233U);
	memory[0] = (uint8_t)word; memory[1] = (uint8_t)(word >> 8);
	memory[2] = (uint8_t)(word >> 16); memory[3] = (uint8_t)(word >> 24);
	check(memory[0] == 0 && memory[1] == 0x11 && memory[2] == 0x22 && memory[3] == 0x33, "XRGB8888 bytes X,R,G,B");
}

int
main(int argc, char **argv)
{
	static const unsigned widths[] = {1025U, 1366U, 4097U};
	static const unsigned formats[] = {KERN_GRAPHICS_FORMAT_RGB24, KERN_GRAPHICS_FORMAT_INDEX8, KERN_GRAPHICS_FORMAT_MONO1};
	struct graphics_mode mode;
	unsigned w, f;

	if (argc > 1 && strcmp(argv[1], "none") == 0) {
		check(drv_graphics_device_register() == 0, "register without backend");
		check(registered_count == 0, "no /dev/graphics without backend");
		check(drv_graphics_backend_register(&backend_low, 0) == EBUSY, "offer after the choice");
		printf(failures ? "FAIL host-graphics-device none\n" : "PASS host-graphics-device none\n");
		return failures != 0;
	}

	check(drv_graphics_backend_register(NULL, 5) == EINVAL, "null backend");
	check(drv_graphics_backend_register(&backend_incomplete, 500) == EINVAL, "incomplete backend");
	check(drv_graphics_backend_register(&backend_low, DRV_GRAPHICS_PRIORITY_FIRMWARE) == 0, "low backend");
	check(drv_graphics_backend_selected() == NULL, "no choice before register");
	check(drv_graphics_backend_register(&backend_high, DRV_GRAPHICS_PRIORITY_CARD) == 0, "high backend");
	check(drv_graphics_backend_register(&backend_equal, DRV_GRAPHICS_PRIORITY_CARD) == 0, "equal backend");
	check(drv_graphics_device_register() == 0 && registered_count == 1, "register /dev/graphics");
	check(drv_graphics_backend_selected() == &backend_high, "highest priority, earliest on a tie");
	check(drv_graphics_backend_register(&backend_low, 1000) == EBUSY, "offer after the choice");

	check(registered_ops->open((struct file *)1) == 0, "open");
	check(registered_ops->open((struct file *)2) == EBUSY, "second open");
	memset(&mode, 0, sizeof(mode));
	check(registered_ops->ioctl((struct file *)1, KERN_GRAPHICS_ENTER, (uintptr_t)&mode) == 0 && mode.width == MODE_WIDTH, "enter");

	for (f = 0; f < 3; f++)
		for (w = 0; w < 3; w++) {
			blit_case(formats[f], widths[w], 0);
			blit_case(formats[f], widths[w], 1);
		}
	packing_cases();
	check(registered_ops->close((struct file *)1) == 0, "close");

	printf(failures ? "FAIL host-graphics-device (%d)\n" : "PASS host-graphics-device\n", failures);
	return failures != 0;
}
