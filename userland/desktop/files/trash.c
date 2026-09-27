/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The trash of files: the home trash of the freedesktop.org
 * Trash specification, so that other desktops' programs (and later GTK and
 * Qt applications on zedBSD) share it.
 *
 * $XDG_DATA_HOME/Trash (or ~/.local/share/Trash) holds files/NAME, the
 * trashed item, and info/NAME.trashinfo, which says where it was and when
 * it was trashed:
 *
 *     [Trash Info]
 *     Path=/home/user/Documents/Report%20v2.pdf
 *     DeletionDate=2026-09-27T16:20:00
 *
 * The path is escaped as a URL's path is (bytes other than letters, digits
 * and -._~/ as %XX), the date is local time.
 */

#include "ops.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The largest .trashinfo file read. */
#define TRASH_INFO_MAX		8192

static int trash_mkdir(const char *path);
static void trash_mkdir_parents(const char *path);
static int trash_hex(int character);
static void trash_escape(const char *path, char *escaped, size_t size);
static int trash_unescape(const char *escaped, char *path, size_t size);

/*
 * Writes the trash's path (its files and info folders are made when
 * missing).
 *
 * Returns 0, or an errno value when there is no home or the folders cannot
 * be made.
 */
int
fm_trash_path(
	char *path,
	size_t size)
{
	char folder[FM_OPS_PATH_MAX];
	const char *data;
	const char *home;
	int error;

	/* $XDG_DATA_HOME/Trash, or ~/.local/share/Trash. */
	data = getenv("XDG_DATA_HOME");
	home = getenv("HOME");
	if (data != NULL && data[0] == '/') {
		trash_mkdir_parents(data);
		snprintf(path, size, "%s/Trash", data);
	} else if (home != NULL && home[0] != '\0') {
		snprintf(folder, sizeof(folder), "%s/.local", home);
		(void)trash_mkdir(folder);
		snprintf(folder, sizeof(folder), "%s/.local/share", home);
		(void)trash_mkdir(folder);
		snprintf(path, size, "%s/.local/share/Trash", home);
	} else {
		return ENOENT;
	}

	/* The trash and its two folders. */
	error = trash_mkdir(path);
	if (error != 0)
		return error;
	snprintf(folder, sizeof(folder), "%s/files", path);
	error = trash_mkdir(folder);
	if (error != 0)
		return error;
	snprintf(folder, sizeof(folder), "%s/info", path);
	error = trash_mkdir(folder);
	if (error != 0)
		return error;

	/* Succeeded: the trash is there. */
	return 0;
}

/*
 * Reads where a trashed item (by its name in the trash's files) was and
 * when it was trashed.
 *
 * Returns 0, or an errno value (ENOENT when it has no record).
 */
int
fm_trash_info_read(
	const char *trash,
	const char *name,
	char *original,
	size_t size,
	time_t *deleted)
{
	struct tm moment;
	char path[FM_OPS_PATH_MAX];
	char text[TRASH_INFO_MAX + 1];
	char *line;
	char *next;
	ssize_t length;
	int descriptor;
	int found;
	int match;
	int fields;

	/* The record's text. */
	snprintf(path, sizeof(path), "%s/info/%s.trashinfo", trash, name);
	descriptor = open(path, O_RDONLY);
	if (descriptor < 0)
		return errno;
	length = read(descriptor, text, TRASH_INFO_MAX);
	close(descriptor);
	if (length < 0)
		return EIO;
	text[length] = '\0';

	/* Each line: the path and the date. */
	found = 0;
	*deleted = 0;
	for (line = text; line != NULL && *line != '\0'; line = next) {
		next = strchr(line, '\n');
		if (next != NULL) {
			*next = '\0';
			next++;
		}

		/* The escaped path. */
		match = strncmp(line, "Path=", 5);
		if (match == 0) {
			found = trash_unescape(line + 5, original, size);
			continue;
		}

		/* The local date and time. */
		match = strncmp(line, "DeletionDate=", 13);
		if (match == 0) {
			memset(&moment, 0, sizeof(moment));
			fields = sscanf(line + 13, "%d-%d-%dT%d:%d:%d", &moment.tm_year, &moment.tm_mon, &moment.tm_mday, &moment.tm_hour, &moment.tm_min, &moment.tm_sec);
			if (fields == 6) {
				moment.tm_year -= 1900;
				moment.tm_mon -= 1;
				moment.tm_isdst = -1;
				*deleted = mktime(&moment);
			}
		}
	}

	/* A record without a path says nothing. */
	if (found == 0)
		return EINVAL;

	/* Succeeded: where the item was. */
	return 0;
}

/*
 * Writes the record of a trashed item at a path: where it was and when.
 *
 * Returns 0, or an errno value.
 */
