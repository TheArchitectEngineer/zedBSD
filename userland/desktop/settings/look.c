/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The desktop's look as Settings keeps it (ws089-p004): the user's
 * preferences (libkeiland's, ~/.config/keiland/desktop.conf, which zdesktop
 * follows within a second, ws089-p007), the pictures the Wallpaper page
 * offers with their small copies, and the file systems the Storage page
 * shows.
 *
 * Settings writes a key when the user has chosen: a picture clicked, a
 * slider let go.  The file is read again once a second, so that a change
 * made elsewhere shows too.
 */

#include "settings.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

/* How often the preferences are read again, in milliseconds. */
#define LOOK_CHECK_MS		1000U

/* The windows' opacity: its range and its default, in percent. */
#define LOOK_OPACITY_MIN	85
#define LOOK_OPACITY_MAX	100

/* The input's keys: their ranges and defaults, as zdesktop takes them (userland/desktop/wayland/preferences.c). */
#define LOOK_SPEED_MIN		25
#define LOOK_SPEED_MAX		300
#define LOOK_SPEED_DEFAULT	100
#define LOOK_RATE_MIN		5
#define LOOK_RATE_MAX		60
#define LOOK_RATE_DEFAULT	25
#define LOOK_DELAY_MIN		150
#define LOOK_DELAY_MAX		1000
#define LOOK_DELAY_DEFAULT	400

/* Where the sound service listens (userland/base/audiod/protocol.h). */
#define LOOK_SOUND_SOCKET	"/run/audiod.sock"

/* The pictures: the default (the session's --wallpaper) and the folder of the others. */
#define LOOK_DEFAULT_PICTURE	"/usr/share/keiland/wallpaper.ppm"
#define LOOK_PICTURES		"/usr/share/keiland/wallpapers"

/* A picture's small copy, in pixels. */
#define LOOK_THUMB_WIDTH	240
#define LOOK_THUMB_HEIGHT	150

/* The largest picture file read (a 4K PPM is 25 MB). */
#define LOOK_PICTURE_MAX	(64U * 1024U * 1024U)

/* The places the Storage page looks at (a place on the same file system as one before it is not shown again). */
static const char *const look_places[] = { "/", "/home", "/usr", "/var", "/tmp", "/boot" };

static void look_read(struct se_app *app);
static int look_write(struct se_app *app, const char *key, const char *value);
static void look_add_picture(struct se_app *app, const char *path, const char *name);
static int look_thumbnail(const char *path, struct fm_image *image);
static int look_ppm_number(const unsigned char *data, size_t size, size_t *at, unsigned *number);
static unsigned char *look_file(const char *path, size_t *size);
static int look_compare_names(const void *left, const void *right);

/*
 * Opens the user's preferences and reads what the look's pages show.  Without
 * a home nothing can be saved, and the pages say so.
 */
void
se_look_open(
	struct se_app *app)
{
	struct se_look *look;

	/* Nothing read yet. */
	look = &app->look;
	look->opacity = LOOK_OPACITY_MAX;
	look->pointer_speed = LOOK_SPEED_DEFAULT;
	look->pointer_natural = 0;
	look->repeat_rate = LOOK_RATE_DEFAULT;
	look->repeat_delay = LOOK_DELAY_DEFAULT;
	look->wallpaper[0] = '\0';

	/* The file; without a home the pages still show the defaults. */
	look->preferences = keiland_preferences_open();
	if (look->preferences == NULL) {
		look->open_error = errno;
		se_log("LOOK none errno=%d", look->open_error);
		return;
	}

	/* What it holds. */
	look_read(app);
	look->checked_at = app->now;
	se_log("LOOK open opacity=%d wallpaper=%s", look->opacity, look->wallpaper);
}

/*
 * Reads the preferences again once a second; a change made elsewhere is
 * shown.
 */
void
se_look_poll(
	struct se_app *app,
	uint64_t now)
{
	struct se_look *look;
	int changed;
	int error;

	/* Nothing to read, or not yet. */
	look = &app->look;
	if (look->preferences == NULL)
		return;
	if (now - look->checked_at < LOOK_CHECK_MS)
		return;
	look->checked_at = now;

	/* The file, when it moved; a drag in progress keeps its own value. */
	error = keiland_preferences_reload(look->preferences, &changed);
	if (error != 0 || changed == 0)
		return;
	if (look->dragging != 0)
		return;

	/* The new values, drawn. */
	look_read(app);
	app->dirty = 1;
	se_log("LOOK changed opacity=%d wallpaper=%s", look->opacity, look->wallpaper);
}

