/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host tests of libkeiland's widgets (ws090-p005): a page of every widget is
 * drawn frame by frame, and the pointer, the keyboard and a finger are
 * given to it through kl_ui; the tests check what each widget reports,
 * where the keyboard's focus goes, and which keys stay the application's.
 * Two frames are written as PPM for a person to look at (the gallery).
 *
 *   host-widgets FONT FALLBACK OUTPUT-PREFIX
 */

#include <keiland/keiland.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The frame's size. */
#define PAGE_WIDTH	800
#define PAGE_HEIGHT	600

/* A second, in microseconds. */
#define SECOND		1000000U

/* The widgets' ids. */
#define ID_SAVE		1U
#define ID_SWITCH	2U
#define ID_SLIDER	3U
#define ID_FIELD	4U
#define ID_LIST		5U
#define ID_SIDEBAR	6U
#define ID_DISABLED	7U
#define ID_DIALOG	8U

/* The index a list gives itself, and a dialog itself. */
#define LIST_SELF	0xffffffffU
#define DIALOG_SELF	0xffffffffU

/* The evdev codes of the letters the tests type. */
#define KEY_A		30U
#define KEY_B		48U
#define KEY_C		46U
#define KEY_S		31U
#define KEY_X		45U

/* How many rows the list has. */
#define LIST_ROWS	100U

/*
 * What one frame of the page reported: the widgets' answers, and the keys
 * and other input no widget took.
 */
struct page_report {
	int saved;
	int disabled_pressed;
	int switched;
	int slid;
	unsigned field;
	unsigned list;
	int place;
	int answer;
	int keys;
	uint32_t last_code;
	unsigned last_modifiers;
};

/* The page's state between frames. */
struct page {
	struct kl_ui *ui;
	struct kl_canvas canvas;
	struct kl_text text;
	struct kl_style style;
	struct kl_field field;
	struct kl_list list;
	int on;
	double level;
	int place;
	int dialog;
	int chip;
};

/* The tests run and failed. */
static int test_count;
static int test_failed;

/* The time of the tests' clock, in microseconds. */
static uint64_t test_now;

/* The frame's pixels. */
static uint32_t page_pixels[PAGE_WIDTH * PAGE_HEIGHT];

/* The dialog's buttons. */
static const char *const dialog_labels[] = { "Delete", "Cancel" };

unsigned kl_appearance_get(const struct kl_appearance *appearance);
static void check(int condition, const char *what);
static void frame(struct page *page, struct page_report *report);
static void click(struct page *page, double x, double y, struct page_report *report);
static void key(struct page *page, uint32_t code, unsigned modifiers, struct page_report *report);
static void tap(struct page *page, double x, double y, struct page_report *report);
static void dialog_button(const struct page *page, int which, double *x, double *y);
static void test_pointer(struct page *page);
static void test_keys(struct page *page);
static void test_field(struct page *page);
static void test_list(struct page *page);
static void test_touch(struct page *page);
static void test_dialog(struct page *page);
static void test_look(struct page *page);
static void write_ppm(const char *path);

/*
 * Reports the light appearance: the host has no desktop to ask (the
 * library's own, appearance.c, needs Wayland).
 */
unsigned
kl_appearance_get(
	const struct kl_appearance *appearance)
{
	(void)appearance;

	/* The light appearance. */
	return KL_APPEARANCE_LIGHT;
}

/*
 * Runs the tests.
 */
int
main(
	int argc,
	char **argv)
{
	struct page page;
	char path[1024];
	int error;

	/* The fonts and where pictures go. */
	if (argc != 4) {
		fprintf(stderr, "usage: host-widgets FONT FALLBACK OUTPUT-PREFIX\n");
		return 2;
	}

	/* The page: its canvas, text, style and state. */
	memset(&page, 0, sizeof(page));
	error = kl_text_open(&page.text, argv[1], argv[2]);
	check(error == 0, "the text opens");
	error = kl_canvas_init(&page.canvas, page_pixels, PAGE_WIDTH, PAGE_WIDTH, PAGE_HEIGHT);
	check(error == 0, "the canvas");
	error = kl_list_init(&page.list);
	check(error == 0, "the list's state");
	page.ui = kl_ui_create();
	check(page.ui != NULL, "the input");
	if (test_failed != 0)
		return 1;
	page.style.canvas = &page.canvas;
	page.style.text = &page.text;
	page.style.theme = kl_theme_default();
	page.style.glass = 0;
	page.level = 25.0;
	test_now = 10U * SECOND;

	/* Each part. */
	test_pointer(&page);
	test_keys(&page);
	test_field(&page);
	test_list(&page);
	test_touch(&page);
	test_dialog(&page);
	test_look(&page);

	/* The gallery: the page with its widgets in use, and with the dialog. */
	snprintf(path, sizeof(path), "%s-page.ppm", argv[3]);
	write_ppm(path);
	page.dialog = 1;
	frame(&page, NULL);
	snprintf(path, sizeof(path), "%s-dialog.ppm", argv[3]);
	write_ppm(path);

	/* Everything goes. */
	kl_ui_destroy(page.ui);
	kl_list_release(&page.list);
	kl_canvas_release(&page.canvas);
	kl_text_close(&page.text);

	/* The outcome. */
	printf("host-widgets: %d/%d passed\n", test_count - test_failed, test_count);
	if (test_failed != 0)
		return 1;
	return 0;
}

