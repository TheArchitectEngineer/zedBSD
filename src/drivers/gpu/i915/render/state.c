/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Gen12 3D state (see state.h).
 *
 * The command list is the one proven to draw on this hardware by the
 * fixture draws, with what a Vulkan draw adds to it: an enabled vertex
 * shader, vertex buffers, push constants, the viewport and scissor, the depth
 * buffer, SBE and a sampled texture.  The words that depend on the compiled
 * kernels are packed from what the compiler reports about them.
 */

#include "state.h"
#include "batch.h"
#include "gfx.h"
#include "heap.h"
#include "math.h"

#include <kern/klog.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "../intel/commands.h"
#include "../intel/genxml.h"

/* The genxml SURFACE_FORMAT values of the VkFormats the render paths read and write. */
#define I915_GFX_SURFACE_R8G8B8A8_UNORM		0x0c7U
#define I915_GFX_SURFACE_B8G8R8A8_UNORM		0x0c0U
#define I915_GFX_SURFACE_R32_FLOAT		0x0d8U
#define I915_GFX_SURFACE_R32G32_FLOAT		0x085U
#define I915_GFX_SURFACE_R32G32B32_FLOAT	0x040U
#define I915_GFX_SURFACE_R32G32B32A32_FLOAT	0x000U

/* SAMPLER_STATE texture coordinate mode for an address mode past the table: CLAMP. */
#define I915_GFX_SAMPLER_CLAMP			2U

/*
 * The EU instructions of a thread that only ends itself.
 *
 * They are a null render target write with end-of-thread: a thread that
 * starts anywhere in the instruction heap other than at a kernel retires at
 * once instead of running whatever the heap held.  Every draw and every
 * rectangle fills the heap with them before placing its kernels.
 */
static const uint32_t i915_gfx_eot_only[4] = {
	0x00030032U, 0x00001004U, 0x58007024U, 0x00c40000U,
};

/*
 * SAMPLER_STATE texture coordinate modes, indexed by VkSamplerAddressMode:
 * REPEAT is WRAP, then MIRROR, CLAMP, CLAMP_BORDER and MIRROR_ONCE.
 */
static const uint32_t i915_gfx_address_modes[5] = {
	0U,
	1U,
	2U,
	4U,
	5U,
};

/*
 * The genxml COMPAREFUNCTION of each VkCompareOp: NEVER .. GREATER_OR_EQUAL
 * shift up by one and ALWAYS is zero.
 */
static const uint32_t i915_gfx_compare_functions[8] = {
	1U,
	2U,
	3U,
	4U,
	5U,
	6U,
	7U,
	0U,
};

/* The 3DSTATE_CONSTANT_* packets of the five stages, the vertex stage first. */
static const uint32_t i915_gfx_constant_opcodes[5] = {
	GEN12_CMD_3DSTATE_CONSTANT_VS,
	GEN12_CMD_3DSTATE_CONSTANT_HS,
	GEN12_CMD_3DSTATE_CONSTANT_DS,
	GEN12_CMD_3DSTATE_CONSTANT_GS,
	GEN12_CMD_3DSTATE_CONSTANT_PS,
};

static int i915_surface_format(uint32_t format, uint32_t *surface_format);
static uint32_t i915_format_components(uint32_t format);
static int i915_image_surface_write(uint32_t *rss, const struct i915_gfx_image *image, uint32_t mocs);
static int i915_state_write_surfaces(uint32_t *surface, uint32_t *dynamic, const struct i915_gfx_draw_state *state, const struct i915_gfx_kernels *kernels, const struct i915_gfx_image *target, uint32_t mocs);
static void i915_state_write_viewport(uint32_t *dynamic, const struct i915_gfx_pipeline *pipeline);

/*
 * Writes everything a draw's batch points at into the state object.
 *
 * The surface and dynamic heaps are cleared and refilled: the binding table
 * with the render target and the one sampled texture, the sampler, blend,
 * viewports and scissor, then the push constants and the instruction heap
 * with both kernels.  Returns EINVAL or ENOTSUP for a draw whose bindings
 * cannot be written; the object is then partly written.
 */
int
drv_i915_gfx_write_state(
	uint8_t *page,
	const struct i915_gfx_draw_state *state,
	const struct i915_gfx_kernels *kernels,
	const struct i915_gfx_image *target,
	uint32_t mocs)
{
	const struct i915_gfx_pipeline *pipeline;
	uint32_t *surface;
	uint32_t *dynamic;
	uint32_t push_bytes;
	int error;

	/* Locates the two heaps and clears everything below the instruction heap. */
	pipeline = state->pipeline;
	surface = (uint32_t *)(void *)(page + I915_GFX_SURFACE_HEAP);
	dynamic = (uint32_t *)(void *)(page + I915_GFX_DYNAMIC_HEAP);
	memset(page, 0, I915_GFX_INSTRUCTION_HEAP);

	/* Writes the binding table, the surfaces and the sampler. */
	error = i915_state_write_surfaces(surface, dynamic, state, kernels, target, mocs);
	if (error != 0)
		return error;

	/* Writes the blend state, the viewports and the scissor. */
	i915_state_write_viewport(dynamic, pipeline);

	/*
	 * Copies the push constants the vertex kernel reads, register by
	 * register, never more than the command buffer carries.
	 */
	push_bytes = kernels->vs_push_regs * 32U;
	if (push_bytes > I915_GFX_PUSH_BYTES)
		push_bytes = I915_GFX_PUSH_BYTES;
	memcpy(page + I915_GFX_PUSH_BUFFER, state->push, push_bytes);

	/* Places both kernels in an instruction heap where every other start retires at once. */
	drv_i915_gfx_instruction_heap_clear(page);
	memcpy(page + I915_GFX_INSTRUCTION_HEAP + I915_GFX_VS_KERNEL, kernels->vs_code, kernels->vs_bytes);
	memcpy(page + I915_GFX_INSTRUCTION_HEAP + I915_GFX_PS_KERNEL, kernels->ps_code, kernels->ps_bytes);

	/* Succeeded: the state object holds everything the batch points at. */
	return 0;
}

/*
 * Writes the RENDER_SURFACE_STATE of a linear 2D surface.
 *
 * The surface is checked first: a GPU address, an extent of at most 16384
 * in each direction and a pitch that holds a row of four-byte texels.  An
 * R32_FLOAT surface is written without the unorm path bit, since it carries
 * a depth value's bits rather than a colour.
 */
