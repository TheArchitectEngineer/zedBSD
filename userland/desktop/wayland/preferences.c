/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The user's preferences on the desktop (ws089-p007, plan/ws089/proposed/
 * desktop-preferences.md): the wallpaper, the windows' opacity, the
 * pointer's speed and the wheel's direction, and the keyboards' repeat,
 * as Settings writes them in ~/.config/keiland/desktop.conf through
 * libkeiland.
 *
 * A desktop that is not the login screen reads them before its look draws
 * the wallpaper, then looks at the file once a second and applies the keys
 * that changed.  The command line's --wallpaper and --window-opacity stay
 * the defaults a removed key returns to.  Each key applied is logged
 * ("ZWL PREFERENCES key=... applied"), which the tests read.
 */

#include "zwl.h"

#include <keiland.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* How often the file is looked at, in milliseconds. */
#define PREFERENCES_CHECK_MS		1000U

/* The ranges of the numeric keys, and their defaults. */
#define PREFERENCES_OPACITY_MIN		85
#define PREFERENCES_OPACITY_MAX		100
#define PREFERENCES_SPEED_MIN		25
#define PREFERENCES_SPEED_MAX		300
#define PREFERENCES_SPEED_DEFAULT	100
#define PREFERENCES_RATE_MIN		5
#define PREFERENCES_RATE_MAX		60
#define PREFERENCES_RATE_DEFAULT	25
#define PREFERENCES_DELAY_MIN		150
#define PREFERENCES_DELAY_MAX		1000
#define PREFERENCES_DELAY_DEFAULT	400

static void preferences_apply(struct zwl_server *server, int starting);
static void preferences_wallpaper(struct zwl_server *server, int starting);
static void preferences_opacity(struct zwl_server *server);
static void preferences_number(const char *key, int32_t *target, int32_t value);

/*
 * Opens the user's preferences and applies them before the look is made.
 * Without a home the desktop keeps its command line's settings.
 */
void
zwl_preferences_open(
	struct zwl_server *server)
{
	/* What the command line gave is what a removed key returns to. */
	server->window_opacity_started = server->window_opacity;
	server->wallpaper_started = server->wallpaper_path;
	server->wallpaper_chosen[0] = '\0';

	/* The file; without a home there are no preferences. */
	server->preferences = keiland_preferences_open();
	if (server->preferences == NULL) {
		printf("ZWL PREFERENCES none errno=%d\n", errno);
		return;
	}

	/* Every key, before anything is drawn. */
	server->preferences_checked_ms = zwl_milliseconds();
	preferences_apply(server, 1);
	printf("ZWL PREFERENCES open\n");
}

/*
 * Looks at the preferences once a second and applies the keys that
 * changed.
 */
void
zwl_preferences_tick(
	struct zwl_server *server,
	uint64_t now)
{
	int changed;
	int error;

	/* Nothing to look at, or not yet. */
	if (server->preferences == NULL)
		return;
	if (now - server->preferences_checked_ms < PREFERENCES_CHECK_MS)
		return;
	server->preferences_checked_ms = now;

	/* The file, when it moved. */
	error = keiland_preferences_reload(server->preferences, &changed);
	if (error != 0) {
		printf("ZWL PREFERENCES reload-failed errno=%d\n", error);
		return;
	}

	/* The keys that changed. */
	if (changed != 0)
		preferences_apply(server, 0);
}

/*
 * Closes the preferences (at the end of the run).
 */
void
zwl_preferences_close(
	struct zwl_server *server)
{
	/* Nothing was opened. */
	if (server->preferences == NULL)
		return;

	/* The file is not looked at any more. */
	keiland_preferences_close(server->preferences);
	server->preferences = NULL;
}

