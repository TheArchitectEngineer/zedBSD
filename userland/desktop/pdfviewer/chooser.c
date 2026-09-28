/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The file chooser of PDF Viewer (File > Open, Ctrl+O): the folders and
 * the PDF files of one folder, folders first, each group in name order,
 * with ".." to go up.  Hidden entries are not listed.
 */

#include "viewer.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static int compare_entries(const void *left, const void *right);
static int is_pdf_name(const char *name);

/*
 * Lists a folder in place of the one listed.
 *
 * Returns 0, or an errno value (the old list is kept then).
 */
int
pv_chooser_open(
	struct pv_chooser *chooser,
	const char *folder)
{
	char resolved[PV_PATH_MAX];
	char path[PV_PATH_MAX * 2];
	struct pv_entry *entries;
	struct dirent *entry;
	struct stat status;
	DIR *directory;
	size_t count;
	int is_folder;
	int result;

	/* The folder's absolute name, when the system can say. */
	snprintf(resolved, sizeof(resolved), "%s", folder);
	if (realpath(folder, resolved) == NULL)
		snprintf(resolved, sizeof(resolved), "%s", folder);

	/* Opens the folder. */
	directory = opendir(resolved);
	if (directory == NULL)
		return errno;

	/* Allocates the entries. */
	entries = calloc(PV_CHOOSER_ENTRIES, sizeof(entries[0]));
	if (entries == NULL) {
		closedir(directory);
		return ENOMEM;
	}

	/* The parent first, except at the root. */
	count = 0;
	if (strcmp(resolved, "/") != 0) {
		snprintf(entries[0].name, sizeof(entries[0].name), "..");
		entries[0].folder = 1;
		count = 1;
	}

	/* Each visible folder and PDF file, while there is room. */
	for (;;) {
		entry = readdir(directory);
		if (entry == NULL)
			break;
		if (entry->d_name[0] == '.')
			continue;
		if (count == PV_CHOOSER_ENTRIES)
			break;
		snprintf(path, sizeof(path), "%s/%s", resolved, entry->d_name);
		result = stat(path, &status);
		if (result != 0)
			continue;
		is_folder = S_ISDIR(status.st_mode);
		if (!is_folder && !is_pdf_name(entry->d_name))
			continue;
		snprintf(entries[count].name, sizeof(entries[count].name), "%s", entry->d_name);
		entries[count].folder = is_folder;
		count++;
	}
	closedir(directory);

	/* Folders first, by name (the parent stays first). */
	if (count > 1 && strcmp(entries[0].name, "..") == 0) {
		qsort(entries + 1, count - 1, sizeof(entries[0]), compare_entries);
	} else if (count > 1) {
		qsort(entries, count, sizeof(entries[0]), compare_entries);
	}

	/* Replaces the old list. */
	free(chooser->entries);
	chooser->entries = entries;
	chooser->count = count;
	chooser->selected = 0;
	chooser->first = 0;
	snprintf(chooser->folder, sizeof(chooser->folder), "%s", resolved);

	/* Starts on the first entry after the parent. */
	if (count > 1 && strcmp(entries[0].name, "..") == 0)
		chooser->selected = 1;

	/* Succeeded: the folder is listed. */
	return 0;
}

/*
 * Frees the list.
 */
void
pv_chooser_close(
	struct pv_chooser *chooser)
{
	/* The entries, and nothing is listed. */
	free(chooser->entries);
	memset(chooser, 0, sizeof(*chooser));
}

/*
 * Makes the path of an entry of the listed folder ("..": the parent).
 *
 * Returns 0, or ENAMETOOLONG or EINVAL.
 */
int
pv_chooser_path(
	const struct pv_chooser *chooser,
	size_t index,
	char *path,
	size_t size)
{
	char *slash;
	int written;

	/* Refuses an entry the list does not have. */
	if (index >= chooser->count)
		return EINVAL;

	/* The parent: the folder without its last part. */
	if (strcmp(chooser->entries[index].name, "..") == 0) {
		snprintf(path, size, "%s", chooser->folder);
		slash = strrchr(path, '/');
		if (slash == path)
			slash[1] = '\0';
		else if (slash != NULL)
			*slash = '\0';
		return 0;
	}

	/* Any other entry under the folder. */
	if (strcmp(chooser->folder, "/") == 0) {
		written = snprintf(path, size, "/%s", chooser->entries[index].name);
	} else {
		written = snprintf(path, size, "%s/%s", chooser->folder, chooser->entries[index].name);
	}
	if (written < 0 || (size_t)written >= size)
		return ENAMETOOLONG;

	/* Succeeded: the path. */
	return 0;
}

/* Orders entries: folders first, then by name without regard to case, for qsort. */
static int
compare_entries(
	const void *left,
	const void *right)
{
	const struct pv_entry *first;
	const struct pv_entry *second;
	int order;

	/* Folders before files. */
	first = left;
	second = right;
	if (first->folder != second->folder)
		return second->folder - first->folder;

	/* By name. */
	order = strcasecmp(first->name, second->name);
	return order;
}

/* Tells whether a file name ends in .pdf, in any case. */
static int
is_pdf_name(
	const char *name)
{
	size_t length;
	int order;

	/* A name of at least one character and the extension. */
	length = strlen(name);
	if (length < 5)
		return 0;

	/* The last four characters. */
	order = strcasecmp(name + length - 4, ".pdf");
	if (order != 0)
		return 0;

	/* It is a PDF's name. */
	return 1;
}
