/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The xdg_toplevel requests a toolkit makes of the window manager
 * (ws035-p076), and the xdg_wm_base ping.
 *
 * A toolkit that draws its own title bar asks the window manager to move
 * or resize the window from a press on it (move, resize), and to maximize,
 * unmaximize or minimize it; it gives the smallest and largest size it can
 * draw (set_min_size, set_max_size), a dialog's parent (set_parent), and
 * asks for the window menu (show_window_menu).  A move or a resize is taken
 * only while the press it names is still held.
 *
 * An interactive resize follows the pointer until the button is let go:
 * each new size goes to the client in a configure with the resizing state,
 * and the edge opposite the one being dragged stays where it was, so a
 * window resized from its left or top edge is moved as its new images come.
 * The anchor holds until the client has drawn the configure sent when the
 * resize ended.
 *
 * The compositor pings a client when a press reaches it; a client that has
 * not answered PING_TIMEOUT_MS later is not responding, which its title bar
 * says until the answer comes.
 */

#include "toplevel.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* xdg_toplevel's requests handled here (the others are in protocol.c). */
#define TOPLEVEL_SET_PARENT		1U
#define TOPLEVEL_SHOW_WINDOW_MENU	4U
#define TOPLEVEL_MOVE			5U
#define TOPLEVEL_RESIZE			6U
#define TOPLEVEL_SET_MAX_SIZE		7U
#define TOPLEVEL_SET_MIN_SIZE		8U
#define TOPLEVEL_SET_MAXIMIZED		9U
#define TOPLEVEL_UNSET_MAXIMIZED	10U
#define TOPLEVEL_SET_MINIMIZED		13U

/* xdg_toplevel's errors: a resize edge that is not one, and a parent that cannot be one. */
#define TOPLEVEL_ERROR_INVALID_RESIZE_EDGE	0U
#define TOPLEVEL_ERROR_INVALID_PARENT		1U

/* The edges a resize drags (xdg_toplevel.resize_edge is made of these bits). */
#define EDGE_TOP		1U
#define EDGE_BOTTOM		2U
#define EDGE_LEFT		4U
#define EDGE_RIGHT		8U

/* xdg_wm_base's ping event. */
#define WM_BASE_PING		0U

/* How long a ping may go unanswered before its client is not responding. */
#define PING_TIMEOUT_MS		5000U

/* The smallest window a resize makes, whatever the client's minimum. */
#define RESIZE_MINIMUM		64

static int toplevel_set_parent(struct zwl_object *toplevel, const unsigned char *bytes, size_t size);
static int toplevel_size_hint(struct zwl_object *surface, uint32_t opcode, const unsigned char *bytes, size_t size);
static int toplevel_move(struct zwl_object *toplevel, struct zwl_object *surface, const unsigned char *bytes, size_t size);
static int toplevel_resize(struct zwl_object *toplevel, struct zwl_object *surface, const unsigned char *bytes, size_t size);
static int toplevel_state(struct zwl_object *surface, uint32_t opcode, size_t size);
static int press_held(struct zwl_client *client, uint32_t seat_id, uint32_t serial);
static void window_extent(const struct zwl_object *surface, int32_t *x, int32_t *y, int32_t *width, int32_t *height);
static void resize_limit(struct zwl_server *server, struct zwl_object *surface, int32_t *width, int32_t *height);
static void resize_settle(struct zwl_object *surface, int32_t width, int32_t height);
static int32_t window_lowest(const struct zwl_server *server);
static uint32_t toplevel_word(const unsigned char *bytes, size_t offset);

/*
 * Carries out an xdg_toplevel request of the window manager: set_parent,
 * show_window_menu, move, resize, the size limits, maximize, unmaximize and
 * minimize.
 */
