/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A check, in a test kernel, of the core's render path: a session of the
 * driver's own makes its render context and a render target, and builds
 * the commands of a render with no draw, its geometry stream only the
 * word that ends it.
 *
 * The fragment of a render needs the pixel event and end of tile
 * programs that store its tiles, which this driver does not build yet,
 * so the check builds the render without kicking it and says so; the
 * render context and the target are made and returned through the same
 * paths a session's render uses.
 */

#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/lock.h>
#include <uapi/errno.h>

#include "drivers/gpu/powervr/render-private.h"
#include "drivers/gpu/powervr/interface.h"
#include "drivers/gpu/powervr/render-kick-layout.h"

/* The frame the check renders, and the bytes of its geometry stream. */
#define CHECK_WIDTH		64U
#define CHECK_HEIGHT		64U
#define CHECK_STREAM_BYTES	0x1000U

/* What the check holds while it runs. */
struct render_check {
	struct powervr_render *render;
	struct powervr_session *session;
	struct powervr_render_context *context;
	struct powervr_target *target;
	struct powervr_resource *stream;
	uint8_t geometry[POWERVR_GEOMETRY_BYTES];
	uint8_t partial[POWERVR_FRAGMENT_BYTES];
	uint8_t fragment[POWERVR_FRAGMENT_BYTES];
};

/* The check's state; the node's start runs it once, alone. */
static struct render_check check_state;

static int check_prepare(struct render_check *check, const char **step);
static int check_build(struct render_check *check);

/*
 * Runs the driver's own render on the core and reports how it went: one
 * line, prepared or failed at a step.
 */
void
powervr_render_check(
	struct powervr_render *render)
{
	struct render_check *check;
	const char *step;
	int error;

	/* The session, the render context and the target, then the render's commands. */
	check = &check_state;
	kern_memset(check, 0, sizeof(*check));
	check->render = render;
	step = "open";
	error = check_prepare(check, &step);
	if (error == 0) {
		step = "build";
		error = check_build(check);
	}

	/* The stream, then the session, whose close returns the context and the target. */
	if (check->stream != NULL)
		powervr_resource_unmake(check->session, check->stream);
	if (check->session != NULL)
		render->ops.close(render, check->session);

	/* A step failed. */
	if (error != 0) {
		kern_logf("powervr: render check: FAILED at %s (%d)\n", step, error);
		return;
	}

	/* Succeeded: built, not kicked. */
	kern_logf("powervr: render check: built, not run (no fragment programs yet)\n");
}

/* Opens the check's session and makes its render context and render target. */
static int
check_prepare(
	struct render_check *check,
	const char **step)
{
	struct powervr_target_shape shape;
	struct powervr_buffer *buffer;
	void *opaque;
	int error;

	/* A session of the check's own. */
	*step = "open";
	error = check->render->ops.open(check->render, &opaque);
	if (error != 0)
		return error;
	check->session = opaque;

	/* Its render context. */
	*step = "render context";
	error = powervr_session_render_context(check->render, check->session, &check->context);
	if (error != 0)
		return error;

	/* A render target of the check's frame, one sample, one layer. */
	*step = "target";
	shape.width = CHECK_WIDTH;
	shape.height = CHECK_HEIGHT;
	shape.samples = 1;
	shape.layers = 1;
	error = powervr_session_target(check->render, check->session, &shape, &check->target);
	if (error != 0)
		return error;

	/* The geometry stream: the word that ends it, and nothing before. */
	*step = "stream";
	error = powervr_buffer_create(check->render, CHECK_STREAM_BYTES, &buffer);
	if (error != 0)
		return error;

	buffer->view[0] = (uint8_t)POWERVR_VDM_TERMINATE;
	buffer->view[1] = (uint8_t)(POWERVR_VDM_TERMINATE >> 8);
	buffer->view[2] = (uint8_t)(POWERVR_VDM_TERMINATE >> 16);
	buffer->view[3] = (uint8_t)(POWERVR_VDM_TERMINATE >> 24);
	error = powervr_resource_make(check->session, buffer, 1, 0, &check->stream);
	powervr_buffer_put(check->render, buffer);
	if (error != 0) {
		check->stream = NULL;
		return error;
	}

	/* Succeeded: the check can build its render. */
	return 0;
}

/*
 * Builds the commands of a render with no draw on the first half of the
 * target, as a kick would put them in the context's buffers.
 */
static int
check_build(
	struct render_check *check)
{
	struct powervr_render_request request;
	struct powervr_render_context *context;
	int error;

	/* The firmware commands: zero until their registers are filled, the geometry with its stream. */
	context = check->context;
	kern_memset(check->geometry, 0, sizeof(check->geometry));
	kern_memset(check->partial, 0, sizeof(check->partial));
	kern_memset(check->fragment, 0, sizeof(check->fragment));
	powervr_geometry_stream(check->geometry, check->stream->address);

	/* The render's place on the two timelines and the target's first half, with no wait. */
	request.geometry_timeline = powervr_object_address(&context->geometry.timeline, 0);
	request.geometry_value = context->geometry.submitted + 1U;
	request.fragment_timeline = powervr_object_address(&context->fragment.timeline, 0);
	request.fragment_value = context->fragment.submitted + 2U;
	request.target_data = powervr_target_data_address(check->target, 0);
	request.waits = NULL;
	request.wait_count = 0;
	request.geometry = check->geometry;
	request.partial = check->partial;
	request.fragment = check->fragment;
	request.partial_background = context->background;

	/* The three kicks' commands, under the lock that guards the context. */
	mutex_lock(&check->render->core->lock);

	error = powervr_render_sequence_make(&request, &context->sequence);

	mutex_unlock(&check->render->core->lock);

	if (error != 0)
		return error;

	/* Succeeded: the render is built. */
	return 0;
}
