/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws074-p045: the host test of browser's links: a link's target
 * resolved against the page's file, and the link under a point of a laid
 * out page.
 *
 *   host-link PAGES SANS MONO FALLBACK
 *
 * (PAGES: plan/ws074/tests/pages; the fonts: build/ws035-fonts/Inter.ttf,
 * JetBrainsMono-Regular.ttf, DroidSansFallbackFull.ttf).  Prints one line
 * per failed check and a summary.
 */

#include "page/page.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int checks;

static void check(int condition, const char *what);
static void resolves(const char *base, const char *href, const char *expected, int expected_error);
static const struct layout_fragment *find_fragment(const struct layout_box *box, const char *ascii, layout_unit *x, layout_unit *y);

int
main(
	int argc,
	char **argv)
{
	struct text_font_paths paths;
	struct wb_buffer href;
	struct page *page;
	const struct layout_fragment *fragment;
	char path[1024];
	layout_unit x;
	layout_unit y;
	int found;
	int error;

	/* ws074-p080: localStorage goes under XDG_DATA_HOME; the test keeps it under build/, away from ~/.local/share. */
	setenv("XDG_DATA_HOME", "build/ws074-host-data", 1);

	if (argc < 5) {
		fprintf(stderr, "usage: host-link PAGES SANS MONO FALLBACK\n");
		return 2;
	}

	/* Resolution against the page's file. */
	resolves("/usr/share/tests/first.html", "second.html", "/usr/share/tests/second.html", 0);
	resolves("/usr/share/tests/first.html", "./blocks.html#top", "/usr/share/tests/blocks.html", 0);
	resolves("/usr/share/tests/first.html", "../other/a.html?x=1", "/usr/share/other/a.html", 0);
	resolves("/usr/share/tests/first.html", "/etc/motd", "/etc/motd", 0);
	resolves("/usr/share/tests/first.html", "file:///tmp/a%20b.html", "/tmp/a b.html", 0);
	resolves("/usr/share/tests/first.html", "file://localhost/tmp/x.html", "/tmp/x.html", 0);
	resolves("/usr/share/tests/first.html", "#section", "/usr/share/tests/first.html", 0);
	resolves("/usr/share/tests/first.html", "../../../../../x.html", "/x.html", 0);
	resolves("/usr/share/tests/first.html", "https://example.com/", "", EPROTONOSUPPORT);
	resolves("/usr/share/tests/first.html", "mailto:someone@example.com", "", EPROTONOSUPPORT);
	resolves("/usr/share/tests/first.html", "a:b/c.html", "", EPROTONOSUPPORT);
	resolves("/usr/share/tests/first.html", "dir/a:b.html", "/usr/share/tests/dir/a:b.html", 0);

	/* first.html laid out at 800 by 600. */
	paths.sans = argv[2];
	paths.mono = argv[3];
	paths.fallback = argv[4];
	snprintf(path, sizeof(path), "%s/first.html", argv[1]);
	error = page_create(&page, __builtin_frame_address(0));
	check(error == 0, "page: made");
	if (error != 0)
		return 1;
	error = page_load_file(page, path);
	check(error == 0, "page: first.html loads");
	if (error == 0)
		error = page_open_fonts(page, &paths);
	if (error == 0)
		error = page_layout(page, 800, 600);
	check(error == 0, "page: laid out");
	if (error != 0)
		return 1;

	/* The middle of the word "link" is on the link to second.html. */
	fragment = find_fragment(page->layout.root, "link", &x, &y);
	check(fragment != NULL, "hit: the word link is on a line");
	if (fragment != NULL) {
		wb_buffer_init(&href);
		error = page_link_at(page, (int)((x + fragment->width / 2) / LAYOUT_UNIT), (int)(y / LAYOUT_UNIT), &href, &found);
		check(error == 0 && found == 1, "hit: a link is under the word link");
		check(found == 1 && strcmp(wb_buffer_string(&href), "second.html") == 0, "hit: its href is second.html");
		printf("host-link: link at %d,%d: %s\n", (int)((x + fragment->width / 2) / LAYOUT_UNIT), (int)(y / LAYOUT_UNIT),
		    wb_buffer_string(&href));
		wb_buffer_release(&href);
	}

	/* The word "note" before it is not a link. */
	fragment = find_fragment(page->layout.root, "note", &x, &y);
	check(fragment != NULL, "hit: the word note is on a line");
	if (fragment != NULL) {
		wb_buffer_init(&href);
		error = page_link_at(page, (int)((x + fragment->width / 2) / LAYOUT_UNIT), (int)(y / LAYOUT_UNIT), &href, &found);
		check(error == 0 && found == 0, "hit: no link under the word note");
		wb_buffer_release(&href);
	}

	/* A point in the margin is on no text. */
	wb_buffer_init(&href);
	error = page_link_at(page, 2, 2, &href, &found);
	check(error == 0 && found == 0, "hit: nothing in the margin");
	wb_buffer_release(&href);

	/* The title. */
	wb_buffer_init(&href);
	error = page_title(page, &href);
	check(error == 0 && strcmp(wb_buffer_string(&href), "browser: the first page") == 0, "title: first.html's");
	wb_buffer_release(&href);

	page_destroy(page);
	printf("host-link: %d checks, %d failed\n", checks, failures);
	return failures != 0;
}

/* Counts a check and reports it when it failed. */
static void
check(
	int condition,
	const char *what)
{
	checks++;
	if (condition)
		return;
	failures++;
	printf("FAIL %s\n", what);
}

/* Checks one resolution. */
static void
resolves(
	const char *base,
	const char *href,
	const char *expected,
	int expected_error)
{
	struct wb_buffer out;
	char what[512];
	int error;

	wb_buffer_init(&out);
	error = page_resolve_file(base, href, &out);
	snprintf(what, sizeof(what), "resolve: %s -> %s (got %s, error %d)", href, expected, wb_buffer_string(&out), error);
	if (expected_error != 0)
		check(error == expected_error, what);
	else
		check(error == 0 && strcmp(wb_buffer_string(&out), expected) == 0, what);
	wb_buffer_release(&out);
}

/* Finds the first fragment whose text is an ASCII word, with its top-left in the document. */
static const struct layout_fragment *
find_fragment(
	const struct layout_box *box,
	const char *ascii,
	layout_unit *x,
	layout_unit *y)
{
	const struct layout_box *child;
	const struct layout_fragment *found;
	const struct layout_fragment *fragment;
	const struct layout_line *line;
	size_t length;
	size_t index;
	size_t item;
	size_t unit;
	int same;

	length = strlen(ascii);
	if (box->children_inline) {
		for (index = 0; index < box->line_count; index++) {
			line = &box->lines[index];
			for (item = 0; item < line->fragment_count; item++) {
				fragment = &line->fragments[item];
				if (fragment->length != length)
					continue;
				same = 1;
				for (unit = 0; unit < length; unit++) {
					if (fragment->text[unit] != (uint16_t)ascii[unit])
						same = 0;
				}
				if (!same)
					continue;
				*x = box->x + box->border[CSS_LEFT] + box->padding[CSS_LEFT] + line->left + fragment->x;
				*y = box->y + box->border[CSS_TOP] + box->padding[CSS_TOP] + line->y + line->height / 2;
				return fragment;
			}
		}
		return NULL;
	}
	for (child = box->first_child; child != NULL; child = child->next) {
		found = find_fragment(child, ascii, x, y);
		if (found != NULL)
			return found;
	}
	return NULL;
}
