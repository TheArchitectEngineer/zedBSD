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
 * THE KERNELS (E-128).  The pipeline's SPIR-V goes through the executor's own compiler (spirv.c ->
 * compile.c -> eu.c) and the words of 3DSTATE_VS / PS / PS_EXTRA / WM / SBE / SBE_SWIZ are packed
 * here from what that compiler reports (compile.h).  -DI915_VK_REFERENCE_KERNELS=1 builds the
 * comparison path instead: the kernels and packet words Mesa's compiler produced from the same two
 * SPIR-V modules (tools/refvk.c, vkref-generated.inc), recognised by content; everything else in the
 * draw is identical, so a difference between the two builds is a difference between the compilers.
 */

#include "gfx.h"
#include "spirv.h"
#include "compile.h"

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

#ifndef I915_VK_REFERENCE_KERNELS
#define I915_VK_REFERENCE_KERNELS 0
#endif

#ifndef I915_VK_GFX_DUMP
#define I915_VK_GFX_DUMP 0
#endif

/* genxml gen120: commands 3dstate-gen12.inc does not carry yet */
#define GFX_CMD_3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP	0x7821U
#define GFX_CMD_3DSTATE_SCISSOR_STATE_POINTERS		0x780FU
#define GFX_CMD_3DSTATE_SAMPLER_STATE_POINTERS_PS	0x782FU
#define GFX_CMD_3DSTATE_SBE_SWIZ			0x7851U
#define GFX_3DSTATE_SBE_SWIZ_DWORDS			11U
#define GFX_MAX_VS_THREADS			546U	/* intel_device_info (ADL-P GT2): max_vs_threads */

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
#define GFX_INSTRUCTION_HEAP		0x4000U		/* instruction base */
#define GFX_VS_KERNEL			0x0000U		/*   offsets from the instruction base (= VKREF_*_KERNEL_OFFSET) */
#define GFX_PS_KERNEL			0x1000U
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

/* The two kernels of a draw and what has to be programmed around them, whichever compiler made them. */
struct gfx_kernels {
	const uint32_t *vs_code;
	uint32_t vs_bytes;
	const uint32_t *ps_code;
	uint32_t ps_bytes;
	uint32_t vs_grf_start;
	uint32_t vs_push_regs;
	uint32_t vs_input_count;
	uint32_t vs_inputs[GFX_MAX_VERTEX_ATTRIBUTES];	/* attribute locations in payload order */
	uint32_t varyings;				/* VUE slots after the position = fragment inputs */
	uint32_t ps_grf_start;
	uint32_t ps_samplers;
};

_Static_assert(VKREF_VS_KERNEL_OFFSET == GFX_VS_KERNEL && VKREF_PS_KERNEL_OFFSET == GFX_PS_KERNEL,
	"the reference packets name the kernels at these offsets");

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

static int
gfx_compile_stage(const struct gfx_shader *shader, enum i915_vk_stage stage, struct i915_vk_shader_binary **out)
{
	struct i915_vk_shader_ir *ir;
	struct i915_vk_spirv_diag diag;
	int error;

	memset(&diag, 0, sizeof(diag));
	error = i915_vk_spirv_parse_diag(shader->words, shader->word_count, stage, &ir, &diag);
	if (error != 0) {
		kern_logf("i915: vk: %s shader refused by the SPIR-V parser: error %d (%s; opcode %u at word %u)\n",
			stage == I915_VK_STAGE_VERTEX ? "vertex" : "fragment", error,
			diag.reason != NULL ? diag.reason : "?", diag.opcode, diag.word_offset);
		return error;
	}
	error = i915_vk_compile(NULL, ir, out);
	i915_vk_spirv_free(ir);
	if (error != 0)
		kern_logf("i915: vk: %s shader refused by the compiler: error %d\n",
			stage == I915_VK_STAGE_VERTEX ? "vertex" : "fragment", error);
	return error;
}

int
i915_vk_gfx_pipeline_prepare(struct i915_vk_session *session, struct gfx_pipeline *pipeline)
{
	int error;

	(void)session;
	if (pipeline->vertex == NULL || pipeline->fragment == NULL)
		return EINVAL;

	if (I915_VK_REFERENCE_KERNELS) {
		/* XXX: comparison build -- kernels by content, not from the executor's compiler. */
		if (pipeline->vertex->word_count != VKREF_VS_SPIRV_WORDS ||
		    gfx_fnv1a(pipeline->vertex->words, pipeline->vertex->word_count) != VKREF_VS_SPIRV_FNV1A ||
		    pipeline->fragment->word_count != VKREF_FS_SPIRV_WORDS ||
		    gfx_fnv1a(pipeline->fragment->words, pipeline->fragment->word_count) != VKREF_FS_SPIRV_FNV1A) {
			kern_logf("i915: vk: XXX reference-kernel build: the pipeline's SPIR-V (vs %u words, fs %u words) is not "
				"the pair the reference kernels were generated from\n",
				pipeline->vertex->word_count, pipeline->fragment->word_count);
			return ENOTSUP;
		}
		kern_logf("i915: vk: XXX reference-kernel build: Mesa-generated kernels (vs %u bytes, ps %u bytes)\n",
			VKREF_VS_BYTES, VKREF_PS_BYTES);
		pipeline->kernels_ready = 1;
		return 0;
	}

	error = gfx_compile_stage(pipeline->vertex, I915_VK_STAGE_VERTEX, &pipeline->vs_binary);
	if (error == 0)
		error = gfx_compile_stage(pipeline->fragment, I915_VK_STAGE_FRAGMENT, &pipeline->fs_binary);
	if (error == 0 && (pipeline->vs_binary->code_bytes > GFX_PS_KERNEL - GFX_VS_KERNEL ||
	    pipeline->fs_binary->code_bytes > GFX_INSTRUCTION_BYTES - GFX_PS_KERNEL ||
	    pipeline->vs_binary->varying_count != pipeline->fs_binary->input_count ||
	    pipeline->fs_binary->push_regs != 0U || pipeline->vs_binary->push_regs * 32U > GFX_PUSH_BYTES)) {
		/* XXX: kernels of at most 4 KiB / 12 KiB, the stages' interfaces equal, push constants in the vertex stage only */
		kern_logf("i915: vk: XXX unimplemented path: vs %u bytes / %u varyings / %u push registers, "
			"fs %u bytes / %u inputs / %u push registers\n",
			pipeline->vs_binary->code_bytes, pipeline->vs_binary->varying_count, pipeline->vs_binary->push_regs,
			pipeline->fs_binary->code_bytes, pipeline->fs_binary->input_count, pipeline->fs_binary->push_regs);
		error = ENOTSUP;
	}
	if (error != 0) {
		i915_vk_gfx_pipeline_release(pipeline);
		return error;
	}
	kern_logf("i915: vk: pipeline compiled by the executor: vs %u bytes (%u attributes, %u push registers, %u varyings), "
		"fs %u bytes (%u inputs, %u sampled images)\n",
		pipeline->vs_binary->code_bytes, pipeline->vs_binary->input_count, pipeline->vs_binary->push_regs,
		pipeline->vs_binary->varying_count, pipeline->fs_binary->code_bytes, pipeline->fs_binary->input_count,
		pipeline->fs_binary->sampler_count);
	pipeline->kernels_ready = 1;
	return 0;
}

