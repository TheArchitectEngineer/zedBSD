/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * App Home (ws035-p069, plan/ws035/app-home-design.md): the applications'
 * home screen, under the desktop.
 *
 * Opening it does not darken the desktop: the desktop layer (the wallpaper
 * and the windows) slides towards the bottom right and shrinks a little,
 * until only its top-left corner and its shadow are left in the output's
 * bottom-right corner, and the bright home screen under it shows: the
 * blurred wallpaper, whitened, with a grid of application icons.  The
 * launcher in the system bar, or a drag from the top-left corner towards
 * the bottom right, opens it; the drag follows the pointer.  The launcher
 * again, the corner of the desktop that is left, Esc, or starting an
 * application closes it the opposite way.
 *
 * Typing while it is open searches: the text shows at the top and only the
 * applications whose name, command or keywords contain it stay, centred.
 * The search takes an input method's text too (ws090-p022): while Home is
 * open the input method serves it as zdesktop's own field (input-method.c,
 * kwl_home_field_state and kwl_home_field_input), its composed text shown
 * underlined after the search.
 * Enter starts the selected (at first the first) one; the arrow keys and
 * Tab move the selection; Backspace and Esc clear the search.
 *
 * Without a search the icons are in pages of six columns and four rows
 * (ws035-p071): a sideways drag on Home follows the pointer and snaps to a
 * page, the wheel and PageUp/PageDown turn pages, the dots at the bottom
 * show where one is.  A drag towards the top left closes Home, and so does a
 * swipe up from the bottom edge (which opens Wiseview only on the desktop,
 * never over Home, ws079-p010).  A started
 * application's icon grows as Home closes, and its first window grows out
 * of the icon's place (shell.c).
 *
 * The applications come from /etc/keiland/apps.conf, one a line:
 * name|command|keywords|RRGGBB|picture; without the file, a built-in list.
 * The picture is one of the names icons.c draws a picture for ("files",
 * "notes", "terminal", ...; ws035-p123); an application with a picture
 * shows its tile in the picture's own colours with the picture cut out
 * (icons.c, ws128-p012), one without shows its name's first letter on a
 * tile in its colour.  An application whose command names
 * an absolute path that is not there is not shown.  An application is
 * started with /bin/sh -c and the compositor's socket in its environment.
 */

#include "glass.h"
#include "activation.h"
#include "ime.h"

#include "userland/desktop/paths.h"

#include "userland/desktop/libkeiland-backend/keiland-backend.h"

#include <keiland/keiland.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* A page of icons: its columns and rows, how long a page turn takes, and how far a press moves before it is a drag. */
#define HOME_ROWS		4
#define HOME_PAGE		(HOME_COLUMNS * HOME_ROWS)
#define HOME_PAGE_MS		200U
#define HOME_PAGE_START		10

/* How long a launch waits for its window, and how far towards the top left a drag closes Home. */
#define HOME_LAUNCH_WAIT_MS	5000U

/*
 * How long a launch is remembered at all: a window that comes later than
 * HOME_LAUNCH_WAIT_MS does not grow out of the icon, but is still named as
 * the launch's (KWL GLASS launch-late), so that a slow start can be told
 * from a launch that never came (ws099-p024, BUG-147).
 */
#define HOME_LAUNCH_FORGET_MS	30000U
#define HOME_CLOSE_DRAG		120

/* The keys that turn pages and move to the next icon. */
#define HOME_KEY_TAB		15U
#define HOME_KEY_PAGE_UP	104U
#define HOME_KEY_PAGE_DOWN	109U

/* The applications' list, and how many it may hold. */
#define HOME_APPS_PATH		KEILAND_SYSCONFDIR "/keiland/apps.conf"

/* The command of a login session's Power Off, whose dialog the compositor shows itself (ws099-p037). */
#define HOME_POWER		"@power"
#define HOME_LOCK		"@lock"

/* The page the built-in list's browser opens (shown only when the page is there). */
#define HOME_BROWSER_START	KEILAND_DATADIR "/browser/start.html"
#define HOME_APPS_MAX		48U

/* How long opening and closing take, and the least move of the corner drag. */
#define HOME_OPEN_MS		280U
#define HOME_CLOSE_MS		240U
#define HOME_DRAG_START		14
#define HOME_DRAG_DISTANCE	360.0f
#define HOME_THRESHOLD		0.30f

/* The corner the gesture starts in, and how much of the desktop stays in view when Home is open. */
#define HOME_CORNER		28
#define HOME_KEEP		26.0f
#define HOME_KEEP_NEAR		40.0f
#define HOME_NEAR		120

/*
 * The swipe up from the bottom edge that closes Home (the 2026-09-28
 * decision at the end of plan/ws079/design-input-notes.md): where it starts
 * (the same strip that opens Wiseview on the desktop, shell.c), how far it
 * moves before it is one, how far up Home is closed by it, and the part of
 * that past which letting go closes Home.
 */
#define HOME_BOTTOM_EDGE	20
#define HOME_BOTTOM_START	12
#define HOME_BOTTOM_DISTANCE	240.0f
#define HOME_BOTTOM_THRESHOLD	0.35f

/*
 * The grid: columns, a cell's size, the icon's size and corner, the label's
 * baseline under the icon (under the floor and the reflection, ws099-p035b).
 */
#define HOME_COLUMNS		6
#define HOME_CELL_WIDTH		144
#define HOME_CELL_HEIGHT	152
#define HOME_ICON		72
#define HOME_ICON_RADIUS	18.0f
#define HOME_LABEL		49

/*
 * The stage (ws099-p035b, BUG-236, the user's choice A of the p035a
 * montage, dark in both appearances): the black glass over the blurred
 * desktop and the light in the top's middle; a row's glossy floor line
 * HOME_FLOOR_GAP under its tiles and how bright it is; each tile's
 * spotlight on the floor (brighter under the pointer); its reflection's
 * opacity at the floor and its height (of the tile's); the labels' white.
 */
#define HOME_STAGE_DARK		0.82f
#define HOME_STAGE_LIGHT	0.10f
#define HOME_FLOOR_GAP		6
#define HOME_FLOOR		0.10f
#define HOME_FLOOR_BANDS	24
#define HOME_SPOT		0.08f
#define HOME_SPOT_LIT		0.16f
#define HOME_REFLECTION		0.15f
#define HOME_REFLECTION_HEIGHT	0.35f
#define HOME_NAME_ALPHA		0.90f

/*
 * The content's own animation (ws099-p035c, BUG-225: the stage at once, the
 * icons after it): each icon rises HOME_CONTENT_RISE pixels and fades in
 * over HOME_CONTENT_MS, HOME_CONTENT_STEP_MS after the one before it.
 */
#define HOME_CONTENT_MS		180U
#define HOME_CONTENT_STEP_MS	30U
#define HOME_CONTENT_RISE	12.0f

/* How much a tile is whitened under the pointer. */
#define HOME_LIT		0.15f

/*
 * The tile's shading (ws035-p123): the tile is drawn in this many bands,
 * whitened towards its top by up to HOME_SHADE_LIGHT and darkened towards
 * its bottom by up to HOME_SHADE_DARK, so its colour runs smoothly as on
 * lit glass; its rim is white at HOME_RIM_ALPHA.
 */
#define HOME_SHADE_BANDS	24
#define HOME_SHADE_LIGHT	0.22f
#define HOME_SHADE_DARK		0.10f
#define HOME_RIM_ALPHA		0.22f

/* The evdev codes of the keys Home takes. */
#define HOME_KEY_ESC		1U
#define HOME_KEY_BACKSPACE	14U
#define HOME_KEY_ENTER		28U
#define HOME_KEY_KPENTER	96U
#define HOME_KEY_LEFT		105U
#define HOME_KEY_RIGHT		106U
#define HOME_KEY_UP		103U
#define HOME_KEY_DOWN		108U

/* How many evdev codes the character table covers (up to the space bar). */
#define HOME_KEYS		58U

/*
 * One application Home can start.
 */
struct home_app {
	/* The name under its icon, the command that starts it, and more words search finds it by. */
	char name[40];
	char command[160];
	char keywords[80];

	/* The colour of the letter's tile, and the picture (GLASS_ICON_APP_*, whose tile has colours of its own, or -1 for the name's first letter). */
	float color[4];
	int picture;
};

/*
 * The applications, read once when Home first opens.  home_app_count is 0
 * until then; the list is not read again while the compositor runs.
 */
static struct home_app home_apps[HOME_APPS_MAX];
static unsigned home_app_count;
static unsigned home_apps_read;

/*
 * Whether Home's opening was prepared ahead (home_prepare, ws099-p035c):
 * set once, after the list was read and the first frame drawn, for the
 * compositor's life.
 */
static unsigned home_prepared;

/*
 * The applications shown now (all, or those the search finds), as indexes
 * into home_apps, and where each one's icon is.  Laid out again on every
 * frame and every change of the search.
 */
static unsigned home_shown[HOME_APPS_MAX];
static unsigned home_shown_count;
static int32_t home_icon_x[HOME_APPS_MAX];
static int32_t home_icon_y[HOME_APPS_MAX];

/* How many pages the icons take (1 while searching). */
static unsigned home_pages = 1U;

/*
 * The character each key types into the search (lower case), by evdev code; 0 for none.
 */
static const char home_characters[HOME_KEYS] = {
	0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', 0, 0, 0,
	'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', 0, 0, 0, 0,
	'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', 0, 0, 0, 0, 0,
	'z', 'x', 'c', 'v', 'b', 'n', 'm', 0, '.', '/', 0, 0, 0, ' '
};

