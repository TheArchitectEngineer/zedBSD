/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The System Menu (WS070, plan/ws070/design.md): the menu models clients
 * give with xdg_menu_v1 (menu.c), and the menus zdesktop draws and operates
 * in the title bars and the system bar (menu-shell.c).
 */

#ifndef ZWL_MENU_H
#define ZWL_MENU_H

#include "glass.h"

/* The item types of xdg_menu_v1. */
#define ZWL_MENU_NORMAL		0U
#define ZWL_MENU_SEPARATOR	1U
#define ZWL_MENU_CHECKBOX	2U
#define ZWL_MENU_RADIO		3U
#define ZWL_MENU_SUBMENU	4U

/* The modifier bits of a shortcut (xdg_menu_v1.modifier). */
#define ZWL_MENU_SHIFT		1U
#define ZWL_MENU_CTRL		2U
#define ZWL_MENU_ALT		4U
#define ZWL_MENU_SUPER		8U

/* The parent of the top-level items. */
#define ZWL_MENU_ROOT		0U

/*
 * One item of a menu model, as the client last committed it.
 *
 * The label and the icon name are owned by the item (never NULL, possibly
 * empty).  A child follows its parent's other children in the model's
 * array order.
 */
struct zwl_menu_item {
	uint32_t id;
	uint32_t parent;
	uint32_t type;
	uint32_t action;
	uint32_t enabled;
	uint32_t visible;
	uint32_t checked;
	uint32_t role;
	uint32_t modifiers;
	uint32_t keysym;
	char *label;
	char *icon_name;
};

/*
 * The model of one xdg_menu_v1 object.
 *
 * items is what is shown.  Between begin_update and commit the client's
 * changes go to pending, a deep copy made at begin_update; commit makes it
 * the shown model at once and moves generation on.  The model lives as
 * long as its xdg_menu_v1 object.
 */
struct zwl_menu_model {
	struct zwl_menu_item *items;
	unsigned count;
	unsigned capacity;
	struct zwl_menu_item *pending;
	unsigned pending_count;
	unsigned pending_capacity;
	unsigned updating;
	uint32_t update_serial;
	uint64_t generation;
};

/*
 * Where a window's top-level items go in a title bar or the system bar: the
 * first item's left edge, the bar's top and height, the right edge the items
 * must end before, and the bar's left edge (the log gives the items'
 * places from it).
 */
struct zwl_menu_area {
	int32_t x;
	int32_t top;
	int32_t right;
	int32_t height;
	int32_t origin;
};

/* The model and the protocol (menu.c). */
int zwl_menu_request(struct zwl_object *object, uint32_t opcode, const unsigned char *bytes, size_t size);
void zwl_menu_object_gone(struct zwl_object *object);
struct zwl_menu_model *zwl_menu_of_surface(struct zwl_object *surface, struct zwl_object **place);
const struct zwl_menu_item *zwl_menu_item(const struct zwl_menu_model *model, uint32_t id);
unsigned zwl_menu_children(const struct zwl_menu_model *model, uint32_t parent, const struct zwl_menu_item **children, unsigned capacity);
void zwl_menu_send_activated(struct zwl_object *place, const struct zwl_menu_item *item, const char *via);
void zwl_menu_send_popup(struct zwl_object *place, uint32_t item, unsigned opened);

/* The menus zdesktop draws and operates (menu-shell.c). */
void zwl_menu_frame(struct zwl_server *server);
int32_t zwl_menu_title_limit(struct zwl_server *server, struct zwl_object *surface, int32_t available);
void zwl_menu_draw_bar(struct zwl_server *server, VkCommandBuffer command, struct zwl_object *surface, unsigned docked, const struct zwl_menu_area *area, const float *ink, float fade);
void zwl_menu_draw_popups(struct zwl_server *server, VkCommandBuffer command);
int zwl_menu_button(struct zwl_server *server, uint32_t button, uint32_t state);
int zwl_menu_motion(struct zwl_server *server);
int zwl_menu_grab_key(struct zwl_server *server, uint32_t key, uint32_t state);
int zwl_menu_key(struct zwl_server *server, uint32_t key, uint32_t state);
void zwl_menu_tick(struct zwl_server *server);
void zwl_menu_forget(struct zwl_server *server, struct zwl_object *object);

/* The glass look's shell, for the menus (shell.c). */
void zwl_glass_raise(struct zwl_server *server, struct zwl_object *surface);
struct zwl_object *zwl_glass_window_at(struct zwl_server *server, int32_t x, int32_t y);

#endif
