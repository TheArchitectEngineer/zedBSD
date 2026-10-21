/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The firmware's framebuffer on a Raspberry Pi: asked for through the
 * property mailbox, and painted with 80x25 text cells of the 8x16 font in
 * the middle 640x400 pixels of the screen.
 */

#include <hal/hal.h>
#include "../bsp.h"
#include "../defs.h"
#include "font.h"
#include "framebuffer.h"
#include "led.h"
#include "mailbox.h"

/* The property tags the HAL sends: get the display's size, then set up and allocate. */
#define TAG_GET_PHYSICAL 0x00040003U
#define TAG_PHYSICAL 0x00048003U
#define TAG_VIRTUAL 0x00048004U
#define TAG_DEPTH 0x00048005U
#define TAG_ORDER 0x00048006U
#define TAG_ALLOCATE 0x00040001U
#define TAG_PITCH 0x00040008U

/*
 * Where the firmware's answers lie in the set-up request: the width and
 * the height of the first tag, the pixel order, the base and the size of
 * the allocation, and the pitch.
 */
#define ANSWER_WIDTH 5U
#define ANSWER_HEIGHT 6U
#define ANSWER_ORDER 19U
#define ANSWER_BASE 23U
#define ANSWER_SIZE 24U
#define ANSWER_PITCH 28U

/* The words of the set-up request, and of the size request. */
#define SETUP_WORDS 30U
#define SIZE_WORDS 8U

/* The smallest framebuffer the console fits in, and the largest size taken from the firmware. */
#define MIN_WIDTH 640U
#define MIN_HEIGHT 400U
#define MAX_SIZE 4096U

/* The size asked for when the firmware reports none (QEMU). */
#define DEFAULT_WIDTH 640U
#define DEFAULT_HEIGHT 480U

/* The console's grid, and the rows of the cursor's underline in a cell. */
#define CELL_COLUMNS 80U
#define CELL_ROWS 25U
#define CURSOR_FIRST_ROW 14U

/*
 * The message the property requests are built in, 16-byte aligned as the
 * mailbox requires.  Used only on the boot CPU while the framebuffer is
 * set up.
 */
static uint32_t request[40] __attribute__((aligned(16)));

/*
 * The framebuffer through the direct map, NULL until the firmware has
 * allocated one; the rest describes it.  Filled once by
 * rpi_framebuffer_init() on the boot CPU and not changed afterwards.
 */
static volatile uint8_t *pixels;
static uint64_t pixels_phys;
static uint64_t pixels_size;
static uint32_t width;
static uint32_t height;
static uint32_t pitch;

/* The firmware's pixel order: 1 when red is in the low byte, 0 when blue is. */
static uint32_t order;

/* The top-left pixel of the text grid, which is centred on the screen. */
static uint32_t x_origin;
static uint32_t y_origin;

/* The sixteen text colours as 0xRRGGBB, in the PC's attribute order. */
static const uint32_t palette[16] = {
	0x000000, 0x0000aa, 0x00aa00, 0x00aaaa, 0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
	0x555555, 0x5555ff, 0x55ff55, 0x55ffff, 0xff5555, 0xff55ff, 0xffff55, 0xffffff
};

static uint32_t colour(uint8_t index);
static void put_pixel(unsigned x, unsigned y, uint32_t value);
static void flush_rows(unsigned x, unsigned y, unsigned columns, unsigned rows);
static void fill_screen(uint32_t value);
static void flush_screen(void);

/*
 * Paints one character cell: a glyph of the 8x16 console font in the
 * attribute's foreground on its background.
 */
void
rpi_framebuffer_cell(
	unsigned row,
	unsigned column,
	int character,
	uint8_t attribute)
{
	const uint8_t *glyph;
	uint32_t foreground;
	uint32_t background;
	uint32_t value;
	unsigned x;
	unsigned y;
	unsigned line;
	unsigned bit;
	uint8_t bits;

	/* Nothing to paint before the framebuffer exists. */
	if (pixels == 0)
		return;

	/* Nothing to paint off the grid. */
	if (row >= CELL_ROWS || column >= CELL_COLUMNS)
		return;

	/* The glyph of the character (the font covers every byte value). */
	glyph = &rpi_font8x16[((unsigned)character & 0xffU) * RPI_FONT_HEIGHT];
	foreground = colour(attribute & 15U);
	background = colour(attribute >> 4);
	x = x_origin + column * 8U;
	y = y_origin + row * RPI_FONT_HEIGHT;

	/* Each row, from its leftmost pixel in the high bit. */
	for (line = 0; line < RPI_FONT_HEIGHT; line++) {
		bits = glyph[line];

		/* Each pixel of the row is the foreground where its bit is set. */
		for (bit = 0; bit < 8U; bit++) {
			value = background;
			if ((bits & (0x80U >> bit)) != 0)
				value = foreground;
			put_pixel(x + bit, y + line, value);
		}
	}

	/* The cell, out to the memory the display reads. */
	flush_rows(x, y, 8U, RPI_FONT_HEIGHT);
}