/* Records one check. */
static void
check(
	int condition,
	const char *what)
{
	/* Counted, and a failure said. */
	test_count++;
	if (condition)
		return;
	test_failed++;
	printf("FAIL: %s\n", what);
}

/* Draws one frame of the page and reports what its widgets and the input said (report may be NULL). */
static void
frame(
	struct page *page,
	struct page_report *report)
{
	static const struct kl_rect sidebar = { 8, 8, 200, 584 };
	static const struct kl_rect content = { 216, 8, 576, 584 };
	static const struct kl_rect card = { 232, 96, 544, 250 };
	static const struct kl_rect save = { 250, 170, 100, 32 };
	static const struct kl_rect disabled = { 360, 170, 100, 32 };
	static const struct kl_rect slider = { 250, 220, 300, 24 };
	static const struct kl_rect field = { 250, 260, 300, 32 };
	static const struct kl_rect list = { 250, 360, 300, 220 };
	static const struct kl_rect progress = { 580, 230, 170, 6 };
	static const char *const places[] = { "Home", "Documents", "Pictures" };
	static const enum kl_icon icons[] = { KL_ICON_HOME, KL_ICON_DOCUMENTS, KL_ICON_PICTURES };
	struct page_report local;
	struct kl_event event;
	struct kl_rect place;
	struct kl_rect row;
	struct kl_rect background;
	kl_color ink;
	char label[32];
	size_t first;
	size_t last;
	size_t index;
	int top;
	int taken;
	int pressed;

	/* The answers of this frame. */
	if (report == NULL)
		report = &local;
	memset(report, 0, sizeof(report[0]));
	report->place = -1;
	report->answer = -1;

	/* The ground. */
	kl_ui_begin(page->ui, test_now);
	background.x = 0;
	background.y = 0;
	background.width = PAGE_WIDTH;
	background.height = PAGE_HEIGHT;
	kl_canvas_gradient(&page->canvas, &background, KL_RGB(0xdfe8f5), KL_RGB(0xc9d6ea));

	/* The sidebar with its places. */
	kl_panel(&page->style, &sidebar, 1);
	top = kl_sidebar_section(&page->style, 16, 16, 184, "Places");
	for (index = 0; index < 3U; index++) {
		place.x = 16;
		place.y = top + (int)index * 30;
		place.width = 184;
		place.height = 30;
		pressed = kl_sidebar_item(page->ui, &page->style, ID_SIDEBAR, (uint32_t)index, &place, icons[index], places[index], (int)index == page->place);
		if (pressed) {
			page->place = (int)index;
			report->place = (int)index;
		}
	}

	/* The content: its header and a card of controls. */
	kl_panel(&page->style, &content, 0);
	(void)kl_header(&page->style, 240, 24, 520, "Widgets", "Every control of libkeiland");
	top = kl_card(&page->style, &card, "Controls", "Buttons, a switch, a slider and a field");
	(void)top;
	report->saved = kl_button(page->ui, &page->style, ID_SAVE, &save, "Save", KL_BUTTON_PRIMARY);
	report->disabled_pressed = kl_button(page->ui, &page->style, ID_DISABLED, &disabled, "Disabled", KL_BUTTON_DISABLED);
	report->switched = kl_switch(page->ui, &page->style, ID_SWITCH, 480, 174, &page->on, 0U);
	report->slid = kl_slider(page->ui, &page->style, ID_SLIDER, &slider, 0.0, 100.0, 1.0, &page->level);
	report->field = kl_field(page->ui, &page->style, ID_FIELD, &field, &page->field, "Name");
	kl_progress(&page->style, &progress, page->level / 100.0, test_now);

