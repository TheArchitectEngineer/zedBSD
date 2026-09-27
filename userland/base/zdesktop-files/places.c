/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The places of the sidebar and the names of places.
 *
 * Favorites are the home dashboard and the usual folders under the home
 * folder; Locations are the recent files, the trash and the computer's
 * root; Tags are the tags' colored dots.  A favorite whose folder does not
 * exist is kept (pale), so the sidebar keeps its shape on a new account.
 */

#include "files.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/*
 * One of the usual folders under the home folder.
 */
struct places_folder {
	const char *label;
	const char *name;
	unsigned icon;
};

/*
 * One of the tags shown before the user makes their own.
 */
struct places_tag {
	const char *label;
	fm_color color;
};

/* The usual folders, in the sidebar's order. */
static const struct places_folder places_folders[] = {
	{ "Desktop", "Desktop", FM_ICON_DESKTOP },
	{ "Documents", "Documents", FM_ICON_DOCUMENTS },
	{ "Downloads", "Downloads", FM_ICON_DOWNLOADS },
	{ "Pictures", "Pictures", FM_ICON_PICTURES },
	{ "Music", "Music", FM_ICON_MUSIC },
	{ "Movies", "Movies", FM_ICON_MOVIES }
};

/* The default tags (the mock-up's work, private, ideas, reference and archive). */
static const struct places_tag places_tags[] = {
	{ "Work", FM_RGB(0x3b82f6) },
	{ "Personal", FM_RGB(0x8b5cf6) },
	{ "Ideas", FM_RGB(0xec4899) },
	{ "Reference", FM_RGB(0xf59e0b) },
	{ "Archive", FM_RGB(0x9ca3af) }
};

static struct fm_place *places_add(struct fm_places *places, unsigned section, unsigned icon, const char *label, unsigned kind, const char *path);

/*
 * Fills the sidebar with its default places for a home folder.
 */
void
fm_places_init(
	struct fm_places *places,
	const char *home)
{
	struct fm_place *place;
	struct stat status;
	char path[FM_PATH_MAX];
	size_t index;
	int error;

	/* The sidebar starts empty. */
	memset(places, 0, sizeof(*places));

	/* Favorites: the home dashboard first. */
	(void)places_add(places, FM_SECTION_FAVORITES, FM_ICON_HOME, "Home", FM_LOCATION_HOME, home);

	/* Then the usual folders, pale when they do not exist. */
	for (index = 0; index < sizeof(places_folders) / sizeof(places_folders[0]); index++) {
		snprintf(path, sizeof(path), "%s/%s", home, places_folders[index].name);
		place = places_add(places, FM_SECTION_FAVORITES, places_folders[index].icon, places_folders[index].label, FM_LOCATION_FOLDER, path);
		if (place == NULL)
			break;

		/* A missing folder is shown pale. */
		error = stat(path, &status);
		if (error != 0)
			place->missing = 1;
	}

	/* Locations: the recent files, the trash and the computer's root. */
	(void)places_add(places, FM_SECTION_LOCATIONS, FM_ICON_RECENTS, "Recents", FM_LOCATION_RECENTS, "");
	(void)places_add(places, FM_SECTION_LOCATIONS, FM_ICON_TRASH, "Trash", FM_LOCATION_TRASH, "");
	(void)places_add(places, FM_SECTION_LOCATIONS, FM_ICON_COMPUTER, "Computer", FM_LOCATION_FOLDER, "/");

	/* Tags: the default ones, each with its color. */
	for (index = 0; index < sizeof(places_tags) / sizeof(places_tags[0]); index++) {
		place = places_add(places, FM_SECTION_TAGS, 0, places_tags[index].label, FM_LOCATION_TAG, places_tags[index].label);
		if (place == NULL)
			break;
		place->color = places_tags[index].color;
	}
}

/*
 * Reports the name a place is shown by: the dashboard is Home, the home
 * folder is Home, the root is Computer, and a folder is its last part.
 */
const char *
fm_location_name(
	const struct fm_location *location,
	const char *home)
{
	const char *slash;
	int match;

	/* Places that are not folders have names of their own. */
	switch (location->kind) {
	case FM_LOCATION_HOME:
		return "Home";
	case FM_LOCATION_RECENTS:
		return "Recents";
	case FM_LOCATION_TRASH:
		return "Trash";
	case FM_LOCATION_TAG:
		return location->path;
	case FM_LOCATION_SEARCH:
		return "Search";
	default:
		break;
	}

	/* The home folder and the root. */
	match = strcmp(location->path, home);
	if (match == 0)
		return "Home";
	match = strcmp(location->path, "/");
	if (match == 0)
		return "Computer";

	/* A folder is named by the last part of its path. */
	slash = strrchr(location->path, '/');
	if (slash == NULL || slash[1] == '\0')
		return location->path;

	/* Reports the part after the last slash. */
	return slash + 1;
}

/*
 * Writes the names of the tags of a mask (the sidebar's tags in order,
 * bit n for the n-th), separated by commas.
 */
void
fm_tags_text(
	struct fm_app *app,
	unsigned tags,
	char *text,
	size_t length)
{
	const struct fm_place *place;
	size_t used;
	int number;
	int index;

	/* Each tag of the sidebar whose bit is set. */
	text[0] = '\0';
	used = 0;
	number = 0;
	for (index = 0; index < app->places.count; index++) {
		place = &app->places.items[index];
		if (place->section != FM_SECTION_TAGS)
			continue;

		/* A tag in the mask is named, after a comma when others came before. */
		if ((tags & (1U << number)) != 0U && used + 1U < length) {
			if (used != 0U)
				used += (size_t)snprintf(text + used, length - used, ", ");
			if (used < length)
				used += (size_t)snprintf(text + used, length - used, "%s", place->label);
		}

		/* The next tag of the sidebar has the next bit. */
		number++;
	}
}

/* Adds a place to a section of the sidebar; NULL when the sidebar is full. */
static struct fm_place *
places_add(
	struct fm_places *places,
	unsigned section,
	unsigned icon,
	const char *label,
	unsigned kind,
	const char *path)
{
	struct fm_place *place;

	/* The sidebar holds a fixed number of places. */
	if (places->count == FM_PLACES)
		return NULL;

	/* The place, after the others. */
	place = &places->items[places->count];
	memset(place, 0, sizeof(*place));
	place->section = section;
	place->icon = icon;
	snprintf(place->label, sizeof(place->label), "%s", label);
	place->location.kind = kind;
	snprintf(place->location.path, sizeof(place->location.path), "%s", path);
	places->count++;

	/* Reports the new place. */
	return place;
}
