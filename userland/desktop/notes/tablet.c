/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The pen of Notes: the tablet protocol (zwp_tablet_manager_v2, ws079-p003).
 *
 * Notes binds the tablet manager when the compositor offers it and asks
 * for its seat's tablets.  Each tool (a pen's tip, its eraser end) reports
 * its state in pieces -- proximity, contact, position, pressure, tilt,
 * buttons -- and a frame ends each group; at the frame the tool's state is
 * turned into Notes' input events (window.c's queue): a contact starting
 * is NOTES_INPUT_DOWN, a move in contact NOTES_INPUT_MOTION, a lift
 * NOTES_INPUT_UP.  The pressure comes as 0..65535 and is given on as 0..1;
 * the tilt comes in degrees.  The eraser end is NOTES_SOURCE_ERASER, and
 * so is the pen's tip while its first barrel button is held
 * (design-input-notes.md D5).
 *
 * A compositor sends the pen to a client that holds a tool as the tablet
 * protocol, and to any other client as the pointer; without the tablet
 * manager Notes takes the pen through the pointer (window.c), with the
 * pointer's fixed pressure.
 */

#include "app.h"

#include <stdio.h>

#include <stdlib.h>
#include <string.h>

/* The evdev code of the pen's first barrel button. */
#define TABLET_BUTTON_STYLUS	0x14bU

static void tablet_seat_tablet(void *data, struct zwp_tablet_seat_v2 *seat, struct zwp_tablet_v2 *tablet);
static void tablet_seat_tool(void *data, struct zwp_tablet_seat_v2 *seat, struct zwp_tablet_tool_v2 *tool);
static void tablet_seat_pad(void *data, struct zwp_tablet_seat_v2 *seat, struct zwp_tablet_pad_v2 *pad);
static void tablet_name(void *data, struct zwp_tablet_v2 *tablet, const char *name);
static void tablet_id(void *data, struct zwp_tablet_v2 *tablet, uint32_t vendor, uint32_t product);
static void tablet_path(void *data, struct zwp_tablet_v2 *tablet, const char *path);
static void tablet_done(void *data, struct zwp_tablet_v2 *tablet);
static void tablet_removed(void *data, struct zwp_tablet_v2 *tablet);
static void tool_type(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t type);
static void tool_serial(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t high, uint32_t low);
static void tool_wacom(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t high, uint32_t low);
static void tool_capability(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t capability);
static void tool_done(void *data, struct zwp_tablet_tool_v2 *tool);
static void tool_removed(void *data, struct zwp_tablet_tool_v2 *tool);
static void tool_proximity_in(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t serial, struct zwp_tablet_v2 *tablet, struct wl_surface *surface);
static void tool_proximity_out(void *data, struct zwp_tablet_tool_v2 *tool);
static void tool_down(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t serial);
static void tool_up(void *data, struct zwp_tablet_tool_v2 *tool);
static void tool_motion(void *data, struct zwp_tablet_tool_v2 *tool, wl_fixed_t x, wl_fixed_t y);
static void tool_pressure(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t pressure);
static void tool_distance(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t distance);
static void tool_tilt(void *data, struct zwp_tablet_tool_v2 *tool, wl_fixed_t x, wl_fixed_t y);
static void tool_rotation(void *data, struct zwp_tablet_tool_v2 *tool, wl_fixed_t degrees);
static void tool_slider(void *data, struct zwp_tablet_tool_v2 *tool, int32_t position);
static void tool_wheel(void *data, struct zwp_tablet_tool_v2 *tool, wl_fixed_t degrees, int32_t clicks);
static void tool_button(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t serial, uint32_t button, uint32_t state);
static void tool_frame(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t time);
static void tool_event(struct notes_tablet_tool *state, unsigned kind, uint32_t time);

/* The seat's tablets and tools as they are announced. */
static const struct zwp_tablet_seat_v2_listener seat_listener = {
	tablet_seat_tablet, tablet_seat_tool, tablet_seat_pad
};

/* A tablet's description, which Notes does not need, and its removal. */
static const struct zwp_tablet_v2_listener tablet_listener = {
	tablet_name, tablet_id, tablet_path, tablet_done, tablet_removed
};

/* A tool's description and its state, frame by frame. */
static const struct zwp_tablet_tool_v2_listener tool_listener = {
	tool_type, tool_serial, tool_wacom, tool_capability, tool_done, tool_removed,
	tool_proximity_in, tool_proximity_out, tool_down, tool_up, tool_motion,
	tool_pressure, tool_distance, tool_tilt, tool_rotation, tool_slider, tool_wheel,
	tool_button, tool_frame
};

/*
 * Binds the compositor's tablet manager (version 1).
 */
