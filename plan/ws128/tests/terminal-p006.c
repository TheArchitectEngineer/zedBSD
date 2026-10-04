/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of ws128-p006: Terminal's Edit > Find (search.c) over the scrollback and the screen, and the
 * settings kept for the font's size and the theme (settings.c, ~/.config/keiland/terminal.conf under a scratch HOME).
 * It links the terminal's screen.c, width.c, search.c and settings.c, with libkeiland's settings under the host
 * stand-in plan/tools/settings/host-kl-settings.c (WS135).
 */

#include "userland/desktop/terminal/terminal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The grid under test; file-scope because it is too large for the stack. */
static struct terminal_screen test_screen;

/* How many checks failed. */
static unsigned test_failures;

static void test_expect(int condition, const char *what);
static void test_write(const char *text);

int
main(void)
{
	struct terminal_settings settings;
	unsigned char marks[TERMINAL_MAX_COLUMNS];
	uint32_t query[TERMINAL_SEARCH_LENGTH];
	char line_text[64];
	char path[1024];
	char text[512];
	unsigned long line;
	unsigned long first_line;
	unsigned column;
	unsigned cells;
	size_t count;
	size_t length;
	FILE *file;
	int found;
	int index;

	/* 40 lines on a 20 x 5 grid: 36 go to the scrollback; "needle" on lines 3, 20 and 38 (line 38 on the screen). */
	terminal_screen_init(&test_screen, 20U, 5U);
	for (index = 0; index < 40; index++) {
		if (index == 3 || index == 20 || index == 38)
			snprintf(line_text, sizeof(line_text), "row %d Needle x\r\n", index);
		else
			snprintf(line_text, sizeof(line_text), "row %d plain\r\n", index);
		test_write(line_text);
	}
	test_expect(test_screen.history_count == 36U, "screen: 36 lines in the scrollback");
	first_line = test_screen.scrolled - test_screen.history_count;

	/* The text, case folded. */
	count = terminal_search_decode("NEEDLE", 6U, query, TERMINAL_SEARCH_LENGTH);
	test_expect(count == 6U && query[0] == 'n', "decode: six characters, folded");

	/* Back from below the screen: the newest match first (line 38). */
	found = terminal_search_find(&test_screen, query, count, test_screen.scrolled + test_screen.rows, 0U, 1, &line, &column, &cells);
	test_expect(found && line == first_line + 38U && column == 7U && cells == 6U, "find: the newest match on the screen");

	/* Back again: line 20, then line 3, then nothing. */
	found = terminal_search_find(&test_screen, query, count, line, column, 1, &line, &column, &cells);
	test_expect(found && line == first_line + 20U, "find: the next older match in the scrollback");
	found = terminal_search_find(&test_screen, query, count, line, column, 1, &line, &column, &cells);
	test_expect(found && line == first_line + 3U, "find: the oldest match");
	found = terminal_search_find(&test_screen, query, count, line, column, 1, &line, &column, &cells);
	test_expect(!found, "find: none older than the oldest");

	/* Forward from line 3: line 20. */
	found = terminal_search_find(&test_screen, query, count, first_line + 3U, 7U, -1, &line, &column, &cells);
	test_expect(found && line == first_line + 20U, "find: the next newer match");

	/* Showing line 20 moves the view back into the scrollback so that it is in the window. */
	terminal_search_show(&test_screen, first_line + 20U);
	test_expect(test_screen.view != 0U &&
		    first_line + 20U >= test_screen.scrolled - test_screen.view &&
		    first_line + 20U < test_screen.scrolled - test_screen.view + test_screen.rows,
		    "show: the match is in the window");

	/* The marks of a line: the six cells of the match. */
	found = terminal_search_line(&test_screen, query, count, first_line + 20U, marks);
	test_expect(found == 1 && marks[6] == 0 && marks[7] == 1 && marks[12] == 1 && marks[13] == 0, "marks: the match's cells");

	/* A text that is nowhere. */
	count = terminal_search_decode("absent", 6U, query, TERMINAL_SEARCH_LENGTH);
	found = terminal_search_find(&test_screen, query, count, test_screen.scrolled + test_screen.rows, 0U, 1, &line, &column, &cells);
	test_expect(!found, "find: a text that is nowhere");

	/* A wide character is found as one character. */
	terminal_screen_init(&test_screen, 20U, 5U);
	test_write("ab \xe6\x97\xa5\xe6\x9c\xac x\r\n");
	count = terminal_search_decode("\xe6\x97\xa5\xe6\x9c\xac", 6U, query, TERMINAL_SEARCH_LENGTH);
	found = terminal_search_find(&test_screen, query, count, test_screen.scrolled + test_screen.rows, 0U, 1, &line, &column, &cells);
	test_expect(count == 2U && found && column == 3U && cells == 4U, "find: two wide characters cover four cells");

	/* The settings: none kept is the default size (16, the desktop's table) and the dark theme. */
	terminal_settings_load(&settings);
	test_expect(settings.font_size == 16U && settings.theme == TERMINAL_THEME_DARK, "settings: none kept");

	/* A size and the light theme are kept and read back. */
	memset(&settings, 0, sizeof(settings));
	settings.font_size = 20U;
	settings.theme = TERMINAL_THEME_LIGHT;
	test_expect(terminal_settings_save(&settings) == 0, "settings: save");
	memset(&settings, 0, sizeof(settings));
	terminal_settings_load(&settings);
	test_expect(settings.font_size == 20U && settings.theme == TERMINAL_THEME_LIGHT, "settings: read back");

	/* The dark theme leaves no line; the other keys stay. */
	settings.theme = TERMINAL_THEME_DARK;
	test_expect(terminal_settings_save(&settings) == 0, "settings: save dark");
	snprintf(path, sizeof(path), "%s/.config/keiland/terminal.conf", getenv("HOME"));
	file = fopen(path, "r");
	length = 0;
	if (file != NULL) {
		length = fread(text, 1U, sizeof(text) - 1U, file);
		fclose(file);
	}
	text[length] = '\0';
	test_expect(strstr(text, "font-size=20\n") != NULL && strstr(text, "theme=") == NULL, "settings: the dark theme leaves no line");

	/* Values out of the range are brought within it by libkeiland (the table's 8..32 and 0..2). */
	file = fopen(path, "w");
	if (file != NULL) {
		fputs("font-size=99\ntheme=7\n", file);
		fclose(file);
	}
	terminal_settings_load(&settings);
	test_expect(settings.font_size == TERMINAL_PIXELS_MAX && settings.theme == TERMINAL_THEME_CONTRAST, "settings: values out of range come within the table's range (libkeiland)");

	/* The default size and the dark theme leave the file without their lines. */
	settings.font_size = 16U;
	settings.theme = TERMINAL_THEME_DARK;
	settings.ambiguous_wide = 1;
	test_expect(terminal_settings_save(&settings) == 0, "settings: save the defaults");
	file = fopen(path, "r");
	length = 0;
	if (file != NULL) {
		length = fread(text, 1U, sizeof(text) - 1U, file);
		fclose(file);
	}
	text[length] = '\0';
	test_expect(strcmp(text, "ambiguous-wide=1\n") == 0, "settings: only the width setting's line is left");

	printf("terminal-p006: %s (%u failures)\n", test_failures == 0U ? "PASS" : "FAIL", test_failures);
	return test_failures == 0U ? 0 : 1;
}

/* Prints a check's outcome. */
static void
test_expect(
	int condition,
	const char *what)
{
	printf("%s: %s\n", condition ? "ok" : "FAIL", what);
	if (!condition)
		test_failures++;
}

/* Writes text on the grid as a shell would. */
static void
test_write(
	const char *text)
{
	terminal_screen_write(&test_screen, (const unsigned char *)text, strlen(text));
}