int
drv_i915_gfx_surface_write(
	uint32_t *rss,
	const struct i915_gfx_surface *surface,
	uint32_t mocs)
{
	uint32_t format;
	uint32_t unorm;
	int error;

	/* Refuses a surface with no storage. */
	if (surface->va == 0U)
		return EINVAL;

	/* Refuses an empty surface. */
	if (surface->width == 0U || surface->height == 0U)
		return EINVAL;

	/* Refuses a surface larger than a 2D surface state describes. */
	if (surface->width > 16384U || surface->height > 16384U)
		return EINVAL;

	/* Refuses a pitch too short for a row of four-byte texels. */
	if (surface->pitch < surface->width * 4U)
		return EINVAL;

	/* Refuses a format the surface state cannot name. */
	error = i915_surface_format(surface->format, &format);
	if (error != 0)
		return EINVAL;

	/* A colour surface takes the unorm path bit; the depth-bits view does not. */
	unorm = 1U << 31;
	if (surface->format == VK_FORMAT_R32_SFLOAT)
		unorm = 0U;

	/*
	 * Fills the surface state as isl fills it for a linear 2D one-level
	 * surface: 2D, horizontal and vertical alignment 4, linear; the unorm
	 * path bit, MOCS and QPitch; width and height; pitch; mip tail start 1;
	 * identity channel select; the address.
	 */
	memset(rss, 0, GEN12_RENDER_SURFACE_STATE_DWORDS * 4U);
	rss[0] = (GEN12_SURFTYPE_2D << 29) |
	    (format << 18) |
	    (GEN12_SURFACE_ALIGN_4 << 16) |
	    (GEN12_SURFACE_ALIGN_4 << 14) |
	    (GEN12_TILEMODE_LINEAR << 12);
	rss[1] = unorm | (mocs << 24) | (((surface->height + 3U) & ~3U) / 4U);
	rss[2] = (surface->width - 1U) | ((surface->height - 1U) << 16);
	rss[3] = surface->pitch - 1U;
	rss[5] = 0x00000100U;
	rss[7] = (4U << 25) | (5U << 22) | (6U << 19) | (7U << 16);
	rss[8] = (uint32_t)surface->va;
	rss[9] = (uint32_t)(surface->va >> 32);

	/* Succeeded: the surface state describes the surface. */
	return 0;
}

/*
 * Writes the SAMPLER_STATE of a sampler.
 *
 * Only the fields the fixture sampler sets are written: the OpenGL LOD
 * pre-clamp, the two filters, address rounding for a linear filter and the
 * u and v address modes.  An address mode past the table clamps.
 */
void
drv_i915_gfx_sampler_write(
	uint32_t *state,
	const struct i915_gfx_sampler *sampler)
{
	uint32_t address_u;
	uint32_t address_v;
	uint32_t mag_linear;
	uint32_t min_linear;
	uint32_t rounding;

	/* Translates the u address mode, clamping one the table does not know. */
	address_u = I915_GFX_SAMPLER_CLAMP;
	if (sampler->address_u < 5U)
		address_u = i915_gfx_address_modes[sampler->address_u];

	/* Translates the v address mode the same way. */
	address_v = I915_GFX_SAMPLER_CLAMP;
	if (sampler->address_v < 5U)
		address_v = i915_gfx_address_modes[sampler->address_v];

	/* Notes which of the two filters is linear. */
	mag_linear = 0U;
	if (sampler->mag_filter == VK_FILTER_LINEAR)
		mag_linear = 1U;
	min_linear = 0U;
	if (sampler->min_filter == VK_FILTER_LINEAR)
		min_linear = 1U;

	/* A linear filter in either direction turns address rounding on. */
	rounding = 0U;
	if (mag_linear != 0U || min_linear != 0U)
		rounding = 0x0007e000U;

	/*
	 * Packs the sampler: the OpenGL LOD pre-clamp and the filters; the LOD
	 * range; the address rounding and the address modes.
	 * XXX: the LOD range is [0, 0]: one mip level.
	 */
	state[0] = (2U << 27) | (mag_linear << 17) | (min_linear << 14);
	state[1] = 0U;
	state[2] = 0U;
	state[3] = rounding | (address_u << 6) | (address_v << 3) | 2U;
}

/*
 * Fills the instruction heap of the state object with threads that only
 * end themselves.
 *
 * A kernel copied in afterwards replaces the fill at its own offset; a
 * thread that starts anywhere else retires at once.
 */
void
drv_i915_gfx_instruction_heap_clear(
	uint8_t *page)
{
	unsigned at;

	/* Repeats the end-of-thread instructions over the whole heap. */
	for (at = 0U; at + sizeof(i915_gfx_eot_only) <= I915_GFX_INSTRUCTION_BYTES; at += sizeof(i915_gfx_eot_only))
		memcpy(page + I915_GFX_INSTRUCTION_HEAP + at, i915_gfx_eot_only, sizeof(i915_gfx_eot_only));
}

/*
 * Emits the start of a 3D batch: the pipeline switch, the state bases and
 * the state anv programs once per context before its first draw.
 *
 * A stalling flush precedes the pipeline select and the base addresses, and
 * the caches that hold state are invalidated after them.
 */