	/* The list of rows. */
	report->list = kl_list_begin(page->ui, &page->style, ID_LIST, &list, &page->list, LIST_ROWS, &first, &last);
	for (index = first; index < last; index++) {
		report->list |= kl_list_row(page->ui, &page->style, ID_LIST, &list, &page->list, index, &row, &ink);
		snprintf(label, sizeof(label), "Row %u", (unsigned)index);
		(void)kl_text_draw(&page->text, &page->canvas, row.x + 10, kl_text_center(13U, row.y, row.height), label, strlen(label), 13U, 0, ink);
	}

	/* The list ends. */
	kl_list_end(page->ui, &page->style, &list, &page->list);

	/* The chip and the dialog, when they show. */
	if (page->chip)
		kl_chip(&page->style, 504, 590, "Saved");
	if (page->dialog)
		report->answer = kl_dialog(page->ui, &page->style, ID_DIALOG, &content, "Delete the file?", "The file goes to the Trash, and you can put it back from there later.", dialog_labels, 2);

	/* The frame is drawn; the input no widget took. */
	(void)kl_ui_end(page->ui, test_now);
	for (;;) {
		taken = kl_ui_take(page->ui, &event);
		if (!taken)
			break;
		if (event.kind != KL_EVENT_KEY)
			continue;
		report->keys++;
		report->last_code = event.code;
		report->last_modifiers = event.modifiers;
	}
}

/* A click of the pointer at a place, with a frame drawn after it. */
static void
click(
	struct page *page,
	double x,
	double y,
	struct page_report *report)
{
	/* Over it, down and up. */
	(void)kl_ui_pointer_motion(page->ui, x, y);
	(void)kl_ui_pointer_button(page->ui, 1, test_now);
	(void)kl_ui_pointer_button(page->ui, 0, test_now);
	frame(page, report);
}

/* A key pressed and released, with a frame drawn after it. */
static void
key(
	struct page *page,
	uint32_t code,
	unsigned modifiers,
	struct page_report *report)
{
	/* Down and up. */
	(void)kl_ui_key(page->ui, code, 1, modifiers);
	(void)kl_ui_key(page->ui, code, 0, modifiers);
	frame(page, report);
}

/* A finger's tap at a place, with a frame drawn after it. */
static void
tap(
	struct page *page,
	double x,
	double y,
	struct page_report *report)
{
	/* Down, and up a moment later. */
	(void)kl_ui_touch_down(page->ui, 1, test_now, test_now, x, y);
	test_now += 60000U;
	(void)kl_ui_touch_up(page->ui, 1, test_now, test_now);
	frame(page, report);
}

/* Reports the middle of one of the dialog's buttons (0 the main one, at the right). */
static void
dialog_button(
	const struct page *page,
	int which,
	double *x,
	double *y)
{
	int right;
	int width;
	int index;

	/* The card is 392 wide and 170 high in the middle of the content; the buttons stand from its right, 8 apart. */
	right = 216 + (576 - 392) / 2 + 392 - 18;
	width = 0;
	for (index = 0; index <= which; index++) {
		width = kl_button_width(&page->style, dialog_labels[index]);
		if (index < which)
			right -= width + 8;
	}

	/* The button's middle. */
	*x = (double)(right - width / 2);
	*y = (double)(8 + (584 - 170) / 2 + 170 - 16 - 16);
}

/* The pointer: a button, a disabled one, the switch, the slider (a click and a drag) and a sidebar's place. */
static void
test_pointer(
	struct page *page)
{
	struct page_report report;
	int step;

	/* The first frame records the widgets. */
	frame(page, &report);

	/* A click on the button presses it, once. */
	click(page, 300.0, 186.0, &report);
	check(report.saved == 1, "a click presses the button");
	frame(page, &report);
	check(report.saved == 0, "the press is reported once");
	check(kl_ui_has_focus(page->ui, ID_SAVE, 0U), "the clicked button has the keyboard");

	/* A disabled button is never pressed. */
	test_now += SECOND;
	click(page, 410.0, 186.0, &report);
	check(report.disabled_pressed == 0, "a disabled button is not pressed");
	check(!kl_ui_has_focus(page->ui, ID_DISABLED, 0U), "a disabled button takes no keyboard");

