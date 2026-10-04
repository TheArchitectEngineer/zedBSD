/* -*- mode: c; c-file-style: "linux"; tab-width: 8; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws113-p012: the GPU core's display control (GPU_DISPLAY_POWER and
 * GPU_DISPLAY_REFRESH) on the host, with the real GPU core and cdev
 * registry and the ws014 fixture's allocator, credentials, copies and
 * locks (plan/ws014/tests/gpu-framework.c), and a display-only backend that
 * records what it is asked.
 */

#define main gpu_framework_unused_main
#include "../../ws014/tests/gpu-framework.c"
#undef main

#include <kern/process.h>
#include <kern/filedesc.h>

/* What the display backend was asked, and what it answers. */
static unsigned control_powers;
static uint32_t control_power_state;
static int control_power_error;
static unsigned control_refreshes;
static uint64_t control_refresh_timeout;
static uint64_t control_refresh_answer;

void gpu_test_set_process(struct process *process);
static int control_query(void *opaque, void *session, struct gpu_display_info *request);
static int control_mode(void *opaque, void *session, struct gpu_display_mode *request);
static int control_claim(void *opaque, void *session, struct gpu_display_claim *request);
static int control_release(void *opaque, void *session, const struct gpu_display_release *request);
static int control_present(void *opaque, void *session, void *object, struct gpu_display_present *request);
static int control_wait(void *opaque, void *session, struct gpu_display_wait *request);
static int control_power(void *opaque, void *session, const struct gpu_display_power *request);
static int control_refresh(void *opaque, void *session, struct gpu_display_refresh *request);
static int control_import(void *opaque, void *session, const struct gpu_image_descriptor *image, const struct drv_gpu_scanout_backing *backing, void **result);
static void control_destroy(void *opaque, void *session, void *object);
static int control_device(void *opaque, void *session, struct gpu_device_info *request);
static int control_constraints(void *opaque, void *session, struct gpu_scanout_constraints *request);
static int control_ioctl(struct test_file *opened, unsigned long command, void *request);
static void prepare_power(struct gpu_display_power *request, uint32_t state);
static void prepare_refresh(struct gpu_display_refresh *request, uint64_t cursor, uint64_t timeout_ns);

/*
 * Registers display backends with and without the control and drives both requests.
 */