static void home_read_apps(struct kwl_server *server);
static int home_present(const char *name, const char *command);
static void home_add_app(const char *name, const char *command, const char *keywords, uint32_t rgb, const char *picture);
static void home_draw_letter(struct kwl_server *server, VkCommandBuffer command, const struct home_app *app, int32_t x, int32_t y, float left, float top, float size, int over, float opacity);
static void home_draw_tile(struct kwl_server *server, VkCommandBuffer command, float left, float top, float size, const float *color);
static void home_parse_line(char *line);
static uint32_t home_hex(const char *text);
static void home_layout(struct kwl_server *server);
static int home_matches(const struct home_app *app, const char *query);
static int home_contains(const char *text, const char *query);
static int home_icon_at(struct kwl_server *server, int32_t x, int32_t y);
static void home_draw_icon(struct kwl_server *server, VkCommandBuffer command, unsigned slot, float opacity, float rise);
static float home_content(struct kwl_server *server, unsigned order, float progress, float *rise);
static void home_prepare(struct kwl_server *server);
static void home_draw_floors(struct kwl_server *server, VkCommandBuffer command, float opacity);
static void home_draw_floor(struct kwl_server *server, VkCommandBuffer command, float left, float right, float y, float opacity);
static void home_draw_spotlight(struct kwl_server *server, VkCommandBuffer command, float middle, float floor, float strength);
static void home_draw_search(struct kwl_server *server, VkCommandBuffer command, float opacity);
static void home_open(struct kwl_server *server, float from, const char *via);
static void home_close(struct kwl_server *server, float from, const char *via);
static void home_settle(struct kwl_server *server, float from, float to);
static void home_erase(struct kwl_server *server, uint32_t characters);
static void home_erase_bytes(struct kwl_server *server, uint32_t bytes);
static int home_launch(struct kwl_server *server, unsigned app);
static struct kwl_object *home_running_window(struct kwl_server *server, unsigned app);
static void home_search_changed(struct kwl_server *server);
static void home_log_icons(void);
static float home_ease(float t);
static float home_page_position(struct kwl_server *server);
static void home_page_turn(struct kwl_server *server, int target, const char *via);
static void home_page_release(struct kwl_server *server, float progress);
static void home_draw_dots(struct kwl_server *server, VkCommandBuffer command, float opacity);
static void home_select(struct kwl_server *server, int selected);
static void home_bottom_release(struct kwl_server *server);

/*
 * Returns how far Home is open now: 0 closed, 1 open, between while the
 * drag or the animation moves it.
 */
float
kwl_home_progress(
	struct kwl_server *server)
{
	uint64_t elapsed;
	uint64_t length;
	float t;

	/* The drag, once it is one, is followed as it is. */
	if (server->home_dragging)
		return server->home_drag;

	/* Settled: where it went. */
	if (!server->home_moving)
		return server->home;

	/* Moving: eased from where it started to where it goes, over the open or the close time. */
	length = HOME_CLOSE_MS;
	if (server->home_to > server->home_from)
		length = HOME_OPEN_MS;
	elapsed = kwl_milliseconds() - server->home_start_ms;
	t = (float)elapsed / (float)length;
	if (t > 1.0f)
		t = 1.0f;

	/* Reports the eased position. */
	return server->home_from + (server->home_to - server->home_from) * home_ease(t);
}

/*
 * Works out where the desktop layer is when Home is open by progress: slid
 * towards the bottom right and a little smaller, leaving HOME_KEEP pixels
 * (more when the pointer is near) of its corner in view.
 */
void
kwl_home_layer(
	struct kwl_server *server,
	float progress,
	float *x,
	float *y,
	float *scale)
{
	float keep;
	int32_t near_x;
	int32_t near_y;

	/* The corner left in view widens when the pointer comes near it, to show it can be taken. */
	keep = HOME_KEEP;
	near_x = (int32_t)server->width - server->pointer_x;
	near_y = (int32_t)server->height - server->pointer_y;
	if (progress >= 1.0f && near_x < HOME_NEAR && near_y < HOME_NEAR)
		keep = HOME_KEEP_NEAR;

	/* Sliding is the main motion; the shrinking only helps it. */
	*x = progress * ((float)server->width - keep);
	*y = progress * ((float)server->height - keep);
	*scale = 1.0f - 0.03f * progress;
}

/*
 * Draws Home, faded in by progress: the whitened blurred wallpaper and the
 * icons, and the search text while there is one.
 */
void
kwl_home_draw(
	struct kwl_server *server,
	VkCommandBuffer command,
	float progress)
{
	struct glass_shape shape;
	float content;
	float width;
	float rise;
	unsigned order;
	unsigned slot;

	/* The applications and where they go. */
	home_read_apps(server);
	home_layout(server);

	/* The stage keeps its own colours in both appearances (it is always dark, ws099-p035b). */
	server->keep_colours = 1U;

	/*
	 * The dark stage, whole from the start (only the icons fade in): the
	 * blurred desktop under black glass.
	 */
	width = (float)server->width;
	glass_shape_init(&shape, 0.0f, 0.0f, width, (float)server->height);
	shape.mode = MODE_GLASS;
	shape.light = 1U;
	shape.color[3] = HOME_STAGE_DARK;
	glass_shape_draw(server, command, &shape);

	/* A soft bluish light from the top's middle, as the bar's middle is lighter. */
	glass_shape_init(&shape, width * 0.3f, -(float)server->height * 0.3f, width * 0.4f, (float)server->height * 0.6f);
	shape.quad[0] = 0.0f;
	shape.quad[1] = 0.0f;
	shape.quad[2] = width;
	shape.quad[3] = (float)server->height;
	shape.mode = MODE_SHADOW;
	shape.radius = (float)server->height * 0.3f;
	shape.soft = width * 0.3f;
	shape.color[0] = 0.80f;
	shape.color[1] = 0.86f;
	shape.color[2] = 1.0f;
	shape.color[3] = HOME_STAGE_LIGHT;
	glass_shape_draw(server, command, &shape);

	/* The stage's first frame since Home was asked to open (BUG-225's wait, measured). */
	if (!server->home_cover_logged && server->home_to > 0.0f) {
		server->home_cover_logged = 1U;
		printf("KWL HOME layer=cover after_ms=%llu\n", (unsigned long long)(kwl_milliseconds() - server->home_asked_ms));
	}

	/* The rows' floors, under the icons, coming in with the first of them. */
	content = home_content(server, 0U, progress, &rise);
	home_draw_floors(server, command, content);

	/* Each icon shown on the output, coming in after the one before it (fading out with Home as it closes). */
	order = 0U;
	for (slot = 0U; slot < home_shown_count; slot++) {
		if (home_icon_x[slot] + HOME_CELL_WIDTH < 0 || home_icon_x[slot] - HOME_CELL_WIDTH > (int32_t)server->width)
			continue;
		content = home_content(server, order, progress, &rise);
		home_draw_icon(server, command, slot, content, rise);
		order++;
	}

	/* The content's first frame (its first icon begun). */
	content = home_content(server, 0U, progress, &rise);
	if (!server->home_content_logged && server->home_to > 0.0f && content > 0.0f) {
		server->home_content_logged = 1U;
		printf("KWL HOME layer=content after_ms=%llu\n", (unsigned long long)(kwl_milliseconds() - server->home_asked_ms));
	}

	/* The search text, while something has been typed or is being composed; the pages' dots, when there are pages. */
	if (server->home_query_length != 0U || server->home_preedit[0] != '\0')
		home_draw_search(server, command, progress);
	if (home_pages > 1U)
		home_draw_dots(server, command, progress);

	/* The rest of the frame is drawn in the appearance's colours again. */
	server->keep_colours = 0U;
}

/*
 * Handles a pointer button for Home.  While Home is open (or opening) it
 * takes every button: the launcher and the desktop's corner close it, an
 * icon starts its application.  While it is closed, a press on the launcher
 * or in the top-left corner may open it: a click opens it, a drag towards
 * the bottom right follows the pointer.  Returns 1 when the button is Home's.
 */
int
kwl_home_button(
	struct kwl_server *server,
	uint32_t button,
	uint32_t state)
{
	float progress;
	int app;
	int32_t x;
	int32_t y;

	/* Only the left button acts. */
	x = server->pointer_x;
	y = server->pointer_y;
	progress = kwl_home_progress(server);

	/* The end of a press that may have been the gesture. */
	if (state == 0 && server->home_press) {
		server->home_press = 0;

		/* A click on the launcher or the corner toggles Home. */
		if (!server->home_dragging) {
			if (progress > 0.0f && server->home_to > 0.0f) {
				home_close(server, progress, "launcher");
			} else {
				home_open(server, progress, "launcher");
			}

			/* The click was Home's. */
			return 1;
		}

		/* A drag opens Home past the threshold, and goes back otherwise. */
		server->home_dragging = 0;
		if (server->home_drag >= HOME_THRESHOLD) {
			home_open(server, server->home_drag, "drag");
		} else {
			home_close(server, server->home_drag, "drag");
		}

		/* The drag was Home's. */
		return 1;
	}

	/* The end of a swipe up from the bottom edge: far enough up closes Home, otherwise it opens again. */
	if (state == 0 && server->home_bottom_press) {
		home_bottom_release(server);
		return 1;
	}

	/* The end of a press on Home: a page drag snaps, a drag to the top left closes, a click on an icon starts it. */
	if (state == 0 && server->home_page_press) {
		home_page_release(server, progress);
		return 1;
	}

	/* A release Home did not start belongs to Home only while Home shows. */
	if (state == 0)
		return progress > 0.0f;

	/* The other buttons do nothing while Home shows, and are the desktop's otherwise. */
	if (button != KWL_BUTTON_LEFT)
		return progress > 0.0f;

	/* A press on the launcher or in the top-left corner: a click or the start of the gesture. */
	if ((x < 40 && y < KWL_GLASS_BAR) || (x < HOME_CORNER && y < HOME_CORNER)) {
		server->home_press = 1;
		server->home_dragging = 0;
		server->home_start_x = x;
		server->home_start_y = y;
		return 1;
	}

	/* With Home closed, the press is the desktop's. */
	if (progress <= 0.0f && server->home_to <= 0.0f)
		return 0;

	/* The corner of the desktop that is left in view takes it back. */
	if (x >= (int32_t)server->width - (int32_t)HOME_KEEP_NEAR && y >= (int32_t)server->height - (int32_t)HOME_KEEP_NEAR) {
		home_close(server, progress, "corner");
		return 1;
	}

	/* A press at the bottom edge may be the swipe up that closes Home (on the desktop the same edge opens Wiseview). */
	if (y >= (int32_t)server->height - HOME_BOTTOM_EDGE) {
		server->home_bottom_press = 1;
		server->home_bottom_dragging = 0;
		server->home_bottom_start_y = y;
		server->home_bottom_from = progress;
		return 1;
	}

	/* Elsewhere on Home a press may be a click on an icon, a page drag, or a drag that closes Home: its release decides. */
	app = home_icon_at(server, x, y);
	server->home_page_press = 1;
	server->home_page_dragging = 0;
	server->home_page_start_x = x;
	server->home_page_start_y = y;
	server->home_page_app = app;
	server->home_page_offset = 0;
	return 1;
}

