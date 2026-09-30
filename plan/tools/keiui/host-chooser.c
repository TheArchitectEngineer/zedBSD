/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host tests of the file chooser's model and view (ws090-p006, from the
 * tests of libkeiland's chooser of ws092-p003): a tree of folders and
 * files under a temporary home, the keys, clicks and taps a user makes
 * through kui_ui, the frames drawn with the widgets, the answers they lead
 * to, and pictures of the window (written as PPM, turned into PNG by
 * host-chooser.sh).
 *
 *   host-chooser FONT FALLBACK OUTPUT-PREFIX
 */

#include "chooser.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The window's size (the chooser's own), and a narrow one. */
#define TEST_WIDTH	760
#define TEST_HEIGHT	480
#define TEST_NARROW_W	540
#define TEST_NARROW_H	360

/* A second, in microseconds. */
#define SECOND		1000000U

/* The parts' places at the chooser's size (chooser-view.c's layout). */
#define LIST_X		212
#define LIST_Y		84
#define ROW		28
#define PLACES_Y	46
#define BUTTON_Y	448
#define ACCEPT_X	694
#define CANCEL_X	598
#define OPEN_FILTER_X	280
#define SAVE_FILTER_X	476
#define NAME_X		330
#define UP_X		226
#define UP_Y		32
#define LOCATION_X	400

/* The evdev codes of the letters of the commands. */
#define KEY_A		30U
#define KEY_H		35U
#define KEY_L		38U

/*
 * One chooser under test: its model, its input, the frame it draws into,
 * the style, and its size.
 */
struct tester {
	struct keiui_chooser chooser;
	struct kui_ui *ui;
	struct kui_canvas canvas;
	struct kui_style style;
	uint32_t *pixels;
	int width;
	int height;
};

/* The tests run and failed so far. */
static int test_count;
static int test_failed;

/* The temporary home, the text, where pictures go, and the tests' clock. */
static char test_home[512];
static struct kui_text test_text;
static const char *test_prefix;
static uint64_t test_now;

static void check(int condition, const char *what);
static void make_file(const char *relative, const char *contents);
static void make_folder(const char *relative);
static void path_of(const char *relative, char *out, size_t size);
static int start(struct tester *tester, const struct kui_file_chooser_options *options, int width, int height);
static void finish(struct tester *tester);
static void frame(struct tester *tester);
static void click(struct tester *tester, int x, int y);
static void tap(struct tester *tester, int x, int y);
static void key(struct tester *tester, uint32_t code, unsigned modifiers);
static void type_text(struct tester *tester, const char *text);
static long find(const struct keiui_chooser *chooser, const char *name);
static int row_y(const struct tester *tester, long index);
static void picture(struct tester *tester, const char *name);
static void test_open(void);
static void test_save(void);
static void test_path(void);
static void test_pictures(void);

/*
 * Runs the tests.
 */
