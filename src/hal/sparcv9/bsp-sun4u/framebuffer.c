/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The sun4u board's framebuffer.
 *
 * The emulator's standard VGA has a linear framebuffer and the Bochs
 * display interface, which sets its resolution and depth.  The screen is
 * set to 640 by 400 pixels of 32 bits, so that 80 by 25 cells of the 8x16
 * font fill it exactly and a text console needs no margin.  The pixels and
 * the registers are PCI memory, little-endian, reached through the
 * physical bypass; on a big-endian machine the emulator reads the pixels
 * big-endian unless told otherwise, so the byte order is set first.
 */

#include <hal/hal.h>
#include "../asi.h"
#include "font.h"
#include "framebuffer.h"

/* The screen: its size in pixels, and the bytes of a row. */
#define FRAMEBUFFER_WIDTH	640U
#define FRAMEBUFFER_HEIGHT	400U
#define FRAMEBUFFER_DEPTH	32U
#define FRAMEBUFFER_STRIDE	(FRAMEBUFFER_WIDTH * 4U)

/*
 * The register window of the standard VGA: the VGA ports from 0x3c0 at
 * offset 0x400, the Bochs display interface's registers, 16 bits each, at
 * offset 0x500, and the emulator's own registers at offset 0x600, of which
 * the second sets the byte order of the framebuffer's pixels.
 *
 * Values referenced: QEMU v10.0.0 hw/display/vga-pci.c
 * (PCI_VGA_IOPORT_OFFSET, PCI_VGA_BOCHS_OFFSET, PCI_VGA_QEXT_OFFSET,
 * PCI_VGA_QEXT_REG_BYTEORDER, PCI_VGA_QEXT_LITTLE_ENDIAN).
 */
#define FRAMEBUFFER_VGA_PORTS		0x400U
#define FRAMEBUFFER_BOCHS_REGISTERS	0x500U
#define FRAMEBUFFER_QEXT_BYTEORDER	0x604U
#define FRAMEBUFFER_QEXT_LITTLE_ENDIAN	0x1e1e1e1eU

/* The VGA attribute controller's port, relative to port 0x3c0. */
#define FRAMEBUFFER_VGA_ATTRIBUTE	0x00U

/*
 * The attribute controller's index value that lets the screen show.
 *
 * Values referenced: Linux 6.8.12 drivers/gpu/drm/tiny/bochs.c (the value
 * 0x20 written to port 0x3c0 to unblank the screen).
 */
#define FRAMEBUFFER_VGA_UNBLANK		0x20U

/*
 * The Bochs display interface's registers, by index, and the bits of its
 * enable register.
 *
 * Values referenced: QEMU v10.0.0 include/hw/display/bochs-vbe.h
 * (VBE_DISPI_INDEX_XRES, _YRES, _BPP, _ENABLE, _BANK, _VIRT_WIDTH,
 * _VIRT_HEIGHT, _X_OFFSET, _Y_OFFSET; VBE_DISPI_ENABLED,
 * VBE_DISPI_LFB_ENABLED).
 */
#define FRAMEBUFFER_BOCHS_XRES		1U
#define FRAMEBUFFER_BOCHS_YRES		2U
#define FRAMEBUFFER_BOCHS_BPP		3U
#define FRAMEBUFFER_BOCHS_ENABLE	4U
#define FRAMEBUFFER_BOCHS_BANK		5U
#define FRAMEBUFFER_BOCHS_VIRT_WIDTH	6U
#define FRAMEBUFFER_BOCHS_VIRT_HEIGHT	7U
#define FRAMEBUFFER_BOCHS_X_OFFSET	8U
#define FRAMEBUFFER_BOCHS_Y_OFFSET	9U
#define FRAMEBUFFER_BOCHS_ENABLED	0x01U
#define FRAMEBUFFER_BOCHS_LINEAR	0x40U

/*
 * The physical addresses of the first pixel and of the register window.
 *
 * Set once by sun4u_framebuffer_init() on the boot processor; zero until
 * then.
 */
static uint64_t framebuffer_pixels;
static uint64_t framebuffer_registers;

/*
 * Nonzero once the screen shows; until then nothing is drawn.  Set once
 * by sun4u_framebuffer_init().
 */
static int framebuffer_on;

/*
 * The colours of the sixteen text attributes, as XRGB8888 values.
 *
 * Values referenced: the IBM VGA text-mode palette, the sixteen default
 * attribute colours.
 */
static const uint32_t framebuffer_palette[16] = {
	0x000000U, 0x0000aaU, 0x00aa00U, 0x00aaaaU,
	0xaa0000U, 0xaa00aaU, 0xaa5500U, 0xaaaaaaU,
	0x555555U, 0x5555ffU, 0x55ff55U, 0x55ffffU,
	0xff5555U, 0xff55ffU, 0xffff55U, 0xffffffU
};

static void bochs_write(unsigned index, uint16_t value);
static void put_pixel(unsigned x, unsigned y, uint32_t value);
static void fill_black(void);

/*
 * Sets the display to its linear mode and clears it.
 */
