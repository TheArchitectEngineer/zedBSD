/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws071: drives zdesktop-files' interface on the host and draws its frames
 * into PPM pictures, without Wayland or Vulkan.
 *
 *   files-render [OPTION]... ACTION...
 *
 * Options (before the actions):
 *   --font=PATH --fallback=PATH   the fonts (default build/ws035-fonts/Inter.ttf)
 *   --size=WxH                    the window's size (default 1120x720)
 *   --start=PATH                  the folder shown first (default: the home dashboard)
 *
 * Actions, run in order (each one 150 ms after the one before):
 *   move=X,Y  click=X,Y[:MODS]  double=X,Y  right=X,Y  press=X,Y  release=X,Y  scroll=PIXELS
 *   key=CODE[:MODS]  (evdev code; MODS a sum of 1 shift, 2 ctrl, 4 alt)
 *   text=STRING      (types ASCII letters, digits, '.', '-', '_' and ' ' as keys)
 *   wait=MS          (lets time pass and runs the ticks)
 *   hits             (prints the clickable regions of the last frame)
 *   draw=PATH        (draws the frame into a PPM picture)
 *   focus=0|1
 */

#include "files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The evdev codes of the letters, digits and a few signs, for text=. */
static const char host_keys[] = "\0\0" "1234567890-=\0\0" "qwertyuiop[]\0\0" "asdfghjkl;'`\0\\" "zxcvbnm,./";

static int host_write_ppm(const char *path, const uint32_t *pixels, int width, int height);
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
	uint32_t *pixels;
	uint64_t now;
	unsigned code;
	unsigned modifiers;
	int width;
	int height;
	int index;
	int x;
	int y;

	/* The options. */
	font = "build/ws035-fonts/Inter.ttf";
	fallback = NULL;
	start = NULL;
	width = FM_WIDTH;
	height = FM_HEIGHT;
	for (index = 1; index < argc && strncmp(argv[index], "--", 2) == 0; index++) {
		if (strncmp(argv[index], "--font=", 7) == 0)
			font = argv[index] + 7;
		else if (strncmp(argv[index], "--fallback=", 11) == 0)
			fallback = argv[index] + 11;
		else if (strncmp(argv[index], "--start=", 8) == 0)
			start = argv[index] + 8;
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
		} else if (sscanf(argv[index], "right=%d,%d", &x, &y) == 2) {
			host_event(&app, FM_EVENT_BUTTON, x, y, FM_BUTTON_RIGHT, 1, 0, 0, &now);
			host_event(&app, FM_EVENT_BUTTON, x, y, FM_BUTTON_RIGHT, 0, 0, 0, &now);
		} else if (sscanf(argv[index], "press=%d,%d", &x, &y) == 2) {
			host_event(&app, FM_EVENT_BUTTON, x, y, FM_BUTTON_LEFT, 1, 0, app.modifiers, &now);
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
		} else if (strncmp(argv[index], "draw=", 5) == 0) {
			fm_ui_tick(&app, now);
			fm_ui_draw(&app, &canvas);
			if (host_write_ppm(argv[index] + 5, pixels, width, height) != 0) {
				fprintf(stderr, "files-render: cannot write %s\n", argv[index] + 5);
				return 1;
			}
			printf("drew %s\n", argv[index] + 5);
		} else {
			fprintf(stderr, "files-render: unknown action %s\n", argv[index]);
			return 2;
		}
		fm_ui_tick(&app, now);
		if (app.dirty)
			fm_ui_draw(&app, &canvas);
	}

	fm_app_release(&app);
	fm_canvas_release(&canvas);
	fm_text_close(&text);
	free(pixels);
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
