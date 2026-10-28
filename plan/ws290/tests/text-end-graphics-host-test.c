/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the console's leaving of the display on the PC/AT, through
 * /dev/graphics and the backend: the real src/kern/text-display.c and
 * src/drivers/platform/pcat/graphics/{text.c,pcat-graphics.c,backend.c,
 * font.c,vgafont.c,splash.c} over the stand-ins of text-end-stubs.c, which
 * model the amd64 hardware layer's device mappings (a removed mapping turns
 * read-only, so a write through it ends the process).
 *
 * Each scene runs in its own process: text-end-graphics-host-test SCENE.
 * The scenes trigger and trigger-none need the build with
 * KERN_TEST_TEXT_END_SECONDS defined.  text-end-host-test.sh runs them all.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal/hal.h"
#include "kern/cdev.h"
#include "kern/clock.h"
#include "kern/graphics-device.h"
#include "kern/text-display.h"
#include "uapi/errno.h"
#include "uapi/graphics.h"
#include "drivers/platform/pcat/graphics/pcat.h"
#include "drivers/platform/pcat/graphics/backend.h"
#include "drivers/platform/pcat/graphics/splash.h"

#include "text-end-stubs.h"

/* The places another device's memory lies (nothing of the console is there), and a moved framebuffer's BAR. */
#define APART_PHYSICAL		0x100000000ULL
#define APART_BYTES		0x10000000ULL
#define BAR_OLD_PHYSICAL	0xc0000000ULL
#define BAR_NEW_PHYSICAL	0xd0000000ULL
#define BAR_BYTES		0x10000000ULL

/* The text memory inside the legacy aperture. */
#define LEGACY_TEXT_PHYSICAL	0x000b8000ULL
#define LEGACY_TEXT_OFFSET	0x00018000U
#define LEGACY_TEXT_BYTES	0x00001000U

/* One scene: its name and what it runs. */
struct scene {
	const char *name;
	void (*run)(void);
};

/* How many checks ran. */
static unsigned checks;

/* The image a snapshot of the console is drawn into. */
static uint32_t snapshot_pixels[STUB_FB_WIDTH * STUB_FB_HEIGHT];

/* Two open files of /dev/graphics, as the device sees them. */
static long file_storage[2][32];

/* What the scenes with threads and hooks saw. */
static int second_result;
static unsigned long second_sequence;
static uint64_t window_at_sleep[3];
static int ticker_end_result;
static unsigned ticker_sleeps;

static void expect(int condition, const char *what);
static void boot(int quiet, int handoff, int register_device, unsigned fail_first_map);
static uint64_t framebuffer_sum(const volatile uint32_t *pixels);
static uint64_t snapshot_sum(void);
static struct kern_text_range range_of(uint64_t physical, uint64_t bytes);
static struct file *file_of(unsigned index);
static int graphics_enter(struct file *file);
static int graphics_fill(struct file *file);
static void expect_handoff_given_back(unsigned first, const char *what);
static void scene_draw(void);
static void scene_end(void);
static void scene_quiet(void);
static void quiet_sleep(unsigned sleeps);
static void scene_graphics(void);
static void scene_unregistered(void);
static void scene_unmap_fail(void);
static void scene_unmap_fail_apart(void);
static void scene_window_busy(void);
static void scene_window_map_busy(void);
static void scene_vga(void);
static void scene_vga_apart(void);
static void scene_moved(void);
static void scene_moved_fail(void);
static void scene_order_bars_first(void);
static void scene_order_null_first(void);
static void scene_concurrent(void);
static void concurrent_unmap(void);
static void *concurrent_second(void *argument);
static void scene_trigger(void);
static void scene_trigger_none(void);

/* The scenes, by name. */
static const struct scene scenes[] = {
	{ "draw", scene_draw },
	{ "end", scene_end },
	{ "quiet", scene_quiet },
	{ "graphics", scene_graphics },
	{ "unregistered", scene_unregistered },
	{ "unmap-fail", scene_unmap_fail },
	{ "unmap-fail-apart", scene_unmap_fail_apart },
	{ "window-busy", scene_window_busy },
	{ "window-map-busy", scene_window_map_busy },
	{ "vga", scene_vga },
	{ "vga-apart", scene_vga_apart },
	{ "moved", scene_moved },
	{ "moved-fail", scene_moved_fail },
	{ "order-bars-first", scene_order_bars_first },
	{ "order-null-first", scene_order_null_first },
	{ "concurrent", scene_concurrent },
	{ "trigger", scene_trigger },
	{ "trigger-none", scene_trigger_none },
};

