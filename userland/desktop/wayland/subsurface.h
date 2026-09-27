/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * wl_subcompositor and wl_subsurface (ws035-p077, subsurface.c): surfaces
 * shown with a parent surface, at a place relative to it, below or above it.
 */

#ifndef ZWL_SUBSURFACE_H
#define ZWL_SUBSURFACE_H

#include "compose.h"

int zwl_subcompositor_request(struct zwl_object *subcompositor, uint32_t opcode, const unsigned char *bytes, size_t size);
int zwl_subsurface_request(struct zwl_object *subsurface, uint32_t opcode, const unsigned char *bytes, size_t size);
int zwl_subsurface_commit(struct zwl_object *surface);
void zwl_subsurface_applied(struct zwl_object *surface);
void zwl_subsurface_object_gone(struct zwl_object *object);
void zwl_subsurface_draw(struct zwl_server *server, VkCommandBuffer command, struct zwl_object *parent, float x, float y, float scale_x, float scale_y, unsigned above);
unsigned zwl_subsurface_collect(struct zwl_server *server, struct zwl_object **surfaces, unsigned capacity);
struct zwl_object *zwl_subsurface_at(struct zwl_object *root, int32_t x, int32_t y);

/* What subsurface.c needs from protocol.c: a commit of a surface without a shell role, applied now. */
int zwl_surface_queue(struct zwl_object *surface);

#endif