int
zwl_toplevel_request(
	struct zwl_object *toplevel,
	struct zwl_object *surface,
	uint32_t opcode,
	const unsigned char *bytes,
	size_t size)
{
	int error;

	/* Each request by its opcode. */
	switch (opcode) {
	case TOPLEVEL_SET_PARENT:
		error = toplevel_set_parent(toplevel, bytes, size);
		break;
	case TOPLEVEL_SHOW_WINDOW_MENU:
		/* The window menu at a point (seat, serial, x, y): zdesktop has none, so it is only checked. */
		error = 0;
		if (size != 16U)
			error = EPROTO;
		break;
	case TOPLEVEL_MOVE:
		error = toplevel_move(toplevel, surface, bytes, size);
		break;
	case TOPLEVEL_RESIZE:
		error = toplevel_resize(toplevel, surface, bytes, size);
		break;
	case TOPLEVEL_SET_MAX_SIZE:
	case TOPLEVEL_SET_MIN_SIZE:
		error = toplevel_size_hint(surface, opcode, bytes, size);
		break;
	case TOPLEVEL_SET_MAXIMIZED:
	case TOPLEVEL_UNSET_MAXIMIZED:
	case TOPLEVEL_SET_MINIMIZED:
		error = toplevel_state(surface, opcode, size);
		break;
	default:
		/* No other request of xdg_toplevel version 3 comes here. */
		error = EPROTO;
		break;
	}

	/* Reports a request that was refused. */
	if (error != 0)
		return error;

	/* Succeeded: the request was carried out. */
	return 0;
}

/*
 * Tells whether a window is being resized now (its configures carry the
 * resizing state).
 */
unsigned
zwl_toplevel_resizing(
	const struct zwl_server *server,
	const struct zwl_object *surface)
{
	/* Only the window the resize follows. */
	if (server->resize != surface)
		return 0;

	/* Succeeded: it is being resized. */
	return 1;
}

/*
 * Keeps the edge opposite the dragged one in place when a window being
 * resized commits a new image, and ends the resize's anchor once the client
 * has drawn the configure sent when the resize ended.
 */
void
zwl_toplevel_committed(
	struct zwl_server *server,
	struct zwl_object *surface)
{
	int32_t geometry_x;
	int32_t geometry_y;
	int32_t width;
	int32_t height;

	/* Only a window with a resize's anchor, shown with an image. */
	if (surface->resize_edges == 0U || surface->current == NULL)
		return;

	/* A window moved, docked or animated since has lost its anchor. */
	if (surface->maximized ||
	    surface->fullscreen ||
	    server->drag == surface ||
	    server->anim == surface) {
		surface->resize_edges = 0;
		return;
	}

	/* The window's extent in the new image. */
	window_extent(surface, &geometry_x, &geometry_y, &width, &height);

	/* Dragged from the left, the right edge stays; dragged from the top, the bottom does. */
	if ((surface->resize_edges & EDGE_LEFT) != 0U)
		surface->x = surface->resize_right - geometry_x - width;
	if ((surface->resize_edges & EDGE_TOP) != 0U)
		surface->y = surface->resize_bottom - geometry_y - height;
	server->dirty = 1;

	/* While the resize goes on, the anchor stays. */
	if (server->resize == surface)
		return;

	/*
	 * After it, the anchor goes with the first image of the last size, or
	 * the first drawn after the client acknowledged the last configure (a
	 * client may draw a size of its own, a terminal a whole number of
	 * cells).
	 */
	if (width == (int32_t)surface->window_width && height == (int32_t)surface->window_height) {
		resize_settle(surface, width, height);
		return;
	}

	/* Succeeded: the anchor goes once the last configure is drawn. */
	if (surface->acked_serial == surface->resize_final_serial)
		resize_settle(surface, width, height);
}

/*
 * Ends a resize of a surface that is going.
 */
void
zwl_toplevel_surface_gone(
	struct zwl_object *surface)
{
	struct zwl_server *server;

	/* Only the window being resized has anything to end. */
	server = surface->client->server;
	if (server->resize != surface)
		return;

	/* The pointer is nobody's resize any more. */
	server->resize = NULL;
}

/*
 * Follows the pointer while a window is being resized: the new size, kept
 * within the window's limits, goes to the client in a configure.  Returns 1
 * when the motion was the resize's.
 */
int
zwl_toplevel_motion(
	struct zwl_server *server)
{
	struct zwl_object *surface;
	int32_t width;
	int32_t height;
	int error;

	/* Without a resize the motion goes on as usual. */
	surface = server->resize;
	if (surface == NULL)
		return 0;

