/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The collection (ws120-p008): the songs of a folder (~/Music) and of the
 * files opened, grouped by album.  A folder is looked through four levels
 * deep for .m4a files and .mp4 files with sound and no pictures; each
 * file's tags (tags.c) give its song.  An album is its artist (the album's
 * artist, or the song's) with its title; it keeps the first cover one of
 * its songs has.  The albums are in the order of their titles, the songs
 * in the order of their albums, then of their numbers, then of their
 * titles, so that the next song of a song is the next one of the list.
 */

#include "music.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* How deep a folder is looked through, and the most songs kept. */
#define LIBRARY_DEPTH		4
#define LIBRARY_SONGS_MAX	4096U

/* The longest path looked at, with its NUL. */
#define LIBRARY_PATH_MAX	1024U

/* What a song without them is called. */
#define LIBRARY_NO_ARTIST	"Unknown Artist"
#define LIBRARY_NO_ALBUM	"Unknown Album"

/*
 * The collection: the songs and the albums, with the room allocated for
 * each.
 */
struct library {
	struct mu_song *songs;
	size_t song_count;
	size_t song_room;
	struct mu_album *albums;
	size_t album_count;
	size_t album_room;
};

/* The collection of the program. */
static struct library library;

static int library_walk(const char *folder, int depth);
static int library_wanted(const char *name);
static int library_insert(const char *path, struct mu_tags *tags);
static int library_album(const char *artist, const char *title, size_t *album);
static int library_sort(void);
static int library_compare_albums(const void *left, const void *right);
static int library_compare_songs(const void *left, const void *right);
static int library_compare_text(const char *left, const char *right);
static int library_contains(const char *text, const char *search);
static char *library_copy(const char *text);
static const char *library_base_name(const char *path);
static int library_has_suffix(const char *name, const char *suffix);

/*
 * Looks through a folder for songs and adds them to the collection.
 * Returns 0 (a folder that does not exist has none), or an errno value.
 */
int
mu_library_scan(
	const char *folder)
{
	int error;

	/* The folder's songs. */
	error = library_walk(folder, 0);
	if (error != 0)
		return error;

	/* In order. */
	error = library_sort();
	return error;
}

/*
 * Adds a file's song to the collection (it may be outside the folder),
 * or finds it there.  Returns 0 with its index, or an errno value: the
 * file's or its tags', ENOTSUP for one without sound.
 */
int
mu_library_add_file(
	const char *path,
	long *song)
{
	struct mu_tags tags;
	size_t index;
	int same;
	int error;

	/* Already there. */
	for (index = 0; index < library.song_count; index++) {
		same = strcmp(library.songs[index].path, path);
		if (same == 0) {
			*song = (long)index;
			return 0;
		}
	}

	/* Its tags, which need a track of sound. */
	error = mu_tags_read(path, &tags);
	if (error != 0)
		return error;
	if (!tags.has_sound) {
		mu_tags_release(&tags);
		return ENOTSUP;
	}

	/* Added, and the order made again. */
	error = library_insert(path, &tags);
	mu_tags_release(&tags);
	if (error != 0)
		return error;
	error = library_sort();
	if (error != 0)
		return error;

	/* Its place in the order. */
	for (index = 0; index < library.song_count; index++) {
		same = strcmp(library.songs[index].path, path);
		if (same == 0) {
			*song = (long)index;
			return 0;
		}
	}

	/* Not reached: the song was added. */
	return ENOENT;
}

/*
 * Reports the songs, in order.
 */
const struct mu_song *
mu_songs(
	size_t *count)
{
	/* The array. */
	*count = library.song_count;
	return library.songs;
}

/*
 * Reports the albums, in order.
 */
struct mu_album *
mu_albums(
	size_t *count)
{
	/* The array. */
	*count = library.album_count;
	return library.albums;
}

/*
 * Lists the songs shown: those of an album (-1 for every one) whose
 * title, artist or album has a search (any for an empty one or NULL), in
 * order.  Returns how many indices are stored, at most the capacity.
 */