void
drv_i915_gfx_emit_context_setup(
	struct i915_gfx_batch *batch,
	uint64_t state_va,
	uint32_t mocs)
{
	uint64_t surface;
	uint64_t dynamic;
	uint64_t instruction;
	uint32_t index;
	uint32_t pattern;

	/* Locates the three heaps the state bases name. */
	surface = state_va + I915_GFX_SURFACE_HEAP;
	dynamic = state_va + I915_GFX_DYNAMIC_HEAP;
	instruction = state_va + I915_GFX_INSTRUCTION_HEAP;

	/* Flushes and stalls before the pipeline is switched to 3D. */
	drv_i915_batch_pipe_control(batch,
				    PIPE_CONTROL_CS_STALL |
				    PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH |
				    PIPE_CONTROL_DEPTH_CACHE_FLUSH |
				    PIPE_CONTROL_DC_FLUSH_ENABLE |
				    PIPE_CONTROL_FLUSH_ENABLE);
	drv_i915_batch_emit(batch, GEN12_PIPELINE_SELECT_DWORD(GEN12_PIPELINE_SELECT_3D));

	/*
	 * Programs STATE_BASE_ADDRESS: general and indirect bases at zero, the
	 * surface and dynamic heaps with the given MOCS, the instruction heap
	 * write-back (instruction fetches go through the L3), every size the
	 * largest, the bindless surface heap over the surface heap and no
	 * bindless sampler heap.
	 */
	drv_i915_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_STATE_BASE_ADDRESS, GEN12_STATE_BASE_ADDRESS_DWORDS));
	drv_i915_batch_emit(batch, 1U | (mocs << 4));
	drv_i915_batch_emit(batch, 0U);
	drv_i915_batch_emit(batch, mocs << 16);
	drv_i915_batch_emit(batch, 1U | (mocs << 4) | ((uint32_t)surface & 0xfffff000U));
	drv_i915_batch_emit(batch, (uint32_t)(surface >> 32));
	drv_i915_batch_emit(batch, 1U | (mocs << 4) | ((uint32_t)dynamic & 0xfffff000U));
	drv_i915_batch_emit(batch, (uint32_t)(dynamic >> 32));
	drv_i915_batch_emit(batch, 1U | (mocs << 4));
	drv_i915_batch_emit(batch, 0U);
	drv_i915_batch_emit(batch, 1U | (GEN12_MOCS(I915_MOCS_WRITEBACK_INDEX) << 4) | ((uint32_t)instruction & 0xfffff000U));
	drv_i915_batch_emit(batch, (uint32_t)(instruction >> 32));
	drv_i915_batch_emit(batch, 1U | (0xfffffU << 12));
	drv_i915_batch_emit(batch, 1U | (0xfffffU << 12));
	drv_i915_batch_emit(batch, 1U | (0xfffffU << 12));
	drv_i915_batch_emit(batch, 1U | (0xfffffU << 12));
	drv_i915_batch_emit(batch, 1U | (mocs << 4) | ((uint32_t)surface & 0xfffff000U));
	drv_i915_batch_emit(batch, (uint32_t)(surface >> 32));
	drv_i915_batch_emit(batch, (4096U / 64U - 1U) << 12);
	drv_i915_batch_emit(batch, 1U | (mocs << 4));
	drv_i915_batch_emit(batch, 0U);
	drv_i915_batch_emit(batch, 0U);

	/* Invalidates the caches that may hold state from before the new bases. */
	drv_i915_batch_pipe_control(batch,
				    PIPE_CONTROL_CS_STALL |
				    PIPE_CONTROL_STATE_CACHE_INVALIDATE |
				    PIPE_CONTROL_CONST_CACHE_INVALIDATE |
				    PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE |
				    PIPE_CONTROL_INSTRUCTION_CACHE_INVALIDATE);

	/* Clears the state anv clears once per context. */
	drv_i915_batch_zero(batch, GEN12_CMD_3DSTATE_WM_HZ_OP, GEN12_3DSTATE_WM_HZ_OP_DWORDS);
	drv_i915_batch_zero(batch, GEN12_CMD_3DSTATE_AA_LINE_PARAMETERS, GEN12_3DSTATE_AA_LINE_PARAMETERS_DWORDS);
	drv_i915_batch_zero(batch, GEN12_CMD_3DSTATE_WM_CHROMAKEY, GEN12_3DSTATE_WM_CHROMAKEY_DWORDS);
	drv_i915_batch_zero(batch, GEN12_CMD_3DSTATE_POLY_STIPPLE_OFFSET, GEN12_3DSTATE_POLY_STIPPLE_OFFSET_DWORDS);
	drv_i915_batch_zero(batch, GEN12_CMD_3DSTATE_LINE_STIPPLE, GEN12_3DSTATE_LINE_STIPPLE_DWORDS);

	/* Places the single sample at the pixel centre. */
	drv_i915_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_SAMPLE_PATTERN, GEN12_3DSTATE_SAMPLE_PATTERN_DWORDS));
	for (index = 1U; index < GEN12_3DSTATE_SAMPLE_PATTERN_DWORDS; index++) {
		/* Only dword 8 carries the 1x pattern; every other dword is zero. */
		pattern = 0U;
		if (index == 8U)
			pattern = GEN12_SAMPLE_PATTERN_1X_CENTRE;
		drv_i915_batch_emit(batch, pattern);
	}

	/* Clears the depth bounds and the binding tables of the unused geometry stages. */
	drv_i915_batch_zero(batch, GEN12_CMD_3DSTATE_DEPTH_BOUNDS, GEN12_3DSTATE_DEPTH_BOUNDS_DWORDS);
	drv_i915_batch_zero(batch, GEN12_CMD_3DSTATE_BINDING_TABLE_POINTERS_VS, GEN12_3DSTATE_POINTERS_DWORDS);
	drv_i915_batch_zero(batch, GEN12_CMD_3DSTATE_BINDING_TABLE_POINTERS_HS, GEN12_3DSTATE_POINTERS_DWORDS);
	drv_i915_batch_zero(batch, GEN12_CMD_3DSTATE_BINDING_TABLE_POINTERS_DS, GEN12_3DSTATE_POINTERS_DWORDS);
	drv_i915_batch_zero(batch, GEN12_CMD_3DSTATE_BINDING_TABLE_POINTERS_GS, GEN12_3DSTATE_POINTERS_DWORDS);
}

/*
 * Emits the vertex buffers and vertex elements of a draw.
 *
 * One vertex element feeds each attribute the vertex kernel reads, in the
 * kernel's payload order, which is the ascending order of the locations.
 * Returns EINVAL for an attribute, binding or buffer the draw lacks and
 * ENOTSUP for a vertex format or topology the path does not implement; the
 * batch is then incomplete and must not run.
 */