int
sun4u_framebuffer_init(
	uint64_t pci_memory_base,
	uint32_t framebuffer,
	uint32_t framebuffer_size,
	uint32_t registers)
{
	/* Refuses a display the loader did not find, or one too small for the screen. */
	if (framebuffer == 0 || registers == 0)
		return HAL_ERR_UNSUPPORTED;
	if (framebuffer_size < FRAMEBUFFER_STRIDE * FRAMEBUFFER_HEIGHT)
		return HAL_ERR_UNSUPPORTED;

	/* The PCI addresses as the processor sees them. */
	framebuffer_pixels = pci_memory_base + framebuffer;
	framebuffer_registers = pci_memory_base + registers;

	/* Turns the display off while its mode is set, and reads the pixels little-endian. */
	bochs_write(FRAMEBUFFER_BOCHS_ENABLE, 0);
	sparcv9_phys_write32_le(framebuffer_registers + FRAMEBUFFER_QEXT_BYTEORDER, FRAMEBUFFER_QEXT_LITTLE_ENDIAN);

	/* Sets the resolution, the depth and an unscrolled view of the whole framebuffer. */
	bochs_write(FRAMEBUFFER_BOCHS_XRES, (uint16_t)FRAMEBUFFER_WIDTH);
	bochs_write(FRAMEBUFFER_BOCHS_YRES, (uint16_t)FRAMEBUFFER_HEIGHT);
	bochs_write(FRAMEBUFFER_BOCHS_BPP, (uint16_t)FRAMEBUFFER_DEPTH);
	bochs_write(FRAMEBUFFER_BOCHS_BANK, 0);
	bochs_write(FRAMEBUFFER_BOCHS_VIRT_WIDTH, (uint16_t)FRAMEBUFFER_WIDTH);
	bochs_write(FRAMEBUFFER_BOCHS_VIRT_HEIGHT, (uint16_t)FRAMEBUFFER_HEIGHT);
	bochs_write(FRAMEBUFFER_BOCHS_X_OFFSET, 0);
	bochs_write(FRAMEBUFFER_BOCHS_Y_OFFSET, 0);

	/* Turns the linear mode on, and lets the VGA show it. */
	bochs_write(FRAMEBUFFER_BOCHS_ENABLE, FRAMEBUFFER_BOCHS_ENABLED | FRAMEBUFFER_BOCHS_LINEAR);
	sparcv9_phys_write8(framebuffer_registers + FRAMEBUFFER_VGA_PORTS + FRAMEBUFFER_VGA_ATTRIBUTE, FRAMEBUFFER_VGA_UNBLANK);

	/* Clears the screen; from now on the console draws to it. */
	fill_black();
	framebuffer_on = 1;

	/* Succeeded: the screen shows. */
	return HAL_OK;
}

/*
 * Reports whether the screen shows.
 */
int
sun4u_framebuffer_ready(
	void)
{
	/* Succeeded: whether sun4u_framebuffer_init() succeeded. */
	return framebuffer_on;
}

/*
 * Draws one text cell: a glyph in the attribute's colours.
 */
void
sun4u_framebuffer_cell(
	unsigned row,
	unsigned column,
	int character,
	uint8_t attribute)
{
	const uint8_t *glyph;
	uint32_t foreground;
	uint32_t background;
	uint32_t value;
	unsigned line;
	unsigned bit;
	uint8_t bits;

	/* Nothing to draw before the screen is shown, or off the grid. */
	if (!framebuffer_on)
		return;
	if (row >= SUN4U_CONSOLE_ROWS || column >= SUN4U_CONSOLE_COLUMNS)
		return;

	/* The glyph of the character (the font covers every byte value) and its colours. */
	glyph = &sun4u_font8x16[((unsigned)character & 0xffU) * SUN4U_FONT_HEIGHT];
	foreground = framebuffer_palette[attribute & 15U];
	background = framebuffer_palette[(attribute >> 4) & 15U];

	/* Each row of the glyph, from its leftmost pixel in the high bit. */
	for (line = 0; line < SUN4U_FONT_HEIGHT; line++) {
		bits = glyph[line];
		for (bit = 0; bit < 8U; bit++) {
			value = background;
			if ((bits & (0x80U >> bit)) != 0)
				value = foreground;
			put_pixel(column * 8U + bit, row * SUN4U_FONT_HEIGHT + line, value);
		}
	}
}

/*
 * Draws the cursor: the two bottom pixel rows of a cell in white.
 */
void
sun4u_framebuffer_cursor(
	unsigned row,
	unsigned column)
{
	unsigned line;
	unsigned bit;

	/* Nothing to draw before the screen is shown, or off the grid. */
	if (!framebuffer_on)
		return;
	if (row >= SUN4U_CONSOLE_ROWS || column >= SUN4U_CONSOLE_COLUMNS)
		return;

	/* Whitens rows 14 and 15 of the cell. */
	for (line = SUN4U_FONT_HEIGHT - 2U; line < SUN4U_FONT_HEIGHT; line++) {
		for (bit = 0; bit < 8U; bit++)
			put_pixel(column * 8U + bit, row * SUN4U_FONT_HEIGHT + line, framebuffer_palette[15]);
	}
}

/* Writes one register of the Bochs display interface. */
static void
bochs_write(
	unsigned index,
	uint16_t value)
{
	/* Each register is a little-endian 16-bit word at twice its index. */
	sparcv9_phys_write16_le(framebuffer_registers + FRAMEBUFFER_BOCHS_REGISTERS + index * 2U, value);
}

/* Stores one pixel of the screen. */
static void
put_pixel(
	unsigned x,
	unsigned y,
	uint32_t value)
{
	uint64_t address;

	/* The pixel's place in the framebuffer, a little-endian XRGB8888 word. */
	address = framebuffer_pixels + (uint64_t)y * FRAMEBUFFER_STRIDE + (uint64_t)x * 4U;
	sparcv9_phys_write32_le(address, value);
}

/* Paints the whole screen black. */
static void
fill_black(
	void)
{
	unsigned x;
	unsigned y;

	/* Clears every pixel, row by row. */
	for (y = 0; y < FRAMEBUFFER_HEIGHT; y++) {
		for (x = 0; x < FRAMEBUFFER_WIDTH; x++)
			put_pixel(x, y, 0);
	}
}