	/* A click on the switch flips it. */
	test_now += SECOND;
	click(page, 500.0, 186.0, &report);
	check(report.switched == 1 && page->on == 1, "a click turns the switch on");
	test_now += SECOND;
	click(page, 500.0, 186.0, &report);
	check(report.switched == 1 && page->on == 0, "a second click turns it off");

	/* A click on the slider's track moves the value there (the track runs from 259 to 541). */
	test_now += SECOND;
	click(page, 259.0 + 282.0 * 0.5, 232.0, &report);
	check(report.slid == 1 && page->level == 50.0, "a click on the slider moves it to the point");

	/* A drag of the slider follows the pointer, even off the track. */
	test_now += SECOND;
	(void)kl_ui_pointer_motion(page->ui, 259.0 + 282.0 * 0.5, 232.0);
	(void)kl_ui_pointer_button(page->ui, 1, test_now);
	frame(page, &report);
	for (step = 1; step <= 5; step++) {
		test_now += 16667U;
		(void)kl_ui_pointer_motion(page->ui, 259.0 + 282.0 * (0.5 + 0.06 * step), 232.0 + 10.0 * step);
		frame(page, &report);
	}

	/* The value followed. */
	check(page->level == 80.0, "a drag of the slider follows the pointer");
	(void)kl_ui_pointer_motion(page->ui, 700.0, 300.0);
	frame(page, &report);
	check(page->level == 100.0, "the drag keeps the value at its end");
	(void)kl_ui_pointer_button(page->ui, 0, test_now);
	frame(page, &report);
	(void)kl_ui_pointer_motion(page->ui, 259.0, 232.0);
	frame(page, &report);
	check(page->level == 100.0, "after the release the slider stays");

	/* A last motion and the release between two frames: the value is where the pointer let go. */
	test_now += SECOND;
	(void)kl_ui_pointer_motion(page->ui, 259.0 + 282.0 * 0.5, 232.0);
	frame(page, &report);
	(void)kl_ui_pointer_button(page->ui, 1, test_now);
	frame(page, &report);
	(void)kl_ui_pointer_motion(page->ui, 259.0 + 282.0 * 0.7, 250.0);
	(void)kl_ui_pointer_button(page->ui, 0, test_now);
	frame(page, &report);
	check(page->level == 70.0, "a drag let go between frames ends where the pointer was");

	/* A click on a sidebar's place reports it. */
	test_now += SECOND;
	click(page, 80.0, 46.0 + 30.0 + 15.0, &report);
	check(report.place == 1 && page->place == 1, "a click on a place chooses it");
	check(!kl_ui_has_focus(page->ui, ID_SAVE, 0U), "a click elsewhere takes the keyboard away");
}

/* The keyboard: Tab through the widgets, Enter and Space, the slider's keys, and the keys that stay the application's. */
static void
test_keys(
	struct page *page)
{
	struct page_report report;
	int step;

	/* Without a focused widget every key is the application's. */
	test_now += SECOND;
	kl_ui_clear_focus(page->ui);
	key(page, KEY_A, 0U, &report);
	check(report.keys == 1 && report.last_code == KEY_A, "a key with no focus is the application's");

	/* Tab goes through the widgets that take the keyboard, in order (not the disabled button, one stop for the list). */
	key(page, KL_KEY_TAB, 0U, &report);
	check(kl_ui_has_focus(page->ui, ID_SAVE, 0U), "Tab reaches the button first");
	check(report.keys == 0, "Tab is not the application's");
	key(page, KL_KEY_TAB, 0U, &report);
	check(kl_ui_has_focus(page->ui, ID_SWITCH, 0U), "then the switch");
	key(page, KL_KEY_TAB, 0U, &report);
	check(kl_ui_has_focus(page->ui, ID_SLIDER, 0U), "then the slider");
	key(page, KL_KEY_TAB, 0U, &report);
	check(kl_ui_has_focus(page->ui, ID_FIELD, 0U), "then the field");
	key(page, KL_KEY_TAB, 0U, &report);
	check(kl_ui_has_focus(page->ui, ID_LIST, LIST_SELF), "then the list");
	key(page, KL_KEY_TAB, 0U, &report);
	check(kl_ui_has_focus(page->ui, ID_SAVE, 0U), "and round to the button (the rows are not stops)");
	key(page, KL_KEY_TAB, KL_MOD_SHIFT, &report);
	check(kl_ui_has_focus(page->ui, ID_LIST, LIST_SELF), "Shift+Tab goes back round");
	key(page, KL_KEY_TAB, KL_MOD_SHIFT, &report);
	check(kl_ui_has_focus(page->ui, ID_FIELD, 0U), "and back again");