/*
 * Draws the cursor: an underline in the two bottom rows of a cell.
 *
 * Only a visible cursor is drawn; the cell's own repaint removes it.
 */
void
rpi_framebuffer_cursor(
	unsigned row,
	unsigned column,
	int visible)
{
	unsigned x;
	unsigned y;
	unsigned cell_x;
	unsigned cell_y;

	/* Nothing to draw before the framebuffer exists, for a hidden cursor, or off the grid. */
	if (pixels == 0)
		return;
	if (!visible)
		return;
	if (row >= CELL_ROWS || column >= CELL_COLUMNS)
		return;

	/* Paints the underline white. */
	cell_x = x_origin + column * 8U;
	cell_y = y_origin + row * RPI_FONT_HEIGHT;
	for (y = CURSOR_FIRST_ROW; y < RPI_FONT_HEIGHT; y++) {
		for (x = 0; x < 8U; x++)
			put_pixel(cell_x + x, cell_y + y, colour(15));
	}

	/* The underline, out to the memory the display reads. */
	flush_rows(cell_x, cell_y + CURSOR_FIRST_ROW, 8U, RPI_FONT_HEIGHT - CURSOR_FIRST_ROW);
}

/*
 * Asks the firmware for a 32-bit framebuffer of the display's size and
 * clears it.
 *
 * The size is the one the firmware set up for the display (config.txt's
 * framebuffer_width and framebuffer_height, or the mode's), so that the
 * framebuffer is scanned out as it is, without scaling; 640x480 when the
 * firmware reports none (QEMU).  Returns 0 when the framebuffer is ready.
 */
int
rpi_framebuffer_init(
	uintptr_t mailbox_phys)
{
	uint32_t want_width;
	uint32_t want_height;
	int error;

	/* Asks for the display's size: one tag with room for two words. */
	request[0] = SIZE_WORDS * 4U;
	request[1] = 0;
	request[2] = TAG_GET_PHYSICAL;
	request[3] = 8;
	request[4] = 0;
	request[5] = 0;
	request[6] = 0;
	request[7] = 0;
	error = rpi_mailbox_property(mailbox_phys, request, request[0]);

	/* Takes the firmware's size when it reported a sensible one. */
	want_width = DEFAULT_WIDTH;
	want_height = DEFAULT_HEIGHT;
	if (error == 0) {
		/* A size the console fits in and the firmware can scan out. */
		if (request[5] >= MIN_WIDTH &&
		    request[6] >= MIN_HEIGHT &&
		    request[5] <= MAX_SIZE &&
		    request[6] <= MAX_SIZE) {
			want_width = request[5];
			want_height = request[6];
		}
	}

	/* Starts the set-up request: its size and the request code. */
	request[0] = SETUP_WORDS * 4U;
	request[1] = 0;

	/* The display's size in pixels. */
	request[2] = TAG_PHYSICAL;
	request[3] = 8;
	request[4] = 8;
	request[5] = want_width;
	request[6] = want_height;

	/* The framebuffer's size, the same, so nothing is scaled. */
	request[7] = TAG_VIRTUAL;
	request[8] = 8;
	request[9] = 8;
	request[10] = want_width;
	request[11] = want_height;

	/* 32 bits per pixel. */
	request[12] = TAG_DEPTH;
	request[13] = 4;
	request[14] = 4;
	request[15] = 32;

	/* Red in the low byte, which the firmware may refuse; the answer says which it took. */
	request[16] = TAG_ORDER;
	request[17] = 4;
	request[18] = 4;
	request[19] = 1;

	/* The allocation, aligned to 4096 bytes; the answer is its base and size. */
	request[20] = TAG_ALLOCATE;
	request[21] = 8;
	request[22] = 8;
	request[23] = 4096;
	request[24] = 0;

	/* The row length in bytes. */
	request[25] = TAG_PITCH;
	request[26] = 4;
	request[27] = 4;
	request[28] = 0;

	/* The end tag. */
	request[29] = 0;

	/* Sends the request; a refused one leaves no framebuffer. */
	error = rpi_mailbox_property(mailbox_phys, request, request[0]);
	if (error != 0)
		return -1;

	/*
	 * Reads the answers.  The base is a bus address whose top two bits
	 * name the VideoCore's cache alias; dropping them leaves the ARM's
	 * physical address, which on both boards lies in the first gigabyte.
	 */
	width = request[ANSWER_WIDTH];
	height = request[ANSWER_HEIGHT];
	order = request[ANSWER_ORDER];
	pixels_phys = (uint64_t)(request[ANSWER_BASE] & 0x3fffffffU);
	pixels_size = request[ANSWER_SIZE];
	pitch = request[ANSWER_PITCH];

	/* Refuses a framebuffer too small for the console. */
	if (width < MIN_WIDTH || height < MIN_HEIGHT)
		return -1;

	/* Refuses rows shorter than a row of pixels, or a buffer shorter than the rows. */
	if (pitch < width * 4U)
		return -1;
	if (pixels_size < (uint64_t)pitch * height)
		return -1;

	/* Refuses a missing buffer, or one that wraps around the address space. */
	if (pixels_phys == 0)
		return -1;
	if (pixels_phys + pixels_size < pixels_phys)
		return -1;

	/* Reaches the framebuffer through the direct map, with the text grid centred. */
	pixels = (volatile uint8_t *)(ARM64_DIRECT_BASE + pixels_phys);
	x_origin = (width - MIN_WIDTH) / 2U;
	y_origin = (height - MIN_HEIGHT) / 2U;

	/* Hands the framebuffer to the kernel and keeps it out of the allocator. */
	rpi_boot_set_framebuffer(pixels_phys, pixels_size, width, height, pitch, order);

	/* Clears the screen to black. */
	fill_screen(0);
	flush_screen();

	/* Succeeded: the framebuffer is ready for the console. */
	return 0;
}