void
notes_tablet_bind(
	struct notes_window *window,
	struct wl_registry *registry,
	uint32_t name)
{
	/* One manager is enough. */
	if (window->tablet_manager != NULL)
		return;

	/* The manager of the tablet protocol's version 1. */
	window->tablet_manager = wl_registry_bind(registry, name, &zwp_tablet_manager_v2_interface, 1U);
}

/*
 * Asks for the seat's tablets and tools, once both the manager and the seat
 * are bound.
 */
void
notes_tablet_start(
	struct notes_window *window)
{
	int status;

	/* Without the manager or the seat the pen comes as the pointer. */
	if (window->tablet_manager == NULL || window->seat == NULL)
		return;

	/* The seat's tablets, announced to the listener. */
	window->tablet_seat = zwp_tablet_manager_v2_get_tablet_seat(window->tablet_manager, window->seat);
	if (window->tablet_seat == NULL)
		return;

	/* Listens for the tablets and the tools. */
	status = zwp_tablet_seat_v2_add_listener(window->tablet_seat, &seat_listener, window);
	if (status != 0)
		return;

	/* The log line the tests read. */
	printf("NOTES TABLET seat\n");
	fflush(stdout);
}

/*
 * Releases the tools, the tablets' seat and the manager.
 */
void
notes_tablet_close(
	struct notes_window *window)
{
	unsigned index;

	/* Each tool and its state. */
	for (index = 0; index < window->tool_count; index++) {
		zwp_tablet_tool_v2_destroy(window->tools[index]->tool);
		free(window->tools[index]);
	}

	/* None is left. */
	window->tool_count = 0;

	/* The seat, then the manager. */
	if (window->tablet_seat != NULL)
		zwp_tablet_seat_v2_destroy(window->tablet_seat);
	if (window->tablet_manager != NULL)
		zwp_tablet_manager_v2_destroy(window->tablet_manager);
	window->tablet_seat = NULL;
	window->tablet_manager = NULL;
}

/* A new tablet: its events are listened to, only for its removal. */
static void
tablet_seat_tablet(
	void *data,
	struct zwp_tablet_seat_v2 *seat,
	struct zwp_tablet_v2 *tablet)
{
	/* The tablet's description and removal. */
	(void)seat;
	(void)zwp_tablet_v2_add_listener(tablet, &tablet_listener, data);
}

/* A new tool: its state is kept, and its events listened to. */
static void
tablet_seat_tool(
	void *data,
	struct zwp_tablet_seat_v2 *seat,
	struct zwp_tablet_tool_v2 *tool)
{
	struct notes_window *window;
	struct notes_tablet_tool *state;

	/* A full table leaves the tool alone (its events are never listened to). */
	(void)seat;
	window = data;
	if (window->tool_count >= NOTES_TABLET_TOOLS)
		return;

	/* The tool's state, a pen until its type says otherwise. */
	state = calloc(1U, sizeof(*state));
	if (state == NULL)
		return;
	state->window = window;
	state->tool = tool;
	state->type = ZWP_TABLET_TOOL_V2_TYPE_PEN;

	/* Succeeded: the tool is kept and heard. */
	window->tools[window->tool_count] = state;
	window->tool_count++;
	(void)zwp_tablet_tool_v2_add_listener(tool, &tool_listener, state);
}

/* A pad (never announced by the compositor) is not used. */
static void
tablet_seat_pad(
	void *data,
	struct zwp_tablet_seat_v2 *seat,
	struct zwp_tablet_pad_v2 *pad)
{
	/* The pad is given back. */
	(void)data;
	(void)seat;
	zwp_tablet_pad_v2_destroy(pad);
}

/* A tablet's name is not used. */
static void
tablet_name(
	void *data,
	struct zwp_tablet_v2 *tablet,
	const char *name)
{
	/* Nothing to do. */
	(void)data;
	(void)tablet;
	(void)name;
}

/* A tablet's USB identity is not used. */
static void
tablet_id(
	void *data,
	struct zwp_tablet_v2 *tablet,
	uint32_t vendor,
	uint32_t product)
{
	/* Nothing to do. */
	(void)data;
	(void)tablet;
	(void)vendor;
	(void)product;
}

/* A tablet's device path is not used. */
static void
tablet_path(
	void *data,
	struct zwp_tablet_v2 *tablet,
	const char *path)
{
	/* Nothing to do. */
	(void)data;
	(void)tablet;
	(void)path;
}

/* A tablet's description is complete. */
static void
tablet_done(
	void *data,
	struct zwp_tablet_v2 *tablet)
{
	/* Nothing to do. */
	(void)data;
	(void)tablet;
}

