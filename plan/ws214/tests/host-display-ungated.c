/* -*- mode: c; c-file-style: "linux"; tab-width: 8; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws214-p003a: the GPU core's ungated display operations
 * (DRV_GPU_DISPLAY_UNGATED) on the host, with the real GPU core and cdev
 * registry and the ws014 fixture (plan/ws014/tests/gpu-framework.c): the
 * table's checks, present and the cursor outside the open's admission, the
 * pin that holds a resource only through the callback, the adoption wait
 * after it, and a destruction that waits for the pin.
 */

#define main gpu_framework_unused_main
#include "../../ws014/tests/gpu-framework.c"
#undef main

#include <kern/process.h>
#include <kern/filedesc.h>

/* What the display callbacks saw and answer, and what they do from inside. */
static struct test_file *ungated_file;
static uint64_t ungated_handle;
static unsigned ungated_presents;
static unsigned ungated_waits;
static unsigned ungated_adoptions;
static unsigned ungated_moves;
static unsigned ungated_sets;
static int ungated_nested_move;
static int ungated_nested_info;
static int ungated_nested_destroy;
static int ungated_destroy_in_wait;
static unsigned ungated_sleeps;
static bool ungated_move_inside;
static bool ungated_destroy_inside;
static bool ungated_destroy_in_adoption;
static bool ungated_gated;

extern int (*gpu_test_wait_hook)(struct wait_queue *, struct spinlock *, uint64_t, uint64_t, unsigned);
void gpu_test_set_process(struct process *process);
static int ungated_query(void *opaque, void *session, struct gpu_display_info *request);
static int ungated_mode(void *opaque, void *session, struct gpu_display_mode *request);
static int ungated_claim(void *opaque, void *session, struct gpu_display_claim *request);
static int ungated_release(void *opaque, void *session, const struct gpu_display_release *request);
static int ungated_present(void *opaque, void *session, void *object, struct gpu_display_present *request);
static int ungated_adoption(void *opaque, void *session, struct gpu_display_present *request);
static int ungated_wait(void *opaque, void *session, struct gpu_display_wait *request);
static int ungated_set(void *opaque, void *session, void *object, const struct gpu_display_cursor_image *request);
static int ungated_move(void *opaque, void *session, const struct gpu_display_cursor_move *request);
static int ungated_hide(void *opaque, void *session, const struct gpu_display_cursor_hide *request);
static int ungated_sleep(struct wait_queue *queue, struct spinlock *lock, uint64_t observed, uint64_t deadline, unsigned flags);
static int ungated_ioctl(struct test_file *opened, unsigned long command, void *request);
static void prepare_present(struct gpu_display_present *request, uint64_t handle);
static void prepare_move(struct gpu_display_cursor_move *request);

