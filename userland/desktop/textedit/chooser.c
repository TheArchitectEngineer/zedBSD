/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The file chooser of Text Editor (PDF Viewer's chooser.c), for File >
 * Open and File > Save As: the folders and the files of one folder,
 * folders first, each group in name order, with ".." to go up.  Hidden
 * entries are not listed.  Save As also has the name being typed, which
 * makes a path in the folder listed.
 */

#include "textedit.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static int compare_entries(const void *left, const void *right);

/*
 * Lists a folder in place of the one listed.
 *
 * Returns 0, or an errno value (the old list is kept then).
 */
int
te_chooser_open(
	struct te_chooser *chooser,
	const char *folder)
{
	char resolved[TE_PATH_MAX];
	char path[TE_PATH_MAX * 2];
	struct te_entry *entries;
	struct dirent *entry;
	struct stat status;
	DIR *directory;
	char *absolute;
	size_t count;
	int has_parent;
	int is_folder;
	int differs;
	int result;

	/* The folder's absolute name, when the system can say. */
	absolute = realpath(folder, resolved);
	if (absolute == NULL)
		snprintf(resolved, sizeof(resolved), "%s", folder);

	/* Opens the folder. */
	directory = opendir(resolved);
	if (directory == NULL)
		return errno;

	/* Allocates the entries. */
	entries = calloc(TE_CHOOSER_ENTRIES, sizeof(entries[0]));
	if (entries == NULL) {
		closedir(directory);
		return ENOMEM;
	}

	/* The parent first, except at the root. */
	count = 0;
	differs = strcmp(resolved, "/");
	if (differs != 0) {
		snprintf(entries[0].name, sizeof(entries[0].name), "..");
		entries[0].folder = 1;
		count = 1;
	}

	/* Each visible folder and file, while there is room. */
	for (;;) {
		entry = readdir(directory);
		if (entry == NULL)
			break;
		if (entry->d_name[0] == '.')
			continue;
		if (count == TE_CHOOSER_ENTRIES)
			break;
		snprintf(path, sizeof(path), "%s/%s", resolved, entry->d_name);
		result = stat(path, &status);
		if (result != 0)
			continue;
		is_folder = S_ISDIR(status.st_mode);

		/* The entry is listed. */
		snprintf(entries[count].name, sizeof(entries[count].name), "%s", entry->d_name);
		entries[count].folder = is_folder;
		count++;
	}

	/* The folder is read. */
	closedir(directory);

	/* Folders first, by name (the parent stays first). */
	has_parent = 0;
	if (count > 0) {
		differs = strcmp(entries[0].name, "..");
		if (differs == 0)
			has_parent = 1;
	}

	/* Sorts the entries after the parent. */
	if (count > 1)
		qsort(entries + has_parent, count - (size_t)has_parent, sizeof(entries[0]), compare_entries);

	/* Replaces the old list. */
	free(chooser->entries);
	chooser->entries = entries;
	chooser->count = count;
	chooser->selected = 0;
	chooser->first = 0;
	snprintf(chooser->folder, sizeof(chooser->folder), "%s", resolved);

	/* Starts on the first entry after the parent. */
	if (count > 1 && has_parent)
		chooser->selected = 1;

	/* Succeeded: the folder is listed. */
	return 0;
}

/*
 * Frees the list.
 */
void
te_chooser_close(
	struct te_chooser *chooser)
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
te_chooser_path(
	const struct te_chooser *chooser,
	size_t index,
	char *path,
	size_t size)
{
	char *slash;
	int differs;
	int written;

	/* Refuses an entry the list does not have. */
	if (index >= chooser->count)
		return EINVAL;

	/* The parent: the folder without its last part. */
	differs = strcmp(chooser->entries[index].name, "..");
	if (differs == 0) {
		snprintf(path, size, "%s", chooser->folder);
		slash = strrchr(path, '/');
		if (slash == path)
			slash[1] = '\0';
		else if (slash != NULL)
			*slash = '\0';
		return 0;
	}

	/* Any other entry under the folder. */
	differs = strcmp(chooser->folder, "/");
	if (differs == 0) {
		written = snprintf(path, size, "/%s", chooser->entries[index].name);
	} else {
		written = snprintf(path, size, "%s/%s", chooser->folder, chooser->entries[index].name);
	}

	/* A path longer than the room is refused. */
	if (written < 0 || (size_t)written >= size)
		return ENAMETOOLONG;

	/* Succeeded: the path. */
	return 0;
}

/*
 * Makes the path Save As saves to: the name being typed in the listed
 * folder.
 *
 * Returns 0, EINVAL for an empty name or one with a slash, or ENAMETOOLONG.
 */
int
te_chooser_name_path(
	const struct te_chooser *chooser,
	char *path,
	size_t size)
{
	const char *slash;
	int differs;
	int written;

	/* A name, and only a name. */
	if (chooser->name_length == 0U)
		return EINVAL;
	slash = strchr(chooser->name, '/');
	if (slash != NULL)
		return EINVAL;

	/* The name under the folder. */
	differs = strcmp(chooser->folder, "/");
	if (differs == 0) {
		written = snprintf(path, size, "/%s", chooser->name);
	} else {
		written = snprintf(path, size, "%s/%s", chooser->folder, chooser->name);
	}

	/* A path longer than the room is refused. */
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
	const struct te_entry *first;
	const struct te_entry *second;
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
