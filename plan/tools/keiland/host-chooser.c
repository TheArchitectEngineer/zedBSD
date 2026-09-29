/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host tests of the file chooser's model and drawing (ws092-p003): a tree
 * of folders and files under a temporary home, the keys, clicks and taps
 * a user makes, the answers they lead to, and pictures of the window
 * (written as PPM, turned into PNG by host-chooser.sh).
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

/* The tests run and failed so far. */
static int test_count;
static int test_failed;

/* The temporary home and the fonts. */
static char test_home[512];
static const char *test_font;
static const char *test_fallback;
static const char *test_prefix;

static void check(int condition, const char *what);
static void make_file(const char *relative, const char *contents);
static void make_folder(const char *relative);
static void path_of(const char *relative, char *out, size_t size);
static int find(const struct kl_chooser *chooser, const char *name);
static void click_row(struct kl_chooser *chooser, int index, uint64_t time_ms);
static void click_part(struct kl_chooser *chooser, const struct kl_rect *rect, uint64_t time_ms);
static void type_text(struct kl_chooser *chooser, const char *text);
static void picture(struct kl_chooser *chooser, const char *name, int glass);
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

	/* The fonts and where pictures go. */
	if (argc != 4) {
		fprintf(stderr, "usage: host-chooser FONT FALLBACK OUTPUT-PREFIX\n");
		return 2;
	}
	test_font = argv[1];
	test_fallback = argv[2];
	test_prefix = argv[3];

	/* A home of its own, with the usual folders and some files. */
	snprintf(test_home, sizeof(test_home), "/tmp/host-chooser-XXXXXX");
	made = mkdtemp(test_home);
	if (made == NULL) {
		perror("mkdtemp");
		return 2;
	}
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
	test_open();
	test_save();
	test_path();
	test_pictures();

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

/* Reports the index of an item by its name, or -1. */
static int
find(
	const struct kl_chooser *chooser,
	const char *name)
{
	size_t index;
	int same;

	/* Each item. */
	for (index = 0; index < chooser->count; index++) {
		same = strcmp(chooser->entries[index].name, name);
		if (same == 0)
			return (int)index;
	}

	/* Not listed. */
	return -1;
}

/* Clicks the middle of an item's row. */
static void
click_row(
	struct kl_chooser *chooser,
	int index,
	uint64_t time_ms)
{
	int x;
	int y;

	/* The row's middle, scrolled. */
	x = chooser->layout.list.x + 60;
	y = chooser->layout.list.y + index * KL_CHOOSER_ROW + KL_CHOOSER_ROW / 2 - (int)chooser->scroll;
	(void)kl_chooser_click(chooser, x, y, time_ms);
}

/* Clicks the middle of a part. */
static void
click_part(
	struct kl_chooser *chooser,
	const struct kl_rect *rect,
	uint64_t time_ms)
{
	/* The part's middle. */
	(void)kl_chooser_click(chooser, rect->x + rect->width / 2, rect->y + rect->height / 2, time_ms);
}

/* Types ASCII text through the keys' characters. */
static void
type_text(
	struct kl_chooser *chooser,
	const char *text)
{
	/* Each character as a key that types it (the key's code does not matter to a field). */
	while (*text != '\0') {
		(void)kl_chooser_key(chooser, 200U, (uint32_t)(unsigned char)*text);
		text++;
	}
}

