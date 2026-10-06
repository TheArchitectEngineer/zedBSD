/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The direct scanout on zedBSD (ws122-p005b, keiland-backend-display.h):
 * the compositor's game mode shows a client's GPU buffer on the display
 * without composing it, through the kernel's display ioctls -- the old
 * fullscreen mode's (removed from the compositor in ws099-p015), moved
 * here by the 2026-10-06 user decision B as the Guardrail's one exception
 * to "the compositor uses only libvulkan".  Besides gpu-zedbsd.c this is
 * the only file of the desktop that reads the kernel's GPU types, and the
 * only one that calls GPU ioctls (plan/tools/gpu-boundary/v1-check.sh).
 *
 * A scanout opens its own GPU fd, finds the first connected display that
 * shows shared images (GPU_DISPLAY_BLOB), checks the output's size and
 * takes its refresh (GPU_DISPLAY_MODE), and claims it (the compositor has
 * closed its swapchain, so libvulkan no longer holds it).  A buffer is
 * imported once into the scanout's fd (GPU_RESOURCE_IMPORT, from the
 * backend's record of the buffer, gpu-buffer-zedbsd.c) and presented as it
 * is (GPU_DISPLAY_PRESENT, FIFO and BLOB).  Closing releases the display
 * (GPU_DISPLAY_RELEASE) and the fd, which retires every imported handle.
 *
 * One scanout is open at a time (the compositor has one output).
 */

#include "userland/desktop/libkeiland-backend/keiland-backend-display.h"
#include "userland/desktop/libkeiland-backend/keiland-backend-gpu.h"
#include "userland/desktop/libkeiland-backend-zedbsd/gpu-zedbsd.h"

#include <uapi/gpu.h>
#include <uapi/gpu-display.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* The GPU device the scanout opens. */
#define SCANOUT_DEVICE		"/dev/gpu0"

/*
 * An open scanout: its GPU fd, the display and its generation, the lease
 * the claim gave, the output's size and refresh, the frames presented, and
 * the number of the claim (which tells a buffer's handle is of this one).
 */
struct kl_backend_scanout {
	int gpu;
	uint32_t display_id;
	uint64_t generation;
	uint64_t lease;
	uint32_t width;
	uint32_t height;
	uint32_t refresh;
	uint64_t frame;
	uint64_t claim;
};

/* The scanout open now, for the buffers that go meanwhile, and the number of the last claim. */
static struct kl_backend_scanout *scanout_open;
static uint64_t scanout_claims;

static int scanout_display(struct kl_backend_scanout *scanout);
static int scanout_import(struct kl_backend_scanout *scanout, struct zwl_gpu_buffer_record *record);

/*
 * Claims the display for an output of a size.  Returns 0, EBUSY while
 * another scanout is open, ENOTSUP when no display shows shared images,
 * or the failure of the GPU device or the claim.
 */
int
kl_backend_scanout_open(
	struct kl_backend *backend,
	uint32_t width,
	uint32_t height,
	struct kl_backend_scanout **scanout)
{
	struct kl_backend_scanout *opened;
	struct gpu_display_claim claim;
	int error;

	/* One at a time. */
	(void)backend;
	if (scanout == NULL || width == 0U || height == 0U)
		return EINVAL;
	if (scanout_open != NULL)
		return EBUSY;

	/* The record and its own GPU fd. */
	opened = calloc(1, sizeof(*opened));
	if (opened == NULL)
		return ENOMEM;
	opened->width = width;
	opened->height = height;
	opened->gpu = open(SCANOUT_DEVICE, O_RDWR | O_CLOEXEC);
	if (opened->gpu < 0) {
		error = errno;
		free(opened);
		return error;
	}

	/* The display, its size checked and its refresh. */
	error = scanout_display(opened);
	if (error != 0) {
		close(opened->gpu);
		free(opened);
		return error;
	}

	/* The claim, which the compositor's closed swapchain left free. */
	memset(&claim, 0, sizeof(claim));
	claim.version = GPU_ABI_VERSION;
	claim.size = sizeof(claim);
	claim.display_id = opened->display_id;
	claim.generation = opened->generation;
	error = ioctl(opened->gpu, GPU_DISPLAY_CLAIM, &claim);
	if (error != 0) {
		error = errno;
		close(opened->gpu);
		free(opened);
		return error;
	}

	/* Succeeded: the display is the scanout's. */
	opened->lease = claim.lease;
	scanout_claims++;
	opened->claim = scanout_claims;
	scanout_open = opened;
	*scanout = opened;
	return 0;
}

/*
 * Shows a client's GPU buffer as the whole output.  Returns 0, EINVAL for
 * a buffer without the backend's record or of another size, or the
 * failure of its import or of the present.
 */
int
kl_backend_scanout_present(
	struct kl_backend_scanout *scanout,
	const struct kl_backend_protocol_host *host,
	struct kl_backend_resource *buffer)
{
	struct zwl_gpu_buffer_record *record;
	struct gpu_display_present present;
	struct gpu_image_descriptor image;
	unsigned role;
	void **owned;
	int error;

	/* A GPU buffer with the backend's record. */
	if (scanout == NULL || host == NULL || buffer == NULL)
		return EINVAL;
	role = host->resource_role(buffer);
	if (role != KL_BACKEND_ROLE_BUFFER)
		return EINVAL;
	owned = host->resource_private(buffer);
	if (owned == NULL || *owned == NULL)
		return EINVAL;
	record = *owned;

