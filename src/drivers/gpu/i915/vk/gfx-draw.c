/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * One recorded draw -> Gen12 state and a batch -> the GPU (see gfx.h).
 *
 * The command list is the one that draws on this hardware (selftest.c i915_draw_build_batch, the
 * fixture of E-101 .. E-105) with what a Vulkan draw adds to it: an enabled vertex shader, vertex
 * buffers, push constants, the viewport / scissor, the depth buffer, SBE and a sampled texture.
 * Bit positions are those of Mesa's genxml (gen120); the words that depend on the compiled shaders
 * come from vkref-generated.inc.
 *
 * XXX: THE KERNELS.  The pipeline's SPIR-V is recognised by content (word count + FNV-1a) and the
 * kernels used are the ones Mesa's compiler produced from those very modules (tools/refvk.c).  The
 * executor's own compiler (spirv.c / compile.c / eu.c) is NOT in this path yet: its payload, URB
 * write, sampler and render-target messages have never run on a GPU.  A pipeline with any other
 * SPIR-V is refused at creation.
 */

#include "gfx.h"

#include "../internal.h"
#include "../parity/legacy_shim.h"

#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/pmem.h>
#include <kern/device-io.h>

#include <errno.h>
#include <string.h>

#include "../linux/i915-commands.inc"
#include "linux/3dstate-gen12.inc"
#include "vkref-generated.inc"

#ifndef I915_VK_GFX_DUMP
#define I915_VK_GFX_DUMP 0
#endif

/* genxml gen120: commands 3dstate-gen12.inc does not carry yet */
#define GFX_CMD_3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP	0x7821U
#define GFX_CMD_3DSTATE_SCISSOR_STATE_POINTERS		0x780FU
#define GFX_CMD_3DSTATE_SAMPLER_STATE_POINTERS_PS	0x782FU

/* One object holds every heap of a draw.  Offsets from its start: */
#define GFX_STATE_BYTES			65536U
#define GFX_SURFACE_HEAP		0x0000U		/* surface state base */
#define GFX_BINDING_TABLE		0x0000U		/*   offsets from the surface state base */
#define GFX_RSS_TARGET			0x0040U
#define GFX_RSS_TEXTURE			0x0080U
#define GFX_DYNAMIC_HEAP		0x1000U		/* dynamic state base */
#define GFX_DYN_COLOR_CALC		0x0000U		/*   offsets from the dynamic state base */
#define GFX_DYN_BLEND			0x0040U
#define GFX_DYN_CC_VIEWPORT		0x0080U
#define GFX_DYN_SF_CLIP_VIEWPORT	0x00c0U
#define GFX_DYN_SCISSOR			0x0100U
#define GFX_DYN_SAMPLER			0x0140U
#define GFX_DYN_CPS			0x0180U
#define GFX_PUSH_BUFFER			0x2000U		/* absolute address in 3DSTATE_CONSTANT_VS */
#define GFX_SCRATCH			0x3000U		/* post-sync writes land here */
#define GFX_INSTRUCTION_HEAP		0x4000U		/* instruction base; kernels at VKREF_*_KERNEL_OFFSET */
#define GFX_INSTRUCTION_BYTES		0x4000U
#define GFX_BATCH_BYTES			16384U

/* What a session keeps between draws. */
struct gfx_session {
	struct i915_gem_object *state;
	struct i915_gem_object *batch;
	unsigned draws;
};

struct gfx_batch {
	uint32_t *cmds;
	unsigned count;
	unsigned capacity;
	int overflow;
};

static void
emit(struct gfx_batch *b, uint32_t dword)
{
	if (b->count < b->capacity)
		b->cmds[b->count] = dword;
	else
		b->overflow = 1;
	b->count++;
}

static void
emit_zero(struct gfx_batch *b, uint32_t opcode, uint32_t dwords)
{
	uint32_t index;

	emit(b, GEN12_CMD_HEADER(opcode, dwords));
	for (index = 1U; index < dwords; index++)
		emit(b, 0U);
}

static void
emit_words(struct gfx_batch *b, const uint32_t *words, unsigned count)
{
	unsigned index;

	for (index = 0U; index < count; index++)
		emit(b, words[index]);
}

static void
emit_pointer(struct gfx_batch *b, uint32_t opcode, uint32_t value)
{
	emit(b, GEN12_CMD_HEADER(opcode, GEN12_3DSTATE_POINTERS_DWORDS));
	emit(b, value);
}

/* selftest.c i915_draw_emit_pipe_control(): the RT flush also asks for the HDC pipeline flush. */
static void
emit_pc(struct gfx_batch *b, uint32_t flags)
{
	unsigned index;

	emit(b, GFX_OP_PIPE_CONTROL(6) |
		((flags & PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH) != 0U ? PIPE_CONTROL0_HDC_PIPELINE_FLUSH : 0U));
	emit(b, flags);
	for (index = 0U; index < 4U; index++)
		emit(b, 0U);
}

/* ---------------- floats without a floating-point register ---------------- */

static uint32_t
sf_half(uint32_t a)
{
	if (((a >> 23) & 0xffU) <= 1U)
		return a & 0x80000000U;
	return a - (1U << 23);
}

/* a + b for zeros and normal numbers (the viewport arithmetic needs nothing else). */
static uint32_t
sf_add(uint32_t a, uint32_t b)
{
	uint32_t ea, eb, sign;
	uint64_t ma, mb, m;

	if ((a & 0x7fffffffU) == 0U)
		return b;
	if ((b & 0x7fffffffU) == 0U)
		return a;
	/* |a| >= |b| */
	if ((a & 0x7fffffffU) < (b & 0x7fffffffU)) {
		uint32_t t = a;

		a = b;
		b = t;
	}
	ea = (a >> 23) & 0xffU;
	eb = (b >> 23) & 0xffU;
	sign = a & 0x80000000U;
	ma = ((uint64_t)((a & 0x7fffffU) | 0x800000U)) << 8;
	mb = ((uint64_t)((b & 0x7fffffU) | 0x800000U)) << 8;
	if (ea - eb > 40U)
		return a;
	mb >>= ea - eb;
	m = ((a ^ b) & 0x80000000U) != 0U ? ma - mb : ma + mb;
	if (m == 0U)
		return 0U;
	while (m >= ((uint64_t)1U << 32)) {
		m >>= 1;
		ea++;
	}
	while (m < ((uint64_t)1U << 31)) {
		m <<= 1;
		ea--;
	}
	m = (m + 0x80U) >> 8;
	if (m >= (1U << 24)) {
		m >>= 1;
		ea++;
	}
	return sign | (ea << 23) | ((uint32_t)m & 0x7fffffU);
}

