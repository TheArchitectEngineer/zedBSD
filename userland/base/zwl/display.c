/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Independent GPU import and completed shared-allocation scanout selection.
 */

#include "zwl.h"
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static int claim_display(struct zwl_server *server);

/*
 * Opens the compositor's independent renderer context and queries its display.
 */
int
zwl_gpu_open(
	struct zwl_server *server)
{
	struct gpu_info information;
	struct gpu_display_mode mode;
	int error;

	/* The compositor obtains its own GPU fd; no producer session fd is accepted. */
	server->gpu = open(server->gpu_path, O_RDWR | O_CLOEXEC);
	if (server->gpu < 0)
		return errno;

	/* Shared images require both typed import and native blob scanout support. */
	memset(&information, 0, sizeof(information));
	information.version = GPU_ABI_VERSION;
	information.size = sizeof(information);
	error = ioctl(server->gpu, GPU_GET_INFO, &information);
	if (error != 0)
		return errno;

	/* Do not fall back to CPU copies when shared display support is absent. */
	if ((information.capabilities & (GPU_CAP_SHARE | GPU_CAP_DISPLAY)) != (GPU_CAP_SHARE | GPU_CAP_DISPLAY))
		return ENOTSUP;

	/* Select the first native display without claiming it during idle service startup. */
	memset(&server->display, 0, sizeof(server->display));
	server->display.version = GPU_ABI_VERSION;
	server->display.size = sizeof(server->display);
	error = ioctl(server->gpu, GPU_DISPLAY_QUERY, &server->display);
	if (error != 0)
		return errno;

	/* Blob support is explicit rather than inferred from ordinary pixel presentation. */
	if ((server->display.flags & (GPU_DISPLAY_CONNECTED | GPU_DISPLAY_BLOB)) != (GPU_DISPLAY_CONNECTED | GPU_DISPLAY_BLOB))
		return ENOTSUP;

	/* Validate the configured fullscreen geometry without changing active scanout. */
	memset(&mode, 0, sizeof(mode));
	mode.version = GPU_ABI_VERSION;
	mode.size = sizeof(mode);
	mode.display_id = server->display.display_id;
	mode.generation = server->display.generation;
	mode.operation = GPU_DISPLAY_MODE_VALIDATE;
	mode.width = server->width;
	mode.height = server->height;
	mode.refresh_millihz = 0;
	error = ioctl(server->gpu, GPU_DISPLAY_MODE, &mode);
	if (error != 0)
		return errno;

	/* The driver resolves the selected extent to its own supported refresh. */
	server->refresh = mode.refresh_millihz;
	if (server->refresh == 0)
		return EINVAL;

	/* The machine log distinguishes this consumer context from producer imports. */
	printf("ZWL GPU fd=%d driver=%s display=%u generation=%llu width=%u height=%u\n", server->gpu, information.driver_name, server->display.display_id, (unsigned long long)server->display.generation, server->width, server->height);

	/* Succeeded: the independent compositor session supports its selected output. */
	return 0;
}

/*
 * Imports a typed capability and compares authoritative metadata with its wire claim.
 */
int
zwl_gpu_import(
	struct zwl_object *buffer,
	int descriptor,
	const struct gpu_image_descriptor *image)
{
	int error;
	int mismatch;

	/* K accepts only fd input and supplies every native identity and metadata field. */
	memset(&buffer->image, 0, sizeof(buffer->image));
	buffer->image.version = GPU_ABI_VERSION;
	buffer->image.size = sizeof(buffer->image);
	buffer->image.fd = descriptor;
	error = ioctl(buffer->client->server->gpu, GPU_RESOURCE_IMPORT, &buffer->image);
	if (error != 0)
		return errno;

	/* Userspace metadata cannot reinterpret a capability's storage or device identity. */
	mismatch = memcmp(&buffer->image.image, image, sizeof(*image));
	if (mismatch != 0)
		return EINVAL;

	/* This full-screen compositor presents only its negotiated geometry. */
	if (image->width != buffer->client->server->width || image->height != buffer->client->server->height)
		return EINVAL;

	/* Import reports the native allocation identity shared across independent contexts. */
	printf("ZWL IMPORT client=%llu buffer=%u gpu_fd=%d resource=%u handle=%llu device=%llu bytes=%llu\n", (unsigned long long)buffer->client->number, buffer->id, buffer->client->server->gpu, buffer->image.resource_id, (unsigned long long)buffer->image.handle, (unsigned long long)image->device_id, (unsigned long long)image->allocation_bytes);

	/* Succeeded: this wl_buffer owns a distinct consumer resource handle. */
	return 0;
}

