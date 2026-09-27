/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * xdg_positioner and xdg_popup (ws035-p076): popups placed by a positioner
 * relative to their parent, drawn over the windows, and the seat's grab a
 * popup menu takes (popup.c).
 */

#ifndef ZWL_POPUP_H
#define ZWL_POPUP_H

#include "compose.h"

/*
 * The rules of one xdg_positioner: the popup's size, the anchor rectangle
 * in the parent's window geometry, the anchor edge, the gravity, the
 * adjustments allowed when the popup would leave the output, and the offset.
 * A positioner is complete once its size and its anchor rectangle are set.
 */
struct zwl_positioner {
	int32_t width;
	int32_t height;
	int32_t anchor_x;
	int32_t anchor_y;
	int32_t anchor_width;
	int32_t anchor_height;
	uint32_t anchor;
	uint32_t gravity;
	uint32_t constraint;
	int32_t offset_x;
	int32_t offset_y;
	unsigned reactive;
	unsigned size_set;
	unsigned anchor_set;
};

int zwl_positioner_create(struct zwl_object *wm, uint32_t id);
int zwl_popup_create(struct zwl_object *role, uint32_t id, uint32_t parent_id, uint32_t positioner_id);
int zwl_popup_request(struct zwl_object *object, uint32_t opcode, const unsigned char *bytes, size_t size);
int zwl_popup_send_configure(struct zwl_object *surface);
void zwl_popup_mapped(struct zwl_server *server, struct zwl_object *surface);
void zwl_popup_object_gone(struct zwl_object *object);
int zwl_popup_origin(struct zwl_server *server, struct zwl_object *surface, int32_t *x, int32_t *y);
unsigned zwl_popup_collect(struct zwl_server *server, struct zwl_object **popups, unsigned capacity);
void zwl_popup_draw(struct zwl_server *server, VkCommandBuffer command);
struct zwl_object *zwl_popup_focus(struct zwl_server *server, struct zwl_object *target);
struct zwl_object *zwl_popup_chain_at(struct zwl_server *server);
int zwl_popup_button(struct zwl_server *server, uint32_t button, uint32_t state);

/* What popup.c needs from the compositor and the shell. */
void zwl_compose_surface_quad(struct zwl_server *server, VkCommandBuffer command, const struct zwl_object *surface, const struct zwl_import *import, int32_t x, int32_t y);
int zwl_glass_body_origin(struct zwl_server *server, struct zwl_object *surface, int32_t *x, int32_t *y);
struct zwl_object *zwl_glass_body_at(struct zwl_server *server, int32_t x, int32_t y);
void zwl_seat_pointer_move(struct zwl_server *server, struct zwl_object *from, struct zwl_object *to);
void zwl_seat_pointer_update(struct zwl_server *server);

#endif