/*
 * Closes the preferences and frees the pictures' small copies.
 */
void
se_look_close(
	struct se_app *app)
{
	struct se_look *look;
	unsigned index;

	/* The small copies. */
	look = &app->look;
	for (index = 0; index < look->wallpaper_count; index++)
		fm_image_release(&look->wallpapers[index].thumbnail);
	look->wallpaper_count = 0;

	/* The file. */
	if (look->preferences != NULL)
		keiland_preferences_close(look->preferences);
	look->preferences = NULL;
}

/*
 * Saves the windows' opacity (85 to 100 percent), which zdesktop takes
 * within a second; 100 removes the key (the desktop's own).
 */
void
se_look_set_opacity(
	struct se_app *app,
	int percent)
{
	char value[16];
	int error;

	/* Within the range. */
	if (percent < LOOK_OPACITY_MIN)
		percent = LOOK_OPACITY_MIN;
	if (percent > LOOK_OPACITY_MAX)
		percent = LOOK_OPACITY_MAX;
	app->look.opacity = percent;

	/* The key, or none for fully opaque. */
	if (percent == LOOK_OPACITY_MAX) {
		error = look_write(app, "window.opacity", NULL);
	} else {
		(void)snprintf(value, sizeof(value), "%d", percent);
		error = look_write(app, "window.opacity", value);
	}

	/* The log line the tests read. */
	se_log("LOOK set key=window.opacity value=%d error=%d", percent, error);
}

/*
 * Saves a whole number under a key, which zdesktop takes within a second;
 * the default value removes the key (the desktop's own).
 */
void
se_look_set_number(
	struct se_app *app,
	const char *key,
	int value,
	int fallback)
{
	char text[16];
	int error;

	/* The key, or none for the default. */
	if (value == fallback) {
		error = look_write(app, key, NULL);
	} else {
		(void)snprintf(text, sizeof(text), "%d", value);
		error = look_write(app, key, text);
	}

	/* The log line the tests read. */
	se_log("LOOK set key=%s value=%d error=%d", key, value, error);
}

/*
 * Tells whether the sound service is running (its socket is there).
 */
int
se_look_sound(
	void)
{
	struct stat status;
	int result;

	/* The service's socket. */
	result = stat(LOOK_SOUND_SOCKET, &status);
	if (result != 0)
		return 0;

	/* Only a socket counts. */
	if ((status.st_mode & S_IFMT) != S_IFSOCK)
		return 0;

	/* The service is there. */
	return 1;
}

/*
 * Chooses a picture of the Wallpaper page by its index (the default's, or
 * index -1 when there is no default picture, removes the key).
 */
void
se_look_set_wallpaper(
	struct se_app *app,
	int index)
{
	struct se_look *look;
	const char *path;
	int error;

	/* The default removes the key; any other picture is written. */
	look = &app->look;
	path = NULL;
	if (index >= 0 && (unsigned)index < look->wallpaper_count) {
		if (index != 0 || look->has_default == 0)
			path = look->wallpapers[index].path;
	}

	/* The key, and what the page shows chosen. */
	error = look_write(app, "wallpaper", path);
	if (error == 0) {
		look->wallpaper[0] = '\0';
		if (path != NULL)
			(void)snprintf(look->wallpaper, sizeof(look->wallpaper), "%s", path);
	}

	/* The log line the tests read. */
	se_log("LOOK set key=wallpaper value=%s error=%d", look->wallpaper, error);
}

/*
 * Finds the pictures the Wallpaper page offers and reads their small
 * copies (once, when the page is first shown): the default first, then
 * the folder's, by name.
 */
void
se_look_scan(
	struct se_app *app)
{
	struct se_look *look;
	struct dirent *entry;
	char names[SE_WALLPAPERS][64];
	char path[SE_PATH];
	DIR *folder;
	unsigned count;
	unsigned index;
	size_t length;
	int differs;

	/* Once. */
	look = &app->look;
	if (look->scanned != 0)
		return;
	look->scanned = 1;

	/* The default picture, when the image has one (tile 0; else tile 0 is the drawn landscape). */
	look_add_picture(app, LOOK_DEFAULT_PICTURE, "Kei");
	look->has_default = 1;