	/* Enter and Space press the focused button; a shortcut stays the application's. */
	kl_ui_set_focus(page->ui, ID_SAVE, 0U);
	key(page, KL_KEY_ENTER, 0U, &report);
	check(report.saved == 1 && report.keys == 0, "Enter presses the focused button");
	key(page, KL_KEY_SPACE, 0U, &report);
	check(report.saved == 1, "Space presses it too");
	key(page, KEY_S, KL_MOD_CTRL, &report);
	check(report.saved == 0 && report.keys == 1 && report.last_code == KEY_S && report.last_modifiers == KL_MOD_CTRL, "Ctrl+S passes the button to the application");

	/* Space flips the focused switch. */
	kl_ui_set_focus(page->ui, ID_SWITCH, 0U);
	key(page, KL_KEY_SPACE, 0U, &report);
	check(report.switched == 1 && page->on == 1, "Space flips the focused switch");

	/* The slider's keys: a step, a page, the ends; Control's arrows stay the application's. */
	kl_ui_set_focus(page->ui, ID_SLIDER, 0U);
	key(page, KL_KEY_HOME, 0U, &report);
	check(page->level == 0.0, "Home moves the slider to its start");
	key(page, KL_KEY_RIGHT, 0U, &report);
	check(page->level == 1.0 && report.slid == 1, "Right moves it a step");
	key(page, KL_KEY_PAGEUP, 0U, &report);
	check(page->level == 11.0, "Page Up moves it ten steps");
	key(page, KL_KEY_DOWN, 0U, &report);
	check(page->level == 10.0, "Down moves it back a step");
	key(page, KL_KEY_END, 0U, &report);
	check(page->level == 100.0, "End moves it to its end");
	key(page, KL_KEY_RIGHT, 0U, &report);
	check(page->level == 100.0 && report.slid == 0, "past the end it stays");
	key(page, KL_KEY_LEFT, KL_MOD_CTRL, &report);
	check(page->level == 100.0 && report.keys == 1 && report.last_code == KL_KEY_LEFT, "Ctrl+Left is the application's");

	/* Several keys in one frame are carried out in order. */
	(void)kl_ui_key(page->ui, KL_KEY_HOME, 1, 0U);
	for (step = 0; step < 3; step++)
		(void)kl_ui_key(page->ui, KL_KEY_RIGHT, 1, 0U);
	frame(page, &report);
	check(page->level == 3.0, "keys between frames are all carried out");
}

/* The field: a click takes the keyboard, typing, moving and selecting, erasing, Enter and Esc, and the keys it leaves. */
static void
test_field(
	struct page *page)
{
	struct page_report report;

	/* A click takes the keyboard; characters go in. */
	test_now += SECOND;
	kl_field_set(&page->field, "");
	click(page, 400.0, 276.0, &report);
	check(kl_ui_has_focus(page->ui, ID_FIELD, 0U), "a click gives the field the keyboard");
	key(page, KEY_A, 0U, &report);
	check((report.field & KL_FIELD_CHANGED) != 0U && report.keys == 0, "a character changes the field");
	key(page, KEY_B, 0U, &report);
	key(page, KEY_C, KL_MOD_SHIFT, &report);
	check(strcmp(page->field.text, "abC") == 0 && page->field.caret == 3U, "the characters go in at the caret");

	/* Left, Shift+Home selects to the start, and a character replaces the selection. */
	key(page, KL_KEY_LEFT, 0U, &report);
	key(page, KL_KEY_HOME, KL_MOD_SHIFT, &report);
	check(page->field.anchor == 2U && page->field.caret == 0U, "Shift+Home selects to the start");
	key(page, KEY_X, 0U, &report);
	check(strcmp(page->field.text, "xC") == 0 && page->field.caret == 1U, "a character replaces the selection");

	/* Backspace and Delete. */
	key(page, KL_KEY_BACKSPACE, 0U, &report);
	check(strcmp(page->field.text, "C") == 0 && page->field.caret == 0U, "Backspace erases the character before the caret");
	key(page, KL_KEY_DELETE, 0U, &report);
	check(page->field.length == 0U && (report.field & KL_FIELD_CHANGED) != 0U, "Delete erases the one after it");

