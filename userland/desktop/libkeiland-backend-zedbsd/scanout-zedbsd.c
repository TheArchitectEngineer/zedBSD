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
 * The present returns once the display reads the new buffer, at the next
 * refresh, so the scanout's own thread makes it: the compositor hands the
 * buffer over (kl_backend_scanout_begin) and goes on taking its clients'
 * commits meanwhile, which a mailbox client's next images wait for; the
 * thread writes a byte to the scanout's pipe when the present has ended,
 * and the compositor takes its outcome (kl_backend_scanout_end).  One
 * present is in flight at a time.
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
#include <pthread.h>
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
 *
 * Its present thread: lock guards request, waiting, ended, result and
 * stop, and changed wakes the thread.  waiting is a present handed over
 * and not yet taken by the thread, ended one the thread has made (result:
 * 0 or its errno).  flight, the main thread's own, is a present begun and
 * not yet taken by kl_backend_scanout_end.  The thread writes a byte to
 * done[1] each time a present has ended; done[0] does not block.
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
	pthread_t thread;
	pthread_mutex_t lock;
	pthread_cond_t changed;
	struct gpu_display_present request;
	int waiting;
	int ended;
	int result;
	int stop;
	int flight;
	int done[2];
};

/* The scanout open now, for the buffers that go meanwhile, and the number of the last claim. */
static struct kl_backend_scanout *scanout_open;
static uint64_t scanout_claims;

static int scanout_display(struct kl_backend_scanout *scanout);
static int scanout_import(struct kl_backend_scanout *scanout, struct kwl_gpu_buffer_record *record);
static int scanout_thread_start(struct kl_backend_scanout *scanout);
static void scanout_thread_stop(struct kl_backend_scanout *scanout);
static void *scanout_thread(void *argument);
static void scanout_release(struct kl_backend_scanout *scanout);

/*
 * Tells whether a scanout of a size could open, without claiming the
 * display: its own GPU fd asks for a connected display that shows shared
 * images and takes the size, and is closed again.  Returns 0, ENOTSUP, or
 * the failure of the GPU device or the query.
 */
int
kl_backend_scanout_possible(
	struct kl_backend *backend,
	uint32_t width,
	uint32_t height)
{
	struct kl_backend_scanout probe;
	int error;

	/* The size the scanout would show. */
	(void)backend;
	if (width == 0U || height == 0U)
		return EINVAL;
	memset(&probe, 0, sizeof(probe));
	probe.width = width;
	probe.height = height;

	/* A GPU fd of the question's own, which claims nothing. */
	probe.gpu = open(SCANOUT_DEVICE, O_RDWR | O_CLOEXEC);
	if (probe.gpu < 0)
		return errno;

	/* The same display and size checks as an open, then the fd goes. */
	error = scanout_display(&probe);
	close(probe.gpu);

	/* Reports why no scanout of the size can open. */
	if (error != 0)
		return error;

	/* Succeeded: a claim of the display could show the output. */
	return 0;
}

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

	/* The present thread and its pipe; without them the display goes back. */
	opened->lease = claim.lease;
	error = scanout_thread_start(opened);
	if (error != 0) {
		scanout_release(opened);
		close(opened->gpu);
		free(opened);
		return error;
	}

	/* Succeeded: the display is the scanout's. */
	scanout_claims++;
	opened->claim = scanout_claims;
	scanout_open = opened;
	*scanout = opened;
	return 0;
}

/*
 * Begins showing a client's GPU buffer as the whole output: imports it
 * when it is new to the scanout and hands it to the present thread.
 * Returns 0, EBUSY while a present is in flight, EINVAL for a buffer
 * without the backend's record or of another size, or the failure of its
 * import.
 */
int
kl_backend_scanout_begin(
	struct kl_backend_scanout *scanout,
	const struct kl_backend_protocol_host *host,
	struct kl_backend_resource *buffer)
{
	struct kwl_gpu_buffer_record *record;
	struct gpu_display_present present;
	struct gpu_image_descriptor image;
	unsigned role;
	void **owned;
	int error;

