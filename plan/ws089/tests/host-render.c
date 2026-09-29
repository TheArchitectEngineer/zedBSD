/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws089: drives Settings' interface on the host and draws its frames into
 * PPM pictures, without Wayland or Vulkan.
 *
 *   settings-render [OPTION]... ACTION...
 *
 * Options (before the actions):
 *   --font=PATH        the font (default userland/desktop/fonts/Inter.ttf)
 *   --size=WxH         the window's size (default 1180x800)
 *   --page=WORD        the page shown first (default Home)
 *   --network=SCENARIO a made-up network (host-network.c: wifi, wired, absent, down; default wifi)
 *
 * Actions, run in order:
 *   move=X,Y  click=X,Y  scroll=PIXELS  key=CODE[:MODS]  action=N
 *   text=STRING        (types lower-case letters and digits as keys)
 *   control=N          (clicks the page's control N of the last frame)
 *   tb=CONTROL:DETAIL  (a titlebar control chosen)
 *   draw=PATH          (draws the frame into a PPM picture)
 *   hits               (prints the clickable regions of the last frame)
 *   state              (prints what the titlebar and the menus show)
 */

#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void host_network_fake(struct se_app *app, const char *scenario);
static int host_write_ppm(const char *path, const uint32_t *pixels, int width, int height);
static void host_event(struct se_app *app, unsigned type, int x, int y, uint32_t button, int pressed, uint32_t key, uint32_t modifiers);

int
main(
	int argc,
	char **argv)
{
	static struct se_app app;
	static struct fm_text text;
	struct fm_canvas canvas;
	struct se_titlebar_state titlebar;
	struct se_titlebar_event event;
	struct se_event wheel;
	struct se_menu_state menu;
	const struct se_page *page;
	const char *font;
	const char *scenario;
	const char *typed;
	static const char letters[] = "qwertyuiop\0\0\0\0asdfghjkl\0\0\0\0\0zxcvbnm";
	static const char digits[] = "1234567890";
	const char *found;
	uint32_t *pixels;
	unsigned start;
	unsigned code;
	unsigned mods;
	int width;
	int height;
	int x;
	int y;
	int index;
	int error;

	/* The options. */
	font = "userland/desktop/fonts/Inter.ttf";
	width = SE_WIDTH;
	height = SE_HEIGHT;
	start = SE_PAGE_HOME;
	scenario = "wifi";
	for (index = 1; index < argc && strncmp(argv[index], "--", 2) == 0; index++) {
		if (strncmp(argv[index], "--font=", 7) == 0)
			font = argv[index] + 7;
		if (strncmp(argv[index], "--size=", 7) == 0)
			(void)sscanf(argv[index] + 7, "%dx%d", &width, &height);
		if (strncmp(argv[index], "--network=", 10) == 0)
			scenario = argv[index] + 10;
		if (strncmp(argv[index], "--page=", 7) == 0) {
			page = se_page_find(argv[index] + 7);
			if (page != NULL)
				start = page->id;
		}
	}

	/* The font, the canvas and the interface. */
	error = fm_text_open(&text, font, NULL);
	if (error != 0) {
		fprintf(stderr, "font %s: %d\n", font, error);
		return 1;
	}
	pixels = calloc((size_t)width * (size_t)height, sizeof(uint32_t));
	if (pixels == NULL)
		return 1;
	error = fm_canvas_init(&canvas, pixels, (size_t)width, width, height);
	if (error != 0)
		return 1;
	se_about_read(&app.about);
	(void)snprintf(app.about.graphics, sizeof(app.about.graphics), "%s", "Host test (no GPU)");
	(void)snprintf(app.about.display, sizeof(app.about.display), "%dx%d", width, height);
	app.now = 3723000U;
	host_network_fake(&app, scenario);
	se_ui_init(&app, &text, start);
	se_ui_draw(&app, &canvas);

	/* Each action, a frame after it. */
	for (; index < argc; index++) {
		if (sscanf(argv[index], "move=%d,%d", &x, &y) == 2) {
			host_event(&app, SE_EVENT_MOTION, x, y, 0, 0, 0, 0);
		} else if (sscanf(argv[index], "click=%d,%d", &x, &y) == 2) {
			host_event(&app, SE_EVENT_MOTION, x, y, 0, 0, 0, 0);
			host_event(&app, SE_EVENT_BUTTON, x, y, SE_BUTTON_LEFT, 1, 0, 0);
			host_event(&app, SE_EVENT_BUTTON, x, y, SE_BUTTON_LEFT, 0, 0, 0);
		} else if (sscanf(argv[index], "scroll=%d", &y) == 1) {
			memset(&wheel, 0, sizeof(wheel));
			wheel.type = SE_EVENT_AXIS;
			wheel.x = app.layout.page.x + 40;
			wheel.y = app.layout.page.y + 40;
			wheel.scroll = y;
			se_ui_event(&app, &wheel);
		} else if (sscanf(argv[index], "key=%u:%u", &code, &mods) == 2) {
			host_event(&app, SE_EVENT_KEY, 0, 0, 0, 1, code, mods);
			host_event(&app, SE_EVENT_KEY, 0, 0, 0, 0, code, mods);
		} else if (sscanf(argv[index], "key=%u", &code) == 1) {
			host_event(&app, SE_EVENT_KEY, 0, 0, 0, 1, code, 0);
			host_event(&app, SE_EVENT_KEY, 0, 0, 0, 0, code, 0);
		} else if (strncmp(argv[index], "text=", 5) == 0) {
			for (typed = argv[index] + 5; *typed != '\0'; typed++) {
				found = strchr(digits, *typed);
				code = 0;
				if (found != NULL)
					code = 2U + (unsigned)(found - digits);
				found = memchr(letters, *typed, sizeof(letters) - 1U);
				if (*typed >= 'a' && *typed <= 'z' && found != NULL)
					code = 16U + (unsigned)(found - letters);
				if (code != 0U) {
					host_event(&app, SE_EVENT_KEY, 0, 0, 0, 1, code, 0);
					host_event(&app, SE_EVENT_KEY, 0, 0, 0, 0, code, 0);
				}
			}
		} else if (sscanf(argv[index], "control=%u", &code) == 1) {
			for (x = app.hit_count - 1; x >= 0; x--) {
				if (app.hits[x].kind == SE_HIT_CONTROL && app.hits[x].index == (int)code)
					break;
			}
			if (x < 0) {
				fprintf(stderr, "no control %u\n", code);
				return 2;
			}
			y = app.hits[x].rect.y + app.hits[x].rect.height / 2;
			code = (unsigned)(app.hits[x].rect.x + app.hits[x].rect.width / 2);
			host_event(&app, SE_EVENT_MOTION, (int)code, y, 0, 0, 0, 0);
			host_event(&app, SE_EVENT_BUTTON, (int)code, y, SE_BUTTON_LEFT, 1, 0, 0);
			host_event(&app, SE_EVENT_BUTTON, (int)code, y, SE_BUTTON_LEFT, 0, 0, 0);
		} else if (sscanf(argv[index], "action=%u", &code) == 1) {
			se_ui_action(&app, code);
		} else if (sscanf(argv[index], "tb=%u:%u", &code, &mods) == 2) {
			memset(&event, 0, sizeof(event));
			event.kind = SE_TITLEBAR_ACTIVATED;
			event.id = code;
			event.detail = mods;
			se_ui_titlebar(&app, &event);
		} else if (strncmp(argv[index], "draw=", 5) == 0) {
			se_ui_draw(&app, &canvas);
			if (app.dirty != 0)
				se_ui_draw(&app, &canvas);
			error = host_write_ppm(argv[index] + 5, pixels, width, height);
			if (error != 0)
				return 1;
		} else if (strcmp(argv[index], "hits") == 0) {
			for (x = 0; x < app.hit_count; x++)
				printf("HIT kind=%u index=%d x=%d y=%d w=%d h=%d\n", app.hits[x].kind, app.hits[x].index, app.hits[x].rect.x, app.hits[x].rect.y, app.hits[x].rect.width, app.hits[x].rect.height);
		} else if (strcmp(argv[index], "state") == 0) {
			se_ui_titlebar_state(&app, &titlebar);
			se_ui_menu_state(&app, &menu);
			printf("STATE page=%s back=%d forward=%d parts=%d last=%s sidebar=%d menu-page=%u\n", se_pages[app.page].word, titlebar.can_back, titlebar.can_forward, titlebar.part_count, titlebar.parts[titlebar.part_count - 1], titlebar.sidebar, menu.page);
		} else {
			fprintf(stderr, "unknown action %s\n", argv[index]);
			return 2;
		}
		se_ui_draw(&app, &canvas);
	}

	/* Done. */
	fm_canvas_release(&canvas);
	free(pixels);
	fm_text_close(&text);
	return 0;
}

/* Hands one input to the interface. */
static void
host_event(
	struct se_app *app,
	unsigned type,
	int x,
	int y,
	uint32_t button,
	int pressed,
	uint32_t key,
	uint32_t modifiers)
{
	struct se_event event;

	/* The input. */
	memset(&event, 0, sizeof(event));
	event.type = type;
	event.x = x;
	event.y = y;
	event.button = button;
	event.pressed = pressed;
	event.key = key;
	event.modifiers = modifiers;
	event.time = app->now;
	se_ui_event(app, &event);
}

/* Writes premultiplied pixels over white as a binary PPM; nonzero on failure. */
static int
host_write_ppm(
	const char *path,
	const uint32_t *pixels,
	int width,
	int height)
{
	FILE *file;
	uint32_t pixel;
	unsigned alpha;
	unsigned char rgb[3];
	int index;
	int channel;

	/* The file. */
	file = fopen(path, "wb");
	if (file == NULL)
		return -1;
	fprintf(file, "P6\n%d %d\n255\n", width, height);

	/* Each pixel over a pale ground (premultiplied: colour + ground * (1 - alpha)). */
	for (index = 0; index < width * height; index++) {
		pixel = pixels[index];
		alpha = pixel >> 24;
		for (channel = 0; channel < 3; channel++)
			rgb[channel] = (unsigned char)(((pixel >> (16 - 8 * channel)) & 0xffU) + (0xd8U * (255U - alpha)) / 255U);
		fwrite(rgb, 1, 3, file);
	}

	/* Done. */
	fclose(file);
	return 0;
}
