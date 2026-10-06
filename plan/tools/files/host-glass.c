/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws071-p015: lays a glass window's frame over a desktop the way zdesktop
 * does (userland/desktop/wayland/panels.c), for the host's pictures: the
 * wallpaper, the cards' shadows, each panel's frosted glass (the wallpaper
 * blurred and whitened, with a bright rim),
 * then the frame by its alpha.  The blur is a coarse stand-in for
 * zdesktop's (the wallpaper shrunk and grown again).
 */

#include "files.h"

#include <stdlib.h>
#include <string.h>

/* How much the wallpaper is shrunk for its blur. */
#define GLASS_BLUR_STEP		24

/* The glass's white, its rim, and the cards' shadow, as zdesktop draws them. */
#define GLASS_WHITE		KL_RGBA(0xffffff, 87)
#define GLASS_RIM		KL_RGBA(0xffffff, 150)
#define GLASS_SHADOW		KL_RGBA(0x1a2e59, 41)
#define GLASS_SHADOW_SOFT	16.0f
#define GLASS_SHADOW_DROP	6.0f

int host_glass_compose(struct fm_app *app, const uint32_t *frame, uint32_t *out, int width, int height, const char *wallpaper);

static int glass_desktop(const char *wallpaper, struct kl_image *desktop);
static int glass_part(const struct kl_image *source, const struct kl_rect *rect, struct kl_image *part);
static void glass_panel(struct kl_canvas *canvas, const struct kl_image *blurred, const struct fm_panel *panel);

/*
 * Draws into out (width by height) the desktop, the glass of the frame's
 * panels (fm_ui_panels) and the frame over them.  Returns 0, or an errno
 * value when a picture cannot be made.
 */
int
host_glass_compose(
	struct fm_app *app,
	const uint32_t *frame,
	uint32_t *out,
	int width,
	int height,
	const char *wallpaper)
{
	struct fm_panel panels[FM_PANELS];
	struct kl_image loaded;
	struct kl_image desktop;
	struct kl_image small;
	struct kl_image blurred;
	struct kl_image shown;
	struct kl_canvas canvas;
	const struct kl_rect *rect;
	size_t count;
	size_t index;
	int error;

	/* The wallpaper at the window's size. */
	error = glass_desktop(wallpaper, &loaded);
	if (error != 0)
		return error;
	error = kl_image_create(&desktop, width, height);
	if (error != 0) {
		kl_image_release(&loaded);
		return error;
	}

	/* The wallpaper scaled into it. */
	kl_image_scale(&loaded, &desktop);
	kl_image_release(&loaded);

	/* Its blur: shrunk, then grown back. */
	error = kl_image_create(&small, width / GLASS_BLUR_STEP + 1, height / GLASS_BLUR_STEP + 1);
	if (error != 0) {
		kl_image_release(&desktop);
		return error;
	}

	/* The blur's picture at full size. */
	error = kl_image_create(&blurred, width, height);
	if (error != 0) {
		kl_image_release(&small);
		kl_image_release(&desktop);
		return error;
	}

	/* Shrunk and grown back. */
	kl_image_scale(&desktop, &small);
	kl_image_scale(&small, &blurred);
	kl_image_release(&small);

	/* The picture: the desktop first. */
	memset(out, 0, sizeof(out[0]) * (size_t)width * (size_t)height);
	error = kl_canvas_init(&canvas, out, (size_t)width, width, height);
	if (error != 0) {
		kl_image_release(&blurred);
		kl_image_release(&desktop);
		return error;
	}

	/* The desktop at the bottom. */
	kl_canvas_image(&canvas, &desktop, 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f);

	/* The cards' shadows under every panel. */
	count = fm_ui_panels(app, panels, FM_PANELS);
	for (index = 0; index < count; index++) {
		rect = &panels[index].rect;
		if (panels[index].kind == FM_PANEL_CARD)
			kl_canvas_shadow(&canvas, (float)rect->x, (float)rect->y + GLASS_SHADOW_DROP, (float)rect->width, (float)rect->height, (float)panels[index].radius, GLASS_SHADOW_SOFT, GLASS_SHADOW);
	}

	/* Each panel's glass. */
	for (index = 0; index < count; index++)
		glass_panel(&canvas, &blurred, &panels[index]);

	/* The frame over it all, by its alpha. */
	shown.pixels = (uint32_t *)frame;
	shown.width = width;
	shown.height = height;
	shown.stride = (size_t)width;
	kl_canvas_image(&canvas, &shown, 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f);

	/* The pictures go. */
	kl_canvas_release(&canvas);
	kl_image_release(&blurred);
	kl_image_release(&desktop);

	/* Succeeded: out holds the window on the desktop. */
	return 0;
}