	/* The folder's pictures (PPM files), at most as many as fit after the default. */
	count = 0;
	folder = opendir(LOOK_PICTURES);
	while (folder != NULL && count < SE_WALLPAPERS - 1U) {
		entry = readdir(folder);
		if (entry == NULL)
			break;

		/* Only a name ending in .ppm, short enough. */
		length = strlen(entry->d_name);
		if (length <= 4U || length >= sizeof(names[0]))
			continue;
		differs = strcmp(entry->d_name + length - 4U, ".ppm");
		if (differs != 0)
			continue;

		/* The name is kept for sorting. */
		(void)snprintf(names[count], sizeof(names[count]), "%s", entry->d_name);
		count++;
	}

	/* The folder is not needed any more. */
	if (folder != NULL)
		(void)closedir(folder);

	/* By name, each with its small copy. */
	qsort(names, count, sizeof(names[0]), look_compare_names);
	for (index = 0; index < count; index++) {
		(void)snprintf(path, sizeof(path), "%s/%.63s", LOOK_PICTURES, names[index]);
		names[index][strlen(names[index]) - 4U] = '\0';
		look_add_picture(app, path, names[index]);
	}

	/* The log line the tests read. */
	se_log("LOOK pictures count=%u", look->wallpaper_count);
}

/*
 * Reads the file systems the Storage page shows: each place's, once for a
 * file system.
 */
void
se_look_volumes(
	struct se_app *app)
{
	struct se_look *look;
	struct se_volume *volume;
	struct statvfs status;
	uint64_t seen[SE_VOLUMES];
	unsigned place;
	unsigned index;
	int result;
	int known;

	/* Each place, until the table is full. */
	look = &app->look;
	look->volume_count = 0;
	for (place = 0; place < sizeof(look_places) / sizeof(look_places[0]); place++) {
		if (look->volume_count == SE_VOLUMES)
			break;

		/* A place that is not there, or has no size, is passed over. */
		result = statvfs(look_places[place], &status);
		if (result != 0 || status.f_blocks == 0U)
			continue;

		/* A file system already shown is not shown again. */
		known = 0;
		for (index = 0; index < look->volume_count; index++) {
			if (seen[index] == (uint64_t)status.f_fsid)
				known = 1;
		}

		/* One already shown is passed over. */
		if (known != 0)
			continue;

		/* Its sizes. */
		seen[look->volume_count] = (uint64_t)status.f_fsid;
		volume = &look->volumes[look->volume_count];
		(void)snprintf(volume->path, sizeof(volume->path), "%s", look_places[place]);
		volume->total = (uint64_t)status.f_blocks * (uint64_t)status.f_frsize;
		volume->available = (uint64_t)status.f_bavail * (uint64_t)status.f_frsize;
		volume->used = volume->total - (uint64_t)status.f_bfree * (uint64_t)status.f_frsize;
		look->volume_count++;
	}
}

/*
 * Names the picture chosen for Home's tile: the default's name, or the
 * chosen file's name.
 */
const char *
se_look_wallpaper_name(
	const struct se_app *app)
{
	const char *slash;
	unsigned index;
	int differs;

	/* No key: the default. */
	if (app->look.wallpaper[0] == '\0')
		return "Kei (default)";

	/* A picture the page found has its name. */
	for (index = 0; index < app->look.wallpaper_count; index++) {
		differs = strcmp(app->look.wallpapers[index].path, app->look.wallpaper);
		if (differs == 0)
			return app->look.wallpapers[index].name;
	}

	/* Else the file's name. */
	slash = strrchr(app->look.wallpaper, '/');
	if (slash == NULL)
		return app->look.wallpaper;

	/* Succeeded: the part after the last slash. */
	return slash + 1;
}

/* Reads the look's keys from the preferences. */
static void
look_read(
	struct se_app *app)
{
	struct se_look *look;
	int error;

	/* The opacity, 100 when unset. */
	look = &app->look;
	look->opacity = keiland_preferences_get_int(look->preferences, "window.opacity", LOOK_OPACITY_MAX, LOOK_OPACITY_MIN, LOOK_OPACITY_MAX);

	/* The pointer and the keyboards. */
	look->pointer_speed = keiland_preferences_get_int(look->preferences, "pointer.speed", LOOK_SPEED_DEFAULT, LOOK_SPEED_MIN, LOOK_SPEED_MAX);
	look->pointer_natural = keiland_preferences_get_int(look->preferences, "pointer.natural", 0, 0, 1);
	look->repeat_rate = keiland_preferences_get_int(look->preferences, "keyboard.repeat.rate", LOOK_RATE_DEFAULT, LOOK_RATE_MIN, LOOK_RATE_MAX);
	look->repeat_delay = keiland_preferences_get_int(look->preferences, "keyboard.repeat.delay", LOOK_DELAY_DEFAULT, LOOK_DELAY_MIN, LOOK_DELAY_MAX);