	/* A window that went away or changed its state ends the resize. */
	if (surface->dead ||
	    !surface->mapped ||
	    surface->fullscreen ||
	    surface->maximized) {
		server->resize = NULL;
		surface->resize_edges = 0;
		return 1;
	}

	/* The size at the start, grown or shrunk by how far each dragged edge has moved. */
	width = server->resize_width;
	height = server->resize_height;
	if ((surface->resize_edges & EDGE_RIGHT) != 0U)
		width += server->pointer_x - server->resize_pointer_x;
	if ((surface->resize_edges & EDGE_LEFT) != 0U)
		width -= server->pointer_x - server->resize_pointer_x;
	if ((surface->resize_edges & EDGE_BOTTOM) != 0U)
		height += server->pointer_y - server->resize_pointer_y;
	if ((surface->resize_edges & EDGE_TOP) != 0U)
		height -= server->pointer_y - server->resize_pointer_y;

	/* Within the window's limits and the output. */
	resize_limit(server, surface, &width, &height);

	/* An unchanged size tells the client nothing. */
	if (width == (int32_t)surface->window_width && height == (int32_t)surface->window_height)
		return 1;

	/* The client is told the new size, with the resizing state. */
	surface->window_width = (uint32_t)width;
	surface->window_height = (uint32_t)height;
	error = zwl_window_send_configure(surface);
	if (error != 0)
		printf("ZWL RESIZE configure errno=%d\n", error);

	/* Succeeded: the motion was the resize's. */
	return 1;
}

/*
 * Ends a resize when its button is let go: the client is told the last
 * size without the resizing state.  Returns 1 when the button was the
 * resize's (a press while resizing is eaten too).
 */
int
zwl_toplevel_button(
	struct zwl_server *server,
	uint32_t state)
{
	struct zwl_object *surface;
	int32_t geometry_x;
	int32_t geometry_y;
	int32_t width;
	int32_t height;
	int error;

	/* Without a resize the button goes on as usual. */
	surface = server->resize;
	if (surface == NULL)
		return 0;

	/* Another press while resizing is the resize's. */
	if (state != 0U)
		return 1;

	/* The release ends it; the client is told the last size again, without the resizing state. */
	server->resize = NULL;
	error = zwl_window_send_configure(surface);
	if (error != 0)
		printf("ZWL RESIZE configure errno=%d\n", error);
	surface->resize_final_serial = surface->configure_serial;
	printf("ZWL RESIZE end surface=%u width=%u height=%u\n", surface->id, surface->window_width, surface->window_height);

	/* A client that has drawn the last size already needs the anchor no more. */
	window_extent(surface, &geometry_x, &geometry_y, &width, &height);
	if (width == (int32_t)surface->window_width && height == (int32_t)surface->window_height)
		resize_settle(surface, width, height);

	/* Succeeded: the button was the resize's. */
	return 1;
}

/*
 * Pings a client (xdg_wm_base.ping), unless a ping is already waiting for
 * its answer or the client has no xdg_wm_base.
 */
void
zwl_ping_send(
	struct zwl_client *client)
{
	struct zwl_object *wm;
	uint32_t serial;
	int error;

	/* A failed client, or one with a ping out already, is not pinged again. */
	if (client->fatal || client->ping_serial != 0U)
		return;

	/* The client's shell binding, which the ping goes to. */
	for (wm = client->objects; wm != NULL; wm = wm->next) {
		/* Only a live xdg_wm_base. */
		if (wm->kind == ZWL_WM && !wm->dead)
			break;
	}

	/* A client without one cannot be pinged. */
	if (wm == NULL)
		return;

	/* The ping with a new serial, which the answer must name. */
	serial = zwl_next_serial(client->server);
	error = zwl_emit(client, wm->id, WM_BASE_PING, &serial, sizeof(serial));
	if (error != 0)
		return;

	/* The ping is out from now. */
	client->ping_serial = serial;
	client->ping_ms = zwl_milliseconds();
}

/*
 * Takes a client's answer to a ping (xdg_wm_base.pong): the client that
 * was not responding is again.  An answer to no ping is ignored.
 */