/*
 * Registers ungated and gated display backends and drives present, wait, the cursor and destruction.
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
	struct process process;
	struct gpu_resource_create create;
	struct gpu_display_present present;
	struct gpu_display_wait wait;
	struct gpu_display_cursor_image image;
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
	display.query = ungated_query;
	display.mode = ungated_mode;
	display.claim = ungated_claim;
	display.release = ungated_release;
	display.present = ungated_present;
	display.wait = ungated_wait;
	display.cursor_set = ungated_set;
	display.cursor_move = ungated_move;
	display.cursor_hide = ungated_hide;
	native.display = &display;

	/* The flag and the adoption wait come together; an unknown flag is refused. */
	display.flags = DRV_GPU_DISPLAY_UNGATED;
	error = drv_gpu_register(&native, &backend, &device);
	assert(error == EINVAL);
	display.flags = 0;
	display.adoption_wait = ungated_adoption;
	error = drv_gpu_register(&native, &backend, &device);
	assert(error == EINVAL);
	display.flags = DRV_GPU_DISPLAY_UNGATED | 2U;
	error = drv_gpu_register(&native, &backend, &device);
	assert(error == EINVAL);
	puts("ok: DRV_GPU_DISPLAY_UNGATED comes with adoption_wait, and unknown flags are refused");

	/* An ungated display, a writer and one storage resource. */
	display.flags = DRV_GPU_DISPLAY_UNGATED;
	error = drv_gpu_register(&native, &backend, &device);
	assert(error == 0);
	error = open_file(&writer, "gpu0", O_RDWR);
	assert(error == 0);
	ungated_file = &writer;
	prepare_create(&create);
	create.bytes = 16384;
	error = ungated_ioctl(&writer, GPU_RESOURCE_CREATE, &create);
	assert(error == 0 && create.handle != 0U);
	ungated_handle = create.handle;

	/*
	 * Present records outside admission: from inside its callback, the same
	 * open's cursor move and an admitted request go through; then the
	 * adoption wait numbers the frame.
	 */
	prepare_present(&present, create.handle);
	ungated_move_inside = true;
	error = ungated_ioctl(&writer, GPU_DISPLAY_PRESENT, &present);
	assert(error == 0 && ungated_presents == 1U && ungated_adoptions == 1U);
	assert(present.sequence == 5U);
	assert(ungated_nested_move == 0 && ungated_moves == 1U && ungated_nested_info == 0);
	puts("ok: a present's callback runs outside admission; the cursor and admitted requests go on meanwhile");

	/* A destruction during the present's callback waits for the pin; interrupted, it leaves the resource. */
	prepare_present(&present, create.handle);
	ungated_destroy_inside = true;
	gpu_test_wait_hook = ungated_sleep;
	error = ungated_ioctl(&writer, GPU_DISPLAY_PRESENT, &present);
	gpu_test_wait_hook = NULL;
	assert(error == 0 && ungated_nested_destroy == EINTR && ungated_sleeps == 1U);
	assert(backend.destroyed == 0U);
	puts("ok: destruction waits while an ungated callback pins the resource");

	/* By the adoption wait the pin is gone: a destruction there does not wait. */
	prepare_present(&present, create.handle);
	ungated_destroy_in_adoption = true;
	gpu_test_wait_hook = ungated_sleep;
	error = ungated_ioctl(&writer, GPU_DISPLAY_PRESENT, &present);
	gpu_test_wait_hook = NULL;
	assert(error == 0 && ungated_destroy_in_wait == 0 && ungated_sleeps == 0U && backend.destroyed == 1U);
	puts("ok: the adoption wait holds no pin");

	/* A destroyed handle no longer resolves for present or cursor. */
	prepare_present(&present, create.handle);
	error = ungated_ioctl(&writer, GPU_DISPLAY_PRESENT, &present);
	assert(error == EINVAL && ungated_presents == 3U);
	memset(&image, 0, sizeof(image));
	image.version = GPU_ABI_VERSION;
	image.size = sizeof(image);
	image.lease = 7U;
	image.handle = create.handle;
	image.width = 16U;
	image.height = 16U;
	image.stride = 64U;
	image.format = GPU_PIXEL_BGRA8888;
	image.generation = 1U;
	error = ungated_ioctl(&writer, GPU_DISPLAY_CURSOR_SET, &image);
	assert(error == EINVAL && ungated_sets == 0U);

	/* The cursor set and the wait reach the backend through the ungated path. */
	prepare_create(&create);
	create.bytes = 16384;
	error = ungated_ioctl(&writer, GPU_RESOURCE_CREATE, &create);
	assert(error == 0);
	image.handle = create.handle;
	error = ungated_ioctl(&writer, GPU_DISPLAY_CURSOR_SET, &image);
	assert(error == 0 && ungated_sets == 1U);
	memset(&wait, 0, sizeof(wait));
	wait.version = GPU_ABI_VERSION;
	wait.size = sizeof(wait);
	wait.lease = 7U;
	wait.sequence = 1U;
	error = ungated_ioctl(&writer, GPU_DISPLAY_WAIT, &wait);
	assert(error == 0 && ungated_waits == 1U && wait.completed_sequence == 5U);
	error = destroy_handle(&writer, create.handle);
	assert(error == 0);
	close_file(&writer);
	error = drv_gpu_unregister(device);
	assert(error == 0);
	puts("ok: cursor set and wait take the ungated path");

	/* A gated display keeps the admission: the same nested move is refused as recursive. */
	display.flags = 0;
	display.adoption_wait = NULL;
	ungated_gated = true;
	error = drv_gpu_register(&native, &backend, &device);
	assert(error == 0);
	error = open_file(&writer, "gpu0", O_RDWR);
	assert(error == 0);
	ungated_file = &writer;
	prepare_create(&create);
	create.bytes = 16384;
	error = ungated_ioctl(&writer, GPU_RESOURCE_CREATE, &create);
	assert(error == 0);
	ungated_handle = create.handle;
	prepare_present(&present, create.handle);
	ungated_move_inside = true;
	error = ungated_ioctl(&writer, GPU_DISPLAY_PRESENT, &present);
	assert(error == 0 && ungated_nested_move == EBUSY && ungated_adoptions == 3U && ungated_presents == 4U);
	assert(present.sequence == 5U);
	error = destroy_handle(&writer, create.handle);
	assert(error == 0);
	close_file(&writer);
	error = drv_gpu_unregister(device);
	assert(error == 0);
	puts("ok: a gated display's present still holds the admission");

	/* Closed and withdrawn without leaks or held locks. */
	filedesc_destroy(process.fd);
	gpu_test_set_process(NULL);
	assert(allocations == 0U && credential_references == 0U && held_spinlocks == 0U);
	puts("host-display-ungated: PASS");
	return 0;
}

