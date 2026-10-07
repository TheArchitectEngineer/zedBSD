/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The favourites and the turns kept (ws157-p002): Photos never writes a
 * picture's file; what the user marked is kept in
 * $XDG_CONFIG_HOME/keiland/photos.conf (or ~/.config/...), a line a mark:
 *
 *   favorite <path>
 *   turn <quarter turns clockwise, 1 to 3> <path>
 *
 * The lines of photos the library does not have now (a file opened once
 * from elsewhere, a folder not there) are kept and written back.  A save
 * writes a new file beside the old one and renames it over.
 */

#include "photos.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The folder and the file under the user's configuration folder. */
#define STORE_FOLDER		"keiland"
#define STORE_FILE		"photos.conf"

/* The longest line read, with its newline and NUL. */
#define STORE_LINE_MAX		(PH_PATH_MAX + 32U)

/* A mark of a photo the library does not have now. */
struct store_entry {
	char *path;
	int favorite;
	int turns;
};

/* The marks kept for photos the library does not have. */
static struct store_entry *store_kept;
static size_t store_kept_count;
static size_t store_kept_room;

static int store_apply(const char *path, int favorite, int turns);
static struct store_entry *store_keep(const char *path);
static int store_write(FILE *file);
static int store_folder(const char *path);

/*
 * Writes the path of the file of the marks.  Returns 0, or ENOENT when
 * neither $XDG_CONFIG_HOME nor $HOME is set, ENAMETOOLONG.
 */
int
ph_store_path(
	char *path,
	size_t size)
{
	const char *config;
	const char *home;
	int length;

	/* $XDG_CONFIG_HOME, or ~/.config. */
	config = getenv("XDG_CONFIG_HOME");
	home = getenv("HOME");
	if (config != NULL && config[0] == '/')
		length = snprintf(path, size, "%s/%s/%s", config, STORE_FOLDER, STORE_FILE);
	else if (home != NULL && home[0] == '/')
		length = snprintf(path, size, "%s/.config/%s/%s", home, STORE_FOLDER, STORE_FILE);
	else
		return ENOENT;

	/* The path fits. */
	if (length < 0 || (size_t)length >= size)
		return ENAMETOOLONG;
	return 0;
}

/*
 * Reads the marks and gives them to the library's photos (the library is
 * read first).  Returns 0 (a file that is not there has none), or an
 * errno value.
 */
int
ph_store_load(
	const char *path)
{
	char line[STORE_LINE_MAX];
	char *end;
	char *got;
	size_t length;
	long turns;
	int same;
	int error;
	FILE *file;

	/* The file; none is no marks. */
	file = fopen(path, "r");
	if (file == NULL) {
		if (errno == ENOENT)
			return 0;
		return errno;
	}

	/* Each line. */
	error = 0;
	for (;;) {
		got = fgets(line, sizeof(line), file);
		if (got == NULL)
			break;

		/* Without its newline; a line too long is passed over. */
		length = strlen(line);
		if (length == 0U || line[length - 1U] != '\n')
			continue;
		line[length - 1U] = '\0';

		/* A favourite. */
		same = strncmp(line, "favorite /", 10U);
		if (same == 0) {
			error = store_apply(line + 9, 1, -1);
			if (error != 0)
				break;
			continue;
		}

		/* A turn. */
		same = strncmp(line, "turn ", 5U);
		if (same != 0)
			continue;
		turns = strtol(line + 5, &end, 10);
		if (end == line + 5 || end[0] != ' ' || end[1] != '/' || turns < 1 || turns > 3)
			continue;
		error = store_apply(end + 1, -1, (int)turns);
		if (error != 0)
			break;
	}

	/* Read through. */
	(void)fclose(file);
	return error;
}

/*
 * Writes the marks (the library's photos' and those kept), a new file
 * renamed over the old.  Returns 0 or an errno value.
 */
int
ph_store_save(
	const char *path)
{
	char temporary[PH_PATH_MAX + 32U];
	int length;
	int error;
	int status;
	FILE *file;

	/* The folder, the user's alone. */
	error = store_folder(path);
	if (error != 0)
		return error;

	/* The new file beside the old one. */
	length = snprintf(temporary, sizeof(temporary), "%s.new-%ld", path, (long)getpid());
	if (length < 0 || (size_t)length >= sizeof(temporary))
		return ENAMETOOLONG;
	file = fopen(temporary, "w");
	if (file == NULL)
		return errno;

	/* The lines. */
	error = store_write(file);
	status = fclose(file);
	if (error == 0 && status != 0)
		error = EIO;
	if (error != 0) {
		(void)unlink(temporary);
		return error;
	}

	/* In place of the old. */
	status = rename(temporary, path);
	if (status != 0) {
		error = errno;
		(void)unlink(temporary);
		return error;
	}

	/* Succeeded. */
	return 0;
}