int
drv_i915_gfx_emit_vertex_input(
	struct i915_gfx_batch *batch,
	const struct i915_gfx_draw_state *state,
	const struct i915_gfx_kernels *kernels,
	uint32_t mocs)
{
	const struct i915_gfx_pipeline *pipeline;
	struct i915_gfx_buffer *buffer;
	uint32_t order[I915_GFX_MAX_VERTEX_ATTRIBUTES];
	uint32_t index;
	uint32_t other;
	uint32_t count;
	uint32_t binding;
	uint32_t attribute;
	uint32_t format;
	uint32_t components;
	uint32_t component_y;
	uint32_t component_z;
	uint32_t component_w;
	uint64_t va;
	int error;

	/* A draw needs at least one attribute and one vertex buffer binding. */
	pipeline = state->pipeline;
	count = kernels->vs_input_count;
	if (count == 0U || pipeline->binding_count == 0U)
		return EINVAL;

	/* Finds the pipeline attribute of every location the kernel reads. */
	for (index = 0U; index < count; index++) {
		/* Looks the location up among the pipeline's attributes. */
		for (other = 0U; other < pipeline->attribute_count; other++) {
			if (pipeline->attributes[other].location == kernels->vs_inputs[index])
				break;
		}

		/* Refuses a location the pipeline does not describe. */
		if (other == pipeline->attribute_count) {
			kern_logf("i915: vk: draw refused: the vertex shader reads location %u and the pipeline has no such attribute\n",
				  kernels->vs_inputs[index]);
			return EINVAL;
		}

		order[index] = other;
	}

	/* Emits one VERTEX_BUFFER_STATE for each binding of the pipeline. */
	drv_i915_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_VERTEX_BUFFERS, 1U + pipeline->binding_count * GEN12_VERTEX_BUFFER_STATE_DWORDS));
	for (index = 0U; index < pipeline->binding_count; index++) {
		/* Refuses a binding number past the bindings a command buffer tracks. */
		binding = pipeline->bindings[index].binding;
		if (binding >= I915_GFX_MAX_VERTEX_BINDINGS)
			return EINVAL;

		/* Refuses a binding with no buffer bound. */
		buffer = state->vertex[binding].buffer;
		if (buffer == NULL)
			return EINVAL;

		/* Refuses an offset past the end of the buffer. */
		if (state->vertex[binding].offset > buffer->size)
			return EINVAL;

		/* Resolves the buffer range to its GPU address. */
		va = drv_i915_gfx_memory_va(buffer->memory, buffer->offset + state->vertex[binding].offset);
		if (va == 0U)
			return EINVAL;

		/* Writes the binding, MOCS, address modify enable and stride; the address; the size. */
		drv_i915_batch_emit(batch,
				    (binding << 26) |
				    GEN12_VERTEX_BUFFER_L3_BYPASS_DISABLE |
				    (mocs << 16) |
				    (1U << 14) |
				    (pipeline->bindings[index].stride & 0xfffU));
		drv_i915_batch_emit(batch, (uint32_t)va);
		drv_i915_batch_emit(batch, (uint32_t)(va >> 32));
		drv_i915_batch_emit(batch, (uint32_t)(buffer->size - state->vertex[binding].offset));
	}

	/* Emits one VERTEX_ELEMENT_STATE for each attribute, in payload order. */
	drv_i915_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_VERTEX_ELEMENTS, 1U + count * GEN12_VERTEX_ELEMENT_STATE_DWORDS));
	for (index = 0U; index < count; index++) {
		/* Refuses a vertex format the surface formats do not cover. */
		attribute = order[index];
		error = i915_surface_format(pipeline->attributes[attribute].format, &format);
		if (error != 0)
			return ENOTSUP;

		/*
		 * Stores the components the format has; a missing y or z is 0 and
		 * a missing w is 1.0.
		 */
		components = i915_format_components(pipeline->attributes[attribute].format);
		component_y = GEN12_VFCOMP_STORE_0;
		if (components > 1U)
			component_y = GEN12_VFCOMP_STORE_SRC;
		component_z = GEN12_VFCOMP_STORE_0;
		if (components > 2U)
			component_z = GEN12_VFCOMP_STORE_SRC;
		component_w = GEN12_VFCOMP_STORE_1_FP;
		if (components > 3U)
			component_w = GEN12_VFCOMP_STORE_SRC;

		/* Writes the binding, valid bit, format and offset; the component controls. */
		drv_i915_batch_emit(batch,
				    (pipeline->attributes[attribute].binding << 26) |
				    (1U << 25) |
				    (format << 16) |
				    (pipeline->attributes[attribute].offset & 0xfffU));
		drv_i915_batch_emit(batch,
				    (GEN12_VFCOMP_STORE_SRC << 28) |
				    (component_y << 24) |
				    (component_z << 20) |
				    (component_w << 16));
	}

	/* Enables the vertex fetch statistics and clears the fetcher's other state. */
	drv_i915_batch_emit(batch, (GEN12_CMD_3DSTATE_VF_STATISTICS << 16) | 1U);
	drv_i915_batch_zero(batch, GEN12_CMD_3DSTATE_VF, GEN12_3DSTATE_VF_DWORDS);
	drv_i915_batch_zero(batch, GEN12_CMD_3DSTATE_VF_SGVS, GEN12_3DSTATE_VF_SGVS_DWORDS);
	drv_i915_batch_zero(batch, GEN12_CMD_3DSTATE_VF_SGVS_2, GEN12_3DSTATE_VF_SGVS_2_DWORDS);

	/* Turns instancing off for every vertex element. */
	for (index = 0U; index < count; index++) {
		drv_i915_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_VF_INSTANCING, GEN12_3DSTATE_VF_INSTANCING_DWORDS));
		drv_i915_batch_emit(batch, index);
		drv_i915_batch_emit(batch, 0U);
	}

	/* Refuses a topology other than a triangle list. */
	if (pipeline->topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) {
		kern_logf("i915: vk: XXX unimplemented path: primitive topology %u\n", pipeline->topology);
		return ENOTSUP;
	}

	/* Draws triangle lists. */
	drv_i915_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_VF_TOPOLOGY, GEN12_3DSTATE_VF_TOPOLOGY_DWORDS));
	drv_i915_batch_emit(batch, GEN12_3DPRIM_TRILIST);

	/* Succeeded: the vertex fetcher is programmed. */
	return 0;
}

/*
 * Emits the push constant and URB allocations.
 *
 * The push constant space is split in halves between the vertex and the
 * pixel stage.  The vertex stage owns the URB past the push constants:
 * 3576 entries of entry_size 64-byte units; the other geometry stages get
 * none.
 */
void
drv_i915_gfx_emit_urb(
	struct i915_gfx_batch *batch,
	uint32_t entry_size)
{
	uint32_t opcode;

	/* Gives the vertex stage the first half of the push constant space. */
	drv_i915_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_PUSH_CONSTANT_ALLOC_VS, GEN12_3DSTATE_PUSH_CONSTANT_ALLOC_DWORDS));
	drv_i915_batch_emit(batch, (0U << 16) | (GEN12_PUSH_CONSTANT_KB / 2U));

	/* Gives the hull, domain and geometry stages none. */
	for (opcode = GEN12_CMD_3DSTATE_PUSH_CONSTANT_ALLOC_HS; opcode < GEN12_CMD_3DSTATE_PUSH_CONSTANT_ALLOC_PS; opcode++)
		drv_i915_batch_zero(batch, opcode, GEN12_3DSTATE_PUSH_CONSTANT_ALLOC_DWORDS);

	/* Gives the pixel stage the second half. */
	drv_i915_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_PUSH_CONSTANT_ALLOC_PS, GEN12_3DSTATE_PUSH_CONSTANT_ALLOC_DWORDS));
	drv_i915_batch_emit(batch, ((GEN12_PUSH_CONSTANT_KB / 2U) << 16) | (GEN12_PUSH_CONSTANT_KB / 2U));

	/* Gives the vertex stage the URB past the push constants. */
	drv_i915_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_URB_ALLOC_VS, GEN12_3DSTATE_URB_ALLOC_DWORDS));
	drv_i915_batch_emit(batch, (4U << 10) | (4U << 21) | (entry_size - 1U));
	drv_i915_batch_emit(batch, 3576U | (3576U << 16));

	/* Gives the hull, domain and geometry stages no URB entries. */
	for (opcode = GEN12_CMD_3DSTATE_URB_ALLOC_HS; opcode <= GEN12_CMD_3DSTATE_URB_ALLOC_GS; opcode++) {
		drv_i915_batch_emit(batch, GEN12_CMD_HEADER(opcode, GEN12_3DSTATE_URB_ALLOC_DWORDS));
		drv_i915_batch_emit(batch, (5U << 10) | (5U << 21));
		drv_i915_batch_emit(batch, 0U);
	}
}

