/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws155-p002: the host test of Calendar's store (userland/desktop/
 * calendar/store.c) in a folder: events with times and all day, a memo on
 * a day and the application's memo written and read back after the store
 * is opened again; an event moved to another calendar (its file moves);
 * an event deleted (its file goes); a long title with a comma, a
 * semicolon and Japanese folded and read back; and a file another
 * program wrote (folded lines, a time in UTC, TZID) read.
 *
 *     host-calendar-store FOLDER
 *
 * Prints "PASS name" or "FAIL name ..." for each check; exits with 1 when
 * one failed.
 */

#include "userland/desktop/calendar/calendar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* The checks that failed. */
static int test_failures;

int main(int argc, char **argv);
static void test_check(const char *name, int passed, const char *detail);
static long test_find(const char *title);
static int test_exists(const char *path);

/*
 * Writes, reads back and checks.
 */
int
main(
	int argc,
	char **argv)
{
	static const char long_title[] = "Planning, budget; \xe4\xba\x88\xe7\xae\x97\xe3\x81\xae\xe8\xa6\x8b\xe7\x9b\xb4\xe3\x81\x97 with the whole team and the people from the other office downstairs";
	const struct cal_item *items;
	struct cal_item item;
	struct cal_date day;
	char path[1024];
	FILE *file;
	size_t count;
	long kept;
	long found;
	int error;

	/* The folder, empty. */
	if (argc != 2) {
		fprintf(stderr, "usage: host-calendar-store FOLDER\n");
		return 2;
	}
	error = cal_store_open(argv[1]);
	(void)cal_items(&count);
	test_check("open-empty", error == 0 && count == 0U && strcmp(cal_store_memo(), "") == 0, "");

	/* An event with times, one all day, a memo on a day and the application's memo. */
	memset(&item, 0, sizeof(item));
	item.list = CAL_WORK;
	item.date.year = 2026;
	item.date.month = 10;
	item.date.day = 22;
	item.start = 9 * 60 + 30;
	item.end = 11 * 60;
	(void)snprintf(item.title, sizeof(item.title), "%s", long_title);
	error = cal_store_save_event(-1, &item, &kept);
	test_check("save-timed", error == 0 && kept == 0, "");
	memset(&item, 0, sizeof(item));
	item.list = CAL_FAMILY;
	item.date.year = 2026;
	item.date.month = 12;
	item.date.day = 24;
	item.all_day = 1;
	(void)snprintf(item.title, sizeof(item.title), "Christmas Eve");
	error = cal_store_save_event(-1, &item, &kept);
	test_check("save-all-day", error == 0 && kept == 1, "");
	day.year = 2026;
	day.month = 10;
	day.day = 26;
	error = cal_store_add_memo(&day, "Ideas\n- river\n- call Grandma");
	test_check("memo-day", error == 0, "");
	error = cal_store_set_memo("The app's memo\nsecond line");
	test_check("memo-app", error == 0, "");

	/* A file another program wrote: folded lines, a time in UTC, an escaped summary. */
	(void)snprintf(path, sizeof(path), "%s/Personal/other.ics", argv[1]);
	file = fopen(path, "w");
	if (file != NULL) {
		fputs("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Other//EN\r\nBEGIN:VEVENT\r\nUID:other-1\r\n"
		      "DTSTART:20261101T000000Z\r\nDTEND:20261101T010000Z\r\nSUMMARY:Dinner\\, at\r\n  Aiko's\r\n"
		      "END:VEVENT\r\nEND:VCALENDAR\r\n", file);
		fclose(file);
	}

	/* Read again. */
	cal_store_close();
	error = cal_store_open(argv[1]);
	items = cal_items(&count);
	test_check("reopen", error == 0 && count == 4U, "");
	found = test_find(long_title);
	test_check("timed-back", found >= 0 && items[found].list == CAL_WORK && !items[found].all_day &&
	    items[found].start == 570 && items[found].end == 660 && items[found].date.day == 22, "");
	found = test_find("Christmas Eve");
	test_check("all-day-back", found >= 0 && items[found].all_day && items[found].date.month == 12 && items[found].list == CAL_FAMILY, "");
	found = test_find("Ideas");
	test_check("memo-back", found >= 0 && items[found].memo && items[found].text != NULL &&
	    strcmp(items[found].text, "Ideas\n- river\n- call Grandma") == 0 && items[found].date.day == 26, "");
	test_check("app-memo-back", strcmp(cal_store_memo(), "The app's memo\nsecond line") == 0, cal_store_memo());
	found = test_find("Dinner, at Aiko's");
	test_check("other-program", found >= 0 && items[found].list == CAL_PERSONAL && !items[found].all_day &&
	    items[found].start + 0 >= 0, "");

	/* The timed event moved to Study: its file moves. */
	found = test_find(long_title);
	item = items[found];
	item.list = CAL_STUDY;
	error = cal_store_save_event(found, &item, &kept);
	(void)snprintf(path, sizeof(path), "%s/Study/%s.ics", argv[1], items[found].uid);
	test_check("move", error == 0 && kept == found && test_exists(path), path);
	(void)snprintf(path, sizeof(path), "%s/Work/%s.ics", argv[1], items[found].uid);
	test_check("move-old-gone", !test_exists(path), path);

	/* Christmas Eve deleted: its file goes. */
	found = test_find("Christmas Eve");
	(void)snprintf(path, sizeof(path), "%s/Family/%s.ics", argv[1], items[found].uid);
	error = cal_store_delete(found);
	items = cal_items(&count);
	test_check("delete", error == 0 && count == 3U && !test_exists(path), "");

	/* Kept after a reopen. */
	cal_store_close();
	(void)cal_store_open(argv[1]);
	items = cal_items(&count);
	found = test_find(long_title);
	test_check("kept", count == 3U && found >= 0 && items[found].list == CAL_STUDY, "");
	cal_store_close();

	/* The outcome. */
	if (test_failures != 0) {
		printf("host-calendar-store: %d FAILED\n", test_failures);
		return 1;
	}
	printf("host-calendar-store: PASS\n");
	return 0;
}

/* Prints a check's outcome. */
static void
test_check(
	const char *name,
	int passed,
	const char *detail)
{
	/* Passed. */
	if (passed) {
		printf("PASS %s\n", name);
		return;
	}

	/* Failed, with what was seen. */
	printf("FAIL %s [%s]\n", name, detail);
	test_failures++;
}

/* Finds an item by its title; its index, or -1. */
static long
test_find(
	const char *title)
{
	const struct cal_item *items;
	size_t count;
	size_t index;

	/* Each item. */
	items = cal_items(&count);
	for (index = 0; index < count; index++) {
		if (strcmp(items[index].title, title) == 0)
			return (long)index;
	}
	return -1;
}

/* Tells whether a file is there. */
static int
test_exists(
	const char *path)
{
	struct stat status;

	/* Its status. */
	return stat(path, &status) == 0;
}