/* Open: listing, sorting, hidden items, filters, places, keys, clicks and taps. */
static void
test_open(void)
{
	static const struct keiland_file_filter filters[] = {
		{ "Text Files", "txt md" },
		{ "All Files", NULL }
	};
	struct keiland_file_chooser_options options;
	struct kl_chooser chooser;
	char folder[1024];
	char expected[1024];
	int index;
	int error;

	/* A chooser at Documents with the text filter. */
	memset(&options, 0, sizeof(options));
	options.mode = KEILAND_FILE_CHOOSER_OPEN;
	path_of("Documents", folder, sizeof(folder));
	options.folder = folder;
	options.filters = filters;
	options.filter_count = 2;
	options.filter = 0;
	error = kl_chooser_init(&chooser, &options);
	check(error == 0, "open: init");
	check(strcmp(chooser.folder, folder) == 0, "open: starts in the folder given");
	check(strcmp(chooser.title, "Open") == 0, "open: title");

	/* Folders first, then names without regard to case; hidden and filtered items left out. */
	check(chooser.count == 5U, "open: five items (two folders, three text files)");
	check(chooser.count >= 5U && strcmp(chooser.entries[0].name, "sub") == 0, "open: sub first");
	check(chooser.count >= 5U && strcmp(chooser.entries[1].name, "Zeta") == 0, "open: Zeta second");
	check(chooser.count >= 5U && strcmp(chooser.entries[2].name, "a.txt") == 0, "open: a.txt");
	check(chooser.count >= 5U && strcmp(chooser.entries[3].name, "B.md") == 0, "open: B.md (case ignored)");
	check(find(&chooser, "c.png") < 0, "open: c.png filtered out");
	check(find(&chooser, ".hidden.txt") < 0, "open: hidden file left out");

	/* The places: Recent, Home, Desktop, Documents, Computer (no Downloads). */
	check(chooser.place_count == 5U, "open: five places");
	check(strcmp(chooser.places[0].label, "Recent") == 0, "open: Recent first");
	check(strcmp(chooser.places[4].label, "Computer") == 0, "open: Computer last");

	/* Ctrl+H shows the hidden items, and again hides them. */
	chooser.modifiers = KL_MOD_CTRL;
	(void)kl_chooser_key(&chooser, KL_KEY_H, 0U);
	check(find(&chooser, ".hidden.txt") >= 0, "open: Ctrl+H shows hidden files");
	check(find(&chooser, ".secret") >= 0, "open: Ctrl+H shows hidden folders");
	(void)kl_chooser_key(&chooser, KL_KEY_H, 0U);
	check(find(&chooser, ".hidden.txt") < 0, "open: Ctrl+H again hides them");
	chooser.modifiers = 0;

	/* The filter pill: All Files shows the picture. */
	click_part(&chooser, &chooser.layout.filter, 1000U);
	check(chooser.filter == 1U, "open: filter clicked to All Files");
	check(find(&chooser, "c.png") >= 0, "open: All Files shows c.png");

	/* Nothing selected: Open cannot be pressed. */
	check(kl_chooser_can_accept(&chooser) == 0, "open: nothing to open yet");

	/* Down selects the first item, Enter goes into it, Backspace comes back. */
	(void)kl_chooser_key(&chooser, KL_KEY_DOWN, 0U);
	check(chooser.selected == 0, "open: Down selects the first item");
	(void)kl_chooser_key(&chooser, KL_KEY_ENTER, 0U);
	path_of("Documents/sub", expected, sizeof(expected));
	check(strcmp(chooser.folder, expected) == 0, "open: Enter goes into sub");
	check(chooser.answered == 0, "open: going into a folder answers nothing");
	(void)kl_chooser_key(&chooser, KL_KEY_BACKSPACE, 0U);
	check(strcmp(chooser.folder, folder) == 0, "open: Backspace goes up");

	/* A letter finds an item; the up button and Alt+Up go up. */
	(void)kl_chooser_key(&chooser, 48U, 'b');
	check(chooser.selected >= 0 && strcmp(chooser.entries[chooser.selected].name, "B.md") == 0, "open: typing b selects B.md");
	click_part(&chooser, &chooser.layout.up, 5000U);
	check(strcmp(chooser.folder, test_home) == 0, "open: the up button goes to the home folder");
	(void)kl_chooser_go(&chooser, folder);
	chooser.modifiers = KL_MOD_ALT;
	(void)kl_chooser_key(&chooser, KL_KEY_UP, 0U);
	chooser.modifiers = 0;
	check(strcmp(chooser.folder, test_home) == 0, "open: Alt+Up goes up");

	/* A place: Documents. */
	click_part(&chooser, &(struct kl_rect){ chooser.layout.sidebar.x + 20, chooser.layout.places_top + 3 * KL_CHOOSER_ROW_PLACE, 10, 10 }, 7000U);
	check(strcmp(chooser.folder, folder) == 0, "open: the Documents place");

	/* One click selects a file; a second one soon after opens it. */
	index = find(&chooser, "a.txt");
	click_row(&chooser, index, 10000U);
	check(chooser.selected == index && chooser.answered == 0, "open: one click selects");
	check(kl_chooser_can_accept(&chooser) == 1, "open: a file selected can be opened");
	click_row(&chooser, index, 10200U);
	path_of("Documents/a.txt", expected, sizeof(expected));
	check(chooser.answered == 1 && chooser.result == KEILAND_FILE_CHOOSER_CHOSEN, "open: double click chooses");
	check(strcmp(chooser.answer, expected) == 0, "open: the path of a.txt");
	check(kl_chooser_key(&chooser, KL_KEY_ESC, 0U) == 0, "open: nothing after the answer");
	kl_chooser_fini(&chooser);

	/* Clicks too far apart are two single clicks; a tap on a folder goes into it; Esc cancels. */
	error = kl_chooser_init(&chooser, &options);
	check(error == 0, "open: init again");
	index = find(&chooser, "a.txt");
	click_row(&chooser, index, 10000U);
	click_row(&chooser, index, 11000U);
	check(chooser.answered == 0, "open: slow clicks do not open");
	index = find(&chooser, "sub");
	(void)kl_chooser_tap(&chooser, chooser.layout.list.x + 60, chooser.layout.list.y + index * KL_CHOOSER_ROW + 10, 0);
	path_of("Documents/sub", expected, sizeof(expected));
	check(strcmp(chooser.folder, expected) == 0, "open: a tap on a folder goes into it");
	index = find(&chooser, "inner.txt");
	(void)kl_chooser_tap(&chooser, chooser.layout.list.x + 60, chooser.layout.list.y + index * KL_CHOOSER_ROW + 10, 0);
	check(chooser.selected == index && chooser.answered == 0, "open: a tap on a file selects it");
	(void)kl_chooser_tap(&chooser, chooser.layout.list.x + 60, chooser.layout.list.y + index * KL_CHOOSER_ROW + 10, 1);
	check(chooser.answered == 1 && chooser.result == KEILAND_FILE_CHOOSER_CHOSEN, "open: a double tap chooses");
	kl_chooser_fini(&chooser);

	/* Esc cancels with no path. */
	error = kl_chooser_init(&chooser, &options);
	(void)kl_chooser_key(&chooser, KL_KEY_ESC, 0U);
	check(error == 0 && chooser.answered == 1 && chooser.result == KEILAND_FILE_CHOOSER_CANCELLED && chooser.answer[0] == '\0', "open: Esc cancels");
	kl_chooser_fini(&chooser);

	/* Recent lists the files added that pass the filter. */
	path_of("Documents/a.txt", expected, sizeof(expected));
	(void)keiland_recent_add(expected, "host");
	path_of("Documents/c.png", expected, sizeof(expected));
	(void)keiland_recent_add(expected, "host");
	error = kl_chooser_init(&chooser, &options);
	click_part(&chooser, &(struct kl_rect){ chooser.layout.sidebar.x + 20, chooser.layout.places_top, 10, 10 }, 1000U);
	check(error == 0 && chooser.recent == 1, "open: the Recent place");
	check(chooser.count == 1U && strcmp(chooser.entries[0].name, "a.txt") == 0, "open: Recent shows a.txt only (c.png is filtered)");
	kl_chooser_fini(&chooser);

	/* Bad options are refused. */
	options.filter = 2;
	error = kl_chooser_init(&chooser, &options);
	check(error == EINVAL, "open: a filter past the filters is refused");
	options.filter = 0;
	options.mode = 7;
	error = kl_chooser_init(&chooser, &options);
	check(error == EINVAL, "open: an unknown mode is refused");
}

