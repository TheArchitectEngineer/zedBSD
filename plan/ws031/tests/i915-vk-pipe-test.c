/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host fixture for shader modules and graphics pipelines
 * (render/pipeline.c, pipeline-prepare.c) and the shader state a draw emits
 * for a pipeline (render/state.c).
 *
 * vkCreateShaderModule and vkCreateGraphicsPipelines are driven through the
 * wire with the vkdemo shaders, so the pipeline's kernels come from the
 * executor's own compiler; the vertex and pixel shader state is then
 * emitted from those kernels.
 */

#include "i915-vk-render-stubs.inc"

#include "../../../src/drivers/gpu/i915/compiler/compiler.h"
#include "../../../src/drivers/gpu/i915/render/batch.h"
#include "../../../src/drivers/gpu/i915/render/heap.h"
#include "../../../src/drivers/gpu/i915/render/state.h"

#include "../../../src/drivers/gpu/i915/intel/genxml.h"

/* The wire opcodes the fixture sends, as libvulkan numbers them. */
#define FIXTURE_CREATE_SHADER_MODULE		59U
#define FIXTURE_DESTROY_SHADER_MODULE		60U
#define FIXTURE_CREATE_GRAPHICS_PIPELINES	65U
#define FIXTURE_DESTROY_PIPELINE		67U

/* The SPIR-V opcodes the fixture rewrites: a float multiply becomes a float division. */
#define FIXTURE_SPIRV_FMUL	133U
#define FIXTURE_SPIRV_FDIV	136U

/* The wire identities the fixture gives its objects. */
#define FIXTURE_DEVICE		0xd0ULL
#define FIXTURE_PROBE		0xc00ULL
#define FIXTURE_VS		0xd00ULL
#define FIXTURE_FS		0xd01ULL
#define FIXTURE_BAD_VS		0xd10ULL
#define FIXTURE_PIPELINE	0xe00ULL
#define FIXTURE_BAD_PIPELINE	0xe10ULL

/* The stream every command is built in. */
static struct stub_wire fixture_wire;

static uint32_t *fixture_load_spirv(const char *name, size_t *words);
static void fixture_shader_module(const uint32_t *code, size_t words, uint64_t identity);
static void fixture_stage(uint32_t stage, uint64_t module);
static void fixture_pipeline(uint64_t vertex, uint64_t fragment, uint64_t identity);
static void fixture_destroy(uint32_t opcode, uint64_t identity);
static int fixture_find_command(const uint32_t *batch, unsigned used, uint32_t opcode);
static void test_shader_module(void);
static void test_graphics_pipeline(void);

/*
 * Runs the pipeline checks.
 */
int
main(void)
{
	/* Checks shader modules, then pipelines and the state they emit. */
	test_shader_module();
	test_graphics_pipeline();

	/* Succeeded: every check held. */
	printf("i915 vk pipe host test PASS\n");
	return 0;
}

/* Loads one vkdemo SPIR-V shader into words the caller frees. */
static uint32_t *
fixture_load_spirv(
	const char *name,
	size_t *words)
{
	char path[512];
	FILE *file;
	uint32_t *code;
	size_t read;
	long size;
	int status;

	/* Opens the shader shipped with vkdemo. */
	snprintf(path, sizeof(path), "%s/userland/base/vkdemo/shaders/%s", VK_REPO, name);
	file = fopen(path, "rb");
	assert(file != NULL);

	/* Measures it: a module is whole words. */
	status = fseek(file, 0, SEEK_END);
	assert(status == 0);
	size = ftell(file);
	assert(size > 0);
	assert((size % 4) == 0);
	status = fseek(file, 0, SEEK_SET);
	assert(status == 0);

	/* Reads the words. */
	code = malloc((size_t)size);
	assert(code != NULL);
	read = fread(code, 1U, (size_t)size, file);
	assert(read == (size_t)size);
	fclose(file);

	/* Succeeded: the caller owns the words. */
	*words = (size_t)size / 4U;
	return code;
}

/* Appends vkCreateShaderModule for `words` words of SPIR-V. */
static void
fixture_shader_module(
	const uint32_t *code,
	size_t words,
	uint64_t identity)
{
	size_t index;

	/* The header, the device and the create info's presence marker. */
	stub_put32(&fixture_wire, FIXTURE_CREATE_SHADER_MODULE);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_DEVICE);
	stub_put64(&fixture_wire, 1U);

	/* VkShaderModuleCreateInfo: sType 16, no chain, flags, the size in bytes and the word count. */
	stub_put32(&fixture_wire, 16U);
	stub_put64(&fixture_wire, 0U);
	stub_put32(&fixture_wire, 0U);
	stub_put64(&fixture_wire, (uint64_t)words * 4U);
	stub_put64(&fixture_wire, (uint64_t)words);

	/* The words themselves. */
	for (index = 0U; index < words; index++)
		stub_put32(&fixture_wire, code[index]);

	/* The tail: no allocator, the identity behind its presence marker. */
	stub_put64(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 1U);
	stub_put64(&fixture_wire, identity);
}

