/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host fixture for the pipeline module (p007) and the shader-module / graphics
 * pipeline wire decode (p011 increment C).  test_emit checks the retained
 * 3DSTATE emission; test_shader_module and test_graphics_pipeline drive
 * vkCreateShaderModule and vkCreateGraphicsPipelines through the router against
 * the real WS029 GEM allocator and the SPIR-V-to-GEN compiler, using the vkdemo
 * shaders.
 */

#include "../../ws029/tests/i915-fixture.inc"
#include "../../../src/drivers/gpu/i915/uncore.c"
#include "../../../src/drivers/gpu/i915/ggtt.c"
#include "../../../src/drivers/gpu/i915/ppgtt.c"
#include "../../../src/drivers/gpu/i915/gem.c"
#include "../../../src/drivers/gpu/i915/irq.c"
#include "../../../src/drivers/gpu/i915/engine.c"
#include "../../../src/drivers/gpu/i915/lrc.c"
#include "../../../src/drivers/gpu/i915/request.c"
#include "../../../src/drivers/gpu/i915/i915.c"
#include "../../../src/drivers/gpu/i915/vk/cmd.c"
#include "../../../src/drivers/gpu/i915/vk/spirv.c"
#include "../../../src/drivers/gpu/i915/vk/eu.c"
#include "../../../src/drivers/gpu/i915/vk/compile.c"
#include "../../../src/drivers/gpu/i915/vk/pipe.c"

static int fixture_pci_token;
#define fixture_pci_device	((struct drv_pci_device *)&fixture_pci_token)

/* The modules outside pipe are not exercised here; routing never reaches them. */
int
i915_vk_res_dispatch(struct i915_vk_session *s, uint32_t o, struct i915_vk_reader *r, struct i915_vk_writer *w)
{ (void)s; (void)o; (void)r; (void)w; return EINVAL; }
int
i915_vk_cmdbuf_dispatch(struct i915_vk_session *s, uint32_t o, struct i915_vk_reader *r, struct i915_vk_writer *w)
{ (void)s; (void)o; (void)r; (void)w; return EINVAL; }
int
i915_vk_sync_dispatch(struct i915_vk_session *s, uint32_t o, struct i915_vk_reader *r, struct i915_vk_writer *w)
{ (void)s; (void)o; (void)r; (void)w; return EINVAL; }
int
i915_vk_wsi_dispatch(struct i915_vk_session *s, uint32_t o, struct i915_vk_reader *r, struct i915_vk_writer *w)
{ (void)s; (void)o; (void)r; (void)w; return EINVAL; }

/* A little-endian encoder mirroring the libvulkan wire writer. */
static uint8_t wire[65536];
static size_t wire_len;
static void w32(uint32_t v) { wire[wire_len++] = (uint8_t)v; wire[wire_len++] = (uint8_t)(v >> 8); wire[wire_len++] = (uint8_t)(v >> 16); wire[wire_len++] = (uint8_t)(v >> 24); }
static void w64(uint64_t v) { w32((uint32_t)v); w32((uint32_t)(v >> 32)); }
static uint32_t rd32(const uint8_t *b, size_t o) { return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8) | ((uint32_t)b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24); }
static uint64_t rd64(const uint8_t *b, size_t o) { return (uint64_t)rd32(b, o) | ((uint64_t)rd32(b, o + 4) << 32); }

static int
find_command(const uint32_t *batch, uint32_t used, uint32_t opcode)
{
	uint32_t index;

	for (index = 0U; index < used; index++) {
		if ((batch[index] >> 16) == opcode)
			return (int)index;
	}
	return -1;
}

static struct i915_device *
attach(void)
{
	struct i915_device *device;
	int error;

	error = drv_i915_pci_driver_register();
	assert(error == 0);
	error = fixture_driver->attach(fixture_pci_device, &fixture_driver->ids[5]);
	assert(error == 0);
	device = fixture_driver_data;
	assert(device != NULL);
	error = fixture_service->publish(fixture_pci_device, fixture_service_argument);
	assert(error == 0);
	return device;
}

static size_t
run_command(struct i915_vk_session *session, uint8_t *reply, size_t reply_size)
{
	struct i915_vk_reader reader;
	struct i915_vk_writer writer;
	int error;

	reader.base = wire;
	reader.size = wire_len;
	reader.offset = 0U;
	reader.error = 0;
	writer.base = reply;
	writer.size = reply_size;
	writer.offset = 0U;
	writer.error = 0;
	error = i915_vk_cmd_dispatch(session, &reader, &writer);
	assert(error == 0);
	assert(writer.error == 0);
	return writer.offset;
}