/*
 * Releases the display lease before withdrawing the previous front-image hold.
 */
int
zwl_unscan(
	struct zwl_server *server)
{
	struct gpu_display_release release;
	struct zwl_client *client;
	struct zwl_object *front;
	int error;

	/* An idle compositor has no scanout allocation to retire. */
	if (server->lease == 0)
		return 0;

	/* Only a successful release proves the hardware no longer borrows this image. */
	memset(&release, 0, sizeof(release));
	release.version = GPU_ABI_VERSION;
	release.size = sizeof(release);
	release.lease = server->lease;
	error = ioctl(server->gpu, GPU_DISPLAY_RELEASE, &release);
	if (error != 0) {
		error = errno;
		printf("ZWL GPU_ERROR operation=release errno=%d\n", error);
		server->failed = 1;

		/* Closing the owning session lets K retain any uncertain hardware ownership. */
		close(server->gpu);
		server->gpu = -1;
	}

	/* Stop issuing reuse events after an uncertain release; every connection will close. */
	if (server->failed) {
		/* Every producer must stop receiving reuse events after uncertain native retirement. */
		for (client = server->clients; client != NULL; client = client->next)
			client->fatal = 1;
	}

	/* The hardware lease or the entire owning fd has now been withdrawn. */
	server->lease = 0;
	front = server->front;
	server->front = NULL;
	server->front_surface = NULL;
	zwl_buffer_put(front);

	/* A display without a front surface gives no client input focus. */
	zwl_seat_focus(server);

	/* Reports a hardware release failure without announcing safe producer reuse. */
	if (error != 0)
		return error;

	/* Succeeded: no front image is retained by this display lease. */
	return 0;
}

/*
 * Presents a completed producer image and releases the displaced image afterward.
 */
int
zwl_present(
	struct zwl_object *surface)
{
	struct zwl_server *server;
	struct zwl_object *buffer;
	struct zwl_object *previous;
	struct gpu_display_present present;
	struct gpu_image_descriptor *image;
	int error;

	/* Null committed attachment unmaps this surface instead of presenting pixels. */
	server = surface->client->server;
	buffer = surface->queued;
	if (buffer == NULL) {
		/* Unmapping another client's hidden surface must not withdraw the current front. */
		if (server->front_surface == surface) {
			/* Native release completes before the previous mapped content loses its hold. */
			error = zwl_unscan(server);
			if (error != 0)
				return error;
		}

		/* No pending content can retain the previous mapped surface image. */
		zwl_buffer_put(surface->current);
		surface->current = NULL;
		surface->ready = 0;
		zwl_callbacks_done(&surface->committed_callbacks);
		return 0;
	}

	/* Display ownership belongs to this consumer open alone. */
	error = claim_display(server);
	if (error != 0)
		return error;

	/* GPU-resident linear storage is sent directly to SET_SCANOUT_BLOB. */
	image = &buffer->image.image;
	memset(&present, 0, sizeof(present));
	present.version = GPU_ABI_VERSION;
	present.size = sizeof(present);
	present.lease = server->lease;
	present.handle = buffer->image.handle;
	present.offset = image->offset;
	present.frame = server->frame + 1U;
	present.width = image->width;
	present.height = image->height;
	present.stride = image->stride;
	present.format = image->format;
	present.refresh_millihz = server->refresh;
	present.flags = GPU_DISPLAY_PRESENT_FIFO | GPU_DISPLAY_PRESENT_BLOB;
	present.generation = server->display.generation;
	error = ioctl(server->gpu, GPU_DISPLAY_PRESENT, &present);
	if (error != 0)
		return errno;

