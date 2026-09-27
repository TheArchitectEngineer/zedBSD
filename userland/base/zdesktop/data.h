/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The clipboard between clients and drag and drop: wl_data_device_manager,
 * wl_data_source, wl_data_device and wl_data_offer (ws035-p079, ws035-p084,
 * data.c).
 */

#ifndef ZWL_DATA_H
#define ZWL_DATA_H

#include "zwl.h"

int zwl_data_request(struct zwl_object *object, uint32_t opcode, const unsigned char *bytes, size_t size);
void zwl_data_focus(struct zwl_server *server, struct zwl_object *focus);
void zwl_data_object_gone(struct zwl_object *object);
void zwl_data_drag_motion(struct zwl_server *server, uint32_t time);
void zwl_data_drag_release(struct zwl_server *server);
void zwl_data_drag_cancel(struct zwl_server *server);

#endif