/*
 * Emits the 3DSTATE_CONSTANT_* packets of all five stages.
 *
 * Only the vertex stage reads push constants.  Like anv, the buffer goes in
 * the highest slot, so that slot 0 is never the only one in use; every other
 * stage is given an empty packet.
 */
void
drv_i915_gfx_emit_constants(
	struct i915_gfx_batch *batch,
	uint64_t push_va,
	uint32_t push_regs,
	uint32_t mocs)
{
	unsigned stage;
	unsigned index;

	/* Emits one packet for each stage. */
	for (stage = 0U; stage < 5U; stage++) {
		drv_i915_batch_emit(batch, GEN12_CMD_HEADER(i915_gfx_constant_opcodes[stage], GEN12_3DSTATE_CONSTANT_DWORDS) | (mocs << 8));

		/* The vertex stage with push constants reads them from buffer 3. */
		if (stage == 0U && push_regs != 0U) {
			drv_i915_batch_emit(batch, 0U);
			drv_i915_batch_emit(batch, push_regs << 16);
			for (index = 3U; index < 9U; index++)
				drv_i915_batch_emit(batch, 0U);
			drv_i915_batch_emit(batch, (uint32_t)push_va);
			drv_i915_batch_emit(batch, (uint32_t)(push_va >> 32));
			continue;
		}

		/* Every other packet reads nothing. */
		for (index = 1U; index < GEN12_3DSTATE_CONSTANT_DWORDS; index++)
			drv_i915_batch_emit(batch, 0U);
	}
}

/*
 * Emits 3DSTATE_CLIP, SF and RASTER of an ordinary Vulkan pipeline, as anv
 * programs them.
 */
void
drv_i915_gfx_emit_raster(
	struct i915_gfx_batch *batch,
	const struct i915_gfx_pipeline *pipeline)
{
	uint32_t cull;
	uint32_t counter_clockwise;
	uint32_t index;

	/*
	 * Clips with statistics, early cull and 8-bit subpixel precision; the
	 * D3D API mode (z in [0, 1]), viewport XY test and guardband; point
	 * widths 0.125 .. 255.875.
	 */
	drv_i915_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_CLIP, GEN12_3DSTATE_CLIP_DWORDS));
	drv_i915_batch_emit(batch, (1U << 10) | (1U << 18));
	drv_i915_batch_emit(batch, (1U << 31) | (1U << 30) | (1U << 28) | (1U << 26));
	drv_i915_batch_emit(batch, (1U << 17) | (2047U << 6));

	/*
	 * Sets up with the viewport transform, statistics and line width 1.0;
	 * the URB deref block; point width 1.0 from state and the AA line
	 * distance.
	 */
	drv_i915_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_SF, GEN12_3DSTATE_SF_DWORDS));
	drv_i915_batch_emit(batch, (1U << 1) | (1U << 10) | (128U << 12));
	drv_i915_batch_emit(batch, GEN12_URB_DEREF_BLOCK_SIZE_32 << 29);
	drv_i915_batch_emit(batch, 8U | (1U << 11) | (1U << 14));

	/* Translates the pipeline's cull mode; front and back together cull both. */
	switch (pipeline->cull_mode) {
	case VK_CULL_MODE_NONE:
		cull = GEN12_CULLMODE_NONE;
		break;
	case VK_CULL_MODE_FRONT_BIT:
		cull = GEN12_CULLMODE_FRONT;
		break;
	case VK_CULL_MODE_BACK_BIT:
		cull = GEN12_CULLMODE_BACK;
		break;
	default:
		cull = GEN12_CULLMODE_BOTH;
		break;
	}

	/* A counter-clockwise front face sets the front winding bit. */
	counter_clockwise = 0U;
	if (pipeline->front_face == VK_FRONT_FACE_COUNTER_CLOCKWISE)
		counter_clockwise = 1U;

	/* Rasterizes with z near and far clip tests, scissor, the cull mode, the winding and the DX10.1+ API mode. */
	drv_i915_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_RASTER, GEN12_3DSTATE_RASTER_DWORDS));
	drv_i915_batch_emit(batch,
			    (1U << 0) |
			    (1U << 26) |
			    (1U << 1) |
			    (cull << 16) |
			    (counter_clockwise << 21) |
			    (2U << 22));
	for (index = 2U; index < GEN12_3DSTATE_RASTER_DWORDS; index++)
		drv_i915_batch_emit(batch, 0U);
}

/*
 * Emits the depth test, the depth buffer and the post-sync write anv makes
 * after the depth state.
 *
 * With no depth attachment the depth buffer is a null D32_FLOAT surface and
 * the test is off.  Returns EINVAL for a depth attachment with no storage or
 * in a format other than D32_SFLOAT.
 */
