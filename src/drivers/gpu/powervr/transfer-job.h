/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Runs one transfer (a clear or a copy of a rectangle) on a session's
 * transfer context: its programs and command are built, kicked and
 * waited for.
 */

#ifndef KERN_DRIVERS_GPU_POWERVR_TRANSFER_JOB_H
#define KERN_DRIVERS_GPU_POWERVR_TRANSFER_JOB_H

#include <stdint.h>

#include "drivers/gpu/powervr/render-private.h"
#include "drivers/gpu/powervr/transfer-program.h"

/*
 * One transfer: a clear of the destination's rectangle to a colour
 * (four float or integer bit patterns), or a copy into it from a source
 * view whose corner the rectangle's corner reads.
 */
struct powervr_transfer_request {
	enum powervr_transfer_kind kind;
	struct powervr_render_surface destination;
	struct powervr_render_area area;
	uint32_t colour[4];
	const struct powervr_view_shape *source;
	uint32_t source_x;
	uint32_t source_y;
};

/*
 * Runs a transfer, one row at a time when the destination's rows need
 * it; ETIMEDOUT when it did not finish in time, the core then may still
 * run it and its memory stays held.
 */
int powervr_transfer_run(struct powervr_render *render, struct powervr_session *session, const struct powervr_transfer_request *request);

#endif
