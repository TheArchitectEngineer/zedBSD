/*
 * Host test of the sun4u console's cursor (WS275 p004, T1-745).
 *
 * Compiles src/drivers/platform/sun4u/sun4u-console.c against a fake
 * firmware framebuffer of 640 by 400 pixels (an array of 0xRRGGBB values)
 * and fakes of the kernel's names.  Writes text, carriage returns,
 * newlines, a backspace and a tab, and enough lines to scroll, and after
 * each step checks that the cursor (the two white bottom lines of a cell)
 * is drawn in exactly one cell, the cursor's.
 *
 * Usage: host-console-cursor   (prints one line a check, then PASS or FAIL)
 */

#include "src/drivers/platform/sun4u/sun4u-console.c"

#include <stdio.h>

#define TEST_WIDTH	640U
#define TEST_HEIGHT	400U

static uint32_t pixels[TEST_HEIGHT][TEST_WIDTH];
static int failures;
void (*kernel_putc)(int character);

int drv_sun4u_fb_init(void) { return 0; }
void
drv_sun4u_fb_get_size(unsigned *width, unsigned *height)
{
	*width = TEST_WIDTH;
	*height = TEST_HEIGHT;
}
void
drv_sun4u_fb_fill(unsigned x, unsigned y, unsigned width, unsigned height, uint32_t color)
{
	unsigned i;
	unsigned j;

	for (j = y; j < y + height && j < TEST_HEIGHT; j++)
		for (i = x; i < x + width && i < TEST_WIDTH; i++)
			pixels[j][i] = color;
}
void
drv_sun4u_fb_glyph(unsigned x, unsigned y, const uint8_t *glyph, uint32_t foreground, uint32_t background)
{
	unsigned line;
	unsigned bit;

	for (line = 0; line < 16U; line++)
		for (bit = 0; bit < 8U; bit++)
			pixels[y + line][x + bit] = (glyph[line] & (0x80U >> bit)) ? foreground : background;
}
void kern_text_register(const struct kern_text_ops *ops) { (void)ops; }
void *hal_get_arch_handoff(const char *name) { (void)name; return NULL; }
bool hal_irq_disable(void) { return false; }
void hal_irq_enable(void) { }
void hal_putc(int c) { (void)c; }
int kthread_create(void (*entry)(void *), void *arg, int priority, struct thread **result)
{ (void)entry; (void)arg; (void)priority; (void)result; return 0; }
void thread_start(struct thread *thread) { (void)thread; }
void tty_console_input_byte(uint8_t byte) { (void)byte; }
uint64_t sched_ticks(void) { return 0; }
void sched_sleep(uint64_t timeout_tick) { (void)timeout_tick; }
uint8_t kern_io_in8(uint16_t port) { (void)port; return 0; }
void kern_io_out8(uint16_t port, uint8_t value) { (void)port; (void)value; }

/* Reports whether the cell's two bottom lines are all the cursor's white. */
static int
has_cursor(unsigned row, unsigned column)
{
	unsigned line;
	unsigned bit;

	for (line = 14; line < 16U; line++)
		for (bit = 0; bit < 8U; bit++)
			if (pixels[row * 16U + line][column * 8U + bit] != SUN4U_CONSOLE_CURSOR)
				return 0;
	return 1;
}

/* Checks the cursor is drawn at (row, column) and nowhere else. */
static void
only_at(unsigned row, unsigned column, const char *what)
{
	unsigned r;
	unsigned c;
	unsigned count;
	int ok;

	count = 0;
	for (r = 0; r < screen_rows; r++)
		for (c = 0; c < screen_columns; c++)
			count += (unsigned)has_cursor(r, c);
	ok = count == 1 && has_cursor(row, column) && screen_row == row && screen_column == column;
	printf("%s: %s (cells with cursor %u)\n", what, ok ? "ok" : "FAIL", count);
	if (!ok)
		failures++;
}

static void
put(const char *text)
{
	while (*text != '\0')
		console_kernel_putc(*text++);
}

int
main(void)
{
	unsigned i;

	drv_sun4u_console_init();
	only_at(0, 0, "start");
	put("abc");
	only_at(0, 3, "text");
	put("\r\n");
	only_at(1, 0, "cr lf after text");
	put("login: ");
	put("\n");
	only_at(2, 0, "lf after text");
	put("\r");
	only_at(2, 0, "cr at column 0");
	put("x\b");
	only_at(2, 0, "backspace");
	put("y\t");
	only_at(2, 8, "tab");
	put("\r\r\n\n");
	only_at(4, 0, "cr cr lf lf");
	for (i = 0; i < 40U; i++)
		put("root@kei:~# echo ws275-ok\r\n");
	only_at(24, 0, "after scrolling");
	put("ws275-ok");
	only_at(24, 8, "text on the last row");
	for (i = 0; i < 24U; i++) {
		if (has_cursor(i, 0))
			break;
	}
	printf("no stale column-0 cursor: %s\n", i == 24U ? "ok" : "FAIL");
	if (i != 24U)
		failures++;
	console_resume();
	only_at(24, 8, "repaint");

	printf("%s\n", failures == 0 ? "PASS" : "FAIL");
	return failures == 0 ? 0 : 1;
}