int
drv_i915_gfx_emit_depth(
	struct i915_gfx_batch *batch,
	const struct i915_gfx_draw_state *state,
	const struct i915_gfx_image *depth,
	uint64_t scratch_va,
	uint32_t mocs)
{
	const struct i915_gfx_pipeline *pipeline;
	uint32_t depth_state;
	uint32_t write_enable;
	uint32_t test_enable;
	uint32_t index;
	uint64_t va;

	/* Packs the depth write, the depth test and its compare function when there is a depth buffer. */
	pipeline = state->pipeline;
	depth_state = 0U;
	if (depth != NULL) {
		write_enable = 0U;
		if (pipeline->depth_write != 0U)
			write_enable = 1U;
		test_enable = 0U;
		if (pipeline->depth_test != 0U)
			test_enable = 1U;
		depth_state = write_enable | (test_enable << 1) | (i915_gfx_compare_functions[pipeline->depth_compare & 7U] << 5);
	}

	/* Programs the depth test. */
	drv_i915_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_WM_DEPTH_STENCIL, GEN12_3DSTATE_WM_DEPTH_STENCIL_DWORDS));
	drv_i915_batch_emit(batch, depth_state);
	drv_i915_batch_emit(batch, 0U);
	drv_i915_batch_emit(batch, 0U);

	/* Describes the depth buffer, or a null one. */
	drv_i915_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_DEPTH_BUFFER, GEN12_3DSTATE_DEPTH_BUFFER_DWORDS));
	if (depth != NULL) {
		/* Refuses a depth image with no storage or in another format. */
		va = drv_i915_gfx_memory_va(depth->memory, depth->offset);
		if (va == 0U || depth->format != VK_FORMAT_D32_SFLOAT)
			return EINVAL;

		/*
		 * Writes what isl_emit_depth_stencil_hiz_s() writes: 2D, D32_FLOAT,
		 * write enable and the pitch (Y-tiled: Gen9+ depth always is); the
		 * address; the extent; MOCS; the QPitch.
		 */
		drv_i915_batch_emit(batch, (GEN12_SURFTYPE_2D << 29) | (1U << 28) | (GEN12_DEPTH_FORMAT_D32_FLOAT << 24) | (depth->pitch - 1U));
		drv_i915_batch_emit(batch, (uint32_t)va);
		drv_i915_batch_emit(batch, (uint32_t)(va >> 32));
		drv_i915_batch_emit(batch, ((depth->width - 1U) << 1) | ((depth->height - 1U) << 17));
		drv_i915_batch_emit(batch, mocs);
		drv_i915_batch_emit(batch, 0U);
		drv_i915_batch_emit(batch, ((depth->height + 3U) & ~3U) / 4U);
	} else {
		/* A null depth buffer is still typed D32_FLOAT. */
		drv_i915_batch_emit(batch, (GEN12_SURFTYPE_NULL << 29) | (GEN12_DEPTH_FORMAT_D32_FLOAT << 24));
		for (index = 2U; index < GEN12_3DSTATE_DEPTH_BUFFER_DWORDS; index++)
			drv_i915_batch_emit(batch, 0U);
	}

	/* Describes a null stencil buffer, and clears the hierarchical depth and the clear values. */
	drv_i915_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_STENCIL_BUFFER, GEN12_3DSTATE_STENCIL_BUFFER_DWORDS));
	drv_i915_batch_emit(batch, GEN12_SURFTYPE_NULL << 29);
	for (index = 2U; index < GEN12_3DSTATE_STENCIL_BUFFER_DWORDS; index++)
		drv_i915_batch_emit(batch, 0U);
	drv_i915_batch_zero(batch, GEN12_CMD_3DSTATE_HIER_DEPTH_BUFFER, GEN12_3DSTATE_HIER_DEPTH_BUFFER_DWORDS);
	drv_i915_batch_zero(batch, GEN12_CMD_3DSTATE_CLEAR_PARAMS, GEN12_3DSTATE_CLEAR_PARAMS_DWORDS);

	/* Makes the post-sync write anv makes after the depth state (Wa_1408224581, Wa_14014097488, Wa_14016712196). */
	drv_i915_batch_emit(batch, GFX_OP_PIPE_CONTROL(6));
	drv_i915_batch_emit(batch, PIPE_CONTROL_QW_WRITE);
	drv_i915_batch_emit(batch, (uint32_t)scratch_va);
	drv_i915_batch_emit(batch, (uint32_t)(scratch_va >> 32));
	drv_i915_batch_emit(batch, 0U);
	drv_i915_batch_emit(batch, 0U);

	/* Succeeded: the depth state is programmed. */
	return 0;
}

/*
 * Emits 3DSTATE_VS for the vertex kernel.
 *
 * The kernel starts at the vertex kernel offset of the instruction heap and
 * runs SIMD8 with statistics; it reads its attributes from the URB in pairs
 * starting at 0.
 */
void
drv_i915_gfx_emit_vertex_shader(
	struct i915_gfx_batch *batch,
	const struct i915_gfx_kernels *kernels)
{
	/*
	 * Writes the kernel start pointer; IEEE-754 with no samplers and no
	 * binding table; no scratch space; the first payload register and the
	 * URB read length; the thread count, statistics, SIMD8 and enable.
	 */
	drv_i915_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_VS, GEN12_3DSTATE_VS_DWORDS));
	drv_i915_batch_emit(batch, I915_GFX_VS_KERNEL);
	drv_i915_batch_emit(batch, 0U);
	drv_i915_batch_emit(batch, 0U);
	drv_i915_batch_emit(batch, 0U);
	drv_i915_batch_emit(batch, 0U);
	drv_i915_batch_emit(batch, (kernels->vs_grf_start << 20) | (((kernels->vs_input_count + 1U) / 2U) << 11));
	drv_i915_batch_emit(batch, ((I915_GFX_MAX_VS_THREADS - 1U) << 22) | (1U << 10) | (1U << 2) | 1U);
	drv_i915_batch_emit(batch, 0U);
}

/*
 * Emits SBE, SBE_SWIZ, WM, PS and PS_EXTRA for the pixel kernel.
 *
 * The varyings are read from VUE slot 2 on, and fragment input n is the
 * n-th slot read.  The kernel starts at the pixel kernel offset and runs
 * 8-pixel dispatch only.
 */
void
drv_i915_gfx_emit_pixel_shader(
	struct i915_gfx_batch *batch,
	const struct i915_gfx_kernels *kernels)
{
	uint32_t index;
	uint32_t read_length;
	uint32_t low;
	uint32_t high;
	uint32_t has_samplers;
	uint32_t has_varyings;

	/* Reads the varyings in pairs of slots, at least one pair. */
	read_length = 1U;
	if (kernels->varyings != 0U)
		read_length = (kernels->varyings + 1U) / 2U;

	/*
	 * Programs SBE: the attribute swizzle and the read offset override, the
	 * number of attributes, the read length and the read offset of slot 2;
	 * every attribute with all four components active.
	 */
	drv_i915_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_SBE, GEN12_3DSTATE_SBE_DWORDS));
	drv_i915_batch_emit(batch,
			    (1U << 29) |
			    (1U << 28) |
			    (kernels->varyings << 22) |
			    (1U << 21) |
			    (read_length << 11) |
			    (1U << 5));
	drv_i915_batch_emit(batch, 0U);
	drv_i915_batch_emit(batch, 0U);
	drv_i915_batch_emit(batch, 0xffffffffU);
	drv_i915_batch_emit(batch, 0xffffffffU);

	/* Programs SBE_SWIZ: fragment input n takes the n-th slot read, two inputs to a dword. */
	drv_i915_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_SBE_SWIZ, GEN12_3DSTATE_SBE_SWIZ_DWORDS));
	for (index = 0U; index < 16U; index += 2U) {
		/* An input past the varyings takes slot 0. */
		low = 0U;
		if (index < kernels->varyings)
			low = index;
		high = 0U;
		if (index + 1U < kernels->varyings)
			high = index + 1U;
		drv_i915_batch_emit(batch, low | (high << 16));
	}

	/* Leaves the two swizzle control dwords at zero. */
	drv_i915_batch_emit(batch, 0U);
	drv_i915_batch_emit(batch, 0U);

	/* Programs WM: statistics, perspective pixel barycentrics, line AA width 1.0. */
	drv_i915_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_WM, GEN12_3DSTATE_WM_DWORDS));
	drv_i915_batch_emit(batch, (1U << 31) | (1U << 11) | (1U << 6));

	/* Notes whether the kernel samples. */
	has_samplers = 0U;
	if (kernels->ps_samplers != 0U)
		has_samplers = 1U;

	/*
	 * Programs PS: kernel 0 is the SIMD8 one; the vector mask, the sampler
	 * count and the binding table entries (the render target and the
	 * samplers); the thread count and 8-pixel dispatch; the first payload
	 * register.
	 */
	drv_i915_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_PS, GEN12_3DSTATE_PS_DWORDS));
	drv_i915_batch_emit(batch, I915_GFX_PS_KERNEL);
	drv_i915_batch_emit(batch, 0U);
	drv_i915_batch_emit(batch, (1U << 30) | (has_samplers << 27) | ((1U + kernels->ps_samplers) << 18));
	drv_i915_batch_emit(batch, 0U);
	drv_i915_batch_emit(batch, 0U);
	drv_i915_batch_emit(batch, ((GEN12_MAX_THREADS_PER_PSD - 1U) << 23) | 1U);
	drv_i915_batch_emit(batch, kernels->ps_grf_start << 16);
	drv_i915_batch_emit(batch, 0U);
	drv_i915_batch_emit(batch, 0U);
	drv_i915_batch_emit(batch, 0U);
	drv_i915_batch_emit(batch, 0U);

	/* Notes whether the kernel reads attributes. */
	has_varyings = 0U;
	if (kernels->varyings != 0U)
		has_varyings = 1U;

	/* Programs PS_EXTRA: valid, and whether the kernel reads attributes. */
	drv_i915_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_PS_EXTRA, GEN12_3DSTATE_PS_EXTRA_DWORDS));
	drv_i915_batch_emit(batch, (1U << 31) | (has_varyings << 8));
}