/* Reads the wallpaper, or makes a plain blue-green one when there is none. */
static int
glass_desktop(
	const char *wallpaper,
	struct kl_image *desktop)
{
	struct kl_canvas canvas;
	struct kl_rect whole;
	int error;

	/* The wallpaper, when it can be read. */
	error = fm_image_load(wallpaper, desktop);
	if (error == 0)
		return 0;

	/* Otherwise a gradient with a few round shapes, so that the blur shows. */
	error = kl_image_create(desktop, 320, 200);
	if (error != 0)
		return error;
	error = kl_canvas_init(&canvas, desktop->pixels, desktop->stride, desktop->width, desktop->height);
	if (error != 0) {
		kl_image_release(desktop);
		return error;
	}

	/* A sky-to-grass gradient with a sun and a bush. */
	whole.x = 0;
	whole.y = 0;
	whole.width = desktop->width;
	whole.height = desktop->height;
	kl_canvas_gradient(&canvas, &whole, KL_RGB(0x7fb4d8), KL_RGB(0x5c8f5a));
	kl_canvas_circle(&canvas, 80.0f, 60.0f, 40.0f, KL_RGB(0xe8c35a));
	kl_canvas_circle(&canvas, 240.0f, 140.0f, 50.0f, KL_RGB(0x2f5d3a));
	kl_canvas_release(&canvas);

	/* Succeeded: the stand-in. */
	return 0;
}

/* Copies a rectangle of a picture into a new picture of its own. */
static int
glass_part(
	const struct kl_image *source,
	const struct kl_rect *rect,
	struct kl_image *part)
{
	int error;
	int x;
	int y;
	int sx;
	int sy;

	/* The part's picture. */
	error = kl_image_create(part, rect->width, rect->height);
	if (error != 0)
		return error;

	/* Each pixel, from inside the source (the edge repeated past it). */
	for (y = 0; y < rect->height; y++) {
		sy = rect->y + y;
		if (sy < 0)
			sy = 0;
		if (sy >= source->height)
			sy = source->height - 1;
		for (x = 0; x < rect->width; x++) {
			sx = rect->x + x;
			if (sx < 0)
				sx = 0;
			if (sx >= source->width)
				sx = source->width - 1;
			part->pixels[(size_t)y * part->stride + (size_t)x] = source->pixels[(size_t)sy * source->stride + (size_t)sx];
		}
	}

	/* Succeeded. */
	return 0;
}

/* Draws one panel's glass: the blurred desktop in its shape, whitened, with a rim. */
static void
glass_panel(
	struct kl_canvas *canvas,
	const struct kl_image *blurred,
	const struct fm_panel *panel)
{
	struct kl_image part;
	struct kl_rect box;
	float radius;
	int error;

	/* The shape. */
	box = panel->rect;
	radius = (float)panel->radius;

	/* The blurred desktop under the shape. */
	error = glass_part(blurred, &box, &part);
	if (error == 0) {
		kl_canvas_image(canvas, &part, (float)box.x, (float)box.y, (float)box.width, (float)box.height, radius, 1.0f);
		kl_image_release(&part);
	}

	/* Whitened, with its bright rim. */
	kl_canvas_round(canvas, (float)box.x, (float)box.y, (float)box.width, (float)box.height, radius, GLASS_WHITE);
	kl_canvas_round_border(canvas, (float)box.x, (float)box.y, (float)box.width, (float)box.height, radius, 1.0f, GLASS_RIM);
}