int
main(
	int argc,
	char **argv)
{
	size_t index;

	/* The scene the script asked for. */
	if (argc != 2)
		return 2;
	for (index = 0; index < sizeof(scenes) / sizeof(scenes[0]); index++) {
		if (strcmp(scenes[index].name, argv[1]) == 0)
			break;
	}
	if (index == sizeof(scenes) / sizeof(scenes[0]))
		return 2;
	scenes[index].run();

	/* The scene's summary. */
	printf("text-end-graphics-host-test %s: %u checks, %u failed\n", argv[1], checks, stub_failures);
	if (stub_failures != 0U)
		return 1;
	return 0;
}

/* Before the end: the console draws through the backend's mapping, and /dev/graphics enters and leaves. */
static void
scene_draw(void)
{
	const volatile uint32_t *pixels;
	uint64_t before;

	/* The boot, with the framebuffer mapped once. */
	boot(0, 1, 1, 0U);
	pixels = text_end_peek_pixels();
	expect(stub_count(0) == 1U && pixels == stub_find(0, 1U)->mapped, "draw: the console draws through the one mapping");
	expect(kernel_putc == kern_text_kernel_putc, "draw: the kernel's path is the console's");

	/* A character draws. */
	before = framebuffer_sum(pixels);
	kern_text_putc('A');
	expect(framebuffer_sum(pixels) != before, "draw: a character reaches the framebuffer");

	/* /dev/graphics: enter, fill, close; the close resumes the console. */
	expect(stub_graphics_ops->open(file_of(0U)) == 0, "draw: open");
	expect(graphics_enter(file_of(0U)) == 0, "draw: enter");
	before = framebuffer_sum(pixels);
	expect(graphics_fill(file_of(0U)) == 0 && framebuffer_sum(pixels) != before, "draw: a fill reaches the framebuffer");
	expect(stub_graphics_ops->close(file_of(0U)) == 0 && stub_resumes == 1U, "draw: the close resumes the console");
	expect(stub_count(1) == 0U, "draw: nothing unmapped");
}

/* The end over the framebuffer: every mapping goes, the window is taken and removed, the cells go on. */
static void
scene_end(void)
{
	struct kern_text_range range;
	const volatile uint32_t *pixels;
	unsigned first;
	unsigned row;
	unsigned column;
	unsigned column_after;
	unsigned mirrored;
	uint64_t before;
	uint64_t image;
	int visible;
	int error;

	/* The boot, a character on the screen. */
	boot(0, 1, 1, 0U);
	pixels = text_end_peek_pixels();
	kern_text_putc('A');

	/* The end over the framebuffer's range. */
	range = range_of(STUB_FB_PHYSICAL, STUB_FB_BYTES);
	first = stub_call_count;
	error = kern_text_end(&range, 1U);
	expect(error == 0, "end: 0");
	expect(stub_call_count == first + 3U, "end: three calls (unmap, map, unmap)");
	expect(stub_calls[first].unmap && stub_calls[first].mapped == (void *)(uintptr_t)pixels &&
	       stub_calls[first].bytes == (size_t)STUB_FB_BYTES && stub_calls[first].result == 0,
	       "end: the prepare's mapping removed at its address and size");
	expect_handoff_given_back(first + 1U, "end");
	expect(text_end_peek_pixels() == pixels && pixels != NULL, "end: the text layer's pointer is kept");
	expect(text_end_peek_ended(), "end: the text layer ended");

	/* A character: the cursor and the cells follow, the serial line too, nothing drawn. */
	before = framebuffer_sum(pixels);
	image = snapshot_sum();
	kern_text_get_cursor(&row, &column, &visible);
	mirrored = stub_mirrored;
	kern_text_putc('B');
	kern_text_get_cursor(&row, &column_after, &visible);
	expect(column_after == column + 1U && stub_mirrored == mirrored + 1U, "end: putc moves the cursor and reaches the serial line");
	expect(snapshot_sum() != image && framebuffer_sum(pixels) == before, "end: putc changes the cells, not the framebuffer");

	/* A write at a cell. */
	image = snapshot_sum();
	kern_text_write(2U, 0U, KERN_TEXT_ATTRIB_NORMAL, "XYZ");
	expect(snapshot_sum() != image && framebuffer_sum(pixels) == before, "end: write changes the cells, not the framebuffer");

	/* A clear. */
	image = snapshot_sum();
	kern_text_clear();
	kern_text_get_cursor(&row, &column, &visible);
	expect(row == 0U && column == 0U && snapshot_sum() != image && framebuffer_sum(pixels) == before,
	       "end: clear empties the cells and homes the cursor, not the framebuffer");

	/* A cursor placement. */
	error = kern_text_set_cursor(3U, 4U);
	kern_text_get_cursor(&row, &column, &visible);
	expect(error == 0 && row == 3U && column == 4U && framebuffer_sum(pixels) == before, "end: set_cursor moves the cursor, not the framebuffer");

	/* The ways back draw nothing and write no VGA register. */
	kern_text_update_cursor();
	kern_text_resume();
	kern_text_reveal();
	kern_text_reveal_fatal();
	expect(framebuffer_sum(pixels) == before && stub_io_writes == 0U, "end: resume, reveal and reveal_fatal draw nothing");

	/* A second end: 0, nothing mapped or unmapped. */
	first = stub_call_count;
	error = kern_text_end(&range, 1U);
	expect(error == 0 && stub_call_count == first, "end: a second end returns 0 without a call");
	expect(stub_bad_unmaps == 0U, "end: no unmap of an unknown mapping");
}