	/* The picture; none, or one that is not an absolute path, is the default. */
	error = keiland_preferences_get(look->preferences, "wallpaper", look->wallpaper, sizeof(look->wallpaper));
	if (error != 0 || look->wallpaper[0] != '/')
		look->wallpaper[0] = '\0';
}

/* Writes a key (or removes it when value is NULL); a failure is shown on the page. Returns 0 or an errno value. */
static int
look_write(
	struct se_app *app,
	const char *key,
	const char *value)
{
	struct se_look *look;
	int error;

	/* Without a home nothing is saved. */
	look = &app->look;
	if (look->preferences == NULL) {
		(void)snprintf(look->message, sizeof(look->message), "%s", "Settings cannot be saved: this account has no home folder.");
		look->message_bad = 1;
		return ENOENT;
	}

	/* The key, or its removal. */
	if (value != NULL) {
		error = keiland_preferences_set(look->preferences, key, value);
	} else {
		error = keiland_preferences_unset(look->preferences, key);
	}

	/* A failure is shown; a success clears the last message. */
	look->message[0] = '\0';
	look->message_bad = 0;
	if (error != 0) {
		(void)snprintf(look->message, sizeof(look->message), "The setting could not be saved (error %d).", error);
		look->message_bad = 1;
		return error;
	}

	/* Succeeded: zdesktop takes it within a second. */
	app->dirty = 1;
	return 0;
}

/* Adds a picture to the page's list with its small copy (a picture that cannot be read is listed without one). */
static void
look_add_picture(
	struct se_app *app,
	const char *path,
	const char *name)
{
	struct se_wallpaper *wallpaper;
	int error;

	/* A full list keeps the pictures it has. */
	if (app->look.wallpaper_count == SE_WALLPAPERS)
		return;

	/* The picture's names. */
	wallpaper = &app->look.wallpapers[app->look.wallpaper_count];
	memset(wallpaper, 0, sizeof(*wallpaper));
	(void)snprintf(wallpaper->path, sizeof(wallpaper->path), "%s", path);
	(void)snprintf(wallpaper->name, sizeof(wallpaper->name), "%s", name);

	/* Its small copy. */
	error = look_thumbnail(path, &wallpaper->thumbnail);
	if (error == 0)
		wallpaper->read = 1;
	se_log("LOOK picture path=%s error=%d", path, error);
	app->look.wallpaper_count++;
}

/* Reads a binary PPM into a small copy (LOOK_THUMB_WIDTH by LOOK_THUMB_HEIGHT, averaged); returns 0 or an errno value. */
static int
look_thumbnail(
	const char *path,
	struct fm_image *image)
{
	unsigned char *data;
	const unsigned char *pixel;
	size_t size;
	size_t at;
	unsigned width;
	unsigned height;
	unsigned maximum;
	unsigned x;
	unsigned y;
	unsigned source_x;
	unsigned source_y;
	unsigned sums[3];
	unsigned samples;
	unsigned dx;
	unsigned dy;
	unsigned channel;
	unsigned crop_width;
	unsigned crop_height;
	unsigned crop_left;
	unsigned crop_top;
	int error;

	/* The file. */
	data = look_file(path, &size);
	if (data == NULL)
		return errno;

	/* The header: P6, the width, the height and the largest value (255), one space before the pixels. */
	at = 2;
	error = EINVAL;
	if (size > 2U &&
	    data[0] == 'P' &&
	    data[1] == '6')
		error = 0;
	if (error == 0)
		error = look_ppm_number(data, size, &at, &width);
	if (error == 0)
		error = look_ppm_number(data, size, &at, &height);
	if (error == 0)
		error = look_ppm_number(data, size, &at, &maximum);
	if (error == 0 &&
	    (maximum != 255U ||
	     width == 0U ||
	     height == 0U))
		error = EINVAL;
	at++;
	if (error == 0 &&
	    (size < at ||
	     (size - at) / 3U / width < height))
		error = EINVAL;
	if (error != 0) {
		free(data);
		return error;
	}

	/*
	 * The middle of the picture in the tile's proportions (16:10): a wider
	 * picture loses its sides, a taller one its top and bottom.
	 */
	crop_width = width;
	crop_height = height;
	if ((uint64_t)width * LOOK_THUMB_HEIGHT > (uint64_t)height * LOOK_THUMB_WIDTH) {
		crop_width = (unsigned)((uint64_t)height * LOOK_THUMB_WIDTH / LOOK_THUMB_HEIGHT);
	} else {
		crop_height = (unsigned)((uint64_t)width * LOOK_THUMB_HEIGHT / LOOK_THUMB_WIDTH);
	}

