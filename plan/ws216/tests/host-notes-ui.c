/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws216-p006: Notes' toolbar on the host -- ui.c, linked as it is with
 * libtruetype and a small stand-in for libkeiland's theme -- checked
 * against the acceptance of plan/ws216/phase006/phase.md section 8:
 *
 *   U2  the drawing tool button in place of the pen's and the marker's,
 *       the strip of six drawing tools under the card, in the window (at
 *       1280 and 1080 pixels), the buttons after the drawing button in
 *       place whatever its name, no widths for the fill, the notice held
 *       back by the strip, the buttons' count and the overflow's log
 *   U3  each tool's colours and widths
 *
 *   host-notes-ui FONT LOG   (the NOTES lines go to LOG)
 */

#include "app.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The windows' widths tried, and the toolbar picture's rows. */
#define TEST_WIDE		1280U
#define TEST_NARROW		1080U
#define TEST_HEIGHT		NOTES_TOOLBAR_IMAGE_HEIGHT

/* The theme libkeiland would give: Kei's blue as the accent. */
static struct kl_theme theme;

/* The checks run, and those that failed. */
static unsigned checks;
static unsigned failures;

/* The log the NOTES lines go to. */
static const char *log_path;

static void check(int passed, const char *format, ...);
static void draw(struct notes_ui *ui, uint32_t width, const struct notes_ui_state *state);
static const struct notes_button *find(const struct notes_ui *ui, uint32_t action);
static size_t log_count(const char *needle);
static void test_bar(struct notes_ui *ui, uint32_t width);
static void test_tables(void);
static void test_overflow(struct notes_ui *ui);
static void test_presses(void);
static int same_places(const struct notes_ui *ui, const struct notes_button *places, unsigned count);
static int transparent(uint32_t width, int32_t x, int32_t y);

/* The theme, as libkeiland gives it to Notes (the stand-in). */
const struct kl_theme *
kl_theme_default(void)
{
	theme.accent = 0xff2f6fdbU;
	theme.accent_text = 0xff2f6fdbU;
	return &theme;
}

/* The light appearance's colour (the stand-in). */
kl_color
kl_theme_choose(
	kl_color light,
	kl_color dark)
{
	(void)dark;
	return light;
}

int
main(
	int argc,
	char **argv)
{
	struct notes_ui ui;
	int error;

	if (argc != 3) {
		fprintf(stderr, "usage: host-notes-ui FONT LOG\n");
		return 2;
	}
	log_path = argv[2];
	if (freopen(log_path, "w", stdout) == NULL)
		return 2;

	/* With the font, and without it (every button 44 pixels). */
	error = notes_ui_open(&ui, argv[1]);
	check(error == 0 && ui.face != NULL, "the font opens (%d): the labels have their widths", error);
	test_bar(&ui, TEST_WIDE);
	test_bar(&ui, TEST_NARROW);
	test_overflow(&ui);
	notes_ui_close(&ui);
	memset(&ui, 0, sizeof(ui));
	test_bar(&ui, TEST_WIDE);
	test_tables();
	test_presses();

	fprintf(stderr, "host-notes-ui: %u checks, %u failed\n", checks, failures);
	if (failures != 0U)
		return 1;
	return 0;
}

/* Counts a check, and reports it when it fails. */
static void
check(
	int passed,
	const char *format,
	...)
{
	va_list arguments;

	checks++;
	if (passed)
		return;
	failures++;
	fprintf(stderr, "FAIL: ");
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
	fprintf(stderr, "\n");
}

/* The toolbar's picture, the widest window's. */
static unsigned char pixels[4096U * 4U * TEST_HEIGHT];

/* Draws the toolbar into a picture of a width. */
static void
draw(
	struct notes_ui *ui,
	uint32_t width,
	const struct notes_ui_state *state)
{
	notes_ui_draw(ui, pixels, (size_t)width * 4U, width, TEST_HEIGHT, state);
}

/* Tells whether a pixel of the last picture drawn is transparent. */
static int
transparent(
	uint32_t width,
	int32_t x,
	int32_t y)
{
	return pixels[((size_t)y * width + (size_t)x) * 4U + 3U] == 0U;
}

/* Tells whether every button of a list (but the widths) is where it was. */
static int
same_places(
	const struct notes_ui *ui,
	const struct notes_button *places,
	unsigned count)
{
	const struct notes_button *button;
	unsigned index;

	for (index = 0; index < count; index++) {
		if (places[index].action >= NOTES_ACTION_WIDTH && places[index].action < NOTES_ACTION_WIDTH + NOTES_WIDTHS)
			continue;
		button = find(ui, places[index].action);
		if (button == NULL || button->x != places[index].x || button->width != places[index].width)
			return 0;
	}
	return 1;
}