void
zwl_ping_pong(
	struct zwl_client *client,
	uint32_t serial)
{
	/* Only the answer to the ping that is out. */
	if (serial == 0U || serial != client->ping_serial)
		return;

	/* No ping is out any more. */
	client->ping_serial = 0;
	printf("ZWL PING pong client=%llu serial=%u\n", (unsigned long long)client->number, serial);

	/* A client that was not responding is again, and its title bars are drawn so. */
	if (client->unresponsive) {
		client->unresponsive = 0;
		client->server->dirty = 1;
		printf("ZWL PING responsive client=%llu\n", (unsigned long long)client->number);
	}
}

/*
 * Finds the clients whose ping has gone unanswered for PING_TIMEOUT_MS:
 * they are not responding until they answer.
 */
void
zwl_ping_check(
	struct zwl_server *server,
	uint64_t now)
{
	struct zwl_client *client;

	/* Every client with a ping out. */
	for (client = server->clients; client != NULL; client = client->next) {
		/* A failed client, one without a ping out, or one already known not to respond. */
		if (client->fatal || client->ping_serial == 0U || client->unresponsive)
			continue;

		/* A ping still within its time. */
		if (now < client->ping_ms || now - client->ping_ms < PING_TIMEOUT_MS)
			continue;

		/* It is not responding; its title bars say so. */
		client->unresponsive = 1;
		server->dirty = 1;
		printf("ZWL PING unresponsive client=%llu\n", (unsigned long long)client->number);
	}
}

/* Takes a dialog's parent (xdg_toplevel.set_parent); zdesktop keeps windows independent. */
static int
toplevel_set_parent(
	struct zwl_object *toplevel,
	const unsigned char *bytes,
	size_t size)
{
	struct zwl_object *parent;
	uint32_t id;

	/* One nullable toplevel. */
	if (size != 4U)
		return EPROTO;

	/* No parent at all. */
	id = toplevel_word(bytes, 0U);
	if (id == 0U)
		return 0;

	/* A parent must be another toplevel of the client. */
	parent = zwl_find(toplevel->client, id);
	if (parent == NULL ||
	    parent->kind != ZWL_TOPLEVEL ||
	    parent == toplevel) {
		(void)zwl_error_code(toplevel->client, toplevel->id, TOPLEVEL_ERROR_INVALID_PARENT, "the parent is not another toplevel");
		return EPROTO;
	}

	/* Succeeded: accepted; a new window is on top of its parent anyway. */
	return 0;
}

/* Takes the smallest or the largest size a client can draw its window at (0 for no limit). */
static int
toplevel_size_hint(
	struct zwl_object *surface,
	uint32_t opcode,
	const unsigned char *bytes,
	size_t size)
{
	int32_t width;
	int32_t height;

	/* One width and one height, neither negative. */
	if (size != 8U)
		return EPROTO;
	width = (int32_t)toplevel_word(bytes, 0U);
	height = (int32_t)toplevel_word(bytes, 4U);
	if (width < 0 || height < 0)
		return EPROTO;

	/* The largest size. */
	if (opcode == TOPLEVEL_SET_MAX_SIZE) {
		surface->max_width = width;
		surface->max_height = height;
		return 0;
	}

	/* Succeeded: the smallest size. */
	surface->min_width = width;
	surface->min_height = height;
	return 0;
}

/* Starts a move the client asked for from a press on its own title bar. */
static int
toplevel_move(
	struct zwl_object *toplevel,
	struct zwl_object *surface,
	const unsigned char *bytes,
	size_t size)
{
	struct zwl_server *server;
	uint32_t seat_id;
	uint32_t serial;
	int held;

	/* A seat and the serial of the press that asks for the move. */
	if (size != 8U)
		return EPROTO;

	/* Only while that press is held; a late request does nothing. */
	server = toplevel->client->server;
	seat_id = toplevel_word(bytes, 0U);
	serial = toplevel_word(bytes, 4U);
	held = press_held(toplevel->client, seat_id, serial);
	if (held < 0)
		return EPROTO;
	if (held == 0) {
		printf("ZWL GLASS request move surface=%u refused=no-press\n", surface->id);
		return 0;
	}

	/* Succeeded: the glass look moves the window until the button is let go. */
	if (server->glass)
		zwl_glass_toplevel_request(server, surface, ZWL_TOPLEVEL_MOVE);
	return 0;
}

