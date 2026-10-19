/*
 * Host test of the sun4u firmware framebuffer (WS275 p004).
 *
 * Compiles src/drivers/platform/sun4u/sun4u-firmware-fb.c with
 * /dev/graphics on, against fakes of the HAL's names and of the device
 * mapping (a heap buffer).  The host is little-endian, so its native
 * 32-bit store lands as the kernel's invert-endian window does on sun4u:
 * the bytes in memory are those the screen reads.  Checks, for
 * BGRX8888, XRGB8888 and RGB332, the bytes of a fill, of an RGB24, an
 * INDEX8 and a patterned blit row and of a line, the one mode, the glyph,
 * and the refusals of a short pitch and of an unknown format.
 *
 * Usage: host-firmware-fb   (prints one line a check, then PASS or FAIL)
 */

#undef CONFIG_DRIVER_GRAPHICS_DEVICE
#define CONFIG_DRIVER_GRAPHICS_DEVICE 1

#include "src/drivers/platform/sun4u/sun4u-firmware-fb.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_WIDTH	16U
#define TEST_HEIGHT	4U

static uint64_t name_phys = 0x1000U;
static uint64_t name_width = TEST_WIDTH;
static uint64_t name_height = TEST_HEIGHT;
static uint64_t name_pitch;
static uint64_t name_format;
static uint8_t *screen;
static size_t screen_size;
static const struct drv_graphics_backend *offered;
static int failures;

void *
hal_get_arch_handoff(const char *name)
{
	if (strcmp(name, "sun4u.framebuffer.phys") == 0)
		return &name_phys;
	if (strcmp(name, "sun4u.framebuffer.width") == 0)
		return &name_width;
	if (strcmp(name, "sun4u.framebuffer.height") == 0)
		return &name_height;
	if (strcmp(name, "sun4u.framebuffer.pitch") == 0)
		return &name_pitch;
	if (strcmp(name, "sun4u.framebuffer.format") == 0)
		return &name_format;
	return NULL;
}

int
kern_device_map(uint64_t address, size_t size, unsigned attributes, void **mapped)
{
	(void)address;
	(void)attributes;
	free(screen);
	screen = calloc(1, size);
	screen_size = size;
	*mapped = screen;
	return screen == NULL ? ENOMEM : 0;
}

int
drv_graphics_backend_register(const struct drv_graphics_backend *backend, int priority)
{
	if (priority != DRV_GRAPHICS_PRIORITY_FIRMWARE)
		return EINVAL;
	offered = backend;
	return 0;
}

static void
check(int ok, const char *what)
{
	printf("%s: %s\n", what, ok ? "ok" : "FAIL");
	if (!ok)
		failures++;
}

/* Maps a fresh framebuffer of one format; reports the init result. */
static int
start(unsigned format, unsigned pitch)
{
	fb_pixels = NULL;
	offered = NULL;
	name_format = format;
	name_pitch = pitch;
	return drv_sun4u_fb_init();
}

/* Compares the bytes of one pixel with the expected ones. */
static int
pixel_is(unsigned x, unsigned y, unsigned bytes, const uint8_t *want)
{
	return memcmp(screen + y * name_pitch + x * bytes, want, bytes) == 0;
}