	/* A GPU buffer with the backend's record, while no present is in flight. */
	if (scanout == NULL || host == NULL || buffer == NULL)
		return EINVAL;
	if (scanout->flight)
		return EBUSY;
	role = host->resource_role(buffer);
	if (role != KL_BACKEND_ROLE_BUFFER)
		return EINVAL;
	owned = host->resource_private(buffer);
	if (owned == NULL || *owned == NULL)
		return EINVAL;
	record = *owned;

	/* The output's size, and a description the kernel's record holds. */
	if (record->width != scanout->width || record->height != scanout->height) {
		printf("KL SCANOUT refused step=size buffer=%ux%u output=%ux%u\n", record->width, record->height, scanout->width, scanout->height);
		return EINVAL;
	}
	if (record->description_size != sizeof(image)) {
		printf("KL SCANOUT refused step=description size=%u\n", (unsigned)record->description_size);
		return EINVAL;
	}

	/* Imported into the scanout's fd once. */
	if (record->handle == 0U || record->claim != scanout->claim) {
		error = scanout_import(scanout, record);
		if (error != 0) {
			printf("KL SCANOUT refused step=import errno=%d\n", error);
			return error;
		}
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

	/* To the thread, which makes it. */
	pthread_mutex_lock(&scanout->lock);

	scanout->request = present;
	scanout->waiting = 1;
	scanout->ended = 0;
	pthread_cond_signal(&scanout->changed);

	pthread_mutex_unlock(&scanout->lock);

	/* Succeeded: the present is in flight until kl_backend_scanout_end takes it. */
	scanout->flight = 1;
	return 0;
}

/*
 * Reports the fd that becomes readable when the present in flight has
 * ended, -1 when none is.
 */
int
kl_backend_scanout_fd(
	struct kl_backend_scanout *scanout)
{
	/* Only a present in flight ends. */
	if (scanout == NULL || !scanout->flight)
		return -1;

	/* Succeeded: the pipe's reading end. */
	return scanout->done[0];
}

/*
 * Takes the present in flight once it has ended: 1 with its outcome in
 * *result (0 when the display reads the buffer, the buffer shown before
 * then no longer read; or the present's errno), 0 while it has not ended
 * or none is in flight.
 */
int
kl_backend_scanout_end(
	struct kl_backend_scanout *scanout,
	int *result)
{
	unsigned char bytes[8];
	ssize_t got;
	int ended;

	/* Nothing in flight. */
	if (scanout == NULL || !scanout->flight)
		return 0;

	/* The pipe's bytes; it does not block. */
	do {
		got = read(scanout->done[0], bytes, sizeof(bytes));
	} while (got > 0 || (got < 0 && errno == EINTR));

	/* The thread's outcome, when it has one. */
	pthread_mutex_lock(&scanout->lock);

	ended = scanout->ended;
	if (ended) {
		*result = scanout->result;
		scanout->ended = 0;
	}

	pthread_mutex_unlock(&scanout->lock);

	/* Not yet. */
	if (!ended)
		return 0;

	/* Succeeded: the present has ended; a frame shown counts. */
	scanout->flight = 0;
	if (*result == 0)
		scanout->frame++;
	return 1;
}

/*
 * Releases the display and the scanout's fd (with every handle imported
 * into it), after the present in flight has ended.
 */
void
kl_backend_scanout_close(
	struct kl_backend_scanout *scanout)
{
	/* Nothing to close. */
	if (scanout == NULL)
		return;

	/* The thread makes the present handed to it, then ends. */
	scanout_thread_stop(scanout);

	/* The display goes back. */
	scanout_release(scanout);

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
kwl_scanout_forget(
	struct kwl_gpu_buffer_record *record)
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
	struct kwl_gpu_buffer_record *record)
{
	struct gpu_resource_import import;
	int error;

	/*
	 * The fd alone: the kernel takes the image's description from the
	 * export and refuses one the importer supplies (gpu_import_ioctl).
	 */
	memset(&import, 0, sizeof(import));
	import.version = GPU_ABI_VERSION;
	import.size = sizeof(import);
	import.fd = record->descriptor;
	error = ioctl(scanout->gpu, GPU_RESOURCE_IMPORT, &import);
	if (error != 0)
		return errno;