/*
 * A quiet boot whose framebuffer could not be mapped: the text is on VGA
 * text memory and the splash on the hardware layer's window, until the end
 * comes during the spinner's ticker.
 */
static void
scene_quiet(void)
{
	uint64_t before;
	uint64_t legacy;
	unsigned writes;
	unsigned index;
	unsigned windows;
	unsigned fixed;

	/* The boot: the framebuffer's mapping fails, the legacy aperture serves. */
	boot(1, 1, 1, 1U);
	expect(stub_find(0, 1U)->result != 0 && stub_find(0, 2U)->kind == STUB_KIND_FIXED, "quiet: the framebuffer unmapped, the legacy aperture mapped");
	expect(kernel_putc == kern_text_kernel_putc && stub_thread_count == 1U, "quiet: the console published, the ticker asked for");

	/* A line nobody sees turns the spinner on the window (the first only starts its clock). */
	before = stub_sum(stub_window, (size_t)STUB_FB_BYTES);
	kern_text_putc('\n');
	kern_text_putc('\n');
	expect(stub_sum(stub_window, (size_t)STUB_FB_BYTES) != before, "quiet: a line turns the spinner on the window");

	/* The ticker: a turn, then the end during its next sleep, then it stops. */
	stub_sleep_hook = quiet_sleep;
	stub_thread_entry[0](stub_thread_argument[0]);
	stub_sleep_hook = NULL;
	expect(ticker_sleeps == 2U, "quiet: the ticker ends after the end");
	expect(window_at_sleep[1] != window_at_sleep[0], "quiet: the ticker turned the spinner before the end");
	expect(stub_sum(stub_window, (size_t)STUB_FB_BYTES) == window_at_sleep[2], "quiet: the ticker drew nothing after the end");
	expect(ticker_end_result == 0, "quiet: the end returned 0");

	/* The window handed over and removed once; the legacy aperture stays. */
	windows = 0U;
	for (index = 0U; index < stub_call_count; index++) {
		if (stub_calls[index].kind == STUB_KIND_WINDOW)
			windows++;
	}
	expect(windows == 2U && stub_window_state == STUB_WINDOW_REMOVED, "quiet: the window taken and removed once each");
	fixed = 0U;
	for (index = 0U; index < stub_call_count; index++) {
		if (stub_calls[index].unmap && stub_calls[index].kind == STUB_KIND_FIXED)
			fixed++;
	}
	expect(fixed == 1U, "quiet: only the backend's own mapping of the legacy aperture let go (the window stays)");

	/* After the end: a line, a fatal stop draw nothing on the window or in text memory. */
	before = stub_sum(stub_window, (size_t)STUB_FB_BYTES);
	legacy = stub_sum(stub_legacy, (size_t)STUB_LEGACY_BYTES);
	writes = stub_io_writes;
	kern_text_putc('\n');
	kern_text_putc('Z');
	kern_text_reveal_fatal();
	expect(stub_sum(stub_window, (size_t)STUB_FB_BYTES) == before, "quiet: nothing on the window after the end");
	expect(stub_sum(stub_legacy, (size_t)STUB_LEGACY_BYTES) == legacy && stub_io_writes == writes, "quiet: nothing in text memory or the VGA registers after the end");
	expect(stub_bad_unmaps == 0U, "quiet: no unmap of an unknown mapping");
}

/* The ticker's sleeps in the quiet scene: the window before each turn, and the end at the second. */
static void
quiet_sleep(
	unsigned sleeps)
{
	struct kern_text_range range;

	/* The window as the turn finds it. */
	ticker_sleeps = sleeps;
	if (sleeps <= 2U)
		window_at_sleep[sleeps - 1U] = stub_sum(stub_window, (size_t)STUB_FB_BYTES);

	/* The end, from another thread's point of view, between two turns. */
	if (sleeps == 2U) {
		range = range_of(STUB_FB_PHYSICAL, STUB_FB_BYTES);
		ticker_end_result = kern_text_end(&range, 1U);
		window_at_sleep[2] = stub_sum(stub_window, (size_t)STUB_FB_BYTES);
	}
}