	/* Ctrl+A selects everything; Enter submits, Esc cancels. */
	kl_field_set(&page->field, "report.txt");
	key(page, KEY_A, KL_MOD_CTRL, &report);
	check(page->field.anchor == 0U && page->field.caret == 10U && report.keys == 0, "Ctrl+A selects the whole text");
	key(page, KL_KEY_ENTER, 0U, &report);
	check(report.field == KL_FIELD_SUBMITTED, "Enter submits");
	key(page, KL_KEY_ESC, 0U, &report);
	check(report.field == KL_FIELD_CANCELLED, "Esc cancels");

	/* Keys, a Tab and more keys between two frames: those before the Tab are the field's, those after the next widget's. */
	kl_field_set(&page->field, "");
	kl_ui_set_focus(page->ui, ID_FIELD, 0U);
	(void)kl_ui_key(page->ui, KEY_A, 1, 0U);
	(void)kl_ui_key(page->ui, KEY_B, 1, 0U);
	(void)kl_ui_key(page->ui, KL_KEY_TAB, 1, 0U);
	(void)kl_ui_key(page->ui, KL_KEY_DOWN, 1, 0U);
	(void)kl_ui_key(page->ui, KEY_C, 1, 0U);
	page->list.selected = 0;
	frame(page, &report);
	check(strcmp(page->field.text, "ab") == 0, "the keys before a Tab are the field's");
	check(page->list.selected == 1L, "the keys after it are the next widget's");
	check(report.keys == 1 && report.last_code == KEY_C, "one the next widget does not want is the application's");
	kl_field_set(&page->field, "report.txt");
	kl_ui_set_focus(page->ui, ID_FIELD, 0U);

	/* Ctrl+S and Tab are not the field's. */
	key(page, KEY_S, KL_MOD_CTRL, &report);
	check(report.field == 0U && report.keys == 1 && report.last_code == KEY_S, "Ctrl+S passes the field to the application");
	key(page, KL_KEY_TAB, 0U, &report);
	check(kl_ui_has_focus(page->ui, ID_LIST, LIST_SELF), "Tab leaves the field");

	/* A double click selects the whole text; a click puts the caret near the point. */
	test_now += SECOND;
	click(page, 400.0, 276.0, &report);
	click(page, 400.0, 276.0, &report);
	check(page->field.anchor == 0U && page->field.caret == 10U, "a double click selects the whole text");
	test_now += SECOND;
	click(page, 263.0, 276.0, &report);
	check(page->field.caret == 0U && page->field.anchor == 0U, "a click at the start puts the caret there");
	kl_field_set(&page->field, "Kei");
}

/* The list: a click selects and gives it the keyboard, the keys move the selection and scroll, Enter and a double click activate. */
static void
test_list(
	struct page *page)
{
	struct page_report report;
	int step;

	/* A click on the fourth row selects it, and the list has the keyboard. */
	test_now += SECOND;
	click(page, 350.0, 360.0 + 3.0 * 28.0 + 14.0, &report);
	check((report.list & KL_LIST_SELECTED) != 0U && page->list.selected == 3L, "a click selects a row");
	check(kl_ui_has_focus(page->ui, ID_LIST, LIST_SELF), "the list has the keyboard");

	/* Down, Page Down and Up move the selection. */
	key(page, KL_KEY_DOWN, 0U, &report);
	check(page->list.selected == 4L && (report.list & KL_LIST_SELECTED) != 0U, "Down selects the next row");
	key(page, KL_KEY_PAGEDOWN, 0U, &report);
	check(page->list.selected == 10L, "Page Down moves a page (six rows)");
	key(page, KL_KEY_UP, 0U, &report);
	check(page->list.selected == 9L, "Up selects the row before");

	/* End selects the last row and the list glides to show it. */
	key(page, KL_KEY_END, 0U, &report);
	check(page->list.selected == 99L, "End selects the last row");
	for (step = 0; step < 60; step++) {
		test_now += 16667U;
		frame(page, &report);
	}

	/* Glided to the end. */
	check(fabs(page->list.scroll.y - (100.0 * 28.0 - 220.0)) < 0.5, "the list shows the last row");
	key(page, KL_KEY_HOME, 0U, &report);
	for (step = 0; step < 60; step++) {
		test_now += 16667U;
		frame(page, &report);
	}

	/* And back. */
	check(page->list.selected == 0L && page->list.scroll.y < 0.5, "Home selects the first and scrolls back");

	/* Enter activates; a letter is the application's. */
	key(page, KL_KEY_ENTER, 0U, &report);
	check((report.list & KL_LIST_ACTIVATED) != 0U, "Enter activates the selected row");
	key(page, KEY_A, 0U, &report);
	check(report.keys == 1 && report.last_code == KEY_A, "a letter passes the list to the application");