void
i915_vk_gfx_pipeline_release(struct gfx_pipeline *pipeline)
{
	i915_vk_shader_binary_free(pipeline->vs_binary);
	i915_vk_shader_binary_free(pipeline->fs_binary);
	pipeline->vs_binary = NULL;
	pipeline->fs_binary = NULL;
	pipeline->kernels_ready = 0;
}

static void
gfx_kernels(const struct gfx_pipeline *pipeline, struct gfx_kernels *k)
{
	uint32_t index;

	memset(k, 0, sizeof(*k));
	if (I915_VK_REFERENCE_KERNELS) {
		k->vs_code = vkref_vs_kernel;
		k->vs_bytes = VKREF_VS_BYTES;
		k->ps_code = vkref_ps_kernel;
		k->ps_bytes = VKREF_PS_BYTES;
		k->vs_push_regs = VKREF_VS_PUSH_REGS;
		/* refvk: inputs_read, location n = bit VERT_ATTRIB_GENERIC0 + n, ascending */
		for (index = 0U; index < GFX_MAX_VERTEX_ATTRIBUTES; index++)
			if (((VKREF_VS_INPUTS_READ >> (VKREF_VERT_ATTRIB_GENERIC0 + index)) & 1U) != 0U)
				k->vs_inputs[k->vs_input_count++] = index;
		k->varyings = 1U;
		k->ps_samplers = 1U;
		return;
	}
	k->vs_code = pipeline->vs_binary->code;
	k->vs_bytes = pipeline->vs_binary->code_bytes;
	k->ps_code = pipeline->fs_binary->code;
	k->ps_bytes = pipeline->fs_binary->code_bytes;
	k->vs_grf_start = pipeline->vs_binary->dispatch_grf_start;
	k->vs_push_regs = pipeline->vs_binary->push_regs;
	k->vs_input_count = pipeline->vs_binary->input_count;
	for (index = 0U; index < k->vs_input_count && index < GFX_MAX_VERTEX_ATTRIBUTES; index++)
		k->vs_inputs[index] = pipeline->vs_binary->input_locations[index];
	k->varyings = pipeline->vs_binary->varying_count;
	k->ps_grf_start = pipeline->fs_binary->dispatch_grf_start;
	k->ps_samplers = pipeline->fs_binary->sampler_count;
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
	const struct gfx_kernels *k, const struct gfx_image *target, uint32_t mocs)
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
	if (k->ps_samplers == 0U)
		goto no_texture;
	if (k->ps_samplers > 1U) {
		kern_logf("i915: vk: XXX unimplemented path: %u sampled images in one fragment shader\n", k->ps_samplers);
		return ENOTSUP;
	}
	/* XXX: the one sampled image is set 0 binding 0 (binding table entry 1, sampler 0) */
	if (state->dset[0] == NULL || state->dset[0]->slots[0].view == NULL || state->dset[0]->slots[0].sampler == NULL) {
		kern_logf("i915: vk: draw refused: set 0 binding 0 has no image view and sampler\n");
		return EINVAL;
	}
	error = gfx_write_rss(&surface[GFX_RSS_TEXTURE / 4U], state->dset[0]->slots[0].view->image, mocs);
	if (error != 0)
		return error;
	gfx_write_sampler(&dynamic[GFX_DYN_SAMPLER / 4U], state->dset[0]->slots[0].sampler);