/* Finds a button by its action. */
static const struct notes_button *
find(
	const struct notes_ui *ui,
	uint32_t action)
{
	unsigned index;

	for (index = 0; index < ui->button_count; index++) {
		if (ui->buttons[index].action == action)
			return &ui->buttons[index];
	}
	return NULL;
}

/* Counts the log lines that hold a text. */
static size_t
log_count(
	const char *needle)
{
	char line[1024];
	FILE *file;
	size_t count;

	fflush(stdout);
	file = fopen(log_path, "r");
	if (file == NULL)
		return 0;
	count = 0;
	while (fgets(line, sizeof(line), file) != NULL) {
		if (strstr(line, needle) != NULL)
			count++;
	}
	fclose(file);
	return count;
}

/* U2: the drawing tool button, the strip, the widths, the notice, at a window's width. */
static void
test_bar(
	struct notes_ui *ui,
	uint32_t width)
{
	static const unsigned tools[6] = {
		NOTES_ACTION_PEN, NOTES_ACTION_BRUSH, NOTES_ACTION_PENCIL, NOTES_ACTION_WATERCOLOR, NOTES_ACTION_HIGHLIGHTER, NOTES_ACTION_FILL
	};
	struct notes_ui_state state;
	struct notes_button places[NOTES_BUTTONS];
	unsigned place_count;
	const struct notes_button *button;
	const struct notes_button *eraser;
	int32_t eraser_x;
	unsigned index;
	unsigned tool;
	int good;

	/* The pen chosen, the strip closed: the drawing button, no pen's nor marker's. */
	memset(&state, 0, sizeof(state));
	state.tool = NOTES_ACTION_PEN;
	state.draw_tool = NOTES_ACTION_PEN;
	state.width = 1U;
	state.page_count = 1U;
	draw(ui, width, &state);
	check(find(ui, NOTES_ACTION_DRAW) != NULL, "%u: the drawing button", width);
	check(find(ui, NOTES_ACTION_PEN) == NULL && find(ui, NOTES_ACTION_HIGHLIGHTER) == NULL, "%u: no pen's nor marker's button on the card", width);
	check(!ui->strip_shown, "%u: no strip while closed", width);
	check(ui->button_count <= NOTES_BUTTONS, "%u: %u buttons", width, ui->button_count);
	eraser = find(ui, NOTES_ACTION_ERASER);
	eraser_x = -1;
	if (eraser != NULL && find(ui, NOTES_ACTION_DRAW) != NULL)
		eraser_x = eraser->x - find(ui, NOTES_ACTION_DRAW)->x;

	/* Each drawing tool: every button stays in place (review-1 C-4); the fill has no widths. */
	memcpy(places, ui->buttons, ui->button_count * sizeof(places[0]));
	place_count = ui->button_count;
	good = 1;
	for (tool = 0; tool < 6U; tool++) {
		state.tool = tools[tool];
		state.draw_tool = tools[tool];
		draw(ui, width, &state);
		eraser = find(ui, NOTES_ACTION_ERASER);
		button = find(ui, NOTES_ACTION_DRAW);
		if (eraser == NULL || button == NULL || eraser->x - button->x != eraser_x)
			good = 0;
		if ((find(ui, NOTES_ACTION_WIDTH) == NULL) != (tools[tool] == NOTES_ACTION_FILL))
			good = 0;
		if (!same_places(ui, places, place_count))
			good = 0;
	}
	check(good, "%u: the eraser stays %d after the drawing button whatever the drawing tool, and only the fill has no widths", width, eraser_x);

	/* Another tool: the drawing button is still there (back to the tool used last). */
	state.tool = NOTES_ACTION_ERASER;
	state.draw_tool = NOTES_ACTION_PENCIL;
	draw(ui, width, &state);
	check(find(ui, NOTES_ACTION_DRAW) != NULL && find(ui, NOTES_ACTION_WIDTH) != NULL, "%u: with the eraser, the drawing button and the pencil's widths", width);

	/* The strip open: the six tools under the card, in the window. */
	state.tool = NOTES_ACTION_BRUSH;
	state.draw_tool = NOTES_ACTION_BRUSH;
	state.draws_open = 1;
	draw(ui, width, &state);
	check(ui->strip_shown && ui->strip_y >= (int32_t)NOTES_TOOLBAR_HEIGHT && ui->strip_y + ui->strip_height <= (int32_t)TEST_HEIGHT,
	      "%u: the strip under the toolbar's band (%d..%d)", width, ui->strip_y, ui->strip_y + ui->strip_height);
	check(ui->strip_x >= 8 && ui->strip_x + ui->strip_width <= (int32_t)width - 8, "%u: the strip in the window (%d..%d)", width, ui->strip_x,
	      ui->strip_x + ui->strip_width);
	good = 1;
	for (index = 0; index < 6U; index++) {
		button = find(ui, tools[index]);
		if (button == NULL ||
		    !notes_ui_in_strip(ui, (float)button->x + 1.0f, (float)button->y + 1.0f) ||
		    notes_ui_hit(ui, (float)button->x + (float)button->width / 2.0f, (float)button->y + (float)button->height / 2.0f) != tools[index])
			good = 0;
		if (index > 0U && button != NULL && find(ui, tools[index - 1U]) != NULL && button->x <= find(ui, tools[index - 1U])->x)
			good = 0;
	}
	check(good, "%u: the six tools on the strip, in order, each hit where it is", width);
	check(!notes_ui_in_strip(ui, 4.0f, 90.0f) && !notes_ui_in_strip(ui, (float)ui->strip_x, 20.0f), "%u: off the strip", width);
	check(ui->button_count <= NOTES_BUTTONS, "%u: %u buttons with the strip", width, ui->button_count);

	/*
	 * A status too long to stand beside the card is a notice under it,
	 * centred; while the strip is open it waits (review-1 L11): at the
	 * window's middle, where the notice's pill would be, the picture stays
	 * clear when the strip is elsewhere.
	 */
	state.status = "This notice is too long to stand beside the card";
	state.draws_open = 0;
	draw(ui, width, &state);
	if (ui->face != NULL)
		check(!transparent(width, (int32_t)width / 2, 90), "%u: the notice under the card without the strip", width);
	state.draws_open = 1;
	draw(ui, width, &state);
	check(ui->strip_shown, "%u: the strip with a long status", width);
	if (ui->face != NULL && (ui->strip_x + ui->strip_width < (int32_t)width / 2 - 4 || ui->strip_x > (int32_t)width / 2 + 4))
		check(transparent(width, (int32_t)width / 2, 90), "%u: no notice while the strip is open", width);

	/* A short status beside the card shows with the strip open. */
	state.status = "Saved";
	draw(ui, width, &state);
	check(ui->strip_shown, "%u: the strip with a short status", width);
	state.draws_open = 0;
	state.status = NULL;
}