/* Runs the drawing checks for one four-byte order. */
static void
four_byte(unsigned format, const char *label, const uint8_t want_color[4], const uint8_t want_blue[4])
{
	struct graphics_mode_info mode;
	struct graphics_mode entered;
	struct graphics_rect rect = {1, 1, 2, 1};
	struct drv_graphics_row row;
	static const uint8_t rgb[6] = {0x11, 0x22, 0x33, 0x00, 0x00, 0xff};
	static const uint8_t black[4] = {0, 0, 0, 0};
	static const uint32_t palette[2] = {0x112233U, 0x0000ffU};
	static const uint8_t index[4] = {0, 1, 0, 1};
	char what[96];

	snprintf(what, sizeof(what), "%s: init with pitch 72", label);
	check(start(format, 72U) == 0 && screen_size == 72U * TEST_HEIGHT, what);
	check(drv_sun4u_fb_register_backend() == 0 && offered != NULL, "  offered at the firmware priority");
	check(offered->get_modes(&mode, 1) == 1 && mode.width == TEST_WIDTH &&
	      mode.height == TEST_HEIGHT && mode.bits_per_pixel == 32U && mode.stride == 72U,
	      "  one 32 bpp mode");
	memset(&entered, 0, sizeof(entered));
	check(offered->enter(&entered) && entered.bits_per_pixel == 32U, "  enter");

	offered->fill(&rect, 0x112233U);
	check(pixel_is(1, 1, 4, want_color) && pixel_is(2, 1, 4, want_color) &&
	      pixel_is(0, 1, 4, black) && pixel_is(3, 1, 4, black), "  fill 0x112233");

	memset(&row, 0, sizeof(row));
	row.format = KERN_GRAPHICS_FORMAT_RGB24;
	row.width = 2;
	row.pixels = rgb;
	offered->blit_row(4, 2, &row, 0, 0);
	check(pixel_is(4, 2, 4, want_color) && pixel_is(5, 2, 4, want_blue), "  blit RGB24 (0x112233, 0x0000ff)");

	row.format = KERN_GRAPHICS_FORMAT_INDEX8;
	row.width = 4;
	row.pixels = index;
	row.palette = palette;
	row.palette_size = 2;
	row.pattern_x = 1;
	row.pattern_y = 0;
	offered->blit_row(8, 3, &row, 0x00000000000000a5ULL, 1);
	/* Row 0 of the pattern is 0xa5: columns 0, 2, 5, 7; this chunk starts at column 1. */
	check(pixel_is(8, 3, 4, black) && pixel_is(9, 3, 4, want_blue) &&
	      pixel_is(10, 3, 4, black) && pixel_is(11, 3, 4, black), "  patterned INDEX8 keeps the phase");

	offered->line(0, 0, 3, 0, 0x0000ffU);
	check(pixel_is(0, 0, 4, want_blue) && pixel_is(3, 0, 4, want_blue) && pixel_is(4, 0, 4, black),
	      "  line");
}

int
main(void)
{
	static const uint8_t bgrx_color[4] = {0x33, 0x22, 0x11, 0x00};
	static const uint8_t bgrx_blue[4] = {0xff, 0x00, 0x00, 0x00};
	static const uint8_t xrgb_color[4] = {0x00, 0x11, 0x22, 0x33};
	static const uint8_t xrgb_blue[4] = {0x00, 0x00, 0x00, 0xff};
	struct graphics_mode_info mode;
	struct graphics_rect rect = {0, 0, 1, 1};
	uint8_t bitmap[32];
	unsigned width;
	unsigned height;

	four_byte(KERN_BOOT_FRAMEBUFFER_BGRX8888, "BGRX8888", bgrx_color, bgrx_blue);
	four_byte(KERN_BOOT_FRAMEBUFFER_XRGB8888, "XRGB8888", xrgb_color, xrgb_blue);

	/* RGB332: the design's colors of the bar and of the wallpaper's point (design section 5). */
	check(start(KERN_BOOT_FRAMEBUFFER_RGB332, 16U) == 0, "RGB332: init");
	check(drv_sun4u_fb_register_backend() == 0 &&
	      offered->get_modes(&mode, 1) == 1 && mode.bits_per_pixel == 8U, "  one 8 bpp mode");
	offered->fill(&rect, 0x17212bU);
	check(screen[0] == 0x04U, "  0x17212b is 0x04");
	rect.x = 1;
	offered->fill(&rect, 0xff7f00U);
	check(screen[1] == 0xecU, "  0xff7f00 is 0xec");

	check(start(KERN_BOOT_FRAMEBUFFER_BGRX8888, 60U) == ENODEV, "short pitch refused");
	check(start(KERN_BOOT_FRAMEBUFFER_RGBX8888, 64U) == ENODEV, "RGBX8888 refused");
	check(drv_sun4u_fb_ready() == 0, "  not ready after a refusal");

	check(start(KERN_BOOT_FRAMEBUFFER_BGRX8888, 64U) == 0, "glyph: init");
	drv_sun4u_fb_get_size(&width, &height);
	check(width == TEST_WIDTH && height == TEST_HEIGHT, "  size");
	drv_sun4u_fb_register_backend();
	check(offered->get_glyph('A', bitmap, &width, &height) && width == 8 && height == 16 &&
	      memcmp(bitmap, &drv_pcat_vgafont16['A' * 16], 16) == 0, "  glyph A");
	check(offered->get_glyph(0x3042, bitmap, &width, &height) &&
	      memcmp(bitmap, &drv_pcat_vgafont16['?' * 16], 16) == 0, "  glyph beyond ASCII is ?");

	printf("host-firmware-fb: %s\n", failures == 0 ? "PASS" : "FAIL");
	return failures == 0 ? 0 : 1;
}