/*
 * Emits the end of a 3D batch: the pixel stage's sampler and binding table
 * pointers, the drawing rectangle, the primitive and the closing flush.
 *
 * The pixel pipeline is synced before the primitive, and every cache the
 * primitive wrote through is flushed after it.
 */
void
drv_i915_gfx_emit_primitive(
	struct i915_gfx_batch *batch,
	uint32_t width,
	uint32_t height,
	uint32_t topology,
	uint32_t vertex_count,
	uint32_t first_vertex,
	uint32_t instance_count,
	uint32_t first_instance)
{
	/* Points the pixel stage at its sampler and its binding table. */
	drv_i915_batch_pointer(batch, GEN12_CMD_3DSTATE_SAMPLER_STATE_POINTERS_PS, I915_GFX_DYN_SAMPLER);
	drv_i915_batch_pointer(batch, GEN12_CMD_3DSTATE_BINDING_TABLE_POINTERS_PS, I915_GFX_BINDING_TABLE);

	/* Limits drawing to the target. */
	drv_i915_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_DRAWING_RECTANGLE, GEN12_3DSTATE_DRAWING_RECTANGLE_DWORDS));
	drv_i915_batch_emit(batch, 0U);
	drv_i915_batch_emit(batch, (width - 1U) | ((height - 1U) << 16));
	drv_i915_batch_emit(batch, 0U);

	/* Syncs the pixel pipeline before the primitive. */
	drv_i915_batch_pipe_control(batch,
				    PIPE_CONTROL_CS_STALL |
				    PIPE_CONTROL_STALL_AT_SCOREBOARD |
				    PIPE_CONTROL_DEPTH_STALL_ENABLE);

	/* Draws the primitive. */
	drv_i915_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DPRIMITIVE, GEN12_3DPRIMITIVE_DWORDS));
	drv_i915_batch_emit(batch, topology);
	drv_i915_batch_emit(batch, vertex_count);
	drv_i915_batch_emit(batch, first_vertex);
	drv_i915_batch_emit(batch, instance_count);
	drv_i915_batch_emit(batch, first_instance);
	drv_i915_batch_emit(batch, 0U);

	/* Flushes what the primitive wrote and ends the batch. */
	drv_i915_batch_pipe_control(batch,
				    PIPE_CONTROL_CS_STALL |
				    PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH |
				    PIPE_CONTROL_DEPTH_CACHE_FLUSH |
				    PIPE_CONTROL_DC_FLUSH_ENABLE |
				    PIPE_CONTROL_FLUSH_ENABLE);
	drv_i915_batch_emit(batch, MI_BATCH_BUFFER_END);
	drv_i915_batch_emit(batch, MI_NOOP);
}

/* Translates a VkFormat to its genxml SURFACE_FORMAT; ENOTSUP for any other format. */
static int
i915_surface_format(
	uint32_t format,
	uint32_t *surface_format)
{
	/* Picks the surface format of each supported VkFormat. */
	switch (format) {
	case VK_FORMAT_R8G8B8A8_UNORM:
		*surface_format = I915_GFX_SURFACE_R8G8B8A8_UNORM;
		return 0;
	case VK_FORMAT_B8G8R8A8_UNORM:
		*surface_format = I915_GFX_SURFACE_B8G8R8A8_UNORM;
		return 0;
	case VK_FORMAT_R32_SFLOAT:
		*surface_format = I915_GFX_SURFACE_R32_FLOAT;
		return 0;
	case VK_FORMAT_R32G32_SFLOAT:
		*surface_format = I915_GFX_SURFACE_R32G32_FLOAT;
		return 0;
	case VK_FORMAT_R32G32B32_SFLOAT:
		*surface_format = I915_GFX_SURFACE_R32G32B32_FLOAT;
		return 0;
	case VK_FORMAT_R32G32B32A32_SFLOAT:
		*surface_format = I915_GFX_SURFACE_R32G32B32A32_FLOAT;
		return 0;
	default:
		return ENOTSUP;
	}
}

/* Reports how many components a vertex format has; any other format counts as four. */
static uint32_t
i915_format_components(
	uint32_t format)
{
	/* Picks the component count of the float vertex formats. */
	switch (format) {
	case VK_FORMAT_R32_SFLOAT:
		return 1U;
	case VK_FORMAT_R32G32_SFLOAT:
		return 2U;
	case VK_FORMAT_R32G32B32_SFLOAT:
		return 3U;
	default:
		return 4U;
	}
}

/*
 * Writes the RENDER_SURFACE_STATE of a linear 2D one-level image, as isl
 * fills it for the fixture texture and render target.
 */