/* Loads one vkdemo SPIR-V shader into a word buffer the caller frees. */
static uint32_t *
load_spv(const char *name, size_t *words)
{
	char path[512];
	FILE *file;
	long size;
	uint32_t *code;

	snprintf(path, sizeof(path), "%s/userland/base/vkdemo/shaders/%s", VK_REPO, name);
	file = fopen(path, "rb");
	assert(file != NULL);
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	assert(size > 0 && (size % 4) == 0);
	code = malloc((size_t)size);
	assert(code != NULL);
	assert(fread(code, 1, (size_t)size, file) == (size_t)size);
	fclose(file);
	*words = (size_t)size / 4U;
	return code;
}

/* Encodes a vkCreateShaderModule command for the given SPIR-V and wire id. */
static void
encode_shader_module(const uint32_t *code, size_t words, uint64_t handle)
{
	size_t index;

	wire_len = 0;
	w32(59U); w32(1U);			/* opcode, reply flag */
	w64(0xD0U);				/* device */
	w64(1U);				/* pCreateInfo present */
	w32(16U);				/* VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO */
	w64(0U);				/* pNext present */
	w32(0U);				/* flags */
	w64((uint64_t)words * 4U);		/* codeSize bytes */
	w64((uint64_t)words);			/* word count */
	for (index = 0U; index < words; index++)
		w32(code[index]);
	w64(0U);				/* pAllocator present */
	w64(1U);				/* pShaderModule present */
	w64(handle);				/* module wire id */
}

/* Appends one VkPipelineShaderStageCreateInfo naming a module for a stage. */
static void
encode_stage(uint32_t stage, uint64_t module)
{
	w32(18U);				/* VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO */
	w64(0U);				/* pNext present */
	w32(0U);				/* flags */
	w32(stage);				/* VkShaderStageFlagBits */
	w64(module);				/* module handle */
	w64(5U);				/* pName length ("main\0") */
	w32(0x6e69616dU);			/* "main" */
	w32(0x00000000U);			/* terminator + padding */
	w64(0U);				/* pSpecializationInfo present */
}