/*
 * Follows the corner gesture.  Returns 1 when the motion is Home's.
 */
int
kwl_home_motion(
	struct kwl_server *server)
{
	int32_t dx;
	int32_t dy;
	int32_t up;
	float moved;
	float progress;

	/* A swipe up from the bottom edge: past HOME_BOTTOM_START Home follows it, closing. */
	if (server->home_bottom_press) {
		up = server->home_bottom_start_y - server->pointer_y;
		if (!server->home_bottom_dragging) {
			if (up < HOME_BOTTOM_START)
				return 1;

			/* Far enough up: the swipe is one, and Home follows it instead of its animation. */
			server->home_bottom_dragging = 1;
			server->home_dragging = 1;
			server->home_moving = 0;
			printf("KWL HOME bottom swipe\n");
		}

		/* Home closes as far as the pointer has come up, from where it was at the press. */
		moved = server->home_bottom_from * (1.0f - (float)up / HOME_BOTTOM_DISTANCE);
		if (moved < 0.0f)
			moved = 0.0f;
		if (moved > server->home_bottom_from)
			moved = server->home_bottom_from;

		/* The swipe has the motion. */
		server->home_drag = moved;
		server->dirty = 1;
		return 1;
	}

	/* A press on Home: past HOME_PAGE_START it is a drag, whose sideways part moves the pages. */
	if (server->home_page_press) {
		dx = server->pointer_x - server->home_page_start_x;
		dy = server->pointer_y - server->home_page_start_y;
		if (!server->home_page_dragging && dx * dx + dy * dy >= HOME_PAGE_START * HOME_PAGE_START) {
			server->home_page_dragging = 1;
			printf("KWL HOME page drag\n");
		}

		/* The pages follow the pointer, only when there are pages and no search. */
		if (server->home_page_dragging && home_pages > 1U && server->home_query_length == 0U) {
			server->home_page_moving = 0;
			server->home_page_offset = dx;
		}

		/* Drawn again. */
		server->dirty = 1;
		return 1;
	}

	/* Without a press Home may start from, the motion is Home's only while it shows (for the hover). */
	if (!server->home_press) {
		progress = kwl_home_progress(server);
		if (progress > 0.0f) {
			server->dirty = 1;
			return 1;
		}

		/* With Home closed the motion is the desktop's. */
		return 0;
	}

	/* The gesture starts once the pointer has moved right and down by enough. */
	dx = server->pointer_x - server->home_start_x;
	dy = server->pointer_y - server->home_start_y;
	if (!server->home_dragging) {
		if (dx < HOME_DRAG_START || dy < HOME_DRAG_START)
			return 1;
		server->home_dragging = 1;
		server->home_moving = 0;
		printf("KWL HOME gesture\n");
	}

	/* How far along the diagonal the pointer is, from the start. */
	moved = ((float)dx + (float)dy) * 0.5f / HOME_DRAG_DISTANCE;
	if (moved < 0.0f)
		moved = 0.0f;
	if (moved > 1.0f)
		moved = 1.0f;

	/* Succeeded: Home follows the pointer. */
	server->home_drag = moved;
	server->dirty = 1;
	return 1;
}

/*
 * Handles a key while Home shows: typing searches, Enter starts the
 * selected application, the arrows move the selection, Backspace erases,
 * Esc clears the search or closes Home.  Returns 1 when the key is Home's
 * (every key while Home shows), 0 otherwise.
 */
int
kwl_home_key(
	struct kwl_server *server,
	uint32_t key,
	uint32_t state)
{
	float progress;
	char character;
	int closed;

	/* Home takes the keys only while it shows or is opening. */
	progress = kwl_home_progress(server);
	if (progress <= 0.0f && server->home_to <= 0.0f)
		return 0;

	/* Releases do nothing, but are Home's too. */
	if (state == 0U)
		return 1;

	/* Routes the key by its code. */
	switch (key) {
	case HOME_KEY_ESC:
		/* Esc clears the search first, and closes Home when there is none. */
		if (server->home_query_length != 0U) {
			server->home_query_length = 0U;
			server->home_query[0] = '\0';
			home_search_changed(server);
		} else {
			home_close(server, progress, "escape");
		}

		/* The key was Home's. */
		return 1;
	case HOME_KEY_BACKSPACE:
		/* Backspace erases the last character, all its bytes. */
		if (server->home_query_length != 0U) {
			home_erase(server, 1U);
			home_search_changed(server);
		}

		/* The key was Home's. */
		return 1;
	case HOME_KEY_ENTER:
	case HOME_KEY_KPENTER:
		/* Enter starts the selected application and closes Home. */
		home_layout(server);
		if (server->home_selected >= 0 && (unsigned)server->home_selected < home_shown_count) {
			closed = home_launch(server, home_shown[server->home_selected]);
			if (!closed)
				home_close(server, progress, "launch");
		}

		/* The key was Home's. */
		return 1;
	case HOME_KEY_RIGHT:
	case HOME_KEY_DOWN:
		/* The selection moves to the next application (the page follows it). */
		home_select(server, server->home_selected + 1);
		return 1;
	case HOME_KEY_LEFT:
	case HOME_KEY_UP:
		/* And to the one before. */
		home_select(server, server->home_selected - 1);
		return 1;
	case HOME_KEY_TAB:
		/* Tab moves to the next, from the last back to the first. */
		home_layout(server);
		if (server->home_selected + 1 >= (int)home_shown_count) {
			home_select(server, 0);
		} else {
			home_select(server, server->home_selected + 1);
		}

		/* The key was Home's. */
		return 1;
	case HOME_KEY_PAGE_UP:
		/* The page before. */
		home_page_turn(server, (int)server->home_page - 1, "key");
		return 1;
	case HOME_KEY_PAGE_DOWN:
		/* The page after. */
		home_page_turn(server, (int)server->home_page + 1, "key");
		return 1;
	default:
		break;
	}

	/* Any other key that types a character adds it to the search. */
	if (key >= HOME_KEYS)
		return 1;
	character = home_characters[key];
	if (character == 0)
		return 1;

	/* The search holds what fits. */
	if (server->home_query_length + 1U >= sizeof(server->home_query))
		return 1;

	/* Succeeded: the search changes. */
	server->home_query[server->home_query_length] = character;
	server->home_query_length++;
	server->home_query[server->home_query_length] = '\0';
	home_search_changed(server);
	return 1;
}

/*
 * Reports the search as the input method's text field while Home is open
 * or opening (ws090-p022): its text, the caret at its end, and the
 * rectangle after it on the screen (zdesktop's own field has no window, so
 * the rectangle is the output's).  Returns 1 with the state, 0 while Home
 * is not to be open.
 */
int
kwl_home_field_state(
	struct kwl_server *server,
	char *text,
	size_t size,
	int32_t *cursor,
	int32_t *anchor,
	int32_t *rectangle)
{
	int32_t width;
	int32_t x;

	/* Only while Home is open or opening. */
	if (server->home_to <= 0.0f || size == 0U)
		return 0;

	/* The search, the caret at its end. */
	(void)snprintf(text, size, "%s", server->home_query);
	*cursor = (int32_t)strlen(text);
	*anchor = *cursor;

	/* The caret's place after the search text, as home_draw_search centres it. */
	width = glass_text_width(server, SIZE_SEARCH, server->home_query);
	x = ((int32_t)server->width - width) / 2;
	rectangle[0] = x + width;
	rectangle[1] = KWL_GLASS_BAR + 44;
	rectangle[2] = 2;
	rectangle[3] = 40;

	/* Succeeded: the state. */
	return 1;
}

/*
 * Takes what the input method made for the search (ws090-p022): bytes to
 * delete before the caret (its end), the text it committed (without
 * control characters, as much as fits), and the text being composed, shown
 * after the search.
 */
void
kwl_home_field_input(
	struct kwl_server *server,
	const char *preedit,
	const char *commit,
	uint32_t before)
{
	size_t length;
	size_t at;
	unsigned char byte;
	int changed;

	/* The bytes before the caret, whole characters. */
	changed = 0;
	if (before != 0U && server->home_query_length != 0U) {
		home_erase_bytes(server, before);
		changed = 1;
	}

	/* The text committed, as much as fits, cut at a character's start. */
	if (commit != NULL && commit[0] != '\0') {
		length = server->home_query_length;
		for (at = 0; commit[at] != '\0' && length + 1U < sizeof(server->home_query); at++) {
			byte = (unsigned char)commit[at];
			if (byte < 0x20U || byte == 0x7fU)
				continue;
			server->home_query[length] = (char)byte;
			length++;
		}

		/* A character the room cut goes whole (a continuation byte left out means its first bytes go too). */
		while (((unsigned char)commit[at] & 0xc0U) == 0x80U && length > server->home_query_length) {
			at--;
			length--;
		}
		server->home_query[length] = '\0';
		server->home_query_length = (unsigned)length;
		changed = 1;
	}

	/* The text being composed (none: empty). */
	server->home_preedit[0] = '\0';
	if (preedit != NULL)
		(void)snprintf(server->home_preedit, sizeof(server->home_preedit), "%s", preedit);
	server->dirty = 1;

	/* A changed search finds again. */
	if (changed)
		home_search_changed(server);
}

/* Erases the search's last characters, all their bytes. */
static void
home_erase(
	struct kwl_server *server,
	uint32_t characters)
{
	uint32_t erased;

	/* Each character: back over its continuation bytes to its first. */
	for (erased = 0U; erased < characters && server->home_query_length != 0U; erased++) {
		server->home_query_length--;
		while (server->home_query_length != 0U && ((unsigned char)server->home_query[server->home_query_length] & 0xc0U) == 0x80U)
			server->home_query_length--;
	}

	/* The new end. */
	server->home_query[server->home_query_length] = '\0';
}

/* Erases at least a number of bytes from the search's end, back to a character's start. */
static void
home_erase_bytes(
	struct kwl_server *server,
	uint32_t bytes)
{
	/* The bytes, or all of them. */
	if (bytes >= server->home_query_length)
		server->home_query_length = 0U;
	else
		server->home_query_length -= bytes;

	/* Back to a character's start. */
	while (server->home_query_length != 0U && ((unsigned char)server->home_query[server->home_query_length] & 0xc0U) == 0x80U)
		server->home_query_length--;
	server->home_query[server->home_query_length] = '\0';
}

