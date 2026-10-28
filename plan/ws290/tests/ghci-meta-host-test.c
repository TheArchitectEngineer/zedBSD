/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the driver's own transfer programs (vk/vk-meta.c and
 * the transfers of vk/vk-execute.c that use them) against the GPU model,
 * the kernel stubs and amaterrace-gpu's GSI1 model (design section 11.6
 * and the scenes of p010): on the no-blt profile, the packets of each
 * transfer (BIND_PIPELINE, the resource table, LOAD_CONST, DISPATCH) and
 * the bytes of the program's pipeline state and descriptors; every
 * dispatch the GPU model took is run on gsi1model.py (ghci-meta-run.py)
 * with the memory it reaches, its writes are put back into the GPU
 * model's memory, and the memory is compared with what the Vulkan
 * commands make of it, written here by hand from the bytes before (the
 * bytes at the ends of a copy, rows sharing words, one aspect of a depth
 * and stencil image, query results packed into 32 and 64 bits); the
 * command buffer's binds placed again after a transfer; the transfers the
 * device cannot make without the blitter; and the copy of query results
 * on the profiles with and without the blitter.
 *
 * It is compiled within ghci-pipeline-host-test.c (which includes it at
 * its end, after ghci-execute-host-test.c, for both files' helpers), and
 * runs when the binary is given "meta" with the directory of the
 * tests, the one of the GSI1 tools and a new directory for the model's
 * files.
 */

/* The identities of the scenes' own objects. */
#define ID_M_R8				0x3000U
#define ID_M_D24S8			0x3001U
#define ID_M_D32S8			0x3002U
#define ID_M_QUERIES			0x3100U

/* Where the scenes bind their images in the memory. */
#define M_R8_OFFSET			0x50000U
#define M_D24S8_OFFSET			0x58000U
#define M_D32S8_OFFSET			0x60000U

/* The scene's memory (ID_MEMORY) and the buffer's offset in it. */
#define M_MEMORY_BYTES			(1024U * 1024U)
#define M_BUFFER			0x80000U

/*
 * The GSI1 model's data image: 4096 words at 0x8000, the first 256 the
 * constant bank (gsi1model.py); the test puts the resource table at
 * 0x8400, the source at 0x8800 and the destination at 0xa000.
 */
#define M_DATA_BASE			0x8000U
#define M_DATA_WORDS			4096U
#define M_BANK_WORDS			256U
#define M_TABLE				0x8400U
#define M_SOURCE			0x8800U
#define M_DESTINATION			0xa000U
#define M_DATA_END			(M_DATA_BASE + M_DATA_WORDS * 4U)

/* The directories of the run: the tests' (ghci-meta-run.py), the GSI1 tools', and the one the model's files go to. */
static const char *meta_tests;
static const char *meta_tools;
static const char *meta_out;

/* The dispatches run on the model so far, which names each one's files. */
static unsigned meta_runs;

static int meta_main(const char *directory, const char *compiler_directory, const char *tests, const char *tools, const char *out);
static uint8_t *m_memory(struct node *node);
static uint8_t *m_at(uint32_t session, uint64_t va, uint64_t bytes);
static int m_image(struct node *node, uint64_t identity, uint32_t format, uint64_t offset);
static int m_run(struct node *node, unsigned mark, unsigned *dispatches);
static int m_dispatch(uint32_t session, const struct ghci_vk_meta_pipeline *pipeline, uint64_t table, const uint32_t *bank, uint32_t bank_words, uint32_t groups);
static int m_same(const uint8_t *memory, const uint8_t *reference, const char *name);
static void m_fill(uint8_t *bytes, uint32_t count, uint32_t seed);
static void m_region(uint64_t offset, uint32_t row_length, uint32_t height, uint32_t aspect, uint32_t level, uint32_t layer, int32_t x, int32_t y, uint32_t width, uint32_t rows);
static void scene_m_buffers(void);
static void scene_m_images(void);
static void scene_m_queries(enum model_profile profile);
static void scene_m_binds(void);
static void scene_m_refusals(void);
static void m_copy_image(uint64_t source, uint32_t source_aspect, uint64_t destination, uint32_t destination_aspect);
static void scene_m_draws(void);
static void m_draws_record(void);
static void scene_m_subpass(void);

/* Runs the scenes of the transfer programs and reports.  Returns the exit status. */
static int
meta_main(
	const char *directory,
	const char *compiler_directory,
	const char *tests,
	const char *tools,
	const char *out)
{
	int error;

	/* The modules and the directories. */
	error = load_modules(directory, compiler_directory);
	if (error != 0) {
		printf("FAIL ghci-meta-host-test: the SPIR-V modules are not there\n");
		return 1;
	}
	meta_tests = tests;
	meta_tools = tools;
	meta_out = out;

	/* The model knows the formats of the scenes' images. */
	model_more_formats = 1;

	/* Keeps the driver's log out of the report. */
	stub.quiet = 1;

	/* The scenes. */
	scene_m_buffers();
	scene_m_images();
	scene_m_queries(MODEL_NO_BLT);
	scene_m_queries(MODEL_FULL);
	scene_m_binds();
	scene_m_refusals();
	scene_m_draws();
	scene_m_subpass();
	model_fini();

	/* The counts. */
	printf("ghci-meta-host-test: %u passed, %u failed, %u dispatches on the GSI1 model\n", checks_passed, checks_failed, meta_runs);
	if (checks_failed != 0U) {
		printf("FAIL ghci-meta-host-test\n");
		return 1;
	}

	/* Succeeded: every check held. */
	printf("PASS ghci-meta-host-test\n");
	return 0;
}

/* Reports the CPU's view of the scene's whole memory (NULL when it is not one run of the model's memory). */
static uint8_t *
m_memory(
	struct node *node)
{
	const struct ghci_vk_session *session;

	session = session_of(node);
	if (session == NULL || session->devices == NULL)
		return NULL;
	return m_at(session->devices->vm->session, x_va(node, 0U), M_MEMORY_BYTES);
}

/* Reports the CPU's view of bytes at a session's address (NULL unless one mapping holds them all). */
static uint8_t *
m_at(
	uint32_t session,
	uint64_t va,
	uint64_t bytes)
{
	const struct model_mapping *mapping;

	mapping = model_lookup(session, va);
	if (mapping == NULL || va + bytes > mapping->va + mapping->bytes)
		return NULL;
	return model_dma(mapping->phys + (va - mapping->va), bytes);
}

/* Makes a 64 x 32 image of one level and layer of a format, for transfers, bound at offset.  Returns 0, or 1. */
static int
m_image(
	struct node *node,
	uint64_t identity,
	uint32_t format,
	uint64_t offset)
{
	int error;

	begin(node, GPU_OP_CREATE_IMAGE);
	put64(ID_DEVICE);
	put64(1U);
	put32(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
	put64(0U);
	put32(0U);
	put32(VK_IMAGE_TYPE_2D);
	put32(format);
	put32(64U);
	put32(32U);
	put32(1U);
	put32(1U);
	put32(1U);
	put32(VK_SAMPLE_COUNT_1_BIT);
	put32(VK_IMAGE_TILING_OPTIMAL);
	put32(VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	put32(VK_SHARING_MODE_EXCLUSIVE);
	put32(0U);
	put64(0U);
	put32(VK_IMAGE_LAYOUT_UNDEFINED);
	tail(identity);
	error = run(node);
	if (error != 0 || reply32(node, 4U) != 0U)
		return 1;
	begin(node, GPU_OP_BIND_IMAGE_MEMORY);
	put64(ID_DEVICE);
	put64(identity);
	put64(ID_MEMORY);
	put64(offset);
	error = run(node);
	if (error != 0 || reply32(node, 4U) != 0U)
		return 1;
	return 0;
}

/*
 * Runs on the GSI1 model every dispatch of the driver's transfer
 * programs the GPU model took after mark, in order, each with the
 * program, table and constants bound before it, and puts each one's
 * writes into the GPU model's memory.  Returns 0 with the count, or 1.
 */
static int
m_run(
	struct node *node,
	unsigned mark,
	unsigned *dispatches)
{
	const struct ghci_vk_meta_pipeline *pipeline;
	const struct model_command *command;
	struct ghci_vk_device *vk_device;
	uint32_t bank[M_BANK_WORDS];
	uint32_t bank_words;
	uint64_t table;
	uint64_t pso;
	unsigned index;
	unsigned kind;
	int error;

	/* The device's placed programs. */
	*dispatches = 0U;
	vk_device = session_of(node)->devices;
	pipeline = NULL;
	table = 0U;
	bank_words = 0U;
	memset(bank, 0, sizeof(bank));

	/* Each packet in turn: the binds, then each dispatch of a transfer program. */
	for (index = mark; index < model.command_count; index++) {
		command = &model.commands[index];
		switch (command->opcode) {
		case GHCI_OP_BIND_PIPELINE:
			pso = command->words[1] | ((uint64_t)command->words[2] << 32);
			pipeline = NULL;
			for (kind = 0U; kind < GHCI_VK_META_KINDS; kind++) {
				if (vk_device->meta[kind].pso_va == pso)
					pipeline = &vk_device->meta[kind];
			}
			break;
		case GHCI_OP_BIND_DESCRIPTOR_TABLE:
			if ((command->words[3] & 0xffffU) == GSI1_ABI_SET_RESOURCES)
				table = command->words[1] | ((uint64_t)command->words[2] << 32);
			break;
		case GHCI_OP_LOAD_CONST:
			if (command->words[1] + command->words[2] <= M_BANK_WORDS && command->words[2] + 3U <= MODEL_COMMAND_WORDS) {
				memcpy(bank + command->words[1], &command->words[3], command->words[2] * 4U);
				if (command->words[1] + command->words[2] > bank_words)
					bank_words = command->words[1] + command->words[2];
			}
			break;
		case GHCI_OP_DISPATCH:
			if (pipeline == NULL)
				break;
			error = m_dispatch(command->session, pipeline, table, bank, bank_words, command->words[1]);
			if (error != 0)
				return 1;
			(*dispatches)++;
			break;
		default:
			break;
		}
	}
	return 0;
}

/*
 * Runs one dispatch on the GSI1 model: the code at the placed program's
 * address, the constant bank, the resource table with its two
 * descriptors moved to the model's addresses, the source's and the
 * destination's bytes; then the destination's bytes the model left go
 * back to the GPU model's memory.  Returns 0, or 1.
 */
static int
m_dispatch(
	uint32_t session,
	const struct ghci_vk_meta_pipeline *pipeline,
	uint64_t table,
	const uint32_t *bank,
	uint32_t bank_words,
	uint32_t groups)
{
	static uint8_t data[M_DATA_WORDS * 4U];
	char prefix[512];
	char path[600];
	char command[2048];
	const uint8_t *code;
	const uint8_t *descriptors;
	uint8_t *source;
	uint8_t *destination;
	uint64_t source_base;
	uint64_t destination_base;
	uint32_t source_bytes;
	uint32_t destination_bytes;
	uint32_t word;
	uint32_t index;
	FILE *file;
	int status;

	/* The program's code and table, and the two buffers the table gives. */
	code = m_at(session, pipeline->code_va, pipeline->code_words * 4U);
	descriptors = m_at(session, table, pipeline->resource_bytes);
	if (code == NULL || descriptors == NULL) {
		printf("  meta: no code or table at the dispatch\n");
		return 1;
	}
	source_base = x_load(descriptors + pipeline->source_entry + GSI1_ABI_BUFFER_BASE, 8U);
	source_bytes = (uint32_t)x_load(descriptors + pipeline->source_entry + GSI1_ABI_BUFFER_SIZE, 4U);
	destination_base = x_load(descriptors + pipeline->destination_entry + GSI1_ABI_BUFFER_BASE, 8U);
	destination_bytes = (uint32_t)x_load(descriptors + pipeline->destination_entry + GSI1_ABI_BUFFER_SIZE, 4U);
	source = m_at(session, source_base, source_bytes);
	destination = m_at(session, destination_base, destination_bytes);
	if (source == NULL || destination == NULL || M_SOURCE + source_bytes > M_DESTINATION || M_DESTINATION + destination_bytes > M_DATA_END ||
	    pipeline->resource_bytes > M_SOURCE - M_TABLE || bank_words > 24U) {
		printf("  meta: a dispatch past what the model's image holds (source %u, destination %u bytes)\n", source_bytes, destination_bytes);
		return 1;
	}

	/* The data image: the bank (c28 the table's address), the table, the source and the destination. */
	memset(data, 0, sizeof(data));
	memcpy(data, bank, bank_words * 4U);
	word = M_TABLE;
	memcpy(data + GSI1_ABI_BANK_SET(GSI1_ABI_SET_RESOURCES) * 4U, &word, 4U);
	memcpy(data + (M_TABLE - M_DATA_BASE), descriptors, pipeline->resource_bytes);
	memset(data + (M_TABLE - M_DATA_BASE) + pipeline->source_entry + GSI1_ABI_BUFFER_BASE, 0, 8U);
	memset(data + (M_TABLE - M_DATA_BASE) + pipeline->destination_entry + GSI1_ABI_BUFFER_BASE, 0, 8U);
	word = M_SOURCE;
	memcpy(data + (M_TABLE - M_DATA_BASE) + pipeline->source_entry + GSI1_ABI_BUFFER_BASE, &word, 4U);
	word = M_DESTINATION;
	memcpy(data + (M_TABLE - M_DATA_BASE) + pipeline->destination_entry + GSI1_ABI_BUFFER_BASE, &word, 4U);
	memcpy(data + (M_SOURCE - M_DATA_BASE), source, source_bytes);
	memcpy(data + (M_DESTINATION - M_DATA_BASE), destination, destination_bytes);

	/* The files: the code, the data image. */
	snprintf(prefix, sizeof(prefix), "%s/dispatch-%03u", meta_out, meta_runs);
	meta_runs++;
	snprintf(path, sizeof(path), "%s.bin", prefix);
	file = fopen(path, "wb");
	if (file == NULL)
		return 1;
	fwrite(code, 4U, pipeline->code_words, file);
	fclose(file);
	snprintf(path, sizeof(path), "%s.data.hex", prefix);
	file = fopen(path, "w");
	if (file == NULL)
		return 1;
	for (index = 0U; index < M_DATA_WORDS; index++)
		fprintf(file, "%08x\n", (unsigned)x_load(data + index * 4U, 4U));
	fclose(file);

	/* The model. */
	snprintf(command, sizeof(command), "python3 -I '%s/ghci-meta-run.py' '%s' '%s' %u", meta_tests, meta_tools, prefix, groups);
	status = system(command);
	if (status != 0) {
		printf("  meta: %s did not run (status %d)\n", prefix, status);
		return 1;
	}

	/* The memory after it: the destination's bytes go back. */
	snprintf(path, sizeof(path), "%s.mem", prefix);
	file = fopen(path, "r");
	if (file == NULL)
		return 1;
	for (index = 0U; index < M_DATA_WORDS; index++) {
		if (fscanf(file, "%x", &word) != 1) {
			fclose(file);
			return 1;
		}
		memcpy(data + index * 4U, &word, 4U);
	}
	fclose(file);
	memcpy(destination, data + (M_DESTINATION - M_DATA_BASE), destination_bytes);
	return 0;
}

/* Reports whether the scene's memory is the reference, and the first byte that differs. */
static int
m_same(
	const uint8_t *memory,
	const uint8_t *reference,
	const char *name)
{
	uint32_t index;

	for (index = 0U; index < M_MEMORY_BYTES; index++) {
		if (memory[index] != reference[index]) {
			printf("  %s: memory byte 0x%x is %02x, expected %02x\n", name, index, memory[index], reference[index]);
			return 0;
		}
	}
	return 1;
}

/* Fills bytes with a pattern of their own (no two neighbours equal). */
static void
m_fill(
	uint8_t *bytes,
	uint32_t count,
	uint32_t seed)
{
	uint32_t index;

	for (index = 0U; index < count; index++)
		bytes[index] = (uint8_t)(seed + index * 7U + (index >> 8) * 3U);
}

/* Appends one buffer-image region: [u64 offset][row length][image height][layers][offset][extent 1 deep]. */
static void
m_region(
	uint64_t offset,
	uint32_t row_length,
	uint32_t height,
	uint32_t aspect,
	uint32_t level,
	uint32_t layer,
	int32_t x,
	int32_t y,
	uint32_t width,
	uint32_t rows)
{
	put64(offset);
	put32(row_length);
	put32(height);
	put32(aspect);
	put32(level);
	put32(layer);
	put32(1U);
	put32((uint32_t)x);
	put32((uint32_t)y);
	put32(0U);
	put32(width);
	put32(rows);
	put32(1U);
}

/*
 * Buffers without the blitter: three copies in one command (13 bytes
 * whose ends and offsets within a word differ: two bytes, two words read
 * across words, three bytes; 64 bytes of whole words; one byte), a fill
 * and an update.  The packets of the first dispatch by hand, every
 * dispatch on the model, and the memory against the bytes by hand.
 */
static void
scene_m_buffers(void)
{
	static uint8_t reference[M_MEMORY_BYTES];
	const struct model_command *command;
	const uint8_t *bytes;
	const uint8_t *pso;
	uint8_t *memory;
	uint64_t buffer_va;
	uint32_t ops[X_OPS_MAX];
	uint32_t index;
	unsigned first;
	unsigned count;
	unsigned mark;
	unsigned dispatches;
	unsigned kind;
	struct node node;
	int error;

	/* The device, and the buffer: a pattern from 1000, 0xee below 600, 0x55 from 2000. */
	error = x_open(&node, MODEL_NO_BLT);
	memory = m_memory(&node);
	expect(error == 0 && memory != NULL, "meta buffers: device and memory");
	if (error != 0 || memory == NULL) {
		node_close(&node);
		stop("meta buffers");
		return;
	}
	buffer_va = x_va(&node, M_BUFFER);
	m_fill(memory + M_BUFFER + 1000U, 400U, 0x11U);
	memset(memory + M_BUFFER, 0xee, 600U);
	memset(memory + M_BUFFER + 2000U, 0x55, 2000U);

	/* The commands. */
	x_record(&node, ID_X_COMMANDS);
	x_command(GPU_OP_CMD_COPY_BUFFER, ID_X_COMMANDS);
	put64(ID_BUFFER);
	put64(ID_BUFFER);
	put32(3U);
	put64(3U);
	put64(1001U);
	put64(6U);
	put64(13U);
	put64(1100U);
	put64(200U);
	put64(64U);
	put64(1300U);
	put64(403U);
	put64(1U);
	x_command(GPU_OP_CMD_FILL_BUFFER, ID_X_COMMANDS);
	put64(ID_BUFFER);
	put64(2048U);
	put64(100U);
	put32(0xa1b2c3d4U);
	x_command(GPU_OP_CMD_UPDATE_BUFFER, ID_X_COMMANDS);
	put64(ID_BUFFER);
	put64(3000U);
	put64(12U);
	put64(12U);
	for (index = 0U; index < 3U; index++)
		put32(0x04030201U + index * 0x04040404U);
	error = x_end(&node, ID_X_COMMANDS);

	/* What the commands make of the memory, by hand. */
	memcpy(reference, memory, M_MEMORY_BYTES);
	memcpy(reference + M_BUFFER + 6U, memory + M_BUFFER + 1001U, 13U);
	memcpy(reference + M_BUFFER + 200U, memory + M_BUFFER + 1100U, 64U);
	reference[M_BUFFER + 403U] = memory[M_BUFFER + 1300U];
	for (index = 0U; index < 100U; index += 4U) {
		reference[M_BUFFER + 2048U + index] = 0xd4U;
		reference[M_BUFFER + 2048U + index + 1U] = 0xc3U;
		reference[M_BUFFER + 2048U + index + 2U] = 0xb2U;
		reference[M_BUFFER + 2048U + index + 3U] = 0xa1U;
	}
	for (index = 0U; index < 12U; index++)
		reference[M_BUFFER + 3000U + index] = (uint8_t)(index + 1U);

	/* The packets: seven dispatches of the copy program (2 bytes, 2 words, 3 bytes; 16 words; a byte; the fill; the update). */
	mark = model.command_count;
	error |= x_submit(&node, ID_X_COMMANDS, 0U);
	count = x_ops(mark, &first, ops);
	expect(error == 0 && count == 2U + 7U * 4U + 3U && model_count_commands(GHCI_OP_COPY_BUFFER) == 0U &&
	       model_count_commands(GHCI_OP_FILL_BUFFER) == 0U && ops[2] == GHCI_OP_BIND_PIPELINE &&
	       ops[3] == GHCI_OP_BIND_DESCRIPTOR_TABLE && ops[4] == GHCI_OP_LOAD_CONST && ops[5] == GHCI_OP_DISPATCH &&
	       ops[2U + 6U * 4U + 3U] == GHCI_OP_DISPATCH, "meta buffers: seven dispatches, no blitter packets");
	if (count != 2U + 7U * 4U + 3U) {
		node_close(&node);
		stop("meta buffers");
		return;
	}

	/*
	 * The first dispatch, by hand: the copy program's pipeline state (cs_code
	 * at 0x10, the workgroup at 0x38 of ghci.md section 8.2's record: 64 x 1 x
	 * 1); its table's source (the word of byte 1001, 4 bytes, stride and format
 * 0, flags ReadOnly at 0x0f of section 8.3's descriptor) and
	 * destination (the word of byte 6, 4 bytes); c0-c13: 2 elements of a row
	 * of 2, one row, mask 0xff, the source from byte 1 of its word, 1 apart,
	 * the destination from byte 2 of its word, 1 apart, 1 byte, the first 0;
	 * one workgroup.
	 */
	kind = GHCI_VK_META_COPY;
	command = &model.commands[first + 2U];
	pso = x_gpu(command->session, command->words[1] | ((uint64_t)command->words[2] << 32));
	expect(pso != NULL && x_load(pso + 0x10U, 8U) == session_of(&node)->devices->meta[kind].code_va && x_load(pso + 0x00U, 8U) == 0U &&
	       x_load(pso + 0x38U, 4U) == (64U | (1U << 10) | (1U << 20)), "meta buffers: the copy program's compute pipeline state");
	command = &model.commands[first + 3U];
	bytes = x_gpu(command->session, command->words[1] | ((uint64_t)command->words[2] << 32));
	expect(bytes != NULL && (command->words[3] & 0xffffU) == 2U &&
	       x_load(bytes + session_of(&node)->devices->meta[kind].source_entry, 8U) == buffer_va + 1000U &&
	       x_load(bytes + session_of(&node)->devices->meta[kind].source_entry + 8U, 4U) == 4U &&
	       x_load(bytes + session_of(&node)->devices->meta[kind].source_entry + 12U, 4U) == 0x01000000U &&
	       x_load(bytes + session_of(&node)->devices->meta[kind].destination_entry, 8U) == buffer_va + 4U &&
	       x_load(bytes + session_of(&node)->devices->meta[kind].destination_entry + 8U, 4U) == 4U,
	       "meta buffers: set 2 holds the source's and the destination's words");
	command = &model.commands[first + 4U];
	expect(command->words[1] == 0U && command->words[2] == 14U && command->words[3] == 2U && command->words[4] == 2U &&
	       command->words[5] == 1U && command->words[6] == 0xffU && command->words[7] == 1U && command->words[8] == 1U &&
	       command->words[11] == 2U && command->words[12] == 1U && command->words[15] == 1U,
	       "meta buffers: LOAD_CONST c0-c13 of the two head bytes");
	command = &model.commands[first + 5U];
	expect(command->words[1] == 1U && command->words[2] == 1U && command->words[3] == 1U, "meta buffers: one workgroup");

	/* Every dispatch on the GSI1 model, then the memory. */
	error = m_run(&node, mark, &dispatches);
	expect(error == 0 && dispatches == 7U, "meta buffers: the seven dispatches ran on the GSI1 model");
	expect(m_same(memory, reference, "meta buffers"), "meta buffers: the bytes copied, filled and updated, the bytes around them kept");
	node_close(&node);
	stop("meta buffers");
}

/*
 * Images without the blitter: a buffer to level 1 of layer 1 of an
 * R8G8B8A8 image (rows of 7 texels in the buffer), an R8 image to rows of
 * 5 bytes (rows sharing words), the stencil of a D24S8 image from a
 * buffer of bytes, its depth to a buffer of 4-byte texels (the high byte
 * of each kept), the depth of a D32S8 image from a buffer, and an image
 * copy between R8G8B8A8 images (a blit of R8G8B8A8 is a draw, which the
 * GSI1 model does not run: its packets are scene_m_draws').
 */
static void
scene_m_images(void)
{
	static uint8_t reference[M_MEMORY_BYTES];
	uint8_t *memory;
	uint32_t x;
	uint32_t y;
	unsigned mark;
	unsigned dispatches;
	struct node node;
	int error;

	/* The device, the images, and patterns in them and the buffer. */
	error = x_open(&node, MODEL_NO_BLT);
	error |= m_image(&node, ID_M_R8, VK_FORMAT_R8_UNORM, M_R8_OFFSET);
	error |= m_image(&node, ID_M_D24S8, VK_FORMAT_D24_UNORM_S8_UINT, M_D24S8_OFFSET);
	error |= m_image(&node, ID_M_D32S8, VK_FORMAT_D32_SFLOAT_S8_UINT, M_D32S8_OFFSET);
	memory = m_memory(&node);
	expect(error == 0 && memory != NULL, "meta images: device, images and memory");
	if (error != 0 || memory == NULL) {
		node_close(&node);
		stop("meta images");
		return;
	}
	m_fill(memory, 0x6000U, 0x21U);
	m_fill(memory + X_ATTACHMENT_OFFSET, 0x2000U, 0x99U);
	m_fill(memory + M_R8_OFFSET, 0x800U, 0x45U);
	m_fill(memory + M_D24S8_OFFSET, 0x2000U, 0x67U);
	m_fill(memory + M_D32S8_OFFSET, 0x4000U, 0x89U);
	m_fill(memory + M_BUFFER, 8192U, 0x3bU);

	/* The commands. */
	x_record(&node, ID_X_COMMANDS);
	x_command(GPU_OP_CMD_COPY_BUFFER_TO_IMAGE, ID_X_COMMANDS);
	put64(ID_BUFFER);
	put64(ID_IMAGE);
	put32(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	put32(1U);
	put64(1U);
	m_region(0U, 7U, 0U, VK_IMAGE_ASPECT_COLOR_BIT, 1U, 1U, 3, 2, 5U, 4U);
	x_command(GPU_OP_CMD_COPY_IMAGE_TO_BUFFER, ID_X_COMMANDS);
	put64(ID_M_R8);
	put32(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	put64(ID_BUFFER);
	put32(1U);
	put64(1U);
	m_region(4001U, 0U, 0U, VK_IMAGE_ASPECT_COLOR_BIT, 0U, 0U, 1, 1, 5U, 3U);
	x_command(GPU_OP_CMD_COPY_BUFFER_TO_IMAGE, ID_X_COMMANDS);
	put64(ID_BUFFER);
	put64(ID_M_D24S8);
	put32(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	put32(1U);
	put64(1U);
	m_region(5000U, 0U, 0U, VK_IMAGE_ASPECT_STENCIL_BIT, 0U, 0U, 2, 0, 6U, 2U);
	x_command(GPU_OP_CMD_COPY_IMAGE_TO_BUFFER, ID_X_COMMANDS);
	put64(ID_M_D24S8);
	put32(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	put64(ID_BUFFER);
	put32(1U);
	put64(1U);
	m_region(5200U, 0U, 0U, VK_IMAGE_ASPECT_DEPTH_BIT, 0U, 0U, 0, 3, 3U, 2U);
	x_command(GPU_OP_CMD_COPY_BUFFER_TO_IMAGE, ID_X_COMMANDS);
	put64(ID_BUFFER);
	put64(ID_M_D32S8);
	put32(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	put32(1U);
	put64(1U);
	m_region(5400U, 0U, 0U, VK_IMAGE_ASPECT_DEPTH_BIT, 0U, 0U, 1, 1, 2U, 2U);
	x_command(GPU_OP_CMD_COPY_IMAGE, ID_X_COMMANDS);
	put64(ID_IMAGE);
	put32(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	put64(ID_X_IMAGE);
	put32(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	put32(1U);
	put64(1U);
	put32(VK_IMAGE_ASPECT_COLOR_BIT);
	put32(0U);
	put32(0U);
	put32(1U);
	put32(4U);
	put32(4U);
	put32(0U);
	put32(VK_IMAGE_ASPECT_COLOR_BIT);
	put32(0U);
	put32(0U);
	put32(1U);
	put32(10U);
	put32(5U);
	put32(0U);
	put32(6U);
	put32(3U);
	put32(1U);
	x_command(GPU_OP_CMD_BLIT_IMAGE, ID_X_COMMANDS);
	put64(ID_IMAGE);
	put32(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	put64(ID_X_IMAGE);
	put32(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	put32(1U);
	put64(1U);
	put32(VK_IMAGE_ASPECT_COLOR_BIT);
	put32(0U);
	put32(1U);
	put32(1U);
	put64(2U);
	put32(0U);
	put32(0U);
	put32(0U);
	put32(4U);
	put32(2U);
	put32(1U);
	put32(VK_IMAGE_ASPECT_COLOR_BIT);
	put32(0U);
	put32(0U);
	put32(1U);
	put64(2U);
	put32(20U);
	put32(20U);
	put32(0U);
	put32(24U);
	put32(22U);
	put32(1U);
	put32(VK_FILTER_NEAREST);
	error = x_end(&node, ID_X_COMMANDS);

	/*
	 * What the commands make of the memory, by hand from the layouts of
	 * ghci.md section 8.5.2: the R8G8B8A8 image (64 x 32, three levels, two
	 * layers) has rows of 256, 128 and 64 bytes and a layer of 10752 bytes,
	 * level 1 at 8192; the R8 image rows of 64 bytes; D24S8 rows of 256
	 * bytes, D32S8 of 512.
	 */
	memcpy(reference, memory, M_MEMORY_BYTES);
	for (y = 0U; y < 4U; y++) {
		for (x = 0U; x < 20U; x++)
			reference[10752U + 8192U + (2U + y) * 128U + 12U + x] = memory[M_BUFFER + y * 28U + x];
	}
	for (y = 0U; y < 3U; y++) {
		for (x = 0U; x < 5U; x++)
			reference[M_BUFFER + 4001U + y * 5U + x] = memory[M_R8_OFFSET + (1U + y) * 64U + 1U + x];
	}
	for (y = 0U; y < 2U; y++) {
		for (x = 0U; x < 6U; x++)
			reference[M_D24S8_OFFSET + y * 256U + (2U + x) * 4U + 3U] = memory[M_BUFFER + 5000U + y * 6U + x];
	}
	for (y = 0U; y < 2U; y++) {
		for (x = 0U; x < 3U; x++) {
			reference[M_BUFFER + 5200U + (y * 3U + x) * 4U] = memory[M_D24S8_OFFSET + (3U + y) * 256U + x * 4U];
			reference[M_BUFFER + 5200U + (y * 3U + x) * 4U + 1U] = memory[M_D24S8_OFFSET + (3U + y) * 256U + x * 4U + 1U];
			reference[M_BUFFER + 5200U + (y * 3U + x) * 4U + 2U] = memory[M_D24S8_OFFSET + (3U + y) * 256U + x * 4U + 2U];
		}
	}
	for (y = 0U; y < 2U; y++) {
		for (x = 0U; x < 2U; x++)
			memcpy(reference + M_D32S8_OFFSET + (1U + y) * 512U + (1U + x) * 8U, memory + M_BUFFER + 5400U + (y * 2U + x) * 4U, 4U);
	}
	for (y = 0U; y < 3U; y++)
		memcpy(reference + X_ATTACHMENT_OFFSET + (5U + y) * 256U + 40U, memory + (4U + y) * 256U + 16U, 24U);

	/* The packets, every dispatch on the model, and the memory. */
	mark = model.command_count;
	error |= x_submit(&node, ID_X_COMMANDS, 0U);
	expect(error == 0 && model_count_commands(GHCI_OP_COPY_BUFFER_TO_IMAGE) == 0U && model_count_commands(GHCI_OP_COPY_IMAGE) == 0U &&
	       model_count_commands(GHCI_OP_BLIT) == 0U && model_count_commands(GHCI_OP_DRAW) == 1U,
	       "meta images: submitted, no blitter packets, the blit a draw (scene_m_draws)");
	error = m_run(&node, mark, &dispatches);
	expect(error == 0 && dispatches == 6U, "meta images: the six dispatches ran on the GSI1 model");
	expect(m_same(memory, reference, "meta images"), "meta images: the texels and aspects copied, the bytes around them kept");
	node_close(&node);
	stop("meta images");
}

/*
 * Query results on a profile: four timestamp slots written by hand
 * (query 2 not available), copied as 64-bit results with availability,
 * three as 32-bit results with availability 8 bytes apart, four as
 * 32-bit results alone (the unavailable one not written), and one with
 * VK_QUERY_RESULT_WAIT_BIT after a barrier that waits; without the
 * blitter, a reset of queries 2 and 3 after them (the copy program's
 * zeros).
 */
static void
scene_m_queries(
	enum model_profile profile)
{
	static uint8_t reference[M_MEMORY_BYTES];
	const struct ghci_vk_object *object;
	const struct ghci_vk_query_pool *pool;
	const struct model_command *command;
	uint8_t *memory;
	uint64_t value;
	uint32_t index;
	uint32_t at;
	unsigned mark;
	unsigned dispatches;
	unsigned barrier;
	unsigned after;
	const struct ghci_gsi1_program *program;
	uint32_t word;
	uint32_t loads_before;
	uint32_t loads_after;
	uint32_t acquires;
	struct node node;
	int error;

	/* The device and the pool. */
	error = x_open(&node, profile);
	begin(&node, GPU_OP_CREATE_QUERY_POOL);
	put64(ID_DEVICE);
	put64(1U);
	put32(VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);
	put64(0U);
	put32(0U);
	put32(VK_QUERY_TYPE_TIMESTAMP);
	put32(4U);
	put32(0U);
	tail(ID_M_QUERIES);
	error |= run(&node);
	object = ghci_vk_object_find(session_of(&node), GHCI_VK_QUERY_POOL, ID_M_QUERIES);
	memory = m_memory(&node);
	expect(error == 0 && object != NULL && memory != NULL, "meta queries: device, pool and memory");
	if (object == NULL || memory == NULL) {
		node_close(&node);
		stop("meta queries");
		return;
	}
	pool = &object->as.query_pool;

	/* The slots: value 0x1_0000_0000 x (q + 1) + 100 + q, available but query 2; the buffer 0xcc. */
	for (index = 0U; index < 4U; index++) {
		value = 0x100000000ULL * (index + 1U) + 100U + index;
		memcpy(pool->view + index * 16U, &value, 8U);
		value = index == 2U ? 0U : 1U;
		memcpy(pool->view + index * 16U + 8U, &value, 8U);
	}
	memset(memory + M_BUFFER + 6000U, 0xcc, 512U);

	/* The copies (and the reset without the blitter). */
	x_record(&node, ID_X_COMMANDS);
	x_command(GPU_OP_CMD_COPY_QUERY_POOL_RESULTS, ID_X_COMMANDS);
	put64(ID_M_QUERIES);
	put32(0U);
	put32(4U);
	put64(ID_BUFFER);
	put64(6000U);
	put64(16U);
	put32(VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
	x_command(GPU_OP_CMD_COPY_QUERY_POOL_RESULTS, ID_X_COMMANDS);
	put64(ID_M_QUERIES);
	put32(1U);
	put32(3U);
	put64(ID_BUFFER);
	put64(6100U);
	put64(8U);
	put32(VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
	x_command(GPU_OP_CMD_COPY_QUERY_POOL_RESULTS, ID_X_COMMANDS);
	put64(ID_M_QUERIES);
	put32(0U);
	put32(4U);
	put64(ID_BUFFER);
	put64(6200U);
	put64(4U);
	put32(0U);
	x_command(GPU_OP_CMD_COPY_QUERY_POOL_RESULTS, ID_X_COMMANDS);
	put64(ID_M_QUERIES);
	put32(3U);
	put32(1U);
	put64(ID_BUFFER);
	put64(6304U);
	put64(8U);
	put32(VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
	if (profile == MODEL_NO_BLT) {
		x_command(GPU_OP_CMD_RESET_QUERY_POOL, ID_X_COMMANDS);
		put64(ID_M_QUERIES);
		put32(2U);
		put32(2U);
	}
	error = x_end(&node, ID_X_COMMANDS);

	/* The results, by hand. */
	memcpy(reference, memory, M_MEMORY_BYTES);
	for (index = 0U; index < 4U; index++) {
		at = M_BUFFER + 6000U + index * 16U;
		if (index != 2U) {
			value = 0x100000000ULL * (index + 1U) + 100U + index;
			memcpy(reference + at, &value, 8U);
		}
		value = index == 2U ? 0U : 1U;
		memcpy(reference + at + 8U, &value, 8U);
	}
	for (index = 1U; index < 4U; index++) {
		at = M_BUFFER + 6100U + (index - 1U) * 8U;
		if (index != 2U)
			reference[at] = (uint8_t)(100U + index);
		if (index != 2U)
			memset(reference + at + 1U, 0, 3U);
		memset(reference + at + 4U, 0, 4U);
		reference[at + 4U] = index == 2U ? 0U : 1U;
	}
	for (index = 0U; index < 4U; index++) {
		at = M_BUFFER + 6200U + index * 4U;
		if (index == 2U)
			continue;
		memset(reference + at, 0, 4U);
		reference[at] = (uint8_t)(100U + index);
	}
	value = 0x400000000ULL + 103U;
	memcpy(reference + M_BUFFER + 6304U, &value, 8U);

	/*
	 * The packets: four dispatches of the query program (and the reset's),
	 * each copy between two barriers of every unit idle, L1 flushed and
	 * invalidated, and the same barrier after the reset's fill (whose
	 * program follows the last copy's barrier).
	 */
	mark = model.command_count;
	error |= x_submit(&node, ID_X_COMMANDS, 0U);
	barrier = 0U;
	after = 0U;
	for (index = mark + 1U; index + 1U < model.command_count; index++) {
		command = &model.commands[index];
		if (command->opcode != GHCI_OP_BARRIER || command->words[1] != 0U || command->words[2] != 0U ||
		    command->words[3] != (GHCI_BARRIER_WAIT_IDLE | GHCI_BARRIER_FLUSH_L1_DATA | GHCI_BARRIER_INVAL_L1_DATA))
			continue;
		if (model.commands[index + 1U].opcode == GHCI_OP_BIND_PIPELINE)
			barrier++;
		if (model.commands[index - 1U].opcode == GHCI_OP_DISPATCH)
			after++;
	}
	expect(error == 0 && barrier == (profile == MODEL_NO_BLT ? 5U : 4U) && after == (profile == MODEL_NO_BLT ? 5U : 4U) &&
	       model_count_commands(GHCI_OP_FILL_BUFFER) == 0U,
	       profile == MODEL_NO_BLT ? "meta queries no-blt: submitted, each copy between two barriers, the reset's fill and its barrier"
	                               : "meta queries full: submitted, each copy between two barriers");
	error = m_run(&node, mark, &dispatches);

	/*
	 * The query program: the availability's read, then fence 4 (acquire,
	 * ghci.md section 12.8), then the value's two reads (lw is LOAD, fence
	 * is CUSTOM0 funct3 4 with the fences in the immediate, section 12.4).
	 */
	program = ((struct ghci_vk *)device.vk)->meta[GHCI_VK_META_QUERY][0];
	loads_before = 0U;
	loads_after = 0U;
	acquires = 0U;
	for (index = 0U; program != NULL && index + 1U < program->word_count; index += 2U) {
		word = program->words[index];
		if ((word & 0x7fU) == GSI1_OP_CUSTOM0 && ((word >> GSI1_W0_FUNCT3_SHIFT) & 7U) == GSI1_F3_FENCE) {
			if ((word >> GSI1_W0_IMM_SHIFT) == GSI1_FENCE_ACQUIRE)
				acquires++;
			continue;
		}
		if ((word & 0x7fU) == GSI1_OP_LOAD && acquires == 0U)
			loads_before++;
		if ((word & 0x7fU) == GSI1_OP_LOAD && acquires != 0U)
			loads_after++;
	}
	expect(program != NULL && acquires == 1U && loads_before >= 1U && loads_after >= 2U,
	       profile == MODEL_NO_BLT ? "meta queries no-blt: the program reads the availability, fence 4, then the value"
	                               : "meta queries full: the program reads the availability, fence 4, then the value");
	expect(error == 0 && dispatches == (profile == MODEL_NO_BLT ? 5U : 4U),
	       profile == MODEL_NO_BLT ? "meta queries no-blt: five dispatches on the GSI1 model" : "meta queries full: four dispatches on the GSI1 model");
	expect(m_same(memory, reference, "meta queries"),
	       profile == MODEL_NO_BLT ? "meta queries no-blt: the results packed" : "meta queries full: the results packed");
	if (profile == MODEL_NO_BLT) {
		value = 0U;
		for (index = 32U; index < 64U; index++)
			value |= pool->view[index];
		expect(value == 0U && pool->view[16] == 101U, "meta queries no-blt: the reset zeroes queries 2 and 3 alone");
	}
	node_close(&node);
	stop("meta queries");
}

/*
 * The binds after a transfer: a dispatch of dyn.comp, a fill (the copy
 * program's dispatch), and the same dispatch again: the client's
 * pipeline, set 2 and constants are placed again before it; and a draw,
 * a fill and a draw: the pipeline, its state and the vertex buffer again.
 */
static void
scene_m_binds(void)
{
	static const uint32_t expected[] = {
		GHCI_OP_BIND_SESSION, GHCI_OP_BARRIER,
		GHCI_OP_BIND_PIPELINE, GHCI_OP_BIND_DESCRIPTOR_TABLE, GHCI_OP_LOAD_CONST, GHCI_OP_LOAD_CONST, GHCI_OP_DISPATCH,
		GHCI_OP_BIND_PIPELINE, GHCI_OP_BIND_DESCRIPTOR_TABLE, GHCI_OP_LOAD_CONST, GHCI_OP_DISPATCH,
		GHCI_OP_BIND_PIPELINE, GHCI_OP_BIND_DESCRIPTOR_TABLE, GHCI_OP_LOAD_CONST, GHCI_OP_LOAD_CONST, GHCI_OP_DISPATCH,
		GHCI_OP_BARRIER, GHCI_OP_FENCE, GHCI_OP_BIND_SESSION
	};
	const struct ghci_vk_object *object;
	const struct model_command *command;
	uint32_t ops[X_OPS_MAX];
	uint64_t client_pso;
	uint32_t draw_ops;
	uint32_t index;
	unsigned first;
	unsigned count;
	unsigned mark;
	struct node node;
	int error;
	int held;

	/* The device and the compute pipeline with its set. */
	error = x_open(&node, MODEL_NO_BLT);
	error |= x_compute_set(&node);
	object = ghci_vk_object_find(session_of(&node), GHCI_VK_PIPELINE, ID_X_COMPUTE);
	expect(error == 0 && object != NULL, "meta binds: device and pipeline");
	if (object == NULL) {
		node_close(&node);
		stop("meta binds");
		return;
	}
	client_pso = object->as.pipeline->pso_va;

	/* Dispatch, fill, dispatch. */
	x_record(&node, ID_X_COMMANDS);
	x_command(GPU_OP_CMD_BIND_PIPELINE, ID_X_COMMANDS);
	put32(VK_PIPELINE_BIND_POINT_COMPUTE);
	put64(ID_X_COMPUTE);
	x_command(GPU_OP_CMD_BIND_DESCRIPTOR_SETS, ID_X_COMMANDS);
	put32(VK_PIPELINE_BIND_POINT_COMPUTE);
	put64(ID_LAYOUT);
	put32(0U);
	put32(1U);
	put64(1U);
	put64(ID_SET);
	put32(1U);
	put64(1U);
	put32(64U);
	x_command(GPU_OP_CMD_PUSH_CONSTANTS, ID_X_COMMANDS);
	put64(ID_LAYOUT);
	put32(VK_SHADER_STAGE_COMPUTE_BIT);
	put32(0U);
	put32(104U);
	put64(104U);
	for (index = 0U; index < 26U; index++)
		put32(index);
	x_command(GPU_OP_CMD_DISPATCH, ID_X_COMMANDS);
	put32(1U);
	put32(1U);
	put32(1U);
	x_command(GPU_OP_CMD_FILL_BUFFER, ID_X_COMMANDS);
	put64(ID_BUFFER);
	put64(4096U);
	put64(64U);
	put32(7U);
	x_command(GPU_OP_CMD_DISPATCH, ID_X_COMMANDS);
	put32(1U);
	put32(1U);
	put32(1U);
	error = x_end(&node, ID_X_COMMANDS);
	mark = model.command_count;
	error |= x_submit(&node, ID_X_COMMANDS, 0U);
	count = x_ops(mark, &first, ops);
	held = error == 0 && x_same(ops, count, expected, sizeof(expected) / sizeof(expected[0]));
	expect(held, "meta binds: the client's pipeline, set and constants placed again after the transfer");
	if (held) {
		command = &model.commands[first + 11U];
		expect((command->words[1] | ((uint64_t)command->words[2] << 32)) == client_pso &&
		       (command->words[1] | ((uint64_t)command->words[2] << 32)) != model.commands[first + 7U].words[1],
		       "meta binds: the second dispatch binds the client's pipeline");
		command = &model.commands[first + 13U];
		expect(command->words[1] == 0U && command->words[3] == 0U && command->words[3U + 23U] == 23U,
		       "meta binds: the client's push constants c0 on again");
	}

	/* Draw, fill, draw: the second draw binds the pipeline, its state and the vertex buffer again. */
	error = (int)x_triangle(&node, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, UINT32_MAX, ID_PIPELINE);
	x_record(&node, ID_X_COMMANDS);
	x_begin_pass(ID_X_COMMANDS, 64U, 32U);
	x_bind_triangle(ID_X_COMMANDS, ID_PIPELINE, 0U);
	x_command(GPU_OP_CMD_DRAW, ID_X_COMMANDS);
	put32(3U);
	put32(1U);
	put32(0U);
	put32(0U);
	x_command(GPU_OP_CMD_END_RENDER_PASS, ID_X_COMMANDS);
	x_command(GPU_OP_CMD_FILL_BUFFER, ID_X_COMMANDS);
	put64(ID_BUFFER);
	put64(4096U);
	put64(64U);
	put32(7U);
	x_begin_pass(ID_X_COMMANDS, 64U, 32U);
	x_command(GPU_OP_CMD_DRAW, ID_X_COMMANDS);
	put32(3U);
	put32(1U);
	put32(0U);
	put32(0U);
	x_command(GPU_OP_CMD_END_RENDER_PASS, ID_X_COMMANDS);
	error |= x_end(&node, ID_X_COMMANDS);
	mark = model.command_count;
	error |= x_submit(&node, ID_X_COMMANDS, 0U);
	count = x_ops(mark, &first, ops);
	draw_ops = 0U;
	for (index = 0U; index < count; index++) {
		if (ops[index] == GHCI_OP_DISPATCH)
			draw_ops = index;
	}
	held = 0;
	for (index = draw_ops; index < count && draw_ops != 0U; index++) {
		if (ops[index] == GHCI_OP_BIND_PIPELINE &&
		    (model.commands[first + index].words[1] | ((uint64_t)model.commands[first + index].words[2] << 32)) ==
		    ghci_vk_object_find(session_of(&node), GHCI_VK_PIPELINE, ID_PIPELINE)->as.pipeline->pso_va)
			held |= 1;
		if (ops[index] == GHCI_OP_SET_VIEWPORT)
			held |= 2;
		if (ops[index] == GHCI_OP_BIND_VERTEX_BUFFERS)
			held |= 4;
		if (ops[index] == GHCI_OP_DRAW)
			break;
	}
	expect(error == 0 && held == 7, "meta binds: the draw after the transfer binds its pipeline, viewport and vertex buffer again");
	node_close(&node);
	stop("meta binds");
}

/*
 * What the device cannot make without the blitter, and what Vulkan does
 * not allow: a blit of depth that scales is skipped (nothing placed,
 * the submission runs); a copy of query results 4 bytes into a
 * 64-bit result, a buffer copy of both aspects of a D24S8 image, and
 * image copies of other texel sizes, a combined depth and stencil
 * format on one side, or other samples refuse the submission; a
 * transfer whose program the compiler refuses is skipped, and the
 * refusal is kept.
 */
static void
scene_m_refusals(void)
{
	unsigned dispatches;
	unsigned fences;
	unsigned lines;
	uint32_t registers;
	uint32_t index;
	struct node node;
	int error;

	/* The device, the D24S8 image, the pool. */
	error = x_open(&node, MODEL_NO_BLT);
	error |= m_image(&node, ID_M_D24S8, VK_FORMAT_D24_UNORM_S8_UINT, M_D24S8_OFFSET);
	begin(&node, GPU_OP_CREATE_QUERY_POOL);
	put64(ID_DEVICE);
	put64(1U);
	put32(VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);
	put64(0U);
	put32(0U);
	put32(VK_QUERY_TYPE_TIMESTAMP);
	put32(4U);
	put32(0U);
	tail(ID_M_QUERIES);
	error |= run(&node);
	expect(error == 0, "meta refusals: objects");

	/* A scaling blit of depth (no BLIT feature without the blitter): skipped. */
	x_record(&node, ID_X_COMMANDS);
	x_command(GPU_OP_CMD_BLIT_IMAGE, ID_X_COMMANDS);
	put64(ID_M_D24S8);
	put32(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	put64(ID_M_D24S8);
	put32(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	put32(1U);
	put64(1U);
	put32(VK_IMAGE_ASPECT_DEPTH_BIT);
	put32(0U);
	put32(0U);
	put32(1U);
	put64(2U);
	put32(0U);
	put32(0U);
	put32(0U);
	put32(64U);
	put32(32U);
	put32(1U);
	put32(VK_IMAGE_ASPECT_DEPTH_BIT);
	put32(0U);
	put32(0U);
	put32(1U);
	put64(2U);
	put32(0U);
	put32(0U);
	put32(0U);
	put32(32U);
	put32(16U);
	put32(1U);
	put32(VK_FILTER_NEAREST);
	error = x_end(&node, ID_X_COMMANDS);
	dispatches = model_count_commands(GHCI_OP_DISPATCH);
	error |= x_submit(&node, ID_X_COMMANDS, 0U);
	expect(error == 0 && model_count_commands(GHCI_OP_DISPATCH) == dispatches && model_count_commands(GHCI_OP_BLIT) == 0U &&
	       model_count_commands(GHCI_OP_DRAW) == 0U, "meta refusals: a scaling blit of depth is skipped without the blitter");

	/* A 64-bit result 4 bytes into its place: refused. */
	x_record(&node, ID_X_COMMANDS);
	x_command(GPU_OP_CMD_COPY_QUERY_POOL_RESULTS, ID_X_COMMANDS);
	put64(ID_M_QUERIES);
	put32(0U);
	put32(1U);
	put64(ID_BUFFER);
	put64(4U);
	put64(8U);
	put32(VK_QUERY_RESULT_64_BIT);
	error = x_end(&node, ID_X_COMMANDS);
	fences = model_count_commands(GHCI_OP_FENCE);
	expect(error == 0 && x_submit(&node, ID_X_COMMANDS, 0U) == EINVAL && model_count_commands(GHCI_OP_FENCE) == fences,
	       "meta refusals: 64-bit results not on 8 bytes refuse the submission");

	/* Both aspects of a D24S8 image to a buffer: refused. */
	x_record(&node, ID_X_COMMANDS);
	x_command(GPU_OP_CMD_COPY_IMAGE_TO_BUFFER, ID_X_COMMANDS);
	put64(ID_M_D24S8);
	put32(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	put64(ID_BUFFER);
	put32(1U);
	put64(1U);
	m_region(0U, 0U, 0U, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0U, 0U, 0, 0, 4U, 4U);
	error = x_end(&node, ID_X_COMMANDS);
	fences = model_count_commands(GHCI_OP_FENCE);
	expect(error == 0 && x_submit(&node, ID_X_COMMANDS, 0U) == EINVAL && model_count_commands(GHCI_OP_FENCE) == fences,
	       "meta refusals: both aspects of a D24S8 image to a buffer refuse the submission");

	/* Image copies Vulkan does not allow: R8 to D24S8's depth (1 and 4 bytes a texel), R8G8B8A8 to D24S8 (4 bytes each). */
	error = m_image(&node, ID_M_R8, VK_FORMAT_R8_UNORM, M_R8_OFFSET);
	x_record(&node, ID_X_COMMANDS);
	m_copy_image(ID_M_R8, VK_IMAGE_ASPECT_COLOR_BIT, ID_M_D24S8, VK_IMAGE_ASPECT_DEPTH_BIT);
	error |= x_end(&node, ID_X_COMMANDS);
	fences = model_count_commands(GHCI_OP_FENCE);
	dispatches = model_count_commands(GHCI_OP_DISPATCH);
	expect(error == 0 && x_submit(&node, ID_X_COMMANDS, 0U) == EINVAL && model_count_commands(GHCI_OP_FENCE) == fences &&
	       model_count_commands(GHCI_OP_DISPATCH) == dispatches, "meta refusals: a copy of R8 to D24S8 (other texel sizes) refuses the submission");
	x_record(&node, ID_X_COMMANDS);
	m_copy_image(ID_X_IMAGE, VK_IMAGE_ASPECT_COLOR_BIT, ID_M_D24S8, VK_IMAGE_ASPECT_DEPTH_BIT);
	error = x_end(&node, ID_X_COMMANDS);
	fences = model_count_commands(GHCI_OP_FENCE);
	expect(error == 0 && x_submit(&node, ID_X_COMMANDS, 0U) == EINVAL && model_count_commands(GHCI_OP_FENCE) == fences,
	       "meta refusals: a copy of R8G8B8A8 to D24S8 (a combined format on one side) refuses the submission");
	node_close(&node);
	stop("meta refusals");

	/* A copy of a 4-sample R8G8B8A8 image to a single-sampled one (the full profile without the blitter, which has 4 samples). */
	test_caps_off = GHCI_CAP_BLT;
	error = x_open(&node, MODEL_FULL);
	test_caps_off = 0U;
	error |= x_image(&node, ID_X_IMAGE + 1U, ID_X_VIEW + 1U, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_4_BIT,
	                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 0x48000U);
	x_record(&node, ID_X_COMMANDS);
	m_copy_image(ID_X_IMAGE + 1U, VK_IMAGE_ASPECT_COLOR_BIT, ID_X_IMAGE, VK_IMAGE_ASPECT_COLOR_BIT);
	error |= x_end(&node, ID_X_COMMANDS);
	fences = model_count_commands(GHCI_OP_FENCE);
	expect(error == 0 && x_submit(&node, ID_X_COMMANDS, 0U) == EINVAL && model_count_commands(GHCI_OP_FENCE) == fences,
	       "meta refusals: a copy of 4 samples to 1 refuses the submission");
	node_close(&node);
	stop("meta refusals");

	/*
	 * A copy program the compiler refuses (a lane of 8 registers, which
	 * it would spill past): two fills are skipped with one line of log,
	 * the submission runs, and the GPU remembers the refusal (with 63
	 * registers again, the next fill is still skipped, not compiled).
	 */
	error = x_open(&node, MODEL_NO_BLT);
	registers = device.caps.lane_registers;
	device.caps.lane_registers = 8U;
	x_record(&node, ID_X_COMMANDS);
	for (index = 0U; index < 2U; index++) {
		x_command(GPU_OP_CMD_FILL_BUFFER, ID_X_COMMANDS);
		put64(ID_BUFFER);
		put64(256U * index);
		put64(256U);
		put32(0x12345678U);
	}
	error |= x_end(&node, ID_X_COMMANDS);
	dispatches = model_count_commands(GHCI_OP_DISPATCH);
	lines = stub.log_lines;
	error |= x_submit(&node, ID_X_COMMANDS, 0U);
	expect(error == 0 && model_count_commands(GHCI_OP_DISPATCH) == dispatches && stub.log_lines == lines + 1U &&
	       ((struct ghci_vk *)device.vk)->meta_refused[GHCI_VK_META_COPY] && ((struct ghci_vk *)device.vk)->meta[GHCI_VK_META_COPY][0] == NULL,
	       "meta refusals: fills whose program the compiler refuses are skipped, one line of log, the refusal kept");
	device.caps.lane_registers = registers;
	x_record(&node, ID_X_COMMANDS);
	x_command(GPU_OP_CMD_FILL_BUFFER, ID_X_COMMANDS);
	put64(ID_BUFFER);
	put64(0U);
	put64(256U);
	put32(0x12345678U);
	error = x_end(&node, ID_X_COMMANDS);
	error |= x_submit(&node, ID_X_COMMANDS, 0U);
	expect(error == 0 && model_count_commands(GHCI_OP_DISPATCH) == dispatches && ((struct ghci_vk *)device.vk)->meta[GHCI_VK_META_COPY][0] == NULL,
	       "meta refusals: the next fill is skipped without a compile");
	node_close(&node);
	stop("meta refusals");
}

/*
 * Records a copy of 8 x 8 texels from (0, 0) of level 0, layer 0 of one
 * image's aspect to (0, 0) of another's.
 */
static void
m_copy_image(
	uint64_t source,
	uint32_t source_aspect,
	uint64_t destination,
	uint32_t destination_aspect)
{
	/* The images and the one region. */
	x_command(GPU_OP_CMD_COPY_IMAGE, ID_X_COMMANDS);
	put64(source);
	put32(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	put64(destination);
	put32(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	put32(1U);
	put64(1U);
	put32(source_aspect);
	put32(0U);
	put32(0U);
	put32(1U);
	put32(0U);
	put32(0U);
	put32(0U);
	put32(destination_aspect);
	put32(0U);
	put32(0U);
	put32(1U);
	put32(0U);
	put32(0U);
	put32(0U);
	put32(8U);
	put32(8U);
	put32(1U);
}

/*
 * The draws without the blitter (the full profile with CAP_BLT taken
 * away, which keeps multisampling): a barrier of colour writes before
 * transfer reads, a blit of layer 1 of an R8G8B8A8 image, scaled and
 * flipped on both axes with the linear filter, and a resolve of a
 * 4-sample image moved by (-3, -3).  The packets of each by hand: the
 * barrier (the units of the driver's draws), the render pass of the
 * destination (loaded and stored) over the destination's rectangle, the
 * draw's pipeline and its blend word of colour attachment 0, the
 * viewport and scissor of the rectangle, the source's texture and the
 * sampler in sets 0 and 1, the push constants, one triangle; before the
 * resolve, a barrier that waits for every unit.  The same recording on
 * the full profile: the barriers name the blitter, and BLIT and
 * RESOLVE_IMAGE are the blitter's.
 */
static void
scene_m_draws(void)
{
	static const uint32_t draw_ops[] = {
		GHCI_OP_BEGIN_RENDER_PASS, GHCI_OP_BIND_PIPELINE, GHCI_OP_SET_DYNAMIC_STATE, GHCI_OP_SET_VIEWPORT, GHCI_OP_SET_SCISSOR,
		GHCI_OP_BIND_DESCRIPTOR_TABLE, GHCI_OP_BIND_DESCRIPTOR_TABLE, GHCI_OP_LOAD_CONST, GHCI_OP_DRAW, GHCI_OP_END_RENDER_PASS
	};
	static const uint32_t blit_push[14] = { 0U, 64U, 40U, 8U, 8U, 40U, 0U, 32U, 30U, 2U, 2U, 30U, 64U, 32U };
	const struct ghci_vk_meta_pipeline *blit;
	const struct ghci_vk_meta_pipeline *resolve;
	const struct model_command *command;
	const uint8_t *bytes;
	uint32_t ops[X_OPS_MAX];
	uint64_t image_va;
	uint64_t attachment_va;
	uint64_t samples_va;
	uint32_t index;
	unsigned first;
	unsigned count;
	unsigned mark;
	struct node node;
	int error;
	int held;

	/* The device without the blitter, and a 4-sample image. */
	test_caps_off = GHCI_CAP_BLT;
	error = x_open(&node, MODEL_FULL);
	test_caps_off = 0U;
	error |= x_image(&node, ID_X_IMAGE + 1U, ID_X_VIEW + 1U, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_4_BIT,
	                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 0x48000U);
	expect(error == 0 && !ghci_caps_has(&device.caps, GHCI_HAS_BLT), "meta draws: a device without the blitter, with multisampling");
	image_va = x_va(&node, 0U);
	attachment_va = x_va(&node, X_ATTACHMENT_OFFSET);
	samples_va = x_va(&node, 0x48000U);

	/* The barrier, the blit and the resolve. */
	x_record(&node, ID_X_COMMANDS);
	m_draws_record();
	error = x_end(&node, ID_X_COMMANDS);
	mark = model.command_count;
	error |= x_submit(&node, ID_X_COMMANDS, 0U);
	count = x_ops(mark, &first, ops);
	held = error == 0 && count == 2U + 1U + 10U + 1U + 10U + 3U && ops[2] == GHCI_OP_BARRIER && ops[13] == GHCI_OP_BARRIER;
	for (index = 0U; index < 10U && held; index++) {
		if (ops[3U + index] != draw_ops[index] || ops[14U + index] != draw_ops[index])
			held = 0;
	}
	expect(held && model_count_commands(GHCI_OP_BLIT) == 0U && model_count_commands(GHCI_OP_RESOLVE_IMAGE) == 0U,
	       "meta draws: the barrier, the blit's draw, the resolve's barrier and draw, no blitter packets");
	if (!held) {
		node_close(&node);
		stop("meta draws");
		return;
	}
	blit = &session_of(&node)->devices->meta[GHCI_VK_META_BLIT];
	resolve = &session_of(&node)->devices->meta[GHCI_VK_META_RESOLVE_AVERAGE4];

	/*
	 * The client's barrier of colour writes before transfer reads: ROP
	 * before the units of the driver's programs and draws (CS, FS, ROP),
	 * L1 flushed, L1 and the texture cache invalidated.  The resolve's own
	 * barrier (ghci.md section 12.11): ROP before FS, every unit idle, L1
	 * flushed, the texture cache invalidated.
	 */
	command = &model.commands[first + 2U];
	expect(command->words[1] == GHCI_STAGE_ROP && command->words[2] == (GHCI_STAGE_CS | GHCI_STAGE_FS | GHCI_STAGE_ROP) &&
	       command->words[3] == (GHCI_BARRIER_FLUSH_L1_DATA | GHCI_BARRIER_INVAL_L1_DATA | GHCI_BARRIER_INVAL_TEX),
	       "meta draws: the draw's barrier before the blit, ROP to CS|FS|ROP, flush L1, invalidate L1 and the texture cache");
	command = &model.commands[first + 13U];
	expect(command->words[1] == GHCI_STAGE_ROP && command->words[2] == GHCI_STAGE_FS &&
	       command->words[3] == (GHCI_BARRIER_WAIT_IDLE | GHCI_BARRIER_FLUSH_L1_DATA | GHCI_BARRIER_INVAL_TEX),
	       "meta draws: before the resolve, ROP to FS, idle, flush L1, invalidate the texture cache");

	/*
	 * After each draw's pipeline, colour attachment 0's blend word of its
	 * record (the device blends each attachment on its own): dw11 at 0x2c of
	 * ghci.md section 8.2, blending off (bit 0), every component written.
	 */
	command = &model.commands[first + 5U];
	bytes = x_gpu(command->session, blit->pso_va);
	expect(bytes != NULL && command->words[1] == 0x07U && command->words[2] == (uint32_t)x_load(bytes + 0x2cU, 4U) &&
	       (command->words[2] & 1U) == 0U && command->words[2] != 0U && command->words[3] == 0U && blit->blend_word == command->words[2],
	       "meta draws: the blit's SET_DYNAMIC_STATE 0x07 is its record's blend word, no blending");
	command = &model.commands[first + 16U];
	expect(command->words[1] == 0x07U && command->words[2] == resolve->blend_word, "meta draws: the resolve's SET_DYNAMIC_STATE 0x07");

	/*
	 * The blit: the pass of the attachment (colour 0 at 0x00 of ghci.md
	 * section 8.4's description: its address, row pitch 256, format 37,
	 * LOAD | STORE) over (8, 2) 32 x 28; the blit's pipeline (its vertex and
	 * fragment code at 0x00 and 0x08 of the record).
	 */
	command = &model.commands[first + 3U];
	bytes = x_gpu(command->session, command->words[1] | ((uint64_t)command->words[2] << 32));
	expect(bytes != NULL && command->words[3] == (8U | (2U << 16)) && command->words[4] == (32U | (28U << 16)) && command->words[8] == 0U &&
	       x_load(bytes, 8U) == attachment_va && x_load(bytes + 0x08U, 4U) == 256U && bytes[0x0cU] == 37U && bytes[0x0dU] == 0U &&
	       x_load(bytes + 0x10U, 8U) == 0U, "meta draws: the blit's pass of the attachment, loaded and stored, over the rectangle");
	command = &model.commands[first + 4U];
	bytes = x_gpu(command->session, command->words[1] | ((uint64_t)command->words[2] << 32));
	expect(bytes != NULL && (command->words[1] | ((uint64_t)command->words[2] << 32)) == blit->pso_va && x_load(bytes, 8U) == blit->code_va &&
	       x_load(bytes + 0x08U, 8U) == blit->fragment_va && x_load(bytes + 0x10U, 8U) == 0U && blit->fragment_va != 0U,
	       "meta draws: the blit's pipeline, its vertex and fragment programs");

	/* The viewport and scissor: (8, 2) 32 x 28 (the floats 8, 2, 32, 28; depth 0 to 1). */
	command = &model.commands[first + 6U];
	expect(command->words[1] == 0x41000000U && command->words[2] == 0x40000000U && command->words[3] == 0x42000000U &&
	       command->words[4] == 0x41e00000U && command->words[5] == 0U && command->words[6] == 0x3f800000U,
	       "meta draws: the blit's viewport is the rectangle");
	command = &model.commands[first + 7U];
	expect(command->words[1] == 8U && command->words[2] == 2U && command->words[3] == 32U && command->words[4] == 28U,
	       "meta draws: the blit's scissor is the rectangle");

	/*
	 * The texture (section 8.3: layer 1 at 10752, 64 x 32, format 37, one
	 * level, row pitch 256, one sample, 2D, R G B A as 2 3 4 5) in set 0 and
	 * the sampler (linear, clamped to the edge) in set 1.
	 */
	command = &model.commands[first + 8U];
	bytes = x_gpu(command->session, command->words[1] | ((uint64_t)command->words[2] << 32));
	expect(bytes != NULL && command->words[3] == (0U | (1U << 16)) && x_load(bytes, 8U) == image_va + 10752U &&
	       x_load(bytes + 0x08U, 2U) == 64U && x_load(bytes + 0x0aU, 2U) == 32U && bytes[0x0eU] == 37U && bytes[0x0fU] == 1U &&
	       x_load(bytes + 0x10U, 4U) == 256U && bytes[0x18U] == 1U && bytes[0x19U] == 1U &&
	       x_load(bytes + 0x1cU, 4U) == (2U | (3U << 3) | (4U << 6) | (5U << 9)), "meta draws: the blit's source texture in set 0");
	command = &model.commands[first + 9U];
	bytes = x_gpu(command->session, command->words[1] | ((uint64_t)command->words[2] << 32));
	expect(bytes != NULL && command->words[3] == (1U | (1U << 16)) && bytes[0] == 1U && bytes[1] == 1U && bytes[3] == 2U && bytes[4] == 2U &&
	       bytes[5] == 2U, "meta draws: the blit's sampler in set 1, linear, clamped to the edge");

	/* The corners (x: source 0 to 64, destination 40 to 8, viewport 8 to 40; y the same), the source's size; one triangle. */
	command = &model.commands[first + 10U];
	held = command->words[1] == 0U && command->words[2] == 14U;
	for (index = 0U; index < 14U && held; index++) {
		if (command->words[3U + index] != blit_push[index])
			held = 0;
	}
	expect(held, "meta draws: the blit's push constants c0-c13");
	command = &model.commands[first + 11U];
	expect(command->words[1] == 3U && command->words[2] == 1U && command->words[3] == 0U && command->words[4] == 0U, "meta draws: one triangle");

	/* The resolve: the attachment over (5, 6) 10 x 8, the average of four samples, the 4-sample source, the move (-3, -3). */
	command = &model.commands[first + 14U];
	expect(command->words[3] == (5U | (6U << 16)) && command->words[4] == (10U | (8U << 16)), "meta draws: the resolve's pass over the rectangle");
	command = &model.commands[first + 15U];
	expect((command->words[1] | ((uint64_t)command->words[2] << 32)) == resolve->pso_va && resolve->pso_va != blit->pso_va,
	       "meta draws: the resolve's pipeline averages four samples");
	command = &model.commands[first + 19U];
	bytes = x_gpu(command->session, command->words[1] | ((uint64_t)command->words[2] << 32));
	expect(bytes != NULL && x_load(bytes, 8U) == samples_va && bytes[0x18U] == 4U && x_load(bytes + 0x10U, 4U) == 1024U,
	       "meta draws: the resolve's source texture of four samples, rows of 1024 bytes");
	command = &model.commands[first + 21U];
	expect(command->words[2] == 2U && command->words[3] == (uint32_t)-3 && command->words[4] == (uint32_t)-3,
	       "meta draws: the resolve's move (-3, -3)");
	node_close(&node);
	stop("meta draws");

	/* The same recording on the full profile: the barriers name the blitter; BLIT and RESOLVE_IMAGE, no draw. */
	error = x_open(&node, MODEL_FULL);
	error |= x_image(&node, ID_X_IMAGE + 1U, ID_X_VIEW + 1U, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_4_BIT,
	                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 0x48000U);
	x_record(&node, ID_X_COMMANDS);
	m_draws_record();
	error |= x_end(&node, ID_X_COMMANDS);
	mark = model.command_count;
	error |= x_submit(&node, ID_X_COMMANDS, 0U);
	count = x_ops(mark, &first, ops);
	expect(error == 0 && count == 2U + 4U + 3U && ops[2] == GHCI_OP_BARRIER && ops[3] == GHCI_OP_BLIT && ops[4] == GHCI_OP_BARRIER &&
	       ops[5] == GHCI_OP_RESOLVE_IMAGE, "meta draws full: the barrier, BLIT, the resolve's barrier, RESOLVE_IMAGE");
	if (count == 9U) {
		command = &model.commands[first + 2U];
		expect(command->words[1] == GHCI_STAGE_ROP &&
		       command->words[2] == (GHCI_STAGE_BLT | GHCI_STAGE_CS | GHCI_STAGE_FS | GHCI_STAGE_ROP) &&
		       command->words[3] == (GHCI_BARRIER_FLUSH_L1_DATA | GHCI_BARRIER_INVAL_L1_DATA | GHCI_BARRIER_INVAL_TEX),
		       "meta draws full: the draw's barrier before the blit, ROP to BLT|CS|FS|ROP");
		command = &model.commands[first + 4U];
		expect(command->words[1] == GHCI_STAGE_ROP && command->words[2] == GHCI_STAGE_BLT &&
		       command->words[3] == (GHCI_BARRIER_WAIT_IDLE | GHCI_BARRIER_FLUSH_L1_DATA),
		       "meta draws full: before RESOLVE_IMAGE, ROP to BLT, idle, flush L1");
	}
	node_close(&node);
	stop("meta draws");
}

/*
 * Records into the command buffer being recorded a barrier of colour
 * writes before transfer reads, a blit of the image's layer 1 (64 x 32)
 * to the attachment's (40, 30)-(8, 2) with the linear filter, and a
 * resolve of the 4-sample image's (2, 3) 10 x 8 to the attachment's
 * (5, 6).
 */
static void
m_draws_record(void)
{
	/* The barrier. */
	x_command(GPU_OP_CMD_PIPELINE_BARRIER, ID_X_COMMANDS);
	put32(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
	put32(VK_PIPELINE_STAGE_TRANSFER_BIT);
	put32(0U);
	put32(1U);
	put64(1U);
	put32(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
	put64(0U);
	put32(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
	put32(VK_ACCESS_TRANSFER_READ_BIT);
	put32(0U);
	put64(0U);
	put32(0U);
	put64(0U);

	/* The blit and the resolve. */
	x_command(GPU_OP_CMD_BLIT_IMAGE, ID_X_COMMANDS);
	put64(ID_IMAGE);
	put32(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	put64(ID_X_IMAGE);
	put32(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	put32(1U);
	put64(1U);
	put32(VK_IMAGE_ASPECT_COLOR_BIT);
	put32(0U);
	put32(1U);
	put32(1U);
	put64(2U);
	put32(0U);
	put32(0U);
	put32(0U);
	put32(64U);
	put32(32U);
	put32(1U);
	put32(VK_IMAGE_ASPECT_COLOR_BIT);
	put32(0U);
	put32(0U);
	put32(1U);
	put64(2U);
	put32(40U);
	put32(30U);
	put32(0U);
	put32(8U);
	put32(2U);
	put32(1U);
	put32(VK_FILTER_LINEAR);
	x_command(GPU_OP_CMD_RESOLVE_IMAGE, ID_X_COMMANDS);
	put64(ID_X_IMAGE + 1U);
	put32(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	put64(ID_X_IMAGE);
	put32(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	put32(1U);
	put64(1U);
	put32(VK_IMAGE_ASPECT_COLOR_BIT);
	put32(0U);
	put32(0U);
	put32(1U);
	put32(2U);
	put32(3U);
	put32(0U);
	put32(VK_IMAGE_ASPECT_COLOR_BIT);
	put32(0U);
	put32(0U);
	put32(1U);
	put32(5U);
	put32(6U);
	put32(0U);
	put32(10U);
	put32(8U);
	put32(1U);
}

/*
 * The resolve of colour 1 of a subpass without the blitter (the scene of
 * ghci-execute-host-test.c's subpasses on the full profile without
 * CAP_BLT): after the subpass, a barrier that waits for the pass's writes
 * and shows them to the texture unit, then the resolve draw of the
 * render area from the 4-sample view to the resolve view.
 */
static void
scene_m_subpass(void)
{
	static const uint32_t expected[] = {
		GHCI_OP_BIND_SESSION, GHCI_OP_BARRIER, GHCI_OP_BEGIN_RENDER_PASS, GHCI_OP_END_RENDER_PASS, GHCI_OP_BARRIER,
		GHCI_OP_BEGIN_RENDER_PASS, GHCI_OP_BIND_PIPELINE, GHCI_OP_SET_DYNAMIC_STATE, GHCI_OP_SET_VIEWPORT, GHCI_OP_SET_SCISSOR,
		GHCI_OP_BIND_DESCRIPTOR_TABLE, GHCI_OP_BIND_DESCRIPTOR_TABLE, GHCI_OP_LOAD_CONST, GHCI_OP_DRAW, GHCI_OP_END_RENDER_PASS,
		GHCI_OP_BEGIN_RENDER_PASS, GHCI_OP_END_RENDER_PASS, GHCI_OP_BARRIER, GHCI_OP_FENCE, GHCI_OP_BIND_SESSION
	};
	const struct model_command *command;
	const uint8_t *bytes;
	struct test_pass pass;
	uint32_t ops[X_OPS_MAX];
	uint64_t va[2];
	unsigned first;
	unsigned count;
	unsigned mark;
	struct node node;
	uint32_t index;
	int error;

	/* Two 4-sample colours and their two resolves. */
	test_caps_off = GHCI_CAP_BLT;
	error = x_open(&node, MODEL_FULL);
	test_caps_off = 0U;
	error |= x_image(&node, ID_X_IMAGE + 1U, ID_X_VIEW + 1U, VK_FORMAT_R8G8B8A8_UNORM, 4U, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 0x50000U);
	error |= x_image(&node, ID_X_IMAGE + 2U, ID_X_VIEW + 2U, VK_FORMAT_R8G8B8A8_UNORM, 4U, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 0x58000U);
	error |= x_image(&node, ID_X_IMAGE + 3U, ID_X_VIEW + 3U, VK_FORMAT_R8G8B8A8_UNORM, 1U,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 0x60000U);
	memset(&pass, 0, sizeof(pass));
	pass.attachment_count = 4U;
	for (index = 0U; index < 4U; index++) {
		pass.format[index] = VK_FORMAT_R8G8B8A8_UNORM;
		pass.samples[index] = index >= 2U ? 1U : 4U;
		pass.load[index] = index >= 2U ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_CLEAR;
		pass.store[index] = index == 0U ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
	}
	pass.subpass_count = 2U;
	pass.color_count[0] = 2U;
	pass.color[0][0] = 0U;
	pass.color[0][1] = 1U;
	pass.resolves[0] = 1U;
	pass.resolve[0][0] = 2U;
	pass.resolve[0][1] = 3U;
	pass.depth[0] = VK_ATTACHMENT_UNUSED;
	pass.color_count[1] = 1U;
	pass.color[1][0] = 1U;
	pass.depth[1] = VK_ATTACHMENT_UNUSED;
	error |= (int)make_render_pass(&node, ID_X_PASS + 1U, &pass);
	begin(&node, GPU_OP_CREATE_FRAMEBUFFER);
	put64(ID_DEVICE);
	put64(1U);
	put32(VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
	put64(0U);
	put32(0U);
	put64(ID_X_PASS + 1U);
	put32(4U);
	put64(4U);
	put64(ID_X_VIEW + 1U);
	put64(ID_X_VIEW + 2U);
	put64(ID_X_VIEW);
	put64(ID_X_VIEW + 3U);
	put32(64U);
	put32(32U);
	put32(1U);
	tail(ID_X_FRAMEBUFFER + 1U);
	error |= run(&node);
	error |= (int)reply32(&node, 4U);
	expect(error == 0, "meta subpass: objects");
	va[0] = x_va(&node, 0x58000U);
	va[1] = x_va(&node, 0x60000U);

	/* The pass begun with four clear values, the next subpass, the end. */
	x_record(&node, ID_X_COMMANDS);
	x_command(GPU_OP_CMD_BEGIN_RENDER_PASS, ID_X_COMMANDS);
	put64(1U);
	put32(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
	put64(0U);
	put64(ID_X_PASS + 1U);
	put64(ID_X_FRAMEBUFFER + 1U);
	put32(0U);
	put32(0U);
	put32(64U);
	put32(32U);
	put32(4U);
	put64(4U);
	for (index = 0U; index < 4U; index++) {
		put32(0U);
		put32(2U);
		put64(4U);
		put32(index);
		put32(0U);
		put32(0U);
		put32(0x3f800000U);
	}
	put32(VK_SUBPASS_CONTENTS_INLINE);
	x_command(GPU_OP_CMD_NEXT_SUBPASS, ID_X_COMMANDS);
	put32(VK_SUBPASS_CONTENTS_INLINE);
	x_command(GPU_OP_CMD_END_RENDER_PASS, ID_X_COMMANDS);
	error = x_end(&node, ID_X_COMMANDS);
	mark = model.command_count;
	error |= x_submit(&node, ID_X_COMMANDS, 0U);
	count = x_ops(mark, &first, ops);
	expect(error == 0 && x_same(ops, count, expected, sizeof(expected) / sizeof(expected[0])),
	       "meta subpass: subpass 0, the barrier, colour 1's resolve draw, subpass 1");
	if (count != sizeof(expected) / sizeof(expected[0])) {
		node_close(&node);
		stop("meta subpass");
		return;
	}

	/* The barrier: the colour stage before the fragment stage, idle, L1 flushed, the texture cache invalidated. */
	command = &model.commands[first + 4U];
	expect(command->words[1] == GHCI_STAGE_ROP && command->words[2] == GHCI_STAGE_FS &&
	       command->words[3] == (GHCI_BARRIER_WAIT_IDLE | GHCI_BARRIER_FLUSH_L1_DATA | GHCI_BARRIER_INVAL_TEX),
	       "meta subpass: BARRIER ROP before FS, idle, flush L1, invalidate the texture cache");

	/* The draw: into attachment 3 over the render area, from the 4-sample view of colour 1, not moved. */
	command = &model.commands[first + 5U];
	bytes = x_gpu(command->session, command->words[1] | ((uint64_t)command->words[2] << 32));
	expect(bytes != NULL && command->words[3] == 0U && command->words[4] == (64U | (32U << 16)) && x_load(bytes, 8U) == va[1] &&
	       bytes[0x0cU] == 37U && bytes[0x0dU] == 0U, "meta subpass: the resolve's pass of attachment 3 over the render area");
	command = &model.commands[first + 6U];
	expect((command->words[1] | ((uint64_t)command->words[2] << 32)) == session_of(&node)->devices->meta[GHCI_VK_META_RESOLVE_AVERAGE4].pso_va,
	       "meta subpass: the pipeline that averages four samples");
	command = &model.commands[first + 10U];
	bytes = x_gpu(command->session, command->words[1] | ((uint64_t)command->words[2] << 32));
	expect(bytes != NULL && x_load(bytes, 8U) == va[0] && bytes[0x18U] == 4U, "meta subpass: colour 1's 4-sample view in set 0");
	command = &model.commands[first + 12U];
	expect(command->words[2] == 2U && command->words[3] == 0U && command->words[4] == 0U, "meta subpass: no move");
	node_close(&node);
	stop("meta subpass");
}