static uint32_t
sf_sub(uint32_t a, uint32_t b)
{
	return sf_add(a, b ^ 0x80000000U);
}

#define SF_ONE		0x3f800000U
#define SF_MINUS_ONE	0xbf800000U

/* ---------------- the session's objects ---------------- */

static int
gfx_object(struct i915_vk_session *session, uint64_t bytes, struct i915_gem_object **out)
{
	struct i915_device *device;
	int error;

	device = session->vk->i915;
	mutex_lock(&device->mutex);
	error = drv_i915_gem_create(device, bytes, out);
	if (error == 0) {
		error = drv_i915_gem_bind_vm(session->gpu->vm, *out);
		if (error != 0)
			drv_i915_gem_destroy(device, *out);
	}
	mutex_unlock(&device->mutex);
	if (error != 0)
		*out = NULL;
	return error;
}

static struct gfx_session *
gfx_session(struct i915_vk_session *session)
{
	struct gfx_session *gs;

	if (session->gfx != NULL)
		return session->gfx;
	gs = kern_calloc(1U, sizeof(*gs));
	if (gs == NULL)
		return NULL;
	if (gfx_object(session, GFX_STATE_BYTES, &gs->state) != 0 ||
	    gfx_object(session, GFX_BATCH_BYTES, &gs->batch) != 0) {
		/* XXX: the first object is not released on this path (happy path only) */
		kern_free(gs);
		return NULL;
	}
	session->gfx = gs;
	return gs;
}

void
i915_vk_gfx_session_close(struct i915_vk_session *session)
{
	struct gfx_session *gs;
	struct i915_device *device;

	gs = session->gfx;
	if (gs == NULL)
		return;
	device = session->vk->i915;
	mutex_lock(&device->mutex);
	drv_i915_gem_unbind_vm(gs->state);
	drv_i915_gem_destroy(device, gs->state);
	drv_i915_gem_unbind_vm(gs->batch);
	drv_i915_gem_destroy(device, gs->batch);
	mutex_unlock(&device->mutex);
	kern_free(gs);
	session->gfx = NULL;
}

/* ---------------- kernels ---------------- */

static uint32_t
gfx_fnv1a(const uint32_t *words, uint32_t count)
{
	const uint8_t *bytes = (const uint8_t *)words;
	uint32_t hash = 2166136261U;
	uint32_t index;

	for (index = 0U; index < count * 4U; index++) {
		hash ^= bytes[index];
		hash *= 16777619U;
	}
	return hash;
}

int
i915_vk_gfx_pipeline_prepare(struct i915_vk_session *session, struct gfx_pipeline *pipeline)
{
	(void)session;
	if (pipeline->vertex == NULL || pipeline->fragment == NULL)
		return EINVAL;

	/* XXX: see the header comment -- kernels by content, not from the executor's compiler. */
	if (pipeline->vertex->word_count != VKREF_VS_SPIRV_WORDS ||
	    gfx_fnv1a(pipeline->vertex->words, pipeline->vertex->word_count) != VKREF_VS_SPIRV_FNV1A ||
	    pipeline->fragment->word_count != VKREF_FS_SPIRV_WORDS ||
	    gfx_fnv1a(pipeline->fragment->words, pipeline->fragment->word_count) != VKREF_FS_SPIRV_FNV1A) {
		kern_logf("i915: vk: XXX unimplemented path: the pipeline's SPIR-V (vs %u words, fs %u words) is not the pair "
			"the reference kernels were generated from, and the executor's own compiler is not connected\n",
			pipeline->vertex->word_count, pipeline->fragment->word_count);
		return ENOTSUP;
	}
	kern_logf("i915: vk: XXX pipeline runs Mesa-generated reference kernels for its SPIR-V "
		"(vs %u bytes, ps %u bytes); the executor's compiler is not in this path\n",
		VKREF_VS_BYTES, VKREF_PS_BYTES);
	pipeline->kernels_ready = 1;
	return 0;
}

/* ---------------- state ---------------- */

/* genxml SURFACE_FORMAT */
static int
gfx_surface_format(uint32_t format, uint32_t *out)
{
	switch (format) {
	case VK_FORMAT_R8G8B8A8_UNORM:		*out = 0x0c7U; return 0;
	case VK_FORMAT_B8G8R8A8_UNORM:		*out = 0x0c0U; return 0;
	case VK_FORMAT_R32_SFLOAT:		*out = 0x0d8U; return 0;
	case VK_FORMAT_R32G32_SFLOAT:		*out = 0x085U; return 0;
	case VK_FORMAT_R32G32B32_SFLOAT:	*out = 0x040U; return 0;
	case VK_FORMAT_R32G32B32A32_SFLOAT:	*out = 0x000U; return 0;
	default:
		return ENOTSUP;
	}
}

static uint32_t
gfx_format_components(uint32_t format)
{
	switch (format) {
	case VK_FORMAT_R32_SFLOAT:		return 1U;
	case VK_FORMAT_R32G32_SFLOAT:		return 2U;
	case VK_FORMAT_R32G32B32_SFLOAT:	return 3U;
	default:				return 4U;
	}
}

/*
 * RENDER_SURFACE_STATE of a linear 2D one-level image, as isl fills it (tex_fixture_gen.inc
 * texfix_tex_rss, tex_fixture_fhd_gen.inc texfhd_rt_rss): 2D, halign / valign 4, linear; the unorm
 * path bit, MOCS, QPitch; width / height; pitch; mip tail 1; identity channel select; the address.
 */