/* /dev/graphics entered when the console ends: refused from then on, and nothing resumes. */
static void
scene_graphics(void)
{
	const volatile uint32_t *pixels;
	unsigned resumes;
	uint64_t before;
	int error;

	/* Entered and drawing. */
	boot(0, 1, 1, 0U);
	pixels = text_end_peek_pixels();
	expect(stub_graphics_ops->open(file_of(0U)) == 0 && graphics_enter(file_of(0U)) == 0, "graphics: open and enter");
	before = framebuffer_sum(pixels);
	expect(graphics_fill(file_of(0U)) == 0 && framebuffer_sum(pixels) != before, "graphics: a fill draws");

	/* The end, without ranges. */
	resumes = stub_resumes;
	error = kern_text_end(NULL, 0U);
	expect(error == 0, "graphics: the end returns 0");

	/* Everything refused, nothing drawn, nothing resumed. */
	before = framebuffer_sum(pixels);
	expect(graphics_fill(file_of(0U)) == ENODEV && framebuffer_sum(pixels) == before, "graphics: a fill is ENODEV and draws nothing");
	expect(graphics_enter(file_of(0U)) == ENODEV, "graphics: an enter is ENODEV");
	expect(stub_graphics_ops->close(file_of(0U)) == 0 && stub_resumes == resumes, "graphics: the close is 0 and does not resume");
	expect(stub_graphics_ops->open(file_of(1U)) == ENODEV, "graphics: a new open is ENODEV");
	drv_graphics_device_restore_text();
	expect(stub_resumes == resumes && framebuffer_sum(pixels) == before, "graphics: restore_text does nothing");
}

/* The end before /dev/graphics is registered: no lock, and the device registered later refuses. */
static void
scene_unregistered(void)
{
	unsigned resumes;
	int error;

	/* The boot without the device. */
	boot(0, 1, 0, 0U);

	/* The end, without the lock. */
	error = kern_text_end(NULL, 0U);
	expect(error == 0 && stub_mutex_locks == 0U, "unregistered: 0, without a lock");
	expect(stub_count(1) == 2U && stub_window_state == STUB_WINDOW_REMOVED, "unregistered: the mappings and the window removed");

	/* The device registered after: open refused, restore_text does nothing. */
	expect(drv_graphics_device_register() == 0, "unregistered: the device registers");
	expect(stub_graphics_ops->open(file_of(0U)) == ENODEV, "unregistered: open is ENODEV");
	resumes = stub_resumes;
	drv_graphics_device_restore_text();
	expect(stub_resumes == resumes, "unregistered: restore_text does nothing");
}

/* A removal that fails, in the caller's ranges: its error, the same again, 0 for ranges apart. */
static void
scene_unmap_fail(void)
{
	struct kern_text_range range;
	const volatile uint32_t *pixels;
	unsigned first;
	uint64_t before;
	int error;

	/* The framebuffer's removal fails. */
	boot(0, 1, 1, 0U);
	pixels = text_end_peek_pixels();
	stub_inject_unmap(1U, EIO);

	/* The end over the framebuffer: EIO, the window still taken and removed. */
	range = range_of(STUB_FB_PHYSICAL, STUB_FB_BYTES);
	first = stub_call_count;
	error = kern_text_end(&range, 1U);
	expect(error == EIO, "unmap-fail: the removal's error");
	expect(stub_call_count == first + 3U && stub_calls[first].result == EIO, "unmap-fail: the failed removal, then the window");
	expect_handoff_given_back(first + 1U, "unmap-fail");

	/* The console draws nothing through the mapping that stays. */
	before = framebuffer_sum(pixels);
	kern_text_putc('X');
	kern_text_reveal_fatal();
	expect(framebuffer_sum(pixels) == before, "unmap-fail: nothing drawn through the mapping left");

	/* Again: the same, with no call; apart and without ranges: 0. */
	first = stub_call_count;
	error = kern_text_end(&range, 1U);
	expect(error == EIO && stub_call_count == first, "unmap-fail: again EIO, no unmap again");
	range = range_of(APART_PHYSICAL, APART_BYTES);
	error = kern_text_end(&range, 1U);
	expect(error == 0 && stub_call_count == first, "unmap-fail: 0 for ranges apart");
	error = kern_text_end(NULL, 0U);
	expect(error == 0, "unmap-fail: 0 without ranges");
}

/* The same failure, first judged against ranges apart. */
static void
scene_unmap_fail_apart(void)
{
	struct kern_text_range range;
	unsigned first;
	int error;

	/* The framebuffer's removal fails. */
	boot(0, 1, 1, 0U);
	stub_inject_unmap(1U, EIO);

	/* Apart first: 0; then over the framebuffer: EIO, no call. */
	range = range_of(APART_PHYSICAL, APART_BYTES);
	error = kern_text_end(&range, 1U);
	expect(error == 0 && stub_count(1) == 2U, "unmap-fail-apart: 0 for ranges apart");
	first = stub_call_count;
	range = range_of(STUB_FB_PHYSICAL, STUB_FB_BYTES);
	error = kern_text_end(&range, 1U);
	expect(error == EIO && stub_call_count == first, "unmap-fail-apart: EIO over the framebuffer, no call");
}

