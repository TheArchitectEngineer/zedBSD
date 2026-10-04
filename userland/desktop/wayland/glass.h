/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The glass look's drawing, shared by glass.c (images, glyphs, shapes) and
 * shell.c (windows, title bars, the system bar).
 */

#ifndef ZWL_GLASS_H
#define ZWL_GLASS_H

#include "compose.h"
#include "icons.h"

/* The atlas's index of the multiplication sign (the close button). */
#define GLASS_CLOSE_GLYPH	95U

/*
 * The atlas's indices of the check mark and the single right angle quote
 * (the System Menu's checked items and submenu arrows).  A font without
 * them leaves them empty: glass_glyph_advance reports 0.
 */
#define GLASS_CHECK_GLYPH	96U
#define GLASS_ARROW_GLYPH	97U

/* The shapes the panel shader draws (shaders/panel.frag). */
#define MODE_GLASS		0.0f
#define MODE_SHADOW		1.0f
#define MODE_IMAGE		2.0f
#define MODE_SOLID		3.0f
#define MODE_RING		4.0f
#define MODE_TEXT		5.0f
#define MODE_BLUR		6.0f

/* The corner radius of title bars and bodies. */
#define GLASS_RADIUS		14.0f

/* The text sizes: the system bar, the titles, the close sign, App Home's icon letters and its search text. */
enum glass_size {
	SIZE_BAR,
	SIZE_TITLE,
	SIZE_SIGN,
	SIZE_ICON,
	SIZE_SEARCH
};

/*
 * The Kei mark's colours: the translucent glass of the boot splash (the
 * greeter and the lock screen), or the deeper and less see-through ones
 * that read on the light system bar (the launcher, ws035-p118).
 */
enum glass_mark_look {
	GLASS_MARK_SPLASH,
	GLASS_MARK_BAR
};

/*
 * One shape for the panel shader, in output pixels: the quad drawn, the
 * rounded box the shader measures from (it may reach past the quad), the
 * part of the image, a color, and how the shape is drawn.  opacity fades the
 * whole shape.  A shape with no image of its own gives the shader the
 * blurred wallpaper.
 */
struct glass_shape {
	float quad[4];
	float box[4];
	float uv[4];
	float color[4];
	float radius;
	float mode;
	float soft;
	float opaque;
	float edge;
	float opacity;
	VkDescriptorSet set;
};

void glass_shape_init(struct glass_shape *shape, float x, float y, float width, float height);
void glass_shape_draw(struct zwl_server *server, VkCommandBuffer command, const struct glass_shape *shape);
void glass_draw_solid(struct zwl_server *server, VkCommandBuffer command, float x, float y, float width, float height, float radius, const float *color);
int32_t glass_text_width(struct zwl_server *server, enum glass_size size, const char *text);
void glass_draw_text(struct zwl_server *server, VkCommandBuffer command, enum glass_size size, int32_t x, int32_t baseline, const char *text, int32_t limit, const float *color);
void glass_draw_text_middle(struct zwl_server *server, VkCommandBuffer command, enum glass_size size, int32_t x, int32_t baseline, const char *text, int32_t limit, const float *color);
void glass_draw_glyph(struct zwl_server *server, VkCommandBuffer command, enum glass_size size, unsigned index, int32_t x, int32_t baseline, const float *color);
int32_t glass_glyph_advance(struct zwl_server *server, enum glass_size size, unsigned index);
void glass_draw_icon(struct zwl_server *server, VkCommandBuffer command, unsigned icon, int32_t x, int32_t y, unsigned pixels, const float *color);
void glass_draw_mark(struct zwl_server *server, VkCommandBuffer command, int32_t x, int32_t y, unsigned pixels, enum glass_mark_look look, float opacity);
VkDescriptorSet glass_wallpaper_set(struct zwl_server *server);

/* The applications' icons in the system bar and their previews (apps-bar.c), drawn with the shell's marks and Wiseview's tiles (shell.c). */
int zwl_apps_bar_draw(struct zwl_server *server, VkCommandBuffer command);
void zwl_apps_bar_draw_popup(struct zwl_server *server, VkCommandBuffer command);
void zwl_glass_draw_app_mark(struct zwl_server *server, VkCommandBuffer command, struct zwl_object *surface, int32_t x, int32_t middle, int32_t size, float alpha);
void zwl_glass_draw_preview(struct zwl_server *server, VkCommandBuffer command, struct zwl_object *surface, int32_t x, int32_t y, int32_t width, int32_t height, int over);

/* The login screen in place of the desktop (greeter.c). */
void zwl_greeter_draw(struct zwl_server *server, VkCommandBuffer command);

/* The network's icon in the system bar and its menu (network.c). */
void zwl_network_draw_icon(struct zwl_server *server, VkCommandBuffer command, int32_t x, const float *ink);
void zwl_network_draw_menu(struct zwl_server *server, VkCommandBuffer command);
void zwl_volume_draw_icon(struct zwl_server *server, VkCommandBuffer command, int32_t x, const float *ink);
void zwl_volume_draw_popup(struct zwl_server *server, VkCommandBuffer command);

/* App Home under the desktop layer (home.c). */
void zwl_home_draw(struct zwl_server *server, VkCommandBuffer command, float progress);
void zwl_corner_draw(struct zwl_server *server, VkCommandBuffer command);

/* The on-screen keyboard over everything (keyboard.c). */
void zwl_keyboard_draw(struct zwl_server *server, VkCommandBuffer command);

#endif