static int
gfx_write_rss(uint32_t *rss, const struct gfx_image *image, uint32_t mocs)
{
	uint64_t va;
	uint32_t format;

	va = i915_vk_gfx_memory_va(image->memory, image->offset);
	if (va == 0U || gfx_surface_format(image->format, &format) != 0)
		return EINVAL;
	memset(rss, 0, GEN12_RENDER_SURFACE_STATE_DWORDS * 4U);
	rss[0] = (GEN12_SURFTYPE_2D << 29) | (format << 18) | (GEN12_SURFACE_ALIGN_4 << 16) |
		(GEN12_SURFACE_ALIGN_4 << 14) | (GEN12_TILEMODE_LINEAR << 12);
	rss[1] = (1U << 31) | (mocs << 24) | (((image->height + 3U) & ~3U) / 4U);
	rss[2] = (image->width - 1U) | ((image->height - 1U) << 16);
	rss[3] = image->pitch - 1U;
	rss[5] = 0x00000100U;
	rss[7] = (4U << 25) | (5U << 22) | (6U << 19) | (7U << 16);
	rss[8] = (uint32_t)va;
	rss[9] = (uint32_t)(va >> 32);
	return 0;
}

/* genxml SAMPLER_STATE, the fields reftex.c sets (tex_fixture_gen.inc texfix_sampler / _linear). */
static void
gfx_write_sampler(uint32_t *state, const struct gfx_sampler *sampler)
{
	static const uint32_t address[5] = { 0U /* REPEAT = WRAP */, 1U /* MIRRORED */, 2U /* CLAMP */,
		4U /* CLAMP_TO_BORDER */, 5U /* MIRROR_ONCE */ };
	uint32_t u, v, linear;

	u = sampler->address_u < 5U ? address[sampler->address_u] : 2U;
	v = sampler->address_v < 5U ? address[sampler->address_v] : 2U;
	linear = sampler->mag_filter == VK_FILTER_LINEAR || sampler->min_filter == VK_FILTER_LINEAR;
	state[0] = (2U << 27) |					/* LOD pre-clamp: OGL */
		((sampler->mag_filter == VK_FILTER_LINEAR ? 1U : 0U) << 17) |
		((sampler->min_filter == VK_FILTER_LINEAR ? 1U : 0U) << 14);
	state[1] = 0U;						/* XXX: LOD [0, 0]: one mip level */
	state[2] = 0U;
	state[3] = (linear != 0U ? 0x0007e000U : 0U) |		/* address rounding on for a linear filter */
		(u << 6) | (v << 3) | 2U;
}

static const uint32_t gfx_eot_only[4] = {	/* selftest.c: a null render-target write that ends the thread */
	0x00030032U, 0x00001004U, 0x58007024U, 0x00c40000U,
};

/* Everything the batch points at.  Returns 0 or what is missing. */
static int
gfx_write_state(uint8_t *page, uint64_t state_va, const struct gfx_draw_state *state,
	const struct gfx_image *target, uint32_t mocs)
{
	const struct gfx_pipeline *pipeline = state->pipeline;
	uint32_t *surface = (uint32_t *)(void *)(page + GFX_SURFACE_HEAP);
	uint32_t *dynamic = (uint32_t *)(void *)(page + GFX_DYNAMIC_HEAP);
	uint32_t *words;
	uint32_t x, y, w, h, half_w, half_h;
	unsigned at;
	int error;

	(void)state_va;
	memset(page, 0, GFX_INSTRUCTION_HEAP);

	/* binding table: [0] the render target, [1] the texture (the layout refvk.c compiled for) */
	surface[GFX_BINDING_TABLE / 4U] = GFX_RSS_TARGET;
	surface[GFX_BINDING_TABLE / 4U + 1U] = GFX_RSS_TEXTURE;
	error = gfx_write_rss(&surface[GFX_RSS_TARGET / 4U], target, mocs);
	if (error != 0)
		return error;
	if (state->dset[0] == NULL || state->dset[0]->slots[0].view == NULL || state->dset[0]->slots[0].sampler == NULL) {
		kern_logf("i915: vk: draw refused: set 0 binding 0 has no image view and sampler\n");
		return EINVAL;
	}
	error = gfx_write_rss(&surface[GFX_RSS_TEXTURE / 4U], state->dset[0]->slots[0].view->image, mocs);
	if (error != 0)
		return error;
	gfx_write_sampler(&dynamic[GFX_DYN_SAMPLER / 4U], state->dset[0]->slots[0].sampler);

	/* BLEND_STATE: global dword, then the entry -- pre / post blend clamp to the target's format */
	words = &dynamic[GFX_DYN_BLEND / 4U];
	words[2] = 1U | (1U << 1) | (GEN12_COLORCLAMP_RTFORMAT << 2);

	/* CC_VIEWPORT: the depth range */
	dynamic[GFX_DYN_CC_VIEWPORT / 4U] = pipeline->viewport[4];
	dynamic[GFX_DYN_CC_VIEWPORT / 4U + 1U] = pipeline->viewport[5];

	/*
	 * SF_CLIP_VIEWPORT: m00 m11 m22 m30 m31 m32 (2 reserved) guardband x- x+ y- y+ viewport x- x+ y- y+.
	 * XXX: the guardband is the viewport itself ([-1, 1] in NDC): correct, and every primitive that
	 * leaves the viewport is clipped rather than trivially accepted.
	 */
	x = pipeline->viewport[0];
	y = pipeline->viewport[1];
	w = pipeline->viewport[2];
	h = pipeline->viewport[3];
	half_w = sf_half(w);
	half_h = sf_half(h);
	words = &dynamic[GFX_DYN_SF_CLIP_VIEWPORT / 4U];
	words[0] = half_w;
	words[1] = half_h;
	words[2] = sf_sub(pipeline->viewport[5], pipeline->viewport[4]);
	words[3] = sf_add(x, half_w);
	words[4] = sf_add(y, half_h);
	words[5] = pipeline->viewport[4];
	words[8] = SF_MINUS_ONE;
	words[9] = SF_ONE;
	words[10] = SF_MINUS_ONE;
	words[11] = SF_ONE;
	words[12] = x;
	words[13] = sf_sub(sf_add(x, w), SF_ONE);
	words[14] = y;
	words[15] = sf_sub(sf_add(y, h), SF_ONE);

	/* SCISSOR_RECT: inclusive */
	words = &dynamic[GFX_DYN_SCISSOR / 4U];
	words[0] = ((uint32_t)pipeline->scissor.offset.x & 0xffffU) | (((uint32_t)pipeline->scissor.offset.y & 0xffffU) << 16);
	words[1] = (((uint32_t)pipeline->scissor.offset.x + pipeline->scissor.extent.width - 1U) & 0xffffU) |
		((((uint32_t)pipeline->scissor.offset.y + pipeline->scissor.extent.height - 1U) & 0xffffU) << 16);

	/* push constants: the block the vertex shader reads, register by register */
	memcpy(page + GFX_PUSH_BUFFER, state->push, VKREF_VS_PUSH_REGS * 32U <= GFX_PUSH_BYTES ? VKREF_VS_PUSH_REGS * 32U : GFX_PUSH_BYTES);

	/* instruction heap: a thread that starts anywhere it should not retires at once */
	for (at = 0U; at + sizeof(gfx_eot_only) <= GFX_INSTRUCTION_BYTES; at += sizeof(gfx_eot_only))
		memcpy(page + GFX_INSTRUCTION_HEAP + at, gfx_eot_only, sizeof(gfx_eot_only));
	memcpy(page + GFX_INSTRUCTION_HEAP + VKREF_VS_KERNEL_OFFSET, vkref_vs_kernel, VKREF_VS_BYTES);
	memcpy(page + GFX_INSTRUCTION_HEAP + VKREF_PS_KERNEL_OFFSET, vkref_ps_kernel, VKREF_PS_BYTES);
	return 0;
}