/*
 * Keeps Home's animation drawing until it settles, and collects the
 * applications Home started that have ended.
 */
void
kwl_home_tick(
	struct kwl_server *server)
{
	uint64_t elapsed;
	uint64_t length;
	uint64_t now;
	pid_t child;
	int status;

	/*
	 * The applications' list is read ahead once the output shows
	 * (ws099-p002, C5): Home's first opening does not wait for the file
	 * and for each program to be looked up.
	 */
	if (server->windowed) {
		home_read_apps(server);
		home_prepare(server);
	}

	/* Every ended child is collected, so none is left a zombie. */
	for (;;) {
		child = waitpid(-1, &status, WNOHANG);
		if (child <= 0)
			break;
		printf("KWL HOME ended pid=%d status=%d\n", (int)child, status);
	}

	/* A page turn draws every frame until it is done; then where the icons are is logged. */
	now = kwl_milliseconds();
	if (server->home_page_moving) {
		server->dirty = 1;
		if (now - server->home_page_start_ms >= HOME_PAGE_MS) {
			server->home_page_moving = 0;
			home_layout(server);
			printf("KWL HOME page settled page=%u pages=%u\n", server->home_page + 1U, home_pages);
			home_log_icons();
		}
	}

	/* A launch whose window never came is forgotten. */
	if (server->home_launching && now - server->home_launch_ms > HOME_LAUNCH_FORGET_MS)
		server->home_launching = 0;

	/* No animation: nothing to draw. */
	if (!server->home_moving)
		return;

	/* The animation draws every frame; at its end Home is where it went. */
	server->dirty = 1;
	length = HOME_CLOSE_MS;
	if (server->home_to > server->home_from)
		length = HOME_OPEN_MS;
	elapsed = kwl_milliseconds() - server->home_start_ms;
	if (elapsed < length)
		return;

	/* Settled: opened or closed. */
	server->home_moving = 0;
	server->home = server->home_to;
	if (server->home > 0.0f) {
		home_layout(server);
		printf("KWL HOME opened apps=%u pages=%u page=%u at_ms=%llu\n", home_shown_count, home_pages, server->home_page + 1U, (unsigned long long)kwl_milliseconds());
		home_log_icons();
	} else {
		printf("KWL HOME closed at_ms=%llu\n", (unsigned long long)kwl_milliseconds());
	}
}

/*
 * Turns Home's pages with the wheel (either direction, a page a notch)
 * while Home shows.  Returns 1 when the scrolling is Home's.
 */
int
kwl_home_axis(
	struct kwl_server *server,
	int32_t vertical,
	int32_t horizontal)
{
	float progress;
	int32_t steps;

	/* Home takes the wheel only while it shows. */
	progress = kwl_home_progress(server);
	if (progress <= 0.0f && server->home_to <= 0.0f)
		return 0;

	/* Down or right is the next page, up or left the one before. */
	steps = vertical + horizontal;
	if (steps > 0)
		home_page_turn(server, (int)server->home_page + 1, "wheel");
	if (steps < 0)
		home_page_turn(server, (int)server->home_page - 1, "wheel");
	return 1;
}

/*
 * Tells a newly mapped window whether it is the one a launch from Home
 * waits for: once, with the icon's rectangle (x, y, width, height) it grows
 * from.  Returns 1 when it is and came within HOME_LAUNCH_WAIT_MS (it grows
 * out of the icon), 2 when it came later (it is the launch's but does not
 * grow), and 0 when no launch waits.
 */
int
kwl_home_launched(
	struct kwl_server *server,
	int32_t *rect)
{
	uint64_t waited;

	/* No launch waits (or it was forgotten, HOME_LAUNCH_FORGET_MS). */
	if (!server->home_launching)
		return 0;
	server->home_launching = 0;
	waited = kwl_milliseconds() - server->home_launch_ms;

	/* A window that came too late to grow out of the icon is still the launch's. */
	if (waited > HOME_LAUNCH_WAIT_MS) {
		printf("KWL HOME launched-late waited_ms=%llu at_ms=%llu\n", (unsigned long long)waited, (unsigned long long)kwl_milliseconds());
		memcpy(rect, server->home_launch_rect, sizeof(server->home_launch_rect));
		return 2;
	}

	/* The time from the icon's click to the window's first image (ws099-p016 measures it). */
	printf("KWL HOME launched waited_ms=%llu at_ms=%llu\n", (unsigned long long)waited, (unsigned long long)kwl_milliseconds());

	/* Succeeded: the icon's place. */
	memcpy(rect, server->home_launch_rect, sizeof(server->home_launch_rect));
	return 1;
}

/*
 * Starts a command with /bin/sh -c in a session of its own, without the
 * compositor's descriptors and with the compositor's socket in its
 * environment.  Returns the child's process ID, or -1 with errno set.
 */
pid_t
kwl_spawn(
	struct kwl_server *server,
	const char *command)
{
	char token[KWL_ACTIVATION_TOKEN_SIZE];
	char directory[108];
	const char *name;
	char *slash;
	pid_t child;
	int descriptor;
	int error;

	/*
	 * The program's activation token (activation.c, ws089-p016): with it
	 * the program may bring a window to the front, its own or that of a
	 * running copy of itself it hands its request to.  A program starts
	 * without one when none can be made.
	 */
	error = kwl_activation_issue(server, command, "spawn", token, sizeof(token));
	if (error != 0)
		token[0] = '\0';

	/* Forks the process that runs the command. */
	child = fork();
	if (child < 0)
		return -1;

	/* The child: its own session, none of the compositor's descriptors, the socket's place, and the command. */
	if (child == 0) {
		(void)setsid();
		for (descriptor = 3; descriptor < 1024; descriptor++)
			(void)close(descriptor);
		descriptor = open("/dev/null", O_RDONLY);
		if (descriptor >= 0 && descriptor != 0) {
			(void)dup2(descriptor, 0);
			(void)close(descriptor);
		}

		/* The socket as XDG_RUNTIME_DIR and WAYLAND_DISPLAY, the way clients look for it. */
		snprintf(directory, sizeof(directory), "%s", server->socket_path);
		slash = strrchr(directory, '/');
		name = directory;
		if (slash != NULL) {
			*slash = '\0';
			name = slash + 1;
			(void)setenv("XDG_RUNTIME_DIR", directory, 1);
		}

		/* The socket's name within that directory. */
		(void)setenv("WAYLAND_DISPLAY", name, 1);

		/* The program's own activation token, never one the compositor was started with. */
		if (token[0] != '\0') {
			(void)setenv("XDG_ACTIVATION_TOKEN", token, 1);
		} else {
			(void)unsetenv("XDG_ACTIVATION_TOKEN");
		}

		/* Only a failed exec comes back. */
		(void)execl("/bin/sh", "sh", "-c", command, (char *)NULL);
		_exit(127);
	}

	/* Succeeded: the parent has the child's process ID. */
	return child;
}

/*
 * Opens App Home, or closes it when it shows or is opening (the Windows
 * key pressed alone, ws142-p002, seat.c).
 */
void
kwl_home_toggle(
	struct kwl_server *server,
	const char *via)
{
	float progress;

	/* Showing or opening: it closes, the way its launcher closes it. */
	progress = kwl_home_progress(server);
	if (progress > 0.0f && server->home_to > 0.0f) {
		kwl_home_dismiss(server, via);
		return;
	}

	/* Otherwise it opens from where it is. */
	home_open(server, progress, via);
}

/*
 * Closes App Home when it shows or is opening, the way its launcher does
 * (for the top-right corner's swipe, which brings Notes over Home).
 */
void
kwl_home_dismiss(
	struct kwl_server *server,
	const char *via)
{
	float progress;

	/* Closed, or already closing: nothing to do. */
	progress = kwl_home_progress(server);
	if (progress <= 0.0f && server->home_to <= 0.0f)
		return;
	if (server->home_moving && server->home_to <= 0.0f)
		return;

	/* Any press Home was following is over. */
	server->home_press = 0;
	server->home_dragging = 0;
	server->home_page_press = 0;
	server->home_page_dragging = 0;
	server->home_page_offset = 0;
	server->home_bottom_press = 0;
	server->home_bottom_dragging = 0;

	/* Succeeded: Home closes from where it is. */
	home_close(server, progress, via);
}

/* Reads the applications' list once: the file, or the built-in list when there is none; a login session adds Log Out. */
static void
home_read_apps(
	struct kwl_server *server)
{
	char line[320];
	FILE *file;
	char *end;
	char *got;
	int managed;

	/* Read once. */
	if (home_apps_read)
		return;
	home_apps_read = 1;

	/* The file, a line an application; blank lines and # comments are skipped. */
	file = fopen(HOME_APPS_PATH, "r");
	if (file != NULL) {
		for (;;) {
			got = fgets(line, sizeof(line), file);
			if (got == NULL)
				break;

			/* The line without its newline; blank and comment lines are skipped. */
			end = strchr(line, '\n');
			if (end != NULL)
				*end = '\0';
			if (line[0] == '\0' || line[0] == '#')
				continue;
			home_parse_line(line);
		}

		/* The file is not needed again. */
		fclose(file);
	}