	/* Succeeded: the handle is the scanout's. */
	record->handle = import.handle;
	record->claim = scanout->claim;
	return 0;
}

/* Gives the display back; a failed release is retired by closing the fd. */
static void
scanout_release(
	struct kl_backend_scanout *scanout)
{
	struct gpu_display_release release;
	int error;

	/* The lease's end. */
	memset(&release, 0, sizeof(release));
	release.version = GPU_ABI_VERSION;
	release.size = sizeof(release);
	release.lease = scanout->lease;
	error = ioctl(scanout->gpu, GPU_DISPLAY_RELEASE, &release);
	if (error != 0)
		printf("KWL SCANOUT release errno=%d\n", errno);
}

/*
 * Makes the present thread, its lock and its pipe (whose reading end
 * does not block).  Returns 0 or the failure of one of them (none is left
 * then).
 */
static int
scanout_thread_start(
	struct kl_backend_scanout *scanout)
{
	int error;

	/* The pipe the thread tells an end through. */
	error = pipe(scanout->done);
	if (error != 0)
		return errno;
	(void)fcntl(scanout->done[0], F_SETFL, O_NONBLOCK);
	(void)fcntl(scanout->done[0], F_SETFD, FD_CLOEXEC);
	(void)fcntl(scanout->done[1], F_SETFD, FD_CLOEXEC);

	/* The lock, the condition and the thread. */
	pthread_mutex_init(&scanout->lock, NULL);
	pthread_cond_init(&scanout->changed, NULL);
	error = pthread_create(&scanout->thread, NULL, scanout_thread, scanout);
	if (error != 0) {
		pthread_cond_destroy(&scanout->changed);
		pthread_mutex_destroy(&scanout->lock);
		close(scanout->done[0]);
		close(scanout->done[1]);
		return error;
	}

	/* Succeeded: the thread waits for a present. */
	return 0;
}

/* Ends the present thread once it has made the present handed to it, and frees its lock and pipe. */
static void
scanout_thread_stop(
	struct kl_backend_scanout *scanout)
{
	/* The request to end, which wakes it. */
	pthread_mutex_lock(&scanout->lock);

	scanout->stop = 1;
	pthread_cond_signal(&scanout->changed);

	pthread_mutex_unlock(&scanout->lock);

	/* Waited for, then its objects go. */
	(void)pthread_join(scanout->thread, NULL);
	pthread_cond_destroy(&scanout->changed);
	pthread_mutex_destroy(&scanout->lock);
	close(scanout->done[0]);
	close(scanout->done[1]);
	scanout->flight = 0;
}

/*
 * The present thread: makes each present handed to it, notes its outcome
 * and writes a byte to the pipe, until told to end with none waiting.
 */
static void *
scanout_thread(
	void *argument)
{
	struct kl_backend_scanout *scanout;
	struct gpu_display_present present;
	unsigned char byte;
	int error;

	/* The scanout it serves. */
	scanout = argument;
	byte = 1U;

	for (;;) {
		/* Waits for a present, or the end with none waiting. */
		pthread_mutex_lock(&scanout->lock);

		while (!scanout->waiting && !scanout->stop)
			pthread_cond_wait(&scanout->changed, &scanout->lock);
		if (!scanout->waiting) {
			pthread_mutex_unlock(&scanout->lock);
			break;
		}
		present = scanout->request;
		scanout->waiting = 0;

		pthread_mutex_unlock(&scanout->lock);

		/* The present, which returns once the display reads the buffer. */
		error = ioctl(scanout->gpu, GPU_DISPLAY_PRESENT, &present);
		if (error != 0) {
			error = errno;
			printf("KL SCANOUT refused step=present errno=%d format=%u stride=%u offset=%llu %ux%u refresh=%u\n", error, (unsigned)present.format,
			       (unsigned)present.stride, (unsigned long long)present.offset, (unsigned)present.width, (unsigned)present.height,
			       (unsigned)present.refresh_millihz);
		}

		/* Its outcome, then the byte that wakes the compositor. */
		pthread_mutex_lock(&scanout->lock);

		scanout->result = error;
		scanout->ended = 1;

		pthread_mutex_unlock(&scanout->lock);

		while (write(scanout->done[1], &byte, 1U) < 0 && errno == EINTR)
			continue;
	}

	/* Ended. */
	return NULL;
}