/* ---------------- the batch ---------------- */

static void
emit_sba(struct gfx_batch *b, uint64_t state_va, uint32_t mocs)
{
	uint64_t surface = state_va + GFX_SURFACE_HEAP;
	uint64_t dynamic = state_va + GFX_DYNAMIC_HEAP;
	uint64_t instruction = state_va + GFX_INSTRUCTION_HEAP;

	emit(b, GEN12_CMD_HEADER(GEN12_CMD_STATE_BASE_ADDRESS, GEN12_STATE_BASE_ADDRESS_DWORDS));
	emit(b, 1U | (mocs << 4));				/* general: base 0, modify */
	emit(b, 0U);
	emit(b, mocs << 16);
	emit(b, 1U | (mocs << 4) | ((uint32_t)surface & 0xfffff000U));
	emit(b, (uint32_t)(surface >> 32));
	emit(b, 1U | (mocs << 4) | ((uint32_t)dynamic & 0xfffff000U));
	emit(b, (uint32_t)(dynamic >> 32));
	emit(b, 1U | (mocs << 4));				/* indirect: base 0, modify */
	emit(b, 0U);
	emit(b, 1U | (GEN12_MOCS(I915_MOCS_WRITEBACK_INDEX) << 4) | ((uint32_t)instruction & 0xfffff000U));
	emit(b, (uint32_t)(instruction >> 32));
	emit(b, 1U | (0xfffffU << 12));				/* general size */
	emit(b, 1U | (0xfffffU << 12));				/* dynamic size */
	emit(b, 1U | (0xfffffU << 12));				/* indirect size */
	emit(b, 1U | (0xfffffU << 12));				/* instruction size */
	emit(b, 1U | (mocs << 4) | ((uint32_t)surface & 0xfffff000U));	/* bindless surface state */
	emit(b, (uint32_t)(surface >> 32));
	emit(b, (4096U / 64U - 1U) << 12);
	emit(b, 1U | (mocs << 4));				/* bindless sampler: null */
	emit(b, 0U);
	emit(b, 0U);
}

/* Vertex buffers and elements.  The elements follow the locations in ascending order: that is the
 * order in which the compiled vertex shader finds its attributes in the payload. */
static int
emit_vertex_input(struct gfx_batch *b, const struct gfx_draw_state *state, uint32_t mocs)
{
	const struct gfx_pipeline *pipeline = state->pipeline;
	uint32_t order[GFX_MAX_VERTEX_ATTRIBUTES];
	uint32_t index, other, count, format, components;

	count = pipeline->attribute_count;
	if (count == 0U || pipeline->binding_count == 0U)
		return EINVAL;
	for (index = 0U; index < count; index++)
		order[index] = index;
	for (index = 0U; index < count; index++)
		for (other = index + 1U; other < count; other++)
			if (pipeline->attributes[order[other]].location < pipeline->attributes[order[index]].location) {
				uint32_t t = order[index];

				order[index] = order[other];
				order[other] = t;
			}
	/* XXX: every declared attribute must be one the shader reads (refvk: inputs_read), or the order shifts */
	for (index = 0U; index < count; index++)
		if (((VKREF_VS_INPUTS_READ >> (VKREF_VERT_ATTRIB_GENERIC0 + pipeline->attributes[order[index]].location)) & 1U) == 0U)
			return ENOTSUP;

	emit(b, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_VERTEX_BUFFERS, 1U + pipeline->binding_count * GEN12_VERTEX_BUFFER_STATE_DWORDS));
	for (index = 0U; index < pipeline->binding_count; index++) {
		uint32_t binding = pipeline->bindings[index].binding;
		struct gfx_buffer *buffer;
		uint64_t va;

		if (binding >= GFX_MAX_VERTEX_BINDINGS || (buffer = state->vertex[binding].buffer) == NULL ||
		    state->vertex[binding].offset > buffer->size)
			return EINVAL;
		va = i915_vk_gfx_memory_va(buffer->memory, buffer->offset + state->vertex[binding].offset);
		if (va == 0U)
			return EINVAL;
		emit(b, (binding << 26) | GEN12_VERTEX_BUFFER_L3_BYPASS_DISABLE | (mocs << 16) | (1U << 14) |
			(pipeline->bindings[index].stride & 0xfffU));
		emit(b, (uint32_t)va);
		emit(b, (uint32_t)(va >> 32));
		emit(b, (uint32_t)(buffer->size - state->vertex[binding].offset));
	}

	emit(b, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_VERTEX_ELEMENTS, 1U + count * GEN12_VERTEX_ELEMENT_STATE_DWORDS));
	for (index = 0U; index < count; index++) {
		if (gfx_surface_format(pipeline->attributes[order[index]].format, &format) != 0)
			return ENOTSUP;
		components = gfx_format_components(pipeline->attributes[order[index]].format);
		emit(b, (pipeline->attributes[order[index]].binding << 26) | (1U << 25) | (format << 16) |
			(pipeline->attributes[order[index]].offset & 0xfffU));
		emit(b, (GEN12_VFCOMP_STORE_SRC << 28) |
			((components > 1U ? GEN12_VFCOMP_STORE_SRC : GEN12_VFCOMP_STORE_0) << 24) |
			((components > 2U ? GEN12_VFCOMP_STORE_SRC : GEN12_VFCOMP_STORE_0) << 20) |
			((components > 3U ? GEN12_VFCOMP_STORE_SRC : GEN12_VFCOMP_STORE_1_FP) << 16));
	}

	emit(b, (GEN12_CMD_3DSTATE_VF_STATISTICS << 16) | 1U);
	emit_zero(b, GEN12_CMD_3DSTATE_VF, GEN12_3DSTATE_VF_DWORDS);
	emit_zero(b, GEN12_CMD_3DSTATE_VF_SGVS, GEN12_3DSTATE_VF_SGVS_DWORDS);
	emit_zero(b, GEN12_CMD_3DSTATE_VF_SGVS_2, GEN12_3DSTATE_VF_SGVS_2_DWORDS);
	for (index = 0U; index < count; index++) {
		emit(b, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_VF_INSTANCING, GEN12_3DSTATE_VF_INSTANCING_DWORDS));
		emit(b, index);
		emit(b, 0U);
	}
	if (pipeline->topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) {
		kern_logf("i915: vk: XXX unimplemented path: primitive topology %u\n", pipeline->topology);
		return ENOTSUP;
	}
	emit(b, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_VF_TOPOLOGY, GEN12_3DSTATE_VF_TOPOLOGY_DWORDS));
	emit(b, GEN12_3DPRIM_TRILIST);
	return 0;
}

