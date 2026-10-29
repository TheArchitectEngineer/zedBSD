/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The game mode (ws122-p005b, BUG-223, the 2026-10-06 user decision B): a
 * fullscreen window that says it shows a video or a game
 * (wp_content_type_v1, content-type.c), alone on the output, is shown
 * without composing -- its image goes straight to the display through the
 * backend's direct scanout (kl_backend_scanout_*, zedBSD's alone).  Every
 * other window, a fullscreen one that does not ask too, is composed with
 * libvulkan as before (display.c).
 *
 * Each pass the rules (scanout-rules.c) decide from the facts gathered
 * here.  To enter, the display thread asks the backend whether the
 * display can show the window, then the swapchain is closed in a change of
 * the display (display-thread.c: the frame in flight completes first,
 * libvulkan gives the display back) and the backend claims the display,
 * while window mode's passes go on;
 * each new image of the window is then presented, and when the present has
 * ended (the backend's thread makes it while the compositor takes the
 * clients' commits, so a mailbox client's replaced images go back at
 * once) the frame callbacks are done and the image it replaces let go.
 * One present is in flight at a time; the newest image goes next.  As
 * soon as the facts change --
 * something to draw over it (an edge's gesture, App Home, a menu, the
 * keyboard), the pointer moving, a screenshot asked, another size -- the
 * backend releases the display and window mode opens the swapchain again
 * (display.c), so the gesture's feedback and the pointer are drawn.  The
 * input goes to the compositor all along.
 *
 * Log: "KWL SCANOUT direct=1 surface=N client=C switch_ms=M" and
 * "KWL SCANOUT direct=0 reason=R surface=N frames=F" (also once for a
 * window that asks but is not shown straight, for each new reason).
 */

#include "desktop.h"
#include "compose.h"
#include "extras.h"
#include "menu.h"
#include "scanout-rules.h"

#include "userland/desktop/libkeiland-backend/keiland-backend-display.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static void scanout_facts(struct kwl_server *server, struct kwl_object *top, struct kwl_scanout_facts *facts);
static int scanout_overlay(struct kwl_server *server, struct kwl_object *top);
static void scanout_enter(struct kwl_server *server, struct kwl_object *top);
static void scanout_answered(struct kwl_server *server, int error, void *context);
static void scanout_parked(struct kwl_server *server, int error, void *context);
static void scanout_closed(struct kwl_server *server, int error, void *context);
static void scanout_frame(struct kwl_server *server, struct kwl_object *top);
static void scanout_refuse(struct kwl_server *server, struct kwl_object *top, int error);
static void scanout_callbacks(struct kwl_server *server, struct kwl_object *shown);
static void scanout_note(struct kwl_server *server, struct kwl_object *top, unsigned reason);

/*
 * Runs the game mode's part of a pass, after the commits were taken.
 * Returns 1 when the pass is the game mode's (the display shows the window
 * straight, nothing is composed), 0 when window mode draws it (the game
 * mode was left, or not entered).
 */
int
kwl_scanout_pass(
	struct kwl_server *server,
	struct kwl_object *top)
{
	struct kwl_scanout_facts facts;
	unsigned reason;

	/* The pass's facts and the rules' answer. */
	scanout_facts(server, top, &facts);
	reason = kwl_scanout_decide(&facts);

	/* In the game mode: the window's new image, or back to window mode. */
	if (server->scanout != NULL) {
		if (reason == KWL_SCANOUT_DIRECT && top == server->scanout_surface) {
			scanout_frame(server, top);
			return 1;
		}

		/* The facts changed: window mode draws from this pass. */
		kwl_scanout_leave(server, reason);
		return 0;
	}

	/* Not to be shown straight: window mode (the reason logged for a window that asked). */
	if (reason != KWL_SCANOUT_DIRECT) {
		scanout_note(server, top, reason);
		return 0;
	}

	/* One entry at a time, not while the display changes; a later pass tries again. */
	if (server->compose == NULL || server->scanout_entering != NULL || kwl_display_busy(server))
		return 0;

	/*
	 * Enters, while window mode draws on: the display thread asks whether
	 * the display can show it, then the swapchain is closed and the display
	 * claimed (scanout_answered); a later pass shows the first image
	 * straight.  A refusal stays window mode for this window.
	 */
	scanout_enter(server, top);
	return 0;
}

/*
 * Leaves the game mode for a reason (KWL_SCANOUT_*): the backend releases
 * the display, the image shown is let go, and window mode opens the
 * swapchain again on the next pass (server->windowed is 0).
 */