/*
 * Reports whether the firmware's framebuffer is set up.
 */
int
rpi_framebuffer_ready(
	void)
{
	/* Succeeded: whether there is a framebuffer to paint. */
	return pixels != 0;
}

/*
 * Shows a test pattern for three seconds: the left third red, the middle
 * green and the right blue, with a white frame 16 pixels wide.  Seen on a
 * board, it tells whether the display shows what the CPU writes, where, and
 * in which colour order.
 */
void
rpi_framebuffer_test_pattern(
	void)
{
	uint32_t value;
	unsigned x;
	unsigned y;

	/* Nothing to show without a framebuffer. */
	if (pixels == 0)
		return;

	/* Each pixel: the frame, or the colour of its third. */
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			value = colour(12);
			if (x >= width / 3U)
				value = colour(10);
			if (x >= width / 3U * 2U)
				value = colour(9);
			if (x < 16U || y < 16U || x >= width - 16U || y >= height - 16U)
				value = colour(15);
			put_pixel(x, y, value);
		}
	}

	/* Out to memory, then time to look at it. */
	flush_screen();
	rpi_delay_ms(3000U);

	/* Black again for the console. */
	fill_screen(0);
	flush_screen();
}

/*
 * Reports what the firmware gave for the framebuffer.
 */
void
rpi_framebuffer_describe(
	void)
{
	/* The size, the row length, the order and where it is. */
	hal_printf(ARM64_BOARD_TAG " FRAMEBUFFER %ux%u pitch=%u order=%u phys=%llx size=%llx\n",
		   width,
		   height,
		   pitch,
		   order,
		   (unsigned long long)pixels_phys,
		   (unsigned long long)pixels_size);
}

/* Turns a text colour into a pixel in the firmware's byte order. */
static uint32_t
colour(
	uint8_t index)
{
	uint32_t rgb;
	uint32_t swapped;

	/* The colour as 0xRRGGBB. */
	rgb = palette[index & 15U];

	/* Red in the low byte is the palette's own order. */
	if (order)
		return rgb;

	/* Succeeded: blue in the low byte, red and blue exchanged. */
	swapped = ((rgb & 0xffU) << 16) | (rgb & 0xff00U) | ((rgb >> 16) & 0xffU);
	return swapped;
}

/* Writes one pixel. */
static void
put_pixel(
	unsigned x,
	unsigned y,
	uint32_t value)
{
	volatile uint32_t *pixel;

	/* The pixel's word: its row, then its column of four bytes. */
	pixel = (volatile uint32_t *)(pixels + (size_t)y * pitch + (size_t)x * 4U);
	*pixel = value;
}

/*
 * Makes rows of pixels reach memory.  The framebuffer is written through
 * the cacheable direct map, and the display reads memory past the CPU's
 * caches, so a pixel that stays in the data cache is never shown.
 */
static void
flush_rows(
	unsigned x,
	unsigned y,
	unsigned columns,
	unsigned rows)
{
	uintptr_t start;
	unsigned line;

	/* Each row's bytes, cleaned from the data cache to memory. */
	for (line = 0; line < rows; line++) {
		start = (uintptr_t)(pixels + (size_t)(y + line) * pitch + (size_t)x * 4U);
		hal_dcache_clean_range(start, (size_t)columns * 4U);
	}

	/* The display is outside the CPU's shareable domain: a full barrier. */
	__asm__ volatile("dsb sy" ::: "memory");
}

/* Sets every pixel of the screen to one value. */
static void
fill_screen(
	uint32_t value)
{
	unsigned x;
	unsigned y;

	/* Each pixel, row by row. */
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++)
			put_pixel(x, y, value);
	}
}

/* Makes the whole framebuffer reach memory. */
static void
flush_screen(
	void)
{
	/* Cleans the buffer from the data cache, then a full barrier for the display. */
	hal_dcache_clean_range((uintptr_t)pixels, (size_t)pixels_size);
	__asm__ volatile("dsb sy" ::: "memory");
}
