/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The display thread and the changes of the display (design 4.3): what
 * waits when the output opens, closes or moves -- the swapchain's and the
 * surface's creation and destruction (which claim and give back the
 * display), the cursor plane's, the cursor thread's stop, the device's
 * idle -- runs on one resident thread, a lane named "display", while the
 * event loop goes on taking the clients' requests and the input.
 *
 * A change of the display begins (kwl_display_begin): nothing is wanted of
 * the render thread from then on, and its park is asked without waiting.
 * The render thread parks after the frame in flight and the tasks given
 * before (drained: nothing it submitted still runs on the GPU) and says so
 * through its queue; the change goes on from the event loop's next look
 * (kwl_display_parked).  Its steps (kwl_display_close, kwl_display_open)
 * each do the main thread's part (what the frames use: the targets, the
 * scene, the layers, the heads), give the display thread its part, and go
 * on when the display thread's result is taken after the wake pipe
 * (kwl_display_take).  The change ends (kwl_display_end): the render
 * thread goes on and the next pass draws.  The display thread writes no
 * field of the compositor's: it works on the job's copies (the swapchain's
 * record, the cursor thread's) and the main thread takes them in.
 *
 * Where waiting is the point -- the end, the hand-over to another
 * compositor, the seat's pause -- or without the thread, the same steps
 * run on the event loop's thread, one after the other (display_inline).
 * A change running when such a caller comes ends first (kwl_display_settle).
 *
 * The game mode's question whether the display could show a window
 * straight (kl_backend_scanout_possible, which opens the GPU device) is the
 * display thread's too.
 */

#include "compose.h"
#include "cursor-thread.h"
#include "extras.h"

#include "userland/desktop/libkeiland-backend/keiland-backend-display.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* How long a change of the display may take before a caller that waits for it ends the compositor, in milliseconds. */
#define DISPLAY_SETTLE_MS	10000U

/* How long one wait for the render thread's park lasts while a caller waits, in milliseconds. */
#define DISPLAY_PARK_LOOK_MS	100U

/* What a job of the display thread does. */
enum display_kind {
	/* The output's cursor plane and swapchain destroyed, the display given back. */
	DISPLAY_CLOSE = 1,
	/* The display taken, the output's swapchain made, and the cursor plane's. */
	DISPLAY_OPEN,
	/* Whether the display could show a window straight (the game mode). */
	DISPLAY_SCANOUT_QUERY
};

/*
 * One job of the display thread: its kind, the server (read on the thread
 * for the backend and the device only), what goes on with its result
 * (then and context), and its inputs and results.
 *
 * output is the output's swapchain record (the one to destroy, or the
 * prepared surface whose swapchain is made, as the main thread gave it),
 * display the output's display; parts the cursor plane taken off the
 * output (DISPLAY_CLOSE), plane whether a cursor plane is to be made and
 * cursor, plane_extent and plane_index the one made (DISPLAY_OPEN);
 * readback and scene_supported what the swapchain's images allow; width
 * and height the game mode's size (DISPLAY_SCANOUT_QUERY); error the
 * result.  spare says the job is display_spare (no memory for one).
 */
struct display_job {
	struct kwl_lane_job lane;
	enum display_kind kind;
	struct kwl_server *server;
	kwl_display_fn *then;
	void *context;
	struct vkdemo_display output;
	VkDisplayKHR display;
	struct kwl_cursor_parts parts;
	unsigned plane;
	struct kwl_cursor_thread *cursor;
	VkExtent2D plane_extent;
	uint32_t plane_index;
	int readback;
	unsigned scene_supported;
	uint32_t width;
	uint32_t height;
	int error;
	unsigned spare;
};

/* A change of the display made of its steps: what goes on with its result (then and context). */
struct display_change {
	kwl_display_fn *then;
	void *context;
};

static struct display_job *display_job_new(struct kwl_server *server, enum display_kind kind, kwl_display_fn *then, void *context);
static void display_job_free(struct display_job *job);
static void display_submit(struct kwl_server *server, struct display_job *job);
static void display_run(struct kwl_lane_job *lane);
static void display_done(void *context, struct kwl_lane_job *lane);
static void display_drop(struct kwl_lane_job *lane);
static void display_open_run(struct display_job *job);
static struct display_change *display_change_new(kwl_display_fn *then, void *context);
static void display_change_end(struct kwl_server *server, struct display_change *change, int error);
static void display_open_parked(struct kwl_server *server, int error, void *context);
static void display_open_opened(struct kwl_server *server, int error, void *context);
static void display_close_parked(struct kwl_server *server, int error, void *context);
static void display_close_closed(struct kwl_server *server, int error, void *context);
static void display_sync_then(struct kwl_server *server, int error, void *context);

/* The display thread's jobs. */
static const struct kwl_lane_ops display_ops = {
	display_run,
	display_done,
	display_drop
};

/* The job used when no memory is left for one: run on the event loop's thread at once, so one is enough. */
static struct display_job display_spare;
static unsigned display_spare_used;

/*
 * Starts the display thread once the render thread runs (it needs the
 * threads' wake pipe).  Without it the changes run on the event loop's
 * thread (logged).
 */
void
kwl_display_start(
	struct kwl_server *server)
{
	struct kwl_compose *compose;
	int error;

	/* Once, after the wake pipe. */
	compose = server->compose;
	if (compose == NULL || compose->display_started || !server->threads_prepared)
		return;

	/* The thread, which waits for the first job. */
	error = kwl_lane_start(&compose->display_lane, "display", 1U, server->wake[1]);
	if (error != 0) {
		printf("KWL DISPLAY thread=display errno=%d the display changes run in the event loop\n", error);
		return;
	}

	/* Succeeded: the changes run there from now on. */
	compose->display_started = 1U;
}

/*
 * Stops the display thread at the end, after the change running (if any)
 * has ended.
 */
void
kwl_display_stop(
	struct kwl_server *server)
{
	struct kwl_compose *compose;

	/* Nothing runs. */
	compose = server->compose;
	if (compose == NULL || !compose->display_started)
		return;

	/* The change running ends, then the thread (a question not taken is dropped). */
	kwl_display_settle(server);
	kwl_lane_stop(&compose->display_lane);
	compose->display_started = 0U;
}

/*
 * Takes what the display thread finished, on the event loop's thread,
 * after its wake pipe: each change goes on with its step's result.
 */
void
kwl_display_take(
	struct kwl_server *server)
{
	/* Nothing runs. */
	if (server->compose == NULL || !server->compose->display_started)
		return;

	/* Each result, in its order. */
	(void)kwl_lane_take(&server->compose->display_lane, server);
}

/* Tells whether a change of the display runs: no frame is drawn meanwhile. */
int
kwl_display_busy(
	struct kwl_server *server)
{
	/* Without the device nothing changes. */
	if (server->compose == NULL)
		return 0;

	/* Succeeded: whether one runs. */
	return (int)server->compose->display_busy;
}

/*
 * Waits for the change of the display running (if any) to end, taking the
 * render thread's park and the display thread's results meanwhile: only
 * where waiting is the point (the end, the hand-over, the seat's pause),
 * or before a park of the event loop's own while a change's park is asked.
 * A change that does not end in DISPLAY_SETTLE_MS ends the compositor.
 */
void
kwl_display_settle(
	struct kwl_server *server)
{
	struct kwl_compose *compose;
	uint64_t deadline;
	unsigned taken;
	unsigned busy;

	/* Nothing runs. */
	compose = server->compose;
	if (compose == NULL || !compose->display_busy)
		return;

	/* Until it ends. */
	deadline = kwl_milliseconds() + DISPLAY_SETTLE_MS;
	while (compose->display_busy) {
		/* A change that lasts leaves nothing safe. */
		if (kwl_milliseconds() > deadline) {
			kwl_fatal("KWL FAILED site=display errno=%d step=settle parking=%u", ETIMEDOUT, compose->display_parking);
			_exit(1);
		}

		/* Its park: the render thread's queue, which tells it, then the next look. */
		if (compose->display_parking) {
			(void)kwl_render_wait_message(&compose->render, DISPLAY_PARK_LOOK_MS);
			kwl_compose_poll(server);
			continue;
		}

		/* A step's result the display thread finished, which may give the next step. */
		taken = kwl_lane_take(&compose->display_lane, server);
		if (taken != 0U)
			continue;

		/* A step on the display thread: its end. */
		busy = kwl_lane_busy(&compose->display_lane);
		if (busy) {
			kwl_lane_finish(&compose->display_lane, server);
			continue;
		}

		/* Nothing it waits for: a change left half done is a fault of this file. */
		kwl_fatal("KWL FAILED site=display errno=%d step=settle-stuck", EINVAL);
		_exit(1);
	}
}

/*
 * Begins a change of the display: nothing is wanted of the render thread
 * from now on, and its park is asked; parked(server, 0, context) is called
 * once it is parked and drained, on the event loop's thread (at once when
 * the steps run there).  Returns 0, or EBUSY while another change runs
 * (the caller tries again later).
 */
int
kwl_display_begin(
	struct kwl_server *server,
	kwl_display_fn *parked,
	void *context)
{
	struct kwl_render_message *message;
	struct kwl_compose *compose;

	/* One change at a time. */
	compose = server->compose;
	if (compose->display_busy)
		return EBUSY;
	compose->display_busy = 1U;
	compose->display_began_ms = kwl_milliseconds();

	/* No frame from now on. */
	kwl_compose_want_nothing(server);

	/*
	 * The park asked without waiting, told by its message; where the steps
	 * run here, or inside a park of the event loop's own (the thread is
	 * parked already, and stays so until this change ends too), at once.
	 */
	message = NULL;
	if (compose->render_started && !compose->display_inline && compose->park_depth == 0U)
		message = calloc(1, sizeof(*message));
	if (message == NULL) {
		kwl_compose_park(server);
		parked(server, 0, context);
		return 0;
	}

	/* Succeeded: the change goes on at the park's message (kwl_display_parked). */
	compose->display_parking = 1U;
	compose->display_step = parked;
	compose->display_context = context;
	kwl_render_park_request(&compose->render, message);
	return 0;
}

/*
 * Goes on with the change whose park the render thread answered (its
 * message taken by kwl_compose_poll, after the completions before it).
 */
void
kwl_display_parked(
	struct kwl_server *server)
{
	struct kwl_compose *compose;
	kwl_display_fn *step;
	void *context;

	/* Only a park asked by a change. */
	compose = server->compose;
	if (compose == NULL || !compose->display_parking)
		return;

	/* Parked as kwl_compose_park would have, until kwl_display_end. */
	compose->display_parking = 0U;
	compose->park_depth++;
	step = compose->display_step;
	context = compose->display_context;
	compose->display_step = NULL;
	compose->display_context = NULL;

	/* The change's next step. */
	step(server, 0, context);
}

/*
 * Ends a change of the display: the render thread goes on, and the next
 * pass draws.
 */
void
kwl_display_end(
	struct kwl_server *server)
{
	struct kwl_compose *compose;

	/* Only a change that runs. */
	compose = server->compose;
	if (compose == NULL || !compose->display_busy)
		return;

	/* The render thread steps again, and everything is drawn. */
	kwl_compose_unpark(server);
	compose->display_busy = 0U;
	server->dirty = 1;
	printf("KWL DISPLAY change ms=%llu\n", (unsigned long long)(kwl_milliseconds() - compose->display_began_ms));
}

/*
 * Closes the output, a step of a change of the display (the render thread
 * parked and drained): the main thread lets go of what the frames use and
 * takes the cursor plane off; the display thread stops the cursor thread,
 * waits for the device to be idle and destroys the plane's objects and the
 * output's swapchain and surface, which gives the display back.
 * then(server, 0, context) is called after.  A surface prepared but never
 * given a swapchain goes at once.
 */
void
kwl_display_close(
	struct kwl_server *server,
	kwl_display_fn *then,
	void *context)
{
	struct kwl_compose *compose;
	struct display_job *job;

	/* A surface prepared but never given a swapchain goes on its own. */
	compose = server->compose;
	if (!compose->output_open) {
		if (compose->output_prepared)
			vkdemo_display_close(compose->instance, compose->device, &compose->output);
		compose->output_prepared = 0;
		then(server, 0, context);
		return;
	}

	/* The plane taken off, then what the frames use let go. */
	job = display_job_new(server, DISPLAY_CLOSE, then, context);
	kwl_cursor_plane_detach(server, &job->parts);
	kwl_compose_output_let_go(server);

	/* The swapchain and the surface are the display thread's from now on. */
	job->output = compose->output;
	job->display = compose->display;
	memset(&compose->output, 0, sizeof(compose->output));
	compose->output_prepared = 0;
	display_submit(server, job);
}

/*
 * Opens the output, a step of a change of the display (the render thread
 * parked): the main thread prepares the display's surface (and the
 * pipelines the first time); the display thread takes the display, makes
 * the swapchain and the cursor plane's objects; the main thread takes them
 * in (the targets, the scene, the layers).  then(server, error, context) is
 * called after, with 0 or the step's errno (nothing of the output is kept
 * then).  An open output needs nothing.
 */
void
kwl_display_open(
	struct kwl_server *server,
	kwl_display_fn *then,
	void *context)
{
	struct kwl_compose *compose;
	struct display_job *job;
	int error;

	/* An open output needs nothing. */
	compose = server->compose;
	if (compose->output_open) {
		then(server, 0, context);
		return;
	}

	/* The surface, and the pass and pipelines for its format. */
	error = kwl_compose_output_prepare(server);
	if (error != 0) {
		then(server, error, context);
		return;
	}

	/* The display thread's part on a copy of the record, which the main thread does not use until it is taken in. */
	job = display_job_new(server, DISPLAY_OPEN, then, context);
	job->output = compose->output;
	job->display = compose->display;
	job->plane = server->threads_prepared;
	display_submit(server, job);
}

/*
 * Asks the display thread whether the display could show a window of a
 * size straight (the game mode, without claiming it); then(server, error,
 * context) is called with the backend's answer.  Returns 0.
 */
int
kwl_display_scanout_query(
	struct kwl_server *server,
	uint32_t width,
	uint32_t height,
	kwl_display_fn *then,
	void *context)
{
	struct display_job *job;

	/* The question. */
	job = display_job_new(server, DISPLAY_SCANOUT_QUERY, then, context);
	job->width = width;
	job->height = height;
	display_submit(server, job);
	return 0;
}

/*
 * Opens window mode's output as a change of its own: begun, opened, ended,
 * then the desktop fitted to what the output shows now against what it
 * showed when it last opened (kwl_output_rotated).  then(server, error,
 * context) is called after.  Returns 0 (also for an open output, then
 * called at once), or EBUSY while another change runs.
 */
int
kwl_display_output_open(
	struct kwl_server *server,
	kwl_display_fn *then,
	void *context)
{
	struct display_change *change;
	int error;

	/* An open output needs nothing. */
	if (server->compose->output_open) {
		then(server, 0, context);
		return 0;
	}

	/* The change. */
	change = display_change_new(then, context);
	if (change == NULL)
		return ENOMEM;
	error = kwl_display_begin(server, display_open_parked, change);
	if (error != 0)
		free(change);
	return error;
}

/*
 * Creates the swapchain of window mode's display surface (preparing the
 * surface first when that was not done), which claims the display, and
 * waits for it: where the caller needs the output open on return.
 * Returns 0 or the open's errno.
 */
int
kwl_compose_output_open(
	struct kwl_server *server)
{
	struct kwl_compose *compose;
	int result;
	int error;

	/* An open output needs nothing. */
	compose = server->compose;
	if (compose->output_open)
		return 0;

	/* A change running ends first; this one's steps run here. */
	kwl_display_settle(server);
	result = EINPROGRESS;
	compose->display_inline++;
	error = kwl_display_output_open(server, display_sync_then, &result);
	compose->display_inline--;
	if (error != 0)
		return error;

	/* The open's result. */
	return result;
}

/*
 * Destroys the swapchain and the display surface once the device is idle,
 * which gives the display back, and waits for it (the hand-over, the
 * seat's pause, the end).
 */
void
kwl_compose_output_close(
	struct kwl_server *server)
{
	struct display_change *change;
	struct kwl_compose *compose;
	int result;
	int error;

	/* Nothing to destroy. */
	compose = server->compose;
	if (compose == NULL)
		return;

	/* A change running ends first. */
	kwl_display_settle(server);
	if (!compose->output_open && !compose->output_prepared)
		return;

	/* This one's steps run here. */
	result = EINPROGRESS;
	change = display_change_new(display_sync_then, &result);
	if (change == NULL) {
		kwl_fatal("KWL FAILED site=display errno=%d step=close", ENOMEM);
		_exit(1);
	}
	compose->display_inline++;
	error = kwl_display_begin(server, display_close_parked, change);
	compose->display_inline--;
	if (error != 0) {
		free(change);
		kwl_fatal("KWL FAILED site=display errno=%d step=close", error);
		_exit(1);
	}
}

/* A job of a kind, for then and context (display_spare when no memory is left: it runs here at once). */
static struct display_job *
display_job_new(
	struct kwl_server *server,
	enum display_kind kind,
	kwl_display_fn *then,
	void *context)
{
	struct display_job *job;

	/* A new one, or the spare. */
	job = calloc(1, sizeof(*job));
	if (job == NULL) {
		if (display_spare_used) {
			kwl_fatal("KWL FAILED site=display errno=%d step=job", ENOMEM);
			_exit(1);
		}
		display_spare_used = 1U;
		memset(&display_spare, 0, sizeof(display_spare));
		job = &display_spare;
		job->spare = 1U;
	}

	/* Succeeded: its kind and what goes on with its result. */
	job->kind = kind;
	job->server = server;
	job->then = then;
	job->context = context;
	return job;
}

/* Frees a job (the spare is only given back). */
static void
display_job_free(
	struct display_job *job)
{
	/* The spare is free again. */
	if (job->spare) {
		display_spare_used = 0U;
		return;
	}

	/* Succeeded: freed. */
	free(job);
}

/* Gives a job to the display thread, or runs it and takes its result here (the steps run here, or no thread, or the spare). */
static void
display_submit(
	struct kwl_server *server,
	struct display_job *job)
{
	struct kwl_compose *compose;

	/* Here, at once. */
	compose = server->compose;
	if (!compose->display_started || compose->display_inline || job->spare) {
		display_run(&job->lane);
		display_done(server, &job->lane);
		return;
	}

	/* Succeeded: the thread has it. */
	kwl_lane_submit(&compose->display_lane, &job->lane, &display_ops, 0U);
}

/* Runs a job on the display thread (or here): it writes the job only. */
static void
display_run(
	struct kwl_lane_job *lane)
{
	struct display_job *job;
	struct kwl_compose *compose;

	/* By its kind. */
	job = (struct display_job *)lane;
	compose = job->server->compose;
	switch (job->kind) {
	case DISPLAY_CLOSE:
		/* The cursor thread stopped and the plane's objects destroyed (which waits for the device), else the device idle. */
		job->error = 0;
		if (job->parts.thread != NULL)
			job->error = kwl_cursor_plane_stop(compose, job->parts.thread);
		else
			kwl_device_wait_idle(compose);
		if (job->error != 0)
			break;

		/* The swapchain and the surface, then the display given back. */
		vkdemo_display_close(compose->instance, compose->device, &job->output);
		kwl_os_display_release(job->server, compose->physical, job->display);
		break;
	case DISPLAY_OPEN:
		display_open_run(job);
		break;
	case DISPLAY_SCANOUT_QUERY:
		job->error = kl_backend_scanout_possible(job->server->backend, job->width, job->height);
		break;
	default:
		job->error = EINVAL;
		break;
	}
}

/* Takes a job's result on the event loop's thread, and goes on with the change. */
static void
display_done(
	void *context,
	struct kwl_lane_job *lane)
{
	struct kwl_cursor_thread *cursor;
	struct kwl_server *server;
	struct kwl_compose *compose;
	struct display_job *job;
	kwl_display_fn *then;
	VkExtent2D plane_extent;
	uint32_t plane_index;
	unsigned scene_supported;
	void *then_context;
	int error;

	/* What goes on with it, kept before the job goes. */
	server = context;
	compose = server->compose;
	job = (struct display_job *)lane;
	then = job->then;
	then_context = job->context;
	error = job->error;

	/* By its kind. */
	switch (job->kind) {
	case DISPLAY_CLOSE:
		/* The plane's copies and record go (a thread whose copies did not finish ends the compositor). */
		if (job->parts.thread != NULL)
			kwl_cursor_plane_detached(server, &job->parts, error);
		display_job_free(job);
		printf("KWL OUTPUT closed\n");
		then(server, 0, then_context);
		return;
	case DISPLAY_OPEN:
		/* A failure keeps nothing of the output (the display thread destroyed what it made). */
		if (error != 0) {
			display_job_free(job);
			memset(&compose->output, 0, sizeof(compose->output));
			compose->output_prepared = 0;
			then(server, error, then_context);
			return;
		}

		/* The swapchain is the output's, and the main thread's part takes it in. */
		compose->output = job->output;
		compose->readback = (unsigned)job->readback;
		scene_supported = job->scene_supported;
		cursor = job->cursor;
		plane_extent = job->plane_extent;
		plane_index = job->plane_index;
		display_job_free(job);
		error = kwl_compose_output_take(server, scene_supported, cursor, plane_extent, plane_index);
		then(server, error, then_context);
		return;
	default:
		display_job_free(job);
		then(server, error, then_context);
		return;
	}
}

/* Frees a job not run (the thread stopped): its change never goes on, at the end. */
static void
display_drop(
	struct kwl_lane_job *lane)
{
	/* Freed. */
	display_job_free((struct display_job *)lane);
}

/*
 * Takes the display and makes the output's FIFO swapchain on the display
 * thread, in the format the pipelines were made for; its images are a
 * copy's source when the display allows it, for the mirror's heads and a
 * test image's capture.  Then the cursor plane's objects, when the output
 * has one.  A failure destroys what was made and gives the display back.
 */
static void
display_open_run(
	struct display_job *job)
{
	struct kwl_compose *compose;
	uint64_t started;
	VkResult result;

	/* Gives Vulkan the display permission before creating its swapchain. */
	compose = job->server->compose;
	result = kwl_os_display_acquire(job->server, compose->physical, job->display);
	if (result != VK_SUCCESS) {
		printf("KWL VULKAN_ERROR operation=display-acquire result=%d\n", (int)result);
		vkdemo_display_close(compose->instance, compose->device, &job->output);
		job->error = EIO;
		return;
	}

	/* Both transfer directions first, for the scene's copies. */
	started = kwl_milliseconds();
	job->readback = 1;
	job->scene_supported = 1U;
	result = vkdemo_display_create_swapchain_usage(
		compose->physical,
		compose->device,
		compose->family,
		&job->output,
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

	/* A surface without both transfer directions keeps the existing capture-only path. */
	if (result == VK_ERROR_FORMAT_NOT_SUPPORTED) {
		job->scene_supported = 0U;
		result = vkdemo_display_create_swapchain(compose->physical, compose->device, compose->family, &job->output, job->readback);
	}

	/* A surface without readback retains the ordinary colour-only swapchain. */
	if (result == VK_ERROR_FORMAT_NOT_SUPPORTED && job->readback) {
		/* A display whose images cannot be read back still shows the desktop, without the capture. */
		printf("KWL SHOT unsupported: the swapchain images cannot be a copy's source\n");
		job->readback = 0;
		result = vkdemo_display_create_swapchain(compose->physical, compose->device, compose->family, &job->output, 0);
	}
	if (result != VK_SUCCESS ||
	    job->output.image_count > KWL_SWAPCHAIN_MAX ||
	    job->output.format != compose->format) {
		printf("KWL VULKAN_ERROR operation=swapchain result=%d images=%u format=%d\n", (int)result, job->output.image_count, (int)job->output.format);
		vkdemo_display_close(compose->instance, compose->device, &job->output);
		kwl_os_display_release(job->server, compose->physical, job->display);
		job->error = EIO;
		return;
	}
	printf("KWL STARTUP step=output-swapchain ms=%llu\n", (unsigned long long)(kwl_milliseconds() - started));

	/* The cursor plane's objects, when the output has such a plane (it borrows the output's claim). */
	if (job->plane)
		job->cursor = kwl_cursor_plane_make(compose, &job->output, &job->plane_extent, &job->plane_index);

	/* Succeeded: the swapchain for the main thread to take in. */
	job->error = 0;
}

/* A change made of steps, for then and context; NULL without memory. */
static struct display_change *
display_change_new(
	kwl_display_fn *then,
	void *context)
{
	struct display_change *change;

	/* The record. */
	change = calloc(1, sizeof(*change));
	if (change == NULL)
		return NULL;
	change->then = then;
	change->context = context;
	return change;
}

/* Ends a change made of steps and tells its result. */
static void
display_change_end(
	struct kwl_server *server,
	struct display_change *change,
	int error)
{
	kwl_display_fn *then;
	void *context;

	/* The record goes first. */
	then = change->then;
	context = change->context;
	free(change);

	/* The change ends, then its result. */
	kwl_display_end(server);
	then(server, error, context);
}

/* Window mode's open, parked: the output opened. */
static void
display_open_parked(
	struct kwl_server *server,
	int error,
	void *context)
{
	/* The open. */
	(void)error;
	kwl_display_open(server, display_open_opened, context);
}

/* Window mode's open, opened: the change ends, and the desktop follows what the output shows. */
static void
display_open_opened(
	struct kwl_server *server,
	int error,
	void *context)
{
	struct display_change *change;
	kwl_display_fn *then;
	void *then_context;

	/* A failure ends the change and is told. */
	change = context;
	if (error != 0) {
		display_change_end(server, change, error);
		return;
	}

	/*
	 * The change ends, then the desktop follows what the output shows now
	 * against what it showed when it last opened (review-4 C7): another
	 * logical size (a move to a display of another size), another angle.
	 */
	then = change->then;
	then_context = change->context;
	free(change);
	kwl_display_end(server);
	(void)kwl_output_rotated(server);
	then(server, 0, then_context);
}

/* An output's close as a change of its own, parked: the output closed. */
static void
display_close_parked(
	struct kwl_server *server,
	int error,
	void *context)
{
	/* The close. */
	(void)error;
	kwl_display_close(server, display_close_closed, context);
}

/* An output's close as a change of its own, closed: the change ends. */
static void
display_close_closed(
	struct kwl_server *server,
	int error,
	void *context)
{
	/* The end, and the result. */
	display_change_end(server, context, error);
}

/* Keeps the result of a change whose steps ran here. */
static void
display_sync_then(
	struct kwl_server *server,
	int error,
	void *context)
{
	/* The result for the caller. */
	(void)server;
	*(int *)context = error;
}
