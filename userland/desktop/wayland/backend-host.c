/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The seat's callbacks of libkeiland-backend (ws131-p006): what the
 * compositor does when the seat takes the display and the input devices
 * away (another session has the display) and gives them back; and the
 * system's events (ws132-p003): input devices that came or went, the
 * power's changes, the buttons and the lid.
 *
 * The backend calls these from kl_backend_poll_done, and the input
 * devices' two from kl_backend_input_scan; they change the compositor's
 * state only and never call back into the backend (the seat tells its
 * service after they return, and the scan closes a device not kept).  Inputs are known by their device
 * path, which stays the same while their descriptors change.
 */

#include "userland/desktop/wayland/zwl.h"

#include <stdio.h>
#include <string.h>

static struct zwl_input_device *backend_input(struct zwl_server *server, const char *path);
static const char *power_source_text(unsigned source);

/*
 * The seat is paused: drawing stops and the output closes now (Vulkan's
 * duplicate of the primary node goes before the seat returns it).
 */
void
zwl_backend_session_paused(
	void *data)
{
	struct zwl_server *server;

	/* No frame and no new input device from now on. */
	server = data;
	server->os_paused = 1;

	/* The frame in flight finishes and the output closes. */
	if (server->compose != NULL) {
		zwl_compose_quiesce(server);
		zwl_compose_output_close(server);
	}

	/* The next activation makes a new output rather than reusing this one. */
	server->windowed = 0;
	printf("ZWL SEAT paused\n");
}

/*
 * The seat is active again: the next frame opens the output, and the next
 * scan finds the input devices.
 */
void
zwl_backend_session_resumed(
	void *data)
{
	struct zwl_server *server;

	/* The ordinary scheduler opens the output and scans the inputs. */
	server = data;
	server->os_paused = 0;
	server->input_scan_time = 0;
	server->windowed = 0;
	server->dirty = 1;
	printf("ZWL SEAT resumed\n");
}

/*
 * One input is paused: it is not read until it resumes (its descriptor
 * stays the seat's).
 */
void
zwl_backend_input_paused(
	void *data,
	const char *path)
{
	struct zwl_input_device *input;

	/* A node the compositor did not keep has nothing to stop. */
	input = backend_input(data, path);
	if (input == NULL)
		return;
	input->fd = -1;
}

/*
 * One input resumes on a new descriptor; a partial report of the old one
 * does not enter it.
 */
void
zwl_backend_input_resumed(
	void *data,
	const char *path,
	int descriptor)
{
	struct zwl_input_device *input;

	/* A node the compositor did not keep has nothing to resume. */
	input = backend_input(data, path);
	if (input == NULL)
		return;
	input->fd = descriptor;
	input->frame_count = 0;
	input->discarding = 0;
}

/*
 * One input is gone: it is forgotten (its descriptor was the seat's and is
 * closed).
 */
void
zwl_backend_input_gone(
	void *data,
	const char *path)
{
	struct zwl_input_device *input;

	/* A node the compositor did not keep has nothing to forget. */
	input = backend_input(data, path);
	if (input == NULL)
		return;
	zwl_input_forget(data, input);
}

/*
 * Tells whether the compositor already reads the device at path (the
 * backend's scan leaves it alone).
 */
int
zwl_backend_input_known(
	void *data,
	const char *path)
{
	struct zwl_input_device *input;

	/* A live input of that path. */
	input = backend_input(data, path);
	return input != NULL;
}

/*
 * Classifies a device the backend's scan opened: 1 when the seat keeps it
 * (and owns its descriptor), 0 when the backend is to close it.
 */
int
zwl_backend_input_found(
	void *data,
	int descriptor,
	const char *path,
	const struct kl_backend_input_caps *caps)
{
	int kept;

	/* The seat's classification (input.c). */
	kept = zwl_input_probe(data, descriptor, path, caps);
	return kept;
}

/*
 * An input device came or went (ws132-p003): the devices are scanned again
 * in the event loop's next pass instead of at the next ZWL_INPUT_SCAN_MS.
 */
void
zwl_backend_input_changed(
	void *data)
{
	struct zwl_server *server;

	/* The next pass scans (main.c compares the time of the last scan). */
	server = data;
	server->input_scan_time = 0;
	printf("ZWL EVENT input changed\n");
}

/*
 * The AC adapter or a battery changed (ws132-p003): the power is read
 * again for the bar, and the system extension reads it again for its
 * clients.
 */
void
zwl_backend_power_changed(
	void *data)
{
	struct zwl_server *server;

	/* The bar's state, then the extension's. */
	server = data;
	zwl_power_read(server);
	zwl_system_power_changed(server);
	zwl_schedule(server);
}

/*
 * A power or sleep button was pressed (ws132-p003).  What it does waits
 * for the decision D1 (ws132-p008); it is only written in the log.
 */
void
zwl_backend_power_button(
	void *data,
	unsigned button)
{
	/* Only the log, until p008. */
	(void)data;
	if (button == KL_BACKEND_BUTTON_SLEEP) {
		printf("ZWL EVENT sleep button\n");
		return;
	}

	/* The power button. */
	printf("ZWL EVENT power button\n");
}

/*
 * The lid opened or closed (ws132-p003).  What it does waits for the
 * decision D2 (ws132-p008); it is only written in the log.
 */
void
zwl_backend_lid_changed(
	void *data,
	unsigned open)
{
	/* Only the log, until p008. */
	(void)data;
	if (open != 0U) {
		printf("ZWL EVENT lid open\n");
		return;
	}

	/* Closed. */
	printf("ZWL EVENT lid closed\n");
}

/*
 * Reads the power's state for the bar: unknown (no battery shown) when the
 * backend did not open or cannot say.
 */
void
zwl_power_read(
	struct zwl_server *server)
{
	struct kl_backend_power_state state;
	int error;

	/* Unknown unless the backend says. */
	memset(&state, 0, sizeof(state));
	state.source = KL_BACKEND_POWER_SOURCE_UNKNOWN;
	state.percent = -1;
	error = kl_backend_power_get_state(server->backend, &state);
	if (error != 0) {
		state.source = KL_BACKEND_POWER_SOURCE_UNKNOWN;
		state.percent = -1;
		state.charging = 0U;
	}

	/* Kept for the bar, and written in the log. */
	server->power = state;
	printf("ZWL POWER source=%s percent=%d charging=%u\n", power_source_text(state.source), state.percent,
	       state.charging);
}

/* Names a power source for the log. */
static const char *
power_source_text(
	unsigned source)
{
	/* The two the kernel tells, and the rest. */
	if (source == KL_BACKEND_POWER_SOURCE_AC)
		return "ac";
	if (source == KL_BACKEND_POWER_SOURCE_BATTERY)
		return "battery";

	/* Succeeded: not known. */
	return "unknown";
}

/* Finds the input the compositor keeps for a device path, or NULL. */
static struct zwl_input_device *
backend_input(
	struct zwl_server *server,
	const char *path)
{
	unsigned index;
	int same;

	/* The path stays while the descriptors change. */
	for (index = 0; index < ZWL_INPUT_MAX; index++) {
		if (server->inputs[index].live == 0)
			continue;
		same = strcmp(server->inputs[index].path, path);
		if (same == 0)
			return &server->inputs[index];
	}

	/* No input of that path. */
	return NULL;
}
