/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A mode set by a present (amdrdnactl present WIDTHxHEIGHT [SECONDS]
 * [GPU-DEVICE]): takes the display's lease, presents a frame of that size
 * (a colour ramp with a white border), which makes the driver set a mode
 * of that size, holds it, presents a second frame, then lets the lease
 * go, which sets the firmware's mode again.  One line for each step with
 * the display's mode as the query reports it.
 *
 * And the output's off and on (amdrdnactl hotplug off|on [DEVICE]): the
 * driver acts as if the monitor went away or came.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <uapi/amdrdna.h>
#include <uapi/gpu.h>
#include <uapi/gpu-display.h>

#include "amdrdnactl.h"

/* The bytes of a pixel and the border's width. */
#define PRESENT_PIXEL_BYTES		4U
#define PRESENT_BORDER			4U

/* The levels of the hot-plug request: gone and there. */
#define PRESENT_HOTPLUG_GONE		1U
#define PRESENT_HOTPLUG_THERE		2U

static int present_query(int descriptor, struct gpu_display_info *output);
static int present_fill(int descriptor, uint64_t handle, uint32_t width, uint32_t height, uint32_t shade);
static int present_frame(int descriptor, const struct gpu_display_info *output, uint64_t lease, uint64_t handle, uint32_t width, uint32_t height);

/*
 * Presents frames of width by height on the first display of the GPU
 * device at path for seconds, then lets the lease go; returns 0, or 1 when
 * a step failed.
 */
int
control_present(
	const char *path,
	uint32_t width,
	uint32_t height,
	unsigned seconds)
{
	struct gpu_resource_create create;
	struct gpu_display_release release;
	struct gpu_display_claim claim;
	struct gpu_display_info output;
	int descriptor;
	int status;
	int result;

	/* The device and its first display. */
	descriptor = open(path, O_RDWR | O_CLOEXEC);
	if (descriptor < 0) {
		printf("PRESENT fail open %s %d\n", path, errno);
		return 1;
	}
	status = present_query(descriptor, &output);
	if (status != 0) {
		printf("PRESENT fail query %d\n", status);
		(void)close(descriptor);
		return 1;
	}
	printf("PRESENT before current=%ux%u@%u max=%ux%u\n", output.current_width, output.current_height, output.refresh_millihz,
	    output.max_width, output.max_height);

	/* The display's lease. */
	memset(&claim, 0, sizeof(claim));
	claim.version = GPU_ABI_VERSION;
	claim.size = sizeof(claim);
	claim.display_id = output.display_id;
	claim.generation = output.generation;
	status = ioctl(descriptor, GPU_DISPLAY_CLAIM, &claim);
	if (status != 0) {
		printf("PRESENT fail claim %d\n", errno);
		(void)close(descriptor);
		return 1;
	}

	/* The frame's storage, the ramp written into it. */
	memset(&create, 0, sizeof(create));
	create.version = GPU_ABI_VERSION;
	create.size = sizeof(create);
	create.bytes = (uint64_t)width * height * PRESENT_PIXEL_BYTES;
	create.usage = GPU_RESOURCE_USAGE_STORAGE;
	status = ioctl(descriptor, GPU_RESOURCE_CREATE, &create);
	if (status != 0) {
		printf("PRESENT fail storage %d\n", errno);
		(void)close(descriptor);
		return 1;
	}
	result = present_fill(descriptor, create.handle, width, height, 0U);

	/* The first frame sets the mode of its size. */
	if (result == 0)
		result = present_frame(descriptor, &output, claim.lease, create.handle, width, height);

	/* Held, then a second frame of another shade in the same mode. */
	if (result == 0) {
		sleep(seconds);
		result = present_fill(descriptor, create.handle, width, height, 0x80U);
	}
	if (result == 0)
		result = present_frame(descriptor, &output, claim.lease, create.handle, width, height);
	if (result == 0)
		sleep(1U);

	/* The lease goes; the firmware's mode comes back. */
	memset(&release, 0, sizeof(release));
	release.version = GPU_ABI_VERSION;
	release.size = sizeof(release);
	release.lease = claim.lease;
	status = ioctl(descriptor, GPU_DISPLAY_RELEASE, &release);
	if (status != 0) {
		printf("PRESENT fail release %d\n", errno);
		result = 1;
	}
	status = present_query(descriptor, &output);
	if (status == 0)
		printf("PRESENT after current=%ux%u@%u\n", output.current_width, output.current_height, output.refresh_millihz);
	(void)close(descriptor);

	/* Reports a step that failed. */
	if (result != 0)
		return 1;

	/* Succeeded: the mode was set and given back. */
	printf("PRESENT ok %ux%u\n", width, height);
	return 0;
}

/*
 * Asks the control device at path to act as if the monitor went away
 * (on 0) or came (on 1); returns 0, or 1 when the request was refused or
 * the action failed.
 */