	/* Without a usable file, the desktop's applications (ws129-p010: Settings, and no test client). */
	if (home_app_count == 0U) {
		home_add_app("Terminal", KEILAND_BINDIR "/terminal", "term shell console sh", 0x323a4eU, "terminal");
		home_add_app("Model viewer", KEILAND_BINDIR "/mview --windowed --size=960x640", "3d mview model vulkan viewer", 0xe07a5aU, "model");
		home_add_app("X terminal", "/bin/sh " KEILAND_LIBEXECDIR "/keiland-x11 " KEILAND_BINDIR "/zterm -geometry 80x24", "x11 xterm zterm", 0x4a4a78U, "xterm");
		home_add_app("Gears", "/bin/sh " KEILAND_LIBEXECDIR "/keiland-x11 " KEILAND_BINDIR "/zgears --frames=0", "gears opengl glx x11 3d", 0xd05a3aU, "gears");
		home_add_app("Files", KEILAND_BINDIR "/files", "files file manager folder finder browse", 0x2f7cf6U, "files");
		home_add_app("Notes", KEILAND_BINDIR "/notes", "notes note notebook pen handwriting draw pdf", 0xe0a526U, "notes");
		home_add_app("Settings", KEILAND_BINDIR "/settings", "settings preferences control panel system network wifi display sound wallpaper about", 0x6b7a8fU, "settings");
		home_add_app("PDF Viewer", KEILAND_BINDIR "/pdfviewer", "pdf viewer document reader", 0xd9534fU, "pdf");
		home_add_app("Image Viewer", KEILAND_BINDIR "/imageview", "image picture photo viewer png jpeg gif", 0x3fa36bU, "image");
		home_add_app("Text Editor", KEILAND_BINDIR "/textedit", "text editor edit txt notepad write", 0x1f9e9aU, "text");
		home_add_app("Browser", KEILAND_BINDIR "/browser " HOME_BROWSER_START, "browser web www html internet", 0x3a8fd8U, "browser");
	}

	/* A login's session locks (ws035-p102) and ends with Power Off's dialog (ws099-p037, Log Out in it), the last icons. */
	managed = 0;
	if (server->session)
		managed = kl_backend_session_managed(server->backend);
	if (managed)
		home_add_app("Lock Screen", HOME_LOCK, "lock screen away", 0x5a6aa0U, "lock");
	if (server->session)
		home_add_app("Power Off", HOME_POWER, "power off shut down shutdown restart reboot logout log out sign out exit session end", 0x6a7488U, "power");
}

/* Adds an application to the list, when there is room; its picture is named as icons.c names it ("" for none). */
static void
home_add_app(
	const char *name,
	const char *command,
	const char *keywords,
	uint32_t rgb,
	const char *picture)
{
	struct home_app *app;
	int present;

	/* A full list takes no more. */
	if (home_app_count >= HOME_APPS_MAX)
		return;

	/* An application whose program or files are not on this system is not shown. */
	present = home_present(name, command);
	if (!present)
		return;

	/* The application's texts and its colour. */
	app = &home_apps[home_app_count];
	snprintf(app->name, sizeof(app->name), "%s", name);
	snprintf(app->command, sizeof(app->command), "%s", command);
	snprintf(app->keywords, sizeof(app->keywords), "%s", keywords);
	app->color[0] = (float)((rgb >> 16) & 0xffU) / 255.0f;
	app->color[1] = (float)((rgb >> 8) & 0xffU) / 255.0f;
	app->color[2] = (float)(rgb & 0xffU) / 255.0f;
	app->color[3] = 1.0f;

	/* The picture on its tile; a name icons.c does not know leaves the first letter. */
	app->picture = kwl_icon_named(picture);
	home_app_count++;
}

/*
 * Tells whether every absolute path in a command (its program, a script it
 * runs, a file it opens) is on this system; a missing one is logged.
 */
static int
home_present(
	const char *name,
	const char *command)
{
	char path[160];
	const char *word;
	size_t operator;
	size_t length;
	int missing;

	/* Each word, up to a space; a redirection or another shell operator ends the words that are looked at. */
	word = command;
	while (*word != '\0') {
		/* The word's length, and the next one. */
		length = strcspn(word, " ");
		operator = strcspn(word, "<>|;&");
		if (operator < length)
			break;
		if (word[0] == '/' && length < sizeof(path)) {
			memcpy(path, word, length);
			path[length] = '\0';
			missing = access(path, F_OK);
			if (missing != 0) {
				printf("KWL HOME skip name=%s missing=%s\n", name, path);
				return 0;
			}
		}

		/* Past the word and its spaces. */
		word += length;
		while (*word == ' ')
			word++;
	}

	/* Succeeded: everything it names is there. */
	return 1;
}

/* Reads one line of the list: name|command|keywords|RRGGBB|picture (the last three may be left out). */
static void
home_parse_line(
	char *line)
{
	static char empty[1];
	char *fields[5];
	char *bar;
	unsigned count;

	/* Splits the line at the bars; a field left out is empty. */
	fields[0] = line;
	fields[1] = empty;
	fields[2] = empty;
	fields[3] = empty;
	fields[4] = empty;
	count = 1U;
	while (count < 5U) {
		bar = strchr(fields[count - 1U], '|');
		if (bar == NULL)
			break;
		*bar = '\0';
		fields[count] = bar + 1;
		count++;
	}

	/* A line needs a name and a command. */
	if (count < 2U || fields[0][0] == '\0' || fields[1][0] == '\0')
		return;

	/* Succeeded: the application joins the list (grey without a colour, its first letter without a picture). */
	home_add_app(fields[0], fields[1], fields[2], home_hex(fields[3]), fields[4]);
}

/* Reads an RRGGBB colour; a malformed one is a mid grey. */
static uint32_t
home_hex(
	const char *text)
{
	unsigned long value;
	size_t length;
	char *end;

	/* Six hexadecimal digits and nothing more. */
	length = strlen(text);
	value = strtoul(text, &end, 16);
	if (end == text || *end != '\0' || length != 6U)
		return 0x707888U;

	/* Reports the colour. */
	return (uint32_t)value;
}

/*
 * Works out which applications show and where their icons go: a centred
 * grid of up to six columns; without a search, pages of HOME_PAGE side by
 * side, moved by where the pages are (dragged or turning).
 */
static void
home_layout(
	struct kwl_server *server)
{
	unsigned index;
	unsigned columns;
	unsigned rows;
	unsigned slot;
	unsigned place;
	unsigned page;
	float position;
	int32_t left;
	int32_t top;
	int32_t space;
	int32_t shift;
	int found;

	/* The applications the search finds (all, without one). */
	home_shown_count = 0U;
	for (index = 0U; index < home_app_count; index++) {
		found = home_matches(&home_apps[index], server->home_query);
		if (found)
			home_shown[home_shown_count++] = index;
	}

	/* The selection stays on something shown. */
	if (server->home_selected >= (int)home_shown_count)
		server->home_selected = (int)home_shown_count - 1;
	if (server->home_selected < 0 && home_shown_count != 0U)
		server->home_selected = 0;

	/* The pages: one while searching, else as many as the icons fill; the page shown is one of them. */
	home_pages = 1U;
	if (server->home_query_length == 0U && home_shown_count > (unsigned)HOME_PAGE)
		home_pages = (home_shown_count + (unsigned)HOME_PAGE - 1U) / (unsigned)HOME_PAGE;
	if (server->home_page >= home_pages)
		server->home_page = home_pages - 1U;

	/* Nothing shown, nothing to place. */
	if (home_shown_count == 0U)
		return;

	/* The grid (one page's, the fullest), centred in the space under the system bar (a little above the middle). */
	columns = home_shown_count;
	if (columns > HOME_COLUMNS)
		columns = HOME_COLUMNS;
	rows = (home_shown_count + columns - 1U) / columns;
	if (home_pages > 1U)
		rows = HOME_ROWS;
	left = ((int32_t)server->width - (int32_t)columns * HOME_CELL_WIDTH) / 2;
	space = (int32_t)server->height - KWL_GLASS_BAR;
	top = KWL_GLASS_BAR + (space - (int32_t)rows * HOME_CELL_HEIGHT) * 2 / 5;

	/* Where the pages are: the page shown, or between two while dragged or turning. */
	position = 0.0f;
	if (home_pages > 1U)
		position = home_page_position(server);

	/* Each icon's top-left corner, centred in its cell, its page a screen's width from the next. */
	for (slot = 0U; slot < home_shown_count; slot++) {
		page = 0U;
		place = slot;
		if (home_pages > 1U) {
			page = slot / (unsigned)HOME_PAGE;
			place = slot % (unsigned)HOME_PAGE;
		}

		/* The page's shift, and the cell. */
		shift = (int32_t)(((float)page - position) * (float)server->width);
		home_icon_x[slot] = shift + left + (int32_t)(place % columns) * HOME_CELL_WIDTH + (HOME_CELL_WIDTH - HOME_ICON) / 2;
		home_icon_y[slot] = top + (int32_t)(place / columns) * HOME_CELL_HEIGHT + 20;
	}
}

/* Tells whether an application is found by the search (an empty search finds all). */
static int
home_matches(
	const struct home_app *app,
	const char *query)
{
	int found;

	/* Nothing typed finds everything. */
	if (query[0] == '\0')
		return 1;

	/* The name, then the command, then the keywords. */
	found = home_contains(app->name, query);
	if (found)
		return 1;
	found = home_contains(app->command, query);
	if (found)
		return 1;
	found = home_contains(app->keywords, query);
	if (found)
		return 1;

	/* Not found. */
	return 0;
}

/* Tells whether a text contains the query, ignoring the case of letters. */
static int
home_contains(
	const char *text,
	const char *query)
{
	size_t start;
	size_t index;
	char a;
	char b;

	/* Tries the query at each place of the text. */
	for (start = 0U; text[start] != '\0'; start++) {
		for (index = 0U; query[index] != '\0'; index++) {
			a = text[start + index];
			b = query[index];
			if (a >= 'A' && a <= 'Z')
				a = (char)(a - 'A' + 'a');
			if (a != b)
				break;
		}

		/* Every character of the query matched here. */
		if (query[index] == '\0')
			return 1;
	}

	/* No place matches. */
	return 0;
}

/* Returns the application whose icon (or label) is under a point, or -1. */
static int
home_icon_at(
	struct kwl_server *server,
	int32_t x,
	int32_t y)
{
	unsigned slot;

	/* Each icon's cell: the icon and its label under it. */
	home_layout(server);
	for (slot = 0U; slot < home_shown_count; slot++) {
		if (x < home_icon_x[slot] - 20 || x >= home_icon_x[slot] + HOME_ICON + 20)
			continue;
		if (y < home_icon_y[slot] || y >= home_icon_y[slot] + HOME_ICON + HOME_LABEL + 12)
			continue;
		return (int)home_shown[slot];
	}

	/* Nothing there. */
	return -1;
}