	/* A double click on a row activates it. */
	test_now += SECOND;
	click(page, 350.0, 360.0 + 2.0 * 28.0 + 14.0, &report);
	click(page, 350.0, 360.0 + 2.0 * 28.0 + 14.0, &report);
	check(page->list.selected == 2L && (report.list & KL_LIST_ACTIVATED) != 0U, "a double click activates a row");

	/* The wheel scrolls the list. */
	test_now += SECOND;
	(void)kl_ui_pointer_motion(page->ui, 350.0, 450.0);
	(void)kl_ui_wheel(page->ui, 0.0, 300.0, test_now);
	for (step = 0; step < 60; step++) {
		test_now += 16667U;
		frame(page, &report);
	}

	/* Scrolled. */
	check(page->list.scroll.y > 100.0, "the wheel scrolls the list");
	kl_scroll_move_to(&page->list.scroll, 0.0, 0.0, 0, test_now);
	frame(page, &report);
}

/* A finger: a tap presses a button, flips the switch, selects a row (giving the list the keyboard); a drag moves the slider. */
static void
test_touch(
	struct page *page)
{
	struct page_report report;
	int step;

	/* A tap on the button presses it and gives it the keyboard. */
	test_now += SECOND;
	tap(page, 300.0, 186.0, &report);
	check(report.saved == 1, "a tap presses the button");
	check(kl_ui_has_focus(page->ui, ID_SAVE, 0U), "the tapped button has the keyboard");

	/* A tap on the switch flips it. */
	test_now += SECOND;
	page->on = 0;
	tap(page, 500.0, 186.0, &report);
	check(report.switched == 1 && page->on == 1, "a tap flips the switch");

	/* A tap on a row selects it, and the list takes the keyboard. */
	test_now += SECOND;
	tap(page, 350.0, 360.0 + 5.0 * 28.0 + 14.0, &report);
	check(page->list.selected == 5L, "a tap selects a row");
	check(kl_ui_has_focus(page->ui, ID_LIST, LIST_SELF), "the tapped row gives its list the keyboard");

	/* A finger that holds the slider's knob and moves drags it. */
	test_now += SECOND;
	page->level = 50.0;
	frame(page, &report);
	(void)kl_ui_touch_down(page->ui, 1, test_now, test_now, 259.0 + 282.0 * 0.5, 232.0);
	frame(page, &report);
	for (step = 1; step <= 10; step++) {
		test_now += 16667U;
		(void)kl_ui_touch_motion(page->ui, 1, test_now, test_now, 259.0 + 282.0 * (0.5 - 0.03 * step), 232.0 + 2.0 * step);
		frame(page, &report);
	}

	/* The value followed the finger. */
	check(fabs(page->level - 20.0) <= 1.0, "a finger drags the slider");
	(void)kl_ui_touch_up(page->ui, 1, test_now, test_now);
	frame(page, &report);
	check(page->list.scroll.y == 0.0, "the drag does not scroll the list");
}

/* The dialog: it takes the keyboard, Tab stays in it, Enter and Esc answer, a click on a button answers, and nothing under it is pressed. */
static void
test_dialog(
	struct page *page)
{
	struct page_report report;
	double x;
	double y;

	/* Shown, it takes the keyboard. */
	test_now += SECOND;
	kl_ui_set_focus(page->ui, ID_FIELD, 0U);
	page->dialog = 1;
	frame(page, &report);
	check(kl_ui_has_focus(page->ui, ID_DIALOG, DIALOG_SELF), "the dialog takes the keyboard");

	/* Enter chooses the main button; Esc the last. */
	key(page, KL_KEY_ENTER, 0U, &report);
	check(report.answer == 0, "Enter chooses the main button");
	key(page, KL_KEY_ESC, 0U, &report);
	check(report.answer == 1, "Esc chooses Cancel");
	check(report.field == 0U, "the field under the dialog took nothing");

	/* Tab goes round the dialog's buttons only. */
	key(page, KL_KEY_TAB, 0U, &report);
	check(kl_ui_has_focus(page->ui, ID_DIALOG, 0U), "Tab reaches the main button");
	key(page, KL_KEY_TAB, 0U, &report);
	check(kl_ui_has_focus(page->ui, ID_DIALOG, 1U), "then Cancel");
	key(page, KL_KEY_TAB, 0U, &report);
	check(kl_ui_has_focus(page->ui, ID_DIALOG, DIALOG_SELF), "and round to the dialog, not under it");
	key(page, KL_KEY_TAB, KL_MOD_SHIFT, &report);
	key(page, KL_KEY_SPACE, 0U, &report);
	check(report.answer == 1, "Space presses the focused Cancel");
	key(page, KL_KEY_ESC, 0U, &report);
	check(report.answer == 1, "Esc still answers while a button has the keyboard");