/* Appends one VkPipelineShaderStageCreateInfo naming `module` for `stage`, entry "main". */
static void
fixture_stage(
	uint32_t stage,
	uint64_t module)
{
	/* sType 18, no chain, flags, the stage and the module. */
	stub_put32(&fixture_wire, 18U);
	stub_put64(&fixture_wire, 0U);
	stub_put32(&fixture_wire, 0U);
	stub_put32(&fixture_wire, stage);
	stub_put64(&fixture_wire, module);

	/* The entry point: five bytes with the terminator, padded to two words. */
	stub_put64(&fixture_wire, 5U);
	stub_put32(&fixture_wire, 0x6e69616dU);
	stub_put32(&fixture_wire, 0U);

	/* No specialization. */
	stub_put64(&fixture_wire, 0U);
}

/* Appends vkCreateGraphicsPipelines of one triangle-list pipeline with two stages. */
static void
fixture_pipeline(
	uint64_t vertex,
	uint64_t fragment,
	uint64_t identity)
{
	unsigned index;

	/* The header, the device, no cache and one create info. */
	stub_put32(&fixture_wire, FIXTURE_CREATE_GRAPHICS_PIPELINES);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_DEVICE);
	stub_put64(&fixture_wire, 0U);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, 1U);

	/* VkGraphicsPipelineCreateInfo: sType 28, no chain, flags, two stages. */
	stub_put32(&fixture_wire, 28U);
	stub_put64(&fixture_wire, 0U);
	stub_put32(&fixture_wire, 0U);
	stub_put32(&fixture_wire, 2U);
	stub_put64(&fixture_wire, 2U);
	fixture_stage(VK_SHADER_STAGE_VERTEX_BIT, vertex);
	fixture_stage(VK_SHADER_STAGE_FRAGMENT_BIT, fragment);

	/* No vertex input; input assembly: sType 20, no chain, flags, triangle list, no restart. */
	stub_put64(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 1U);
	stub_put32(&fixture_wire, 20U);
	stub_put64(&fixture_wire, 0U);
	stub_put32(&fixture_wire, 0U);
	stub_put32(&fixture_wire, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	stub_put32(&fixture_wire, 0U);

	/* No tessellation and no viewport; the rasterization record, all zero, is always present. */
	stub_put64(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 1U);
	for (index = 0U; index < 14U; index++)
		stub_put32(&fixture_wire, 0U);

	/* No multisample, depth-stencil, colour blend or dynamic state. */
	stub_put64(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 0U);

	/* No layout or render pass, subpass 0, no base pipeline. */
	stub_put64(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 0U);
	stub_put32(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 0U);
	stub_put32(&fixture_wire, 0U);

	/* No allocator, then one identity. */
	stub_put64(&fixture_wire, 0U);
	stub_put64(&fixture_wire, 1U);
	stub_put64(&fixture_wire, identity);
}

/* Appends a generic destroy: [opcode][reply][device][identity][pAllocator]. */
static void
fixture_destroy(
	uint32_t opcode,
	uint64_t identity)
{
	/* The command has no reply body; the reply is its echoed opcode. */
	stub_put32(&fixture_wire, opcode);
	stub_put32(&fixture_wire, 1U);
	stub_put64(&fixture_wire, FIXTURE_DEVICE);
	stub_put64(&fixture_wire, identity);
	stub_put64(&fixture_wire, 0U);
}

/* Finds the first dword of a batch whose command opcode is `opcode`; -1 when none is. */
static int
fixture_find_command(
	const uint32_t *batch,
	unsigned used,
	uint32_t opcode)
{
	unsigned index;

	/* Looks at every dword's high half. */
	for (index = 0U; index < used; index++) {
		if ((batch[index] >> 16) == opcode)
			return (int)index;
	}

	/* No dword carries the opcode. */
	return -1;
}

/*
 * A module keeps a copy of its words from the command's arena; code that is
 * not whole words fails the command.
 */
