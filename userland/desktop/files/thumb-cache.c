/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The thumbnails kept on disk (ws127-p002, F-035).  ws168-p004: the
 * records are written by keiland-preview (thumb.c), the first page of a
 * PDF too; this file finds a record's place and stamp, reads records back
 * and trims the cache.
 *
 * A thumbnail made once is kept in the user's cache folder
 * ($XDG_CACHE_HOME, else ~/.cache, then keiland/thumbnails), named by the
 * SHA-256 of the file's path, as a binary PPM whose comment line records the
 * file's modification time and size.  A file that changed has a stale
 * record, which is made again; the folder is the user's alone (0700).
 *
 * The cache is kept small (ws127-p004): when a new record leaves more than
 * FM_THUMB_RECORDS_MAX records, the oldest written are removed until
 * FM_THUMB_RECORDS_KEEP are left.  A record is named by the SHA-256 of a
 * path, so a record whose file is gone cannot be told apart; it goes in
 * its turn.
 */

#include "files.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sha2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The largest thumbnail read back from the cache, a side in pixels. */
#define CACHE_SIDE_MAX		1024

/* The cache's folder under the user's cache folder, and the comment that marks a record. */
#define CACHE_FOLDER		"keiland/thumbnails"
#define CACHE_MARK		"# keiland-thumbnail"

/*
 * One record of the cache as a trim sees it: its name in the cache's
 * folder and when it was written.
 */
struct cache_entry {
	char *name;
	time_t written;
};

static int cache_trim(const char *folder, unsigned maximum, unsigned keep);
static int cache_trim_list(const char *folder, struct cache_entry **entries, size_t *count);
static int cache_compare_written(const void *left, const void *right);
static int cache_record_path(const char *path, char *record, size_t size);
static int cache_folder(char *folder, size_t size);
static int cache_header(FILE *file, long long *modified, unsigned long long *size, int *width, int *height);

/*
 * Tells whether an item is drawn with a thumbnail: a picture, or a PDF
 * document (its first page).
 */
int
fm_thumb_kind(
	const struct fm_entry *entry)
{
	/* Folders and items without a path have none. */
	if (entry->folder != 0 ||
	    entry->path == NULL ||
	    entry->mime == NULL)
		return 0;

	/* Pictures. */
	if (entry->mime->category == FM_CATEGORY_IMAGE)
		return 1;

	/* PDF documents. */
	if (entry->mime->category == FM_CATEGORY_PDF)
		return 1;

	/* Anything else is drawn with its kind's icon. */
	return 0;
}

/*
 * Reads the kept thumbnail of a file, when the cache has one recorded for
 * the file as it is now (its modification time and size).  Returns 0, or
 * ENOENT when there is none (or a stale or damaged one).
 */
int
fm_thumb_cache_read(
	const char *path,
	struct kl_image *image)
{
	struct stat status;
	char record[FM_PATH_MAX];
	unsigned char *row;
	long long modified;
	unsigned long long size;
	FILE *file;
	size_t got;
	int width;
	int height;
	int error;
	int x;
	int y;

	/* The file as it is now, and its record's place. */
	memset(image, 0, sizeof(*image));
	error = stat(path, &status);
	if (error != 0)
		return ENOENT;
	error = cache_record_path(path, record, sizeof(record));
	if (error != 0)
		return ENOENT;

	/* The record's header, line by line: the PPM's kind, the mark with the time and size, the size, the depth. */
	file = fopen(record, "rb");
	if (file == NULL)
		return ENOENT;
	error = cache_header(file, &modified, &size, &width, &height);
	if (error != 0 ||
	    modified != (long long)status.st_mtime ||
	    size != (unsigned long long)status.st_size ||
	    width < 1 || width > CACHE_SIDE_MAX ||
	    height < 1 || height > CACHE_SIDE_MAX) {
		fclose(file);
		return ENOENT;
	}

	/* The pixels, a row at a time. */
	error = kl_image_create(image, width, height);
	if (error != 0) {
		fclose(file);
		return ENOENT;
	}

	/* A row of the record's bytes. */
	row = malloc((size_t)width * 3U);
	if (row == NULL) {
		kl_image_release(image);
		fclose(file);
		return ENOENT;
	}

	/* Each row's bytes into the image's pixels. */
	for (y = 0; y < height; y++) {
		got = fread(row, 3U, (size_t)width, file);
		if (got != (size_t)width)
			break;
		for (x = 0; x < width; x++) {
			image->pixels[(size_t)y * (image->stride) + (size_t)x] =
			    0xff000000U | ((uint32_t)row[x * 3] << 16) | ((uint32_t)row[x * 3 + 1] << 8) | (uint32_t)row[x * 3 + 2];
		}
	}

	/* The row and the record are let go. */
	free(row);
	fclose(file);

	/* A short record is no thumbnail. */
	if (y != height) {
		kl_image_release(image);
		return ENOENT;
	}

	/* Succeeded: the kept thumbnail. */
	return 0;
}