/* Applies every key whose value differs from what the desktop uses (starting: before the look is made). */
static void
preferences_apply(
	struct zwl_server *server,
	int starting)
{
	struct keiland_preferences *preferences;
	int32_t value;

	/* The wallpaper and the windows' opacity. */
	preferences = server->preferences;
	preferences_wallpaper(server, starting);
	preferences_opacity(server);

	/* The pointer's speed and the wheel's direction. */
	value = keiland_preferences_get_int(preferences, "pointer.speed", PREFERENCES_SPEED_DEFAULT, PREFERENCES_SPEED_MIN, PREFERENCES_SPEED_MAX);
	preferences_number("pointer.speed", &server->pointer_speed, value);
	value = keiland_preferences_get_int(preferences, "pointer.natural", 0, 0, 1);
	preferences_number("pointer.natural", &server->pointer_natural, value);

	/* The keyboards' repeat, for keyboards bound from now on. */
	value = keiland_preferences_get_int(preferences, "keyboard.repeat.rate", PREFERENCES_RATE_DEFAULT, PREFERENCES_RATE_MIN, PREFERENCES_RATE_MAX);
	preferences_number("keyboard.repeat.rate", &server->repeat_rate, value);
	value = keiland_preferences_get_int(preferences, "keyboard.repeat.delay", PREFERENCES_DELAY_DEFAULT, PREFERENCES_DELAY_MIN, PREFERENCES_DELAY_MAX);
	preferences_number("keyboard.repeat.delay", &server->repeat_delay_ms, value);

	/* The sound's volume, once audiod has been reached (volume.c, ws100-p004). */
	zwl_volume_preferences(server);
}

/* Shows the wallpaper the preferences choose (an absolute path), or the command line's when they choose none. */
static void
preferences_wallpaper(
	struct zwl_server *server,
	int starting)
{
	char chosen[KEILAND_PREFERENCES_VALUE_MAX];
	const char *path;
	int differs;
	int error;

	/* The key's value; a key not set, or not an absolute path, chooses the command line's. */
	error = keiland_preferences_get(server->preferences, "wallpaper", chosen, sizeof(chosen));
	if (error != 0 || chosen[0] != '/')
		chosen[0] = '\0';

	/* The same choice as shown changes nothing. */
	differs = strcmp(chosen, server->wallpaper_chosen);
	if (differs == 0)
		return;

	/* The choice, and the picture it means. */
	(void)snprintf(server->wallpaper_chosen, sizeof(server->wallpaper_chosen), "%s", chosen);
	path = server->wallpaper_started;
	if (server->wallpaper_chosen[0] != '\0')
		path = server->wallpaper_chosen;
	server->wallpaper_path = path;

	/* Before the look is made, it draws the picture itself. */
	if (starting != 0) {
		printf("ZWL PREFERENCES key=wallpaper applied\n");
		return;
	}

	/* Afterwards the look draws it now. */
	error = zwl_glass_wallpaper(server, path);
	if (error != 0) {
		printf("ZWL PREFERENCES key=wallpaper failed errno=%d\n", error);
		return;
	}

	/* The new picture is shown. */
	printf("ZWL PREFERENCES key=wallpaper applied\n");
}

/* Sets the windows' opacity the preferences choose (85 to 100 percent), or the command line's. */
static void
preferences_opacity(
	struct zwl_server *server)
{
	float opacity;
	int percent;
	int started;

	/* The command line's opacity in whole percent is the default. */
	started = (int)(server->window_opacity_started * 100.0f + 0.5f);
	percent = keiland_preferences_get_int(server->preferences, "window.opacity", started, PREFERENCES_OPACITY_MIN, PREFERENCES_OPACITY_MAX);

	/* A key not set keeps the command line's exactly (it may lie outside the range). */
	opacity = (float)percent / 100.0f;
	if (percent == started)
		opacity = server->window_opacity_started;

	/* The same opacity changes nothing. */
	if (opacity == server->window_opacity)
		return;

	/* Every window is drawn again at the new opacity. */
	server->window_opacity = opacity;
	server->dirty = 1;
	printf("ZWL PREFERENCES key=window.opacity applied value=%d\n", percent);
}

/* Sets a number the desktop uses when the preferences' value differs, and logs it. */
static void
preferences_number(
	const char *key,
	int32_t *target,
	int32_t value)
{
	/* The same value changes nothing. */
	if (*target == value)
		return;

	/* The new value, from the next input on. */
	*target = value;
	printf("ZWL PREFERENCES key=%s applied value=%d\n", key, (int)value);
}
