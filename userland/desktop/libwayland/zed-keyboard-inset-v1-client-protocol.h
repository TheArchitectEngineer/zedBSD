/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Declares zdesktop's keyboard inset protocol (keiland_keyboard_inset_v1,
 * ws102-p015, plan/ws102/design.md section 2.8).
 *
 * The header is private: it is not installed, and applications reach the
 * protocol through libkeiland (<keiland.h>) only.  libkeiland includes it
 * by its path in the tree.
 */

#ifndef KERN_KEILAND_KEYBOARD_INSET_V1_CLIENT_PROTOCOL_H
#define KERN_KEILAND_KEYBOARD_INSET_V1_CLIENT_PROTOCOL_H

#include <wayland/wayland-client-core.h>
#include <wayland/wayland-client-protocol.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The objects the protocol names; only libwayland knows what is in them. */
struct xdg_toplevel;
struct keiland_keyboard_inset_manager_v1;
struct keiland_keyboard_inset_v1;

/* The two interfaces' descriptions (keyboard-inset-protocol.c). */
extern const struct wl_interface keiland_keyboard_inset_manager_v1_interface;
extern const struct wl_interface keiland_keyboard_inset_v1_interface;

/* keiland_keyboard_inset_manager_v1: the global that gives windows their inset. */
#define KEILAND_KEYBOARD_INSET_MANAGER_V1_DESTROY 0U
#define KEILAND_KEYBOARD_INSET_MANAGER_V1_GET_INSET 1U
void keiland_keyboard_inset_manager_v1_destroy(struct keiland_keyboard_inset_manager_v1 *object);
struct keiland_keyboard_inset_v1 *keiland_keyboard_inset_manager_v1_get_inset(struct keiland_keyboard_inset_manager_v1 *object, struct xdg_toplevel *toplevel);

/*
 * keiland_keyboard_inset_v1: how much of one window the on-screen keyboard
 * covers, in the window's pixels from its right and bottom edges, sent when
 * the keyboard opens, closes or changes the window (before that configure).
 */
#define KEILAND_KEYBOARD_INSET_V1_REASON_NONE 0U
#define KEILAND_KEYBOARD_INSET_V1_REASON_RIGHT 1U
#define KEILAND_KEYBOARD_INSET_V1_REASON_BOTTOM 2U
struct keiland_keyboard_inset_v1_listener {
	void (*inset)(void *data, struct keiland_keyboard_inset_v1 *object, int32_t right, int32_t bottom, uint32_t reason);
};
int keiland_keyboard_inset_v1_add_listener(struct keiland_keyboard_inset_v1 *object, const struct keiland_keyboard_inset_v1_listener *listener, void *data);
#define KEILAND_KEYBOARD_INSET_V1_DESTROY 0U
void keiland_keyboard_inset_v1_destroy(struct keiland_keyboard_inset_v1 *object);

#ifdef __cplusplus
}
#endif

#endif