/* selftest.c i915_draw_emit_urb(), with the push-constant space split between the VS and the PS. */
static void
emit_urb(struct gfx_batch *b)
{
	uint32_t opcode;

	emit(b, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_PUSH_CONSTANT_ALLOC_VS, GEN12_3DSTATE_PUSH_CONSTANT_ALLOC_DWORDS));
	emit(b, (0U << 16) | (GEN12_PUSH_CONSTANT_KB / 2U));
	for (opcode = GEN12_CMD_3DSTATE_PUSH_CONSTANT_ALLOC_HS; opcode < GEN12_CMD_3DSTATE_PUSH_CONSTANT_ALLOC_PS; opcode++)
		emit_zero(b, opcode, GEN12_3DSTATE_PUSH_CONSTANT_ALLOC_DWORDS);
	emit(b, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_PUSH_CONSTANT_ALLOC_PS, GEN12_3DSTATE_PUSH_CONSTANT_ALLOC_DWORDS));
	emit(b, ((GEN12_PUSH_CONSTANT_KB / 2U) << 16) | (GEN12_PUSH_CONSTANT_KB / 2U));

	/* The vertex stage owns the URB past the push constants: 3576 entries of one 64-byte slot. */
	emit(b, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_URB_ALLOC_VS, GEN12_3DSTATE_URB_ALLOC_DWORDS));
	emit(b, (4U << 10) | (4U << 21) | ((VKREF_VS_URB_ENTRY_SIZE - 1U) << 0));
	emit(b, 3576U | (3576U << 16));
	for (opcode = GEN12_CMD_3DSTATE_URB_ALLOC_HS; opcode <= GEN12_CMD_3DSTATE_URB_ALLOC_GS; opcode++) {
		emit(b, GEN12_CMD_HEADER(opcode, GEN12_3DSTATE_URB_ALLOC_DWORDS));
		emit(b, (5U << 10) | (5U << 21));
		emit(b, 0U);
	}
}

static void
emit_constants(struct gfx_batch *b, uint64_t push_va, uint32_t mocs)
{
	static const uint32_t opcodes[5] = {
		GEN12_CMD_3DSTATE_CONSTANT_VS, GEN12_CMD_3DSTATE_CONSTANT_HS, GEN12_CMD_3DSTATE_CONSTANT_DS,
		GEN12_CMD_3DSTATE_CONSTANT_GS, GEN12_CMD_3DSTATE_CONSTANT_PS,
	};
	unsigned stage, index;

	for (stage = 0U; stage < 5U; stage++) {
		emit(b, GEN12_CMD_HEADER(opcodes[stage], GEN12_3DSTATE_CONSTANT_DWORDS) | (mocs << 8));
		if (stage == 0U && VKREF_VS_PUSH_REGS != 0U) {
			/* anv: the highest slot first, so that slot 0 is never the only one in use */
			emit(b, 0U);
			emit(b, VKREF_VS_PUSH_REGS << 16);		/* read length of buffer 3 */
			for (index = 3U; index < 9U; index++)
				emit(b, 0U);
			emit(b, (uint32_t)push_va);
			emit(b, (uint32_t)(push_va >> 32));
			continue;
		}
		for (index = 1U; index < GEN12_3DSTATE_CONSTANT_DWORDS; index++)
			emit(b, 0U);
	}
}

