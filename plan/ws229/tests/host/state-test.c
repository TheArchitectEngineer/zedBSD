/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws229-p004: the 3D and compute state of the render path for Gen12 and
 * Gen9, on the host.
 *
 * The draw (render/draw.c), the rectangle (render/blit.c) and the dispatch
 * (render/compute.c) are built into batches as the kernel builds them, once
 * with a batch of graphics version 12 and once of version 9, and the state
 * they point at (a render target's and a texture's surface states, a
 * sampler, an interface descriptor) is written beside them.  Every batch
 * and structure is written to OUT-DIR as VER-NAME.bin; gen9-check.py
 * decodes the version 12 files with Mesa's gen120.xml and the version 9
 * files with gen90.xml and compares the two field by field
 * (plan/ws229/tests/host/run.sh).
 *
 * The hardware tests' single-colour draw fixture
 * (tests/fixtures/draw-fixture.c) is built the same way: its batch, its
 * state page, its render target's surface state and, for Gen9, its pixel
 * shader (ws229-p005).
 *
 * Built with -DTEST_BASELINE against a tree from before ws229-p004, only
 * the version 12 files are written, through the functions that tree has;
 * run.sh compares them byte for byte with the new tree's.
 *
 *   state-test OUT-DIR
 */

#include "../../../ws031/tests/i915-vk-render-stubs.inc"

#include "../../../../src/drivers/gpu/i915/compiler/compiler.h"
#include "../../../../src/drivers/gpu/i915/render/batch.h"
#include "../../../../src/drivers/gpu/i915/render/compute.h"
#include "../../../../src/drivers/gpu/i915/render/heap.h"
#include "../../../../src/drivers/gpu/i915/render/state.h"
#include "../../../../src/drivers/gpu/i915/tests/fixtures/draw-fixture.h"

/*
 * The stub file stands in for the draw path's session functions, the
 * rectangles and the dispatch; the ones under test are compiled here under
 * other names, so their static builders can be called directly.
 */
#define drv_i915_gfx_draw		test_unused_draw
#define drv_i915_gfx_submit_begin	test_unused_submit_begin
#define drv_i915_gfx_submit_end		test_unused_submit_end
#define drv_i915_gfx_batch_run		test_unused_batch_run
#define drv_i915_gfx_object_create	test_unused_object_create
#define drv_i915_gfx_object_destroy	test_unused_object_destroy
#define drv_i915_gfx_session_get	test_unused_session_get
#define drv_i915_gfx_op_begin		test_unused_op_begin
#define drv_i915_gfx_op_end		test_unused_op_end
#define drv_i915_gfx_session_close	test_unused_session_close
#define drv_i915_gfx_rect		test_unused_rect
#define drv_i915_gfx_resolve		test_unused_resolve
#define drv_i915_gfx_dispatch		test_unused_dispatch

/* The renamed functions, declared again under their new names (draw.h was read under the old ones). */
struct i915_gfx_session *drv_i915_gfx_session_get(struct i915_render_session *session);
void drv_i915_gfx_session_close(struct i915_render_session *session);
int drv_i915_gfx_submit_begin(struct i915_render_session *session);
int drv_i915_gfx_submit_end(struct i915_render_session *session);
int drv_i915_gfx_op_begin(struct i915_render_session *session, struct i915_gfx_session *work, struct i915_gfx_op_space *space);
int drv_i915_gfx_op_end(struct i915_render_session *session, struct i915_gfx_session *work, int error);
int drv_i915_gfx_batch_run(struct i915_render_session *session, struct i915_gfx_batch *batch, uint64_t batch_va, unsigned engine);
int drv_i915_gfx_object_create(struct i915_render_session *session, uint64_t bytes, struct i915_gem_object **result);
void drv_i915_gfx_object_destroy(struct i915_render_session *session, struct i915_gem_object *object);
int drv_i915_gfx_draw(struct i915_render_session *session, const struct i915_gfx_draw_state *state, const struct i915_gfx_draw_args *args);
int drv_i915_gfx_rect(struct i915_render_session *session, const struct i915_gfx_surface *dst, const struct i915_gfx_rect *dst_rect, const struct i915_gfx_surface *src, const struct i915_gfx_rect *src_rect, const uint32_t clear[4], int linear);
int drv_i915_gfx_dispatch(struct i915_render_session *session, const struct i915_gfx_draw_state *state, const struct i915_gfx_grid *grid);

#include "../../../../src/drivers/gpu/i915/render/draw.c"
#include "../../../../src/drivers/gpu/i915/render/blit.c"
#include "../../../../src/drivers/gpu/i915/render/compute.c"

