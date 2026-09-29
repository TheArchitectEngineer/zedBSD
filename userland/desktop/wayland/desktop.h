/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The desktop surface (keiland_desktop_v1, desktop.c, ws094-p002): one
 * client's surface over the wallpaper and under every window, which shows
 * the icons of ~/Desktop; shared with the drawing, the input and the
 * display pass.
 */

#ifndef ZWL_DESKTOP_H
#define ZWL_DESKTOP_H

#include "glass.h"

int zwl_desktop_option(struct zwl_server *server, const char *argument);
int zwl_desktop_request(struct zwl_object *object, uint32_t opcode, const unsigned char *bytes, size_t size);
void zwl_desktop_object_gone(struct zwl_object *object);
void zwl_desktop_tick(struct zwl_server *server);
void zwl_desktop_draw(struct zwl_server *server, VkCommandBuffer command);
struct zwl_object *zwl_desktop_surface(struct zwl_server *server);
int zwl_desktop_is(const struct zwl_object *surface);
struct zwl_object *zwl_desktop_front(struct zwl_server *server, struct zwl_object *top);
int zwl_desktop_press(struct zwl_server *server);
void zwl_desktop_unfocus(struct zwl_server *server);
struct zwl_object *zwl_desktop_at(struct zwl_server *server, int32_t x, int32_t y);

#endif