/* Save: the name, the checks, the question before replacing. */
static void
test_save(void)
{
	struct keiland_file_chooser_options options;
	struct kl_chooser chooser;
	char folder[1024];
	char expected[1024];
	char locked[1024];
	int error;

	/* A chooser at Documents with the name Untitled.txt, selected up to its extension. */
	memset(&options, 0, sizeof(options));
	options.mode = KEILAND_FILE_CHOOSER_SAVE;
	path_of("Documents", folder, sizeof(folder));
	options.folder = folder;
	options.name = "Untitled.txt";
	error = kl_chooser_init(&chooser, &options);
	check(error == 0, "save: init");
	check(strcmp(chooser.title, "Save As") == 0, "save: title");
	check(chooser.focus == KL_FOCUS_NAME, "save: the name has the keyboard");
	check(chooser.name.anchor == 0U && chooser.name.cursor == 8U, "save: the name is selected up to .txt");
	check(chooser.place_count == 4U, "save: no Recent place");

	/* Typing replaces the selection; Enter saves a new file. */
	type_text(&chooser, "notes");
	check(strcmp(chooser.name.text, "notes.txt") == 0, "save: typed over the selection");
	(void)kl_chooser_key(&chooser, KL_KEY_ENTER, 0U);
	path_of("Documents/notes.txt", expected, sizeof(expected));
	check(chooser.answered == 1 && strcmp(chooser.answer, expected) == 0, "save: Enter answers the new path");
	kl_chooser_fini(&chooser);

	/* A file that exists: clicking it takes its name, Save asks, Cancel keeps, Replace answers. */
	error = kl_chooser_init(&chooser, &options);
	click_row(&chooser, find(&chooser, "a.txt"), 1000U);
	check(error == 0 && strcmp(chooser.name.text, "a.txt") == 0, "save: a file clicked gives its name");
	click_part(&chooser, &chooser.layout.accept, 5000U);
	check(chooser.confirm == 1 && chooser.answered == 0, "save: replacing asks first");
	click_part(&chooser, &chooser.layout.keep, 6000U);
	check(chooser.confirm == 0 && chooser.answered == 0, "save: Cancel in the question keeps the file");
	(void)kl_chooser_key(&chooser, KL_KEY_ENTER, 0U);
	check(chooser.confirm == 1, "save: Enter asks again");
	(void)kl_chooser_key(&chooser, KL_KEY_ESC, 0U);
	check(chooser.confirm == 0 && chooser.answered == 0, "save: Esc in the question keeps the file");
	(void)kl_chooser_key(&chooser, KL_KEY_ENTER, 0U);
	click_part(&chooser, &chooser.layout.replace, 7000U);
	path_of("Documents/a.txt", expected, sizeof(expected));
	check(chooser.answered == 1 && strcmp(chooser.answer, expected) == 0, "save: Replace answers the file's path");
	kl_chooser_fini(&chooser);

	/* A folder's name goes into it; a slash is refused; an empty name cannot be saved. */
	error = kl_chooser_init(&chooser, &options);
	chooser.modifiers = KL_MOD_CTRL;
	(void)kl_chooser_key(&chooser, KL_KEY_A, 0U);
	chooser.modifiers = 0;
	type_text(&chooser, "sub");
	(void)kl_chooser_key(&chooser, KL_KEY_ENTER, 0U);
	path_of("Documents/sub", expected, sizeof(expected));
	check(error == 0 && strcmp(chooser.folder, expected) == 0 && chooser.answered == 0, "save: a folder's name goes into it");
	check(chooser.name.length == 0U && kl_chooser_can_accept(&chooser) == 0, "save: an empty name cannot be saved");
	type_text(&chooser, "a/b");
	(void)kl_chooser_key(&chooser, KL_KEY_ENTER, 0U);
	check(chooser.answered == 0 && chooser.message[0] != '\0', "save: a slash is refused with a message");
	(void)kl_chooser_key(&chooser, KL_KEY_BACKSPACE, 0U);
	check(strcmp(chooser.name.text, "a/") == 0 && chooser.message[0] == '\0', "save: Backspace erases, the message goes");

	/* A folder that cannot be written to is refused (unless run as root). */
	path_of("locked", locked, sizeof(locked));
	chmod(locked, 0555);
	(void)kl_chooser_go(&chooser, locked);
	chooser.modifiers = KL_MOD_CTRL;
	(void)kl_chooser_key(&chooser, KL_KEY_A, 0U);
	chooser.modifiers = 0;
	type_text(&chooser, "x.txt");
	(void)kl_chooser_key(&chooser, KL_KEY_ENTER, 0U);
	if (geteuid() != 0)
		check(chooser.answered == 0 && strstr(chooser.message, "can't save") != NULL, "save: a folder that cannot be written is refused");
	chmod(locked, 0755);
	kl_chooser_fini(&chooser);

	/* The Cancel button. */
	error = kl_chooser_init(&chooser, &options);
	click_part(&chooser, &chooser.layout.cancel, 1000U);
	check(error == 0 && chooser.answered == 1 && chooser.result == KEILAND_FILE_CHOOSER_CANCELLED, "save: Cancel");
	kl_chooser_fini(&chooser);
}