size_t
mu_library_list(
	long album,
	const char *search,
	size_t *indices,
	size_t capacity)
{
	const struct mu_song *song;
	size_t count;
	size_t index;
	int matches;

	/* Each song, in order. */
	count = 0;
	for (index = 0; index < library.song_count && count < capacity; index++) {
		/* Of the album. */
		song = &library.songs[index];
		if (album >= 0 && song->album != (size_t)album)
			continue;

		/* With the search in its title, its artist or its album. */
		if (search != NULL && search[0] != '\0') {
			matches = library_contains(song->title, search);
			if (!matches)
				matches = library_contains(song->artist, search);
			if (!matches)
				matches = library_contains(library.albums[song->album].title, search);
			if (!matches)
				continue;
		}

		/* Shown. */
		indices[count] = index;
		count++;
	}

	/* The count. */
	return count;
}

/*
 * Finds the song after (a step of 1) or before (-1) a song in the order,
 * which goes on from an album's last song to the next album's first.
 * Returns -1 past either end.
 */
long
mu_library_next(
	long song,
	int step)
{
	long next;

	/* The neighbour. */
	next = song + step;
	if (song < 0 || next < 0 || next >= (long)library.song_count)
		return -1;
	return next;
}

/*
 * Frees the collection (the albums' pictures are the view's, released before).
 */
void
mu_library_release(void)
{
	size_t index;

	/* The songs' strings. */
	for (index = 0; index < library.song_count; index++) {
		free(library.songs[index].path);
		free(library.songs[index].title);
		free(library.songs[index].artist);
	}

	/* Their array. */
	free(library.songs);

	/* The albums' strings and covers. */
	for (index = 0; index < library.album_count; index++) {
		free(library.albums[index].title);
		free(library.albums[index].artist);
		free(library.albums[index].cover);
	}

	/* Their array. */
	free(library.albums);

	/* Empty. */
	memset(&library, 0, sizeof(library));
}

/* Looks through a folder, and the folders in it to the depth, adding the songs; 0 or an errno value. */
static int
library_walk(
	const char *folder,
	int depth)
{
	struct mu_tags tags;
	struct dirent *entry;
	struct stat status;
	char path[LIBRARY_PATH_MAX];
	DIR *directory;
	int written;
	int wanted;
	int found;
	int video;
	int folder_entry;
	int regular;
	int error;

	/* The folder, which may not exist. */
	directory = opendir(folder);
	if (directory == NULL) {
		if (errno == ENOENT || errno == ENOTDIR)
			return 0;
		return errno;
	}

	/* Each entry, but the hidden ones. */
	error = 0;
	for (;;) {
		entry = readdir(directory);
		if (entry == NULL)
			break;
		if (entry->d_name[0] == '.')
			continue;

		/* Its path. */
		written = snprintf(path, sizeof(path), "%s/%s", folder, entry->d_name);
		if (written < 0 || (size_t)written >= sizeof(path))
			continue;
		found = stat(path, &status);
		if (found != 0)
			continue;

		/* A folder is looked through to the depth. */
		folder_entry = S_ISDIR(status.st_mode);
		if (folder_entry) {
			if (depth + 1 < LIBRARY_DEPTH)
				error = library_walk(path, depth + 1);
			if (error != 0)
				break;
			continue;
		}

		/* A song file, when its tags say it is one. */
		wanted = library_wanted(entry->d_name);
		regular = S_ISREG(status.st_mode);
		if (!wanted || !regular)
			continue;
		found = mu_tags_read(path, &tags);
		if (found != 0)
			continue;

		/* A song has sound; an .mp4 with pictures is a video, not a song. */
		video = library_has_suffix(entry->d_name, ".mp4");
		if (tags.has_sound && !(tags.has_video && video))
			error = library_insert(path, &tags);
		mu_tags_release(&tags);
		if (error != 0)
			break;
	}

	/* The folder is closed. */
	(void)closedir(directory);
	return error;
}

/* Tells whether a file's name is one of a song: .m4a, or .mp4 (one with sound only is kept). */
static int
library_wanted(
	const char *name)
{
	int song;

	/* By its suffix. */
	song = library_has_suffix(name, ".m4a");
	if (!song)
		song = library_has_suffix(name, ".mp4");
	return song;
}

/* Adds a song from its tags (its cover goes to its album when the album has none); 0 or an errno value. */
static int
library_insert(
	const char *path,
	struct mu_tags *tags)
{
	struct mu_song *grown;
	struct mu_song *song;
	const char *artist;
	const char *album_artist;
	const char *album_title;
	const char *title;
	char name[MU_TEXT_MAX];
	char *dot;
	size_t album;
	size_t room;
	int error;

	/* Within the most songs kept. */
	if (library.song_count >= LIBRARY_SONGS_MAX)
		return 0;

