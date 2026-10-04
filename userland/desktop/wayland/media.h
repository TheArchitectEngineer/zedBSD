/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The removable media (media.c, ws132-p004): the volumes libkeiland-backend
 * follows (volumed on zedBSD), for the system extension's devices objects
 * (system.c), and the bar's media icon that stands in for a notification
 * until WS156: shown while a volume inserted is not mounted yet, blinking
 * three times when one comes, and starting Files on its devices when
 * clicked (the 2026-10-05 decision, option A).
 */

#ifndef ZWL_MEDIA_H
#define ZWL_MEDIA_H

#include "zwl.h"

#include "userland/desktop/libkeiland-backend/keiland-backend.h"

#include <vulkan/vulkan.h>

/* What zwl_media_tick found changed (the backend's KL_BACKEND_VOLUMES_CHANGED_* bits). */
unsigned zwl_media_tick(struct zwl_server *server);
size_t zwl_media_volumes(struct kl_backend_volume *list, size_t capacity);
int zwl_media_ask(int mount, const char *id, uint32_t *request);
int zwl_media_take_result(uint32_t *request, int *error, char *user, size_t size);
int32_t zwl_media_width(void);
void zwl_media_draw_icon(struct zwl_server *server, VkCommandBuffer command, int32_t x, const float *ink);
int zwl_media_button(struct zwl_server *server, uint32_t button, uint32_t state);

#endif
