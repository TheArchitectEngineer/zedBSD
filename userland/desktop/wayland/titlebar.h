/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Titlebar Presentation (WS070 p008, plan/ws070/titlebar-design.md):
 * the model a client gives a window's titlebar through keiland_titlebar_v1 (its
 * mode, its controls and its tabs), kept by titlebar.c, and what the
 * presentation (titlebar-shell.c) reads of it and sends back.
 */

#ifndef ZWL_TITLEBAR_H
#define ZWL_TITLEBAR_H

#include "menu.h"

/* The presentation modes. */
#define ZWL_TITLEBAR_MENU		0U
#define ZWL_TITLEBAR_CONTROLS		1U
#define ZWL_TITLEBAR_TABS		2U
#define ZWL_TITLEBAR_SHEET		3U

/* The controls' roles. */
#define ZWL_CONTROL_BACK		1U
#define ZWL_CONTROL_FORWARD		2U
#define ZWL_CONTROL_HOME		3U
#define ZWL_CONTROL_UP			4U
#define ZWL_CONTROL_BREADCRUMB		5U
#define ZWL_CONTROL_SEARCH		6U
#define ZWL_CONTROL_VIEW_GRID		7U
#define ZWL_CONTROL_VIEW_LIST		8U
#define ZWL_CONTROL_VIEW_COLUMNS	9U
#define ZWL_CONTROL_SORT		10U
#define ZWL_CONTROL_FILTER		11U
#define ZWL_CONTROL_SIDEBAR		12U
#define ZWL_CONTROL_PREVIEW		13U
#define ZWL_CONTROL_PROGRESS		14U
#define ZWL_CONTROL_PRIMARY_ACTION	15U
#define ZWL_CONTROL_GENERIC		16U

/* The controls' priorities: the order they give way in when the room runs short. */
#define ZWL_PRIORITY_PRIMARY		0U
#define ZWL_PRIORITY_NORMAL		1U
#define ZWL_PRIORITY_SECONDARY		2U

/* The tabs' flags, and the tab strip's options. */
#define ZWL_TAB_ACTIVE			1U
#define ZWL_TAB_ATTENTION		2U
#define ZWL_TAB_CLOSABLE		4U
#define ZWL_TABS_NEW_BUTTON		1U

/* How a text control's editing ended (text_done). */
#define ZWL_TEXT_SUBMITTED		0U
#define ZWL_TEXT_CANCELLED		1U
#define ZWL_TEXT_LEFT			2U

/* The bounds of one titlebar: its controls, its tabs, a breadcrumb's parts, and a string in bytes. */
#define ZWL_TITLEBAR_CONTROLS_MAX	64U
#define ZWL_TITLEBAR_TABS_MAX		128U
#define ZWL_TITLEBAR_SEGMENTS_MAX	32U
#define ZWL_TITLEBAR_TEXT_MAX		1023U

/*
 * One control of a titlebar: what it is (its role), when it gives way (its
 * priority), the segmented pill it joins (its group, 0 for none), its
 * state, and its strings.  A breadcrumb's parts are its segments.  The
 * strings are allocated with the state the control is in.
 */
struct zwl_titlebar_control {
	uint32_t id;
	uint32_t role;
	uint32_t priority;
	uint32_t group;
	uint32_t enabled;
	uint32_t checked;
	uint32_t value;
	char *label;
	char *text;
	char *placeholder;
	char *segments[ZWL_TITLEBAR_SEGMENTS_MAX];
	unsigned segment_count;
};

/*
 * One tab of a titlebar: its ID, its flags (active, attention, closable)
 * and its title, allocated with the state it is in.
 */
struct zwl_titlebar_tab {
	uint32_t id;
	uint32_t flags;
	char *title;
};

/*
 * One whole state of a titlebar: the mode, the controls and the tabs in
 * their order, and the tab strip's options.  A model keeps the state shown
 * and, during a transaction, the state being built.
 */
struct zwl_titlebar_state {
	uint32_t mode;
	struct zwl_titlebar_control controls[ZWL_TITLEBAR_CONTROLS_MAX];
	unsigned control_count;
	struct zwl_titlebar_tab tabs[ZWL_TITLEBAR_TABS_MAX];
	unsigned tab_count;
	uint32_t options;
};

/*
 * A window's titlebar model, owned by its keiland_titlebar_v1 object: the state
 * shown, the state being built between begin_update and commit, the
 * transaction's serial, and how many commits there were.  A control the
 * client asked the keyboard for (focus_control) waits in focus_id (0 when
 * none) until the presentation takes it.  The presentation keeps the first
 * tab its scrolled strip shows (tab_first) and the commit it last showed
 * (tab_seen), to bring the active tab into sight once after a change.
 */
struct zwl_titlebar_model {
	struct zwl_titlebar_state shown;
	struct zwl_titlebar_state pending;
	unsigned updating;
	uint32_t update_serial;
	uint64_t generation;
	uint32_t focus_id;
	uint32_t focus_mode;
	unsigned tab_first;
	uint64_t tab_seen;
};

/* The protocol and the model (titlebar.c). */
int zwl_titlebar_request(struct zwl_object *object, uint32_t opcode, const unsigned char *bytes, size_t size);
void zwl_titlebar_object_gone(struct zwl_object *object);
struct zwl_titlebar_model *zwl_titlebar_of_surface(struct zwl_object *surface, struct zwl_object **titlebar);
const struct zwl_titlebar_control *zwl_titlebar_control(const struct zwl_titlebar_state *state, uint32_t id);
void zwl_titlebar_send_activated(struct zwl_object *titlebar, uint32_t id, uint32_t detail, const char *via);
void zwl_titlebar_send_text(struct zwl_object *titlebar, uint32_t id, const char *text, int done, uint32_t how);
void zwl_titlebar_send_tab(struct zwl_object *titlebar, uint32_t id, unsigned event);
void zwl_titlebar_send_overflow(struct zwl_object *titlebar);
void zwl_titlebar_send_drop_target(struct zwl_object *titlebar, uint32_t id, uint32_t detail);
int zwl_titlebar_drop_at(struct zwl_server *server, int32_t x, int32_t y, struct zwl_object **surface, struct zwl_object **titlebar, uint32_t *id, uint32_t *detail);

/* The presentation (titlebar-shell.c). */
void zwl_titlebar_frame(struct zwl_server *server);
int32_t zwl_titlebar_title_limit(struct zwl_server *server, struct zwl_object *surface, int32_t available);
void zwl_titlebar_draw(struct zwl_server *server, VkCommandBuffer command, struct zwl_object *surface, unsigned docked, const struct zwl_menu_area *area, const float *ink, float fade);
int zwl_titlebar_button(struct zwl_server *server, uint32_t button, uint32_t state);
int zwl_titlebar_motion(struct zwl_server *server);
int zwl_titlebar_key(struct zwl_server *server, uint32_t key, uint32_t state);
int zwl_titlebar_tab_key(struct zwl_server *server, uint32_t key, uint32_t state);
int zwl_titlebar_axis(struct zwl_server *server, int32_t vertical, int32_t horizontal);
void zwl_titlebar_overflow_chosen(struct zwl_server *server, struct zwl_object *surface, uint32_t id);
void zwl_titlebar_overflow_opened(struct zwl_object *surface);
void zwl_titlebar_forget(struct zwl_server *server, struct zwl_object *object);

/* The tab events zwl_titlebar_send_tab sends. */
#define ZWL_TAB_EVENT_ACTIVATED		0U
#define ZWL_TAB_EVENT_CLOSE		1U
#define ZWL_TAB_EVENT_NEW		2U

#endif