int
control_hotplug(
	const char *path,
	int on)
{
	struct amdrdna_hotplug_ask ask;
	int descriptor;
	int saved;
	int result;

	/* The control device. */
	descriptor = open(path, O_RDONLY | O_CLOEXEC);
	if (descriptor < 0) {
		printf("HOTPLUG fail open %s %d\n", path, errno);
		return 1;
	}

	/* The request; the driver answers when it has acted. */
	memset(&ask, 0, sizeof(ask));
	ask.version = AMDRDNA_INTERFACE_VERSION;
	ask.size = sizeof(ask);
	ask.level = PRESENT_HOTPLUG_GONE;
	if (on)
		ask.level = PRESENT_HOTPLUG_THERE;
	result = ioctl(descriptor, AMDRDNA_IOC_HOTPLUG, &ask);
	saved = errno;
	(void)close(descriptor);
	if (result != 0) {
		printf("HOTPLUG refused %d (%s)\n", saved, strerror(saved));
		return 1;
	}

	/* The action's own result. */
	if (ask.result != 0) {
		printf("HOTPLUG fail result=%d dark=%u mode=%ux%u@%u\n", (int)ask.result, (unsigned)ask.dark, (unsigned)ask.width,
		    (unsigned)ask.height, (unsigned)ask.refresh_millihz);
		return 1;
	}

	/* Succeeded: the output followed. */
	printf("HOTPLUG ok %s dark=%u mode=%ux%u@%u\n", on ? "on" : "off", (unsigned)ask.dark, (unsigned)ask.width, (unsigned)ask.height,
	    (unsigned)ask.refresh_millihz);
	return 0;
}

/* Reads the first display; returns 0 or the query's errno. */
static int
present_query(
	int descriptor,
	struct gpu_display_info *output)
{
	int status;

	/* Display 0. */
	memset(output, 0, sizeof(*output));
	output->version = GPU_ABI_VERSION;
	output->size = sizeof(*output);
	output->index = 0U;
	status = ioctl(descriptor, GPU_DISPLAY_QUERY, output);
	if (status != 0)
		return errno;

	/* Succeeded: the display is read. */
	return 0;
}

/*
 * Writes a frame into the storage: a ramp of red across and green down,
 * blue at the shade, inside a white border; returns 0, or 1 when a write
 * failed.
 */
static int
present_fill(
	int descriptor,
	uint64_t handle,
	uint32_t width,
	uint32_t height,
	uint32_t shade)
{
	struct gpu_transfer transfer;
	uint32_t *rows;
	uint32_t group;
	uint32_t row;
	uint32_t first;
	uint32_t count;
	uint32_t x;
	uint32_t y;
	uint32_t pixel;
	int status;

	/* Room for a group of rows: as many as one transfer carries (at least one). */
	group = GPU_COPY_MAX / (width * PRESENT_PIXEL_BYTES);
	if (group == 0U)
		group = 1U;
	rows = malloc((size_t)width * group * PRESENT_PIXEL_BYTES);
	if (rows == NULL) {
		printf("PRESENT fail memory\n");
		return 1;
	}

	/* Each group of rows: made, then written at its place. */
	for (first = 0U; first < height; first += count) {
		count = height - first;
		if (count > group)
			count = group;
		for (row = 0U; row < count; row++) {
			y = first + row;
			for (x = 0U; x < width; x++) {
				/* The border white, the inside the ramp (blue, green, red, alpha from the low byte up). */
				pixel = 0xffffffffU;
				if (x >= PRESENT_BORDER && x < width - PRESENT_BORDER && y >= PRESENT_BORDER && y < height - PRESENT_BORDER)
					pixel = 0xff000000U | ((x * 255U / width) << 16) | ((y * 255U / height) << 8) | shade;
				rows[(size_t)row * width + x] = pixel;
			}
		}
		memset(&transfer, 0, sizeof(transfer));
		transfer.version = GPU_ABI_VERSION;
		transfer.size = sizeof(transfer);
		transfer.handle = handle;
		transfer.offset = (uint64_t)first * width * PRESENT_PIXEL_BYTES;
		transfer.address = (uint64_t)(uintptr_t)rows;
		transfer.bytes = count * width * PRESENT_PIXEL_BYTES;
		status = ioctl(descriptor, GPU_RESOURCE_WRITE, &transfer);
		if (status != 0) {
			printf("PRESENT fail write %d\n", errno);
			free(rows);
			return 1;
		}
	}

	/* Succeeded: the frame is written. */
	free(rows);
	return 0;
}

/*
 * Presents the storage as a frame of width by height and reports the
 * display's mode after it; returns 0, or 1 when the present failed or the
 * mode is not the frame's size.
 */
static int
present_frame(
	int descriptor,
	const struct gpu_display_info *output,
	uint64_t lease,
	uint64_t handle,
	uint32_t width,
	uint32_t height)
{
	struct gpu_display_present present;
	struct gpu_display_info after;
	int status;

	/* The frame, FIFO, at the size's first refresh. */
	memset(&present, 0, sizeof(present));
	present.version = GPU_ABI_VERSION;
	present.size = sizeof(present);
	present.lease = lease;
	present.handle = handle;
	present.width = width;
	present.height = height;
	present.stride = width * PRESENT_PIXEL_BYTES;
	present.format = GPU_PIXEL_BGRA8888;
	present.flags = GPU_DISPLAY_PRESENT_FIFO;
	present.generation = output->generation;
	status = ioctl(descriptor, GPU_DISPLAY_PRESENT, &present);
	if (status != 0) {
		printf("PRESENT fail present %ux%u %d (%s)\n", width, height, errno, strerror(errno));
		return 1;
	}

	/* The mode the display reports now. */
	status = present_query(descriptor, &after);
	if (status != 0) {
		printf("PRESENT fail query %d\n", status);
		return 1;
	}
	printf("PRESENT shown sequence=%llu current=%ux%u@%u\n", (unsigned long long)present.sequence, after.current_width,
	    after.current_height, after.refresh_millihz);
	if (after.current_width != width || after.current_height != height)
		return 1;

	/* Succeeded: the frame is shown in its mode. */
	return 0;
}
