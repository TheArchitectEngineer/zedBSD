/* -*- mode: c; c-file-style: "linux"; tab-width: 8; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws141-p014: the GPU core's cursor plane requests (GPU_DISPLAY_CURSOR_SET,
 * MOVE and HIDE) on the host, with the real GPU core and cdev registry and
 * the ws014 fixture's allocator, credentials, copies and locks
 * (plan/ws014/tests/gpu-framework.c), and a display backend that records
 * what it is asked.
 */

#define main gpu_framework_unused_main
#include "../../ws014/tests/gpu-framework.c"
#undef main

#include <kern/process.h>
#include <kern/filedesc.h>

/* What the cursor operations were asked, and what they answer. */
static unsigned cursor_sets;
static unsigned cursor_moves;
static unsigned cursor_hides;
static void *cursor_object;
static int32_t cursor_x;
static int32_t cursor_y;
static int cursor_error;

void gpu_test_set_process(struct process *process);
static int cursor_query(void *opaque, void *session, struct gpu_display_info *request);
static int cursor_mode(void *opaque, void *session, struct gpu_display_mode *request);
static int cursor_claim(void *opaque, void *session, struct gpu_display_claim *request);
static int cursor_release(void *opaque, void *session, const struct gpu_display_release *request);
static int cursor_present(void *opaque, void *session, void *object, struct gpu_display_present *request);
static int cursor_wait(void *opaque, void *session, struct gpu_display_wait *request);
static int cursor_set(void *opaque, void *session, void *object, const struct gpu_display_cursor_image *request);
static int cursor_move(void *opaque, void *session, const struct gpu_display_cursor_move *request);
static int cursor_hide(void *opaque, void *session, const struct gpu_display_cursor_hide *request);
static int cursor_ioctl(struct test_file *opened, unsigned long command, void *request);
static void prepare_image(struct gpu_display_cursor_image *request, uint64_t handle);

/*
 * Registers display backends with and without the cursor operations and drives the three requests.
 */