static void
test_graphics_pipeline(void)
{
	struct i915_device *device;
	struct i915_vk_device vk;
	struct i915_vk_session session;
	struct i915_vk_pipeline *pipeline;
	uint32_t *vs;
	uint32_t *fs;
	size_t vs_words;
	size_t fs_words;
	void *gpu_session;
	uint8_t reply[64];
	const uint64_t vs_h = 0xD00ULL;
	const uint64_t fs_h = 0xD01ULL;
	const uint64_t pipe_h = 0xE00ULL;
	int error;

	vs = load_spv("cuboid.vert.spv", &vs_words);
	fs = load_spv("cuboid.frag.spv", &fs_words);

	fixture_reset();
	device = attach();
	error = fixture_gpu_ops->open(device, &gpu_session);
	assert(error == 0);
	memset(&vk, 0, sizeof(vk));
	vk.i915 = device;
	error = i915_vk_object_table_create(&vk.objects);
	assert(error == 0);
	memset(&session, 0, sizeof(session));
	session.vk = &vk;
	session.gpu = gpu_session;

	/* A small SPIR-V blob round-trips as a shader module first. */
	{
		struct i915_vk_shader_module *probe;
		const uint64_t mh = 0xC00ULL;
		const uint32_t small[3] = {0x07230203U, 0x00010000U, 0x0badf00dU};
		unsigned index;

		encode_shader_module(small, 3U, mh);
		assert(run_command(&session, reply, sizeof(reply)) == 24U);
		assert(rd32(reply, 0U) == 59U);
		assert(rd64(reply, 16U) == mh);
		probe = i915_vk_obj_lookup(&vk, I915_VK_OBJ_SHADER_MODULE, mh);
		assert(probe != NULL && probe->words == 3U);
		for (index = 0U; index < 3U; index++)
			assert(probe->code[index] == small[index]);
		wire_len = 0;
		w32(60U); w32(1U); w64(0xD0U); w64(mh); w64(0U);
		assert(run_command(&session, reply, sizeof(reply)) == 4U);
		assert(i915_vk_obj_lookup(&vk, I915_VK_OBJ_SHADER_MODULE, mh) == NULL);
	}

	/* Both stages are stored as shader modules first. */
	encode_shader_module(vs, vs_words, vs_h);
	assert(run_command(&session, reply, sizeof(reply)) == 24U);
	encode_shader_module(fs, fs_words, fs_h);
	assert(run_command(&session, reply, sizeof(reply)) == 24U);

	/* vkCreateGraphicsPipelines for one pipeline referencing both stages. */
	wire_len = 0;
	w32(65U); w32(1U);			/* opcode, reply flag */
	w64(0xD0U);				/* device */
	w64(0U);				/* pipeline cache */
	w32(1U);				/* count */
	w64(1U);				/* count */
	/* VkGraphicsPipelineCreateInfo */
	w32(28U);				/* sType */
	w64(0U);				/* pNext present */
	w32(0U);				/* flags */
	w32(2U);				/* stageCount */
	w64(2U);				/* stageCount */
	encode_stage(0x1U, vs_h);		/* vertex */
	encode_stage(0x10U, fs_h);		/* fragment */
	w64(0U);				/* vertex input absent */
	w64(1U);				/* input assembly present */
	w32(20U); w64(0U); w32(0U); w32(3U); w32(0U);	/* sType,pNext,flags,topology=TRIANGLE_LIST,primRestart */
	w64(0U);				/* tessellation absent */
	w64(0U);				/* viewport absent */
	w64(1U);				/* rasterization present (always) */
	{
		int word;
		for (word = 0; word < 14; word++)
			w32(0U);		/* rasterization state, discard disabled */
	}
	w64(0U);				/* multisample absent */
	w64(0U);				/* depth-stencil absent */
	w64(0U);				/* color blend absent */
	w64(0U);				/* dynamic absent */
	w64(0U);				/* layout */
	w64(0U);				/* renderPass */
	w32(0U);				/* subpass */
	w64(0U);				/* base pipeline */
	w32(0U);				/* base index */
	/* Output identities follow the batch. */
	w64(0U);				/* reserved */
	w64(1U);				/* identity count */
	w64(pipe_h);				/* pipeline wire id */

	assert(run_command(&session, reply, sizeof(reply)) == 24U);
	assert(rd32(reply, 0U) == 65U);		/* echoed opcode */
	assert(rd32(reply, 4U) == 0U);		/* VK_SUCCESS */
	assert(rd64(reply, 8U) == 1U);		/* returned count */
	assert(rd64(reply, 16U) == pipe_h);	/* identity */

	pipeline = i915_vk_obj_lookup(&vk, I915_VK_OBJ_PIPELINE, pipe_h);
	assert(pipeline != NULL);
	assert(pipeline->vs_kernel != 0U && pipeline->fs_kernel != 0U);
	assert(pipeline->vs_code != NULL && pipeline->fs_code != NULL);
	assert(pipeline->topology == 3U);

	/* vkDestroyPipeline releases the pipeline and its shader-code buffers. */
	wire_len = 0;
	w32(67U); w32(1U); w64(0xD0U); w64(pipe_h); w64(0U);
	assert(run_command(&session, reply, sizeof(reply)) == 4U);
	assert(i915_vk_obj_lookup(&vk, I915_VK_OBJ_PIPELINE, pipe_h) == NULL);

	/* Free the shader modules. */
	wire_len = 0;
	w32(60U); w32(1U); w64(0xD0U); w64(vs_h); w64(0U);
	assert(run_command(&session, reply, sizeof(reply)) == 4U);
	wire_len = 0;
	w32(60U); w32(1U); w64(0xD0U); w64(fs_h); w64(0U);
	assert(run_command(&session, reply, sizeof(reply)) == 4U);

	i915_vk_object_table_destroy(vk.objects);
	fixture_gpu_ops->close(device, gpu_session);
	free(vs);
	free(fs);
	printf("i915 vk pipe graphics pipeline wire ok\n");
}

/* Emits a pipeline built directly and checks the retained 3DSTATE. */
static void
test_emit(void)
{
	uint32_t buffer[256];
	struct i915_vk_batch batch;
	struct i915_vk_shader_binary vs;
	struct i915_vk_shader_binary fs;
	struct i915_vk_pipeline_info info;
	struct i915_vk_pipeline *pipeline;
	int error;

	memset(buffer, 0, sizeof(buffer));
	batch.object = NULL;
	batch.map = buffer;
	batch.cursor = 0U;
	batch.capacity = 256U;
	batch.error = 0;

	memset(&vs, 0, sizeof(vs));
	memset(&fs, 0, sizeof(fs));
	vs.grf_used = 24U;
	fs.grf_used = 20U;
	memset(&info, 0, sizeof(info));
	info.vs = &vs;
	info.fs = &fs;
	info.vs_kernel = 0x00100000ULL;
	info.fs_kernel = 0x00200000ULL;
	error = i915_vk_pipeline_create(NULL, &info, &pipeline);
	assert(error == 0);

	i915_vk_pipeline_emit(pipeline, &batch);
	assert(find_command(buffer, batch.cursor, GEN12_CMD_3DSTATE_VS) >= 0);
	assert(find_command(buffer, batch.cursor, GEN12_CMD_3DSTATE_PS) >= 0);

	i915_vk_pipeline_destroy(pipeline);
	printf("i915 vk pipe emit ok\n");
}

int
main(void)
{
	test_emit();
	test_graphics_pipeline();
	printf("i915 vk pipe host test PASS\n");
	return 0;
}