/* A tablet was unplugged: its object goes. */
static void
tablet_removed(
	void *data,
	struct zwp_tablet_v2 *tablet)
{
	/* The object is destroyed, as the protocol asks. */
	(void)data;
	zwp_tablet_v2_destroy(tablet);
}

/* The tool's kind: a pen's tip or its eraser end matter. */
static void
tool_type(
	void *data,
	struct zwp_tablet_tool_v2 *tool,
	uint32_t type)
{
	struct notes_tablet_tool *state;

	/* The kind decides the input's source. */
	(void)tool;
	state = data;
	state->type = type;
}

/* A tool's serial number is not used. */
static void
tool_serial(
	void *data,
	struct zwp_tablet_tool_v2 *tool,
	uint32_t high,
	uint32_t low)
{
	/* Nothing to do. */
	(void)data;
	(void)tool;
	(void)high;
	(void)low;
}

/* A tool's Wacom identity is not used. */
static void
tool_wacom(
	void *data,
	struct zwp_tablet_tool_v2 *tool,
	uint32_t high,
	uint32_t low)
{
	/* Nothing to do. */
	(void)data;
	(void)tool;
	(void)high;
	(void)low;
}

/* A tool's capability: whether it has pressure and tilt. */
static void
tool_capability(
	void *data,
	struct zwp_tablet_tool_v2 *tool,
	uint32_t capability)
{
	struct notes_tablet_tool *state;

	/* Pressure and tilt are kept; the other axes are not used. */
	(void)tool;
	state = data;
	if (capability == ZWP_TABLET_TOOL_V2_CAPABILITY_PRESSURE)
		state->has_pressure = 1;
	if (capability == ZWP_TABLET_TOOL_V2_CAPABILITY_TILT)
		state->has_tilt = 1;
}

/* The tool's description is complete; the log line the tests read. */
static void
tool_done(
	void *data,
	struct zwp_tablet_tool_v2 *tool)
{
	struct notes_tablet_tool *state;

	/* The tool as described. */
	(void)tool;
	state = data;
	printf("NOTES TABLET tool type=0x%x pressure=%d tilt=%d\n", state->type, state->has_pressure, state->has_tilt);
	fflush(stdout);
}

/* A tool was removed: it is forgotten. */
static void
tool_removed(
	void *data,
	struct zwp_tablet_tool_v2 *tool)
{
	struct notes_tablet_tool *state;
	struct notes_window *window;
	unsigned index;

	/* A contact still under way ends. */
	state = data;
	window = state->window;
	if (state->down)
		tool_event(state, NOTES_INPUT_UP, 0U);

	/* The tool leaves the table. */
	for (index = 0; index < window->tool_count; index++) {
		if (window->tools[index] == state)
			break;
	}

	/* The last tool takes its slot. */
	if (index < window->tool_count) {
		window->tools[index] = window->tools[window->tool_count - 1U];
		window->tool_count--;
	}

	/* The object and the state go. */
	zwp_tablet_tool_v2_destroy(tool);
	free(state);
}

/* The tool came over the window. */
static void
tool_proximity_in(
	void *data,
	struct zwp_tablet_tool_v2 *tool,
	uint32_t serial,
	struct zwp_tablet_v2 *tablet,
	struct wl_surface *surface)
{
	struct notes_tablet_tool *state;

	/* Near the window, not yet touching. */
	(void)tool;
	(void)serial;
	(void)tablet;
	(void)surface;
	state = data;
	state->near = 1;
}

/* The tool left the window: a contact still under way ends. */
static void
tool_proximity_out(
	void *data,
	struct zwp_tablet_tool_v2 *tool)
{
	struct notes_tablet_tool *state;

	/* The tool is gone; the frame that follows ends a contact. */
	(void)tool;
	state = data;
	state->near = 0;
	if (state->down)
		state->lifting = 1;
}

/* The tool touched: the frame starts a contact. */
static void
tool_down(
	void *data,
	struct zwp_tablet_tool_v2 *tool,
	uint32_t serial)
{
	struct notes_tablet_tool *state;

	/* The contact starts with the frame. */
	(void)tool;
	(void)serial;
	state = data;
	state->touching = 1;
}

/* The tool lifted: the frame ends the contact. */
static void
tool_up(
	void *data,
	struct zwp_tablet_tool_v2 *tool)
{
	struct notes_tablet_tool *state;

	/* The contact ends with the frame. */
	(void)tool;
	state = data;
	if (state->down)
		state->lifting = 1;
	state->touching = 0;
}