void
kwl_scanout_leave(
	struct kwl_server *server,
	unsigned reason)
{
	struct kwl_object *front;
	uint32_t surface_id;

	/* Not in it. */
	if (server->scanout == NULL)
		return;

	/* The display goes back once the present in flight has ended, then the images it showed. */
	kl_backend_scanout_close(server->scanout);
	server->scanout = NULL;
	front = server->scanout_front;
	server->scanout_front = NULL;
	kwl_buffer_put(front);
	front = server->scanout_flight;
	server->scanout_flight = NULL;
	kwl_buffer_put(front);

	/* The log, and window mode's frame from the next pass. */
	surface_id = 0U;
	if (server->scanout_surface != NULL)
		surface_id = server->scanout_surface->id;
	printf("KWL SCANOUT direct=0 reason=%s surface=%u frames=%llu\n", kwl_scanout_reason_name(reason), surface_id, (unsigned long long)server->scanout_frames);
	server->scanout_surface = NULL;
	server->scanout_reason = reason;
	server->dirty = 1;
}

/*
 * Forgets a surface that goes: the game mode it was shown in ends, and a
 * refusal of it is forgotten.
 */
void
kwl_scanout_surface_gone(
	struct kwl_server *server,
	struct kwl_object *surface)
{
	/* The window shown straight. */
	if (server->scanout != NULL && server->scanout_surface == surface)
		kwl_scanout_leave(server, KWL_SCANOUT_NO_WINDOW);

	/* A window being entered, a window the display refused, and the last one logged. */
	if (server->scanout_entering == surface)
		server->scanout_entering = NULL;
	if (server->scanout_refused == surface)
		server->scanout_refused = NULL;
	if (server->scanout_noted == surface)
		server->scanout_noted = NULL;
}

/* Gathers the facts of a pass for the rules. */
static void
scanout_facts(
	struct kwl_server *server,
	struct kwl_object *top,
	struct kwl_scanout_facts *facts)
{
	uint32_t width;
	uint32_t height;
	uint64_t now;

	/* The pointer's stillness: a move restarts it. */
	now = kwl_milliseconds();
	if (server->pointer_x != server->scanout_pointer[0] || server->pointer_y != server->scanout_pointer[1]) {
		server->scanout_pointer[0] = server->pointer_x;
		server->scanout_pointer[1] = server->pointer_y;
		server->scanout_motion_ms = now;
	}

	/* Nothing known yet; the window, if any. */
	memset(facts, 0, sizeof(*facts));
	facts->pointer_still_ms = now - server->scanout_motion_ms;
	if (top == NULL)
		return;
	facts->window = 1U;
	facts->fullscreen = top->fullscreen;
	facts->content_type = top->content_type;

	/* The output's rotation: the display hardware's 180, the compositor's turn (WS217). */
	if (server->compose != NULL) {
		facts->scanout_rotation = server->compose->scanout_rotation;
		facts->turn_rotation = server->compose->turn_rotation;
	}

	/* Its image: a client's GPU buffer (not shared memory) of the output's size, shown whole. */
	if (top->current != NULL && top->current->shm == NULL)
		facts->gpu_buffer = 1U;
	kwl_surface_size(top, &width, &height);
	if (width == server->width && height == server->height && top->source[2] <= 0 && top->destination[0] <= 0)
		facts->size_matches = 1U;

	/* What may be drawn over it, a screenshot waiting, a refusal of it. */
	facts->overlay = (unsigned)scanout_overlay(server, top);
	facts->shot = (unsigned)kwl_shot_waiting();
	if (server->scanout_refused == top)
		facts->refused = 1U;
}

/*
 * Tells whether anything is drawn over a fullscreen window: the edges'
 * gestures and what they show (App Home, Wiseview, the keyboard, the
 * corner's hint), the switcher, a menu, the network's and the volume's
 * popups, the power dialog, the lock, the system bar, an animation or a
 * transition, and any popup or sub-surface shown.
 */
static int
scanout_overlay(
	struct kwl_server *server,
	struct kwl_object *top)
{
	struct kwl_client *client;
	struct kwl_object *surface;
	int showing;

	/* The edges' gestures and what they show (shell.c). */
	showing = kwl_glass_overlay(server);
	if (showing)
		return 1;

	/* The compositor's own popups and dialogs. */
	showing = kwl_menu_is_open();
	if (showing)
		return 1;
	showing = kwl_network_is_open();
	if (showing)
		return 1;
	showing = kwl_bluetooth_is_open();
	if (showing)
		return 1;
	showing = kwl_bluetooth_ask_showing();
	if (showing)
		return 1;
	showing = kwl_volume_is_open();
	if (showing)
		return 1;
	showing = kwl_status_panel_is_open();
	if (showing)
		return 1;
	showing = kwl_power_dialog_showing(server);
	if (showing)
		return 1;