	/* The output's size, and a description the kernel's record holds. */
	if (record->width != scanout->width || record->height != scanout->height)
		return EINVAL;
	if (record->description_size != sizeof(image))
		return EINVAL;

	/* Imported into the scanout's fd once. */
	if (record->handle == 0U || record->claim != scanout->claim) {
		error = scanout_import(scanout, record);
		if (error != 0)
			return error;
	}

	/* The image as it is: its rows, its format, blob scanout at the display's pace. */
	memcpy(&image, record->description, sizeof(image));
	memset(&present, 0, sizeof(present));
	present.version = GPU_ABI_VERSION;
	present.size = sizeof(present);
	present.lease = scanout->lease;
	present.handle = record->handle;
	present.offset = image.offset;
	present.frame = scanout->frame + 1U;
	present.width = image.width;
	present.height = image.height;
	present.stride = image.stride;
	present.format = image.format;
	present.refresh_millihz = scanout->refresh;
	present.flags = GPU_DISPLAY_PRESENT_FIFO | GPU_DISPLAY_PRESENT_BLOB;
	present.generation = scanout->generation;
	error = ioctl(scanout->gpu, GPU_DISPLAY_PRESENT, &present);
	if (error != 0)
		return errno;

	/* Succeeded: the display shows the buffer. */
	scanout->frame++;
	return 0;
}

/*
 * Releases the display and the scanout's fd (with every handle imported
 * into it).
 */
void
kl_backend_scanout_close(
	struct kl_backend_scanout *scanout)
{
	struct gpu_display_release release;
	int error;

	/* Nothing to close. */
	if (scanout == NULL)
		return;

	/* The display goes back; a failed release is retired by closing the fd. */
	memset(&release, 0, sizeof(release));
	release.version = GPU_ABI_VERSION;
	release.size = sizeof(release);
	release.lease = scanout->lease;
	error = ioctl(scanout->gpu, GPU_DISPLAY_RELEASE, &release);
	if (error != 0)
		printf("ZWL SCANOUT release errno=%d\n", errno);

	/* The fd (and its handles) and the record. */
	close(scanout->gpu);
	if (scanout_open == scanout)
		scanout_open = NULL;
	free(scanout);
}

/*
 * Forgets a buffer's handle when the buffer goes: the open scanout's
 * imported capability is destroyed (one of an older claim went with its
 * fd).
 */
void
zwl_scanout_forget(
	struct zwl_gpu_buffer_record *record)
{
	struct gpu_resource_destroy destroy;

	/* A handle of the scanout open now. */
	if (record == NULL || record->handle == 0U)
		return;
	if (scanout_open == NULL || record->claim != scanout_open->claim) {
		record->handle = 0U;
		return;
	}

	/* The capability's import goes from the scanout's fd. */
	memset(&destroy, 0, sizeof(destroy));
	destroy.version = GPU_ABI_VERSION;
	destroy.size = sizeof(destroy);
	destroy.handle = record->handle;
	(void)ioctl(scanout_open->gpu, GPU_RESOURCE_DESTROY, &destroy);
	record->handle = 0U;
}

/*
 * Finds the first connected display that shows shared images, checks that
 * it takes the output's size, and takes its refresh.  Returns 0, ENOTSUP,
 * or the failure of the query.
 */
static int
scanout_display(
	struct kl_backend_scanout *scanout)
{
	struct gpu_display_info display;
	struct gpu_display_mode mode;
	unsigned wanted;
	int error;

	/* The first display. */
	memset(&display, 0, sizeof(display));
	display.version = GPU_ABI_VERSION;
	display.size = sizeof(display);
	error = ioctl(scanout->gpu, GPU_DISPLAY_QUERY, &display);
	if (error != 0)
		return errno;

	/* Connected, and showing shared images as they are. */
	wanted = GPU_DISPLAY_CONNECTED | GPU_DISPLAY_BLOB;
	if ((display.flags & wanted) != wanted)
		return ENOTSUP;
	scanout->display_id = display.display_id;
	scanout->generation = display.generation;

	/* The output's size, without changing what the display shows. */
	memset(&mode, 0, sizeof(mode));
	mode.version = GPU_ABI_VERSION;
	mode.size = sizeof(mode);
	mode.display_id = display.display_id;
	mode.generation = display.generation;
	mode.operation = GPU_DISPLAY_MODE_VALIDATE;
	mode.width = scanout->width;
	mode.height = scanout->height;
	error = ioctl(scanout->gpu, GPU_DISPLAY_MODE, &mode);
	if (error != 0)
		return errno;

	/* The refresh the display chose for it. */
	if (mode.refresh_millihz == 0U)
		return ENOTSUP;
	scanout->refresh = mode.refresh_millihz;

	/* Succeeded: the display can show the output. */
	return 0;
}

/*
 * Imports a buffer's image capability into the scanout's fd, with the
 * description the client sent (which the kernel checks against its own).
 * Returns 0 or the failure of the import.
 */
static int
scanout_import(
	struct kl_backend_scanout *scanout,
	struct zwl_gpu_buffer_record *record)
{
	struct gpu_resource_import import;
	int error;

	/* The fd and the description. */
	memset(&import, 0, sizeof(import));
	import.version = GPU_ABI_VERSION;
	import.size = sizeof(import);
	import.fd = record->descriptor;
	memcpy(&import.image, record->description, sizeof(import.image));
	error = ioctl(scanout->gpu, GPU_RESOURCE_IMPORT, &import);
	if (error != 0)
		return errno;

	/* Succeeded: the handle is the scanout's. */
	record->handle = import.handle;
	record->claim = scanout->claim;
	return 0;
}