/* The path typed after Ctrl+L. */
static void
test_path(void)
{
	struct keiland_file_chooser_options options;
	struct kl_chooser chooser;
	char expected[1024];
	int error;

	/* Open: ~/Desktop is gone to, then a file's path is chosen. */
	memset(&options, 0, sizeof(options));
	options.mode = KEILAND_FILE_CHOOSER_OPEN;
	error = kl_chooser_init(&chooser, &options);
	check(error == 0 && strcmp(chooser.folder, test_home) == 0, "path: starts at home without a folder");
	chooser.modifiers = KL_MOD_CTRL;
	(void)kl_chooser_key(&chooser, KL_KEY_L, 0U);
	chooser.modifiers = 0;
	check(chooser.focus == KL_FOCUS_PATH, "path: Ctrl+L opens the path field");
	chooser.modifiers = KL_MOD_CTRL;
	(void)kl_chooser_key(&chooser, KL_KEY_A, 0U);
	chooser.modifiers = 0;
	type_text(&chooser, "~/Desktop");
	(void)kl_chooser_key(&chooser, KL_KEY_ENTER, 0U);
	path_of("Desktop", expected, sizeof(expected));
	check(strcmp(chooser.folder, expected) == 0 && chooser.focus == KL_FOCUS_LIST, "path: ~/Desktop is shown");
	click_part(&chooser, &chooser.layout.location, 1000U);
	check(chooser.focus == KL_FOCUS_PATH, "path: a click on the location opens the field");
	type_text(&chooser, "../Documents/B.md");
	(void)kl_chooser_key(&chooser, KL_KEY_ENTER, 0U);
	check(chooser.answered == 1 && strstr(chooser.answer, "Documents/B.md") != NULL, "path: a file's path is chosen");
	kl_chooser_fini(&chooser);

	/* Save: a path to a new file in another folder. */
	options.mode = KEILAND_FILE_CHOOSER_SAVE;
	error = kl_chooser_init(&chooser, &options);
	chooser.modifiers = KL_MOD_CTRL;
	(void)kl_chooser_key(&chooser, KL_KEY_L, 0U);
	chooser.modifiers = 0;
	type_text(&chooser, "Documents/sub/new.txt");
	(void)kl_chooser_key(&chooser, KL_KEY_ENTER, 0U);
	path_of("Documents/sub/new.txt", expected, sizeof(expected));
	check(error == 0 && chooser.answered == 1 && strcmp(chooser.answer, expected) == 0, "path: Save to a typed path");
	kl_chooser_fini(&chooser);
}