/* Where the test's objects are in the GPU's address space. */
#define TEST_SLOT_VA		0x0000000123450000ULL
#define TEST_WINDOW_VA		0x0000000067800000ULL
#define TEST_SCRATCH_VA		0x0000000089a00000ULL
#define TEST_STORAGE_VA		0x0000000200000000ULL
#define TEST_INDIRECT_VA	0x0000000500000040ULL

/* The offsets of the images and buffers in the storage. */
#define TEST_TARGET_OFFSET	0x10000U
#define TEST_DEPTH_OFFSET	0x20000U
#define TEST_STENCIL_OFFSET	0x8000U
#define TEST_VERTEX_OFFSET	0x30000U
#define TEST_INDEX_OFFSET	0x31000U

/* The bytes of the storage and of a batch. */
#define TEST_STORAGE_BYTES	0x40000U
#define TEST_BATCH_WORDS	4096U

/* 1.0, 32.0 and 64.0 as float bits. */
#define TEST_FLOAT_ONE		0x3f800000U
#define TEST_FLOAT_32		0x42000000U
#define TEST_FLOAT_64		0x42800000U

/*
 * Everything a scenario builds from, for one graphics version.
 *
 * One instance lives in a static for the whole run; each scenario clears
 * and fills the parts it uses.
 */
struct test_world {
	/* The graphics version the batches are built for. */
	unsigned graphics_ver;

	/* The memory the images and buffers are bound to. */
	struct i915_gem_object object;
	struct i915_gfx_memory memory;

	/* The colour target, the depth-stencil image and their views. */
	struct i915_gfx_image target;
	struct i915_gfx_image depth;
	struct i915_gfx_view target_view;
	struct i915_gfx_view depth_view;

	/* The pass and the framebuffer of a draw. */
	struct i915_gfx_pass pass;
	struct i915_gfx_framebuffer framebuffer;

	/* The vertex and the index buffer. */
	struct i915_gfx_buffer vertices;
	struct i915_gfx_buffer indices;

	/* The pipeline, the kernels and the bound state. */
	struct i915_gfx_pipeline pipeline;
	struct i915_gfx_kernels kernels;
	struct i915_gfx_draw_state state;
};

/* The storage the images and buffers live in. */
static uint8_t test_storage[TEST_STORAGE_BYTES] __attribute__((aligned(4096)));

/* The slot and the instruction window an operation writes. */
static uint8_t test_slot[I915_GFX_SLOT_BYTES] __attribute__((aligned(4096)));
static uint8_t test_window[I915_GFX_INSTRUCTION_BYTES] __attribute__((aligned(64)));

/* The batch every scenario builds into. */
static uint32_t test_words[TEST_BATCH_WORDS];

/* The scenario's world, and where the files go. */
static struct test_world test_world_state;
static const char *test_out;

/* A geometry kernel's code: only its presence is read. */
static const uint32_t test_gs_code[4];

static void test_write(unsigned ver, const char *name, const void *bytes, size_t count);
static void test_batch_open(struct i915_gfx_batch *batch, unsigned ver);
static void test_batch_close(struct i915_gfx_batch *batch, unsigned ver, const char *name);
static void test_world_init(struct test_world *world, unsigned ver);
static void test_draw_depth(unsigned ver);
static void test_draw_indexed(unsigned ver);
static void test_rect(unsigned ver, int copy);
static void test_dispatch(unsigned ver, int indirect);
static void test_structures(unsigned ver);
static void test_fixture(unsigned ver);
static void test_version(unsigned ver);

/* Stand-ins for what draw.c calls in the kernel: the host has no GPU objects, no timing and no engine. */
int
drv_i915_gem_create(
	struct i915_gem_registry *registry,
	uint64_t bytes,
	struct i915_gem_object **result)
{
	UNUSED_PARAMETER(registry);
	UNUSED_PARAMETER(bytes);

	/* No object on the host. */
	*result = NULL;
	return ENOMEM;
}

int
drv_i915_gem_bind_vm(
	struct i915_ppgtt *vm,
	struct i915_gem_object *object)
{
	UNUSED_PARAMETER(vm);
	UNUSED_PARAMETER(object);

	/* Nothing is bound on the host. */
	return ENOMEM;
}

void
drv_i915_gem_unbind_vm(
	struct i915_gem_object *object)
{
	UNUSED_PARAMETER(object);
}

uint64_t
drv_i915_perf_now(void)
{
	/* The host keeps no time. */
	return 0U;
}

void
drv_i915_perf_add(
	struct i915_perf *perf,
	enum i915_perf_stage stage,
	uint64_t start)
{
	UNUSED_PARAMETER(perf);
	UNUSED_PARAMETER(stage);
	UNUSED_PARAMETER(start);
}