/* Draws one icon: its tile with its picture (or the name's first letter), the selection's ring, and the name under it. */
static void
home_draw_icon(
	struct kwl_server *server,
	VkCommandBuffer command,
	unsigned slot,
	float opacity,
	float rise)
{
	const struct home_app *app;
	struct kwl_object *running;
	const char *label;
	struct glass_shape shape;
	float color[4];
	float size;
	float left;
	float top;
	float lighten;
	float floor;
	float spot;
	int32_t x;
	int32_t y;
	int32_t width;
	int over;

	/* Where the icon is (lower while it rises in), and whether the pointer is on it. */
	app = &home_apps[home_shown[slot]];
	x = home_icon_x[slot];
	y = home_icon_y[slot];
	over = 0;
	if (server->pointer_x >= x && server->pointer_x < x + HOME_ICON && server->pointer_y >= y && server->pointer_y < y + HOME_ICON)
		over = 1;
	y += (int32_t)(rise + 0.5f);

	/* The started application's icon grows as Home closes (about its centre). */
	size = (float)HOME_ICON;
	if (server->home_launch_app == (int)home_shown[slot] && server->home_to <= 0.0f)
		size = (float)HOME_ICON * (1.0f + 0.3f * (1.0f - opacity));
	left = (float)x - (size - (float)HOME_ICON) * 0.5f;
	top = (float)y - (size - (float)HOME_ICON) * 0.5f;

	/*
	 * An application with a picture is its banded tile with the picture cut
	 * out (ws128-p012), lighter under the pointer and without a shadow,
	 * which would show through the picture; the picture is a hole through
	 * to the desktop's wallpaper (ws099-p034b, the 2026-10-06 user
	 * decision; BUG-237 showed the blurred one).  Any other is its letter's
	 * tile.
	 */
	floor = (float)(home_icon_y[slot] + HOME_ICON + HOME_FLOOR_GAP);
	if (app->picture >= 0) {
		/* Brighter under the pointer: the tile whitened, its spotlight stronger. */
		lighten = 0.0f;
		spot = HOME_SPOT;
		if (over) {
			lighten = HOME_LIT;
			spot = HOME_SPOT_LIT;
		}

		/* Its spotlight on the floor behind it, the tile, its reflection under the floor. */
		home_draw_spotlight(server, command, left + size * 0.5f, floor, spot * opacity);
		glass_draw_app_tile(server, command, (unsigned)app->picture, left, top, size, opacity, lighten, GLASS_HOLE_WALLPAPER);
		glass_draw_app_tile_reflection(server, command, (unsigned)app->picture, left, floor + 2.0f, size, size * HOME_REFLECTION_HEIGHT, HOME_REFLECTION * opacity);
	} else {
		home_draw_letter(server, command, app, x, y, left, top, size, over, opacity);
	}

	/* The selection (with the keyboard, or the first search result) has a blue ring. */
	if ((int)slot == server->home_selected && (server->home_query_length != 0U || server->home_selected > 0)) {
		glass_shape_init(&shape, (float)x - 5.0f, (float)y - 5.0f, (float)HOME_ICON + 10.0f, (float)HOME_ICON + 10.0f);
		shape.mode = MODE_RING;
		shape.radius = HOME_ICON_RADIUS + 5.0f;
		shape.soft = 2.5f;
		shape.color[0] = 0.25f;
		shape.color[1] = 0.52f;
		shape.color[2] = 0.98f;
		shape.color[3] = opacity;
		glass_shape_draw(server, command, &shape);
	}

	/* The name under the reflection, centred on the icon, white on the stage, in the desktop's language (the search keeps the English, WS158). */
	color[0] = 1.0f;
	color[1] = 1.0f;
	color[2] = 1.0f;
	color[3] = HOME_NAME_ALPHA * opacity;
	label = kl_tr(app->name);
	width = glass_text_width(server, SIZE_TITLE, label);
	if (width > HOME_CELL_WIDTH - 8)
		width = HOME_CELL_WIDTH - 8;
	glass_draw_text(server, command, SIZE_TITLE, x + (HOME_ICON - width) / 2, y + HOME_ICON + HOME_LABEL, label, HOME_CELL_WIDTH - 8, color);

	/* An application that runs already has a short line under its name, as the bar's current application (BUG-232). */
	running = home_running_window(server, home_shown[slot]);
	if (running != NULL)
		glass_draw_solid(server, command, (float)(x + HOME_ICON / 2 - 4), (float)(y + HOME_ICON + HOME_LABEL + 7), 8.0f, 2.5f, 1.25f, color);
}

/*
 * Gives how far an icon (the order-th drawn) has come in (0 to 1), and how
 * far under its place it still is (rise, pixels): while Home opens or is
 * open, from its own start (HOME_CONTENT_STEP_MS after the one before it,
 * eased out over HOME_CONTENT_MS, BUG-225); while Home closes, or follows
 * a drag, as far as Home is open.
 */
static float
home_content(
	struct kwl_server *server,
	unsigned order,
	float progress,
	float *rise)
{
	uint64_t now;
	uint64_t start;
	float t;

	/* Closing, following a drag, or shown at once: with Home itself. */
	*rise = 0.0f;
	if (server->home_to <= 0.0f || server->home_dragging || server->home_content_ms == 0U)
		return progress;

	/* Not begun yet. */
	now = kwl_milliseconds();
	start = server->home_content_ms + (uint64_t)order * HOME_CONTENT_STEP_MS;
	if (now <= start) {
		*rise = HOME_CONTENT_RISE;
		server->dirty = 1;
		return 0.0f;
	}

	/* On its way (1 - (1 - t)^3), drawn every frame until it has come. */
	t = (float)(now - start) / (float)HOME_CONTENT_MS;
	if (t >= 1.0f)
		return 1.0f;
	server->dirty = 1;
	t = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
	*rise = HOME_CONTENT_RISE * (1.0f - t);

	/* Succeeded: partly in. */
	return t;
}

/*
 * Prepares Home's opening ahead, once the output shows (BUG-225): the
 * glyphs of the names in the desktop's language (any not in the printable
 * ASCII the atlas keeps are rendered on their first use, glass.c), so that
 * the first frame of the content waits for nothing.
 */
static void
home_prepare(
	struct kwl_server *server)
{
	const char *label;
	unsigned app;

	/* Once, after the list is read and the first frame (the glyphs' atlas is made by then). */
	if (home_prepared || home_app_count == 0U || server->frame == 0U)
		return;
	home_prepared = 1U;

	/* Each name's glyphs, by measuring it. */
	for (app = 0U; app < home_app_count; app++) {
		label = kl_tr(home_apps[app].name);
		(void)glass_text_width(server, SIZE_TITLE, label);
	}

	/* The log says it is done. */
	printf("KWL HOME prepared names=%u\n", home_app_count);
}

/*
 * Draws each row's glossy floor under its icons (ws099-p035b): a thin line
 * across the row's icons shown, fading out towards both ends.
 */
static void
home_draw_floors(
	struct kwl_server *server,
	VkCommandBuffer command,
	float opacity)
{
	int32_t rows[HOME_APPS_MAX];
	int32_t lefts[HOME_APPS_MAX];
	int32_t rights[HOME_APPS_MAX];
	unsigned count;
	unsigned slot;
	unsigned row;
	int32_t x;

	/* The rows of the icons on the output: their tops, and the leftmost and rightmost icon of each. */
	count = 0U;
	for (slot = 0U; slot < home_shown_count; slot++) {
		x = home_icon_x[slot];
		if (x + HOME_ICON < 0 || x > (int32_t)server->width)
			continue;

		/* The row it is in, or a new one. */
		for (row = 0U; row < count; row++) {
			if (rows[row] == home_icon_y[slot])
				break;
		}

		/* A new row starts with this icon. */
		if (row == count) {
			rows[count] = home_icon_y[slot];
			lefts[count] = x;
			rights[count] = x + HOME_ICON;
			count++;
			continue;
		}

		/* A row known: wider. */
		if (x < lefts[row])
			lefts[row] = x;
		if (x + HOME_ICON > rights[row])
			rights[row] = x + HOME_ICON;
	}

	/* Each row's floor, a little wider than its icons. */
	for (row = 0U; row < count; row++)
		home_draw_floor(server, command, (float)(lefts[row] - 36), (float)(rights[row] + 36), (float)(rows[row] + HOME_ICON + HOME_FLOOR_GAP), opacity);
}

/*
 * Draws one floor line from left to right at y: bright in the middle and
 * fading out at both ends (in HOME_FLOOR_BANDS pieces), a pixel above it
 * and two under it fainter, so it reads as a glossy edge.
 */
static void
home_draw_floor(
	struct kwl_server *server,
	VkCommandBuffer command,
	float left,
	float right,
	float y,
	float opacity)
{
	static const float weights[4] = { 1.0f, 0.55f, 0.35f, 0.2f };
	static const float offsets[4] = { 0.0f, 1.0f, -1.0f, 2.0f };
	float color[4];
	float piece;
	float along;
	unsigned band;
	unsigned line;

	/* Each line of the edge, each piece along it. */
	piece = (right - left) / (float)HOME_FLOOR_BANDS;
	color[0] = 0.86f;
	color[1] = 0.90f;
	color[2] = 1.0f;
	for (line = 0U; line < 4U; line++) {
		for (band = 0U; band < HOME_FLOOR_BANDS; band++) {
			/* How far the piece's middle is from the line's middle (0 there, 1 at the ends), its brightness falling. */
			along = ((float)band + 0.5f) / (float)HOME_FLOOR_BANDS * 2.0f - 1.0f;
			if (along < 0.0f)
				along = -along;
			color[3] = HOME_FLOOR * weights[line] * (1.0f - along) * opacity;
			glass_draw_solid(server, command, left + piece * (float)band, y + offsets[line], piece, 1.0f, 0.0f, color);
		}
	}
}

/* Draws a tile's spotlight: a soft elliptic pool of white light on the floor behind it. */
static void
home_draw_spotlight(
	struct kwl_server *server,
	VkCommandBuffer command,
	float middle,
	float floor,
	float strength)
{
	struct glass_shape shape;
	float half;

	/* An ellipse 1.6 tiles wide and 36 pixels tall, its middle 8 under the floor, softened by 8. */
	half = (float)HOME_ICON * 0.8f;
	glass_shape_init(&shape, middle - half, floor + 8.0f - 18.0f, 2.0f * half, 36.0f);
	shape.quad[0] -= 16.0f;
	shape.quad[1] -= 16.0f;
	shape.quad[2] += 32.0f;
	shape.quad[3] += 32.0f;
	shape.mode = MODE_SHADOW;
	shape.radius = 18.0f;
	shape.soft = 8.0f;
	shape.color[0] = 1.0f;
	shape.color[1] = 1.0f;
	shape.color[2] = 1.0f;
	shape.color[3] = strength;
	glass_shape_draw(server, command, &shape);
}