/* U3: each tool's colours and widths. */
static void
test_tables(void)
{
	check(notes_ui_width(NOTES_TOOL_PEN, 0) == 1.5f && notes_ui_width(NOTES_TOOL_PEN, 2) == 6.0f, "the pen's widths");
	check(notes_ui_width(NOTES_TOOL_HIGHLIGHTER, 0) == 10.0f && notes_ui_width(NOTES_TOOL_HIGHLIGHTER, 2) == 24.0f, "the marker's widths");
	check(notes_ui_width(NOTES_TOOL_BRUSH, 0) == 3.0f && notes_ui_width(NOTES_TOOL_BRUSH, 1) == 6.0f && notes_ui_width(NOTES_TOOL_BRUSH, 2) == 12.0f,
	      "the brush's widths");
	check(notes_ui_width(NOTES_TOOL_PENCIL, 0) == 0.8f && notes_ui_width(NOTES_TOOL_PENCIL, 1) == 1.4f && notes_ui_width(NOTES_TOOL_PENCIL, 2) == 2.4f,
	      "the pencil's widths");
	check(notes_ui_width(NOTES_TOOL_WATERCOLOR, 0) == 8.0f && notes_ui_width(NOTES_TOOL_WATERCOLOR, 1) == 16.0f &&
	      notes_ui_width(NOTES_TOOL_WATERCOLOR, 2) == 28.0f, "the watercolour's widths");
	check(notes_ui_color(NOTES_TOOL_BRUSH, 1) == 0x1d4ed8ffU && notes_ui_color(NOTES_TOOL_PENCIL, 2) == 0xdc2626ffU &&
	      notes_ui_color(NOTES_TOOL_WATERCOLOR, 3) == 0x15803dffU && notes_ui_color(NOTES_TOOL_FILL, 4) == 0xea580cffU,
	      "the new tools take the pen's opaque colours");
	check(notes_ui_color(NOTES_TOOL_HIGHLIGHTER, 0) == 0xfacc1566U, "the marker's translucent colours");
	check(notes_ui_tool(NOTES_ACTION_FILL) == NOTES_TOOL_FILL && notes_ui_tool(NOTES_ACTION_WATERCOLOR) == NOTES_TOOL_WATERCOLOR &&
	      notes_ui_tool(NOTES_ACTION_ERASER) == NOTES_UI_NOT_DRAWING && notes_ui_tool(NOTES_ACTION_DRAW) == NOTES_UI_NOT_DRAWING,
	      "the drawing tools' kinds");
}