int
main(
	void)
{
	struct drv_gpu_ops native;
	struct drv_gpu_scanout_ops scanout;
	struct drv_gpu_display_ops display;
	struct drv_gpu_device *device;
	struct test_backend backend;
	struct test_file writer;
	struct test_file reader;
	struct process process;
	struct gpu_display_power power;
	struct gpu_display_refresh refresh;
	int error;

	/* The caller's process, and a display-only backend with every display operation. */
	memset(&process, 0, sizeof(process));
	process.fd = filedesc_create(&process);
	assert(process.fd != NULL);
	gpu_test_set_process(&process);
	memset(&native, 0, sizeof(native));
	memset(&scanout, 0, sizeof(scanout));
	memset(&display, 0, sizeof(display));
	memset(&backend, 0, sizeof(backend));
	native.version = DRV_GPU_INTERFACE_VERSION;
	native.size = sizeof(native);
	native.open = backend_open;
	native.close = backend_close;
	native.get_info = backend_get_info;
	native.resource_destroy = control_destroy;
	scanout.query_device = control_device;
	scanout.constraints = control_constraints;
	scanout.import_image = control_import;
	native.scanout = &scanout;
	display.query = control_query;
	display.mode = control_mode;
	display.claim = control_claim;
	display.release = control_release;
	display.present = control_present;
	display.wait = control_wait;
	native.display = &display;

	/* The control advertised without its operations, or its operations without the capability: refused. */
	native.capabilities = GPU_CAP_DISPLAY;
	error = drv_gpu_register(&native, &backend, &device);
	assert(error == 0);
	error = drv_gpu_unregister(device);
	assert(error == 0);
	native.capabilities = GPU_CAP_DISPLAY | GPU_CAP_DISPLAY_CONTROL;
	error = drv_gpu_register(&native, &backend, &device);
	assert(error == EINVAL);
	display.power = control_power;
	error = drv_gpu_register(&native, &backend, &device);
	assert(error == EINVAL);
	display.refresh = control_refresh;
	native.capabilities = GPU_CAP_DISPLAY;
	error = drv_gpu_register(&native, &backend, &device);
	assert(error == EINVAL);
	puts("ok: the control's capability and its two operations come together");

	/* A display without the control answers EOPNOTSUPP to both requests. */
	display.power = NULL;
	display.refresh = NULL;
	error = drv_gpu_register(&native, &backend, &device);
	assert(error == 0);
	error = open_file(&writer, "gpu0", O_RDWR);
	assert(error == 0);
	prepare_power(&power, GPU_DISPLAY_POWER_OFF);
	error = control_ioctl(&writer, GPU_DISPLAY_POWER, &power);
	assert(error == EOPNOTSUPP);
	prepare_refresh(&refresh, 0U, 0U);
	error = control_ioctl(&writer, GPU_DISPLAY_REFRESH, &refresh);
	assert(error == EOPNOTSUPP);
	close_file(&writer);
	error = drv_gpu_unregister(device);
	assert(error == 0);
	puts("ok: a display without the control answers EOPNOTSUPP");

	/* With the control. */
	display.power = control_power;
	display.refresh = control_refresh;
	native.capabilities = GPU_CAP_DISPLAY | GPU_CAP_DISPLAY_CONTROL;
	error = drv_gpu_register(&native, &backend, &device);
	assert(error == 0);
	error = open_file(&writer, "gpu0", O_RDWR);
	assert(error == 0);
	error = open_file(&reader, "gpu0", O_RDONLY);
	assert(error == 0);

	/* POWER: a writer, a display of a generation, one of the three states, reserved zero. */
	prepare_power(&power, GPU_DISPLAY_POWER_OFF);
	error = control_ioctl(&reader, GPU_DISPLAY_POWER, &power);
	assert(error == EACCES && control_powers == 0U);
	prepare_power(&power, 3U);
	error = control_ioctl(&writer, GPU_DISPLAY_POWER, &power);
	assert(error == EINVAL && control_powers == 0U);
	prepare_power(&power, GPU_DISPLAY_POWER_OFF);
	power.reserved = 1U;
	error = control_ioctl(&writer, GPU_DISPLAY_POWER, &power);
	assert(error == EINVAL && control_powers == 0U);
	prepare_power(&power, GPU_DISPLAY_POWER_OFF);
	power.display_id = 0U;
	error = control_ioctl(&writer, GPU_DISPLAY_POWER, &power);
	assert(error == EINVAL && control_powers == 0U);
	prepare_power(&power, GPU_DISPLAY_POWER_OFF);
	power.size = sizeof(power) - 8U;
	error = control_ioctl(&writer, GPU_DISPLAY_POWER, &power);
	assert(error == EINVAL && control_powers == 0U);
	puts("ok: POWER checks the open, the display, the state and the framing before the backend");

	/* POWER reaches the backend with its state, and the backend's refusal comes back. */
	prepare_power(&power, GPU_DISPLAY_POWER_SUSPEND);
	error = control_ioctl(&writer, GPU_DISPLAY_POWER, &power);
	assert(error == 0 && control_powers == 1U && control_power_state == GPU_DISPLAY_POWER_SUSPEND);
	control_power_error = EBUSY;
	prepare_power(&power, GPU_DISPLAY_POWER_ON);
	error = control_ioctl(&writer, GPU_DISPLAY_POWER, &power);
	assert(error == EBUSY && control_powers == 2U);
	control_power_error = 0;
	puts("ok: POWER reaches the backend and its EBUSY comes back");

	/* REFRESH: a reader may wait; outputs on input are refused. */
	prepare_refresh(&refresh, 0U, 0U);
	refresh.sequence = 1U;
	error = control_ioctl(&reader, GPU_DISPLAY_REFRESH, &refresh);
	assert(error == EINVAL && control_refreshes == 0U);
	prepare_refresh(&refresh, 0U, 0U);
	refresh.flags = 1U;
	error = control_ioctl(&reader, GPU_DISPLAY_REFRESH, &refresh);
	assert(error == EINVAL && control_refreshes == 0U);
	prepare_refresh(&refresh, 0U, 0U);
	refresh.generation = 0U;
	error = control_ioctl(&reader, GPU_DISPLAY_REFRESH, &refresh);
	assert(error == EINVAL && control_refreshes == 0U);
	puts("ok: REFRESH refuses outputs on input and a display without a generation");

	/* REFRESH bounds the wait to a second and returns the boundary after the cursor. */
	control_refresh_answer = 8U;
	prepare_refresh(&refresh, 7U, 5000000000ULL);
	error = control_ioctl(&reader, GPU_DISPLAY_REFRESH, &refresh);
	assert(error == 0 && control_refresh_timeout == 1000000000ULL);
	assert(refresh.sequence == 8U && refresh.time_ns == 8000U && refresh.version == GPU_ABI_VERSION);
	puts("ok: REFRESH bounds one wait to a second and reports the boundary");

	/* A backend answer that is not after the cursor is a driver error. */
	control_refresh_answer = 7U;
	prepare_refresh(&refresh, 7U, 1000U);
	error = control_ioctl(&reader, GPU_DISPLAY_REFRESH, &refresh);
	assert(error == EIO);
	control_refresh_answer = 0U;
	prepare_refresh(&refresh, 0U, 0U);
	error = control_ioctl(&reader, GPU_DISPLAY_REFRESH, &refresh);
	assert(error == 0 && refresh.sequence == 0U);
	puts("ok: a boundary not after the cursor is EIO; cursor zero takes the count as it is");

	/* Closed and withdrawn without leaks. */
	close_file(&reader);
	close_file(&writer);
	error = drv_gpu_unregister(device);
	assert(error == 0);
	filedesc_destroy(process.fd);
	gpu_test_set_process(NULL);
	assert(allocations == 0U && credential_references == 0U && held_spinlocks == 0U);
	puts("host-display-control: PASS");
	return 0;
}