/*
 * Draws the tile of an application without a picture at (left, top), size
 * pixels a side (its place x, y when it is not growing): a soft shadow, its
 * rounded square in its colour shaded like lit glass (lighter under the
 * pointer), and the name's first letter.
 */
static void
home_draw_letter(
	struct kwl_server *server,
	VkCommandBuffer command,
	const struct home_app *app,
	int32_t x,
	int32_t y,
	float left,
	float top,
	float size,
	int over,
	float opacity)
{
	static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	struct glass_shape shape;
	float color[4];
	char letter[2];
	int32_t width;

	/* A soft shadow under the icon. */
	glass_shape_init(&shape, left, top + 6.0f, size, size);
	shape.quad[0] -= 24.0f;
	shape.quad[1] -= 24.0f;
	shape.quad[2] += 48.0f;
	shape.quad[3] += 48.0f;
	shape.mode = MODE_SHADOW;
	shape.radius = HOME_ICON_RADIUS;
	shape.soft = 14.0f;
	shape.color[0] = 0.10f;
	shape.color[1] = 0.18f;
	shape.color[2] = 0.35f;
	shape.color[3] = 0.22f;
	shape.opacity = opacity;
	glass_shape_draw(server, command, &shape);

	/* The icon's square in its colour, lighter under the pointer. */
	memcpy(color, app->color, sizeof(color));
	if (over) {
		color[0] = color[0] + (1.0f - color[0]) * HOME_LIT;
		color[1] = color[1] + (1.0f - color[1]) * HOME_LIT;
		color[2] = color[2] + (1.0f - color[2]) * HOME_LIT;
	}

	/* The square, shaded like lit glass and faded in with Home. */
	color[3] = opacity;
	home_draw_tile(server, command, left, top, size, color);

	/* The name's first letter, white and large, in the middle. */
	memcpy(color, white, sizeof(color));
	color[3] = opacity;
	letter[0] = app->name[0];
	letter[1] = '\0';
	width = glass_text_width(server, SIZE_ICON, letter);
	glass_draw_text(server, command, SIZE_ICON, x + (HOME_ICON - width) / 2, y + HOME_ICON / 2 + 13, letter, HOME_ICON, color);
}

/*
 * Draws an application's tile: its rounded square in its colour, in bands
 * whitened towards the top and darkened towards the bottom, and a faint
 * white rim.  Every band is cut by the whole square's rounded corners, so
 * the bands meet without seams.  color's alpha fades the whole tile.
 */
static void
home_draw_tile(
	struct kwl_server *server,
	VkCommandBuffer command,
	float left,
	float top,
	float size,
	const float *color)
{
	struct glass_shape shape;
	float band_top;
	float band_bottom;
	float middle;
	float shade;
	int band;

	/* Each band, from the top. */
	for (band = 0; band < HOME_SHADE_BANDS; band++) {
		band_top = top + size * (float)band / (float)HOME_SHADE_BANDS;
		band_bottom = top + size * (float)(band + 1) / (float)HOME_SHADE_BANDS;
		middle = ((float)band + 0.5f) / (float)HOME_SHADE_BANDS;

		/* The rounded square as the shape, only this band of it drawn. */
		glass_shape_init(&shape, left, top, size, size);
		shape.quad[1] = band_top;
		shape.quad[3] = band_bottom - band_top;
		shape.mode = MODE_SOLID;
		shape.radius = HOME_ICON_RADIUS * size / (float)HOME_ICON;
		memcpy(shape.color, color, sizeof(shape.color));

		/* The upper half whitened, most at the top; the lower half darkened, most at the bottom. */
		if (middle < 0.5f) {
			shade = HOME_SHADE_LIGHT * (1.0f - middle / 0.5f);
			shape.color[0] = color[0] + (1.0f - color[0]) * shade;
			shape.color[1] = color[1] + (1.0f - color[1]) * shade;
			shape.color[2] = color[2] + (1.0f - color[2]) * shade;
		} else {
			shade = HOME_SHADE_DARK * ((middle - 0.5f) / 0.5f);
			shape.color[0] = color[0] * (1.0f - shade);
			shape.color[1] = color[1] * (1.0f - shade);
			shape.color[2] = color[2] * (1.0f - shade);
		}

		/* The band, over the shadow. */
		glass_shape_draw(server, command, &shape);
	}

	/* The faint white rim of lit glass. */
	glass_shape_init(&shape, left, top, size, size);
	shape.mode = MODE_RING;
	shape.radius = HOME_ICON_RADIUS * size / (float)HOME_ICON;
	shape.soft = 1.0f;
	shape.color[0] = 1.0f;
	shape.color[1] = 1.0f;
	shape.color[2] = 1.0f;
	shape.color[3] = HOME_RIM_ALPHA * color[3];
	glass_shape_draw(server, command, &shape);
}

/* Draws the search text at the top, on a faint pill (no search box). */
static void
home_draw_search(
	struct kwl_server *server,
	VkCommandBuffer command,
	float opacity)
{
	static const float pill[4] = { 1.0f, 1.0f, 1.0f, 0.55f };
	static const float ink[4] = { 0.12f, 0.16f, 0.24f, 1.0f };
	float color[4];
	int32_t width;
	int32_t typed;
	int32_t composed;
	int32_t x;
	int32_t y;

	/* The text's width with the one being composed after it, and the pill a little wider, centred under the system bar. */
	typed = glass_text_width(server, SIZE_SEARCH, server->home_query);
	composed = glass_text_width(server, SIZE_SEARCH, server->home_preedit);
	width = typed + composed;
	x = ((int32_t)server->width - width) / 2;
	y = KWL_GLASS_BAR + 44;
	memcpy(color, pill, sizeof(color));
	color[3] = pill[3] * opacity;
	glass_draw_solid(server, command, (float)(x - 22), (float)y, (float)(width + 44), 40.0f, 20.0f, color);

	/* The text itself, and the composed text after it, underlined. */
	memcpy(color, ink, sizeof(color));
	color[3] = opacity;
	glass_draw_text(server, command, SIZE_SEARCH, x, y + 28, server->home_query, (int32_t)server->width, color);
	if (composed > 0) {
		glass_draw_text(server, command, SIZE_SEARCH, x + typed, y + 28, server->home_preedit, (int32_t)server->width, color);
		glass_draw_solid(server, command, (float)(x + typed), (float)(y + 32), (float)composed, 1.5f, 0.0f, color);
	}
}

/* Opens Home, from where it is now. */
static void
home_open(
	struct kwl_server *server,
	float from,
	const char *via)
{
	/* The search starts empty, the first application selected. */
	server->home_query_length = 0U;
	server->home_query[0] = '\0';
	server->home_preedit[0] = '\0';
	server->home_selected = (int)(server->home_page * (unsigned)HOME_PAGE);
	server->home_launch_app = -1;
	server->home_page_press = 0;
	server->drag = NULL;

	/*
	 * The content comes in after the stage (BUG-225), from now; Home already
	 * partly open (a drag let go) shows it whole at once.  The first frames
	 * of both are logged (the time from the request).
	 */
	server->home_asked_ms = kwl_milliseconds();
	server->home_content_ms = server->home_asked_ms;
	if (from > 0.0f)
		server->home_content_ms = 0U;
	server->home_cover_logged = 0U;
	server->home_content_logged = 0U;
	printf("KWL HOME open via=%s at_ms=%llu\n", via, (unsigned long long)kwl_milliseconds());
	kwl_transition_request(server, "home-open");
	home_settle(server, from, 1.0f);
}

/* Closes Home, from where it is now; the search is forgotten. */
static void
home_close(
	struct kwl_server *server,
	float from,
	const char *via)
{
	/* The search goes with it. */
	server->home_query_length = 0U;
	server->home_query[0] = '\0';
	server->home_preedit[0] = '\0';
	printf("KWL HOME close via=%s at_ms=%llu\n", via, (unsigned long long)kwl_milliseconds());
	kwl_transition_request(server, "home-close");
	home_settle(server, from, 0.0f);
}

/* Starts the animation from one position to another. */
static void
home_settle(
	struct kwl_server *server,
	float from,
	float to)
{
	/* The animation's ends and its start; the frames follow in kwl_home_tick. */
	server->home_from = from;
	server->home_to = to;
	server->home_start_ms = kwl_milliseconds();
	server->home_moving = 1;
	server->dirty = 1;

	/* The input method serves the search while Home is to be open, an application's text input otherwise (ws090-p022). */
	kwl_ime_field_changed(server);
}

/*
 * Starts an application with /bin/sh -c, with the compositor's socket in
 * its environment, or switches to it when it runs already (BUG-232).
 * Returns 1 when Home was closed by it (the switch), 0 when the caller
 * closes Home.
 */
static int
home_launch(
	struct kwl_server *server,
	unsigned app)
{
	struct kwl_object *running;
	pid_t child;
	unsigned slot;
	int differs;

	/* Lock Screen locks the session (App Home closes behind it). */
	differs = strcmp(home_apps[app].command, HOME_LOCK);
	if (differs == 0) {
		(void)kwl_lock(server, "home");
		return 0;
	}

	/*
	 * Power Off: its dialog over the desktop (Power Off, Restart, Log Out,
	 * Cancel; power-dialog.c), Home closing behind.  Its Log Out ends the
	 * session as this icon did before (handoff.c).
	 */
	differs = strcmp(home_apps[app].command, HOME_POWER);
	if (differs == 0) {
		kwl_power_dialog_open(server, "home");
		return 0;
	}

	/*
	 * An application that runs already is not started again (BUG-232, the
	 * 2026-10-06 user request): its latest window comes to the front (its
	 * desktop shown, back from minimized, as the layout mode is) and Home
	 * closes.
	 */
	running = home_running_window(server, app);
	if (running != NULL) {
		printf("KWL HOME switch name=%s surface=%u client=%llu\n", home_apps[app].name, running->id, (unsigned long long)running->client->number);
		kwl_glass_activate(server, running, "home");
		return 1;
	}

	/* The application, in its own session with the compositor's socket. */
	child = kwl_spawn(server, home_apps[app].command);
	if (child < 0) {
		printf("KWL HOME launch name=%s error=%d\n", home_apps[app].name, errno);
		return 0;
	}

	/* Its icon grows as Home closes, and its first window will grow out of the icon's place. */
	server->home_launch_app = (int)app;
	server->home_launching = 0;
	for (slot = 0U; slot < home_shown_count; slot++) {
		if (home_shown[slot] != app)
			continue;
		server->home_launching = 1;
		server->home_launch_ms = kwl_milliseconds();
		server->home_launch_rect[0] = home_icon_x[slot];
		server->home_launch_rect[1] = home_icon_y[slot];
		server->home_launch_rect[2] = HOME_ICON;
		server->home_launch_rect[3] = HOME_ICON;
	}

	/* Succeeded: the application is starting. */
	printf("KWL HOME launch name=%s pid=%d\n", home_apps[app].name, (int)child);
	return 0;
}