/* anv genX_gfx_state.c: 3DSTATE_CLIP / SF / RASTER of an ordinary Vulkan pipeline. */
static void
emit_raster(struct gfx_batch *b, const struct gfx_pipeline *pipeline)
{
	uint32_t cull, index;

	emit(b, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_CLIP, GEN12_3DSTATE_CLIP_DWORDS));
	emit(b, (1U << 10) | (1U << 18));			/* statistics, early cull; 8-bit subpixel precision */
	emit(b, (1U << 31) | (1U << 30) | (1U << 28) | (1U << 26));	/* clip, API mode D3D (z in [0, 1]), viewport XY test, guardband */
	emit(b, (1U << 17) | (2047U << 6));			/* point width 0.125 .. 255.875 */

	emit(b, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_SF, GEN12_3DSTATE_SF_DWORDS));
	emit(b, (1U << 1) | (1U << 10) | (128U << 12));		/* viewport transform, statistics, line width 1.0 */
	emit(b, GEN12_URB_DEREF_BLOCK_SIZE_32 << 29);
	emit(b, 8U | (1U << 11) | (1U << 14));			/* point width 1.0 from state, AA line distance */

	switch (pipeline->cull_mode) {
	case VK_CULL_MODE_NONE:		cull = GEN12_CULLMODE_NONE; break;
	case VK_CULL_MODE_FRONT_BIT:	cull = GEN12_CULLMODE_FRONT; break;
	case VK_CULL_MODE_BACK_BIT:	cull = GEN12_CULLMODE_BACK; break;
	default:			cull = GEN12_CULLMODE_BOTH; break;
	}
	emit(b, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_RASTER, GEN12_3DSTATE_RASTER_DWORDS));
	emit(b, (1U << 0) | (1U << 26) | (1U << 1) |		/* z near / far clip test, scissor */
		(cull << 16) | ((pipeline->front_face == VK_FRONT_FACE_COUNTER_CLOCKWISE ? 1U : 0U) << 21) |
		(2U << 22));					/* API mode DX10.1+ */
	for (index = 2U; index < GEN12_3DSTATE_RASTER_DWORDS; index++)
		emit(b, 0U);
}

static int
emit_depth(struct gfx_batch *b, const struct gfx_draw_state *state, const struct gfx_image *depth,
	uint64_t scratch_va, uint32_t mocs)
{
	static const uint32_t compare[8] = { 1U, 2U, 3U, 4U, 5U, 6U, 7U, 0U };	/* VkCompareOp -> COMPAREFUNCTION */
	const struct gfx_pipeline *pipeline = state->pipeline;
	uint32_t index;
	uint64_t va;

	emit(b, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_WM_DEPTH_STENCIL, GEN12_3DSTATE_WM_DEPTH_STENCIL_DWORDS));
	emit(b, depth != NULL ? ((pipeline->depth_write != 0U ? 1U : 0U) | ((pipeline->depth_test != 0U ? 1U : 0U) << 1) |
		(compare[pipeline->depth_compare & 7U] << 5)) : 0U);
	emit(b, 0U);
	emit(b, 0U);

	emit(b, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_DEPTH_BUFFER, GEN12_3DSTATE_DEPTH_BUFFER_DWORDS));
	if (depth != NULL) {
		va = i915_vk_gfx_memory_va(depth->memory, depth->offset);
		if (va == 0U || depth->format != VK_FORMAT_D32_SFLOAT)
			return EINVAL;
		/* isl_emit_depth_stencil_hiz_s(): 2D, D32_FLOAT, write enable, Y-tiled (Gen9+ depth always is) */
		emit(b, (GEN12_SURFTYPE_2D << 29) | (1U << 28) | (GEN12_DEPTH_FORMAT_D32_FLOAT << 24) | (depth->pitch - 1U));
		emit(b, (uint32_t)va);
		emit(b, (uint32_t)(va >> 32));
		emit(b, ((depth->width - 1U) << 1) | ((depth->height - 1U) << 17));
		emit(b, mocs);
		emit(b, 0U);
		emit(b, ((depth->height + 3U) & ~3U) / 4U);
	} else {
		emit(b, (GEN12_SURFTYPE_NULL << 29) | (GEN12_DEPTH_FORMAT_D32_FLOAT << 24));
		for (index = 2U; index < GEN12_3DSTATE_DEPTH_BUFFER_DWORDS; index++)
			emit(b, 0U);
	}
	emit(b, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_STENCIL_BUFFER, GEN12_3DSTATE_STENCIL_BUFFER_DWORDS));
	emit(b, GEN12_SURFTYPE_NULL << 29);
	for (index = 2U; index < GEN12_3DSTATE_STENCIL_BUFFER_DWORDS; index++)
		emit(b, 0U);
	emit_zero(b, GEN12_CMD_3DSTATE_HIER_DEPTH_BUFFER, GEN12_3DSTATE_HIER_DEPTH_BUFFER_DWORDS);
	emit_zero(b, GEN12_CMD_3DSTATE_CLEAR_PARAMS, GEN12_3DSTATE_CLEAR_PARAMS_DWORDS);

	/* anv Wa_1408224581 / 14014097488 / 14016712196: a post-sync write after the depth state */
	emit(b, GFX_OP_PIPE_CONTROL(6));
	emit(b, PIPE_CONTROL_QW_WRITE);
	emit(b, (uint32_t)scratch_va);
	emit(b, (uint32_t)(scratch_va >> 32));
	emit(b, 0U);
	emit(b, 0U);
	return 0;
}