	/* A click on the main button answers; a click on the button under the veil presses nothing. */
	test_now += SECOND;
	dialog_button(page, 0, &x, &y);
	click(page, x, y, &report);
	check(report.answer == 0, "a click on Delete answers");
	test_now += SECOND;
	dialog_button(page, 1, &x, &y);
	click(page, x, y, &report);
	check(report.answer == 1, "a click on Cancel answers");
	test_now += SECOND;
	click(page, 300.0, 186.0, &report);
	check(report.saved == 0 && report.answer == -1, "the button under the veil is not pressed");

	/* Closed, the page takes its input again. */
	page->dialog = 0;
	frame(page, &report);
	test_now += SECOND;
	click(page, 300.0, 186.0, &report);
	check(report.saved == 1, "without the dialog the button works again");
}

/* The look: Settings' and Files' colours on the drawn page. */
static void
test_look(
	struct page *page)
{
	const struct kl_theme *theme;
	uint32_t ring_before;
	uint32_t pixel;
	int red;

	/* The theme's quiet text is WS099's darker grey. */
	theme = page->style.theme;
	check(theme->text_secondary == KL_RGB(0x56606f), "the quiet text is 0x56606f");

	/* The page in use: a selected row, text in the field, a chip. */
	test_now += SECOND;
	page->chip = 1;
	page->list.selected = 1;
	kl_ui_set_focus(page->ui, ID_FIELD, 0U);
	page->level = 40.0;
	(void)kl_ui_pointer_motion(page->ui, 700.0, 500.0);
	frame(page, NULL);
	frame(page, NULL);

	/* The main button is the accent. */
	pixel = page_pixels[178 * PAGE_WIDTH + 262];
	red = (int)((pixel >> 16) & 0xffU);
	check(abs(red - 0x2f) <= 8 && abs((int)(pixel & 0xffU) - 0xf6) <= 8, "the main button is the accent");

	/* The focus's ring shows when Tab moved the focus, not after a click. */
	test_now += SECOND;
	click(page, 300.0, 186.0, NULL);
	frame(page, NULL);
	pixel = page_pixels[186 * PAGE_WIDTH + 248];
	check(((pixel >> 16) & 0xffU) > 0xc0U, "no ring after a click");
	ring_before = pixel;
	kl_ui_clear_focus(page->ui);
	key(page, KL_KEY_TAB, 0U, NULL);
	frame(page, NULL);
	pixel = page_pixels[186 * PAGE_WIDTH + 248];
	check(kl_ui_has_focus(page->ui, ID_SAVE, 0U) && pixel != ring_before && ((pixel >> 16) & 0xffU) < ((ring_before >> 16) & 0xffU), "the ring after Tab");
	(void)kl_ui_pointer_motion(page->ui, 700.0, 500.0);
	kl_ui_set_focus(page->ui, ID_FIELD, 0U);
	frame(page, NULL);

	/* The field is white inside. */
	pixel = page_pixels[264 * PAGE_WIDTH + 540];
	check((pixel & 0xffffffU) == 0xffffffU, "the field is white");
}

/* Writes the frame as a PPM picture. */
static void
write_ppm(
	const char *path)
{
	FILE *file;
	size_t index;
	uint32_t pixel;
	unsigned char rgb[3];

	/* The file and its header. */
	file = fopen(path, "wb");
	if (file == NULL) {
		check(0, "the picture opens");
		return;
	}

	/* The header. */
	fprintf(file, "P6\n%d %d\n255\n", PAGE_WIDTH, PAGE_HEIGHT);

	/* Each pixel's colour. */
	for (index = 0; index < (size_t)PAGE_WIDTH * PAGE_HEIGHT; index++) {
		pixel = page_pixels[index];
		rgb[0] = (unsigned char)((pixel >> 16) & 0xffU);
		rgb[1] = (unsigned char)((pixel >> 8) & 0xffU);
		rgb[2] = (unsigned char)(pixel & 0xffU);
		(void)fwrite(rgb, 1, sizeof(rgb), file);
	}

	/* Done. */
	(void)fclose(file);
}
