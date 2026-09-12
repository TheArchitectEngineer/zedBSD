/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Independent allocation capabilities behind the common GPU resource API.
 */

#ifndef DRIVERS_GPU_SHARE_H
#define DRIVERS_GPU_SHARE_H

#include <uapi/gpu.h>

/*
 * One immutable sharing contract, independent of the exporting open lifetime.
 * Export returns one owned backend reference and leaves the session resource
 * valid. Release consumes only that reference and cannot fail. Import borrows
 * the shared object and returns an independently owned session resource; its
 * ordinary resource_destroy callback retires it. Failures return no ownership.
 * The GPU core retains the registered device until all exported references end.
 */
struct drv_gpu_share_ops {
	int (*export_resource)(void *, void *, void *, const struct gpu_image_descriptor *, void **);
	void (*release)(void *, void *);
	int (*import_resource)(void *, void *, void *, void **, uint32_t *);
};

#endif