static int
gfx_build_batch(struct gfx_batch *b, uint64_t state_va, const struct gfx_draw_state *state,
	const struct gfx_image *target, const struct gfx_image *depth, uint32_t mocs,
	uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance)
{
	uint32_t index;
	int error;

	/* A stalling flush precedes the pipeline select and the base addresses. */
	emit_pc(b, PIPE_CONTROL_CS_STALL | PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH | PIPE_CONTROL_DEPTH_CACHE_FLUSH |
		PIPE_CONTROL_DC_FLUSH_ENABLE | PIPE_CONTROL_FLUSH_ENABLE);
	emit(b, GEN12_PIPELINE_SELECT_DWORD(GEN12_PIPELINE_SELECT_3D));
	emit_sba(b, state_va, mocs);
	emit_pc(b, PIPE_CONTROL_CS_STALL | PIPE_CONTROL_STATE_CACHE_INVALIDATE | PIPE_CONTROL_CONST_CACHE_INVALIDATE |
		PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE | PIPE_CONTROL_INSTRUCTION_CACHE_INVALIDATE);

	/* The once-per-context state anv programs before its first draw (selftest.c). */
	emit_zero(b, GEN12_CMD_3DSTATE_WM_HZ_OP, GEN12_3DSTATE_WM_HZ_OP_DWORDS);
	emit_zero(b, GEN12_CMD_3DSTATE_AA_LINE_PARAMETERS, GEN12_3DSTATE_AA_LINE_PARAMETERS_DWORDS);
	emit_zero(b, GEN12_CMD_3DSTATE_WM_CHROMAKEY, GEN12_3DSTATE_WM_CHROMAKEY_DWORDS);
	emit_zero(b, GEN12_CMD_3DSTATE_POLY_STIPPLE_OFFSET, GEN12_3DSTATE_POLY_STIPPLE_OFFSET_DWORDS);
	emit_zero(b, GEN12_CMD_3DSTATE_LINE_STIPPLE, GEN12_3DSTATE_LINE_STIPPLE_DWORDS);
	emit(b, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_SAMPLE_PATTERN, GEN12_3DSTATE_SAMPLE_PATTERN_DWORDS));
	for (index = 1U; index < GEN12_3DSTATE_SAMPLE_PATTERN_DWORDS; index++)
		emit(b, index == 8U ? GEN12_SAMPLE_PATTERN_1X_CENTRE : 0U);
	emit_zero(b, GEN12_CMD_3DSTATE_DEPTH_BOUNDS, GEN12_3DSTATE_DEPTH_BOUNDS_DWORDS);
	emit_zero(b, GEN12_CMD_3DSTATE_BINDING_TABLE_POINTERS_VS, GEN12_3DSTATE_POINTERS_DWORDS);
	emit_zero(b, GEN12_CMD_3DSTATE_BINDING_TABLE_POINTERS_HS, GEN12_3DSTATE_POINTERS_DWORDS);
	emit_zero(b, GEN12_CMD_3DSTATE_BINDING_TABLE_POINTERS_DS, GEN12_3DSTATE_POINTERS_DWORDS);
	emit_zero(b, GEN12_CMD_3DSTATE_BINDING_TABLE_POINTERS_GS, GEN12_3DSTATE_POINTERS_DWORDS);

	error = emit_vertex_input(b, state, mocs);
	if (error != 0)
		return error;
	emit_urb(b);
	emit_constants(b, state_va + GFX_PUSH_BUFFER, mocs);

	emit_pointer(b, GEN12_CMD_3DSTATE_CC_STATE_POINTERS, GFX_DYN_COLOR_CALC | 1U);
	emit_pointer(b, GEN12_CMD_3DSTATE_BLEND_STATE_POINTERS, GFX_DYN_BLEND | 1U);
	emit_pointer(b, GEN12_CMD_3DSTATE_VIEWPORT_STATE_POINTERS_CC, GFX_DYN_CC_VIEWPORT);
	emit_pointer(b, GFX_CMD_3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP, GFX_DYN_SF_CLIP_VIEWPORT);
	emit_pointer(b, GFX_CMD_3DSTATE_SCISSOR_STATE_POINTERS, GFX_DYN_SCISSOR);
	emit_pointer(b, GEN12_CMD_3DSTATE_CPS_POINTERS, GFX_DYN_CPS);
	emit(b, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_BINDING_TABLE_POOL_ALLOC, GEN12_3DSTATE_BINDING_TABLE_POOL_ALLOC_DWORDS));
	emit(b, mocs);
	emit(b, 0U);
	emit(b, 0U);

	emit_zero(b, GEN12_CMD_3DSTATE_MULTISAMPLE, GEN12_3DSTATE_MULTISAMPLE_DWORDS);
	emit(b, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_SAMPLE_MASK, GEN12_3DSTATE_SAMPLE_MASK_DWORDS));
	emit(b, 1U);

	/* the geometry stages: the vertex shader, and nothing else */
	emit_words(b, vkref_3dstate_vs, GEN12_3DSTATE_VS_DWORDS);
	emit_zero(b, GEN12_CMD_3DSTATE_HS, GEN12_3DSTATE_HS_DWORDS);
	emit_zero(b, GEN12_CMD_3DSTATE_TE, GEN12_3DSTATE_TE_DWORDS);
	emit_zero(b, GEN12_CMD_3DSTATE_DS, GEN12_3DSTATE_DS_DWORDS);
	emit_zero(b, GEN12_CMD_3DSTATE_STREAMOUT, GEN12_3DSTATE_STREAMOUT_DWORDS);
	emit_zero(b, GEN12_CMD_3DSTATE_GS, GEN12_3DSTATE_GS_DWORDS);
	emit_zero(b, GEN12_CMD_3DSTATE_PRIMITIVE_REPLICATION, GEN12_3DSTATE_PRIMITIVE_REPLICATION_DWORDS);

	emit_raster(b, state->pipeline);
	emit_words(b, vkref_3dstate_sbe, sizeof(vkref_3dstate_sbe) / 4U);
	emit_words(b, vkref_3dstate_sbe_swiz, sizeof(vkref_3dstate_sbe_swiz) / 4U);
	emit_words(b, vkref_3dstate_wm, sizeof(vkref_3dstate_wm) / 4U);
	emit_words(b, vkref_3dstate_ps, sizeof(vkref_3dstate_ps) / 4U);
	emit_words(b, vkref_3dstate_ps_extra, sizeof(vkref_3dstate_ps_extra) / 4U);
	emit(b, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_PS_BLEND, GEN12_3DSTATE_PS_BLEND_DWORDS));
	emit(b, 1U << 30);					/* has a writeable render target */

	error = emit_depth(b, state, depth, state_va + GFX_SCRATCH, mocs);
	if (error != 0)
		return error;

	emit_pointer(b, GFX_CMD_3DSTATE_SAMPLER_STATE_POINTERS_PS, GFX_DYN_SAMPLER);
	emit_pointer(b, GEN12_CMD_3DSTATE_BINDING_TABLE_POINTERS_PS, GFX_BINDING_TABLE);

	emit(b, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_DRAWING_RECTANGLE, GEN12_3DSTATE_DRAWING_RECTANGLE_DWORDS));
	emit(b, 0U);
	emit(b, (target->width - 1U) | ((target->height - 1U) << 16));
	emit(b, 0U);

	/* The pixel pipeline is synced before the primitive (selftest.c). */
	emit_pc(b, PIPE_CONTROL_CS_STALL | PIPE_CONTROL_STALL_AT_SCOREBOARD | PIPE_CONTROL_DEPTH_STALL_ENABLE);

	emit(b, GEN12_CMD_HEADER(GEN12_CMD_3DPRIMITIVE, GEN12_3DPRIMITIVE_DWORDS));
	emit(b, GEN12_3DPRIM_TRILIST);
	emit(b, vertex_count);
	emit(b, first_vertex);
	emit(b, instance_count);
	emit(b, first_instance);
	emit(b, 0U);

	emit_pc(b, PIPE_CONTROL_CS_STALL | PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH | PIPE_CONTROL_DEPTH_CACHE_FLUSH |
		PIPE_CONTROL_DC_FLUSH_ENABLE | PIPE_CONTROL_FLUSH_ENABLE);
	emit(b, MI_BATCH_BUFFER_END);
	emit(b, MI_NOOP);
	return b->overflow != 0 ? ENOSPC : 0;
}