/* Describes one connected display with a cursor plane. */
static int
ungated_query(
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
ungated_mode(
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
ungated_claim(
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
ungated_release(
	void *opaque,
	void *session,
	const struct gpu_display_release *request)
{
	(void)opaque;
	(void)session;
	(void)request;
	return EINVAL;
}

/*
 * Records a present: holds no core lock, and from inside does what the
 * scenario asks (a cursor move and an admitted request on the same open, or
 * a destruction of the pinned resource).  An ungated record leaves its
 * number 77 in sequence; a gated present numbers the frame 5 at once.
 */
static int
ungated_present(
	void *opaque,
	void *session,
	void *object,
	struct gpu_display_present *request)
{
	struct gpu_display_cursor_move move;
	struct gpu_info info;
	struct test_backend *backend;

	backend = opaque;
	(void)session;
	assert(held_spinlocks == 0);
	assert(object != NULL);
	ungated_presents++;
	if (ungated_move_inside) {
		ungated_move_inside = false;
		prepare_move(&move);
		ungated_nested_move = ungated_ioctl(ungated_file, GPU_DISPLAY_CURSOR_MOVE, &move);
		memset(&info, 0, sizeof(info));
		info.version = GPU_ABI_VERSION;
		info.size = sizeof(info);
		ungated_nested_info = 0;
		if (ungated_nested_move == 0)
			ungated_nested_info = ungated_ioctl(ungated_file, GPU_GET_INFO, &info);
	}
	if (ungated_destroy_inside) {
		ungated_destroy_inside = false;
		ungated_sleeps = 0;
		ungated_nested_destroy = destroy_handle(ungated_file, ungated_handle);
	}
	(void)backend;
	request->sequence = 77U;
	if (ungated_gated)
		request->sequence = 5U;
	return 0;
}

/* Waits for nothing: checks the record's number and numbers the frame; may destroy the resource. */
static int
ungated_adoption(
	void *opaque,
	void *session,
	struct gpu_display_present *request)
{
	(void)opaque;
	(void)session;
	assert(held_spinlocks == 0);
	assert(request->sequence == 77U);
	ungated_adoptions++;
	if (ungated_destroy_in_adoption) {
		ungated_destroy_in_adoption = false;
		ungated_sleeps = 0;
		ungated_destroy_in_wait = destroy_handle(ungated_file, ungated_handle);
	}
	request->sequence = 5U;
	return 0;
}

/* Answers that frame 5 is completed. */
static int
ungated_wait(
	void *opaque,
	void *session,
	struct gpu_display_wait *request)
{
	(void)opaque;
	(void)session;
	assert(held_spinlocks == 0);
	ungated_waits++;
	request->completed_sequence = 5U;
	request->generation = 1U;
	return 0;
}

/* Records a cursor image set. */
static int
ungated_set(
	void *opaque,
	void *session,
	void *object,
	const struct gpu_display_cursor_image *request)
{
	(void)opaque;
	(void)session;
	(void)request;
	assert(held_spinlocks == 0);
	assert(object != NULL);
	ungated_sets++;
	return 0;
}

/* Records a cursor move. */
static int
ungated_move(
	void *opaque,
	void *session,
	const struct gpu_display_cursor_move *request)
{
	(void)opaque;
	(void)session;
	(void)request;
	assert(held_spinlocks == 0);
	ungated_moves++;
	return 0;
}

/* Records a cursor hide. */
static int
ungated_hide(
	void *opaque,
	void *session,
	const struct gpu_display_cursor_hide *request)
{
	(void)opaque;
	(void)session;
	(void)request;
	assert(held_spinlocks == 0);
	return 0;
}

/* A destruction's sleep for the pin: counted, and interrupted as a signal would. */
static int
ungated_sleep(
	struct wait_queue *queue,
	struct spinlock *lock,
	uint64_t observed,
	uint64_t deadline,
	unsigned flags)
{
	(void)queue;
	(void)lock;
	(void)observed;
	(void)deadline;
	assert((flags & WAITQ_INTERRUPTIBLE) != 0);
	ungated_sleeps++;
	return EINTR;
}

/* Sends one request through the real dispatch chain. */
static int
ungated_ioctl(
	struct test_file *opened,
	unsigned long command,
	void *request)
{
	int error;

	error = cdev_file_ops.ioctl(&opened->file, command, (uintptr_t)request);
	return error;
}

/* A well-formed 32x32 BGRA FIFO present from the start of the given storage, lease 7. */
static void
prepare_present(
	struct gpu_display_present *request,
	uint64_t handle)
{
	memset(request, 0, sizeof(*request));
	request->version = GPU_ABI_VERSION;
	request->size = sizeof(*request);
	request->lease = 7U;
	request->handle = handle;
	request->width = 32U;
	request->height = 32U;
	request->stride = 128U;
	request->format = GPU_PIXEL_BGRA8888;
	request->refresh_millihz = 60000U;
	request->flags = GPU_DISPLAY_PRESENT_FIFO;
	request->generation = 1U;
}

/* A well-formed cursor move of lease 7. */
static void
prepare_move(
	struct gpu_display_cursor_move *request)
{
	memset(request, 0, sizeof(*request));
	request->version = GPU_ABI_VERSION;
	request->size = sizeof(*request);
	request->lease = 7U;
	request->x = 3;
	request->y = 4;
	request->generation = 1U;
}