/* Describes one connected display. */
static int
control_query(
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
	request->flags = GPU_DISPLAY_CONNECTED | GPU_DISPLAY_POWER_CONTROL | GPU_DISPLAY_REFRESH_COUNTER;
	return 0;
}

/* Has no modes (not used here). */
static int
control_mode(
	void *opaque,
	void *session,
	struct gpu_display_mode *request)
{
	(void)opaque;
	(void)session;
	(void)request;
	return EOPNOTSUPP;
}

/* Gives no lease (not used here). */
static int
control_claim(
	void *opaque,
	void *session,
	struct gpu_display_claim *request)
{
	(void)opaque;
	(void)session;
	(void)request;
	return EBUSY;
}

/* Holds no lease (not used here). */
static int
control_release(
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
control_present(
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
control_wait(
	void *opaque,
	void *session,
	struct gpu_display_wait *request)
{
	(void)opaque;
	(void)session;
	(void)request;
	return EINVAL;
}

/* Records a power request. */
static int
control_power(
	void *opaque,
	void *session,
	const struct gpu_display_power *request)
{
	(void)opaque;
	(void)session;
	assert(held_spinlocks == 0U);
	control_powers++;
	control_power_state = request->state;
	return control_power_error;
}

/* Records a refresh wait and answers the chosen boundary. */
static int
control_refresh(
	void *opaque,
	void *session,
	struct gpu_display_refresh *request)
{
	(void)opaque;
	(void)session;
	assert(held_spinlocks == 0U);
	control_refreshes++;
	control_refresh_timeout = request->timeout_ns;
	request->sequence = control_refresh_answer;
	request->time_ns = control_refresh_answer * 1000U;
	return 0;
}

/* Imports no scanout image (not used here). */
static int
control_import(
	void *opaque,
	void *session,
	const struct gpu_image_descriptor *image,
	const struct drv_gpu_scanout_backing *backing,
	void **result)
{
	(void)opaque;
	(void)session;
	(void)image;
	(void)backing;
	(void)result;
	return EOPNOTSUPP;
}

/* Describes no device role (not used here). */
static int
control_device(
	void *opaque,
	void *session,
	struct gpu_device_info *request)
{
	(void)opaque;
	(void)session;
	(void)request;
	return EOPNOTSUPP;
}

/* Has no scanout constraints (not used here). */
static int
control_constraints(
	void *opaque,
	void *session,
	struct gpu_scanout_constraints *request)
{
	(void)opaque;
	(void)session;
	(void)request;
	return EOPNOTSUPP;
}

/* Destroys nothing (not used here). */
static void
control_destroy(
	void *opaque,
	void *session,
	void *object)
{
	(void)opaque;
	(void)session;
	(void)object;
}

/* Sends one request through the real cdev and GPU dispatch. */
static int
control_ioctl(
	struct test_file *opened,
	unsigned long command,
	void *request)
{
	int error;

	/* The real dispatch chain. */
	error = cdev_file_ops.ioctl(&opened->file, command, (uintptr_t)request);
	return error;
}

/* A well-formed POWER request for display 1, generation 1. */
static void
prepare_power(
	struct gpu_display_power *request,
	uint32_t state)
{
	memset(request, 0, sizeof(*request));
	request->version = GPU_ABI_VERSION;
	request->size = sizeof(*request);
	request->display_id = 1U;
	request->generation = 1U;
	request->state = state;
}

/* A well-formed REFRESH request for display 1, generation 1. */
static void
prepare_refresh(
	struct gpu_display_refresh *request,
	uint64_t cursor,
	uint64_t timeout_ns)
{
	memset(request, 0, sizeof(*request));
	request->version = GPU_ABI_VERSION;
	request->size = sizeof(*request);
	request->display_id = 1U;
	request->generation = 1U;
	request->cursor = cursor;
	request->timeout_ns = timeout_ns;
}
