/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Photos (WS157): Keiland's library of the pictures in ~/Pictures.
 *
 * The library (library.c) reads the folder four levels deep for JPEG, PNG
 * and GIF files; each photo's date is when it was taken (its EXIF,
 * exif.c) or else when its file was last written, and its album is the
 * folder of ~/Pictures it is in.  The photos are in the order of their
 * dates, the newest first.  The favourites and the turns the user gave
 * (the file itself is never written) are kept in a file of their own
 * (store.c).
 *
 * The view (view.c) draws a frame with libkeiland's canvas and widgets:
 * the lists at the left, the grid of the photos by month at the right, or
 * one photo over the whole window.  The thumbnails are made by a thread of
 * their own (thumbs.c).  The window (main.c) feeds the view the input.
 */

#ifndef PHOTOS_PHOTOS_H
#define PHOTOS_PHOTOS_H

#include <keiland/keiland.h>

#include <stddef.h>
#include <stdint.h>

/* The longest path kept, with its NUL. */
#define PH_PATH_MAX		1024U

/* The most photos the library keeps. */
#define PH_PHOTOS_MAX		20000U

/* A photo in no album (straight in ~/Pictures, or a file opened from elsewhere). */
#define PH_NO_ALBUM		((size_t)-1)

/*
 * A date and time as a count of seconds, the calendar's time read as if it
 * were UTC (the EXIF date has no zone; a file's time is turned into the
 * local calendar first), so that the year, month and day come back as
 * they were written.
 */
typedef int64_t ph_time;

/*
 * One photo: its file and name (the last part of the path), when it was
 * taken, its album (an index of the albums, or PH_NO_ALBUM), whether it
 * is a favourite, and the quarter turns clockwise the user gave it (0 to
 * 3).  The strings are the library's.
 */
struct ph_photo {
	char *path;
	const char *name;
	ph_time taken;
	size_t album;
	int favorite;
	int turns;
};

/* One album: its name (the folder's) and how many photos it holds. */
struct ph_album {
	char *name;
	size_t count;
};

/* What a file's first bytes say it is. */
#define PH_KIND_NONE		0
#define PH_KIND_JPEG		1
#define PH_KIND_PNG		2
#define PH_KIND_GIF		3

/* The lists at the left: every photo by date, the favourites, an album. */
#define PH_LIST_TIMELINE	0
#define PH_LIST_FAVORITES	1
#define PH_LIST_ALBUM		2

/* The date of a JPEG's EXIF (exif.c). */
int ph_exif_date(const unsigned char *data, size_t size, ph_time *taken);
int ph_exif_file_date(const char *path, ph_time *taken);
ph_time ph_time_make(int year, int month, int day, int hour, int minute, int second);
void ph_time_split(ph_time when, int *year, int *month, int *day);

/* The library (library.c). */
int ph_library_scan(const char *folder);
int ph_library_add_file(const char *path, long *photo);
struct ph_photo *ph_photos(size_t *count);
const struct ph_album *ph_albums(size_t *count);
size_t ph_library_list(int list, size_t album, size_t *indices, size_t capacity);
void ph_library_release(void);
int ph_picture_kind(const unsigned char *data, size_t size);

/* The favourites and the turns kept (store.c). */
int ph_store_path(char *path, size_t size);
int ph_store_load(const char *path);
int ph_store_save(const char *path);
void ph_store_release(void);

/* The log for the tests (main.c, and the host tests' own). */
void ph_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

#endif