static void
test_shader_module(void)
{
	static const uint32_t probe[3] = {0x07230203U, 0x00010000U, 0x0badf00dU};
	struct i915_gfx_shader *shader;
	size_t reply_bytes;
	unsigned index;
	int error;

	/* Opens the fixture session. */
	stub_session_open(NULL);

	/* Three words round-trip: [59][VK_SUCCESS][present][identity]. */
	stub_wire_begin(&fixture_wire);
	fixture_shader_module(probe, 3U, FIXTURE_PROBE);
	reply_bytes = stub_execute_ok(&fixture_wire);
	assert(reply_bytes == 24U);
	assert(stub_get32(stub_reply, 0U) == FIXTURE_CREATE_SHADER_MODULE);
	assert(stub_get64(stub_reply, 16U) == FIXTURE_PROBE);
	shader = drv_i915_object_lookup(stub_vk, I915_VK_OBJ_SHADER_MODULE, FIXTURE_PROBE);
	assert(shader != NULL);
	assert(shader->word_count == 3U);
	for (index = 0U; index < 3U; index++)
		assert(shader->words[index] == probe[index]);

	/* vkDestroyShaderModule forgets and frees the module. */
	stub_wire_begin(&fixture_wire);
	fixture_destroy(FIXTURE_DESTROY_SHADER_MODULE, FIXTURE_PROBE);
	reply_bytes = stub_execute_ok(&fixture_wire);
	assert(reply_bytes == 4U);
	shader = drv_i915_object_lookup(stub_vk, I915_VK_OBJ_SHADER_MODULE, FIXTURE_PROBE);
	assert(shader == NULL);

	/* A code size that is not whole words fails the stream. */
	stub_wire_begin(&fixture_wire);
	fixture_shader_module(probe, 3U, FIXTURE_PROBE);
	fixture_wire.bytes[STUB_SELECTOR_BYTES + 8U + 8U + 8U + 4U + 8U + 4U] = 11U;
	error = stub_execute(&fixture_wire, &reply_bytes);
	assert(error == EINVAL);
	shader = drv_i915_object_lookup(stub_vk, I915_VK_OBJ_SHADER_MODULE, FIXTURE_PROBE);
	assert(shader == NULL);

	/* Closes the session; nothing stays allocated. */
	stub_session_close();
	assert(stub_live == 0U);
}

/*
 * A pipeline made from the vkdemo shaders is compiled by the executor and
 * its kernels drive the shader state; a pipeline whose vertex shader the
 * compiler cannot lower is refused as a whole.
 */