int
main(
	void)
{
	struct drv_gpu_ops native;
	struct drv_gpu_display_ops display;
	struct drv_gpu_device *device;
	struct test_backend backend;
	struct test_file writer;
	struct test_file reader;
	struct process process;
	struct gpu_resource_create create;
	struct gpu_display_cursor_image image;
	struct gpu_display_cursor_move move;
	struct gpu_display_cursor_hide hide;
	int error;

	/* The caller's process, and a display backend with storage. */
	memset(&process, 0, sizeof(process));
	process.fd = filedesc_create(&process);
	assert(process.fd != NULL);
	gpu_test_set_process(&process);
	memset(&native, 0, sizeof(native));
	memset(&display, 0, sizeof(display));
	memset(&backend, 0, sizeof(backend));
	backend.max_bytes = 65536;
	native.version = DRV_GPU_INTERFACE_VERSION;
	native.size = sizeof(native);
	native.capabilities = GPU_CAP_RESOURCE | GPU_CAP_DISPLAY;
	native.open = backend_open;
	native.close = backend_close;
	native.get_info = backend_get_info;
	native.resource_create = backend_create;
	native.resource_destroy = backend_destroy;
	display.query = cursor_query;
	display.mode = cursor_mode;
	display.claim = cursor_claim;
	display.release = cursor_release;
	display.present = cursor_present;
	display.wait = cursor_wait;
	native.display = &display;

	/* Some of the three cursor operations without the others: refused. */
	display.cursor_set = cursor_set;
	error = drv_gpu_register(&native, &backend, &device);
	assert(error == EINVAL);
	display.cursor_move = cursor_move;
	error = drv_gpu_register(&native, &backend, &device);
	assert(error == EINVAL);
	puts("ok: the three cursor operations come together");

	/* A display without them answers EOPNOTSUPP to all three requests. */
	display.cursor_set = NULL;
	display.cursor_move = NULL;
	error = drv_gpu_register(&native, &backend, &device);
	assert(error == 0);
	error = open_file(&writer, "gpu0", O_RDWR);
	assert(error == 0);
	prepare_image(&image, 1U);
	error = cursor_ioctl(&writer, GPU_DISPLAY_CURSOR_SET, &image);
	assert(error == EOPNOTSUPP);
	memset(&move, 0, sizeof(move));
	move.version = GPU_ABI_VERSION;
	move.size = sizeof(move);
	move.lease = 7U;
	move.generation = 1U;
	error = cursor_ioctl(&writer, GPU_DISPLAY_CURSOR_MOVE, &move);
	assert(error == EOPNOTSUPP);
	close_file(&writer);
	error = drv_gpu_unregister(device);
	assert(error == 0);
	puts("ok: a display without a cursor plane answers EOPNOTSUPP");

	/* With the cursor plane, a writer and a reader. */
	display.cursor_set = cursor_set;
	display.cursor_move = cursor_move;
	display.cursor_hide = cursor_hide;
	error = drv_gpu_register(&native, &backend, &device);
	assert(error == 0);
	error = open_file(&writer, "gpu0", O_RDWR);
	assert(error == 0);
	error = open_file(&reader, "gpu0", O_RDONLY);
	assert(error == 0);
	prepare_create(&create);
	create.bytes = 16384;
	error = cursor_ioctl(&writer, GPU_RESOURCE_CREATE, &create);
	assert(error == 0 && create.handle != 0U);

	/* SET: a writer, a lease of a generation, at most 64x64, a packed format, rows inside the storage. */
	prepare_image(&image, create.handle);
	error = cursor_ioctl(&reader, GPU_DISPLAY_CURSOR_SET, &image);
	assert(error == EACCES && cursor_sets == 0U);
	prepare_image(&image, create.handle);
	image.width = 65U;
	error = cursor_ioctl(&writer, GPU_DISPLAY_CURSOR_SET, &image);
	assert(error == EINVAL && cursor_sets == 0U);
	prepare_image(&image, create.handle);
	image.height = 0U;
	error = cursor_ioctl(&writer, GPU_DISPLAY_CURSOR_SET, &image);
	assert(error == EINVAL && cursor_sets == 0U);
	prepare_image(&image, create.handle);
	image.format = 0U;
	error = cursor_ioctl(&writer, GPU_DISPLAY_CURSOR_SET, &image);
	assert(error == EINVAL && cursor_sets == 0U);
	prepare_image(&image, create.handle);
	image.stride = 255U;
	error = cursor_ioctl(&writer, GPU_DISPLAY_CURSOR_SET, &image);
	assert(error == EINVAL && cursor_sets == 0U);
	prepare_image(&image, create.handle);
	image.reserved = 1U;
	error = cursor_ioctl(&writer, GPU_DISPLAY_CURSOR_SET, &image);
	assert(error == EINVAL && cursor_sets == 0U);
	prepare_image(&image, create.handle);
	image.offset = 1U;
	error = cursor_ioctl(&writer, GPU_DISPLAY_CURSOR_SET, &image);
	assert(error == EINVAL && cursor_sets == 0U);
	prepare_image(&image, create.handle + 1000U);
	error = cursor_ioctl(&writer, GPU_DISPLAY_CURSOR_SET, &image);
	assert(error != 0 && cursor_sets == 0U);
	prepare_image(&image, create.handle);
	image.size = sizeof(image) - 8U;
	error = cursor_ioctl(&writer, GPU_DISPLAY_CURSOR_SET, &image);
	assert(error == EINVAL && cursor_sets == 0U);
	puts("ok: SET checks the open, the image, the storage and the framing before the backend");

	/* SET reaches the backend with the resolved storage; the backend's refusal comes back. */
	prepare_image(&image, create.handle);
	error = cursor_ioctl(&writer, GPU_DISPLAY_CURSOR_SET, &image);
	assert(error == 0 && cursor_sets == 1U && cursor_object != NULL);
	cursor_error = EBUSY;
	error = cursor_ioctl(&writer, GPU_DISPLAY_CURSOR_SET, &image);
	assert(error == EBUSY && cursor_sets == 2U);
	cursor_error = 0;
	puts("ok: SET reaches the backend and its EBUSY comes back");

	/* MOVE and HIDE: a writer, a lease of a generation, reserved zero; any position. */
	move.x = -20;
	move.y = 5000;
	error = cursor_ioctl(&reader, GPU_DISPLAY_CURSOR_MOVE, &move);
	assert(error == EACCES && cursor_moves == 0U);
	move.reserved = 1U;
	error = cursor_ioctl(&writer, GPU_DISPLAY_CURSOR_MOVE, &move);
	assert(error == EINVAL && cursor_moves == 0U);
	move.reserved = 0U;
	move.lease = 0U;
	error = cursor_ioctl(&writer, GPU_DISPLAY_CURSOR_MOVE, &move);
	assert(error == EINVAL && cursor_moves == 0U);
	move.lease = 7U;
	error = cursor_ioctl(&writer, GPU_DISPLAY_CURSOR_MOVE, &move);
	assert(error == 0 && cursor_moves == 1U && cursor_x == -20 && cursor_y == 5000);
	memset(&hide, 0, sizeof(hide));
	hide.version = GPU_ABI_VERSION;
	hide.size = sizeof(hide);
	hide.lease = 7U;
	error = cursor_ioctl(&writer, GPU_DISPLAY_CURSOR_HIDE, &hide);
	assert(error == EINVAL && cursor_hides == 0U);
	hide.generation = 1U;
	error = cursor_ioctl(&writer, GPU_DISPLAY_CURSOR_HIDE, &hide);
	assert(error == 0 && cursor_hides == 1U);
	puts("ok: MOVE and HIDE check the open, the lease and the framing, and pass any position");

	/* Closed and withdrawn without leaks. */
	error = destroy_handle(&writer, create.handle);
	assert(error == 0);
	close_file(&reader);
	close_file(&writer);
	error = drv_gpu_unregister(device);
	assert(error == 0);
	filedesc_destroy(process.fd);
	gpu_test_set_process(NULL);
	assert(allocations == 0U && credential_references == 0U && held_spinlocks == 0U);
	puts("host-display-cursor: PASS");
	return 0;
}