/* The window over a fixed window: taken, its removal refused; EBUSY in the ranges, 0 apart. */
static void
scene_window_busy(void)
{
	struct kern_text_range range;
	unsigned first;
	int error;

	/* The framebuffer inside a window the hardware layer keeps. */
	stub_window_fixed = 1;
	boot(0, 1, 1, 0U);

	/* Over the framebuffer: EBUSY, the window stays given. */
	range = range_of(STUB_FB_PHYSICAL, STUB_FB_BYTES);
	first = stub_call_count;
	error = kern_text_end(&range, 1U);
	expect(error == EBUSY && stub_window_state == STUB_WINDOW_GIVEN, "window-busy: EBUSY, the window stays");
	expect(stub_call_count == first + 3U && stub_calls[first + 2U].result == EBUSY, "window-busy: the window's removal refused");

	/* Again: EBUSY with no call; apart: 0. */
	first = stub_call_count;
	error = kern_text_end(&range, 1U);
	expect(error == EBUSY && stub_call_count == first, "window-busy: again EBUSY, no call");
	range = range_of(APART_PHYSICAL, APART_BYTES);
	error = kern_text_end(&range, 1U);
	expect(error == 0, "window-busy: 0 for ranges apart");
}

/* The window's taking refused (EBUSY): it is left, judged as such. */
static void
scene_window_map_busy(void)
{
	struct kern_text_range range;
	unsigned first;
	int error;

	/* The second map (the end's taking of the window) refused. */
	boot(0, 1, 1, 0U);
	stub_inject_map(2U, EBUSY);

	/* Over the framebuffer: EBUSY; the framebuffer's own mapping removed. */
	range = range_of(STUB_FB_PHYSICAL, STUB_FB_BYTES);
	first = stub_call_count;
	error = kern_text_end(&range, 1U);
	expect(error == EBUSY && stub_window_state == STUB_WINDOW_HELD, "window-map-busy: EBUSY, the window held");
	expect(stub_call_count == first + 2U && stub_calls[first].unmap && stub_calls[first].result == 0 &&
	       !stub_calls[first + 1U].unmap && stub_calls[first + 1U].result == EBUSY,
	       "window-map-busy: the removal, then the refused taking");

	/* Again: EBUSY with no call; apart: 0. */
	first = stub_call_count;
	error = kern_text_end(&range, 1U);
	expect(error == EBUSY && stub_call_count == first, "window-map-busy: again EBUSY, no call");
	range = range_of(APART_PHYSICAL, APART_BYTES);
	error = kern_text_end(&range, 1U);
	expect(error == 0, "window-map-busy: 0 for ranges apart");
}

/* No framebuffer handed over: VGA text memory, which stays mapped; EBUSY over it, 0 apart. */
static void
scene_vga(void)
{
	struct kern_text_range range;
	uint64_t before;
	unsigned writes;
	unsigned first;
	int error;

	/* The boot without a handoff: the legacy aperture, twice. */
	boot(0, 0, 1, 0U);
	expect(stub_count(0) == 2U && stub_find(0, 1U)->kind == STUB_KIND_FIXED && stub_find(0, 2U)->kind == STUB_KIND_FIXED,
	       "vga: the legacy aperture mapped by the backend and the text layer");
	before = stub_sum(stub_legacy + LEGACY_TEXT_OFFSET, LEGACY_TEXT_BYTES);
	kern_text_putc('A');
	expect(stub_sum(stub_legacy + LEGACY_TEXT_OFFSET, LEGACY_TEXT_BYTES) != before, "vga: a character reaches text memory");

	/* Over text memory: EBUSY; no map in the end. */
	range = range_of(LEGACY_TEXT_PHYSICAL, LEGACY_TEXT_BYTES);
	first = stub_call_count;
	error = kern_text_end(&range, 1U);
	expect(error == EBUSY, "vga: EBUSY over text memory");
	expect(stub_call_count == first + 1U && stub_calls[first].unmap && stub_calls[first].kind == STUB_KIND_FIXED,
	       "vga: only the backend's aperture let go, nothing taken");

	/* Nothing reaches text memory or the cursor registers afterwards. */
	before = stub_sum(stub_legacy, (size_t)STUB_LEGACY_BYTES);
	writes = stub_io_writes;
	kern_text_putc('B');
	kern_text_update_cursor();
	kern_text_resume();
	kern_text_reveal_fatal();
	expect(stub_sum(stub_legacy, (size_t)STUB_LEGACY_BYTES) == before && stub_io_writes == writes, "vga: nothing drawn after the end");

	/* Again: EBUSY, no call; apart: 0. */
	first = stub_call_count;
	error = kern_text_end(&range, 1U);
	expect(error == EBUSY && stub_call_count == first, "vga: again EBUSY, no call");
	range = range_of(APART_PHYSICAL, APART_BYTES);
	error = kern_text_end(&range, 1U);
	expect(error == 0, "vga: 0 for ranges apart");
}