static int
i915_image_surface_write(
	uint32_t *rss,
	const struct i915_gfx_image *image,
	uint32_t mocs)
{
	uint64_t va;
	uint32_t format;
	int error;

	/* Refuses an image with no storage. */
	va = drv_i915_gfx_memory_va(image->memory, image->offset);
	if (va == 0U)
		return EINVAL;

	/* Refuses a format the surface state cannot name. */
	error = i915_surface_format(image->format, &format);
	if (error != 0)
		return EINVAL;

	/*
	 * Fills the surface state: 2D, horizontal and vertical alignment 4,
	 * linear; the unorm path bit, MOCS and QPitch; width and height; pitch;
	 * mip tail start 1; identity channel select; the address.
	 */
	memset(rss, 0, GEN12_RENDER_SURFACE_STATE_DWORDS * 4U);
	rss[0] = (GEN12_SURFTYPE_2D << 29) |
	    (format << 18) |
	    (GEN12_SURFACE_ALIGN_4 << 16) |
	    (GEN12_SURFACE_ALIGN_4 << 14) |
	    (GEN12_TILEMODE_LINEAR << 12);
	rss[1] = (1U << 31) | (mocs << 24) | (((image->height + 3U) & ~3U) / 4U);
	rss[2] = (image->width - 1U) | ((image->height - 1U) << 16);
	rss[3] = image->pitch - 1U;
	rss[5] = 0x00000100U;
	rss[7] = (4U << 25) | (5U << 22) | (6U << 19) | (7U << 16);
	rss[8] = (uint32_t)va;
	rss[9] = (uint32_t)(va >> 32);

	/* Succeeded: the surface state describes the image. */
	return 0;
}

/*
 * Writes the binding table, the render target and texture surfaces and the
 * sampler of a draw.
 *
 * Binding table entry 0 is the render target and entry 1 the texture, the
 * layout the fixture kernels were compiled for.  The one sampled image is
 * set 0 binding 0.
 */
static int
i915_state_write_surfaces(
	uint32_t *surface,
	uint32_t *dynamic,
	const struct i915_gfx_draw_state *state,
	const struct i915_gfx_kernels *kernels,
	const struct i915_gfx_image *target,
	uint32_t mocs)
{
	const struct i915_gfx_dset *set;
	int error;

	/* Points the binding table at the two surface states. */
	surface[I915_GFX_BINDING_TABLE / 4U] = I915_GFX_RSS_TARGET;
	surface[I915_GFX_BINDING_TABLE / 4U + 1U] = I915_GFX_RSS_TEXTURE;

	/* Describes the render target. */
	error = i915_image_surface_write(&surface[I915_GFX_RSS_TARGET / 4U], target, mocs);
	if (error != 0)
		return error;

	/* A kernel that samples nothing needs no texture. */
	if (kernels->ps_samplers == 0U)
		return 0;

	/* Refuses a kernel that samples more than one image. */
	if (kernels->ps_samplers > 1U) {
		kern_logf("i915: vk: XXX unimplemented path: %u sampled images in one fragment shader\n", kernels->ps_samplers);
		return ENOTSUP;
	}

	/*
	 * Refuses a draw whose set 0 binding 0 lacks the view or the sampler.
	 * XXX: the one sampled image is set 0 binding 0 (binding table entry 1,
	 * sampler 0).
	 */
	set = state->dset[0];
	if (set == NULL ||
	    set->slots[0].view == NULL ||
	    set->slots[0].sampler == NULL) {
		kern_logf("i915: vk: draw refused: set 0 binding 0 has no image view and sampler\n");
		return EINVAL;
	}

	/* Describes the texture. */
	error = i915_image_surface_write(&surface[I915_GFX_RSS_TEXTURE / 4U], set->slots[0].view->image, mocs);
	if (error != 0)
		return error;

	/* Writes the texture's sampler. */
	drv_i915_gfx_sampler_write(&dynamic[I915_GFX_DYN_SAMPLER / 4U], set->slots[0].sampler);

	/* Succeeded: the target, the texture and the sampler are described. */
	return 0;
}

/*
 * Writes the blend state, the viewports and the scissor of a draw.
 *
 * XXX: the guardband is the viewport itself ([-1, 1] in NDC): correct, and
 * every primitive that leaves the viewport is clipped rather than trivially
 * accepted.
 */
static void
i915_state_write_viewport(
	uint32_t *dynamic,
	const struct i915_gfx_pipeline *pipeline)
{
	uint32_t *words;
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
	uint32_t half_width;
	uint32_t half_height;
	uint32_t right_edge;
	uint32_t bottom_edge;

	/* Clamps before and after blending to the render target's format. */
	words = &dynamic[I915_GFX_DYN_BLEND / 4U];
	words[2] = 1U | (1U << 1) | (GEN12_COLORCLAMP_RTFORMAT << 2);

	/* Gives CC_VIEWPORT the depth range. */
	dynamic[I915_GFX_DYN_CC_VIEWPORT / 4U] = pipeline->viewport[4];
	dynamic[I915_GFX_DYN_CC_VIEWPORT / 4U + 1U] = pipeline->viewport[5];

	/* Takes the viewport rectangle and its half extent. */
	x = pipeline->viewport[0];
	y = pipeline->viewport[1];
	width = pipeline->viewport[2];
	height = pipeline->viewport[3];
	half_width = drv_i915_float_half(width);
	half_height = drv_i915_float_half(height);

	/* Finds the far edges of the viewport, one pixel short of x + width and y + height. */
	right_edge = drv_i915_float_add(x, width);
	right_edge = drv_i915_float_sub(right_edge, I915_FLOAT_ONE);
	bottom_edge = drv_i915_float_add(y, height);
	bottom_edge = drv_i915_float_sub(bottom_edge, I915_FLOAT_ONE);

	/*
	 * Fills SF_CLIP_VIEWPORT: the transform m00 m11 m22 m30 m31 m32, two
	 * reserved words, the guardband x- x+ y- y+ and the viewport x- x+ y- y+.
	 */
	words = &dynamic[I915_GFX_DYN_SF_CLIP_VIEWPORT / 4U];
	words[0] = half_width;
	words[1] = half_height;
	words[2] = drv_i915_float_sub(pipeline->viewport[5], pipeline->viewport[4]);
	words[3] = drv_i915_float_add(x, half_width);
	words[4] = drv_i915_float_add(y, half_height);
	words[5] = pipeline->viewport[4];
	words[8] = I915_FLOAT_MINUS_ONE;
	words[9] = I915_FLOAT_ONE;
	words[10] = I915_FLOAT_MINUS_ONE;
	words[11] = I915_FLOAT_ONE;
	words[12] = x;
	words[13] = right_edge;
	words[14] = y;
	words[15] = bottom_edge;

	/* Fills SCISSOR_RECT with the inclusive corners of the scissor. */
	words = &dynamic[I915_GFX_DYN_SCISSOR / 4U];
	words[0] = ((uint32_t)pipeline->scissor.offset.x & 0xffffU) |
	    (((uint32_t)pipeline->scissor.offset.y & 0xffffU) << 16);
	words[1] = (((uint32_t)pipeline->scissor.offset.x + pipeline->scissor.extent.width - 1U) & 0xffffU) |
	    ((((uint32_t)pipeline->scissor.offset.y + pipeline->scissor.extent.height - 1U) & 0xffffU) << 16);
}