/*
 * Writes the place of a file's record in the cache (its folder made) and
 * the stamp its comment line holds for the file as it is now (its
 * modification time and size; the comment's text without "# ").  Returns
 * 0 or an errno value.
 */
int
fm_thumb_cache_target(
	const char *path,
	char *record,
	size_t record_size,
	char *stamp,
	size_t stamp_size)
{
	struct stat status;
	int written;
	int error;

	/* The file as it is now. */
	error = stat(path, &status);
	if (error != 0)
		return errno;

	/* The record's place. */
	error = cache_record_path(path, record, record_size);
	if (error != 0)
		return error;

	/* The stamp. */
	written = snprintf(stamp, stamp_size, "%s mtime=%lld size=%llu", &CACHE_MARK[2], (long long)status.st_mtime, (unsigned long long)status.st_size);
	if (written < 0 || (size_t)written >= stamp_size)
		return ENAMETOOLONG;
	return 0;
}

/*
 * Trims the cache to keep records when it has more than maximum (the
 * oldest written go first); the host tests call it with small bounds.
 * Returns how many records were removed, or -1 when the cache's folder
 * cannot be read.
 */
int
fm_thumb_cache_trim(
	unsigned maximum,
	unsigned keep)
{
	char folder[FM_PATH_MAX];
	int removed;
	int error;

	/* The cache's folder. */
	error = cache_folder(folder, sizeof(folder));
	if (error != 0)
		return -1;

	/* The trim. */
	removed = cache_trim(folder, maximum, keep);
	if (removed < 0)
		return -1;

	/* Succeeded: reports how many went. */
	return removed;
}

/* Writes the place of a file's record in the cache (the cache's folder made); returns 0 or an errno value. */
static int
cache_record_path(
	const char *path,
	char *record,
	size_t size)
{
	SHA2_CTX context;
	char folder[FM_PATH_MAX];
	uint8_t digest[SHA256_DIGEST_LENGTH];
	char hex[SHA256_DIGEST_LENGTH * 2 + 1];
	size_t index;
	int error;
	int written;

	/* The cache's folder, made when it is not there. */
	error = cache_folder(folder, sizeof(folder));
	if (error != 0)
		return error;

	/* The SHA-256 of the path, in hexadecimal. */
	SHA256Init(&context);
	SHA256Update(&context, (const uint8_t *)path, strlen(path));
	SHA256Final(digest, &context);
	for (index = 0; index < SHA256_DIGEST_LENGTH; index++)
		snprintf(hex + index * 2, 3, "%02x", digest[index]);

	/* The record's path. */
	written = snprintf(record, size, "%s/%s.ppm", folder, hex);
	if (written < 0 || (size_t)written >= size)
		return ENAMETOOLONG;

	/* Succeeded: the record's place. */
	return 0;
}

/* Writes the cache's folder, made (with its parents, the user's only) when it is not there; returns 0 or an errno value. */
static int
cache_folder(
	char *folder,
	size_t size)
{
	const char *base;
	const char *home;
	char *slash;
	int written;
	int error;

	/* $XDG_CACHE_HOME, else ~/.cache. */
	base = getenv("XDG_CACHE_HOME");
	home = getenv("HOME");
	if (base != NULL && base[0] == '/') {
		written = snprintf(folder, size, "%s/%s", base, CACHE_FOLDER);
	} else if (home != NULL && home[0] == '/') {
		written = snprintf(folder, size, "%s/.cache/%s", home, CACHE_FOLDER);
	} else {
		return ENOENT;
	}

	/* A path too long for the caller's room. */
	if (written < 0 || (size_t)written >= size)
		return ENAMETOOLONG;

	/* Each part of the path made in turn (one already there is fine). */
	for (slash = strchr(folder + 1, '/'); slash != NULL; slash = strchr(slash + 1, '/')) {
		*slash = '\0';
		error = mkdir(folder, 0700);
		*slash = '/';
		if (error != 0 && errno != EEXIST)
			return errno;
	}

	/* The folder itself. */
	error = mkdir(folder, 0700);
	if (error != 0 && errno != EEXIST)
		return errno;

	/* Succeeded: the folder is there. */
	return 0;
}

