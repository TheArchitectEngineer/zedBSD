/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The library (ws157-p002): the photos of a folder (~/Pictures) and of the
 * files opened.  A folder is looked through four levels deep, leaving out
 * what is hidden, for files whose first bytes are a JPEG's, a PNG's or a
 * GIF's.  The folders straight in it are the albums, in the order of
 * their names; a photo in a folder deeper is its album's.  A photo's date
 * is when it was taken (a JPEG's EXIF, exif.c), else when its file was
 * last written, in the local calendar.  The photos are in the order of
 * their dates, the newest first, then of their paths.
 */

#include "photos.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* How deep a folder is looked through, and how many of its folders are albums. */
#define LIBRARY_DEPTH		4
#define LIBRARY_ALBUMS_MAX	1024U

/* The bytes that tell a picture's kind. */
#define LIBRARY_HEAD		8U

/*
 * The library: the photos and the albums, with the room allocated for
 * each.
 */
struct library {
	struct ph_photo *photos;
	size_t photo_count;
	size_t photo_room;
	struct ph_album *albums;
	size_t album_count;
	size_t album_room;
};

/* The library of the program. */
static struct library library;

static int library_walk(const char *folder, int depth, size_t album);
static int library_albums(const char *folder, char **names, size_t count);
static int library_add(const char *path, const struct stat *status, size_t album);
static int library_date(const char *path, int kind, const struct stat *status, ph_time *taken);
static int library_compare_photos(const void *left, const void *right);
static int library_compare_names(const void *left, const void *right);
static long library_find(const char *path);
static char *library_copy(const char *text);

/*
 * Looks through a folder for photos and adds them to the library.
 * Returns 0 (a folder that does not exist has none), or an errno value.
 */
int
ph_library_scan(
	const char *folder)
{
	int error;

	/* The folder's photos, its folders as albums. */
	error = library_walk(folder, 0, PH_NO_ALBUM);
	if (error != 0)
		return error;

	/* In order. */
	if (library.photo_count > 1U)
		qsort(library.photos, library.photo_count, sizeof(library.photos[0]), library_compare_photos);
	return 0;
}

/*
 * Adds a file's photo to the library (it may be outside the folder), or
 * finds it there.  Returns 0 with its index, EINVAL for a file that is
 * not a picture read, or an errno value of the file's.
 */
int
ph_library_add_file(
	const char *path,
	long *photo)
{
	struct stat status;
	int regular;
	int error;

	/* Already there. */
	*photo = library_find(path);
	if (*photo >= 0)
		return 0;

	/* A file, added in no album. */
	error = stat(path, &status);
	if (error != 0)
		return errno;
	regular = S_ISREG(status.st_mode);
	if (!regular)
		return EINVAL;
	error = library_add(path, &status, PH_NO_ALBUM);
	if (error != 0)
		return error;

	/* The order made again, and its place in it. */
	qsort(library.photos, library.photo_count, sizeof(library.photos[0]), library_compare_photos);
	*photo = library_find(path);
	if (*photo < 0)
		return ENOENT;
	return 0;
}

/*
 * Reports the photos, in order.
 */
struct ph_photo *
ph_photos(
	size_t *count)
{
	/* The array. */
	*count = library.photo_count;
	return library.photos;
}

/*
 * Reports the albums, in the order of their names.
 */
const struct ph_album *
ph_albums(
	size_t *count)
{
	/* The array. */
	*count = library.album_count;
	return library.albums;
}

/*
 * Lists the photos of a list (PH_LIST_*; an album's index for
 * PH_LIST_ALBUM), in order.  Returns how many indices are stored, at most
 * the capacity.
 */
size_t
ph_library_list(
	int list,
	size_t album,
	size_t *indices,
	size_t capacity)
{
	const struct ph_photo *photo;
	size_t count;
	size_t index;

	/* Each photo, in order. */
	count = 0;
	for (index = 0; index < library.photo_count && count < capacity; index++) {
		photo = &library.photos[index];
		if (list == PH_LIST_FAVORITES && !photo->favorite)
			continue;
		if (list == PH_LIST_ALBUM && photo->album != album)
			continue;
		indices[count] = index;
		count++;
	}

	/* The photos listed. */
	return count;
}

/*
 * Frees the library.
 */
void
ph_library_release(void)
{
	size_t index;

	/* Each photo's path and each album's name. */
	for (index = 0; index < library.photo_count; index++)
		free(library.photos[index].path);
	for (index = 0; index < library.album_count; index++)
		free(library.albums[index].name);

	/* The arrays. */
	free(library.photos);
	free(library.albums);
	memset(&library, 0, sizeof(library));
}

/*
 * Tells what a file's first bytes say it is (PH_KIND_*).
 */
int
ph_picture_kind(
	const unsigned char *data,
	size_t size)
{
	int same;

	/* A JPEG: FF D8 FF. */
	if (size >= 3U && data[0] == 0xffU && data[1] == 0xd8U && data[2] == 0xffU)
		return PH_KIND_JPEG;

	/* A PNG's signature. */
	if (size >= 8U) {
		same = memcmp(data, "\x89PNG\r\n\x1a\n", 8U);
		if (same == 0)
			return PH_KIND_PNG;
	}

	/* A GIF: GIF87a or GIF89a. */
	if (size >= 6U) {
		same = memcmp(data, "GIF8", 4U);
		if (same == 0 && (data[4] == '7' || data[4] == '9') && data[5] == 'a')
			return PH_KIND_GIF;
	}

	/* Something else. */
	return PH_KIND_NONE;
}

/*
 * Adds the photos of a folder and of its folders down to the depth; at
 * the top its folders are the albums.  0 (a folder that cannot be opened
 * has none), or ENOMEM.
 */
static int
library_walk(
	const char *folder,
	int depth,
	size_t album)
{
	struct dirent *entry;
	struct stat status;
	char path[PH_PATH_MAX];
	char *names[LIBRARY_ALBUMS_MAX];
	size_t name_count;
	size_t index;
	int length;
	int error;
	int folder_entry;
	int regular;
	DIR *opened;

	/* The folder; one that is not there has no photos. */
	opened = opendir(folder);
	if (opened == NULL)
		return 0;

	/* Each entry but the hidden ones. */
	name_count = 0;
	error = 0;
	for (;;) {
		entry = readdir(opened);
		if (entry == NULL)
			break;
		if (entry->d_name[0] == '.')
			continue;
		length = snprintf(path, sizeof(path), "%s/%s", folder, entry->d_name);
		if (length < 0 || (size_t)length >= sizeof(path))
			continue;
		error = stat(path, &status);
		if (error != 0) {
			error = 0;
			continue;
		}

		/* What it is. */
		folder_entry = S_ISDIR(status.st_mode);
		regular = S_ISREG(status.st_mode);

		/* A folder at the top: an album, walked when every name is known. */
		if (folder_entry && depth == 0) {
			if (name_count == LIBRARY_ALBUMS_MAX)
				continue;
			names[name_count] = library_copy(entry->d_name);
			if (names[name_count] == NULL) {
				error = ENOMEM;
				break;
			}

			/* Walked later. */
			name_count++;
			continue;
		}

		/* A folder deeper: its album's, down to the depth. */
		if (folder_entry) {
			if (depth + 1 < LIBRARY_DEPTH)
				error = library_walk(path, depth + 1, album);
			if (error != 0)
				break;
			continue;
		}

		/* A file: a photo when it is a picture. */
		if (regular) {
			error = library_add(path, &status, album);
			if (error == EINVAL)
				error = 0;
			if (error != 0)
				break;
		}
	}

	/* The folder is read; at the top its folders become the albums, in the order of their names. */
	(void)closedir(opened);
	if (error == 0 && name_count > 0U)
		error = library_albums(folder, names, name_count);

	/* The names go. */
	for (index = 0; index < name_count; index++)
		free(names[index]);
	return error;
}

/* Makes the albums of the top folder's folders and walks each; 0 or ENOMEM. */
static int
library_albums(
	const char *folder,
	char **names,
	size_t count)
{
	struct ph_album *grown;
	char path[PH_PATH_MAX];
	size_t index;
	size_t album;
	size_t first;
	size_t room;
	int length;
	int error;

	/* In the order of their names. */
	qsort(names, count, sizeof(names[0]), library_compare_names);

	/* Room for them. */
	if (library.album_count + count > library.album_room) {
		room = library.album_count + count;
		grown = realloc(library.albums, room * sizeof(grown[0]));
		if (grown == NULL)
			return ENOMEM;
		library.albums = grown;
		library.album_room = room;
	}

	/* Each album and its photos. */
	for (index = 0; index < count; index++) {
		album = library.album_count;
		library.albums[album].name = library_copy(names[index]);
		if (library.albums[album].name == NULL)
			return ENOMEM;
		library.albums[album].count = 0;
		library.album_count++;

		/* Its folder. */
		length = snprintf(path, sizeof(path), "%s/%s", folder, names[index]);
		if (length < 0 || (size_t)length >= sizeof(path))
			continue;
		first = library.photo_count;
		error = library_walk(path, 1, album);
		if (error != 0)
			return error;

		/* Its count. */
		library.albums[album].count = library.photo_count - first;
	}

	/* The albums are made. */
	return 0;
}

/*
 * Adds a file's photo when it is a picture.  0, EINVAL for one that is
 * not, or an errno value.
 */
static int
library_add(
	const char *path,
	const struct stat *status,
	size_t album)
{
	unsigned char head[LIBRARY_HEAD];
	struct ph_photo *grown;
	struct ph_photo *photo;
	const char *slash;
	ssize_t got;
	size_t room;
	int kind;
	int fd;

	/* The library is full. */
	if (library.photo_count == PH_PHOTOS_MAX)
		return ENOSPC;

	/* What its first bytes say. */
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return EINVAL;
	got = read(fd, head, sizeof(head));
	(void)close(fd);
	if (got <= 0)
		return EINVAL;
	kind = ph_picture_kind(head, (size_t)got);
	if (kind == PH_KIND_NONE)
		return EINVAL;

	/* Room for it. */
	if (library.photo_count == library.photo_room) {
		room = library.photo_room * 2U;
		if (room == 0U)
			room = 64U;
		grown = realloc(library.photos, room * sizeof(grown[0]));
		if (grown == NULL)
			return ENOMEM;
		library.photos = grown;
		library.photo_room = room;
	}

	/* The photo. */
	photo = &library.photos[library.photo_count];
	memset(photo, 0, sizeof(*photo));
	photo->path = library_copy(path);
	if (photo->path == NULL)
		return ENOMEM;
	slash = strrchr(photo->path, '/');
	photo->name = photo->path;
	if (slash != NULL)
		photo->name = slash + 1;
	photo->album = album;
	(void)library_date(path, kind, status, &photo->taken);
	library.photo_count++;
	return 0;
}

/* Finds when a photo was taken: its EXIF, else its file's time; 0. */
static int
library_date(
	const char *path,
	int kind,
	const struct stat *status,
	ph_time *taken)
{
	struct tm local;
	time_t written;
	int error;

	/* A JPEG's EXIF. */
	if (kind == PH_KIND_JPEG) {
		error = ph_exif_file_date(path, taken);
		if (error == 0)
			return 0;
	}

	/* The file's time in the local calendar. */
	written = status->st_mtime;
	memset(&local, 0, sizeof(local));
	(void)localtime_r(&written, &local);
	*taken = ph_time_make(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
	return 0;
}

/* Orders two photos: the newest first, then by path. */
static int
library_compare_photos(
	const void *left,
	const void *right)
{
	const struct ph_photo *one;
	const struct ph_photo *other;

	/* The dates. */
	one = left;
	other = right;
	if (one->taken > other->taken)
		return -1;
	if (one->taken < other->taken)
		return 1;

	/* The paths. */
	return strcmp(one->path, other->path);
}

/* Orders two names, without regard to case. */
static int
library_compare_names(
	const void *left,
	const void *right)
{
	const char *const *one;
	const char *const *other;
	int order;

	/* Without case, then with. */
	one = left;
	other = right;
	order = strcasecmp(*one, *other);
	if (order != 0)
		return order;
	return strcmp(*one, *other);
}

/* Finds a photo by its path: its index, or -1. */
static long
library_find(
	const char *path)
{
	size_t index;
	int same;

	/* Each photo. */
	for (index = 0; index < library.photo_count; index++) {
		same = strcmp(library.photos[index].path, path);
		if (same == 0)
			return (long)index;
	}

	/* Not there. */
	return -1;
}

/* Copies a string (NULL when there is no room). */
static char *
library_copy(
	const char *text)
{
	size_t length;
	char *copy;

	/* The bytes and the NUL. */
	length = strlen(text) + 1U;
	copy = malloc(length);
	if (copy == NULL)
		return NULL;
	memcpy(copy, text, length);
	return copy;
}