int
main(
	int argc,
	char **argv)
{
	char *made;
	int error;

	/* The fonts and where pictures go. */
	if (argc != 4) {
		fprintf(stderr, "usage: host-chooser FONT FALLBACK OUTPUT-PREFIX\n");
		return 2;
	}

	/* The text every frame draws with. */
	test_prefix = argv[3];
	error = kui_text_open(&test_text, argv[1], argv[2]);
	if (error != 0) {
		fprintf(stderr, "font %s: %s\n", argv[1], strerror(error));
		return 2;
	}

	/* A home of its own, with the usual folders and some files. */
	snprintf(test_home, sizeof(test_home), "/tmp/host-chooser-XXXXXX");
	made = mkdtemp(test_home);
	if (made == NULL) {
		perror("mkdtemp");
		return 2;
	}

	/* It is the home. */
	setenv("HOME", test_home, 1);
	unsetenv("XDG_DATA_HOME");
	make_folder("Desktop");
	make_folder("Documents");
	make_folder("Documents/sub");
	make_folder("Documents/Zeta");
	make_folder("Documents/.secret");
	make_file("Documents/a.txt", "alpha\n");
	make_file("Documents/B.md", "# bravo\n");
	make_file("Documents/c.png", "not really a picture");
	make_file("Documents/.hidden.txt", "hidden\n");
	make_file("Documents/\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e.txt", "nihongo\n");
	make_file("Documents/sub/inner.txt", "inner\n");
	make_folder("locked");
	chmod(test_home, 0755);

	/* The tests. */
	test_now = 10U * SECOND;
	test_open();
	test_save();
	test_path();
	test_pictures();
	kui_text_close(&test_text);

	/* The outcome. */
	printf("host-chooser: %d/%d passed\n", test_count - test_failed, test_count);
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

/* Writes a file under the home. */
static void
make_file(
	const char *relative,
	const char *contents)
{
	char path[1024];
	FILE *file;

	/* The file with its contents. */
	path_of(relative, path, sizeof(path));
	file = fopen(path, "w");
	if (file == NULL) {
		perror(path);
		exit(2);
	}

	/* Its contents. */
	fputs(contents, file);
	fclose(file);
}

/* Makes a folder under the home. */
static void
make_folder(
	const char *relative)
{
	char path[1024];

	/* The folder. */
	path_of(relative, path, sizeof(path));
	mkdir(path, 0755);
}

/* Writes a path under the home. */
static void
path_of(
	const char *relative,
	char *out,
	size_t size)
{
	/* The home and the relative path. */
	snprintf(out, size, "%s/%s", test_home, relative);
}

/* Makes a chooser under test at a size and draws its first frame; 0 or the model's error. */
static int
start(
	struct tester *tester,
	const struct kui_file_chooser_options *options,
	int width,
	int height)
{
	int error;

	/* The model. */
	memset(tester, 0, sizeof(*tester));
	error = keiui_chooser_init(&tester->chooser, options);
	if (error != 0) {
		keiui_chooser_fini(&tester->chooser);
		return error;
	}

	/* The input, the frame and the style. */
	tester->width = width;
	tester->height = height;
	tester->ui = kui_ui_create();
	tester->pixels = calloc((size_t)width * (size_t)height, sizeof(tester->pixels[0]));
	(void)kui_canvas_init(&tester->canvas, tester->pixels, (size_t)width, width, height);
	tester->style.canvas = &tester->canvas;
	tester->style.text = &test_text;
	tester->style.theme = kui_theme_default();
	tester->style.glass = 0;

	/* The first frame, which records the widgets. */
	test_now += SECOND;
	frame(tester);
	return 0;
}

/* Frees a chooser under test. */
static void
finish(
	struct tester *tester)
{
	/* Everything it holds. */
	keiui_chooser_fini(&tester->chooser);
	kui_ui_destroy(tester->ui);
	kui_canvas_release(&tester->canvas);
	free(tester->pixels);
}

/* Draws one frame (and the frames a glide asks for, a sixtieth of a second apart). */
static void
frame(
	struct tester *tester)
{
	int again;
	int count;

	/* Until nothing moves, within reason. */
	for (count = 0; count < 120; count++) {
		again = keiui_chooser_frame(&tester->chooser, tester->ui, &tester->style, tester->width, tester->height, test_now);
		if (!again)
			break;
		test_now += 16667U;
	}
}

/* A click of the pointer at a place (a second apart from the last, unless called at once again). */
static void
click(
	struct tester *tester,
	int x,
	int y)
{
	/* Over it, down and up, and a frame. */
	(void)kui_ui_pointer_motion(tester->ui, (double)x, (double)y);
	(void)kui_ui_pointer_button(tester->ui, 1, test_now);
	(void)kui_ui_pointer_button(tester->ui, 0, test_now);
	frame(tester);
}

/* A finger's tap at a place, and a frame. */
static void
tap(
	struct tester *tester,
	int x,
	int y)
{
	/* Down, and up a moment later. */
	(void)kui_ui_touch_down(tester->ui, 1, test_now, test_now, (double)x, (double)y);
	test_now += 60000U;
	(void)kui_ui_touch_up(tester->ui, 1, test_now, test_now);
	frame(tester);
}

/* A key pressed with modifiers, and a frame. */
static void
key(
	struct tester *tester,
	uint32_t code,
	unsigned modifiers)
{
	/* Down and up. */
	(void)kui_ui_key(tester->ui, code, 1, modifiers);
	(void)kui_ui_key(tester->ui, code, 0, modifiers);
	frame(tester);
}

/* Types ASCII text through the keys that type it (US layout). */
static void
type_text(
	struct tester *tester,
	const char *text)
{
	uint32_t code;
	uint32_t typed;
	unsigned shift;

	/* Each character: the key that types it, with Shift or without. */
	for (; *text != '\0'; text++) {
		for (code = 1; code < 58U; code++) {
			typed = kui_key_character(code, 0U);
			shift = 0U;
			if (typed != (uint32_t)(unsigned char)*text) {
				typed = kui_key_character(code, KUI_MOD_SHIFT);
				shift = KUI_MOD_SHIFT;
			}

			/* Found. */
			if (typed == (uint32_t)(unsigned char)*text)
				break;
		}

		/* The key. */
		(void)kui_ui_key(tester->ui, code, 1, shift);
	}

	/* One frame for them all. */
	frame(tester);
}

/* Reports the index of an item by its name, or -1. */
static long
find(
	const struct keiui_chooser *chooser,
	const char *name)
{
	size_t index;
	int same;

	/* Each item. */
	for (index = 0; index < chooser->count; index++) {
		same = strcmp(chooser->entries[index].name, name);
		if (same == 0)
			return (long)index;
	}

	/* Not listed. */
	return -1L;
}

/* Reports the middle of an item's row, scrolled. */
static int
row_y(
	const struct tester *tester,
	long index)
{
	/* The row's middle. */
	return LIST_Y + (int)index * ROW + ROW / 2 - (int)tester->chooser.list.scroll.y;
}

/* Open: listing, sorting, hidden items, filters, places, keys, clicks and taps. */
static void
test_open(void)
{
	static const struct kui_file_filter filters[] = {
		{ "Text Files", "txt md" },
		{ "All Files", NULL }
	};
	struct kui_file_chooser_options options;
	struct tester tester;
	struct keiui_chooser *chooser;
	char folder[1024];
	char expected[1024];
	long index;
	int error;

	/* A chooser at Documents with the text filter. */
	memset(&options, 0, sizeof(options));
	options.mode = KUI_FILE_CHOOSER_OPEN;
	path_of("Documents", folder, sizeof(folder));
	options.folder = folder;
	options.filters = filters;
	options.filter_count = 2;
	options.filter = 0;
	error = start(&tester, &options, TEST_WIDTH, TEST_HEIGHT);
	chooser = &tester.chooser;
	check(error == 0, "open: init");
	check(strcmp(chooser->folder, folder) == 0, "open: starts in the folder given");
	check(strcmp(chooser->title, "Open") == 0, "open: title");
	check(kui_ui_has_focus(tester.ui, KEIUI_CHOOSER_ID_LIST, KEIUI_CHOOSER_LIST_SELF), "open: the list has the keyboard");

	/* Folders first, then names without regard to case; hidden and filtered items left out. */
	check(chooser->count == 5U, "open: five items (two folders, three text files)");
	check(chooser->count >= 5U && strcmp(chooser->entries[0].name, "sub") == 0, "open: sub first");
	check(chooser->count >= 5U && strcmp(chooser->entries[1].name, "Zeta") == 0, "open: Zeta second");
	check(chooser->count >= 5U && strcmp(chooser->entries[2].name, "a.txt") == 0, "open: a.txt");
	check(chooser->count >= 5U && strcmp(chooser->entries[3].name, "B.md") == 0, "open: B.md (case ignored)");
	check(find(chooser, "c.png") < 0, "open: c.png filtered out");
	check(find(chooser, ".hidden.txt") < 0, "open: hidden file left out");

	/* The places: Recent, Home, Desktop, Documents, Computer (no Downloads). */
	check(chooser->place_count == 5U, "open: five places");
	check(strcmp(chooser->places[0].label, "Recent") == 0, "open: Recent first");
	check(strcmp(chooser->places[4].label, "Computer") == 0, "open: Computer last");

	/* Ctrl+H shows the hidden items, and again hides them. */
	key(&tester, KEY_H, KUI_MOD_CTRL);
	check(find(chooser, ".hidden.txt") >= 0, "open: Ctrl+H shows hidden files");
	check(find(chooser, ".secret") >= 0, "open: Ctrl+H shows hidden folders");
	key(&tester, KEY_H, KUI_MOD_CTRL);
	check(find(chooser, ".hidden.txt") < 0, "open: Ctrl+H again hides them");

	/* The filter button: All Files shows the picture. */
	test_now += SECOND;
	click(&tester, OPEN_FILTER_X, BUTTON_Y);
	check(chooser->filter == 1U, "open: filter clicked to All Files");
	check(find(chooser, "c.png") >= 0, "open: All Files shows c.png");

	/* Nothing selected: Open cannot be pressed (a click on it does nothing). */
	check(keiui_chooser_can_accept(chooser) == 0, "open: nothing to open yet");
	test_now += SECOND;
	click(&tester, ACCEPT_X, BUTTON_Y);
	check(chooser->answered == 0, "open: the faded Open button does nothing");

	/* Down selects the first item, Enter goes into it, Backspace comes back. */
	key(&tester, KUI_KEY_DOWN, 0U);
	check(chooser->list.selected == 0L, "open: Down selects the first item");
	key(&tester, KUI_KEY_ENTER, 0U);
	path_of("Documents/sub", expected, sizeof(expected));
	check(strcmp(chooser->folder, expected) == 0, "open: Enter goes into sub");
	check(chooser->answered == 0, "open: going into a folder answers nothing");
	key(&tester, KUI_KEY_BACKSPACE, 0U);
	check(strcmp(chooser->folder, folder) == 0, "open: Backspace goes up");

	/* A letter finds an item; the up button and Alt+Up go up. */
	type_text(&tester, "b");
	check(chooser->list.selected >= 0L && strcmp(chooser->entries[chooser->list.selected].name, "B.md") == 0, "open: typing b selects B.md");
	test_now += SECOND;
	click(&tester, UP_X, UP_Y);
	check(strcmp(chooser->folder, test_home) == 0, "open: the up button goes to the home folder");
	(void)keiui_chooser_go(chooser, folder);
	frame(&tester);
	key(&tester, KUI_KEY_UP, KUI_MOD_ALT);
	check(strcmp(chooser->folder, test_home) == 0, "open: Alt+Up goes up");

	/* A place: Documents. */
	test_now += SECOND;
	click(&tester, 60, PLACES_Y + 3 * 30 + 15);
	check(strcmp(chooser->folder, folder) == 0, "open: the Documents place");

	/* A letter and Enter between two frames: the letter selects Zeta first, then Enter goes into it. */
	(void)kui_ui_key(tester.ui, 44U, 1, 0U);
	(void)kui_ui_key(tester.ui, KUI_KEY_ENTER, 1, 0U);
	frame(&tester);
	path_of("Documents/Zeta", expected, sizeof(expected));
	check(strcmp(chooser->folder, expected) == 0, "open: z then Enter goes into Zeta, in order");
	(void)keiui_chooser_go(chooser, folder);
	frame(&tester);

	/* One click selects a file; a second one soon after opens it. */
	index = find(chooser, "a.txt");
	test_now += SECOND;
	click(&tester, LIST_X + 60, row_y(&tester, index));
	check(chooser->list.selected == index && chooser->answered == 0, "open: one click selects");
	check(keiui_chooser_can_accept(chooser) == 1, "open: a file selected can be opened");
	click(&tester, LIST_X + 60, row_y(&tester, index));
	path_of("Documents/a.txt", expected, sizeof(expected));
	check(chooser->answered == 1 && chooser->result == KUI_FILE_CHOOSER_CHOSEN, "open: double click chooses");
	check(strcmp(chooser->answer, expected) == 0, "open: the path of a.txt");
	key(&tester, KUI_KEY_ESC, 0U);
	check(chooser->result == KUI_FILE_CHOOSER_CHOSEN, "open: nothing after the answer");
	finish(&tester);

	/* Clicks too far apart are two single clicks; a tap on a folder goes into it; a double tap chooses. */
	error = start(&tester, &options, TEST_WIDTH, TEST_HEIGHT);
	chooser = &tester.chooser;
	check(error == 0, "open: init again");
	index = find(chooser, "a.txt");
	click(&tester, LIST_X + 60, row_y(&tester, index));
	test_now += SECOND;
	click(&tester, LIST_X + 60, row_y(&tester, index));
	check(chooser->answered == 0, "open: slow clicks do not open");
	index = find(chooser, "sub");
	test_now += SECOND;
	tap(&tester, LIST_X + 60, row_y(&tester, index));
	path_of("Documents/sub", expected, sizeof(expected));
	check(strcmp(chooser->folder, expected) == 0, "open: a tap on a folder goes into it");
	index = find(chooser, "inner.txt");
	test_now += SECOND;
	tap(&tester, LIST_X + 60, row_y(&tester, index));
	check(chooser->list.selected == index && chooser->answered == 0, "open: a tap on a file selects it");
	test_now += 100000U;
	tap(&tester, LIST_X + 60, row_y(&tester, index));
	check(chooser->answered == 1 && chooser->result == KUI_FILE_CHOOSER_CHOSEN, "open: a double tap chooses");
	finish(&tester);

	/* Esc cancels with no path. */
	error = start(&tester, &options, TEST_WIDTH, TEST_HEIGHT);
	chooser = &tester.chooser;
	key(&tester, KUI_KEY_ESC, 0U);
	check(error == 0 && chooser->answered == 1 && chooser->result == KUI_FILE_CHOOSER_CANCELLED && chooser->answer[0] == '\0', "open: Esc cancels");
	finish(&tester);

	/* Recent lists the files added that pass the filter. */
	path_of("Documents/a.txt", expected, sizeof(expected));
	(void)keiland_recent_add(expected, "host");
	path_of("Documents/c.png", expected, sizeof(expected));
	(void)keiland_recent_add(expected, "host");
	error = start(&tester, &options, TEST_WIDTH, TEST_HEIGHT);
	chooser = &tester.chooser;
	test_now += SECOND;
	click(&tester, 60, PLACES_Y + 15);
	check(error == 0 && chooser->recent == 1, "open: the Recent place");
	check(chooser->count == 1U && strcmp(chooser->entries[0].name, "a.txt") == 0, "open: Recent shows a.txt only (c.png is filtered)");
	check(keiui_chooser_can_go_up(chooser) == 0, "open: nothing above Recent");
	finish(&tester);

	/* Bad options are refused. */
	options.filter = 2;
	error = start(&tester, &options, TEST_WIDTH, TEST_HEIGHT);
	check(error == EINVAL, "open: a filter past the filters is refused");
	options.filter = 0;
	options.mode = 7;
	error = start(&tester, &options, TEST_WIDTH, TEST_HEIGHT);
	check(error == EINVAL, "open: an unknown mode is refused");
}

/* Save: the name, the checks, the question before replacing. */
static void
test_save(void)
{
	static const struct kui_file_filter filters[] = {
		{ "Text Files", "txt md" },
		{ "All Files", NULL }
	};
	struct kui_file_chooser_options options;
	struct tester tester;
	struct keiui_chooser *chooser;
	char folder[1024];
	char expected[1024];
	char locked[1024];
	uid_t root;
	double x;
	double y;
	int error;

	/* A chooser at Documents with the name Untitled.txt, selected up to its extension. */
	memset(&options, 0, sizeof(options));
	options.mode = KUI_FILE_CHOOSER_SAVE;
	path_of("Documents", folder, sizeof(folder));
	options.folder = folder;
	options.name = "Untitled.txt";
	options.filters = filters;
	options.filter_count = 2;
	error = start(&tester, &options, TEST_WIDTH, TEST_HEIGHT);
	chooser = &tester.chooser;
	check(error == 0, "save: init");
	check(strcmp(chooser->title, "Save As") == 0, "save: title");
	check(kui_ui_has_focus(tester.ui, KEIUI_CHOOSER_ID_NAME, 0U), "save: the name has the keyboard");
	check(chooser->name.anchor == 0U && chooser->name.caret == 8U, "save: the name is selected up to .txt");
	check(chooser->place_count == 4U, "save: no Recent place");

	/* Typing replaces the selection; Enter saves a new file. */
	type_text(&tester, "notes");
	check(strcmp(chooser->name.text, "notes.txt") == 0, "save: typed over the selection");
	key(&tester, KUI_KEY_ENTER, 0U);
	path_of("Documents/notes.txt", expected, sizeof(expected));
	check(chooser->answered == 1 && strcmp(chooser->answer, expected) == 0, "save: Enter answers the new path");
	finish(&tester);

	/* A file that exists: clicking it takes its name, Save asks, Cancel keeps, Replace answers. */
	error = start(&tester, &options, TEST_WIDTH, TEST_HEIGHT);
	chooser = &tester.chooser;
	test_now += SECOND;
	click(&tester, LIST_X + 60, row_y(&tester, find(chooser, "a.txt")));
	check(error == 0 && strcmp(chooser->name.text, "a.txt") == 0, "save: a file clicked gives its name");
	test_now += SECOND;
	click(&tester, ACCEPT_X, BUTTON_Y);
	check(chooser->confirm == 1 && chooser->answered == 0, "save: replacing asks first");
	frame(&tester);
	check(kui_ui_has_focus(tester.ui, KEIUI_CHOOSER_ID_CONFIRM, 0xffffffffU), "save: the question has the keyboard");

	/* The question's Cancel (the dialog's buttons: Replace at the right, Cancel to its left). */
	x = 200.0 + (552.0 - 392.0) / 2.0 + 392.0 - 18.0 - (double)kui_button_width(&tester.style, "Replace") - 8.0 - (double)kui_button_width(&tester.style, "Cancel") / 2.0;
	y = 8.0 + (464.0 - 170.0) / 2.0 + 170.0 - 16.0 - 16.0;
	test_now += SECOND;
	click(&tester, (int)x, (int)y);
	check(chooser->confirm == 0 && chooser->answered == 0, "save: Cancel in the question keeps the file");
	key(&tester, KUI_KEY_ENTER, 0U);
	check(chooser->confirm == 1, "save: Enter asks again");
	frame(&tester);
	key(&tester, KUI_KEY_ESC, 0U);
	check(chooser->confirm == 0 && chooser->answered == 0, "save: Esc in the question keeps the file");
	key(&tester, KUI_KEY_ENTER, 0U);
	frame(&tester);
	x = 200.0 + (552.0 - 392.0) / 2.0 + 392.0 - 18.0 - (double)kui_button_width(&tester.style, "Replace") / 2.0;
	test_now += SECOND;
	click(&tester, (int)x, (int)y);
	path_of("Documents/a.txt", expected, sizeof(expected));
	check(chooser->answered == 1 && strcmp(chooser->answer, expected) == 0, "save: Replace answers the file's path");
	finish(&tester);

	/* The question's Enter replaces. */
	error = start(&tester, &options, TEST_WIDTH, TEST_HEIGHT);
	chooser = &tester.chooser;
	key(&tester, KEY_A, KUI_MOD_CTRL);
	type_text(&tester, "B.md");
	key(&tester, KUI_KEY_ENTER, 0U);
	frame(&tester);
	key(&tester, KUI_KEY_ENTER, 0U);
	check(error == 0 && chooser->answered == 1 && strstr(chooser->answer, "B.md") != NULL, "save: Enter in the question replaces");
	finish(&tester);

	/* A folder's name goes into it; a slash is refused; an empty name cannot be saved. */
	error = start(&tester, &options, TEST_WIDTH, TEST_HEIGHT);
	chooser = &tester.chooser;
	key(&tester, KEY_A, KUI_MOD_CTRL);
	type_text(&tester, "sub");
	key(&tester, KUI_KEY_ENTER, 0U);
	path_of("Documents/sub", expected, sizeof(expected));
	check(error == 0 && strcmp(chooser->folder, expected) == 0 && chooser->answered == 0, "save: a folder's name goes into it");
	check(chooser->name.length == 0U && keiui_chooser_can_accept(chooser) == 0, "save: an empty name cannot be saved");
	type_text(&tester, "a/b");
	key(&tester, KUI_KEY_ENTER, 0U);
	check(chooser->answered == 0 && chooser->message[0] != '\0', "save: a slash is refused with a message");
	key(&tester, KUI_KEY_BACKSPACE, 0U);
	check(strcmp(chooser->name.text, "a/") == 0 && chooser->message[0] == '\0', "save: Backspace erases, the message goes");

	/* A folder that cannot be written to is refused (unless run as root). */
	path_of("locked", locked, sizeof(locked));
	chmod(locked, 0555);
	(void)keiui_chooser_go(chooser, locked);
	frame(&tester);
	key(&tester, KEY_A, KUI_MOD_CTRL);
	type_text(&tester, "x.txt");
	key(&tester, KUI_KEY_ENTER, 0U);
	root = geteuid();
	if (root != 0)
		check(chooser->answered == 0 && strstr(chooser->message, "can't save") != NULL, "save: a folder that cannot be written is refused");
	chmod(locked, 0755);
	finish(&tester);

	/* The filter button beside the name, and the Cancel button. */
	error = start(&tester, &options, TEST_WIDTH, TEST_HEIGHT);
	chooser = &tester.chooser;
	test_now += SECOND;
	click(&tester, SAVE_FILTER_X, BUTTON_Y);
	check(error == 0 && chooser->filter == 1U, "save: the filter button");
	test_now += SECOND;
	click(&tester, CANCEL_X, BUTTON_Y);
	check(chooser->answered == 1 && chooser->result == KUI_FILE_CHOOSER_CANCELLED, "save: Cancel");
	finish(&tester);
}

/* The path typed after Ctrl+L. */
static void
test_path(void)
{
	struct kui_file_chooser_options options;
	struct tester tester;
	struct keiui_chooser *chooser;
	char expected[1024];
	int error;

	/* Open: ~/Desktop is gone to, then a file's path is chosen. */
	memset(&options, 0, sizeof(options));
	options.mode = KUI_FILE_CHOOSER_OPEN;
	error = start(&tester, &options, TEST_WIDTH, TEST_HEIGHT);
	chooser = &tester.chooser;
	check(error == 0 && strcmp(chooser->folder, test_home) == 0, "path: starts at home without a folder");
	key(&tester, KEY_L, KUI_MOD_CTRL);
	frame(&tester);
	check(chooser->typing_path && kui_ui_has_focus(tester.ui, KEIUI_CHOOSER_ID_PATH, 0U), "path: Ctrl+L opens the path field");
	key(&tester, KEY_A, KUI_MOD_CTRL);
	type_text(&tester, "~/Desktop");
	key(&tester, KUI_KEY_ENTER, 0U);
	frame(&tester);
	path_of("Desktop", expected, sizeof(expected));
	check(strcmp(chooser->folder, expected) == 0 && !chooser->typing_path, "path: ~/Desktop is shown");
	check(kui_ui_has_focus(tester.ui, KEIUI_CHOOSER_ID_LIST, KEIUI_CHOOSER_LIST_SELF), "path: the list has the keyboard again");
	test_now += SECOND;
	click(&tester, LOCATION_X, UP_Y);
	frame(&tester);
	check(chooser->typing_path, "path: a click on the location opens the field");
	type_text(&tester, "../Documents/B.md");
	key(&tester, KUI_KEY_ENTER, 0U);
	check(chooser->answered == 1 && strstr(chooser->answer, "Documents/B.md") != NULL, "path: a file's path is chosen");
	finish(&tester);

	/* Esc closes the field, and again cancels. */
	error = start(&tester, &options, TEST_WIDTH, TEST_HEIGHT);
	chooser = &tester.chooser;
	key(&tester, KEY_L, KUI_MOD_CTRL);
	frame(&tester);
	key(&tester, KUI_KEY_ESC, 0U);
	check(error == 0 && !chooser->typing_path && chooser->answered == 0, "path: Esc closes the field");
	frame(&tester);
	key(&tester, KUI_KEY_ESC, 0U);
	check(chooser->answered == 1 && chooser->result == KUI_FILE_CHOOSER_CANCELLED, "path: Esc again cancels");
	finish(&tester);

	/* Save: a path to a new file in another folder. */
	options.mode = KUI_FILE_CHOOSER_SAVE;
	error = start(&tester, &options, TEST_WIDTH, TEST_HEIGHT);
	chooser = &tester.chooser;
	key(&tester, KEY_L, KUI_MOD_CTRL);
	frame(&tester);
	type_text(&tester, "Documents/sub/new.txt");
	key(&tester, KUI_KEY_ENTER, 0U);
	path_of("Documents/sub/new.txt", expected, sizeof(expected));
	check(error == 0 && chooser->answered == 1 && strcmp(chooser->answer, expected) == 0, "path: Save to a typed path");
	finish(&tester);
}

/* Draws the window in its states into pictures. */
static void
test_pictures(void)
{
	static const struct kui_file_filter filters[] = {
		{ "Text Files", "txt md" },
		{ "All Files", NULL }
	};
	struct kui_file_chooser_options options;
	struct tester tester;
	char folder[1024];
	int error;

	/* Open at Documents: a file selected, the pointer over a place. */
	memset(&options, 0, sizeof(options));
	options.mode = KUI_FILE_CHOOSER_OPEN;
	path_of("Documents", folder, sizeof(folder));
	options.folder = folder;
	options.filters = filters;
	options.filter_count = 2;
	error = start(&tester, &options, TEST_WIDTH, TEST_HEIGHT);
	check(error == 0, "pictures: open");
	test_now += SECOND;
	click(&tester, LIST_X + 60, row_y(&tester, find(&tester.chooser, "a.txt")));
	(void)kui_ui_pointer_motion(tester.ui, 60.0, (double)(PLACES_Y + 30 + 15));
	frame(&tester);
	picture(&tester, "open");
	tester.style.glass = 1;
	frame(&tester);
	picture(&tester, "open-glass");
	finish(&tester);

	/* Save with its name, and the question. */
	options.mode = KUI_FILE_CHOOSER_SAVE;
	options.name = "Untitled.txt";
	error = start(&tester, &options, TEST_WIDTH, TEST_HEIGHT);
	frame(&tester);
	picture(&tester, "save");
	test_now += SECOND;
	click(&tester, LIST_X + 60, row_y(&tester, find(&tester.chooser, "a.txt")));
	test_now += SECOND;
	click(&tester, ACCEPT_X, BUTTON_Y);
	frame(&tester);
	picture(&tester, "replace");
	finish(&tester);

	/* A narrow window, a message, and the path field. */
	error = start(&tester, &options, TEST_NARROW_W, TEST_NARROW_H);
	key(&tester, KEY_A, KUI_MOD_CTRL);
	type_text(&tester, "/");
	key(&tester, KUI_KEY_ENTER, 0U);
	picture(&tester, "narrow");
	key(&tester, KEY_L, KUI_MOD_CTRL);
	frame(&tester);
	picture(&tester, "path");
	check(error == 0, "pictures: save");
	finish(&tester);
}

/*
 * Writes the last frame as a PPM file (the premultiplied pixels over a sky
 * blue that stands for the desktop, frosted when it is on glass).
 */
static void
picture(
	struct tester *tester,
	const char *name)
{
	char path[1024];
	uint32_t pixel;
	unsigned alpha;
	unsigned channel;
	unsigned value;
	unsigned under[3];
	FILE *file;
	size_t index;

	/* The stand-in desktop. */
	under[0] = 0x9c;
	under[1] = 0xc4;
	under[2] = 0xe8;

	/* The picture: each pixel over it. */
	snprintf(path, sizeof(path), "%s-%s.ppm", test_prefix, name);
	file = fopen(path, "wb");
	if (file == NULL) {
		check(0, name);
		return;
	}

	/* The header. */
	fprintf(file, "P6\n%d %d\n255\n", tester->width, tester->height);
	for (index = 0; index < (size_t)tester->width * (size_t)tester->height; index++) {
		pixel = tester->pixels[index];
		alpha = pixel >> 24;
		for (channel = 0; channel < 3U; channel++) {
			value = (pixel >> (16U - channel * 8U)) & 0xffU;
			value += under[channel] * (255U - alpha) / 255U;
			if (tester->style.glass)
				value = (value * 3U + 255U) / 4U;
			fputc((int)value, file);
		}
	}

	/* Written. */
	fclose(file);
	check(1, name);
}