/*
 * Frees the marks kept for photos the library does not have.
 */
void
ph_store_release(void)
{
	size_t index;

	/* Each path, then the array. */
	for (index = 0; index < store_kept_count; index++)
		free(store_kept[index].path);
	free(store_kept);
	store_kept = NULL;
	store_kept_count = 0;
	store_kept_room = 0;
}

/* Gives a mark (-1 for one not given) to the photo of a path, or keeps it; 0 or ENOMEM. */
static int
store_apply(
	const char *path,
	int favorite,
	int turns)
{
	struct ph_photo *photos;
	struct store_entry *entry;
	size_t count;
	size_t index;
	int same;

	/* The library's photo. */
	photos = ph_photos(&count);
	for (index = 0; index < count; index++) {
		same = strcmp(photos[index].path, path);
		if (same != 0)
			continue;
		if (favorite >= 0)
			photos[index].favorite = favorite;
		if (turns >= 0)
			photos[index].turns = turns;

		/* Given. */
		return 0;
	}

	/* Not in the library: kept. */
	entry = store_keep(path);
	if (entry == NULL)
		return ENOMEM;
	if (favorite >= 0)
		entry->favorite = favorite;
	if (turns >= 0)
		entry->turns = turns;
	return 0;
}

/* Finds or makes the kept mark of a path (NULL when there is no room). */
static struct store_entry *
store_keep(
	const char *path)
{
	struct store_entry *grown;
	size_t length;
	size_t index;
	size_t room;
	int same;

	/* Already kept. */
	for (index = 0; index < store_kept_count; index++) {
		same = strcmp(store_kept[index].path, path);
		if (same == 0)
			return &store_kept[index];
	}

	/* Room for one more. */
	if (store_kept_count == store_kept_room) {
		room = store_kept_room * 2U;
		if (room == 0U)
			room = 16U;
		grown = realloc(store_kept, room * sizeof(grown[0]));
		if (grown == NULL)
			return NULL;
		store_kept = grown;
		store_kept_room = room;
	}

	/* A new one, with no mark yet. */
	length = strlen(path) + 1U;
	store_kept[store_kept_count].path = malloc(length);
	if (store_kept[store_kept_count].path == NULL)
		return NULL;
	memcpy(store_kept[store_kept_count].path, path, length);
	store_kept[store_kept_count].favorite = 0;
	store_kept[store_kept_count].turns = 0;
	store_kept_count++;
	return &store_kept[store_kept_count - 1U];
}

/* Writes the lines of the marks; 0 or EIO. */
static int
store_write(
	FILE *file)
{
	const struct ph_photo *photos;
	const char *path;
	const char *newline;
	size_t count;
	size_t index;
	int favorite;
	int turns;
	int status;

	/* The heading. */
	status = fprintf(file, "# Photos: the favourites and the turns (a picture's file is never written)\n");
	if (status < 0)
		return EIO;

	/* The library's photos, then those kept. */
	photos = ph_photos(&count);
	for (index = 0; index < count + store_kept_count; index++) {
		if (index < count) {
			path = photos[index].path;
			favorite = photos[index].favorite;
			turns = photos[index].turns;
		} else {
			path = store_kept[index - count].path;
			favorite = store_kept[index - count].favorite;
			turns = store_kept[index - count].turns;
		}

		/* A path with a newline cannot be a line. */
		newline = strchr(path, '\n');
		if (newline != NULL)
			continue;

		/* Its marks. */
		if (favorite) {
			status = fprintf(file, "favorite %s\n", path);
			if (status < 0)
				return EIO;
		}

		/* Its turn. */
		if (turns > 0) {
			status = fprintf(file, "turn %d %s\n", turns, path);
			if (status < 0)
				return EIO;
		}
	}

	/* Written. */
	return 0;
}

/* Makes the folders of the file's path (the user's alone); 0 or an errno value. */
static int
store_folder(
	const char *path)
{
	char folder[PH_PATH_MAX];
	char *slash;
	int length;
	int status;

	/* Each folder of the path from the top, made when it is not there. */
	length = snprintf(folder, sizeof(folder), "%s", path);
	if (length < 0 || (size_t)length >= sizeof(folder))
		return ENAMETOOLONG;
	for (slash = strchr(folder + 1, '/'); slash != NULL; slash = strchr(slash + 1, '/')) {
		*slash = '\0';
		status = mkdir(folder, 0700);
		if (status != 0 && errno != EEXIST)
			return errno;
		*slash = '/';
	}

	/* The folders are there. */
	return 0;
}