/* The same, first judged against ranges apart. */
static void
scene_vga_apart(void)
{
	struct kern_text_range range;
	int error;

	/* The boot without a handoff. */
	boot(0, 0, 1, 0U);

	/* Apart first: 0; then over the aperture: EBUSY. */
	range = range_of(APART_PHYSICAL, APART_BYTES);
	error = kern_text_end(&range, 1U);
	expect(error == 0, "vga-apart: 0 for ranges apart");
	range = range_of(STUB_LEGACY_PHYSICAL, STUB_LEGACY_BYTES);
	error = kern_text_end(&range, 1U);
	expect(error == EBUSY, "vga-apart: EBUSY over the aperture");
}

/* The framebuffer's BAR moved: the new and the old mapping, then the window, all removed. */
static void
scene_moved(void)
{
	struct kern_text_range range;
	const volatile uint32_t *old_pixels;
	const volatile uint32_t *new_pixels;
	unsigned first;
	uint64_t before;
	int error;

	/* The boot, then the move. */
	boot(0, 1, 1, 0U);
	old_pixels = text_end_peek_pixels();
	drv_pcat_graphics_framebuffer_moved(BAR_OLD_PHYSICAL, BAR_NEW_PHYSICAL, BAR_BYTES);
	new_pixels = text_end_peek_pixels();
	expect(new_pixels != old_pixels && stub_find(0, 2U)->physical == BAR_NEW_PHYSICAL + (STUB_FB_PHYSICAL - BAR_OLD_PHYSICAL),
	       "moved: the new place mapped and drawn through");
	before = framebuffer_sum(new_pixels);
	kern_text_putc('A');
	expect(framebuffer_sum(new_pixels) != before, "moved: a character reaches the new place");

	/* The end over the new BAR: 0; the new, the old, then the window. */
	range = range_of(BAR_NEW_PHYSICAL, BAR_BYTES);
	first = stub_call_count;
	error = kern_text_end(&range, 1U);
	expect(error == 0, "moved: 0");
	expect(stub_call_count == first + 4U &&
	       stub_calls[first].unmap && stub_calls[first].mapped == (void *)(uintptr_t)new_pixels && stub_calls[first].result == 0 &&
	       stub_calls[first + 1U].unmap && stub_calls[first + 1U].mapped == (void *)(uintptr_t)old_pixels && stub_calls[first + 1U].result == 0,
	       "moved: the new and the old mapping removed");
	expect_handoff_given_back(first + 2U, "moved");

	/* The old BAR too: 0. */
	range = range_of(BAR_OLD_PHYSICAL, BAR_BYTES);
	error = kern_text_end(&range, 1U);
	expect(error == 0 && stub_bad_unmaps == 0U, "moved: 0 over the old BAR");
}

/* The move could not map the new place: the old mapping and the window removed, 0. */
static void
scene_moved_fail(void)
{
	struct kern_text_range range;
	const volatile uint32_t *pixels;
	unsigned first;
	int error;

	/* The boot, then a move whose mapping fails. */
	boot(0, 1, 1, 0U);
	pixels = text_end_peek_pixels();
	stub_inject_map(2U, ENOMEM);
	drv_pcat_graphics_framebuffer_moved(BAR_OLD_PHYSICAL, BAR_NEW_PHYSICAL, BAR_BYTES);
	expect(text_end_peek_pixels() == pixels, "moved-fail: the console stays on the old mapping");

	/* The end over the new BAR: 0; the old, then the window. */
	range = range_of(BAR_NEW_PHYSICAL, BAR_BYTES);
	first = stub_call_count;
	error = kern_text_end(&range, 1U);
	expect(error == 0, "moved-fail: 0");
	expect(stub_call_count == first + 3U && stub_calls[first].unmap && stub_calls[first].mapped == (void *)(uintptr_t)pixels &&
	       stub_calls[first].result == 0, "moved-fail: the old mapping removed");
	expect_handoff_given_back(first + 1U, "moved-fail");
}

/* The GPU driver's ranges first, then the core's call without ranges: each judged on its own. */
static void
scene_order_bars_first(void)
{
	struct kern_text_range range;
	unsigned first;
	int error;

	/* What stays (the window over a fixed window) lies in the driver's ranges. */
	stub_window_fixed = 1;
	boot(0, 1, 1, 0U);

	/* The driver: EBUSY; the core: 0, no call. */
	range = range_of(STUB_FB_PHYSICAL, STUB_FB_BYTES);
	error = kern_text_end(&range, 1U);
	expect(error == EBUSY, "order-bars-first: the driver's ranges EBUSY");
	first = stub_call_count;
	error = kern_text_end(NULL, 0U);
	expect(error == 0 && stub_call_count == first, "order-bars-first: the core 0, no call");
}