static void
test_graphics_pipeline(void)
{
	struct i915_gfx_pipeline *pipeline;
	struct i915_gfx_kernels kernels;
	struct i915_gfx_batch batch;
	uint32_t commands[256];
	uint32_t *vertex;
	uint32_t *fragment;
	uint32_t *broken;
	unsigned long mark;
	size_t vertex_words;
	size_t fragment_words;
	size_t reply_bytes;
	size_t at;
	int found;

	/* Loads the vkdemo shaders and opens the fixture session. */
	vertex = fixture_load_spirv("cuboid.vert.spv", &vertex_words);
	fragment = fixture_load_spirv("cuboid.frag.spv", &fragment_words);
	stub_session_open(NULL);

	/*
	 * Makes a valid vertex shader with an instruction the compiler does not
	 * lower: the shipped shader with its first OpFMul turned into OpFDiv on
	 * the same operands.
	 */
	broken = malloc(vertex_words * sizeof(*broken));
	assert(broken != NULL);
	memcpy(broken, vertex, vertex_words * sizeof(*broken));
	for (at = 5U;
	     at < vertex_words && (broken[at] & 0xffffU) != FIXTURE_SPIRV_FMUL;
	     at += broken[at] >> 16)
		assert((broken[at] >> 16) != 0U);
	assert(at < vertex_words);
	broken[at] = (broken[at] & 0xffff0000U) | FIXTURE_SPIRV_FDIV;

	/* Creates the three modules. */
	stub_wire_begin(&fixture_wire);
	fixture_shader_module(vertex, vertex_words, FIXTURE_VS);
	fixture_shader_module(fragment, fragment_words, FIXTURE_FS);
	fixture_shader_module(broken, vertex_words, FIXTURE_BAD_VS);
	reply_bytes = stub_execute_ok(&fixture_wire);
	assert(reply_bytes == 3U * 24U);
	free(broken);

	/*
	 * The pipeline with the unlowered vertex shader fails as a whole: a
	 * defined VkResult and a null identity, and no pipeline published.
	 */
	mark = stub_allocation_mark();
	stub_wire_begin(&fixture_wire);
	fixture_pipeline(FIXTURE_BAD_VS, FIXTURE_FS, FIXTURE_BAD_PIPELINE);
	reply_bytes = stub_execute_ok(&fixture_wire);
	assert(reply_bytes == 24U);
	assert(stub_get32(stub_reply, 0U) == FIXTURE_CREATE_GRAPHICS_PIPELINES);
	assert(stub_get32(stub_reply, 4U) == (uint32_t)VK_ERROR_FEATURE_NOT_PRESENT);
	assert(stub_get64(stub_reply, 8U) == 1U);
	assert(stub_get64(stub_reply, 16U) == 0U);
	pipeline = drv_i915_object_lookup(stub_vk, I915_VK_OBJ_PIPELINE, FIXTURE_BAD_PIPELINE);
	assert(pipeline == NULL);

	/*
	 * XXX: the refused pipeline's record is neither published nor freed
	 * (render/pipeline.c, "happy path only"): exactly that one block is
	 * left behind, and its kernels were released.  The fixture reclaims it
	 * so the rest of the run starts clean.
	 */
	assert(stub_live_since(mark) == 1U);
	assert(stub_release_since(mark) == 1U);

	/* The pipeline made from the shipped shaders: [65][VK_SUCCESS][count 1][identity]. */
	stub_wire_begin(&fixture_wire);
	fixture_pipeline(FIXTURE_VS, FIXTURE_FS, FIXTURE_PIPELINE);
	reply_bytes = stub_execute_ok(&fixture_wire);
	assert(reply_bytes == 24U);
	assert(stub_get32(stub_reply, 4U) == VK_SUCCESS);
	assert(stub_get64(stub_reply, 8U) == 1U);
	assert(stub_get64(stub_reply, 16U) == FIXTURE_PIPELINE);

	/* The pipeline holds both modules, its topology and two compiled kernels. */
	pipeline = drv_i915_object_lookup(stub_vk, I915_VK_OBJ_PIPELINE, FIXTURE_PIPELINE);
	assert(pipeline != NULL);
	assert(pipeline->vertex == drv_i915_object_lookup(stub_vk, I915_VK_OBJ_SHADER_MODULE, FIXTURE_VS));
	assert(pipeline->fragment == drv_i915_object_lookup(stub_vk, I915_VK_OBJ_SHADER_MODULE, FIXTURE_FS));
	assert(pipeline->topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	assert(pipeline->kernels_ready != 0);
	assert(pipeline->vs_binary != NULL);
	assert(pipeline->fs_binary != NULL);
	assert(pipeline->vs_binary->code_bytes != 0U);
	assert(pipeline->fs_binary->code_bytes != 0U);

	/* The draw takes the kernels' code and interface from the pipeline. */
	drv_i915_gfx_pipeline_kernels(pipeline, &kernels);
	assert(kernels.vs_code == pipeline->vs_binary->code);
	assert(kernels.ps_code == pipeline->fs_binary->code);
	assert(kernels.varyings == pipeline->vs_binary->varying_count);

	/* Emits the vertex and pixel shader state of those kernels into a batch. */
	memset(commands, 0, sizeof(commands));
	batch.cmds = commands;
	batch.count = 0U;
	batch.capacity = 256U;
	batch.overflow = 0;
	drv_i915_gfx_emit_vertex_shader(&batch, &kernels);
	drv_i915_gfx_emit_pixel_shader(&batch, &kernels);
	assert(batch.overflow == 0);

	/* 3DSTATE_VS starts the vertex kernel at its heap offset. */
	found = fixture_find_command(commands, batch.count, GEN12_CMD_3DSTATE_VS);
	assert(found >= 0);
	assert(commands[found + 1] == I915_GFX_VS_KERNEL);
	assert((commands[found + 6] >> 20) == kernels.vs_grf_start);

	/* 3DSTATE_PS is emitted for the pixel kernel. */
	found = fixture_find_command(commands, batch.count, GEN12_CMD_3DSTATE_PS);
	assert(found >= 0);

	/* vkDestroyPipeline releases the pipeline and its kernels; the modules go next. */
	stub_wire_begin(&fixture_wire);
	fixture_destroy(FIXTURE_DESTROY_PIPELINE, FIXTURE_PIPELINE);
	fixture_destroy(FIXTURE_DESTROY_SHADER_MODULE, FIXTURE_VS);
	fixture_destroy(FIXTURE_DESTROY_SHADER_MODULE, FIXTURE_FS);
	fixture_destroy(FIXTURE_DESTROY_SHADER_MODULE, FIXTURE_BAD_VS);
	reply_bytes = stub_execute_ok(&fixture_wire);
	assert(reply_bytes == 4U * 4U);
	pipeline = drv_i915_object_lookup(stub_vk, I915_VK_OBJ_PIPELINE, FIXTURE_PIPELINE);
	assert(pipeline == NULL);

	/* Closes the session; nothing stays allocated. */
	stub_session_close();
	assert(stub_live == 0U);
	free(vertex);
	free(fragment);
}