	/* The switcher, the lock, the system bar shown, an animation or a transition. */
	if (server->switcher.on || server->locked || server->anim != NULL || server->transition != NULL)
		return 1;
	if (server->glass && !server->bar_hidden)
		return 1;

	/* Any client's popup or sub-surface with an image (a menu of its own, an input method's candidates). */
	for (client = server->clients; client != NULL; client = client->next) {
		if (client->fatal)
			continue;
		for (surface = client->objects; surface != NULL; surface = surface->next) {
			/* A shown surface that is not the window itself. */
			if (surface->kind != KWL_SURFACE || surface->dead || surface == top || surface->current == NULL)
				continue;
			if (surface->sub_role != NULL)
				return 1;
			if (surface->role != NULL && surface->role->top != NULL && surface->role->top->kind == KWL_POPUP)
				return 1;
		}
	}

	/* Nothing over it. */
	return 0;
}

/*
 * Enters the game mode for a window (design 4.3): the display thread asks
 * the backend first, without claiming, whether the display can show it
 * straight; scanout_answered goes on with the answer.
 */
static void
scanout_enter(
	struct kwl_server *server,
	struct kwl_object *top)
{
	/* The question, for the display's mode (the physical size, WS217). */
	server->scanout_entering = top;
	server->scanout_entering_ms = kwl_milliseconds();
	(void)kwl_display_scanout_query(server, server->compose->mode_width, server->compose->mode_height, scanout_answered, NULL);
}

/*
 * Goes on entering the game mode with the backend's answer: a display that
 * cannot show the window straight (the Raspberry Pi 4's, Linux, FreeBSD)
 * keeps the output open, as closing and opening it again for nothing
 * stopped the Pi 4's firmware video decoder (ws236-p011).  One that can
 * closes the swapchain in a change of the display (libvulkan gives the
 * display back), if the window is still to be shown straight.
 */
static void
scanout_answered(
	struct kwl_server *server,
	int error,
	void *context)
{
	struct kwl_scanout_facts facts;
	struct kwl_object *top;
	unsigned reason;

	/* The window went meanwhile. */
	(void)context;
	top = server->scanout_entering;
	if (top == NULL)
		return;

	/* Refused: the window stays composed until it goes or asks again. */
	if (error != 0) {
		server->scanout_entering = NULL;
		server->scanout_refused = top;
		server->dirty = 1;
		printf("KWL SCANOUT direct=0 reason=backend errno=%d surface=%u client=%llu output=kept\n", error, top->id, (unsigned long long)top->client->number);
		return;
	}

	/* The facts as they are now: a window no longer to be shown straight stays composed. */
	scanout_facts(server, top, &facts);
	reason = kwl_scanout_decide(&facts);
	if (reason != KWL_SCANOUT_DIRECT || top != kwl_top_window(server)) {
		server->scanout_entering = NULL;
		return;
	}

	/* The swapchain goes in a change of the display; one running already is waited for by a later pass. */
	error = kwl_display_begin(server, scanout_parked, NULL);
	if (error != 0)
		server->scanout_entering = NULL;
}

/* Entering the game mode, the render thread parked: the swapchain closes. */
static void
scanout_parked(
	struct kwl_server *server,
	int error,
	void *context)
{
	/* The close. */
	(void)error;
	kwl_display_close(server, scanout_closed, context);
}

/*
 * Entering the game mode, the swapchain closed: the backend claims the
 * display for its mode (the physical size, WS217), and the change ends.  A
 * window gone meanwhile, or a claim's failure, leaves window mode to open
 * the swapchain again at the next pass.
 */
static void
scanout_closed(
	struct kwl_server *server,
	int error,
	void *context)
{
	struct kwl_object *top;

	/* Window mode opens again from the next pass unless the claim holds. */
	(void)error;
	(void)context;
	top = server->scanout_entering;
	server->scanout_entering = NULL;
	server->windowed = 0;
	if (top == NULL) {
		kwl_display_end(server);
		return;
	}

	/* The claim. */
	error = kl_backend_scanout_open(server->backend, server->compose->mode_width, server->compose->mode_height, &server->scanout);
	if (error != 0) {
		server->scanout = NULL;
		server->scanout_refused = top;
		server->dirty = 1;
		printf("KWL SCANOUT direct=0 reason=backend errno=%d surface=%u client=%llu\n", error, top->id, (unsigned long long)top->client->number);
		kwl_display_end(server);
		return;
	}

	/* Succeeded: the window is shown straight from the next pass. */
	server->scanout_surface = top;
	server->scanout_frames = 0U;
	server->scanout_noted = NULL;
	printf("KWL SCANOUT direct=1 surface=%u client=%llu switch_ms=%llu\n", top->id, (unsigned long long)top->client->number, (unsigned long long)(kwl_milliseconds() - server->scanout_entering_ms));
	kwl_display_end(server);
}