void
drv_i915_perf_report(
	struct i915_perf *perf,
	int final)
{
	UNUSED_PARAMETER(perf);
	UNUSED_PARAMETER(final);
}

int
drv_i915_worker_run_sync(
	struct i915_device *device,
	struct i915_context *context,
	uint64_t batch_va)
{
	UNUSED_PARAMETER(device);
	UNUSED_PARAMETER(context);
	UNUSED_PARAMETER(batch_va);

	/* No engine on the host. */
	return ENODEV;
}

int
main(
	int argc,
	char **argv)
{
	assert(argc == 2);
	test_out = argv[1];

	/* Builds every scenario for Gen12, then, in the new tree, for Gen9. */
	test_version(12U);
#ifndef TEST_BASELINE
	test_version(9U);
#endif

	/* Reports where the dumped state went. */
	printf("ws229 state host test: files written to %s\n", test_out);

	/* Succeeded: every scenario was dumped. */
	return 0;
}

/* Writes one file of the run: OUT-DIR/VER-NAME.bin. */
static void
test_write(
	unsigned ver,
	const char *name,
	const void *bytes,
	size_t count)
{
	char path[512];
	FILE *file;

	/* Opens, writes and closes the file. */
	snprintf(path, sizeof(path), "%s/%u-%s.bin", test_out, ver, name);
	file = fopen(path, "wb");
	assert(file != NULL);
	assert(fwrite(bytes, 1U, count, file) == count);
	fclose(file);
}

/* Starts an empty batch of the given version over the test's words. */
static void
test_batch_open(
	struct i915_gfx_batch *batch,
	unsigned ver)
{
	/* An empty batch with room for every scenario. */
	memset(batch, 0, sizeof(*batch));
	memset(test_words, 0, sizeof(test_words));
	batch->cmds = test_words;
	batch->capacity = TEST_BATCH_WORDS;
#ifndef TEST_BASELINE
	batch->graphics_ver = ver;
#else
	UNUSED_PARAMETER(ver);
#endif
}

/* Ends the batch as a draw path ends it and writes it out. */
static void
test_batch_close(
	struct i915_gfx_batch *batch,
	unsigned ver,
	const char *name)
{
	/* Ends the batch; it must have fitted. */
	drv_i915_gfx_emit_batch_end(batch);
	assert(batch->overflow == 0);

	/* Writes its dwords. */
	test_write(ver, name, batch->cmds, batch->count * 4U);
}

/*
 * Fills the world every draw starts from: a 64 x 32 R8G8B8A8 target, a
 * D32_SFLOAT_S8_UINT depth-stencil image of the same extent, a pass and a
 * framebuffer with both, and a pipeline that draws a triangle list with
 * two attributes, depth and stencil tests and alpha blending.
 */
