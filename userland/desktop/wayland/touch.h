/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Touch screens (touch.c, WS079 p013): wl_touch for the clients and the
 * compositor's own gestures of the fingers.
 */

#ifndef ZWL_TOUCH_H
#define ZWL_TOUCH_H

#include "zwl.h"

int zwl_touch_add(struct zwl_input_device *device);
void zwl_touch_remove(struct zwl_server *server, struct zwl_input_device *device, int notify);
void zwl_touch_frame(struct zwl_server *server, struct zwl_input_device *device, uint32_t time);
void zwl_touch_tick(struct zwl_server *server);
void zwl_touch_object_gone(struct zwl_object *object);

#endif