/* Starts a resize the client asked for from a press on its own window's edge. */
static int
toplevel_resize(
	struct zwl_object *toplevel,
	struct zwl_object *surface,
	const unsigned char *bytes,
	size_t size)
{
	struct zwl_server *server;
	uint32_t seat_id;
	uint32_t serial;
	uint32_t edges;
	int32_t geometry_x;
	int32_t geometry_y;
	int32_t width;
	int32_t height;
	int held;

	/* A seat, the serial of the press, and the edges dragged. */
	if (size != 12U)
		return EPROTO;

	/* The edges must be one side or one corner. */
	edges = toplevel_word(bytes, 8U);
	if (edges == 0U ||
	    edges > (EDGE_BOTTOM | EDGE_RIGHT) ||
	    (edges & (EDGE_TOP | EDGE_BOTTOM)) == (EDGE_TOP | EDGE_BOTTOM) ||
	    (edges & (EDGE_LEFT | EDGE_RIGHT)) == (EDGE_LEFT | EDGE_RIGHT)) {
		(void)zwl_error_code(toplevel->client, toplevel->id, TOPLEVEL_ERROR_INVALID_RESIZE_EDGE, "not a resize edge");
		return EPROTO;
	}

	/* Only while that press is held; a late request does nothing. */
	server = toplevel->client->server;
	seat_id = toplevel_word(bytes, 0U);
	serial = toplevel_word(bytes, 4U);
	held = press_held(toplevel->client, seat_id, serial);
	if (held < 0)
		return EPROTO;
	if (held == 0) {
		printf("ZWL RESIZE refused surface=%u reason=no-press\n", surface->id);
		return 0;
	}

	/* Only a shown window in window mode, at its own size, with nothing else following the pointer. */
	if (!server->windowed ||
	    surface->current == NULL ||
	    !surface->mapped ||
	    surface->fullscreen ||
	    surface->maximized ||
	    server->resize != NULL ||
	    server->drag != NULL) {
		printf("ZWL RESIZE refused surface=%u reason=state\n", surface->id);
		return 0;
	}

	/* Where the pointer and the window's size start. */
	window_extent(surface, &geometry_x, &geometry_y, &width, &height);
	server->resize = surface;
	server->resize_pointer_x = server->pointer_x;
	server->resize_pointer_y = server->pointer_y;
	server->resize_width = width;
	server->resize_height = height;

	/* The dragged edges, and the right and bottom edges on the output that stay when the left or top is dragged. */
	surface->resize_edges = edges;
	surface->resize_right = surface->x + geometry_x + width;
	surface->resize_bottom = surface->y + geometry_y + height;
	surface->window_width = (uint32_t)width;
	surface->window_height = (uint32_t)height;

	/* Succeeded: the log line the tests read. */
	printf("ZWL RESIZE start surface=%u edges=%u width=%d height=%d\n", surface->id, edges, width, height);
	return 0;
}

/* Carries out maximize, unmaximize and minimize, which the glass look has. */
static int
toplevel_state(
	struct zwl_object *surface,
	uint32_t opcode,
	size_t size)
{
	struct zwl_server *server;
	int request;

	/* None has arguments. */
	if (size != 0U)
		return EPROTO;

	/* The plain look has none of them. */
	server = surface->client->server;
	if (!server->glass)
		return 0;

	/* The shell's request: dock, undock or hide. */
	request = ZWL_TOPLEVEL_MINIMIZE;
	if (opcode == TOPLEVEL_SET_MAXIMIZED)
		request = ZWL_TOPLEVEL_MAXIMIZE;
	if (opcode == TOPLEVEL_UNSET_MAXIMIZED)
		request = ZWL_TOPLEVEL_UNMAXIMIZE;

	/* Succeeded: the shell carried it out. */
	zwl_glass_toplevel_request(server, surface, request);
	return 0;
}

/*
 * Tells whether a serial names the press the client was sent last, with a
 * button still held (1), or not (0).  Returns -1 when the seat is not one of
 * the client's.
 */