	/* What a song without its tags is called: its file's name without the suffix. */
	title = tags->title;
	if (title[0] == '\0') {
		(void)snprintf(name, sizeof(name), "%s", library_base_name(path));
		dot = strrchr(name, '.');
		if (dot != NULL && dot != name)
			*dot = '\0';
		title = name;
	}

	/* An artist and an album for a song without them. */
	artist = tags->artist;
	if (artist[0] == '\0')
		artist = LIBRARY_NO_ARTIST;
	album_artist = tags->album_artist;
	if (album_artist[0] == '\0')
		album_artist = artist;
	album_title = tags->album;
	if (album_title[0] == '\0')
		album_title = LIBRARY_NO_ALBUM;

	/* Its album, which keeps the first cover. */
	error = library_album(album_artist, album_title, &album);
	if (error != 0)
		return error;
	if (library.albums[album].cover == NULL && tags->cover != NULL) {
		library.albums[album].cover = tags->cover;
		library.albums[album].cover_size = tags->cover_size;
		tags->cover = NULL;
		tags->cover_size = 0;
	}

	/* Room for the song. */
	if (library.song_count == library.song_room) {
		room = library.song_room * 2U;
		if (room == 0U)
			room = 64U;
		grown = realloc(library.songs, room * sizeof(*grown));
		if (grown == NULL)
			return ENOMEM;
		library.songs = grown;
		library.song_room = room;
	}

	/* The song. */
	song = &library.songs[library.song_count];
	memset(song, 0, sizeof(*song));
	song->path = library_copy(path);
	song->title = library_copy(title);
	song->artist = library_copy(artist);
	song->track = tags->track;
	song->duration_ms = tags->duration_ms;
	song->album = album;
	if (song->path == NULL || song->title == NULL || song->artist == NULL) {
		free(song->path);
		free(song->title);
		free(song->artist);
		return ENOMEM;
	}

	/* Succeeded: one more. */
	library.song_count++;
	return 0;
}

/* Finds the album of an artist and a title, or makes it; 0 with its index, or ENOMEM. */
static int
library_album(
	const char *artist,
	const char *title,
	size_t *album)
{
	struct mu_album *grown;
	struct mu_album *made;
	size_t index;
	size_t room;
	int same_artist;
	int same_title;

	/* One already made. */
	for (index = 0; index < library.album_count; index++) {
		same_artist = strcmp(library.albums[index].artist, artist);
		same_title = strcmp(library.albums[index].title, title);
		if (same_artist == 0 && same_title == 0) {
			*album = index;
			return 0;
		}
	}

	/* Room for one more. */
	if (library.album_count == library.album_room) {
		room = library.album_room * 2U;
		if (room == 0U)
			room = 16U;
		grown = realloc(library.albums, room * sizeof(*grown));
		if (grown == NULL)
			return ENOMEM;
		library.albums = grown;
		library.album_room = room;
	}

	/* Made, without a cover yet. */
	made = &library.albums[library.album_count];
	memset(made, 0, sizeof(*made));
	made->title = library_copy(title);
	made->artist = library_copy(artist);
	if (made->title == NULL || made->artist == NULL) {
		free(made->title);
		free(made->artist);
		return ENOMEM;
	}

	/* Succeeded: the new album. */
	*album = library.album_count;
	library.album_count++;
	return 0;
}

/*
 * Puts the albums and the songs in order: the albums by title, the songs
 * by album, number and title.  Returns 0 or ENOMEM.
 */
static int
library_sort(void)
{
	struct mu_album *sorted;
	size_t *order;
	size_t *place;
	size_t index;

	/* Nothing to order. */
	if (library.album_count == 0U)
		return 0;

	/* The albums' order, as indices sorted by the albums they name. */
	order = malloc(library.album_count * sizeof(*order));
	place = malloc(library.album_count * sizeof(*place));
	sorted = malloc(library.album_count * sizeof(*sorted));
	if (order == NULL || place == NULL || sorted == NULL) {
		free(order);
		free(place);
		free(sorted);
		return ENOMEM;
	}

	/* Sorted. */
	for (index = 0; index < library.album_count; index++)
		order[index] = index;
	qsort(order, library.album_count, sizeof(*order), library_compare_albums);

	/* The albums moved into that order, and where each went. */
	for (index = 0; index < library.album_count; index++) {
		sorted[index] = library.albums[order[index]];
		place[order[index]] = index;
	}

	/* Copied back. */
	memcpy(library.albums, sorted, library.album_count * sizeof(*sorted));

	/* The songs follow their albums, then sort. */
	for (index = 0; index < library.song_count; index++)
		library.songs[index].album = place[library.songs[index].album];
	qsort(library.songs, library.song_count, sizeof(*library.songs), library_compare_songs);

	/* Succeeded: in order. */
	free(order);
	free(place);
	free(sorted);
	return 0;
}