	/* Successful fenced selection is the first point when the previous front may be released. */
	previous = server->front;
	zwl_buffer_get(buffer);
	server->front = buffer;
	server->front_surface = surface;
	server->frame++;
	zwl_buffer_put(previous);

	/* The completed commit replaces surface content independently from global scanout. */
	previous = surface->current;
	surface->current = buffer;
	surface->queued = NULL;
	surface->ready = 0;
	zwl_buffer_put(previous);
	printf("ZWL PRESENT client=%llu surface=%u buffer=%u resource=%u frame=%llu sequence=%llu width=%u height=%u flags=%u refresh=%u\n", (unsigned long long)surface->client->number, surface->id, buffer->id, buffer->image.resource_id, (unsigned long long)server->frame, (unsigned long long)present.sequence, image->width, image->height, present.flags, present.refresh_millihz);

	/* Input follows the surface that is now on the display. */
	zwl_seat_focus(server);

	/* Frame callbacks pace FIFO clients but do not release the newly selected front buffer. */
	zwl_callbacks_done(&surface->committed_callbacks);

	/* Succeeded: both the surface and hardware own the completed current image. */
	return 0;
}

/*
 * Presents queued commits fairly after the current batch of client requests is decoded.
 */
void
zwl_schedule(
	struct zwl_server *server)
{
	struct zwl_client *client;
	struct zwl_object *surface;
	struct zwl_object *chosen;
	int error;

	/* A later same-surface commit has already replaced any unpresented mailbox image. */
	chosen = NULL;
	for (client = server->clients; client != NULL; client = client->next) {
		/* A failed connection cannot submit new display ownership. */
		if (client->fatal)
			continue;

		/* Choose the oldest remaining surface commit across independent clients. */
		for (surface = client->objects; surface != NULL; surface = surface->next) {
			/* Only a live surface with committed content participates in scheduling. */
			if (surface->kind != ZWL_SURFACE ||
			    !surface->ready ||
			    surface->dead)
				continue;

			/* A single presentation per event-loop pass bounds scheduling latency. */
			if (chosen == NULL || surface->commit_order < chosen->commit_order)
				chosen = surface;
		}
	}

	/* An idle event-loop pass performs no GPU operation. */
	if (chosen == NULL)
		return;

	/* Failed presentation leaves front ownership intact until disconnect cleanup. */
	error = zwl_present(chosen);
	if (error != 0) {
		printf("ZWL GPU_ERROR operation=present errno=%d\n", error);
		(void)zwl_error(chosen->client, chosen->id, "GPU presentation failed");
	}

	/* Succeeded: the selected commit was presented or its client was marked for cleanup. */
	return;
}

/* Claims an idle display lease while preserving its generation and exclusive owner. */
static int
claim_display(
	struct zwl_server *server)
{
	struct gpu_display_claim claim;
	int error;

	/* Successive frames share the compositor's existing reservation. */
	if (server->lease != 0)
		return 0;

	/* Claiming does not transfer authority from any client GPU session. */
	memset(&claim, 0, sizeof(claim));
	claim.version = GPU_ABI_VERSION;
	claim.size = sizeof(claim);
	claim.display_id = server->display.display_id;
	claim.generation = server->display.generation;
	error = ioctl(server->gpu, GPU_DISPLAY_CLAIM, &claim);
	if (error != 0)
		return errno;

	/* The nonzero lease remains owned until explicit release or GPU fd close. */
	server->lease = claim.lease;

	/* Succeeded: only this compositor session may select the display's front image. */
	return 0;
}