no_texture:

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
	memcpy(page + GFX_PUSH_BUFFER, state->push, k->vs_push_regs * 32U <= GFX_PUSH_BYTES ? k->vs_push_regs * 32U : GFX_PUSH_BYTES);

	/* instruction heap: a thread that starts anywhere it should not retires at once */
	for (at = 0U; at + sizeof(gfx_eot_only) <= GFX_INSTRUCTION_BYTES; at += sizeof(gfx_eot_only))
		memcpy(page + GFX_INSTRUCTION_HEAP + at, gfx_eot_only, sizeof(gfx_eot_only));
	memcpy(page + GFX_INSTRUCTION_HEAP + GFX_VS_KERNEL, k->vs_code, k->vs_bytes);
	memcpy(page + GFX_INSTRUCTION_HEAP + GFX_PS_KERNEL, k->ps_code, k->ps_bytes);
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
emit_vertex_input(struct gfx_batch *b, const struct gfx_draw_state *state, const struct gfx_kernels *k, uint32_t mocs)
{
	const struct gfx_pipeline *pipeline = state->pipeline;
	uint32_t order[GFX_MAX_VERTEX_ATTRIBUTES];
	uint32_t index, other, count, format, components;

	/* one vertex element to an attribute the kernel reads, in the kernel's payload order */
	count = k->vs_input_count;
	if (count == 0U || pipeline->binding_count == 0U)
		return EINVAL;
	for (index = 0U; index < count; index++) {
		for (other = 0U; other < pipeline->attribute_count; other++)
			if (pipeline->attributes[other].location == k->vs_inputs[index])
				break;
		if (other == pipeline->attribute_count) {
			kern_logf("i915: vk: draw refused: the vertex shader reads location %u and the pipeline has no such attribute\n",
				k->vs_inputs[index]);
			return EINVAL;
		}
		order[index] = other;
	}

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
emit_urb(struct gfx_batch *b, uint32_t entry_size)
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
	emit(b, (4U << 10) | (4U << 21) | (entry_size - 1U));
	emit(b, 3576U | (3576U << 16));
	for (opcode = GEN12_CMD_3DSTATE_URB_ALLOC_HS; opcode <= GEN12_CMD_3DSTATE_URB_ALLOC_GS; opcode++) {
		emit(b, GEN12_CMD_HEADER(opcode, GEN12_3DSTATE_URB_ALLOC_DWORDS));
		emit(b, (5U << 10) | (5U << 21));
		emit(b, 0U);
	}
}

static void
emit_constants(struct gfx_batch *b, uint64_t push_va, uint32_t push_regs, uint32_t mocs)
{
	static const uint32_t opcodes[5] = {
		GEN12_CMD_3DSTATE_CONSTANT_VS, GEN12_CMD_3DSTATE_CONSTANT_HS, GEN12_CMD_3DSTATE_CONSTANT_DS,
		GEN12_CMD_3DSTATE_CONSTANT_GS, GEN12_CMD_3DSTATE_CONSTANT_PS,
	};
	unsigned stage, index;

	for (stage = 0U; stage < 5U; stage++) {
		emit(b, GEN12_CMD_HEADER(opcodes[stage], GEN12_3DSTATE_CONSTANT_DWORDS) | (mocs << 8));
		if (stage == 0U && push_regs != 0U) {
			/* anv: the highest slot first, so that slot 0 is never the only one in use */
			emit(b, 0U);
			emit(b, push_regs << 16);			/* read length of buffer 3 */
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

/*
 * The packets that say what the kernels are (genxml gen120; anv genX_shader.c / genX_gfx_state.c).
 * With the reference kernels' parameters these give the reference packets, 16-wide dispatch aside.
 */
static void
emit_shader_state(struct gfx_batch *b, const struct gfx_kernels *k, int vertex)
{
	uint32_t index;

	if (vertex != 0) {
		emit(b, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_VS, GEN12_3DSTATE_VS_DWORDS));
		emit(b, GFX_VS_KERNEL);				/* kernel start pointer */
		emit(b, 0U);
		emit(b, 0U);					/* IEEE-754, no samplers, no binding table */
		emit(b, 0U);					/* no scratch space */
		emit(b, 0U);
		emit(b, (k->vs_grf_start << 20) | (((k->vs_input_count + 1U) / 2U) << 11));	/* URB read: pairs of attributes from 0 */
		emit(b, ((GFX_MAX_VS_THREADS - 1U) << 22) | (1U << 10) | (1U << 2) | 1U);	/* statistics, SIMD8, enable */
		emit(b, 0U);
		return;
	}

	emit(b, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_SBE, GEN12_3DSTATE_SBE_DWORDS));
	emit(b, (1U << 29) | (1U << 28) | (k->varyings << 22) | (1U << 21) |
		((k->varyings != 0U ? (k->varyings + 1U) / 2U : 1U) << 11) | (1U << 5));	/* read from VUE slot 2 */
	emit(b, 0U);
	emit(b, 0U);
	emit(b, 0xffffffffU);					/* every attribute: all four components */
	emit(b, 0xffffffffU);
	emit(b, GEN12_CMD_HEADER(GFX_CMD_3DSTATE_SBE_SWIZ, GFX_3DSTATE_SBE_SWIZ_DWORDS));
	for (index = 0U; index < 16U; index += 2U)		/* fragment input n = the n-th slot read */
		emit(b, (index < k->varyings ? index : 0U) | ((index + 1U < k->varyings ? index + 1U : 0U) << 16));
	emit(b, 0U);
	emit(b, 0U);

	emit(b, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_WM, GEN12_3DSTATE_WM_DWORDS));
	emit(b, (1U << 31) | (1U << 11) | (1U << 6));		/* statistics, perspective pixel barycentrics, line AA 1.0 */

	emit(b, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_PS, GEN12_3DSTATE_PS_DWORDS));
	emit(b, GFX_PS_KERNEL);					/* kernel 0: the SIMD8 one */
	emit(b, 0U);
	emit(b, (1U << 30) | ((k->ps_samplers != 0U ? 1U : 0U) << 27) | ((1U + k->ps_samplers) << 18));	/* vector mask */
	emit(b, 0U);
	emit(b, 0U);
	emit(b, ((GEN12_MAX_THREADS_PER_PSD - 1U) << 23) | 1U);	/* 8-pixel dispatch only */
	emit(b, k->ps_grf_start << 16);
	emit(b, 0U);
	emit(b, 0U);
	emit(b, 0U);
	emit(b, 0U);

	emit(b, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_PS_EXTRA, GEN12_3DSTATE_PS_EXTRA_DWORDS));
	emit(b, (1U << 31) | ((k->varyings != 0U ? 1U : 0U) << 8));	/* valid, attributes */
}

