/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The push buffer the Vulkan executor runs.
 *
 * exec-push.c writes the 3D class's methods for what one recorded command
 * needs, touching no hardware, so the host test checks them as the kernel
 * writes them: a colour target in one of the executor's formats, the
 * viewport and scissor, the blend of the target, the vertex input, a draw,
 * a clear of a rectangle, the barrier between work that writes an image
 * and work that reads it, and the texture header and sampler of a view.
 * The methods and values are NVK's on Turing (Mesa 25.3.6, MIT; the class
 * headers it carries, see draw-push.c); the code is zedBSD's own.
 *
 * The kernel has no floating point: the floats a method takes are built
 * from their bits, and the viewport's arithmetic is done in fixed point.
 */

#ifndef DRIVERS_GPU_NVRTX_NVRTX_EXEC_H
#define DRIVERS_GPU_NVRTX_NVRTX_EXEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The vertex attributes and streams the class has, and the ones the executor sets. */
#define NVRTX_EXEC_ATTRIBUTES		32U
#define NVRTX_EXEC_STREAMS		4U

/*
 * A colour target: its GPU address, size, pitch (bytes between rows) and
 * VkFormat.
 */
struct nvrtx_exec_target {
	uint64_t address;
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint32_t format;
};

/*
 * The blend of colour target 0: whether it blends, the Vulkan factors and
 * operations of the colour and of the alpha, and the channels written (the
 * VkColorComponentFlags).
 */
struct nvrtx_exec_blend {
	uint32_t enable;
	uint32_t colour_op;
	uint32_t colour_source;
	uint32_t colour_destination;
	uint32_t alpha_op;
	uint32_t alpha_source;
	uint32_t alpha_destination;
	uint32_t write_mask;
};

/* One vertex attribute: its location, stream, offset and VkFormat (a float format). */
struct nvrtx_exec_attribute {
	uint32_t location;
	uint32_t stream;
	uint32_t offset;
	uint32_t format;
};

/*
 * One vertex stream: whether it is bound, its GPU address, the bytes
 * readable from there, its stride and whether it steps per instance.
 */
struct nvrtx_exec_stream {
	uint32_t enabled;
	uint64_t address;
	uint64_t bytes;
	uint32_t stride;
	uint32_t instance;
};

/* The vertex input of a draw: its attributes and its streams. */
struct nvrtx_exec_vertex {
	uint32_t attribute_count;
	struct nvrtx_exec_attribute attributes[NVRTX_EXEC_ATTRIBUTES];
	struct nvrtx_exec_stream streams[NVRTX_EXEC_STREAMS];
};

/*
 * A sampler: the Vulkan filters, address modes and whether coordinates
 * are in pixels.
 */
struct nvrtx_exec_sampler {
	uint32_t mag_filter;
	uint32_t min_filter;
	uint32_t address_u;
	uint32_t address_v;
	uint32_t address_w;
	uint32_t unnormalized;
};

bool nvrtx_exec_target_format(uint32_t format, uint32_t *code);
size_t nvrtx_exec_target(uint8_t *push, size_t at, const struct nvrtx_exec_target *target);
size_t nvrtx_exec_viewport(uint8_t *push, size_t at, const uint32_t *viewport);
size_t nvrtx_exec_scissor(uint8_t *push, size_t at, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1);
size_t nvrtx_exec_blend(uint8_t *push, size_t at, const struct nvrtx_exec_blend *blend);
size_t nvrtx_exec_vertex(uint8_t *push, size_t at, const struct nvrtx_exec_vertex *vertex);
size_t nvrtx_exec_draw(uint8_t *push, size_t at, uint32_t topology, uint32_t first, uint32_t count, uint32_t instances, uint32_t first_instance);
size_t nvrtx_exec_clear(uint8_t *push, size_t at, const uint32_t *colour, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1);
size_t nvrtx_exec_barrier(uint8_t *push, size_t at);
void nvrtx_exec_texture_header(uint32_t *header, uint64_t address, uint32_t width, uint32_t height, uint32_t pitch, uint32_t format, const uint32_t *swizzle);
void nvrtx_exec_sampler_words(uint32_t *words, const struct nvrtx_exec_sampler *sampler);
uint32_t nvrtx_exec_float(int32_t value);
int64_t nvrtx_exec_fixed(uint32_t bits, uint32_t fraction);
uint32_t nvrtx_exec_float_fixed(int64_t value, uint32_t fraction);

#endif /* DRIVERS_GPU_NVRTX_NVRTX_EXEC_H */