/*
 * Shows a window's new image straight: its present begun, unless one is in
 * flight (its end shows the newest image next).  A present that cannot
 * begin leaves the game mode.
 */
static void
scanout_frame(
	struct kwl_server *server,
	struct kwl_object *top)
{
	struct kwl_object *buffer;
	int error;

	/* Input goes to the window shown. */
	server->front_surface = kwl_desktop_front(server, top);
	kwl_seat_focus(server);

	/* One present at a time. */
	if (server->scanout_flight != NULL)
		return;

	/* Only a new image is presented. */
	buffer = top->current;
	if (buffer == NULL || (buffer == server->scanout_front && !top->fresh))
		return;

	/* The image to the display's thread; a failure is the backend's refusal from now. */
	error = kl_backend_scanout_begin(server->scanout, kwl_gpu_host(), kwl_gpu_resource(buffer));
	if (error != 0) {
		scanout_refuse(server, top, error);
		return;
	}

	/* The present holds the image until it has ended. */
	kwl_buffer_get(buffer);
	server->scanout_flight = buffer;
	top->fresh = 0;
	server->dirty = 0;
	server->damaged = 0;
}

/*
 * Reports the fd that becomes readable when the game mode's present in
 * flight has ended (for the main loop's poll), -1 when none is.
 */
int
kwl_scanout_fd(
	struct kwl_server *server)
{
	/* Only in the game mode, with a present in flight. */
	if (server->scanout == NULL || server->scanout_flight == NULL)
		return -1;

	/* Succeeded: the backend's fd. */
	return kl_backend_scanout_fd(server->scanout);
}

/*
 * Takes the game mode's present once it has ended: the display holds its
 * image, the one it replaces is let go and the frame callbacks are done
 * (the next pass shows a newer image).  A present that failed leaves the
 * game mode.
 */
void
kwl_scanout_ended(
	struct kwl_server *server)
{
	struct kwl_object *previous;
	int result;
	int ended;

	/* Only a present in flight that has ended. */
	if (server->scanout == NULL || server->scanout_flight == NULL)
		return;
	result = 0;
	ended = kl_backend_scanout_end(server->scanout, &result);
	if (!ended)
		return;

	/* A failure is the backend's refusal from now. */
	if (result != 0) {
		scanout_refuse(server, server->scanout_surface, result);
		return;
	}

	/* The display holds the image; the one it replaces is let go. */
	previous = server->scanout_front;
	server->scanout_front = server->scanout_flight;
	server->scanout_flight = NULL;
	kwl_buffer_put(previous);
	server->scanout_frames++;

	/* The window draws its next frame; the hidden windows are not kept waiting. */
	scanout_callbacks(server, server->scanout_surface);
}

/* Leaves the game mode because the display refused a window's image: the window stays composed until it goes or asks again. */
static void
scanout_refuse(
	struct kwl_server *server,
	struct kwl_object *top,
	int error)
{
	/* Logged, then window mode. */
	printf("KWL SCANOUT present errno=%d surface=%u\n", error, top != NULL ? top->id : 0U);
	server->scanout_refused = top;
	kwl_scanout_leave(server, KWL_SCANOUT_REFUSED);
}

/* Does the frame callbacks of every surface: the one shown, and the others the game mode hides. */
static void
scanout_callbacks(
	struct kwl_server *server,
	struct kwl_object *shown)
{
	struct kwl_client *client;
	struct kwl_object *surface;

	/* Every surface of every live client. */
	(void)shown;
	for (client = server->clients; client != NULL; client = client->next) {
		if (client->fatal)
			continue;
		for (surface = client->objects; surface != NULL; surface = surface->next) {
			if (surface->kind == KWL_SURFACE)
				kwl_callbacks_done(&surface->committed_callbacks);
		}
	}
}

/* Logs, once for each new reason, why a fullscreen window that asks for the game mode is composed. */
static void
scanout_note(
	struct kwl_server *server,
	struct kwl_object *top,
	unsigned reason)
{
	/* Only a fullscreen window that asks. */
	if (top == NULL || !top->fullscreen)
		return;
	if (top->content_type != KWL_SCANOUT_CONTENT_VIDEO && top->content_type != KWL_SCANOUT_CONTENT_GAME)
		return;

	/* The same window and reason as last time. */
	if (server->scanout_noted == top && server->scanout_reason == reason)
		return;

	/* Logged. */
	server->scanout_noted = top;
	server->scanout_reason = reason;
	printf("KWL SCANOUT direct=0 reason=%s surface=%u frames=0\n", kwl_scanout_reason_name(reason), top->id);
}
