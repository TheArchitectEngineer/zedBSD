/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of Photos' library (ws157-p002): the folder make-photos.py
 * writes is read, and the dates (EXIF and the files' times), the order,
 * the albums, the lists, a file added from outside and the marks kept
 * (favourites, turns, written and read again) are checked.  Run with TZ=UTC.
 *   host-photos-library FOLDER CONFIG   (FOLDER as ~/Pictures, CONFIG for photos.conf)
 */

#include "photos.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The checks failed. */
static int failures;

static void test_check(const char *name, int passed, const char *detail);
static int test_ends(size_t index, const char *suffix);
static int test_date(size_t index, int year, int month, int day);
static void test_time(void);
static void test_store(const char *folder, const char *config);

/* The log the program would write (not used by the library). */
void
ph_log(
	const char *format,
	...)
{
	(void)format;
}

/* Reads the library and checks it. */
int
main(
	int argc,
	char **argv)
{
	const struct ph_album *albums;
	struct ph_photo *photos;
	size_t indices[32];
	size_t photo_count;
	size_t album_count;
	size_t count;
	char path[1024];
	char *slash;
	long photo;
	int error;

	/* The folders. */
	if (argc != 3) {
		fprintf(stderr, "usage: host-photos-library FOLDER CONFIG\n");
		return 2;
	}

	/* The calendar. */
	test_time();

	/* The library. */
	error = ph_library_scan(argv[1]);
	test_check("scan", error == 0, "the folder is read");
	photos = ph_photos(&photo_count);
	albums = ph_albums(&album_count);
	test_check("count", photo_count == 8U, "8 pictures: not text, not a fake JPEG, not hidden, not past four levels");
	if (photo_count != 8U) {
		fprintf(stderr, "FAIL count=%lu\n", (unsigned long)photo_count);
		return 1;
	}

	/* The order, the newest first, with the dates each way gives. */
	test_check("order-0", test_ends(0, "/Family/birthday.jpg") && test_date(0, 2026, 1, 20), "EXIF original, big-endian, no APP0");
	test_check("order-1", test_ends(1, "/Trips/Kyoto/temple.jpg") && test_date(1, 2025, 4, 2), "a deeper folder's photo");
	test_check("order-2", test_ends(2, "/beach.jpg") && test_date(2, 2024, 8, 15), "the original before DateTime");
	test_check("order-3", test_ends(3, "/Trips/street.jpeg") && test_date(3, 2023, 12, 31), "only IFD0's DateTime");
	test_check("order-4", test_ends(4, "/Family/noclock.jpg") && test_date(4, 2022, 6, 1), "a date of zeros: the file's time");
	test_check("order-5", test_ends(5, "/Family/drawing.png") && test_date(5, 2021, 3, 4), "a PNG: the file's time");
	test_check("order-6", test_ends(6, "/anim.gif") && test_date(6, 2020, 1, 2), "a GIF: the file's time");
	test_check("order-7", test_ends(7, "/Deep/a/b/four.jpg"), "four levels deep");
	test_check("time", photos[0].taken == ph_time_make(2026, 1, 20, 18, 0, 0), "the time of day too");
	test_check("name", strcmp(photos[2].name, "beach.jpg") == 0, "the name is the path's last part");

	/* The albums, by name, and their photos. */
	test_check("albums", album_count == 4U, "Deep, Empty, Family, Trips");
	if (album_count == 4U) {
		test_check("album-names", strcmp(albums[0].name, "Deep") == 0 && strcmp(albums[1].name, "Empty") == 0 &&
		    strcmp(albums[2].name, "Family") == 0 && strcmp(albums[3].name, "Trips") == 0, "in the order of names");
		test_check("album-counts", albums[0].count == 1U && albums[1].count == 0U && albums[2].count == 3U && albums[3].count == 2U,
		    "how many each holds");
	}

	/* Each photo's album. */
	test_check("album-of", photos[0].album == 2U && photos[1].album == 3U && photos[2].album == PH_NO_ALBUM && photos[7].album == 0U,
	    "a photo's album is its top folder");

	/* The lists. */
	count = ph_library_list(PH_LIST_TIMELINE, 0, indices, 32);
	test_check("list-timeline", count == 8U && indices[0] == 0U && indices[7] == 7U, "every photo in order");
	count = ph_library_list(PH_LIST_ALBUM, 2, indices, 32);
	test_check("list-album", count == 3U && indices[0] == 0U && indices[1] == 4U && indices[2] == 5U, "Family's in order");
	count = ph_library_list(PH_LIST_FAVORITES, 0, indices, 32);
	test_check("list-favorites-none", count == 0U, "no favourites yet");
	photos[3].favorite = 1;
	photos[1].favorite = 1;
	count = ph_library_list(PH_LIST_FAVORITES, 0, indices, 32);
	test_check("list-favorites", count == 2U && indices[0] == 1U && indices[1] == 3U, "the favourites in order");
	count = ph_library_list(PH_LIST_TIMELINE, 0, indices, 3);
	test_check("list-capacity", count == 3U, "at most the capacity");

	/* A file from outside: added in no album, in its place by date; again is the same one. */
	(void)snprintf(path, sizeof(path), "%s", argv[1]);
	slash = strrchr(path, '/');
	if (slash != NULL)
		(void)snprintf(slash + 1, sizeof(path) - (size_t)(slash + 1 - path), "outside-%s.jpg", strrchr(argv[1], '/') + 1);
	error = ph_library_add_file(path, &photo);
	photos = ph_photos(&photo_count);
	test_check("add-outside", error == 0 && photo == 8 && photo_count == 9U && photos[8].album == PH_NO_ALBUM && test_date(8, 2018, 7, 7),
	    "added at its date's place");
	error = ph_library_add_file(path, &photo);
	test_check("add-again", error == 0 && photo == 8 && photo_count == 9U, "found, not added twice");
	(void)snprintf(path, sizeof(path), "%s/notes.txt", argv[1]);
	error = ph_library_add_file(path, &photo);
	test_check("add-text", error == EINVAL, "a file that is not a picture");

	/* The marks. */
	test_store(argv[1], argv[2]);

	/* The result. */
	ph_library_release();
	ph_store_release();
	if (failures != 0) {
		fprintf(stderr, "FAIL %d\n", failures);
		return 1;
	}

	/* Passed. */
	printf("PASS\n");
	return 0;
}

/* Writes a check's result. */
static void
test_check(
	const char *name,
	int passed,
	const char *detail)
{
	/* A line each. */
	if (passed) {
		printf("ok %s: %s\n", name, detail);
		return;
	}

	/* Failed. */
	printf("NOT OK %s: %s\n", name, detail);
	failures++;
}

/* Tells whether a photo's path ends so. */
static int
test_ends(
	size_t index,
	const char *suffix)
{
	struct ph_photo *photos;
	size_t count;
	size_t length;
	size_t want;

	/* The photo's path. */
	photos = ph_photos(&count);
	if (index >= count)
		return 0;
	length = strlen(photos[index].path);
	want = strlen(suffix);
	if (length < want)
		return 0;
	return strcmp(photos[index].path + length - want, suffix) == 0;
}

/* Tells whether a photo's date is a day. */
static int
test_date(
	size_t index,
	int year,
	int month,
	int day)
{
	struct ph_photo *photos;
	size_t count;
	int got_year;
	int got_month;
	int got_day;

	/* The photo's date split. */
	photos = ph_photos(&count);
	if (index >= count)
		return 0;
	ph_time_split(photos[index].taken, &got_year, &got_month, &got_day);
	return got_year == year && got_month == month && got_day == day;
}

/* Checks the calendar both ways. */
static void
test_time(void)
{
	ph_time when;
	int year;
	int month;
	int day;
	int good;

	/* 1970, a leap day, the end of a century year, before 1970. */
	test_check("time-epoch", ph_time_make(1970, 1, 1, 0, 0, 0) == 0, "the epoch is 0");
	test_check("time-2000", ph_time_make(2000, 3, 1, 0, 0, 0) == 951868800, "2000-03-01");
	when = ph_time_make(2024, 2, 29, 23, 59, 59);
	ph_time_split(when, &year, &month, &day);
	good = year == 2024 && month == 2 && day == 29;
	test_check("time-leap", good, "a leap day back");
	when = ph_time_make(1969, 12, 31, 12, 0, 0);
	ph_time_split(when, &year, &month, &day);
	good = when < 0 && year == 1969 && month == 12 && day == 31;
	test_check("time-before", good, "before 1970 back");
}

/* Checks that the marks are written and read again, with those of photos not in the library. */
static void
test_store(
	const char *folder,
	const char *config)
{
	struct ph_photo *photos;
	char path[1024];
	char line[1200];
	char *got;
	size_t count;
	int error;
	int found;
	int same;
	FILE *file;

	/* A mark for a photo the library does not have, already in the file. */
	error = ph_store_path(path, sizeof(path));
	test_check("store-path", error == 0 && strstr(path, "/keiland/photos.conf") != NULL, "under the configuration folder");
	(void)snprintf(path, sizeof(path), "%s/keiland/photos.conf", config);
	file = fopen(path, "w");
	if (file == NULL) {
		test_check("store-seed", 0, "the file is written");
		return;
	}

	/* The lines. */
	fprintf(file, "# an old file\nfavorite /gone/old.jpg\nturn 2 /gone/old.jpg\nturn 7 %s/beach.jpg\nnonsense\n", folder);
	(void)fclose(file);

	/* Read: the library's photos get their marks, a turn out of range is not one. */
	error = ph_store_load(path);
	photos = ph_photos(&count);
	test_check("store-load", error == 0 && photos[2].turns == 0, "a turn of 7 is passed over");

	/* The marks: two favourites (from the lists' check), a turn. */
	photos[2].turns = 3;
	photos[5].turns = 1;
	error = ph_store_save(path);
	test_check("store-save", error == 0, "written and renamed");

	/* Read back into a library with no marks. */
	photos[1].favorite = 0;
	photos[3].favorite = 0;
	photos[2].turns = 0;
	photos[5].turns = 0;
	error = ph_store_load(path);
	test_check("store-reload", error == 0 && photos[1].favorite && photos[3].favorite && photos[2].turns == 3 && photos[5].turns == 1 &&
	    !photos[0].favorite && photos[0].turns == 0, "the marks come back");

	/* The mark of the photo not in the library is written back. */
	file = fopen(path, "r");
	found = 0;
	while (file != NULL) {
		got = fgets(line, sizeof(line), file);
		if (got == NULL)
			break;
		same = strcmp(line, "favorite /gone/old.jpg\n");
		if (same != 0)
			same = strcmp(line, "turn 2 /gone/old.jpg\n");
		if (same == 0)
			found++;
	}

	/*  NULL)=Read through. */
	if (file != NULL)
		(void)fclose(file);
	test_check("store-kept", found == 2, "the marks of a photo not there are kept");

	/* A file that is not there has no marks. */
	(void)snprintf(path, sizeof(path), "%s/keiland/none.conf", config);
	error = ph_store_load(path);
	test_check("store-none", error == 0, "no file, no marks");
}