/* Reads a record's header up to its pixels; returns 0, or EINVAL for one that is not a record. */
static int
cache_header(
	FILE *file,
	long long *modified,
	unsigned long long *size,
	int *width,
	int *height)
{
	char line[160];
	char *text;
	char *end;
	long value;
	int differs;

	/* The PPM's kind. */
	text = fgets(line, sizeof(line), file);
	if (text == NULL)
		return EINVAL;
	differs = strcmp(line, "P6\n");
	if (differs != 0)
		return EINVAL;

	/* The mark, then the file's modification time and size. */
	text = fgets(line, sizeof(line), file);
	if (text == NULL)
		return EINVAL;
	differs = strncmp(line, CACHE_MARK " mtime=", sizeof(CACHE_MARK " mtime=") - 1U);
	if (differs != 0)
		return EINVAL;
	text = line + sizeof(CACHE_MARK " mtime=") - 1U;
	*modified = strtoll(text, &end, 10);
	differs = strncmp(end, " size=", 6);
	if (end == text || differs != 0)
		return EINVAL;
	text = end + 6;
	*size = strtoull(text, &end, 10);
	if (end == text || *end != '\n')
		return EINVAL;

	/* The width and the height. */
	text = fgets(line, sizeof(line), file);
	if (text == NULL)
		return EINVAL;
	value = strtol(line, &end, 10);
	if (end == line ||
	    *end != ' ' ||
	    value < 1 ||
	    value > 65535)
		return EINVAL;
	*width = (int)value;
	text = end + 1;
	value = strtol(text, &end, 10);
	if (end == text ||
	    *end != '\n' ||
	    value < 1 ||
	    value > 65535)
		return EINVAL;
	*height = (int)value;

	/* The depth, which is always 255. */
	text = fgets(line, sizeof(line), file);
	if (text == NULL)
		return EINVAL;
	differs = strcmp(line, "255\n");
	if (differs != 0)
		return EINVAL;

	/* Succeeded: the pixels follow. */
	return 0;
}

/*
 * Removes the oldest written records of the cache's folder until keep are
 * left, when it has more than maximum.  Returns how many were removed, or
 * -1 when the folder cannot be read.
 */
static int
cache_trim(
	const char *folder,
	unsigned maximum,
	unsigned keep)
{
	struct cache_entry *entries;
	char record[2 * FM_PATH_MAX];
	size_t count;
	size_t index;
	int removed;
	int error;

	/* The folder's records with their times. */
	error = cache_trim_list(folder, &entries, &count);
	if (error != 0)
		return -1;

	/* Nothing to do while the cache is within its bound. */
	removed = 0;
	if (count > (size_t)maximum) {
		/* The oldest first; the first count - keep go. */
		qsort(entries, count, sizeof(entries[0]), cache_compare_written);
		for (index = 0; index + (size_t)keep < count; index++) {
			snprintf(record, sizeof(record), "%s/%s", folder, entries[index].name);
			error = unlink(record);
			if (error == 0)
				removed++;
		}

		/* The log line the tests read. */
		fm_log("THUMB cache trim records=%lu removed=%d", (unsigned long)count, removed);
	}

	/* The list goes. */
	for (index = 0; index < count; index++)
		free(entries[index].name);
	free(entries);

	/* Reports how many went. */
	return removed;
}

/* Lists the records of the cache's folder (its regular files that are not hidden) with their times; returns 0 or an errno value. */
static int
cache_trim_list(
	const char *folder,
	struct cache_entry **entries,
	size_t *count)
{
	struct cache_entry *grown;
	struct dirent *item;
	struct stat status;
	char path[2 * FM_PATH_MAX];
	size_t capacity;
	DIR *directory;
	int regular;
	int error;

	/* Nothing listed yet. */
	*entries = NULL;
	*count = 0;
	capacity = 0;

	/* The folder. */
	directory = opendir(folder);
	if (directory == NULL)
		return errno;

	/* Each regular file of the folder that is not hidden. */
	error = 0;
	for (;;) {
		item = readdir(directory);
		if (item == NULL)
			break;

		/* The folder itself, its parent and hidden names are no records. */
		if (item->d_name[0] == '.')
			continue;

		/* Only a regular file, with the time it was written. */
		snprintf(path, sizeof(path), "%s/%s", folder, item->d_name);
		error = lstat(path, &status);
		if (error != 0) {
			error = 0;
			continue;
		}

		/* Only a regular file is a record. */
		regular = S_ISREG(status.st_mode);
		if (regular == 0)
			continue;

		/* Room for one more. */
		if (*count == capacity) {
			capacity += 256U;
			grown = realloc(*entries, capacity * sizeof(grown[0]));
			if (grown == NULL) {
				error = ENOMEM;
				break;
			}

			/* The grown list. */
			*entries = grown;
		}

		/* The record's name and time. */
		(*entries)[*count].name = strdup(item->d_name);
		if ((*entries)[*count].name == NULL) {
			error = ENOMEM;
			break;
		}

		/* When it was written. */
		(*entries)[*count].written = status.st_mtime;
		(*count)++;
	}

	/* The folder is closed whatever happened. */
	closedir(directory);

	/* Memory that ran out lists nothing. */
	if (error != 0) {
		while (*count > 0) {
			(*count)--;
			free((*entries)[*count].name);
		}

		/* The list itself goes. */
		free(*entries);
		*entries = NULL;
		return error;
	}

	/* Succeeded: the records are listed. */
	return 0;
}

/* Orders records by the time they were written, the oldest first (names break ties). */
static int
cache_compare_written(
	const void *left,
	const void *right)
{
	const struct cache_entry *first;
	const struct cache_entry *second;
	int order;

	/* The two records. */
	first = left;
	second = right;

	/* The older one first. */
	if (first->written < second->written)
		return -1;
	if (first->written > second->written)
		return 1;

	/* The same time: by name, so the order is the same on every run. */
	order = strcmp(first->name, second->name);

	/* Reports the names' order. */
	return order;
}