/* ---------------- what the draw left in the target (diagnostic) ---------------- */

static void
gfx_census(const struct gfx_image *target, unsigned draw)
{
	static const char shades[] = " .:-=+*#%@";
	const uint32_t *pixels;
	uint32_t first, x, y, column, row, differ;
	char line[81];

	pixels = (const uint32_t *)(const void *)i915_vk_gfx_memory_cpu(target->memory, target->offset, target->bytes);
	if (pixels == NULL)
		return;
	first = pixels[0];
	differ = 0U;
	for (y = 0U; y < target->height; y++)
		for (x = 0U; x < target->width; x++)
			if (pixels[y * (target->pitch / 4U) + x] != first)
				differ++;
	kern_logf("i915: vk: draw %u: %u of %u pixels differ from pixel (0,0) = %08x\n", draw, differ,
		target->width * target->height, first);
	if (draw > 1U)
		return;
#if I915_VK_GFX_DUMP
	/*
	 * Diagnostic build: the first draw's target as hex RGB, 32 pixels to a line, so that the host can
	 * rebuild the image and hand it to the independent oracle (plan/ws014/tests/vkdemo_oracle.py).
	 */
	for (y = 0U; y < target->height; y++) {
		for (x = 0U; x + 32U <= target->width; x += 32U) {
			static const char hex[] = "0123456789abcdef";
			char text[32U * 6U + 1U];
			uint32_t i, c;

			for (i = 0U; i < 32U; i++) {
				uint32_t p = pixels[y * (target->pitch / 4U) + x + i];

				/* memory order R G B A (or B G R A): the dump is R G B */
				if (target->format == VK_FORMAT_B8G8R8A8_UNORM)
					p = ((p >> 16) & 0xffU) | (p & 0xff00U) | ((p & 0xffU) << 16);
				for (c = 0U; c < 3U; c++) {
					text[i * 6U + c * 2U] = hex[(p >> (c * 8U + 4U)) & 0xfU];
					text[i * 6U + c * 2U + 1U] = hex[(p >> (c * 8U)) & 0xfU];
				}
			}
			text[32U * 6U] = 0;
			kern_logf("vkdump %u %u %s\n", y, x, text);
		}
	}
#endif
	/* the first draw as 64 x 20 cells, each the brightness of its centre pixel */
	for (row = 0U; row < 20U; row++) {
		for (column = 0U; column < 64U; column++) {
			uint32_t p = pixels[((row * 2U + 1U) * target->height / 40U) * (target->pitch / 4U) +
				(column * 2U + 1U) * target->width / 128U];
			uint32_t luma = ((p & 0xffU) + ((p >> 8) & 0xffU) + ((p >> 16) & 0xffU)) / 3U;

			line[column] = p == first ? ' ' : shades[luma * 9U / 255U];
		}
		line[64] = '\0';
		kern_logf("i915: vk: |%s|\n", line);
	}
}

/* ---------------- the entry ---------------- */

int
i915_vk_gfx_draw(struct i915_vk_session *session, const struct gfx_draw_state *state,
	uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance)
{
	struct gfx_session *gs;
	struct gfx_batch batch;
	const struct gfx_image *target;
	const struct gfx_image *depth;
	uint32_t mocs;
	int error;

	if (state->pipeline == NULL || state->pipeline->kernels_ready == 0 || state->pass == NULL ||
	    state->framebuffer == NULL || state->pass->color_attachment >= state->framebuffer->view_count ||
	    state->framebuffer->views[state->pass->color_attachment] == NULL) {
		kern_logf("i915: vk: draw refused: no pipeline, render pass or colour attachment is bound\n");
		return EINVAL;
	}
	target = state->framebuffer->views[state->pass->color_attachment]->image;
	depth = NULL;
	if (state->pass->depth_attachment < state->framebuffer->view_count &&
	    state->framebuffer->views[state->pass->depth_attachment] != NULL)
		depth = state->framebuffer->views[state->pass->depth_attachment]->image;

	gs = gfx_session(session);
	if (gs == NULL)
		return ENOMEM;
	mocs = GEN12_MOCS(I915_MOCS_UNCACHED_INDEX);

	error = gfx_write_state((uint8_t *)kern_pmem_to_kernel(gs->state->run.paddr), gs->state->va, state, target, mocs);
	if (error != 0)
		return error;
	batch.cmds = (uint32_t *)kern_pmem_to_kernel(gs->batch->run.paddr);
	batch.count = 0U;
	batch.capacity = GFX_BATCH_BYTES / 4U;
	batch.overflow = 0;
	error = gfx_build_batch(&batch, gs->state->va, state, target, depth, mocs,
		vertex_count, instance_count, first_vertex, first_instance);
	if (error != 0)
		return error;
	kern_io_write_barrier();

	gs->draws++;
	if (gs->draws == 1U)
		kern_logf("i915: vk: first draw: batch %u dwords at 0x%llx, state at 0x%llx, target %ux%u at 0x%llx, "
			"depth %s, %u vertices\n", batch.count, (unsigned long long)gs->batch->va,
			(unsigned long long)gs->state->va, target->width, target->height,
			(unsigned long long)i915_vk_gfx_memory_va(target->memory, target->offset),
			depth != NULL ? "yes" : "no", vertex_count);

	error = parity_shim_run_sync(session->vk->i915, &session->gpu->contexts[I915_ENGINE_RCS0], gs->batch->va);
	if (error != 0) {
		kern_logf("i915: vk: draw %u failed on the GPU: error %d\n", gs->draws, error);
		return error;
	}
	if (gs->draws <= 3U)
		gfx_census(target, gs->draws);
	return 0;
}
