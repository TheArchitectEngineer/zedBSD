/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The input method program's Wayland side (ws095-p004, plan/ws095/design.md
 * section 5): the connection zdesktop gave it, the input method, its
 * keyboard grab and virtual keyboard, zdesktop's status, and the language
 * engines (engine.h) it drives.
 */

#ifndef IME_PROGRAM_H
#define IME_PROGRAM_H

#include "engine.h"

#include <wayland/wayland-client.h>
#include <wayland/input-method-unstable-v2-client-protocol.h>
#include <wayland/virtual-keyboard-unstable-v1-client-protocol.h>
#include "userland/desktop/libwayland/zed-ime-status-v1-client-protocol.h"

/* The most languages the program keeps, and the evdev codes whose presses it remembers. */
#define PROGRAM_ENGINES_MAX	4U
#define PROGRAM_KEYS		768U

/*
 * The input method program's whole state.
 *
 * It lives in main's frame for the program's lifetime.  The active flags
 * follow the protocol's double buffering: an activate or deactivate is
 * pending until done.
 */
struct program {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_seat *seat;
	struct zwp_input_method_manager_v2 *method_manager;
	struct zwp_virtual_keyboard_manager_v1 *keyboard_manager;
	struct keiland_ime_status_manager_v1 *status_manager;
	struct zwp_input_method_v2 *method;
	struct zwp_input_method_keyboard_grab_v2 *grab;
	struct zwp_virtual_keyboard_v1 *keyboard;
	struct keiland_ime_status_v1 *status;
	unsigned keymap_sent;
	struct ime_engine engines[PROGRAM_ENGINES_MAX];
	unsigned engine_count;
	unsigned current;
	unsigned active;
	unsigned pending_active;
	unsigned pending_active_set;
	uint32_t pending_hint;
	uint32_t pending_purpose;
	uint32_t done_count;
	uint32_t modifiers;
	unsigned composing;
	unsigned char passed[PROGRAM_KEYS];
	unsigned unavailable;
	struct ime_output *out;
};

/* method.c */
int program_method_start(struct program *program);
void program_announce_language(struct program *program);

/* keys.c */
uint32_t program_key_character(uint32_t code, uint32_t modifiers);
uint32_t program_key_modifiers(uint32_t modifiers);

#endif