/* The tool moved, in surface coordinates. */
static void
tool_motion(
	void *data,
	struct zwp_tablet_tool_v2 *tool,
	wl_fixed_t x,
	wl_fixed_t y)
{
	struct notes_tablet_tool *state;

	/* The new place, and a move for the frame. */
	(void)tool;
	state = data;
	state->x = (float)wl_fixed_to_double(x);
	state->y = (float)wl_fixed_to_double(y);
	state->moved = 1;
}

/* The pressure, 0 to 65535. */
static void
tool_pressure(
	void *data,
	struct zwp_tablet_tool_v2 *tool,
	uint32_t pressure)
{
	struct notes_tablet_tool *state;

	/* Kept from 0 to 1, and a change for the frame. */
	(void)tool;
	state = data;
	state->pressure = (float)pressure / 65535.0f;
	state->moved = 1;
}

/* The distance while hovering is not used. */
static void
tool_distance(
	void *data,
	struct zwp_tablet_tool_v2 *tool,
	uint32_t distance)
{
	/* Nothing to do. */
	(void)data;
	(void)tool;
	(void)distance;
}

/* The tilt, in degrees. */
static void
tool_tilt(
	void *data,
	struct zwp_tablet_tool_v2 *tool,
	wl_fixed_t x,
	wl_fixed_t y)
{
	struct notes_tablet_tool *state;

	/* Kept for the next sample. */
	(void)tool;
	state = data;
	state->tilt_x = (float)wl_fixed_to_double(x);
	state->tilt_y = (float)wl_fixed_to_double(y);
}

/* The rotation is not used. */
static void
tool_rotation(
	void *data,
	struct zwp_tablet_tool_v2 *tool,
	wl_fixed_t degrees)
{
	/* Nothing to do. */
	(void)data;
	(void)tool;
	(void)degrees;
}

/* The slider is not used. */
static void
tool_slider(
	void *data,
	struct zwp_tablet_tool_v2 *tool,
	int32_t position)
{
	/* Nothing to do. */
	(void)data;
	(void)tool;
	(void)position;
}

/* The wheel is not used. */
static void
tool_wheel(
	void *data,
	struct zwp_tablet_tool_v2 *tool,
	wl_fixed_t degrees,
	int32_t clicks)
{
	/* Nothing to do. */
	(void)data;
	(void)tool;
	(void)degrees;
	(void)clicks;
}

/* A barrel button: the first one held makes the tip erase (D5); the others are not used. */
static void
tool_button(
	void *data,
	struct zwp_tablet_tool_v2 *tool,
	uint32_t serial,
	uint32_t button,
	uint32_t state_value)
{
	struct notes_tablet_tool *state;

	/* Only the first barrel button. */
	(void)tool;
	(void)serial;
	state = data;
	if (button != TABLET_BUTTON_STYLUS)
		return;

	/* Held or released. */
	state->stylus = 0;
	if (state_value == ZWP_TABLET_TOOL_V2_BUTTON_STATE_PRESSED)
		state->stylus = 1;
}

/* The end of a group of changes: they become Notes' input events. */
static void
tool_frame(
	void *data,
	struct zwp_tablet_tool_v2 *tool,
	uint32_t time)
{
	struct notes_tablet_tool *state;

	/* A contact that starts, a move in contact, a contact that ends. */
	(void)tool;
	state = data;
	if (state->touching && !state->down) {
		state->down = 1;
		tool_event(state, NOTES_INPUT_DOWN, time);
	} else if (state->down && state->moved && !state->lifting) {
		tool_event(state, NOTES_INPUT_MOTION, time);
	}

	/* A lift, after the last move of the frame. */
	if (state->lifting) {
		if (state->moved)
			tool_event(state, NOTES_INPUT_MOTION, time);
		tool_event(state, NOTES_INPUT_UP, time);
		state->down = 0;
		state->lifting = 0;
	}

	/* The frame's changes are taken. */
	state->moved = 0;
}

/* Queues one input event of a tool at its place, with its pressure and tilt. */
static void
tool_event(
	struct notes_tablet_tool *state,
	unsigned kind,
	uint32_t time)
{
	struct notes_input input;

	/* The event, from the eraser end or the tip (erasing while its first button is held). */
	memset(&input, 0, sizeof(input));
	input.kind = kind;
	input.source = NOTES_SOURCE_PEN;
	if (state->type == ZWP_TABLET_TOOL_V2_TYPE_ERASER || state->stylus)
		input.source = NOTES_SOURCE_ERASER;
	input.x = state->x;
	input.y = state->y;
	input.pressure = state->pressure;
	if (!state->has_pressure)
		input.pressure = NOTES_POINTER_PRESSURE;
	input.tilt_x = state->tilt_x;
	input.tilt_y = state->tilt_y;
	input.time_ms = time;

	/* Queued like the pointer's. */
	notes_window_input(state->window, &input);
}
