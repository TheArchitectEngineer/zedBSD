/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Declares zdesktop's input method status protocol (keiland_ime_status_v1,
 * version 1; ws095-p004, plan/ws095/design.md section 8): the input method
 * tells zdesktop its language and whether text is being composed, and
 * zdesktop tells it to change language.  The protocol is zdesktop's own and
 * this header is private to the tree.
 */

#ifndef ZED_IME_STATUS_V1_CLIENT_PROTOCOL_H
#define ZED_IME_STATUS_V1_CLIENT_PROTOCOL_H

#include <wayland/wayland-client-core.h>
#include <wayland/wayland-client-protocol.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The objects the protocol names; only libwayland knows what is in them. */
struct keiland_ime_status_v1;
struct keiland_ime_status_manager_v1;

/* The interfaces' descriptions (userland/desktop/libwayland/ime-status-protocol.c). */
extern const struct wl_interface keiland_ime_status_v1_interface;
extern const struct wl_interface keiland_ime_status_manager_v1_interface;

/* keiland_ime_status_v1: the input method's status. */
struct keiland_ime_status_v1_listener {
	void (*next)(void *data, struct keiland_ime_status_v1 *keiland_ime_status_v1);
	void (*select)(void *data, struct keiland_ime_status_v1 *keiland_ime_status_v1, const char *id);
};
int keiland_ime_status_v1_add_listener(struct keiland_ime_status_v1 *keiland_ime_status_v1, const struct keiland_ime_status_v1_listener *listener, void *data);
#define KEILAND_IME_STATUS_V1_DESTROY 0U
#define KEILAND_IME_STATUS_V1_LANGUAGE 1U
#define KEILAND_IME_STATUS_V1_COMPOSING 2U
void keiland_ime_status_v1_destroy(struct keiland_ime_status_v1 *keiland_ime_status_v1);
void keiland_ime_status_v1_language(struct keiland_ime_status_v1 *keiland_ime_status_v1, const char *id, const char *label);
void keiland_ime_status_v1_composing(struct keiland_ime_status_v1 *keiland_ime_status_v1, uint32_t composing);

/* keiland_ime_status_manager_v1: the global that gives the input method its status. */
#define KEILAND_IME_STATUS_MANAGER_V1_DESTROY 0U
#define KEILAND_IME_STATUS_MANAGER_V1_GET_STATUS 1U
void keiland_ime_status_manager_v1_destroy(struct keiland_ime_status_manager_v1 *keiland_ime_status_manager_v1);
struct keiland_ime_status_v1 *keiland_ime_status_manager_v1_get_status(struct keiland_ime_status_manager_v1 *keiland_ime_status_manager_v1);

#ifdef __cplusplus
}
#endif

#endif