/*
 * Finds the latest window (the highest map order, on any desktop) of an
 * application of the list that runs already: a window whose application ID
 * has the application's picture (icons.c), or for an application without
 * a picture the name of its program (the command's first word's last part,
 * not a shell).  Returns NULL when it does not run.
 */
static struct kwl_object *
home_running_window(
	struct kwl_server *server,
	unsigned app)
{
	struct kwl_client *client;
	struct kwl_object *surface;
	struct kwl_object *found;
	char program[64];
	const char *start;
	size_t length;
	int picture;
	int same;

	/* The program's name: the command's first word, after its last slash. */
	length = strcspn(home_apps[app].command, " ");
	start = home_apps[app].command;
	(void)snprintf(program, sizeof(program), "%.*s", (int)length, start);
	start = strrchr(program, '/');
	if (start != NULL)
		memmove(program, start + 1, strlen(start + 1) + 1U);

	/* Every shown toplevel window of every client: the latest one of the application. */
	found = NULL;
	for (client = server->clients; client != NULL; client = client->next) {
		if (client->fatal)
			continue;
		for (surface = client->objects; surface != NULL; surface = surface->next) {
			/* A mapped window with an application ID, not a dialog or a sheet. */
			if (surface->kind != KWL_SURFACE || surface->dead || !surface->mapped)
				continue;
			if (surface->role == NULL || surface->cursor_role || surface->parent_window != NULL)
				continue;
			if (surface->app_id[0] == '\0')
				continue;

			/* The application's: by its picture, or by its program's name (a shell starts many). */
			if (home_apps[app].picture >= 0) {
				picture = kwl_icon_for_app_id(surface->app_id);
				if (picture != home_apps[app].picture)
					continue;
			} else {
				same = strcmp(surface->app_id, program);
				if (same != 0)
					continue;
				same = strcmp(program, "sh");
				if (same == 0)
					continue;
			}

			/* The latest. */
			if (found == NULL || surface->map_order > found->map_order)
				found = surface;
		}
	}

	/* Succeeded: the window, or NULL. */
	return found;
}

/* Lays the icons out again for a changed search, selects the first result and says what was found. */
static void
home_search_changed(
	struct kwl_server *server)
{
	unsigned slot;

	/* The first result is selected; the input method serving the search hears its new text (ws090-p022). */
	server->home_selected = 0;
	home_layout(server);
	server->dirty = 1;
	kwl_ime_field_changed(server);

	/* The search and its results, for whoever reads the log. */
	printf("KWL HOME search query=\"%s\" results=%u", server->home_query, home_shown_count);
	for (slot = 0U; slot < home_shown_count; slot++)
		printf(" [%s]", home_apps[home_shown[slot]].name);
	printf("\n");
	home_log_icons();
}

/* Says where each icon shown is (its centre), for whoever reads the log (the tests click there). */
static void
home_log_icons(void)
{
	unsigned slot;

	/* One line an icon. */
	for (slot = 0U; slot < home_shown_count; slot++)
		printf("KWL HOME icon name=\"%s\" x=%d y=%d\n", home_apps[home_shown[slot]].name, home_icon_x[slot] + HOME_ICON / 2, home_icon_y[slot] + HOME_ICON / 2);
}

/* Eases an animation: quick at first, settling gently (cubic ease-out). */
static float
home_ease(
	float t)
{
	float remaining;

	/* 1 - (1 - t)^3. */
	remaining = 1.0f - t;
	return 1.0f - remaining * remaining * remaining;
}

/* Returns where the pages are, as a page number: the page shown, dragged by the pointer, or turning. */
static float
home_page_position(
	struct kwl_server *server)
{
	uint64_t elapsed;
	float t;

	/* Dragged: the page shown, moved by the drag (a drag to the left brings the next page). */
	if (server->home_page_dragging && server->home_page_offset != 0)
		return (float)server->home_page - (float)server->home_page_offset / (float)server->width;

	/* Settled on the page. */
	if (!server->home_page_moving)
		return (float)server->home_page;

	/* Turning: eased from where the pages were to the page. */
	elapsed = kwl_milliseconds() - server->home_page_start_ms;
	t = (float)elapsed / (float)HOME_PAGE_MS;
	if (t > 1.0f)
		t = 1.0f;
	return server->home_page_from + (server->home_page_to - server->home_page_from) * home_ease(t);
}

/* Turns to a page (clamped to those there are), from where the pages are now. */
static void
home_page_turn(
	struct kwl_server *server,
	int target,
	const char *via)
{
	float from;

	/* The pages there are, and the page asked for among them. */
	home_layout(server);
	if (target < 0)
		target = 0;
	if (target >= (int)home_pages)
		target = (int)home_pages - 1;

	/* From where the pages are (the drag's or the turn's place) to the page. */
	from = home_page_position(server);
	server->home_page = (unsigned)target;
	server->home_page_from = from;
	server->home_page_to = (float)target;
	server->home_page_start_ms = kwl_milliseconds();
	server->home_page_moving = 1;
	server->home_page_offset = 0;
	server->dirty = 1;
	printf("KWL HOME page page=%u pages=%u via=%s\n", server->home_page + 1U, home_pages, via);
}

/*
 * Ends a press on Home: a drag mostly towards the top left closes Home, a
 * sideways drag turns to the next or the page before when it went a
 * quarter of the output (else back), and a press that did not move starts
 * the application under it when it is still there.
 */
static void
home_page_release(
	struct kwl_server *server,
	float progress)
{
	int32_t dx;
	int32_t dy;
	int closed;
	int app;

	/* The press is over. */
	server->home_page_press = 0;
	dx = server->pointer_x - server->home_page_start_x;
	dy = server->pointer_y - server->home_page_start_y;

	/* A drag up and to the left closes Home (the way it opened, backwards). */
	if (server->home_page_dragging && dy <= -HOME_CLOSE_DRAG && dx <= -HOME_CLOSE_DRAG / 2) {
		server->home_page_dragging = 0;
		server->home_page_offset = 0;
		home_close(server, progress, "drag");
		return;
	}

	/* A sideways drag turns the page, or goes back. */
	if (server->home_page_dragging) {
		server->home_page_dragging = 0;
		if (dx <= -(int32_t)server->width / 4) {
			home_page_turn(server, (int)server->home_page + 1, "drag");
		} else if (dx >= (int32_t)server->width / 4) {
			home_page_turn(server, (int)server->home_page - 1, "drag");
		} else {
			home_page_turn(server, (int)server->home_page, "drag");
		}

		/* The drag was a page turn. */
		return;
	}

	/* A click on an icon starts its application; Home closes. */
	app = home_icon_at(server, server->pointer_x, server->pointer_y);
	if (app >= 0 && app == server->home_page_app) {
		closed = home_launch(server, (unsigned)app);
		if (!closed)
			home_close(server, progress, "launch");
	}
}

/* Ends a swipe up from the bottom edge: far enough up, Home closes to the desktop; otherwise it opens again. */
static void
home_bottom_release(
	struct kwl_server *server)
{
	int32_t up;

	/* The press is over; one that never moved leaves Home as it is. */
	server->home_bottom_press = 0;
	if (!server->home_bottom_dragging)
		return;
	server->home_bottom_dragging = 0;
	server->home_dragging = 0;

	/* Far enough up: Home closes from where the swipe left it (Wiseview does not open). */
	up = server->home_bottom_start_y - server->pointer_y;
	if ((float)up >= HOME_BOTTOM_DISTANCE * HOME_BOTTOM_THRESHOLD) {
		home_close(server, server->home_drag, "bottom");
		return;
	}

	/* Not far enough: Home opens again, keeping its search. */
	printf("KWL HOME bottom back\n");
	home_settle(server, server->home_drag, 1.0f);
}

/* Draws the pages' dots at the bottom centre, the page shown in zedBSD's blue. */
static void
home_draw_dots(
	struct kwl_server *server,
	VkCommandBuffer command,
	float opacity)
{
	static const float current[4] = { 0.25f, 0.52f, 0.98f, 1.0f };
	static const float other[4] = { 1.0f, 1.0f, 1.0f, 0.32f };
	float color[4];
	float left;
	float y;
	unsigned page;

	/* A dot a page, 18 px apart, centred. */
	left = ((float)server->width - (float)(home_pages - 1U) * 18.0f) * 0.5f - 4.0f;
	y = (float)server->height - 44.0f;
	for (page = 0U; page < home_pages; page++) {
		memcpy(color, other, sizeof(color));
		if (page == server->home_page)
			memcpy(color, current, sizeof(color));
		color[3] *= opacity;
		glass_draw_solid(server, command, left + (float)page * 18.0f, y, 8.0f, 8.0f, 4.0f, color);
	}
}

/* Selects an application shown (clamped), and turns to its page when it is on another. */
static void
home_select(
	struct kwl_server *server,
	int selected)
{
	unsigned page;

	/* One of those shown. */
	home_layout(server);
	if (selected >= (int)home_shown_count)
		selected = (int)home_shown_count - 1;
	if (selected < 0)
		selected = 0;
	server->home_selected = selected;
	server->dirty = 1;

	/* Its page, when there are pages. */
	if (home_pages <= 1U)
		return;
	page = (unsigned)selected / (unsigned)HOME_PAGE;
	if (page != server->home_page)
		home_page_turn(server, (int)page, "select");
}