static int
press_held(
	struct zwl_client *client,
	uint32_t seat_id,
	uint32_t serial)
{
	struct zwl_server *server;
	struct zwl_object *seat;

	/* The seat must be the client's. */
	seat = zwl_find(client, seat_id);
	if (seat == NULL || seat->kind != ZWL_SEAT)
		return -1;

	/* A button must be down, and the serial must be of the press the client was sent last. */
	server = client->server;
	if (server->buttons_down == 0U)
		return 0;
	if (serial == 0U || serial != server->press_serial)
		return 0;

	/* Succeeded: the press is held. */
	return 1;
}

/*
 * Finds a window's extent in its image: its window geometry when the client
 * set one, else the whole image.
 */
static void
window_extent(
	const struct zwl_object *surface,
	int32_t *x,
	int32_t *y,
	int32_t *width,
	int32_t *height)
{
	uint32_t buffer_width;
	uint32_t buffer_height;

	/* The window geometry, when there is one. */
	if (surface->geometry_set) {
		*x = surface->geometry[0];
		*y = surface->geometry[1];
		*width = surface->geometry[2];
		*height = surface->geometry[3];
		return;
	}

	/* Else the whole image. */
	buffer_width = 0;
	buffer_height = 0;
	if (surface->current != NULL)
		zwl_buffer_size(surface->current, &buffer_width, &buffer_height);
	*x = 0;
	*y = 0;
	*width = (int32_t)buffer_width;
	*height = (int32_t)buffer_height;
}

/*
 * Keeps a size a resize asks for within the window's limits: at least
 * RESIZE_MINIMUM and the client's minimum, at most the client's maximum and
 * the output; dragged from the top, the title bar stays below the system bar.
 */
static void
resize_limit(
	struct zwl_server *server,
	struct zwl_object *surface,
	int32_t *width,
	int32_t *height)
{
	int32_t geometry_x;
	int32_t geometry_y;
	int32_t current_width;
	int32_t current_height;
	int32_t highest;

	/* No larger than the client can draw, nor than the output. */
	if (surface->max_width > 0 && *width > surface->max_width)
		*width = surface->max_width;
	if (surface->max_height > 0 && *height > surface->max_height)
		*height = surface->max_height;
	if (*width > (int32_t)server->width)
		*width = (int32_t)server->width;
	if (*height > (int32_t)server->height)
		*height = (int32_t)server->height;

	/* Dragged from the top, the window's top may go no higher than the highest a window may be. */
	if ((surface->resize_edges & EDGE_TOP) != 0U) {
		window_extent(surface, &geometry_x, &geometry_y, &current_width, &current_height);
		highest = surface->resize_bottom - geometry_y - window_lowest(server);
		if (*height > highest)
			*height = highest;
	}

	/* No smaller than the client can draw, nor than RESIZE_MINIMUM. */
	if (*width < surface->min_width)
		*width = surface->min_width;
	if (*height < surface->min_height)
		*height = surface->min_height;
	if (*width < RESIZE_MINIMUM)
		*width = RESIZE_MINIMUM;
	if (*height < RESIZE_MINIMUM)
		*height = RESIZE_MINIMUM;
}

/* Ends a finished resize's anchor: the window stays where its last image put it. */
static void
resize_settle(
	struct zwl_object *surface,
	int32_t width,
	int32_t height)
{
	/* No edge is kept in place any more. */
	surface->resize_edges = 0;

	/* The log line the tests read. */
	printf("ZWL RESIZE settled surface=%u x=%d y=%d width=%d height=%d\n", surface->id, surface->x, surface->y, width, height);
}

/* Tells the highest a window's image may be: under the glass look's system bar and title bar, or the output's top. */
static int32_t
window_lowest(
	const struct zwl_server *server)
{
	/* The plain look puts windows anywhere. */
	if (!server->glass)
		return 0;

	/* The glass look keeps the title bar below the system bar. */
	return ZWL_GLASS_BAR + ZWL_GLASS_GAP + ZWL_GLASS_TITLE;
}

/* Reads one native-endian protocol word. */
static uint32_t
toplevel_word(
	const unsigned char *bytes,
	size_t offset)
{
	uint32_t word;

	/* The payload need not be aligned. */
	memcpy(&word, bytes + offset, sizeof(word));

	/* Succeeded: the word. */
	return word;
}
