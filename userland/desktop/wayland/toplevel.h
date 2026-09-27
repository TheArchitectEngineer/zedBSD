/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The xdg_toplevel requests a toolkit makes of the window manager, and the
 * xdg_wm_base ping (ws035-p076, toplevel.c).
 */

#ifndef ZWL_TOPLEVEL_H
#define ZWL_TOPLEVEL_H

#include "zwl.h"

int zwl_toplevel_request(struct zwl_object *toplevel, struct zwl_object *surface, uint32_t opcode, const unsigned char *bytes, size_t size);
unsigned zwl_toplevel_resizing(const struct zwl_server *server, const struct zwl_object *surface);
void zwl_toplevel_committed(struct zwl_server *server, struct zwl_object *surface);
void zwl_toplevel_surface_gone(struct zwl_object *surface);
int zwl_toplevel_motion(struct zwl_server *server);
int zwl_toplevel_button(struct zwl_server *server, uint32_t state);
void zwl_ping_send(struct zwl_client *client);
void zwl_ping_pong(struct zwl_client *client, uint32_t serial);
void zwl_ping_check(struct zwl_server *server, uint64_t now);

#endif