/* Describes one connected display with a cursor plane. */
static int
cursor_query(
	void *opaque,
	void *session,
	struct gpu_display_info *request)
{
	(void)opaque;
	(void)session;
	request->count = 1U;
	if (request->index == GPU_DISPLAY_COUNT_ONLY)
		return 0;
	request->display_id = 1U;
	request->generation = 1U;
	request->flags = GPU_DISPLAY_CONNECTED | GPU_DISPLAY_HAS_CURSOR;
	return 0;
}

/* Has no modes (not used here). */
static int
cursor_mode(
	void *opaque,
	void *session,
	struct gpu_display_mode *request)
{
	(void)opaque;
	(void)session;
	(void)request;
	return EINVAL;
}

/* Grants no lease (not used here). */
static int
cursor_claim(
	void *opaque,
	void *session,
	struct gpu_display_claim *request)
{
	(void)opaque;
	(void)session;
	(void)request;
	return EBUSY;
}

/* Has no lease to release. */
static int
cursor_release(
	void *opaque,
	void *session,
	const struct gpu_display_release *request)
{
	(void)opaque;
	(void)session;
	(void)request;
	return EINVAL;
}

/* Presents nothing (not used here). */
static int
cursor_present(
	void *opaque,
	void *session,
	void *object,
	struct gpu_display_present *request)
{
	(void)opaque;
	(void)session;
	(void)object;
	(void)request;
	return EINVAL;
}

/* Waits for nothing (not used here). */
static int
cursor_wait(
	void *opaque,
	void *session,
	struct gpu_display_wait *request)
{
	(void)opaque;
	(void)session;
	(void)request;
	return EINVAL;
}

/* Records a checked image request and the storage it names. */
static int
cursor_set(
	void *opaque,
	void *session,
	void *object,
	const struct gpu_display_cursor_image *request)
{
	(void)opaque;
	(void)session;
	assert(held_spinlocks == 0);
	assert(request->width != 0U && request->width <= GPU_DISPLAY_CURSOR_MAX);
	cursor_sets++;
	cursor_object = object;
	return cursor_error;
}

/* Records a checked move. */
static int
cursor_move(
	void *opaque,
	void *session,
	const struct gpu_display_cursor_move *request)
{
	(void)opaque;
	(void)session;
	assert(held_spinlocks == 0);
	cursor_moves++;
	cursor_x = request->x;
	cursor_y = request->y;
	return cursor_error;
}

/* Records a checked hide. */
static int
cursor_hide(
	void *opaque,
	void *session,
	const struct gpu_display_cursor_hide *request)
{
	(void)opaque;
	(void)session;
	(void)request;
	assert(held_spinlocks == 0);
	cursor_hides++;
	return cursor_error;
}

/* Sends one request through the real dispatch chain. */
static int
cursor_ioctl(
	struct test_file *opened,
	unsigned long command,
	void *request)
{
	int error;

	/* The real dispatch chain. */
	error = cdev_file_ops.ioctl(&opened->file, command, (uintptr_t)request);
	return error;
}

/* A well-formed 64x64 BGRA image request at the start of the given storage, lease 7, generation 1. */
static void
prepare_image(
	struct gpu_display_cursor_image *request,
	uint64_t handle)
{
	memset(request, 0, sizeof(*request));
	request->version = GPU_ABI_VERSION;
	request->size = sizeof(*request);
	request->lease = 7U;
	request->handle = handle;
	request->width = 64U;
	request->height = 64U;
	request->stride = 256U;
	request->format = GPU_PIXEL_BGRA8888;
	request->generation = 1U;
}
