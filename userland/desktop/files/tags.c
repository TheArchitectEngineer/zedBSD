/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The tags of files (spec §20).
 *
 * A file's tags are kept with the file, in its extended attribute
 * user.zdesktop.tags: the tags' names, one a line.  They move and are
 * renamed with the file, and a copy carries them.  The tags the window
 * knows -- their names and colors, in the sidebar's order -- are
 * $XDG_CONFIG_HOME/zdesktop/tags (NAME<TAB>#RRGGBB a line), or five
 * defaults.  Names the window does not know are kept in the attribute
 * untouched.
 *
 * So that a tag's place in the sidebar lists its files without walking the
 * disk, the window keeps an index, $XDG_DATA_HOME/zdesktop/tag-index (TAG
 * <TAB> PATH a line), updated when it tags a file; the listing checks each
 * file's attribute, so a stale line only costs a look.  Files tagged by
 * other programs are found by a search (tag:NAME).
 */

#include "files.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/xattr.h>

/* The extended attribute that holds a file's tags. */
#define TAGS_ATTRIBUTE		"user.zdesktop.tags"

/* The largest attribute value read or written. */
#define TAGS_VALUE_MAX		2048

/* The largest index line. */
#define TAGS_LINE_MAX		(FM_PATH_MAX + 64)

/*
 * One default tag: its name and color (the mock-up's work, private,
 * ideas, reference and archive).
 */
struct tags_default {
	const char *name;
	uint32_t color;
};

/* The tags a new account starts with. */
static const struct tags_default tags_defaults[] = {
	{ "Work", 0x3b82f6U },
	{ "Personal", 0x8b5cf6U },
	{ "Ideas", 0xec4899U },
	{ "Reference", 0xf59e0bU },
	{ "Archive", 0x9ca3afU }
};

static void tags_folder(const char *variable, const char *fallback, const char *leaf, char *path, size_t size);
static int tags_add_known(struct fm_tags *tags, const char *name, fm_color color);
static void tags_index_update(const struct fm_tags *tags, const char *path, unsigned mask);
static void tags_mkdir_parents(const char *path);

/*
 * Reads the tags the window knows (the user's file, or the defaults).
 */
void
fm_tags_load(
	struct fm_tags *tags)
{
	char path[FM_PATH_MAX];
	char line[128];
	char *tab;
	char *newline;
	char *read;
	FILE *file;
	unsigned long color;
	size_t index;
	int error;

	/* Nothing yet. */
	memset(tags, 0, sizeof(*tags));

	/* The user's file, when there is one. */
	tags_folder("XDG_CONFIG_HOME", ".config", "zdesktop/tags", path, sizeof(path));
	file = fopen(path, "r");
	if (file != NULL) {
		for (;;) {
			read = fgets(line, sizeof(line), file);
			if (read == NULL)
				break;

			/* NAME<TAB>#RRGGBB. */
			newline = strchr(line, '\n');
			if (newline != NULL)
				*newline = '\0';
			tab = strchr(line, '\t');
			if (tab == NULL || tab[1] != '#')
				continue;
			*tab = '\0';
			color = strtoul(tab + 2, NULL, 16);
			error = tags_add_known(tags, line, FM_RGB(color));
			if (error != 0)
				break;
		}

		/* The file is not needed any more. */
		fclose(file);
	}

	/* Without any, the defaults. */
	if (tags->count != 0)
		return;
	for (index = 0; index < sizeof(tags_defaults) / sizeof(tags_defaults[0]); index++)
		(void)tags_add_known(tags, tags_defaults[index].name, FM_RGB(tags_defaults[index].color));
}

/*
 * Reports the tags of a file the window knows, as a mask (bit n for the
 * n-th tag); 0 when it has none or cannot be read.
 */
unsigned
fm_tags_of(
	const struct fm_tags *tags,
	const char *path)
{
	char value[TAGS_VALUE_MAX + 1];
	char *line;
	char *next;
	ssize_t length;
	unsigned mask;
	int index;
	int match;

	/* The attribute. */
	length = getxattr(path, TAGS_ATTRIBUTE, value, TAGS_VALUE_MAX);
	if (length <= 0)
		return 0;
	value[length] = '\0';

	/* Each name a line, matched against the known tags. */
	mask = 0;
	for (line = value; line != NULL && *line != '\0'; line = next) {
		next = strchr(line, '\n');
		if (next != NULL) {
			*next = '\0';
			next++;
		}

		/* A known name sets its bit. */
		for (index = 0; index < tags->count; index++) {
			match = strcmp(line, tags->items[index].name);
			if (match == 0)
				mask |= 1U << index;
		}
	}

	/* Reports the mask. */
	return mask;
}

/*
 * Sets the known tags of a file to a mask (names the window does not know
 * stay), and keeps the index in step.
 *
 * Returns 0, or an errno value (ENOTSUP where the file system has no
 * extended attributes).
 */
int
fm_tags_write(
	const struct fm_tags *tags,
	const char *path,
	unsigned mask)
{
	char value[TAGS_VALUE_MAX + 1];
	char result[TAGS_VALUE_MAX + 1];
	char *line;
	char *next;
	ssize_t length;
	size_t used;
	int status;
	int index;
	int known;
	int match;

	/* The names there now (none when there is no attribute). */
	length = getxattr(path, TAGS_ATTRIBUTE, value, TAGS_VALUE_MAX);
	if (length < 0)
		length = 0;
	value[length] = '\0';

	/* The names the window does not know, kept. */
	used = 0;
	result[0] = '\0';
	for (line = value; line != NULL && *line != '\0'; line = next) {
		next = strchr(line, '\n');
		if (next != NULL) {
			*next = '\0';
			next++;
		}

		/* A known name is written from the mask instead. */
		known = 0;
		for (index = 0; index < tags->count; index++) {
			match = strcmp(line, tags->items[index].name);
			if (match == 0)
				known = 1;
		}

		/* An unknown name is kept when it fits. */
		length = (ssize_t)strlen(line);
		if (known != 0 || used + (size_t)length + 2U > TAGS_VALUE_MAX)
			continue;
		used += (size_t)snprintf(result + used, sizeof(result) - used, "%s\n", line);
	}

	/* The known names of the mask, in the sidebar's order. */
	for (index = 0; index < tags->count; index++) {
		if ((mask & (1U << index)) == 0U)
			continue;
		length = (ssize_t)strlen(tags->items[index].name);
		if (used + (size_t)length + 2U > TAGS_VALUE_MAX)
			break;
		used += (size_t)snprintf(result + used, sizeof(result) - used, "%s\n", tags->items[index].name);
	}

	/* The attribute written, or removed when no name is left. */
	if (used == 0U) {
		status = removexattr(path, TAGS_ATTRIBUTE);
		if (status != 0 && errno != ENODATA && errno != ENOENT)
			return errno;
	} else {
		status = setxattr(path, TAGS_ATTRIBUTE, result, used, 0);
		if (status != 0)
			return errno;
	}

	/* The index follows. */
	tags_index_update(tags, path, mask);

	/* Succeeded: the file has those tags. */
	return 0;
}

/*
 * Reads the paths the index lists for a tag (allocated; free with
 * fm_paths_free).  Only paths whose file still has the tag are reported.
 */
int
fm_tags_paths(
	const struct fm_tags *tags,
	int tag,
	char ***paths,
	size_t *count)
{
	char index_path[FM_PATH_MAX];
	char line[TAGS_LINE_MAX];
	char **grown;
	char *tab;
	char *newline;
	char *read;
	FILE *file;
	unsigned mask;
	size_t known;
	int match;
	int duplicate;

	/* Nothing yet. */
	*paths = NULL;
	*count = 0;
	if (tag < 0 || tag >= tags->count)
		return EINVAL;

	/* The index. */
	tags_folder("XDG_DATA_HOME", ".local/share", "zdesktop/tag-index", index_path, sizeof(index_path));
	file = fopen(index_path, "r");
	if (file == NULL)
		return 0;

	/* Each line of the tag whose file still has it (once). */
	for (;;) {
		read = fgets(line, sizeof(line), file);
		if (read == NULL)
			break;
		newline = strchr(line, '\n');
		if (newline != NULL)
			*newline = '\0';
		tab = strchr(line, '\t');
		if (tab == NULL)
			continue;
		*tab = '\0';
		match = strcmp(line, tags->items[tag].name);
		if (match != 0)
			continue;

		/* The file must still have the tag. */
		mask = fm_tags_of(tags, tab + 1);
		if ((mask & (1U << tag)) == 0U)
			continue;

		/* A path listed twice is taken once. */
		duplicate = 0;
		for (known = 0; known < *count; known++) {
			match = strcmp((*paths)[known], tab + 1);
			if (match == 0)
				duplicate = 1;
		}

		/* A path listed twice is taken once. */
		if (duplicate != 0)
			continue;

		/* One more path. */
		grown = realloc(*paths, (*count + 1U) * sizeof(char *));
		if (grown == NULL)
			break;
		*paths = grown;
		(*paths)[*count] = strdup(tab + 1);
		if ((*paths)[*count] == NULL)
			break;
		(*count)++;
	}

	/* The index is not needed any more. */
	fclose(file);

	/* Succeeded: the paths of the tag. */
	return 0;
}

/*
 * Reports the index of a known tag by its name (without regard to ASCII
 * case), or -1.
 */
int
fm_tags_find(
	const struct fm_tags *tags,
	const char *name)
{
	int index;
	int match;

	/* Each known tag. */
	for (index = 0; index < tags->count; index++) {
		match = strcasecmp(tags->items[index].name, name);
		if (match == 0)
			return index;
	}

	/* No tag of that name. */
	return -1;
}

/* Writes a per-user path: $VARIABLE/leaf, or ~/fallback/leaf. */
static void
tags_folder(
	const char *variable,
	const char *fallback,
	const char *leaf,
	char *path,
	size_t size)
{
	const char *base;
	const char *home;

	/* The variable when it is an absolute path, else under the home folder. */
	base = getenv(variable);
	if (base != NULL && base[0] == '/') {
		snprintf(path, size, "%s/%s", base, leaf);
		return;
	}

	/* Under the home folder (or the root without one). */
	home = getenv("HOME");
	if (home == NULL)
		home = "";
	snprintf(path, size, "%s/%s/%s", home, fallback, leaf);
}

/* Adds a known tag; nonzero when the table is full. */
static int
tags_add_known(
	struct fm_tags *tags,
	const char *name,
	fm_color color)
{
	/* The table holds a fixed number of tags. */
	if (tags->count == FM_TAGS)
		return ENOSPC;

	/* The tag after the others. */
	snprintf(tags->items[tags->count].name, sizeof(tags->items[tags->count].name), "%.47s", name);
	tags->items[tags->count].color = color;
	tags->count++;

	/* Succeeded: the tag is known. */
	return 0;
}

/* Lists a path in the index under the known tags of a mask and no others (the index is rewritten whole; it is small). */
static void
tags_index_update(
	const struct fm_tags *tags,
	const char *path,
	unsigned mask)
{
	char index_path[FM_PATH_MAX];
	char temporary[FM_PATH_MAX + 8];
	char folder[FM_PATH_MAX];
	char line[TAGS_LINE_MAX];
	char copy[TAGS_LINE_MAX];
	char *tab;
	char *read;
	char *slash;
	FILE *in;
	FILE *out;
	int known;
	int same_path;

	/* The index and a new one beside it (its folder made when missing). */
	tags_folder("XDG_DATA_HOME", ".local/share", "zdesktop/tag-index", index_path, sizeof(index_path));
	snprintf(folder, sizeof(folder), "%s", index_path);
	slash = strrchr(folder, '/');
	if (slash != NULL) {
		*slash = '\0';
		tags_mkdir_parents(folder);
	}

	/* The new index beside it. */
	snprintf(temporary, sizeof(temporary), "%s.new", index_path);
	out = fopen(temporary, "w");
	if (out == NULL)
		return;

	/* Every line but the path's under a known tag, copied. */
	in = fopen(index_path, "r");
	while (in != NULL) {
		read = fgets(line, sizeof(line), in);
		if (read == NULL)
			break;

		/* The line split at its tab (a copy keeps the whole line). */
		snprintf(copy, sizeof(copy), "%s", line);
		tab = strchr(line, '\t');
		if (tab == NULL)
			continue;
		*tab = '\0';
		tab[strcspn(tab + 1, "\n") + 1] = '\0';

		/* The path's line under a known tag goes (it is written again below when the mask has it). */
		known = fm_tags_find(tags, line);
		same_path = strcmp(tab + 1, path);
		if (known >= 0 && same_path == 0)
			continue;
		fputs(copy, out);
	}

	/* The old index is not needed any more. */
	if (in != NULL)
		fclose(in);

	/* The path under each tag of the mask. */
	for (known = 0; known < tags->count; known++) {
		if ((mask & (1U << known)) != 0U)
			fprintf(out, "%s\t%s\n", tags->items[known].name, path);
	}

	/* The new index replaces the old. */
	fclose(out);
	(void)rename(temporary, index_path);
}

/* Makes a folder and the folders above it that are missing (as mkdir -p). */
static void
tags_mkdir_parents(
	const char *path)
{
	char partial[FM_PATH_MAX];
	size_t index;

	/* Each prefix that ends at a slash, then the whole path. */
	snprintf(partial, sizeof(partial), "%s", path);
	for (index = 1; partial[index] != '\0'; index++) {
		if (partial[index] != '/')
			continue;
		partial[index] = '\0';
		(void)mkdir(partial, 0700);
		partial[index] = '/';
	}

	/* The folder itself. */
	(void)mkdir(partial, 0700);
}