/* review-1 C-6: what the presses do to the tools, the drawing tool used last and the strip. */
static void
test_presses(void)
{
	struct notes_tools tools;
	unsigned changes;

	/* The pen chosen: the drawing button opens the strip, again closes it. */
	tools.tool = NOTES_ACTION_PEN;
	tools.draw_tool = NOTES_ACTION_PEN;
	tools.draws_open = 0;
	changes = notes_ui_tools_press(&tools, NOTES_ACTION_DRAW);
	check(changes == NOTES_TOOLS_OPENED && tools.draws_open && tools.tool == NOTES_ACTION_PEN, "presses: the drawing button opens the strip");
	changes = notes_ui_tools_press(&tools, NOTES_ACTION_DRAW);
	check(changes == NOTES_TOOLS_CLOSED && !tools.draws_open, "presses: again it closes it");

	/* A tool of the strip: chosen, the one used last, the strip closed. */
	(void)notes_ui_tools_press(&tools, NOTES_ACTION_DRAW);
	changes = notes_ui_tools_press(&tools, NOTES_ACTION_PENCIL);
	check(changes == (NOTES_TOOLS_CHOSEN | NOTES_TOOLS_CLOSED) && tools.tool == NOTES_ACTION_PENCIL && tools.draw_tool == NOTES_ACTION_PENCIL &&
	      !tools.draws_open, "presses: the pencil from the strip");

	/* The eraser, then the drawing button: back to the pencil, no strip. */
	changes = notes_ui_tools_press(&tools, NOTES_ACTION_ERASER);
	check(changes == NOTES_TOOLS_CHOSEN && tools.tool == NOTES_ACTION_ERASER && tools.draw_tool == NOTES_ACTION_PENCIL, "presses: the eraser");
	changes = notes_ui_tools_press(&tools, NOTES_ACTION_DRAW);
	check(changes == NOTES_TOOLS_CHOSEN && tools.tool == NOTES_ACTION_PENCIL && !tools.draws_open, "presses: the drawing button goes back to the pencil");

	/* A colour with the eraser goes back to the drawing tool used last (L-1); without it, the tool stays. */
	(void)notes_ui_tools_press(&tools, NOTES_ACTION_ERASER);
	changes = notes_ui_tools_press(&tools, NOTES_ACTION_COLOR + 2U);
	check(changes == 0U && tools.tool == NOTES_ACTION_PENCIL, "presses: a colour with the eraser goes back to the pencil");
	(void)notes_ui_tools_press(&tools, NOTES_ACTION_SELECT);
	changes = notes_ui_tools_press(&tools, NOTES_ACTION_COLOR + 1U);
	check(changes == 0U && tools.tool == NOTES_ACTION_SELECT, "presses: a colour with Select leaves Select");

	/* Any other action (a page, undo, a key's tool) closes the strip. */
	(void)notes_ui_tools_press(&tools, NOTES_ACTION_DRAW);
	(void)notes_ui_tools_press(&tools, NOTES_ACTION_DRAW);
	check(tools.tool == NOTES_ACTION_PENCIL && tools.draws_open, "presses: the strip open again");
	changes = notes_ui_tools_press(&tools, NOTES_ACTION_NEXT_PAGE);
	check(changes == NOTES_TOOLS_CLOSED && !tools.draws_open && tools.tool == NOTES_ACTION_PENCIL, "presses: the next page closes the strip");
	(void)notes_ui_tools_press(&tools, NOTES_ACTION_DRAW);
	changes = notes_ui_tools_press(&tools, NOTES_ACTION_FILL);
	check(changes == (NOTES_TOOLS_CHOSEN | NOTES_TOOLS_CLOSED) && tools.draw_tool == NOTES_ACTION_FILL, "presses: the fill by its key");
	changes = notes_ui_tools_press(&tools, NOTES_ACTION_TEXT);
	check(changes == NOTES_TOOLS_CHOSEN && tools.draw_tool == NOTES_ACTION_FILL, "presses: Text keeps the fill as the drawing tool");
}

/* M6 of review-1: the buttons beyond the table are logged once. */
static void
test_overflow(
	struct notes_ui *ui)
{
	struct notes_ui_state state;

	/* The Select tool on a line of text, the strip of a narrow window: within the table. */
	memset(&state, 0, sizeof(state));
	state.tool = NOTES_ACTION_SELECT;
	state.draw_tool = NOTES_ACTION_PEN;
	state.page_count = 3U;
	state.page = 1U;
	state.can_undo = 1;
	state.can_redo = 1;
	state.text_selected = 1;
	state.selected = 1;
	state.can_edit_text = 1;
	state.can_reset = 1;
	state.text_size = 12.0f;
	draw(ui, TEST_WIDE, &state);
	check(ui->button_count <= NOTES_BUTTONS && log_count("NOTES BUTTONS overflow") == 0U, "the fullest toolbar fits (%u buttons)", ui->button_count);
}