/* The core's call without ranges first, then the GPU driver's ranges, judged against what the first left. */
static void
scene_order_null_first(void)
{
	struct kern_text_range range;
	unsigned first;
	int error;

	/* What stays (the window over a fixed window) lies in the driver's ranges. */
	stub_window_fixed = 1;
	boot(0, 1, 1, 0U);

	/* The core: 0 with the removals; the driver: EBUSY, no call. */
	error = kern_text_end(NULL, 0U);
	expect(error == 0 && stub_count(1) == 2U, "order-null-first: the core 0, the removals made");
	first = stub_call_count;
	range = range_of(STUB_FB_PHYSICAL, STUB_FB_BYTES);
	error = kern_text_end(&range, 1U);
	expect(error == EBUSY && stub_call_count == first, "order-null-first: the driver's ranges EBUSY, no call");
}

/*
 * Two at once: the core's call takes /dev/graphics' lock and is removing
 * when the GPU driver's call comes; the second waits for the lock and is
 * judged against what the first left (EBUSY: the window over a fixed
 * window), never answering before the removals are done.
 */
static void
scene_concurrent(void)
{
	unsigned index;
	int error;

	/* What stays lies in the second caller's ranges. */
	stub_window_fixed = 1;
	boot(0, 1, 1, 0U);

	/* The first caller; the second starts inside its first removal. */
	stub_unmap_hook = concurrent_unmap;
	error = kern_text_end(NULL, 0U);
	expect(error == 0, "concurrent: the first caller's 0");

	/* The second caller's answer. */
	text_end_host_thread_join();
	expect(stub_mutex_waits >= 1U, "concurrent: the second caller waited for the lock");
	expect(stub_acquire_sequence[1] > stub_unlock_sequence[0], "concurrent: the second took the lock after the first let it go");
	expect(second_sequence > stub_unlock_sequence[0], "concurrent: the second answered after the first's removals");
	expect(second_result == EBUSY, "concurrent: the second judged against what the first left");

	/* Every removal by the first caller. */
	for (index = 0U; index < stub_call_count; index++) {
		if (stub_calls[index].cpu != 0U)
			break;
	}
	expect(index == stub_call_count && stub_count(1) == 2U, "concurrent: the removals made once, by the first caller");
}

/* Inside the first caller's first removal: starts the second caller and waits until it waits for the lock. */
static void
concurrent_unmap(void)
{
	unsigned tries;

	/* The second caller, on its own thread. */
	text_end_host_thread_start(concurrent_second);

	/* Until it waits on /dev/graphics' lock (five seconds at most). */
	for (tries = 0U; tries < 5000U; tries++) {
		if (__atomic_load_n(&stub_mutex_waits, __ATOMIC_SEQ_CST) != 0U)
			break;
		text_end_host_pause(1000000L);
	}

	/* A little longer, so a second caller that did not wait would have answered. */
	text_end_host_pause(20000000L);
}

/* The second caller: the GPU driver's ranges over the framebuffer. */
static void *
concurrent_second(
	void *argument)
{
	struct kern_text_range range;

	/* Another processor. */
	(void)argument;
	stub_cpu = 1U;
	range = range_of(STUB_FB_PHYSICAL, STUB_FB_BYTES);
	second_result = kern_text_end(&range, 1U);
	second_sequence = stub_sequence_next();
	return NULL;
}

/* The console's own end after the configured wait, over the handed-over framebuffer. */
static void
scene_trigger(void)
{
	unsigned first;

	/* The boot asks for the thread; nothing has ended yet. */
	boot(0, 1, 1, 0U);
	expect(stub_thread_count == 1U && !text_end_peek_test_done() && !text_end_peek_ended(), "trigger: the thread asked for, nothing ended");

	/* The thread: the wait, then the end. */
	first = stub_call_count;
	stub_thread_entry[0](stub_thread_argument[0]);
#ifdef KERN_TEST_TEXT_END_SECONDS
	expect(stub_last_sleep == kern_ms_to_ticks((uint64_t)KERN_TEST_TEXT_END_SECONDS * 1000U), "trigger: the wait is the configured seconds");
#else
	stub_fail("trigger: needs KERN_TEST_TEXT_END_SECONDS");
#endif
	expect(text_end_peek_test_done() == 1 && text_end_peek_test_result() == 0 && text_end_peek_ended(), "trigger: done, 0, ended");
	expect(stub_call_count == first + 3U, "trigger: the mapping and the window removed");
	expect_handoff_given_back(first + 1U, "trigger");
}