int
fm_trash_info_write(
	const char *path,
	const char *original,
	time_t deleted)
{
	struct tm *moment;
	char escaped[FM_OPS_PATH_MAX * 3];
	char text[FM_OPS_PATH_MAX * 3 + 128];
	char date[64];
	ssize_t written;
	int descriptor;
	int length;

	/* The date in local time. */
	moment = localtime(&deleted);
	if (moment == NULL)
		return EINVAL;
	snprintf(date, sizeof(date), "%04d-%02d-%02dT%02d:%02d:%02d", moment->tm_year + 1900, moment->tm_mon + 1, moment->tm_mday, moment->tm_hour, moment->tm_min, moment->tm_sec);

	/* The record's text. */
	trash_escape(original, escaped, sizeof(escaped));
	length = snprintf(text, sizeof(text), "[Trash Info]\nPath=%s\nDeletionDate=%s\n", escaped, date);
	if (length < 0 || (size_t)length >= sizeof(text))
		return ENAMETOOLONG;

	/* A new file (the name was chosen free), written in full. */
	descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (descriptor < 0)
		return errno;
	written = write(descriptor, text, (size_t)length);
	close(descriptor);
	if (written != (ssize_t)length) {
		(void)unlink(path);
		return EIO;
	}

	/* Succeeded: the record is written. */
	return 0;
}

/*
 * Chooses a name in the trash for an item's name that neither its files
 * nor its records use: the name, then "name.2", "name.3", ...
 *
 * Returns 0, or an errno value.
 */
int
fm_trash_name(
	const char *trash,
	const char *base,
	char *name,
	size_t size)
{
	struct stat status;
	char path[FM_OPS_PATH_MAX];
	unsigned number;
	int taken;
	int written;

	/* The name, then numbered ones. */
	for (number = 1; number < 100000U; number++) {
		if (number == 1U)
			written = snprintf(name, size, "%s", base);
		else
			written = snprintf(name, size, "%s.%u", base, number);
		if (written < 0 || (size_t)written >= size)
			return ENAMETOOLONG;

		/* Taken by a file in the trash. */
		snprintf(path, sizeof(path), "%s/files/%s", trash, name);
		taken = lstat(path, &status);
		if (taken == 0)
			continue;

		/* Or by a record (a stray one counts too). */
		snprintf(path, sizeof(path), "%s/info/%s.trashinfo", trash, name);
		taken = lstat(path, &status);
		if (taken == 0)
			continue;

		/* Succeeded: this name is free. */
		return 0;
	}

	/* Every name is taken (it does not happen). */
	return EEXIST;
}

/* Makes a folder unless it is there; returns 0 or an errno value. */
static int
trash_mkdir(
	const char *path)
{
	int status;

	/* The folder, private to the user. */
	status = mkdir(path, 0700);
	if (status != 0 && errno != EEXIST)
		return errno;

	/* Succeeded: the folder is there. */
	return 0;
}

/* Makes a folder and the folders above it that are missing (as mkdir -p). */
static void
trash_mkdir_parents(
	const char *path)
{
	char partial[FM_OPS_PATH_MAX];
	size_t index;

	/* Each prefix that ends at a slash, then the whole path. */
	snprintf(partial, sizeof(partial), "%s", path);
	for (index = 1; partial[index] != '\0'; index++) {
		if (partial[index] != '/')
			continue;
		partial[index] = '\0';
		(void)trash_mkdir(partial);
		partial[index] = '/';
	}

	/* The folder itself. */
	(void)trash_mkdir(partial);
}

/* Reports the value of a hexadecimal digit, or -1. */
static int
trash_hex(
	int character)
{
	/* The three ranges of digits. */
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	if (character >= 'A' && character <= 'F')
		return character - 'A' + 10;

	/* Not a digit. */
	return -1;
}

/* Escapes a path as a URL's path: letters, digits and -._~/ stay, other bytes become %XX. */
static void
trash_escape(
	const char *path,
	char *escaped,
	size_t size)
{
	static const char digits[] = "0123456789ABCDEF";
	const unsigned char *byte;
	size_t used;
	int plain;

	/* Each byte, while there is room for three more and the end. */
	used = 0;
	for (byte = (const unsigned char *)path; *byte != '\0' && used + 4U < size; byte++) {
		/* The bytes a URL's path keeps as they are. */
		plain = 0;
		if ((*byte >= 'a' && *byte <= 'z') || (*byte >= 'A' && *byte <= 'Z') || (*byte >= '0' && *byte <= '9'))
			plain = 1;
		if (*byte == '-' || *byte == '.' || *byte == '_' || *byte == '~' || *byte == '/')
			plain = 1;
		if (plain != 0) {
			escaped[used] = (char)*byte;
			used++;
			continue;
		}

		/* Any other as %XX. */
		escaped[used] = '%';
		escaped[used + 1U] = digits[*byte >> 4];
		escaped[used + 2U] = digits[*byte & 0x0fU];
		used += 3U;
	}

	/* The end of the escaped path. */
	escaped[used] = '\0';
}

/* Undoes the escaping of a path; returns 1 when it fits, 0 otherwise. */
static int
trash_unescape(
	const char *escaped,
	char *path,
	size_t size)
{
	size_t used;
	int high;
	int low;

	/* Each character: %XX is a byte, anything else itself. */
	used = 0;
	while (*escaped != '\0' && *escaped != '\r') {
		if (used + 1U >= size)
			return 0;
		high = -1;
		low = -1;
		if (escaped[0] == '%' && escaped[1] != '\0') {
			high = trash_hex(escaped[1]);
			low = trash_hex(escaped[2]);
		}

		/* Two hexadecimal digits after % are a byte; anything else is itself. */
		if (high >= 0 && low >= 0) {
			path[used] = (char)(high * 16 + low);
			escaped += 3;
		} else {
			path[used] = *escaped;
			escaped++;
		}

		/* One more byte of the path. */
		used++;
	}

	/* The end of the path. */
	path[used] = '\0';
	return 1;
}