/* Draws the window in its states into pictures. */
static void
test_pictures(void)
{
	static const struct keiland_file_filter filters[] = {
		{ "Text Files", "txt md" },
		{ "All Files", NULL }
	};
	struct keiland_file_chooser_options options;
	struct kl_chooser chooser;
	char folder[1024];
	int error;

	/* Open at Documents: the second file selected, the pointer over a place. */
	memset(&options, 0, sizeof(options));
	options.mode = KEILAND_FILE_CHOOSER_OPEN;
	path_of("Documents", folder, sizeof(folder));
	options.folder = folder;
	options.filters = filters;
	options.filter_count = 2;
	error = kl_chooser_init(&chooser, &options);
	check(error == 0, "pictures: open");
	chooser.focused = 1;
	click_row(&chooser, find(&chooser, "a.txt"), 1000U);
	(void)kl_chooser_motion(&chooser, chooser.layout.sidebar.x + 30, chooser.layout.places_top + KL_CHOOSER_ROW_PLACE + 5);
	picture(&chooser, "open", 0);
	picture(&chooser, "open-glass", 1);
	kl_chooser_fini(&chooser);

	/* Save with its name, and the question. */
	options.mode = KEILAND_FILE_CHOOSER_SAVE;
	options.name = "Untitled.txt";
	error = kl_chooser_init(&chooser, &options);
	chooser.focused = 1;
	picture(&chooser, "save", 0);
	click_row(&chooser, find(&chooser, "a.txt"), 1000U);
	click_part(&chooser, &chooser.layout.accept, 5000U);
	picture(&chooser, "replace", 0);
	kl_chooser_fini(&chooser);

	/* A narrow window, a message, and the path field. */
	error = kl_chooser_init(&chooser, &options);
	chooser.focused = 1;
	kl_chooser_resize(&chooser, 540, 360);
	type_text(&chooser, "/");
	(void)kl_chooser_key(&chooser, KL_KEY_ENTER, 0U);
	picture(&chooser, "narrow", 0);
	chooser.modifiers = KL_MOD_CTRL;
	(void)kl_chooser_key(&chooser, KL_KEY_L, 0U);
	chooser.modifiers = 0;
	picture(&chooser, "path", 0);
	check(error == 0, "pictures: save");
	kl_chooser_fini(&chooser);
}