	/* The crop's corner, in the middle. */
	crop_left = (width - crop_width) / 2U;
	crop_top = (height - crop_height) / 2U;

	/* The small image. */
	error = fm_image_create(image, LOOK_THUMB_WIDTH, LOOK_THUMB_HEIGHT);
	if (error != 0) {
		free(data);
		return error;
	}

	/* Each small pixel averages a few of the picture's (a grid of 3 by 3 samples of its area). */
	for (y = 0; y < (unsigned)LOOK_THUMB_HEIGHT; y++) {
		for (x = 0; x < (unsigned)LOOK_THUMB_WIDTH; x++) {
			sums[0] = 0;
			sums[1] = 0;
			sums[2] = 0;
			samples = 0;
			for (dy = 0; dy < 3U; dy++) {
				for (dx = 0; dx < 3U; dx++) {
					source_x = crop_left + (unsigned)(((uint64_t)x * 3U + dx) * crop_width / (LOOK_THUMB_WIDTH * 3U));
					source_y = crop_top + (unsigned)(((uint64_t)y * 3U + dy) * crop_height / (LOOK_THUMB_HEIGHT * 3U));
					pixel = data + at + ((size_t)source_y * width + source_x) * 3U;
					for (channel = 0; channel < 3U; channel++)
						sums[channel] += pixel[channel];
					samples++;
				}
			}

			/* Opaque, in the canvas's order (0xAARRGGBB). */
			image->pixels[(size_t)y * image->stride + x] = 0xff000000U |
			    ((sums[0] / samples) << 16) |
			    ((sums[1] / samples) << 8) |
			    (sums[2] / samples);
		}
	}

	/* Succeeded: the small copy is made. */
	free(data);
	return 0;
}

/* Reads a PPM header's number after white space and comments; returns 0 or EINVAL. */
static int
look_ppm_number(
	const unsigned char *data,
	size_t size,
	size_t *at,
	unsigned *number)
{
	unsigned digits;

	/* White space and comments first. */
	while (*at < size) {
		if (data[*at] == '#') {
			while (*at < size && data[*at] != '\n')
				(*at)++;
		} else if (data[*at] == ' ' ||
			   data[*at] == '\t' ||
			   data[*at] == '\n' ||
			   data[*at] == '\r') {
			(*at)++;
		} else {
			break;
		}
	}

	/* The digits, at most seven. */
	*number = 0;
	digits = 0;
	while (*at < size &&
	       data[*at] >= '0' &&
	       data[*at] <= '9' &&
	       digits < 7U) {
		*number = *number * 10U + (unsigned)(data[*at] - '0');
		(*at)++;
		digits++;
	}

	/* No digit is no number. */
	if (digits == 0U)
		return EINVAL;

	/* Succeeded: the number is read. */
	return 0;
}

/* Reads a whole file (at most LOOK_PICTURE_MAX bytes); NULL with errno set when it cannot be read. */
static unsigned char *
look_file(
	const char *path,
	size_t *size)
{
	unsigned char *data;
	FILE *file;
	long length;
	size_t count;
	int status;

	/* The file. */
	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;

	/* Its length, within the limit. */
	status = fseek(file, 0L, SEEK_END);
	length = ftell(file);
	if (status != 0 ||
	    length <= 0 ||
	    (unsigned long)length > LOOK_PICTURE_MAX) {
		(void)fclose(file);
		errno = EFBIG;
		return NULL;
	}

	/* Back to the start for the reading. */
	rewind(file);

	/* Every byte. */
	data = malloc((size_t)length);
	if (data == NULL) {
		(void)fclose(file);
		errno = ENOMEM;
		return NULL;
	}

	/* The bytes, and the file closed. */
	count = fread(data, 1U, (size_t)length, file);
	(void)fclose(file);
	if (count != (size_t)length) {
		free(data);
		errno = EIO;
		return NULL;
	}

	/* Succeeded: the file's bytes. */
	*size = count;
	return data;
}

/* Orders two names as strcmp does (for qsort). */
static int
look_compare_names(
	const void *left,
	const void *right)
{
	int order;

	/* The names' order. */
	order = strcmp((const char *)left, (const char *)right);

	/* Succeeded: the order. */
	return order;
}
