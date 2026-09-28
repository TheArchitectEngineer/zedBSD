/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The pen tablets (tablet.c, WS079 p003): zwp_tablet_manager_v2 version 1
 * and the pen as the pointer for clients and screens that do not take it.
 */

#ifndef ZWL_TABLET_H
#define ZWL_TABLET_H

#include "zwl.h"

/* The slot a tablet or tool object names once its device or tool has gone. */
#define ZWL_TABLET_SLOT_NONE	0xffffffffU

int zwl_tablet_add(struct zwl_server *server, struct zwl_input_device *device);
void zwl_tablet_remove(struct zwl_server *server, struct zwl_input_device *device, int notify);
void zwl_tablet_frame(struct zwl_server *server, struct zwl_input_device *device, uint32_t time);
int zwl_tablet_request(struct zwl_object *object, uint32_t opcode, const unsigned char *bytes, size_t size);
void zwl_tablet_object_gone(struct zwl_object *object);

#endif