/*
 * Draws the chooser and writes it as a PPM file (the premultiplied pixels
 * over a sky blue that stands for the desktop when it is on glass).
 */
static void
picture(
	struct kl_chooser *chooser,
	const char *name,
	int glass)
{
	struct kl_canvas canvas;
	struct kl_text text;
	char path[1024];
	uint32_t *pixels;
	uint32_t pixel;
	unsigned alpha;
	unsigned channel;
	unsigned value;
	unsigned under[3];
	FILE *file;
	size_t index;
	int error;

	/* The fonts. */
	error = kl_text_open(&text, test_font, test_fallback);
	if (error != 0) {
		printf("FAIL: font %s: %s\n", test_font, strerror(error));
		test_failed++;
		return;
	}

	/* The frame. */
	pixels = calloc((size_t)chooser->width * (size_t)chooser->height, sizeof(pixels[0]));
	kl_paint_init(&canvas, pixels, chooser->width, chooser->height, (size_t)chooser->width);
	chooser->glass = glass;
	kl_chooser_draw(chooser, &text, &canvas);

	/* The picture: each pixel over the stand-in desktop. */
	under[0] = 0x9c;
	under[1] = 0xc4;
	under[2] = 0xe8;
	snprintf(path, sizeof(path), "%s-%s.ppm", test_prefix, name);
	file = fopen(path, "wb");
	fprintf(file, "P6\n%d %d\n255\n", chooser->width, chooser->height);
	for (index = 0; index < (size_t)chooser->width * (size_t)chooser->height; index++) {
		pixel = pixels[index];
		alpha = pixel >> 24;
		for (channel = 0; channel < 3U; channel++) {
			value = (pixel >> (16U - channel * 8U)) & 0xffU;
			value += under[channel] * (255U - alpha) / 255U;
			if (glass)
				value = (value * 3U + 255U) / 4U;
			fputc((int)value, file);
		}
	}
	fclose(file);
	free(pixels);
	kl_text_close(&text);
	check(1, name);
}
