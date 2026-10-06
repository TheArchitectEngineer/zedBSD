/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws071: drives files' interface on the host and draws its frames
 * into PPM pictures, without Wayland or Vulkan.
 *
 *   files-render [OPTION]... ACTION...
 *
 * Options (before the actions):
 *   --font=PATH --fallback=PATH   the fonts (default userland/desktop/fonts/Mahora-Regular.ttf, the tree's)
 *   --size=WxH                    the window's size (default 1120x720)
 *   --start=PATH                  the folder shown first (default: the home dashboard)
 *   --wallpaper=PATH              the dashboard's picture (default /usr/share/keiland/wallpaper.png)
 *   --glass=PATH                  a glass window (ws071-p015): the pictures show it on this
 *                                 wallpaper with zdesktop's glass under its panels (host-glass.c)
 *
 * Actions, run in order (each one 150 ms after the one before):
 *   move=X,Y  click=X,Y[:MODS]  double=X,Y  right=X,Y  press=X,Y  release=X,Y  scroll=PIXELS
 *   key=CODE[:MODS]  (evdev code; MODS a sum of 1 shift, 2 ctrl, 4 alt)
 *   text=STRING      (types ASCII letters, digits, '.', '-', '_' and ' ' as keys)
 *   wait=MS          (lets time pass and runs the ticks)
 *   hits             (prints the clickable regions of the last frame)
 *   draw=PATH        (draws the frame into a PPM picture)
 *   peek=PATH        (writes the frame the last action drew, without drawing again: a part drawn alone)
 *   focus=0|1
 *   action=N         (a menu's action, fm_ui_action; a request for the window is printed)
 *   state            (prints what the menus show, fm_ui_menu_state)
 *   context          (prints the context menu of the last right press, fm_ui_context)
 *   titlebar         (prints what the titlebar shows, fm_ui_titlebar_state)
 *   tb=activated:ID:DETAIL  tb=changed:ID:TEXT  tb=done:ID:HOW:TEXT
 *                    (what zdesktop's titlebar tells the window, fm_ui_titlebar)
 */

#include "files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The evdev codes of the letters, digits and a few signs, for text=. */
static const char host_keys[] = "\0\0" "1234567890-=\0\0" "qwertyuiop[]\0\0" "asdfghjkl;'`\0\\" "zxcvbnm,./";

static int host_write_ppm(const char *path, const uint32_t *pixels, int width, int height);
int host_glass_compose(struct fm_app *app, const uint32_t *frame, uint32_t *out, int width, int height, const char *wallpaper);
static void host_context(struct fm_app *app);
static int host_picture(struct fm_app *app, const char *path, const uint32_t *pixels, uint32_t *composed, int width, int height, const char *glass);
static void host_event(struct fm_app *app, unsigned type, int x, int y, uint32_t button, int pressed, uint32_t key, uint32_t modifiers, uint64_t *now);
static void host_type(struct fm_app *app, const char *text, uint64_t *now);

int
main(
	int argc,
	char **argv)
{
	static struct fm_app app;
	struct fm_canvas canvas;
	struct fm_text text;
	struct fm_event event;
	const char *font;
	const char *fallback;
	const char *start;
	const char *wallpaper;
	const char *glass;
	uint32_t *pixels;
	uint32_t *composed;
	uint64_t now;
	unsigned code;
	unsigned modifiers;
	int width;
	int height;
	int index;
	int x;
	int y;

	/* The options. */
	font = "userland/desktop/fonts/Mahora-Regular.ttf";
	fallback = NULL;
	start = NULL;
	wallpaper = NULL;
	glass = NULL;
	width = FM_WIDTH;
	height = FM_HEIGHT;
	for (index = 1; index < argc && strncmp(argv[index], "--", 2) == 0; index++) {
		if (strncmp(argv[index], "--font=", 7) == 0)
			font = argv[index] + 7;
		else if (strncmp(argv[index], "--fallback=", 11) == 0)
			fallback = argv[index] + 11;
		else if (strncmp(argv[index], "--start=", 8) == 0)
			start = argv[index] + 8;
		else if (strncmp(argv[index], "--wallpaper=", 12) == 0)
			wallpaper = argv[index] + 12;
		else if (strncmp(argv[index], "--glass=", 8) == 0)
			glass = argv[index] + 8;
		else if (sscanf(argv[index], "--size=%dx%d", &width, &height) != 2) {
			fprintf(stderr, "files-render: unknown option %s\n", argv[index]);
			return 2;
		}
	}

	/* The fonts, the canvas and the app. */
	if (fm_text_open(&text, font, fallback) != 0) {
		fprintf(stderr, "files-render: cannot open %s\n", font);
		return 1;
	}
	pixels = calloc((size_t)width * (size_t)height, sizeof(uint32_t));
	if (pixels == NULL || fm_canvas_init(&canvas, pixels, (size_t)width, width, height) != 0)
		return 1;
	now = 1000;
	app.now = now;
	if (fm_app_init(&app, &text, start) != 0)
		return 1;
	app.width = width;
	app.height = height;
	if (wallpaper != NULL)
		snprintf(app.wallpaper, sizeof(app.wallpaper), "%s", wallpaper);
	if (glass != NULL)
		app.glass = 1;
	composed = calloc((size_t)width * (size_t)height, sizeof(uint32_t));
	if (composed == NULL)
		return 1;
	fm_ui_draw(&app, &canvas);

	/* The actions. */
	for (; index < argc; index++) {
		now += 150;
		if (sscanf(argv[index], "move=%d,%d", &x, &y) == 2) {
			host_event(&app, FM_EVENT_MOTION, x, y, 0, 0, 0, 0, &now);
		} else if (sscanf(argv[index], "click=%d,%d:%u", &x, &y, &modifiers) == 3) {
			host_event(&app, FM_EVENT_BUTTON, x, y, FM_BUTTON_LEFT, 1, 0, modifiers, &now);
			host_event(&app, FM_EVENT_BUTTON, x, y, FM_BUTTON_LEFT, 0, 0, modifiers, &now);
		} else if (sscanf(argv[index], "click=%d,%d", &x, &y) == 2) {
			host_event(&app, FM_EVENT_BUTTON, x, y, FM_BUTTON_LEFT, 1, 0, 0, &now);
			host_event(&app, FM_EVENT_BUTTON, x, y, FM_BUTTON_LEFT, 0, 0, 0, &now);
		} else if (sscanf(argv[index], "double=%d,%d", &x, &y) == 2) {
			host_event(&app, FM_EVENT_BUTTON, x, y, FM_BUTTON_LEFT, 1, 0, 0, &now);
			host_event(&app, FM_EVENT_BUTTON, x, y, FM_BUTTON_LEFT, 0, 0, 0, &now);
			host_event(&app, FM_EVENT_BUTTON, x, y, FM_BUTTON_LEFT, 1, 0, 0, &now);
			host_event(&app, FM_EVENT_BUTTON, x, y, FM_BUTTON_LEFT, 0, 0, 0, &now);
		} else if (sscanf(argv[index], "middle=%d,%d", &x, &y) == 2) {
			host_event(&app, FM_EVENT_BUTTON, x, y, FM_BUTTON_MIDDLE, 1, 0, 0, &now);
			host_event(&app, FM_EVENT_BUTTON, x, y, FM_BUTTON_MIDDLE, 0, 0, 0, &now);
		} else if (strcmp(argv[index], "tabs") == 0) {
			printf("tabs count=%d shown=%d", app.tab_count, app.tab_index);
			for (x = 0; x < app.tab_count; x++)
				printf(" %d=%s", x, app.tabs[x]->history[app.tabs[x]->history_index].location.path);
			printf("\n");
		} else if (sscanf(argv[index], "right=%d,%d", &x, &y) == 2) {
			host_event(&app, FM_EVENT_BUTTON, x, y, FM_BUTTON_RIGHT, 1, 0, 0, &now);
			host_event(&app, FM_EVENT_BUTTON, x, y, FM_BUTTON_RIGHT, 0, 0, 0, &now);
		} else if (sscanf(argv[index], "press=%d,%d", &x, &y) == 2) {
			host_event(&app, FM_EVENT_BUTTON, x, y, FM_BUTTON_LEFT, 1, 0, app.modifiers, &now);
		} else if (sscanf(argv[index], "mods=%u", &modifiers) == 1) {
			app.modifiers = modifiers;
		} else if (sscanf(argv[index], "drag=%d,%d", &x, &y) == 2) {
			host_event(&app, FM_EVENT_MOTION, x, y, 0, 0, 0, app.modifiers, &now);
		} else if (strcmp(argv[index], "items") == 0) {
			for (x = 0; (size_t)x < fm_ui_tab(&app)->listing.count; x++)
				printf("item %d %s selected=%d\n", x, fm_ui_tab(&app)->listing.entries[x].name, fm_ui_tab(&app)->listing.entries[x].selected);
		} else if (sscanf(argv[index], "release=%d,%d", &x, &y) == 2) {
			host_event(&app, FM_EVENT_MOTION, x, y, 0, 0, 0, app.modifiers, &now);
			host_event(&app, FM_EVENT_BUTTON, x, y, FM_BUTTON_LEFT, 0, 0, app.modifiers, &now);
		} else if (sscanf(argv[index], "scroll=%d", &x) == 1) {
			memset(&event, 0, sizeof(event));
			event.type = FM_EVENT_AXIS;
			event.x = app.pointer_x;
			event.y = app.pointer_y;
			event.scroll = x;
			event.time = now;
			fm_ui_event(&app, &event);
		} else if (sscanf(argv[index], "key=%u:%u", &code, &modifiers) == 2 || sscanf(argv[index], "key=%u", &code) == 1) {
			if (strchr(argv[index], ':') == NULL)
				modifiers = 0;
			host_event(&app, FM_EVENT_KEY, 0, 0, 0, 1, code, modifiers, &now);
			host_event(&app, FM_EVENT_KEY, 0, 0, 0, 0, code, modifiers, &now);
		} else if (sscanf(argv[index], "action=%u", &code) == 1) {
			fm_ui_action(&app, code);
			if (app.request != FM_REQUEST_NONE)
				printf("request %u\n", app.request);
			app.request = FM_REQUEST_NONE;
		} else if (strcmp(argv[index], "state") == 0) {
			struct fm_menu_state state;
			fm_ui_menu_state(&app, &state);
			printf("state selection=%d folder=%d trash=%d field=%d paste=%d undo=%d redo=%d back=%d forward=%d enclose=%d view=%u sort=%u columns=%u sidebar=%d preview=%d hidden=%d openers=%d first=%s tabs=%d\n",
			    state.selection, state.folder, state.trash, state.field, state.can_paste, state.can_undo, state.can_redo, state.can_back, state.can_forward, state.can_enclose,
			    state.view, state.sort, state.columns, state.sidebar, state.preview, state.hidden, state.opener_count, state.opener_count > 0 ? state.openers[0] : "-", state.tabs);
		} else if (strcmp(argv[index], "context") == 0) {
			host_context(&app);
		} else if (strcmp(argv[index], "titlebar") == 0) {
			static struct fm_titlebar_state bar;
			fm_ui_titlebar_state(&app, &bar);
			printf("titlebar back=%d forward=%d parts=%d", bar.can_back, bar.can_forward, bar.part_count);
			for (x = 0; x < bar.part_count; x++)
				printf("%s%s", x == 0 ? " path=" : "|", bar.parts[x]);
			printf(" field=%s query=%s view=%u preview=%d progress=%d focus=%u serial=%u\n", bar.path, bar.query, bar.view, bar.preview, bar.progress, bar.focus, bar.focus_serial);
		} else if (strcmp(argv[index], "suggestions") == 0) {
			static struct fm_titlebar_state shown;
			fm_ui_titlebar_state(&app, &shown);
			printf("suggest serial=%u count=%d\n", shown.suggest_serial, shown.suggest_count);
			for (x = 0; x < shown.suggest_count; x++)
				printf("suggest label=%s text=%s\n", shown.suggest_labels[x], shown.suggest_texts[x]);
		} else if (strncmp(argv[index], "tb=", 3) == 0) {
			static struct fm_titlebar_event told;
			const char *rest;
			unsigned id;
			unsigned detail;
			int used;
			memset(&told, 0, sizeof(told));
			used = 0;
			detail = 0;
			rest = argv[index] + 3;
			if (sscanf(rest, "activated:%u:%u", &id, &detail) == 2) {
				told.kind = FM_TITLEBAR_ACTIVATED;
			} else if (sscanf(rest, "changed:%u:%n", &id, &used) == 1 && used > 0) {
				told.kind = FM_TITLEBAR_CHANGED;
				snprintf(told.text, sizeof(told.text), "%s", rest + used);
			} else if (sscanf(rest, "done:%u:%u:%n", &id, &detail, &used) == 2 && used > 0) {
				told.kind = FM_TITLEBAR_DONE;
				snprintf(told.text, sizeof(told.text), "%s", rest + used);
			} else {
				fprintf(stderr, "files-render: bad %s\n", argv[index]);
				return 2;
			}
			told.id = id;
			told.detail = detail;
			app.now = now;
			fm_ui_titlebar(&app, &told);
		} else if (strcmp(argv[index], "hits") == 0) {
			for (x = 0; x < app.hit_count; x++)
				printf("hit kind=%u index=%d x=%d y=%d width=%d height=%d\n", app.hits[x].kind, app.hits[x].index, app.hits[x].rect.x, app.hits[x].rect.y, app.hits[x].rect.width, app.hits[x].rect.height);
		} else if (strncmp(argv[index], "text=", 5) == 0) {
			host_type(&app, argv[index] + 5, &now);
		} else if (sscanf(argv[index], "wait=%d", &x) == 1) {
			now += (uint64_t)x;
			fm_ui_tick(&app, now);
		} else if (sscanf(argv[index], "focus=%d", &x) == 1) {
			memset(&event, 0, sizeof(event));
			event.type = FM_EVENT_FOCUS;
			event.focused = x;
			event.time = now;
			fm_ui_event(&app, &event);
		} else if (strncmp(argv[index], "peek=", 5) == 0) {
			/* The frame as the last action left it, not drawn again (BUG-221, BUG-226: a part drawn alone). */
			if (host_picture(&app, argv[index] + 5, pixels, composed, width, height, glass) != 0) {
				fprintf(stderr, "files-render: cannot write %s\n", argv[index] + 5);
				return 1;
			}
			printf("peeked %s\n", argv[index] + 5);
		} else if (strncmp(argv[index], "draw=", 5) == 0) {
			fm_ui_tick(&app, now);
			fm_ui_draw(&app, &canvas);
			if (host_picture(&app, argv[index] + 5, pixels, composed, width, height, glass) != 0) {
				fprintf(stderr, "files-render: cannot write %s\n", argv[index] + 5);
				return 1;
			}
			printf("drew %s\n", argv[index] + 5);
		} else {
			fprintf(stderr, "files-render: unknown action %s\n", argv[index]);
			return 2;
		}
		fm_ui_tick(&app, now);
		if (app.dirty || app.damage_pending)
			fm_ui_draw(&app, &canvas);
	}

	fm_app_release(&app);
	fm_canvas_release(&canvas);
	fm_text_close(&text);
	free(pixels);
	free(composed);
	return 0;
}

static void
host_event(
	struct fm_app *app,
	unsigned type,
	int x,
	int y,
	uint32_t button,
	int pressed,
	uint32_t key,
	uint32_t modifiers,
	uint64_t *now)
{
	struct fm_event event;

	memset(&event, 0, sizeof(event));
	event.type = type;
	event.x = x;
	event.y = y;
	event.button = button;
	event.pressed = pressed;
	event.key = key;
	event.modifiers = modifiers;
	event.serial = 1;
	event.time = *now;
	*now += 40;
	fm_ui_event(app, &event);
	if (app->dirty) {
		fm_ui_tick(app, *now);
	}
}

static void
host_type(
	struct fm_app *app,
	const char *text,
	uint64_t *now)
{
	unsigned code;
	unsigned modifiers;
	char character;

	for (; *text != '\0'; text++) {
		character = *text;
		modifiers = 0;
		if (character >= 'A' && character <= 'Z') {
			character = (char)(character - 'A' + 'a');
			modifiers = FM_MOD_SHIFT;
		}
		if (character == '_') {
			character = '-';
			modifiers = FM_MOD_SHIFT;
		}
		if (character == '~') {
			character = '`';
			modifiers = FM_MOD_SHIFT;
		}
		code = 57;
		if (character != ' ') {
			for (code = 0; code < sizeof(host_keys) - 1; code++) {
				if (host_keys[code] == character)
					break;
			}
			if (code == sizeof(host_keys) - 1)
				continue;
		}
		host_event(app, FM_EVENT_KEY, 0, 0, 0, 1, code, modifiers, now);
		host_event(app, FM_EVENT_KEY, 0, 0, 0, 0, code, modifiers, now);
	}
}

/*
 * Prints the context menu the last right press asked for (ws071-p009):
 * the request, where it was, and each row (number, parent, kind, whether
 * it can be chosen and is checked, action, label); the request is taken.
 */
static void
host_context(
	struct fm_app *app)
{
	static struct fm_context context;
	const struct fm_context_row *row;
	unsigned index;

	/* The rows of the press's menu. */
	fm_ui_context(app, &context);
	printf("context request=%u where=%u place=%d x=%d y=%d count=%u\n", app->request, app->context_where, app->context_place, app->context_x, app->context_y, context.count);
	app->request = FM_REQUEST_NONE;

	/* Each row on a line. */
	for (index = 0; index < context.count; index++) {
		row = &context.rows[index];
		printf("row id=%u parent=%u kind=%u enabled=%d checked=%d action=%u label=%s\n", row->id, row->parent, row->kind, row->enabled, row->checked, row->action, row->label);
	}
}

/*
 * Writes the frame as a picture: as it is, or laid on the wallpaper with
 * zdesktop's glass under its panels for a glass window (glass is the
 * wallpaper's path, NULL for none).  Returns 0, or -1 when it cannot.
 */
static int
host_picture(
	struct fm_app *app,
	const char *path,
	const uint32_t *pixels,
	uint32_t *composed,
	int width,
	int height,
	const char *glass)
{
	int error;

	/* The frame itself, or the frame on the desktop. */
	memcpy(composed, pixels, sizeof(pixels[0]) * (size_t)width * (size_t)height);
	if (glass != NULL) {
		error = host_glass_compose(app, pixels, composed, width, height, glass);
		if (error != 0)
			return -1;
	}

	/* The picture's file. */
	error = host_write_ppm(path, composed, width, height);
	if (error != 0)
		return -1;

	/* Succeeded. */
	return 0;
}

static int
host_write_ppm(
	const char *path,
	const uint32_t *pixels,
	int width,
	int height)
{
	FILE *file;
	int x;
	int y;

	file = fopen(path, "wb");
	if (file == NULL)
		return -1;
	fprintf(file, "P6\n%d %d\n255\n", width, height);
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			fputc((int)((pixels[(size_t)y * width + x] >> 16) & 0xff), file);
			fputc((int)((pixels[(size_t)y * width + x] >> 8) & 0xff), file);
			fputc((int)(pixels[(size_t)y * width + x] & 0xff), file);
		}
	}
	fclose(file);
	return 0;
}