/* The console's own end without a framebuffer: no ranges, 0. */
static void
scene_trigger_none(void)
{
	/* VGA text memory only. */
	boot(0, 0, 1, 0U);

	/* The thread: the end without ranges. */
	stub_thread_entry[0](stub_thread_argument[0]);
	expect(text_end_peek_test_done() == 1 && text_end_peek_test_result() == 0 && text_end_peek_ended(), "trigger-none: done, 0, ended");
}

/* Counts one check and reports a failure. */
static void
expect(
	int condition,
	const char *what)
{
	/* One more check. */
	checks++;
	if (!condition)
		stub_fail(what);
}

/*
 * Boots the console: the hardware, a quiet boot's splash on the early
 * console window, the backend's preparation (its first map failing when
 * asked) and /dev/graphics when asked.
 */
static void
boot(
	int quiet,
	int handoff,
	int register_device,
	unsigned fail_first_map)
{
	/* The hardware as the scene wants it. */
	stub_quiet = quiet;
	stub_handoff = handoff;
	stub_setup();
	if (fail_first_map)
		stub_inject_map(1U, ENOMEM);

	/* A quiet boot's splash, drawn by the hardware layer's early console. */
	if (quiet)
		drv_pcat_splash_start(stub_window, STUB_FB_WIDTH, STUB_FB_HEIGHT, STUB_FB_PITCH, 0);

	/* The backend and the text layer. */
	if (!drv_pcat_graphics_prepare())
		stub_fail("boot: the backend not prepared");

	/* /dev/graphics, as the file system's set-up registers it. */
	if (register_device && drv_graphics_device_register() != 0)
		stub_fail("boot: /dev/graphics not registered");
}

/* Sums a framebuffer's bytes. */
static uint64_t
framebuffer_sum(
	const volatile uint32_t *pixels)
{
	/* The whole framebuffer. */
	return stub_sum(pixels, (size_t)STUB_FB_BYTES);
}

/* Sums the console's snapshot, which follows its cells. */
static uint64_t
snapshot_sum(void)
{
	struct kern_text_snapshot snapshot;
	int error;

	/* The snapshot into RAM. */
	memset(snapshot_pixels, 0, sizeof(snapshot_pixels));
	snapshot.pixels = snapshot_pixels;
	snapshot.bytes = sizeof(snapshot_pixels);
	error = kern_text_snapshot(&snapshot);
	if (error != 0)
		stub_fail("a snapshot failed");
	return stub_sum(snapshot_pixels, sizeof(snapshot_pixels));
}

/* Makes one range. */
static struct kern_text_range
range_of(
	uint64_t physical,
	uint64_t bytes)
{
	struct kern_text_range range;

	/* The range. */
	range.physical = physical;
	range.bytes = bytes;
	return range;
}

/* Gives one of the two files. */
static struct file *
file_of(
	unsigned index)
{
	/* The storage, as the device sees it. */
	return (struct file *)(void *)file_storage[index];
}

/* Enters /dev/graphics' mode through the file. */
static int
graphics_enter(
	struct file *file)
{
	struct graphics_mode mode;

	/* Any mode the backend has. */
	memset(&mode, 0, sizeof(mode));
	return stub_graphics_ops->ioctl(file, KERN_GRAPHICS_ENTER, (uintptr_t)&mode);
}

/* Fills a white rectangle through the file. */
static int
graphics_fill(
	struct file *file)
{
	struct graphics_fill fill;

	/* Ten by ten at the corner. */
	memset(&fill, 0, sizeof(fill));
	fill.rect.width = 10U;
	fill.rect.height = 10U;
	fill.color = 0xffffffU;
	return stub_graphics_ops->ioctl(file, KERN_GRAPHICS_FILL_RECT, (uintptr_t)&fill);
}

/* Checks that the calls from first are the window's taking for exactly the framebuffer and its removal. */
static void
expect_handoff_given_back(
	unsigned first,
	const char *what)
{
	const struct stub_call *taking;
	const struct stub_call *removal;
	char name[128];

	/* The taking: exactly the handoff, given the window. */
	taking = &stub_calls[first];
	removal = &stub_calls[first + 1U];
	snprintf(name, sizeof(name), "%s: the window taken for exactly the framebuffer", what);
	expect(first + 1U < stub_call_count && !taking->unmap && taking->physical == STUB_FB_PHYSICAL &&
	       taking->bytes == (size_t)STUB_FB_BYTES && taking->kind == STUB_KIND_WINDOW && taking->result == 0, name);

	/* The removal: the window, at once. */
	snprintf(name, sizeof(name), "%s: the window removed at once", what);
	expect(removal->unmap && removal->mapped == (void *)stub_window && removal->bytes == (size_t)STUB_FB_BYTES &&
	       removal->result == 0 && stub_window_state == STUB_WINDOW_REMOVED, name);
}