static void
test_world_init(
	struct test_world *world,
	unsigned ver)
{
	/* Starts from nothing. */
	memset(world, 0, sizeof(*world));
	world->graphics_ver = ver;

	/* The storage, bound at its address. */
	world->object.address = test_storage;
	world->object.va = TEST_STORAGE_VA;
	world->object.bytes = TEST_STORAGE_BYTES;
	world->memory.object = &world->object;
	world->memory.size = TEST_STORAGE_BYTES;

	/* The linear colour target. */
	world->target.format = VK_FORMAT_R8G8B8A8_UNORM;
	world->target.width = 64U;
	world->target.height = 32U;
	world->target.pitch = 256U;
	world->target.bytes = 256U * 32U;
	world->target.levels = 1U;
	world->target.type = VK_IMAGE_TYPE_2D;
	world->target.layers = 1U;
	world->target.slice_rows = 32U;
	world->target.samples = 1U;
	world->target.memory = &world->memory;
	world->target.offset = TEST_TARGET_OFFSET;

	/* The Y-tiled depth-stencil image with its separate stencil plane. */
	world->depth.format = VK_FORMAT_D32_SFLOAT_S8_UINT;
	world->depth.width = 64U;
	world->depth.height = 32U;
	world->depth.pitch = 256U;
	world->depth.bytes = 0x10000U;
	world->depth.levels = 1U;
	world->depth.type = VK_IMAGE_TYPE_2D;
	world->depth.layers = 1U;
	world->depth.slice_rows = 32U;
	world->depth.samples = 1U;
	world->depth.stencil = 1U;
	world->depth.stencil_offset = TEST_STENCIL_OFFSET;
	world->depth.stencil_pitch = 128U;
	world->depth.stencil_slice_rows = 32U;
	world->depth.memory = &world->memory;
	world->depth.offset = TEST_DEPTH_OFFSET;

	/* A view of each, one level and one layer. */
	world->target_view.image = &world->target;
	world->target_view.format = world->target.format;
	world->target_view.level_count = 1U;
	world->target_view.view_type = VK_IMAGE_VIEW_TYPE_2D;
	world->target_view.layer_count = 1U;
	world->depth_view.image = &world->depth;
	world->depth_view.format = world->depth.format;
	world->depth_view.level_count = 1U;
	world->depth_view.view_type = VK_IMAGE_VIEW_TYPE_2D;
	world->depth_view.layer_count = 1U;

	/* The pass: colour attachment 0, depth attachment 1. */
	world->pass.attachment_count = 2U;
	world->pass.attachments[0].format = world->target.format;
	world->pass.attachments[1].format = world->depth.format;
	world->pass.color_attachment = 0U;
	world->pass.depth_attachment = 1U;
	world->pass.color_count = 1U;
	world->pass.color_attachments[0] = 0U;

	/* The framebuffer over the two views. */
	world->framebuffer.width = 64U;
	world->framebuffer.height = 32U;
	world->framebuffer.layers = 1U;
	world->framebuffer.view_count = 2U;
	world->framebuffer.views[0] = &world->target_view;
	world->framebuffer.views[1] = &world->depth_view;

	/* The vertex buffer and the index buffer. */
	world->vertices.size = 4096U;
	world->vertices.memory = &world->memory;
	world->vertices.offset = TEST_VERTEX_OFFSET;
	world->indices.size = 256U;
	world->indices.memory = &world->memory;
	world->indices.offset = TEST_INDEX_OFFSET;

	/* The pipeline's vertex input: one binding of 32 bytes, a vec4 and a vec2. */
	world->pipeline.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	world->pipeline.binding_count = 1U;
	world->pipeline.bindings[0].binding = 0U;
	world->pipeline.bindings[0].stride = 32U;
	world->pipeline.attribute_count = 2U;
	world->pipeline.attributes[0].location = 0U;
	world->pipeline.attributes[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
	world->pipeline.attributes[1].location = 1U;
	world->pipeline.attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
	world->pipeline.attributes[1].offset = 16U;

	/* The viewport and the scissor over the target. */
	world->pipeline.viewport[2] = TEST_FLOAT_64;
	world->pipeline.viewport[3] = TEST_FLOAT_32;
	world->pipeline.viewport[5] = TEST_FLOAT_ONE;
	world->pipeline.scissor.extent.width = 64U;
	world->pipeline.scissor.extent.height = 32U;

	/* No culling, one sample, depth and stencil tests, alpha blending. */
	world->pipeline.cull_mode = VK_CULL_MODE_NONE;
	world->pipeline.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	world->pipeline.samples = 1U;
	world->pipeline.sample_mask = 1U;
	world->pipeline.depth_test = 1U;
	world->pipeline.depth_write = 1U;
	world->pipeline.depth_compare = VK_COMPARE_OP_LESS;
	world->pipeline.stencil_test = 1U;
	world->pipeline.stencil_compare[0] = VK_COMPARE_OP_EQUAL;
	world->pipeline.stencil_compare[1] = VK_COMPARE_OP_EQUAL;
	world->pipeline.stencil_pass[0] = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
	world->pipeline.stencil_pass[1] = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
	world->pipeline.stencil_compare_mask[0] = 0xffU;
	world->pipeline.stencil_compare_mask[1] = 0xffU;
	world->pipeline.stencil_write_mask[0] = 0xffU;
	world->pipeline.stencil_write_mask[1] = 0xffU;
	world->pipeline.stencil_reference[0] = 1U;
	world->pipeline.stencil_reference[1] = 1U;
	world->pipeline.blend_enable = 1U;
	world->pipeline.blend_src_color = VK_BLEND_FACTOR_SRC_ALPHA;
	world->pipeline.blend_dst_color = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	world->pipeline.blend_color_op = VK_BLEND_OP_ADD;
	world->pipeline.blend_src_alpha = VK_BLEND_FACTOR_ONE;
	world->pipeline.blend_dst_alpha = VK_BLEND_FACTOR_ZERO;
	world->pipeline.blend_alpha_op = VK_BLEND_OP_ADD;
	world->pipeline.kernels_ready = 1;

	/*
	 * The kernels: a vertex kernel reading the two attributes and spilling
	 * to scratch, one varying, a pixel kernel that discards and reads the
	 * pixel's depth, push data in both.
	 */
	world->kernels.vs_grf_start = 1U;
	world->kernels.vs_push_regs = 1U;
	world->kernels.vs_input_count = 2U;
	world->kernels.vs_inputs[0] = 0U;
	world->kernels.vs_inputs[1] = 1U;
	world->kernels.vs_varyings = 1U;
	world->kernels.varyings = 1U;
	world->kernels.ps_grf_start = 2U;
	world->kernels.ps_push_regs = 1U;
	world->kernels.ps_kills = 1U;
	world->kernels.ps_source_depth = 1U;
	world->kernels.vs_scratch_bytes = 2048U;
	world->kernels.vs_scratch_offset = 0x1000U;
	world->kernels.scratch_base = TEST_SCRATCH_VA;

	/* The bound state: the pass, the framebuffer, the pipeline and the vertex buffer. */
	world->state.pass = &world->pass;
	world->state.framebuffer = &world->framebuffer;
	world->state.pipeline = &world->pipeline;
	world->state.vertex[0].buffer = &world->vertices;
	world->state.index.buffer = &world->indices;
	world->state.index.type = VK_INDEX_TYPE_UINT16;
}

/* Builds the depth-stencil draw: three vertices, two instances, the target's state in the slot. */
static void
test_draw_depth(
	unsigned ver)
{
	struct test_world *world;
	struct i915_gfx_op_space space;
	struct i915_gfx_draw_args args;
	struct i915_gfx_batch batch;
	int error;

	/* The world, the slot and the batch. */
	world = &test_world_state;
	test_world_init(world, ver);
	memset(test_slot, 0, sizeof(test_slot));
	test_batch_open(&batch, ver);
	space.slot = test_slot;
	space.slot_va = TEST_SLOT_VA;
	space.window = test_window;
	space.window_va = TEST_WINDOW_VA;
	space.batch = &batch;

	/* Writes the slot: the target's surface state, the blend, viewports and push data. */
#ifndef TEST_BASELINE
	error = drv_i915_gfx_write_state_gen(test_slot, &world->state, &world->kernels, &world->target, drv_i915_gfx_mocs_uncached(ver), ver);
#else
	error = drv_i915_gfx_write_state(test_slot, &world->state, &world->kernels, &world->target, GEN12_MOCS(I915_MOCS_UNCACHED_INDEX));
#endif
	assert(error == 0);
	test_write(ver, "rss-target", test_slot + I915_GFX_SURFACE_HEAP + I915_GFX_RSS_TARGET, GEN12_RENDER_SURFACE_STATE_DWORDS * 4U);

	/* Builds the draw. */
	memset(&args, 0, sizeof(args));
	args.count = 3U;
	args.instance_count = 2U;
#ifndef TEST_BASELINE
	error = i915_draw_build_batch(&batch, &space, &world->state, &world->kernels, &world->target, &world->depth, drv_i915_gfx_mocs_uncached(ver), &args);
#else
	error = i915_draw_build_batch(&batch, &space, &world->state, &world->kernels, &world->target, &world->depth, GEN12_MOCS(I915_MOCS_UNCACHED_INDEX), &args);
#endif
	assert(error == 0);
	test_batch_close(&batch, ver, "draw-depth");
}

/*
 * Builds the indexed draw: a geometry kernel, the vertex and instance
 * indices generated by the fetcher, no depth attachment, linear
 * barycentrics.
 */
static void
test_draw_indexed(
	unsigned ver)
{
	struct test_world *world;
	struct i915_gfx_op_space space;
	struct i915_gfx_draw_args args;
	struct i915_gfx_batch batch;
	int error;

	/* The world without its depth attachment. */
	world = &test_world_state;
	test_world_init(world, ver);
	world->pass.attachment_count = 1U;
	world->pass.depth_attachment = VK_ATTACHMENT_UNUSED;
	world->framebuffer.view_count = 1U;
	world->pipeline.depth_test = 0U;
	world->pipeline.depth_write = 0U;
	world->pipeline.stencil_test = 0U;
	world->pipeline.blend_enable = 0U;

	/* The fetcher generates both inputs. */
	world->kernels.vs_input_count = 2U;
	world->kernels.vs_inputs[0] = DRV_GPU_SHADER_LOCATION_VERTEX_INDEX;
	world->kernels.vs_inputs[1] = DRV_GPU_SHADER_LOCATION_INSTANCE_INDEX;
	world->kernels.vs_scratch_bytes = 0U;
	world->kernels.ps_kills = 0U;
	world->kernels.ps_source_depth = 0U;
	world->kernels.ps_linear_barycentrics = 1U;

	/* The geometry kernel: triangles in, a strip of vertices of two 32-byte units out. */
	world->kernels.gs_code = test_gs_code;
	world->kernels.gs_bytes = sizeof(test_gs_code);
	world->kernels.gs_grf_start = 3U;
	world->kernels.gs_vertices_in = 3U;
	world->kernels.gs_output_topology = GEN12_3DPRIM_TRISTRIP;
	world->kernels.gs_output_vertex_hwords = 2U;
	world->kernels.gs_urb_entry_size = 4U;
	world->kernels.gs_primitive_id = 1U;

	/* The slot and the batch. */
	memset(test_slot, 0, sizeof(test_slot));
	test_batch_open(&batch, ver);
	space.slot = test_slot;
	space.slot_va = TEST_SLOT_VA;
	space.window = test_window;
	space.window_va = TEST_WINDOW_VA;
	space.batch = &batch;

	/* Builds the draw of six indices. */
	memset(&args, 0, sizeof(args));
	args.indexed = 1;
	args.count = 6U;
	args.instance_count = 1U;
	args.first = 2U;
	args.vertex_offset = -1;
#ifndef TEST_BASELINE
	error = i915_draw_build_batch(&batch, &space, &world->state, &world->kernels, &world->target, NULL, drv_i915_gfx_mocs_uncached(ver), &args);
#else
	error = i915_draw_build_batch(&batch, &space, &world->state, &world->kernels, &world->target, NULL, GEN12_MOCS(I915_MOCS_UNCACHED_INDEX), &args);
#endif
	assert(error == 0);
	test_batch_close(&batch, ver, "draw-indexed");
}

/* Builds a rectangle: a fill of the target, or a linear copy into it from a texture. */
static void
test_rect(
	unsigned ver,
	int copy)
{
	struct i915_gfx_surface dst;
	struct i915_gfx_surface src;
	struct i915_gfx_rect dst_rect;
	struct i915_gfx_rect src_rect;
	struct i915_gfx_kernels kernels;
	struct i915_gfx_op_space space;
	struct i915_gfx_batch batch;
	uint32_t clear[4];
	uint32_t mocs;
	int error;

	/* The destination and the source surfaces, and the rectangles. */
	memset(&dst, 0, sizeof(dst));
	dst.va = TEST_STORAGE_VA + TEST_TARGET_OFFSET;
	dst.width = 64U;
	dst.height = 32U;
	dst.pitch = 256U;
	dst.format = VK_FORMAT_B8G8R8A8_UNORM;
	src = dst;
	src.va = TEST_STORAGE_VA + TEST_DEPTH_OFFSET;
	src.format = VK_FORMAT_R8G8B8A8_UNORM;
	dst_rect.x = 1;
	dst_rect.y = 2;
	dst_rect.w = 30U;
	dst_rect.h = 20U;
	src_rect = dst_rect;
	clear[0] = TEST_FLOAT_ONE;
	clear[1] = 0U;
	clear[2] = 0U;
	clear[3] = TEST_FLOAT_ONE;

	/* The slot and the batch. */
	memset(test_slot, 0, sizeof(test_slot));
	test_batch_open(&batch, ver);
	space.slot = test_slot;
	space.slot_va = TEST_SLOT_VA;
	space.window = test_window;
	space.window_va = TEST_WINDOW_VA;
	space.batch = &batch;
#ifndef TEST_BASELINE
	mocs = drv_i915_gfx_mocs_uncached(ver);
#else
	mocs = GEN12_MOCS(I915_MOCS_UNCACHED_INDEX);
#endif

	/* The pixel kernel: one attribute, its payload, one sampler for a copy. */
	memset(&kernels, 0, sizeof(kernels));
	kernels.varyings = 1U;
	kernels.ps_grf_start = 2U;
	if (copy != 0)
		kernels.ps_samplers = 1U;

	/* Writes the state and the commands, as i915_blit_record() does. */
#ifndef TEST_BASELINE
	if (copy != 0) {
		error = i915_blit_write_state(test_slot, &dst, &dst_rect, &src, &src_rect, NULL, I915_GFX_RECT_LINEAR, mocs, ver);
	} else {
		error = i915_blit_write_state(test_slot, &dst, &dst_rect, NULL, NULL, clear, 0, mocs, ver);
	}
#else
	if (copy != 0) {
		error = i915_blit_write_state(test_slot, &dst, &dst_rect, &src, &src_rect, NULL, I915_GFX_RECT_LINEAR, mocs);
	} else {
		error = i915_blit_write_state(test_slot, &dst, &dst_rect, NULL, NULL, clear, 0, mocs);
	}
#endif
	assert(error == 0);
	i915_blit_build_batch(&batch, &space, &dst, &kernels, mocs);

	/* A copy also keeps its texture's surface state and its sampler. */
	if (copy != 0) {
		test_write(ver, "rss-texture", test_slot + I915_GFX_SURFACE_HEAP + I915_GFX_RSS_TEXTURE, GEN12_RENDER_SURFACE_STATE_DWORDS * 4U);
		test_write(ver, "sampler-linear", test_slot + I915_GFX_DYNAMIC_HEAP + I915_GFX_DYN_SAMPLER, 16U);
		test_batch_close(&batch, ver, "rect-copy");
	} else {
		test_batch_close(&batch, ver, "rect-fill");
	}
}

/*
 * Builds a dispatch: two threads a group with shared memory and a barrier,
 * or an indirect one without shared memory.
 */
static void
test_dispatch(
	unsigned ver,
	int indirect)
{
	struct test_world *world;
	struct i915_shader_binary binary;
	struct i915_gfx_op_space space;
	struct i915_gfx_grid grid;
	struct i915_gfx_batch batch;
	uint32_t thread_ids[2 * I915_SHADER_PER_THREAD_REGS * 8U];
	uint32_t mocs;
	int error;

	/* The world, with a compute pipeline of two threads. */
	world = &test_world_state;
	test_world_init(world, ver);
	memset(&binary, 0, sizeof(binary));
	binary.cross_thread_regs = 1U;
	if (indirect == 0) {
		binary.shared_bytes = 4096U;
		binary.uses_barrier = 1U;
	}

	/* Gives the pipeline two threads with zeroed thread ids. */
	memset(thread_ids, 0, sizeof(thread_ids));
	world->pipeline.cs_binary = &binary;
	world->pipeline.threads = 2U;
	world->pipeline.right_mask = 0xffU;
	world->pipeline.thread_ids = thread_ids;
	world->state.compute_pipeline = &world->pipeline;
	world->kernels.cs_scratch_bytes = 1024U;
	world->kernels.cs_scratch_offset = 0x40000U;

	/* The groups, or the indirect buffer. */
	memset(&grid, 0, sizeof(grid));
	grid.groups[0] = 4U;
	grid.groups[1] = 2U;
	grid.groups[2] = 3U;
	if (indirect != 0)
		grid.indirect_va = TEST_INDIRECT_VA;

	/* The slot and the batch. */
	memset(test_slot, 0, sizeof(test_slot));
	test_batch_open(&batch, ver);
	space.slot = test_slot;
	space.slot_va = TEST_SLOT_VA;
	space.window = test_window;
	space.window_va = TEST_WINDOW_VA;
	space.batch = &batch;
#ifndef TEST_BASELINE
	mocs = drv_i915_gfx_mocs_uncached(ver);
#else
	mocs = GEN12_MOCS(I915_MOCS_UNCACHED_INDEX);
#endif

	/* Writes the slot and the commands for three dual-subslices (or subslices). */
#ifndef TEST_BASELINE
	error = drv_i915_gfx_dispatch_write_gen(test_slot, TEST_SLOT_VA, &world->state, &grid, ver);
#else
	error = drv_i915_gfx_dispatch_write(test_slot, TEST_SLOT_VA, &world->state, &grid);
#endif
	assert(error == 0);
	drv_i915_gfx_dispatch_build(&batch, &space, &world->pipeline, &world->kernels, &grid, 3U, mocs);

	/* Keeps the interface descriptor and the batch. */
	if (indirect != 0) {
		test_write(ver, "idd-indirect", test_slot + I915_GFX_DYNAMIC_HEAP + I915_GFX_DYN_INTERFACE, GEN12_INTERFACE_DESCRIPTOR_DWORDS * 4U);
		test_batch_close(&batch, ver, "dispatch-indirect");
	} else {
		test_write(ver, "idd-shared", test_slot + I915_GFX_DYNAMIC_HEAP + I915_GFX_DYN_INTERFACE, GEN12_INTERFACE_DESCRIPTOR_DWORDS * 4U);
		test_batch_close(&batch, ver, "dispatch-shared");
	}
}

/*
 * Writes the structures no batch scenario keeps: an anisotropic sampler
 * with a border colour, a Y-tiled depth surface, and the start of a
 * cleared instruction window.
 */
static void
test_structures(
	unsigned ver)
{
	struct i915_gfx_sampler sampler;
	struct i915_gfx_surface surface;
	uint32_t state[4];
	uint32_t border[16];
	uint32_t rss[GEN12_RENDER_SURFACE_STATE_DWORDS];
	int error;

	/* An anisotropic, comparing sampler with an opaque white border. */
	memset(&sampler, 0, sizeof(sampler));
	sampler.mag_filter = VK_FILTER_LINEAR;
	sampler.min_filter = VK_FILTER_LINEAR;
	sampler.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	sampler.address_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	sampler.address_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	sampler.address_w = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	sampler.max_lod = 0x40800000U;
	sampler.compare_enable = 1U;
	sampler.compare_op = VK_COMPARE_OP_LESS;
	sampler.border_color = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	sampler.anisotropy_enable = 1U;
	sampler.max_anisotropy = 0x41000000U;
	drv_i915_gfx_sampler_write(state, &sampler);
	drv_i915_gfx_sampler_border_write(state, border, 0x2c0U, &sampler);
	test_write(ver, "sampler-border", state, sizeof(state));

	/* A Y-tiled D32 surface, as a depth image is sampled. */
	memset(&surface, 0, sizeof(surface));
	surface.va = TEST_STORAGE_VA + TEST_DEPTH_OFFSET;
	surface.width = 64U;
	surface.height = 32U;
	surface.pitch = 256U;
	surface.format = VK_FORMAT_D32_SFLOAT;
#ifndef TEST_BASELINE
	error = drv_i915_gfx_surface_write_gen(rss, &surface, drv_i915_gfx_mocs_uncached(ver), ver);
#else
	error = drv_i915_gfx_surface_write(rss, &surface, GEN12_MOCS(I915_MOCS_UNCACHED_INDEX));
#endif
	assert(error == 0);
	test_write(ver, "rss-depth", rss, sizeof(rss));

	/* The instructions a cleared window holds. */
#ifndef TEST_BASELINE
	drv_i915_gfx_instruction_heap_clear_gen(test_window, ver);
#else
	drv_i915_gfx_instruction_heap_clear(test_window);
#endif
	test_write(ver, "eot", test_window, 64U);
}

/*
 * Builds the hardware tests' single-colour draw fixture for one version.
 *
 * The new tree builds Gen12 through the functions without a version and
 * checks that the _gen functions give the same bytes; the baseline has only
 * the former.
 */
static void
test_fixture(
	unsigned ver)
{
	static uint8_t page[4096] __attribute__((aligned(4096)));
	static uint8_t again[4096] __attribute__((aligned(4096)));
	uint32_t again_words[TEST_BATCH_WORDS];
	uint32_t mocs;
	unsigned count;
	unsigned again_count;

	/* The state page and the batch, as the draw test writes them. */
	memset(test_words, 0, sizeof(test_words));
#ifndef TEST_BASELINE
	mocs = drv_i915_draw_fixture_mocs_gen(ver);
	drv_i915_draw_fixture_write_state_gen(page, I915_DRAW_FIXTURE_STATE_VA + 0x2000U, mocs, ver);
	count = drv_i915_draw_fixture_build_batch_gen(test_words, TEST_BATCH_WORDS, I915_DRAW_FIXTURE_STATE_VA, mocs, ver);
#else
	mocs = drv_i915_draw_fixture_mocs();
	drv_i915_draw_fixture_write_state(page, I915_DRAW_FIXTURE_STATE_VA + 0x2000U, mocs);
	count = drv_i915_draw_fixture_build_batch(test_words, TEST_BATCH_WORDS, I915_DRAW_FIXTURE_STATE_VA, mocs);
#endif
	assert(count != 0U && count < TEST_BATCH_WORDS);

	/* The Gen12 wrappers must build exactly what the version 12 functions build. */
#ifndef TEST_BASELINE
	if (ver == 12U) {
		memset(again_words, 0, sizeof(again_words));
		assert(drv_i915_draw_fixture_mocs() == mocs);
		drv_i915_draw_fixture_write_state(again, I915_DRAW_FIXTURE_STATE_VA + 0x2000U, mocs);
		again_count = drv_i915_draw_fixture_build_batch(again_words, TEST_BATCH_WORDS, I915_DRAW_FIXTURE_STATE_VA, mocs);
		assert(again_count == count);
		assert(memcmp(again_words, test_words, count * 4U) == 0);
		assert(memcmp(again, page, sizeof(page)) == 0);
		assert(drv_i915_tex_fixture_supported(12U) == 1);
	} else {
		assert(drv_i915_tex_fixture_supported(ver) == 0);
	}
#else
	UNUSED_PARAMETER(again);
	UNUSED_PARAMETER(again_words);
	UNUSED_PARAMETER(again_count);
#endif

	/* The batch, the whole page, the render target's surface state, and the pixel shader. */
	test_write(ver, "fixture-draw", test_words, count * 4U);
	test_write(ver, "fixture-state", page, sizeof(page));
	test_write(ver, "fixture-rss", page + 64U, GEN12_RENDER_SURFACE_STATE_DWORDS * 4U);
#ifndef TEST_BASELINE
	if (ver == 9U)
		test_write(ver, "fixture-ps", page + I915_DRAW_FIXTURE_PS_OFFSET, 2048U - I915_DRAW_FIXTURE_PS_OFFSET);
#endif
}

/* Builds every scenario for one graphics version. */
static void
test_version(
	unsigned ver)
{
	/* The draws, the rectangles, the dispatches and the loose structures. */
	test_draw_depth(ver);
	test_draw_indexed(ver);
	test_rect(ver, 0);
	test_rect(ver, 1);
	test_dispatch(ver, 0);
	test_dispatch(ver, 1);
	test_structures(ver);
	test_fixture(ver);
}