static int
gfx_build_batch(struct gfx_batch *b, uint64_t state_va, const struct gfx_draw_state *state, const struct gfx_kernels *k,
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

	error = emit_vertex_input(b, state, k, mocs);
	if (error != 0)
		return error;
	emit_urb(b, I915_VK_REFERENCE_KERNELS ? VKREF_VS_URB_ENTRY_SIZE : ((2U + k->varyings) * 16U + 63U) / 64U);
	emit_constants(b, state_va + GFX_PUSH_BUFFER, k->vs_push_regs, mocs);

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
	if (I915_VK_REFERENCE_KERNELS)
		emit_words(b, vkref_3dstate_vs, GEN12_3DSTATE_VS_DWORDS);
	else
		emit_shader_state(b, k, 1);
	emit_zero(b, GEN12_CMD_3DSTATE_HS, GEN12_3DSTATE_HS_DWORDS);
	emit_zero(b, GEN12_CMD_3DSTATE_TE, GEN12_3DSTATE_TE_DWORDS);
	emit_zero(b, GEN12_CMD_3DSTATE_DS, GEN12_3DSTATE_DS_DWORDS);
	emit_zero(b, GEN12_CMD_3DSTATE_STREAMOUT, GEN12_3DSTATE_STREAMOUT_DWORDS);
	emit_zero(b, GEN12_CMD_3DSTATE_GS, GEN12_3DSTATE_GS_DWORDS);
	emit_zero(b, GEN12_CMD_3DSTATE_PRIMITIVE_REPLICATION, GEN12_3DSTATE_PRIMITIVE_REPLICATION_DWORDS);

	emit_raster(b, state->pipeline);
	if (I915_VK_REFERENCE_KERNELS) {
		emit_words(b, vkref_3dstate_sbe, sizeof(vkref_3dstate_sbe) / 4U);
		emit_words(b, vkref_3dstate_sbe_swiz, sizeof(vkref_3dstate_sbe_swiz) / 4U);
		emit_words(b, vkref_3dstate_wm, sizeof(vkref_3dstate_wm) / 4U);
		emit_words(b, vkref_3dstate_ps, sizeof(vkref_3dstate_ps) / 4U);
		emit_words(b, vkref_3dstate_ps_extra, sizeof(vkref_3dstate_ps_extra) / 4U);
	} else {
		emit_shader_state(b, k, 0);
	}
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
	struct gfx_kernels kernels;
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

	gfx_kernels(state->pipeline, &kernels);
	error = gfx_write_state((uint8_t *)kern_pmem_to_kernel(gs->state->run.paddr), gs->state->va, state, &kernels, target, mocs);
	if (error != 0)
		return error;
	batch.cmds = (uint32_t *)kern_pmem_to_kernel(gs->batch->run.paddr);
	batch.count = 0U;
	batch.capacity = GFX_BATCH_BYTES / 4U;
	batch.overflow = 0;
	error = gfx_build_batch(&batch, gs->state->va, state, &kernels, target, depth, mocs,
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

/* ---------------- E-130: rectangles on the GPU (clear, copy, blit) ---------------- */

/*
 * Every transfer the executor performs -- attachment and image clears, buffer <-> image copies, image copies and
 * blits, and the display's scaled copy into the scanout buffer -- is one RECTLIST drawn by the 3D pipeline, the way
 * Mesa's blorp does it on the render engine: the vertex shader is disabled, the vertex fetcher writes the VUE itself
 * ([header][position][one attribute]), and one of two fragment kernels runs.  Both kernels come from the executor's
 * own compiler, built from IR here (no SPIR-V):
 *   fill:  colour = the attribute              (a clear: the attribute is the clear value at every vertex)
 *   copy:  colour = texture(source, attribute)  (the attribute is the source coordinate, nearest or linear)
 * A depth clear writes the depth value's bits through an R32_FLOAT view of the depth memory: the value is the same
 * everywhere, so the view does not have to know the depth buffer's tiling.
 */
#define GFX_RECT_VERTICES		0x2800U		/* in the state object: three vertices of 12 floats */
#define GFX_RECT_VERTEX_BYTES		48U

static struct i915_vk_shader_binary *gfx_rect_fill_kernel;
static struct i915_vk_shader_binary *gfx_rect_copy_kernel;

static int
gfx_rect_compile(int copy, struct i915_vk_shader_binary **out)
{
	struct i915_vk_inst insts[8];
	struct i915_vk_uniform uniform;
	struct i915_vk_shader_ir ir;
	uint32_t index, count, first;

	memset(insts, 0, sizeof(insts));
	memset(&ir, 0, sizeof(ir));
	memset(&uniform, 0, sizeof(uniform));
	count = 0U;
	if (copy) {
		/* v0, v1 = the coordinate; v2..v5 = texture(set 0, binding 0) at it */
		for (index = 0U; index < 2U; index++) {
			insts[count].op = I915_VK_IR_LOAD_INPUT;
			insts[count].dst = index;
			insts[count].component = index;
			count++;
		}
		insts[count].op = I915_VK_IR_SAMPLE;
		insts[count].dst = 2U;
		insts[count].src[0] = 0U;
		insts[count].src[1] = 1U;
		count++;
		uniform.kind = 1U;
		ir.uniforms = &uniform;
		ir.uniform_count = 1U;
		first = 2U;
	} else {
		for (index = 0U; index < 4U; index++) {
			insts[count].op = I915_VK_IR_LOAD_INPUT;
			insts[count].dst = index;
			insts[count].component = index;
			count++;
		}
		first = 0U;
	}
	for (index = 0U; index < 4U; index++) {
		insts[count].op = I915_VK_IR_STORE_OUTPUT;
		insts[count].src[0] = first + index;
		insts[count].component = index;
		count++;
	}
	ir.stage = I915_VK_STAGE_FRAGMENT;
	ir.instructions = insts;
	ir.instruction_count = count;
	ir.value_count = first + 4U;
	return i915_vk_compile(NULL, &ir, out);
}

/* Compiles the two kernels once; the session's state and batch objects exist afterwards. */
int
i915_vk_gfx_rect_prepare(struct i915_vk_session *session)
{
	int error;

	if (gfx_rect_fill_kernel == NULL) {
		error = gfx_rect_compile(0, &gfx_rect_fill_kernel);
		if (error != 0) {
			kern_logf("i915: vk: the fill kernel does not compile: %d\n", error);
			return error;
		}
	}
	if (gfx_rect_copy_kernel == NULL) {
		error = gfx_rect_compile(1, &gfx_rect_copy_kernel);
		if (error != 0) {
			kern_logf("i915: vk: the copy kernel does not compile: %d\n", error);
			return error;
		}
		kern_logf("i915: vk: transfer kernels compiled by the executor: fill %u bytes, copy %u bytes\n",
			gfx_rect_fill_kernel->code_bytes, gfx_rect_copy_kernel->code_bytes);
	}
	return gfx_session(session) != NULL ? 0 : ENOMEM;
}

/* float bits of a non-negative integer (exact below 2^24) */
static uint32_t
sf_from_u32(uint32_t value)
{
	uint32_t msb;

	if (value == 0U)
		return 0U;
	msb = 31U - (uint32_t)__builtin_clz(value);
	if (msb > 23U)
		value >>= msb - 23U;
	else
		value <<= 23U - msb;
	return ((127U + msb) << 23) | (value & 0x7fffffU);
}

/* float bits of num / den for 0 <= num <= 2^20, 0 < den <= 2^20, rounded to nearest (no FP register) */
static uint32_t
sf_ratio(uint32_t num, uint32_t den)
{
	uint64_t q;
	uint32_t msb, mantissa;
	int exponent;

	if (num == 0U || den == 0U)
		return 0U;
	q = ((uint64_t)num << 40) / den;		/* num / den * 2^40, at least 2^20 */
	msb = 63U - (uint32_t)__builtin_clzll(q);
	exponent = (int)msb - 40;
	mantissa = (uint32_t)((((msb >= 24U) ? (q >> (msb - 24U)) : (q << (24U - msb))) + 1U) >> 1);	/* 24 bits, rounded */
	if (mantissa >> 24) {
		mantissa >>= 1;
		exponent++;
	}
	return ((uint32_t)(127 + exponent) << 23) | (mantissa & 0x7fffffU);
}

/* RENDER_SURFACE_STATE of a linear 2D surface (see gfx_write_rss) */
static int
gfx_write_surface(uint32_t *rss, const struct gfx_surface *s, uint32_t mocs)
{
	uint32_t format;

	if (s->va == 0U || s->width == 0U || s->height == 0U || s->width > 16384U || s->height > 16384U ||
	    s->pitch < s->width * 4U || gfx_surface_format(s->format, &format) != 0)
		return EINVAL;
	memset(rss, 0, GEN12_RENDER_SURFACE_STATE_DWORDS * 4U);
	rss[0] = (GEN12_SURFTYPE_2D << 29) | (format << 18) | (GEN12_SURFACE_ALIGN_4 << 16) |
		(GEN12_SURFACE_ALIGN_4 << 14) | (GEN12_TILEMODE_LINEAR << 12);
	rss[1] = (s->format == VK_FORMAT_R32_SFLOAT ? 0U : (1U << 31)) | (mocs << 24) | (((s->height + 3U) & ~3U) / 4U);
	rss[2] = (s->width - 1U) | ((s->height - 1U) << 16);
	rss[3] = s->pitch - 1U;
	rss[5] = 0x00000100U;
	rss[7] = (4U << 25) | (5U << 22) | (6U << 19) | (7U << 16);
	rss[8] = (uint32_t)s->va;
	rss[9] = (uint32_t)(s->va >> 32);
	return 0;
}

/* Writes the state and the batch of one rectangle; the caller runs the batch. */
int
i915_vk_gfx_rect_build(struct i915_vk_session *session, const struct gfx_surface *dst, const struct gfx_rect *dst_rect,
	const struct gfx_surface *src, const struct gfx_rect *src_rect, const uint32_t clear[4], int linear,
	uint64_t *batch_va)
{
	const struct i915_vk_shader_binary *kernel;
	struct gfx_session *gs;
	struct gfx_kernels k;
	struct gfx_batch batch;
	struct gfx_sampler sampler;
	uint8_t *page;
	uint32_t *surface, *dynamic, *v;
	uint32_t mocs, index, x0, y0, x1, y1, attr[3][4];
	unsigned at;
	int error;

	error = i915_vk_gfx_rect_prepare(session);
	if (error != 0)
		return error;
	gs = session->gfx;
	mocs = GEN12_MOCS(I915_MOCS_UNCACHED_INDEX);
	kernel = src != NULL ? gfx_rect_copy_kernel : gfx_rect_fill_kernel;
	if (dst_rect->x < 0 || dst_rect->y < 0 || dst_rect->w == 0U || dst_rect->h == 0U ||
	    (uint32_t)dst_rect->x + dst_rect->w > dst->width || (uint32_t)dst_rect->y + dst_rect->h > dst->height)
		return EINVAL;
	if (src != NULL && (src_rect->x < 0 || src_rect->y < 0 || src_rect->w == 0U || src_rect->h == 0U ||
	    (uint32_t)src_rect->x + src_rect->w > src->width || (uint32_t)src_rect->y + src_rect->h > src->height))
		return EINVAL;

	/* ---- state: binding table [0] destination [1] source, the sampler, blend / colour calc, vertices, kernel ---- */
	page = (uint8_t *)kern_pmem_to_kernel(gs->state->run.paddr);
	memset(page, 0, GFX_INSTRUCTION_HEAP);
	surface = (uint32_t *)(void *)(page + GFX_SURFACE_HEAP);
	dynamic = (uint32_t *)(void *)(page + GFX_DYNAMIC_HEAP);
	surface[GFX_BINDING_TABLE / 4U] = GFX_RSS_TARGET;
	surface[GFX_BINDING_TABLE / 4U + 1U] = GFX_RSS_TEXTURE;
	error = gfx_write_surface(&surface[GFX_RSS_TARGET / 4U], dst, mocs);
	if (error == 0 && src != NULL)
		error = gfx_write_surface(&surface[GFX_RSS_TEXTURE / 4U], src, mocs);
	if (error != 0)
		return error;
	memset(&sampler, 0, sizeof(sampler));
	sampler.mag_filter = sampler.min_filter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	sampler.address_u = sampler.address_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	gfx_write_sampler(&dynamic[GFX_DYN_SAMPLER / 4U], &sampler);
	dynamic[GFX_DYN_BLEND / 4U + 2U] = 1U | (1U << 1) | (GEN12_COLORCLAMP_RTFORMAT << 2);
	dynamic[GFX_DYN_CC_VIEWPORT / 4U + 1U] = SF_ONE;

	/* the three corners of a RECTLIST: (x1, y1), (x0, y1), (x0, y0); the attribute follows its corner */
	x0 = (uint32_t)dst_rect->x;
	y0 = (uint32_t)dst_rect->y;
	x1 = x0 + dst_rect->w;
	y1 = y0 + dst_rect->h;
	for (index = 0U; index < 3U; index++) {
		if (src != NULL) {
			uint32_t sx = (uint32_t)src_rect->x + (index == 0U ? src_rect->w : 0U);
			uint32_t sy = (uint32_t)src_rect->y + (index < 2U ? src_rect->h : 0U);

			attr[index][0] = sf_ratio(sx, src->width);	/* normalised: the proven sampling form */
			attr[index][1] = sf_ratio(sy, src->height);
			attr[index][2] = 0U;
			attr[index][3] = 0U;
		} else {
			memcpy(attr[index], clear, sizeof(attr[index]));
		}
	}
	v = (uint32_t *)(void *)(page + GFX_RECT_VERTICES);
	for (index = 0U; index < 3U; index++) {
		uint32_t *vertex = v + index * (GFX_RECT_VERTEX_BYTES / 4U);

		/* [VUE header: zeros][position x y 0 1][attribute] */
		vertex[4] = sf_from_u32(index == 0U ? x1 : x0);
		vertex[5] = sf_from_u32(index < 2U ? y1 : y0);
		vertex[6] = 0U;
		vertex[7] = SF_ONE;
		memcpy(&vertex[8], attr[index], sizeof(attr[index]));
	}

	for (at = 0U; at + sizeof(gfx_eot_only) <= GFX_INSTRUCTION_BYTES; at += sizeof(gfx_eot_only))
		memcpy(page + GFX_INSTRUCTION_HEAP + at, gfx_eot_only, sizeof(gfx_eot_only));
	memcpy(page + GFX_INSTRUCTION_HEAP + GFX_PS_KERNEL, kernel->code, kernel->code_bytes);

	memset(&k, 0, sizeof(k));
	k.varyings = 1U;
	k.ps_grf_start = kernel->dispatch_grf_start;
	k.ps_samplers = kernel->sampler_count;

	/* ---- the batch (selftest.c's order; the RECTLIST path of the fixture with a fragment input) ---- */
	batch.cmds = (uint32_t *)kern_pmem_to_kernel(gs->batch->run.paddr);
	batch.count = 0U;
	batch.capacity = GFX_BATCH_BYTES / 4U;
	batch.overflow = 0;
	emit_pc(&batch, PIPE_CONTROL_CS_STALL | PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH | PIPE_CONTROL_DEPTH_CACHE_FLUSH |
		PIPE_CONTROL_DC_FLUSH_ENABLE | PIPE_CONTROL_FLUSH_ENABLE);
	emit(&batch, GEN12_PIPELINE_SELECT_DWORD(GEN12_PIPELINE_SELECT_3D));
	emit_sba(&batch, gs->state->va, mocs);
	emit_pc(&batch, PIPE_CONTROL_CS_STALL | PIPE_CONTROL_STATE_CACHE_INVALIDATE | PIPE_CONTROL_CONST_CACHE_INVALIDATE |
		PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE | PIPE_CONTROL_INSTRUCTION_CACHE_INVALIDATE);
	emit_zero(&batch, GEN12_CMD_3DSTATE_WM_HZ_OP, GEN12_3DSTATE_WM_HZ_OP_DWORDS);
	emit_zero(&batch, GEN12_CMD_3DSTATE_AA_LINE_PARAMETERS, GEN12_3DSTATE_AA_LINE_PARAMETERS_DWORDS);
	emit_zero(&batch, GEN12_CMD_3DSTATE_WM_CHROMAKEY, GEN12_3DSTATE_WM_CHROMAKEY_DWORDS);
	emit_zero(&batch, GEN12_CMD_3DSTATE_POLY_STIPPLE_OFFSET, GEN12_3DSTATE_POLY_STIPPLE_OFFSET_DWORDS);
	emit_zero(&batch, GEN12_CMD_3DSTATE_LINE_STIPPLE, GEN12_3DSTATE_LINE_STIPPLE_DWORDS);
	emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_SAMPLE_PATTERN, GEN12_3DSTATE_SAMPLE_PATTERN_DWORDS));
	for (index = 1U; index < GEN12_3DSTATE_SAMPLE_PATTERN_DWORDS; index++)
		emit(&batch, index == 8U ? GEN12_SAMPLE_PATTERN_1X_CENTRE : 0U);
	emit_zero(&batch, GEN12_CMD_3DSTATE_DEPTH_BOUNDS, GEN12_3DSTATE_DEPTH_BOUNDS_DWORDS);
	emit_zero(&batch, GEN12_CMD_3DSTATE_BINDING_TABLE_POINTERS_VS, GEN12_3DSTATE_POINTERS_DWORDS);
	emit_zero(&batch, GEN12_CMD_3DSTATE_BINDING_TABLE_POINTERS_HS, GEN12_3DSTATE_POINTERS_DWORDS);
	emit_zero(&batch, GEN12_CMD_3DSTATE_BINDING_TABLE_POINTERS_DS, GEN12_3DSTATE_POINTERS_DWORDS);
	emit_zero(&batch, GEN12_CMD_3DSTATE_BINDING_TABLE_POINTERS_GS, GEN12_3DSTATE_POINTERS_DWORDS);

	/* one buffer of three vertices; three elements = VUE slots 0 (header), 1 (position), 2 (attribute) */
	emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_VERTEX_BUFFERS, 1U + GEN12_VERTEX_BUFFER_STATE_DWORDS));
	emit(&batch, GEN12_VERTEX_BUFFER_L3_BYPASS_DISABLE | (mocs << 16) | (1U << 14) | GFX_RECT_VERTEX_BYTES);
	emit(&batch, (uint32_t)(gs->state->va + GFX_RECT_VERTICES));
	emit(&batch, (uint32_t)((gs->state->va + GFX_RECT_VERTICES) >> 32));
	emit(&batch, 3U * GFX_RECT_VERTEX_BYTES);
	emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_VERTEX_ELEMENTS, 1U + 3U * GEN12_VERTEX_ELEMENT_STATE_DWORDS));
	for (index = 0U; index < 3U; index++) {
		emit(&batch, (1U << 25) | (GEN12_FORMAT_R32G32B32A32_FLOAT << 16) | (index * 16U));
		emit(&batch, (GEN12_VFCOMP_STORE_SRC << 28) | (GEN12_VFCOMP_STORE_SRC << 24) |
			(GEN12_VFCOMP_STORE_SRC << 20) | (GEN12_VFCOMP_STORE_SRC << 16));
	}
	emit(&batch, (GEN12_CMD_3DSTATE_VF_STATISTICS << 16) | 1U);
	emit_zero(&batch, GEN12_CMD_3DSTATE_VF, GEN12_3DSTATE_VF_DWORDS);
	emit_zero(&batch, GEN12_CMD_3DSTATE_VF_SGVS, GEN12_3DSTATE_VF_SGVS_DWORDS);
	emit_zero(&batch, GEN12_CMD_3DSTATE_VF_SGVS_2, GEN12_3DSTATE_VF_SGVS_2_DWORDS);
	for (index = 0U; index < 3U; index++) {
		emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_VF_INSTANCING, GEN12_3DSTATE_VF_INSTANCING_DWORDS));
		emit(&batch, index);
		emit(&batch, 0U);
	}
	emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_VF_TOPOLOGY, GEN12_3DSTATE_VF_TOPOLOGY_DWORDS));
	emit(&batch, GEN12_3DPRIM_RECTLIST);
	emit_urb(&batch, 1U);
	emit_constants(&batch, gs->state->va + GFX_PUSH_BUFFER, 0U, mocs);

	emit_pointer(&batch, GEN12_CMD_3DSTATE_CC_STATE_POINTERS, GFX_DYN_COLOR_CALC | 1U);
	emit_pointer(&batch, GEN12_CMD_3DSTATE_BLEND_STATE_POINTERS, GFX_DYN_BLEND | 1U);
	emit_pointer(&batch, GEN12_CMD_3DSTATE_VIEWPORT_STATE_POINTERS_CC, GFX_DYN_CC_VIEWPORT);
	emit_pointer(&batch, GEN12_CMD_3DSTATE_CPS_POINTERS, GFX_DYN_CPS);
	emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_BINDING_TABLE_POOL_ALLOC, GEN12_3DSTATE_BINDING_TABLE_POOL_ALLOC_DWORDS));
	emit(&batch, mocs);
	emit(&batch, 0U);
	emit(&batch, 0U);
	emit_zero(&batch, GEN12_CMD_3DSTATE_MULTISAMPLE, GEN12_3DSTATE_MULTISAMPLE_DWORDS);
	emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_SAMPLE_MASK, GEN12_3DSTATE_SAMPLE_MASK_DWORDS));
	emit(&batch, 1U);

	/* no geometry shading: the vertex fetcher feeds the clipper (selftest.c) */
	emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_VS, GEN12_3DSTATE_VS_DWORDS));
	for (index = 1U; index < GEN12_3DSTATE_VS_DWORDS; index++)
		emit(&batch, index == 7U ? (1U << 10) : 0U);
	emit_zero(&batch, GEN12_CMD_3DSTATE_HS, GEN12_3DSTATE_HS_DWORDS);
	emit_zero(&batch, GEN12_CMD_3DSTATE_TE, GEN12_3DSTATE_TE_DWORDS);
	emit_zero(&batch, GEN12_CMD_3DSTATE_DS, GEN12_3DSTATE_DS_DWORDS);
	emit_zero(&batch, GEN12_CMD_3DSTATE_STREAMOUT, GEN12_3DSTATE_STREAMOUT_DWORDS);
	emit_zero(&batch, GEN12_CMD_3DSTATE_GS, GEN12_3DSTATE_GS_DWORDS);
	emit_zero(&batch, GEN12_CMD_3DSTATE_PRIMITIVE_REPLICATION, GEN12_3DSTATE_PRIMITIVE_REPLICATION_DWORDS);

	/* screen-space rectangle: no clipping, no perspective divide, no viewport transform (selftest.c) */
	emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_CLIP, GEN12_3DSTATE_CLIP_DWORDS));
	emit(&batch, 1U << 10);
	emit(&batch, 1U << 9);
	emit(&batch, 0U);
	emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_SF, GEN12_3DSTATE_SF_DWORDS));
	emit(&batch, 1U << 10);
	emit(&batch, GEN12_URB_DEREF_BLOCK_SIZE_32 << 29);
	emit(&batch, 0U);
	emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_RASTER, GEN12_3DSTATE_RASTER_DWORDS));
	emit(&batch, GEN12_CULLMODE_NONE << 16);
	for (index = 2U; index < GEN12_3DSTATE_RASTER_DWORDS; index++)
		emit(&batch, 0U);

	emit_shader_state(&batch, &k, 0);
	emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_PS_BLEND, GEN12_3DSTATE_PS_BLEND_DWORDS));
	emit(&batch, 1U << 30);

	/* no depth */
	emit_zero(&batch, GEN12_CMD_3DSTATE_WM_DEPTH_STENCIL, GEN12_3DSTATE_WM_DEPTH_STENCIL_DWORDS);
	emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_DEPTH_BUFFER, GEN12_3DSTATE_DEPTH_BUFFER_DWORDS));
	emit(&batch, (GEN12_SURFTYPE_NULL << 29) | (GEN12_DEPTH_FORMAT_D32_FLOAT << 24));
	for (index = 2U; index < GEN12_3DSTATE_DEPTH_BUFFER_DWORDS; index++)
		emit(&batch, 0U);
	emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_STENCIL_BUFFER, GEN12_3DSTATE_STENCIL_BUFFER_DWORDS));
	emit(&batch, GEN12_SURFTYPE_NULL << 29);
	for (index = 2U; index < GEN12_3DSTATE_STENCIL_BUFFER_DWORDS; index++)
		emit(&batch, 0U);
	emit_zero(&batch, GEN12_CMD_3DSTATE_HIER_DEPTH_BUFFER, GEN12_3DSTATE_HIER_DEPTH_BUFFER_DWORDS);
	emit_zero(&batch, GEN12_CMD_3DSTATE_CLEAR_PARAMS, GEN12_3DSTATE_CLEAR_PARAMS_DWORDS);

	emit_pointer(&batch, GFX_CMD_3DSTATE_SAMPLER_STATE_POINTERS_PS, GFX_DYN_SAMPLER);
	emit_pointer(&batch, GEN12_CMD_3DSTATE_BINDING_TABLE_POINTERS_PS, GFX_BINDING_TABLE);
	emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_DRAWING_RECTANGLE, GEN12_3DSTATE_DRAWING_RECTANGLE_DWORDS));
	emit(&batch, 0U);
	emit(&batch, (dst->width - 1U) | ((dst->height - 1U) << 16));
	emit(&batch, 0U);
	emit_pc(&batch, PIPE_CONTROL_CS_STALL | PIPE_CONTROL_STALL_AT_SCOREBOARD | PIPE_CONTROL_DEPTH_STALL_ENABLE);
	emit(&batch, GEN12_CMD_HEADER(GEN12_CMD_3DPRIMITIVE, GEN12_3DPRIMITIVE_DWORDS));
	emit(&batch, GEN12_3DPRIM_RECTLIST);
	emit(&batch, 3U);
	emit(&batch, 0U);
	emit(&batch, 1U);
	emit(&batch, 0U);
	emit(&batch, 0U);
	emit_pc(&batch, PIPE_CONTROL_CS_STALL | PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH | PIPE_CONTROL_DEPTH_CACHE_FLUSH |
		PIPE_CONTROL_DC_FLUSH_ENABLE | PIPE_CONTROL_FLUSH_ENABLE);
	emit(&batch, MI_BATCH_BUFFER_END);
	emit(&batch, MI_NOOP);
	if (batch.overflow != 0)
		return ENOSPC;
	kern_io_write_barrier();
	*batch_va = gs->batch->va;
	return 0;
}

/* One rectangle to its end on the GPU (the recorded clears and copies). */
int
i915_vk_gfx_rect(struct i915_vk_session *session, const struct gfx_surface *dst, const struct gfx_rect *dst_rect,
	const struct gfx_surface *src, const struct gfx_rect *src_rect, const uint32_t clear[4], int linear)
{
	static unsigned logged;
	uint64_t batch_va;
	int error;

	error = i915_vk_gfx_rect_build(session, dst, dst_rect, src, src_rect, clear, linear, &batch_va);
	if (error != 0)
		return error;
	error = parity_shim_run_sync(session->vk->i915, &session->gpu->contexts[I915_ENGINE_RCS0], batch_va);
	if (error != 0 || logged < 4U) {
		logged++;
		kern_logf("i915: vk: GPU %s %ux%u -> %ux%u at (%d,%d) of a %ux%u surface: %d\n", src != NULL ? "copy" : "fill",
			src != NULL ? src_rect->w : dst_rect->w, src != NULL ? src_rect->h : dst_rect->h, dst_rect->w, dst_rect->h,
			dst_rect->x, dst_rect->y, dst->width, dst->height, error);
	}
	return error;
}