/* Compares two albums by their indices: by title, then by artist. */
static int
library_compare_albums(
	const void *left,
	const void *right)
{
	const struct mu_album *first;
	const struct mu_album *second;
	int order;

	/* The albums the indices name. */
	first = &library.albums[*(const size_t *)left];
	second = &library.albums[*(const size_t *)right];

	/* The titles, then the artists. */
	order = library_compare_text(first->title, second->title);
	if (order == 0)
		order = library_compare_text(first->artist, second->artist);
	return order;
}

/* Compares two songs: by album, then by number (none last), then by title, then by path. */
static int
library_compare_songs(
	const void *left,
	const void *right)
{
	const struct mu_song *first;
	const struct mu_song *second;
	long first_track;
	long second_track;
	int order;

	/* The albums' order. */
	first = left;
	second = right;
	if (first->album < second->album)
		return -1;
	if (first->album > second->album)
		return 1;

	/* The numbers, a song without one after those with. */
	first_track = first->track;
	if (first_track <= 0)
		first_track = 0x7fffffffL;
	second_track = second->track;
	if (second_track <= 0)
		second_track = 0x7fffffffL;
	if (first_track < second_track)
		return -1;
	if (first_track > second_track)
		return 1;

	/* The titles, then the paths. */
	order = library_compare_text(first->title, second->title);
	if (order == 0)
		order = strcmp(first->path, second->path);
	return order;
}

/* Compares two texts without regard to the case of ASCII letters. */
static int
library_compare_text(
	const char *left,
	const char *right)
{
	unsigned char first;
	unsigned char second;

	/* Byte by byte, the capitals as small letters. */
	for (;;) {
		first = (unsigned char)*left;
		second = (unsigned char)*right;
		if (first >= 'A' && first <= 'Z')
			first = (unsigned char)(first - 'A' + 'a');
		if (second >= 'A' && second <= 'Z')
			second = (unsigned char)(second - 'A' + 'a');
		if (first < second)
			return -1;
		if (first > second)
			return 1;
		if (first == '\0')
			return 0;
		left++;
		right++;
	}
}

/* Tells whether a text has a search in it, without regard to the case of ASCII letters. */
static int
library_contains(
	const char *text,
	const char *search)
{
	unsigned char first;
	unsigned char second;
	size_t start;
	size_t index;

	/* Each place the search could start. */
	for (start = 0; text[start] != '\0'; start++) {
		/* The search's bytes against the text's from there. */
		for (index = 0; search[index] != '\0'; index++) {
			first = (unsigned char)text[start + index];
			second = (unsigned char)search[index];
			if (first >= 'A' && first <= 'Z')
				first = (unsigned char)(first - 'A' + 'a');
			if (second >= 'A' && second <= 'Z')
				second = (unsigned char)(second - 'A' + 'a');
			if (first != second)
				break;
		}

		/* All of the search matched. */
		if (search[index] == '\0')
			return 1;
	}

	/* Not in it. */
	return 0;
}

/* Copies a text into a new allocation (NULL when there is no memory). */
static char *
library_copy(
	const char *text)
{
	char *copy;
	size_t size;

	/* The bytes with the NUL. */
	size = strlen(text) + 1U;
	copy = malloc(size);
	if (copy == NULL)
		return NULL;
	memcpy(copy, text, size);
	return copy;
}

/* The last part of a path. */
static const char *
library_base_name(
	const char *path)
{
	const char *slash;

	/* After the last slash. */
	slash = strrchr(path, '/');
	if (slash == NULL)
		return path;
	return slash + 1;
}

/* Tells whether a name ends with a suffix, without regard to the case of ASCII letters. */
static int
library_has_suffix(
	const char *name,
	const char *suffix)
{
	size_t name_length;
	size_t suffix_length;
	int order;

	/* The end of the name against the suffix. */
	name_length = strlen(name);
	suffix_length = strlen(suffix);
	if (name_length < suffix_length)
		return 0;
	order = library_compare_text(name + name_length - suffix_length, suffix);
	return order == 0;
}
