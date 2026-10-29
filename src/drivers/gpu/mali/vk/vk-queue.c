/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's queue: a submission's command buffers become one
 * command stream in the session's stage memory, which the render node's
 * ring calls (sched.c); the submission returns once the work ended, and
 * its fence is signaled then.
 *
 * The stream asks for the endpoints first, sets the scoreboard entries,
 * and then runs each record in order: copies, fills and clears as loops
 * of the front end's own loads and stores (no shader is needed for
 * them), dispatches as compute runs, and render passes as tiler and
 * fragment runs (vk-draw.c).  Every run is waited for before the next
 * record, so a pipeline barrier has nothing left to do.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <uapi/errno.h>
#include <uapi/gpu-op.h>

#include "drivers/gpu/mali/vk/vk-build.h"
#include "drivers/gpu/mali/vk/vk-desc.h"

#include "drivers/gpu/i915/vk/vk-codec.inc"

/* The most submit infos and command buffers of one submission. */
#define SUBMITS_MAX		64U
#define COMMAND_BUFFERS_MAX	64U

/* The FAU words a program takes at most, and the resource tables of a compute program. */
#define FAU_WORDS_MAX		256U
#define TABLES_COMPUTE		2U
#define TABLES_VERTEX		4U

static int submit(struct mali_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int build_stream(struct mali_vk_session *session, struct mali_vk_command_buffer **commands, uint32_t count, uint64_t *address, uint32_t *bytes);
static void run_records(struct mali_vk_build *build, const struct mali_vk_command_buffer *command);
static void run_dispatch(struct mali_vk_build *build, const struct mali_vk_record *entry);
static void run_copy_buffer(struct mali_vk_build *build, const struct mali_vk_record *entry);
static void run_fill_buffer(struct mali_vk_build *build, const struct mali_vk_record *entry);
static void run_copy_image(struct mali_vk_build *build, const struct mali_vk_record *entry);
static void run_buffer_image(struct mali_vk_build *build, const struct mali_vk_record *entry, bool to_image);
static void run_clear_image(struct mali_vk_build *build, const struct mali_vk_record *entry);
static void copy_span(struct mali_vk_build *build, uint64_t destination, uint64_t source, uint64_t bytes);
static uint32_t pack_color(const struct mali_vk_format *format, const uint32_t *words);
static uint32_t unorm8(uint32_t bits);
static const struct mali_vk_descriptor *find_slot(const struct mali_vk_descriptor_set *const *sets, uint32_t set, uint32_t binding);
static int write_texture(struct mali_vk_build *build, const struct mali_vk_descriptor *slot, volatile uint32_t *texture, volatile uint32_t *sampler);
static void store_words(volatile uint32_t *destination, const uint32_t *source, unsigned count);

/* Runs the queue's submission. */
int
mali_vk_queue_route(
	struct mali_vk_session *session,
	uint32_t opcode,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply,
	int *handled)
{
	int error;

	/* Only the submission; the waits are the device's. */
	*handled = 0;
	if (opcode != GPU_OP_QUEUE_SUBMIT)
		return 0;
	*handled = 1;
	error = submit(session, reader, reply);
	if (error != 0)
		return error;

	/* Succeeded: the submission ran. */
	return 0;
}

/* Takes stage memory for the submission being built. */
volatile uint8_t *
mali_vk_build_take(
	struct mali_vk_build *build,
	uint64_t bytes,
	uint64_t align,
	uint64_t *gpu_address)
{
	volatile uint8_t *view;
	int error;

	/* Nothing after a failure. */
	*gpu_address = 0;
	if (build->error != 0)
		return NULL;

	/* The bytes, cleared. */
	error = mali_stage_take(build->session->owner, bytes, align, &view, gpu_address);
	if (error != 0) {
		build->error = error;
		return NULL;
	}
	kern_memset((void *)(uintptr_t)view, 0, (size_t)bytes);

	/* Succeeded: the bytes are the submission's. */
	return view;
}

/* Writes a shader program descriptor into the stage and reports its address. */
uint64_t
mali_vk_build_shader(
	struct mali_vk_build *build,
	unsigned stage,
	uint32_t work_registers,
	uint32_t preload,
	uint64_t code)
{
	uint32_t words[8];
	volatile uint8_t *view;
	uint64_t address;

	/* The descriptor. */
	view = mali_vk_build_take(build, MALI_DESC_BYTES, MALI_DESC_BYTES, &address);
	if (view == NULL)
		return 0;
	mali_desc_shader(words, stage, work_registers, preload, code);
	store_words((volatile uint32_t *)(volatile void *)view, words, 8);

	/* Succeeded: its address. */
	return address;
}

/*
 * Writes a program's FAU words and resource table into the stage.  The
 * FAU pointer carries its count of 64-bit entries in its top byte, the
 * table pointer its count of tables in its low bits.
 *
 * Values referenced:
 * Mesa 26.2.6 src/panfrost/vulkan/csf/panvk_vX_cmd_dispatch.c (the FAU
 * pointer with its count at bit 56); src/panfrost/vulkan/
 * panvk_vX_cmd_desc_state.c (the resource table pointer with its count
 * in the low bits, the count a multiple of four).
 */
int
mali_vk_build_resources(
	struct mali_vk_build *build,
	const struct mali_shader_program *program,
	const struct mali_vk_descriptor_set *const *sets,
	const struct mali_vk_graphics *graphics,
	const uint32_t *viewport_words,
	uint64_t system_address,
	uint64_t *fau,
	uint64_t *table)
{
	const struct mali_vk_descriptor *slot;
	const struct mali_vk_buffer *buffer;
	uint32_t words[FAU_WORDS_MAX];
	uint32_t entry[8];
	volatile uint8_t *view;
	volatile uint8_t *tables;
	volatile uint8_t *textures;
	volatile uint8_t *samplers;
	volatile uint8_t *attributes;
	volatile uint8_t *vertex_buffers;
	uint64_t address;
	uint64_t textures_address;
	uint64_t samplers_address;
	uint64_t attributes_address;
	uint64_t buffers_address;
	uint64_t tables_address;
	uint32_t index;
	uint32_t count;
	uint32_t location;
	int error;

	/* The FAU words: push constants, then each buffer's address, then the viewport. */
	*fau = 0;
	*table = 0;
	if (program->fau_words > FAU_WORDS_MAX)
		return ENOTSUP;
	kern_memset(words, 0, sizeof(words));
	if (program->push_bytes > MALI_VK_PUSH_BYTES)
		return ENOTSUP;
	kern_memcpy(words, build->push, program->push_bytes);
	for (index = 0; index < program->buffer_count; index++) {
		address = 0;
		if (program->buffers[index].set == DRV_GPU_IR_SYSTEM_SET) {
			address = system_address;
		} else {
			slot = find_slot(sets, program->buffers[index].set, program->buffers[index].binding);
			if (slot != NULL && slot->buffer != NULL) {
				buffer = slot->buffer->payload;
				address = mali_vk_buffer_address(buffer) + slot->offset;
			}
		}

		if (program->buffers[index].fau_word + 1U >= FAU_WORDS_MAX)
			return ENOTSUP;
		words[program->buffers[index].fau_word] = (uint32_t)address;
		words[program->buffers[index].fau_word + 1U] = (uint32_t)(address >> 32);
	}

	if (viewport_words != NULL && program->viewport_fau_word + 6U <= FAU_WORDS_MAX)
		kern_memcpy(&words[program->viewport_fau_word], viewport_words, 6U * sizeof(uint32_t));

	/* Into the stage. */
	if (program->fau_words != 0U) {
		view = mali_vk_build_take(build, (uint64_t)program->fau_words * 4U, 64U, &address);
		if (view == NULL)
			return build->error;
		store_words((volatile uint32_t *)(volatile void *)view, words, program->fau_words);
		*fau = address | ((uint64_t)(program->fau_words / 2U) << 56);
	}

	/* The resource table: textures, samplers, and for a vertex program its attributes and buffers. */
	count = TABLES_COMPUTE;
	if (graphics != NULL)
		count = TABLES_VERTEX;
	count = (count + MALI_DESC_TABLE_ALIGN - 1U) & ~(MALI_DESC_TABLE_ALIGN - 1U);
	tables = mali_vk_build_take(build, (uint64_t)count * MALI_DESC_RESOURCE_BYTES, 64U, &tables_address);
	if (tables == NULL)
		return build->error;

	/* The textures and samplers of the program's combined image samplers. */
	if (program->texture_count != 0U) {
		textures = mali_vk_build_take(build, (uint64_t)program->texture_count * MALI_DESC_BYTES * 2U, 64U, &textures_address);
		samplers = mali_vk_build_take(build, (uint64_t)program->texture_count * MALI_DESC_BYTES, 64U, &samplers_address);
		if (textures == NULL || samplers == NULL)
			return build->error;
		for (index = 0; index < program->texture_count; index++) {
			slot = find_slot(sets, program->textures[index].set, program->textures[index].binding);
			error = write_texture(build,
					      slot,
					      (volatile uint32_t *)(volatile void *)(textures + (uint64_t)program->textures[index].index * MALI_DESC_BYTES),
					      (volatile uint32_t *)(volatile void *)(samplers + (uint64_t)program->textures[index].index * MALI_DESC_BYTES));
			if (error != 0)
				return error;
		}

		mali_desc_resource(entry, textures_address, (uint64_t)program->texture_count * MALI_DESC_BYTES);
		store_words((volatile uint32_t *)(volatile void *)(tables + MALI_SHADER_TABLE_TEXTURES * MALI_DESC_RESOURCE_BYTES), entry, 4);
		mali_desc_resource(entry, samplers_address, (uint64_t)program->texture_count * MALI_DESC_BYTES);
		store_words((volatile uint32_t *)(volatile void *)(tables + MALI_SHADER_TABLE_SAMPLERS * MALI_DESC_RESOURCE_BYTES), entry, 4);
	}

	/* A vertex program's attributes by location, and the vertex buffers by binding. */
	if (graphics != NULL && graphics->attribute_count != 0U) {
		attributes = mali_vk_build_take(build, (uint64_t)MALI_SHADER_LOCATIONS * MALI_DESC_BYTES, 64U, &attributes_address);
		vertex_buffers = mali_vk_build_take(build, (uint64_t)MALI_VK_VERTEX_BINDINGS_MAX * MALI_DESC_BYTES, 64U, &buffers_address);
		if (attributes == NULL || vertex_buffers == NULL)
			return build->error;
		for (index = 0; index < graphics->attribute_count; index++) {
			location = graphics->attribute_location[index];
			if (location >= MALI_SHADER_LOCATIONS)
				return ENOTSUP;
			mali_desc_attribute(entry,
					    graphics->attribute_format[index]->pixel,
					    MALI_SHADER_TABLE_VERTEX_BUFFERS,
					    graphics->binding_rate[graphics->attribute_binding[index]] == VK_VERTEX_INPUT_RATE_INSTANCE,
					    (int32_t)graphics->attribute_offset[index],
					    graphics->attribute_binding[index],
					    graphics->binding_stride[graphics->attribute_binding[index]]);
			store_words((volatile uint32_t *)(volatile void *)(attributes + (uint64_t)location * MALI_DESC_BYTES), entry, 8);
		}

		for (index = 0; index < graphics->binding_count; index++) {
			buffer = build->vertex_buffers[index];
			if (buffer == NULL)
				continue;
			address = mali_vk_buffer_address(buffer) + build->vertex_offsets[index];
			mali_desc_buffer(entry, address, (uint32_t)(buffer->size - build->vertex_offsets[index]));
			store_words((volatile uint32_t *)(volatile void *)(vertex_buffers + (uint64_t)index * MALI_DESC_BYTES), entry, 8);
		}

		mali_desc_resource(entry, attributes_address, (uint64_t)MALI_SHADER_LOCATIONS * MALI_DESC_BYTES);
		store_words((volatile uint32_t *)(volatile void *)(tables + MALI_SHADER_TABLE_ATTRIBUTES * MALI_DESC_RESOURCE_BYTES), entry, 4);
		mali_desc_resource(entry, buffers_address, (uint64_t)MALI_VK_VERTEX_BINDINGS_MAX * MALI_DESC_BYTES);
		store_words((volatile uint32_t *)(volatile void *)(tables + MALI_SHADER_TABLE_VERTEX_BUFFERS * MALI_DESC_RESOURCE_BYTES), entry, 4);
	}

	/* Succeeded: the table pointer with its count. */
	*table = tables_address | count;
	return 0;
}

/*
 * Keeps a bound pipeline, sets, push constants, vertex and index
 * buffers, viewport or scissor.
 */
void
mali_vk_build_bind(
	struct mali_vk_build *build,
	const struct mali_vk_record *entry)
{
	const struct mali_vk_pipeline *pipeline;
	uint32_t index;
	uint32_t first;

	/* One case a binding command. */
	switch (entry->opcode) {
	case GPU_OP_CMD_BIND_PIPELINE:
		if (entry->objects[0] == NULL)
			break;
		pipeline = entry->objects[0]->payload;
		if (entry->values[0] == VK_PIPELINE_BIND_POINT_COMPUTE) {
			build->compute = pipeline;
		} else {
			build->graphics = pipeline;
		}
		break;
	case GPU_OP_CMD_BIND_DESCRIPTOR_SETS:
		first = entry->values[1];
		for (index = 0; index < entry->object_count && first + index < MALI_VK_SETS_MAX; index++) {
			if (entry->objects[index] == NULL)
				continue;
			if (entry->values[0] == VK_PIPELINE_BIND_POINT_COMPUTE) {
				build->compute_sets[first + index] = entry->objects[index]->payload;
			} else {
				build->graphics_sets[first + index] = entry->objects[index]->payload;
			}
		}
		break;
	case GPU_OP_CMD_PUSH_CONSTANTS:
		if (entry->data != NULL && entry->values[0] + entry->data_bytes <= MALI_VK_PUSH_BYTES)
			kern_memcpy(&build->push[entry->values[0]], entry->data, entry->data_bytes);
		break;
	case GPU_OP_CMD_BIND_VERTEX_BUFFERS:
		first = entry->values[0];
		for (index = 0; index < entry->object_count && first + index < MALI_VK_VERTEX_BINDINGS_MAX; index++) {
			build->vertex_buffers[first + index] = NULL;
			if (entry->objects[index] != NULL)
				build->vertex_buffers[first + index] = entry->objects[index]->payload;
			build->vertex_offsets[first + index] = entry->wide[index];
		}
		break;
	case GPU_OP_CMD_BIND_INDEX_BUFFER:
		build->index_buffer = NULL;
		if (entry->objects[0] != NULL)
			build->index_buffer = entry->objects[0]->payload;
		build->index_offset = entry->wide[0];
		build->index_type = entry->values[0];
		break;
	case GPU_OP_CMD_SET_VIEWPORT:
		if (entry->data != NULL) {
			kern_memcpy(&build->viewport, entry->data, sizeof(build->viewport));
			build->viewport_set = true;
		}
		break;
	case GPU_OP_CMD_SET_SCISSOR:
		if (entry->data != NULL) {
			kern_memcpy(&build->scissor, entry->data, sizeof(build->scissor));
			build->scissor_set = true;
		}
		break;
	default:
		break;
	}
}

/*
 * vkQueueSubmit: [queue][count][count][submit infos][fence] -> [result].
 * Every command buffer of every info runs in one stream; the fence is
 * signaled once it ended.
 */
static int
submit(
	struct mali_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkSubmitInfo *infos;
	struct mali_vk_command_buffer **commands;
	struct mali_vk_command_buffer *command;
	struct mali_vk_fence *fence;
	uint64_t count;
	uint64_t index;
	uint64_t address;
	uint32_t bytes;
	uint32_t total;
	uint32_t buffer;
	int error;

	/* The queue and the infos. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0 || count > SUBMITS_MAX)
		return EINVAL;
	infos = mali_vk_arena_take(session, (size_t)count * sizeof(*infos) + 8U);
	commands = mali_vk_arena_take(session, COMMAND_BUFFERS_MAX * sizeof(*commands));
	if (infos == NULL || commands == NULL)
		return EINVAL;
	for (index = 0; index < count; index++)
		i915_vkc_dec_VkSubmitInfo(reader, &session->arena, &infos[index]);
	fence = mali_vk_payload(session, I915_VK_OBJ_FENCE, drv_i915_wire_read_u64(reader));
	if (reader->error != 0)
		return EINVAL;

	/* Every command buffer, executable. */
	total = 0;
	error = 0;
	for (index = 0; error == 0 && index < count; index++) {
		for (buffer = 0; buffer < infos[index].commandBufferCount; buffer++) {
			command = mali_vk_payload(session, I915_VK_OBJ_COMMAND_BUFFER, mali_vk_handle(infos[index].pCommandBuffers[buffer]));
			if (command == NULL || command->state != MALI_VK_COMMAND_EXECUTABLE || total == COMMAND_BUFFERS_MAX) {
				error = EINVAL;
				break;
			}
			commands[total] = command;
			total++;
		}
	}

	/* The stream, then its run on the GPU. */
	bytes = 0;
	if (error == 0 && total != 0U)
		error = build_stream(session, commands, total, &address, &bytes);
	if (error == 0 && bytes != 0U)
		error = mali_sched_run(session->owner, address, bytes);
	mali_stage_reset(session->owner);

	/* The fence is signaled once the work ended; a failed one too, so a waiter is not left. */
	if (fence != NULL)
		fence->signaled = true;
	if (error != 0)
		kern_logf("mali: vk: submission of %u command buffers failed (%d)\n", (unsigned)total, error);

	/* Succeeded: the result. */
	drv_i915_wire_reply_u32(reply, mali_vk_result(error));
	return 0;
}

/* Builds one stream of the command buffers' records in the stage. */
static int
build_stream(
	struct mali_vk_session *session,
	struct mali_vk_command_buffer **commands,
	uint32_t count,
	uint64_t *address,
	uint32_t *bytes)
{
	struct mali_vk_build *build;
	volatile uint8_t *view;
	uint32_t index;
	int error;

	/* The front end must have the registers the stream uses. */
	*bytes = 0;
	if (session->owner->render->queue.registers < MALI_VK_REGISTERS_NEEDED + 4U) {
		kern_logf("mali: vk: the command stream has %u registers, %u needed\n",
			  session->owner->render->queue.registers,
			  MALI_VK_REGISTERS_NEEDED + 4U);
		return ENOTSUP;
	}

	/* The build's state. */
	build = kern_calloc(1, sizeof(*build));
	if (build == NULL)
		return ENOMEM;
	build->session = session;

	/* The stream's memory. */
	view = mali_vk_build_take(build, (uint64_t)MALI_VK_STREAM_INSTRUCTIONS * MALI_CS_INSTRUCTION_BYTES, 64U, address);
	if (view == NULL) {
		error = build->error;
		kern_free(build);
		return error;
	}
	mali_cs_init(&build->cs, (volatile uint64_t *)(volatile void *)view, MALI_VK_STREAM_INSTRUCTIONS);

	/* The endpoints, and the scoreboard entries endpoint work and loads and stores signal. */
	mali_cs_emit(&build->cs, mali_cs_request_resource(MALI_CS_RESOURCE_COMPUTE | MALI_CS_RESOURCE_FRAGMENT | MALI_CS_RESOURCE_TILER | MALI_CS_RESOURCE_IDVS));
	mali_cs_emit(&build->cs, mali_cs_set_scoreboard(MALI_VK_SLOT_ENDPOINT, MALI_VK_SLOT_LOAD_STORE));

	/* Each command buffer's records in order. */
	for (index = 0; index < count && build->error == 0; index++)
		run_records(build, commands[index]);

	/* Every run waited for, the endpoints given back. */
	mali_cs_emit(&build->cs, mali_cs_wait(MALI_VK_WAIT_ALL));
	mali_cs_emit(&build->cs, mali_cs_request_resource(0));

	/* A record that could not be built, or a stream that did not fit. */
	error = build->error;
	if (error == 0 && build->cs.error != 0) {
		kern_logf("mali: vk: the command stream did not fit (%u instructions)\n", MALI_VK_STREAM_INSTRUCTIONS);
		error = E2BIG;
	}

	*bytes = mali_cs_bytes(&build->cs);
	kern_free(build);
	if (error != 0)
		return error;

	/* Succeeded: the stream is built. */
	return 0;
}

/* Runs one command buffer's records into the stream. */
static void
run_records(
	struct mali_vk_build *build,
	const struct mali_vk_command_buffer *command)
{
	const struct mali_vk_record *entry;
	uint32_t index;
	int error;

	/* Each record in order; a pass takes its records to its end. */
	for (index = 0; index < command->record_count && build->error == 0; index++) {
		entry = &command->records[index];

		/* One case a recorded command. */
		switch (entry->opcode) {
		case GPU_OP_CMD_BIND_PIPELINE:
		case GPU_OP_CMD_BIND_DESCRIPTOR_SETS:
		case GPU_OP_CMD_PUSH_CONSTANTS:
		case GPU_OP_CMD_BIND_VERTEX_BUFFERS:
		case GPU_OP_CMD_BIND_INDEX_BUFFER:
		case GPU_OP_CMD_SET_VIEWPORT:
		case GPU_OP_CMD_SET_SCISSOR:
			mali_vk_build_bind(build, entry);
			break;
		case GPU_OP_CMD_DISPATCH:
			run_dispatch(build, entry);
			break;
		case GPU_OP_CMD_COPY_BUFFER:
			run_copy_buffer(build, entry);
			break;
		case GPU_OP_CMD_FILL_BUFFER:
			run_fill_buffer(build, entry);
			break;
		case GPU_OP_CMD_COPY_IMAGE:
		case GPU_OP_CMD_BLIT_IMAGE:
			run_copy_image(build, entry);
			break;
		case GPU_OP_CMD_COPY_BUFFER_TO_IMAGE:
			run_buffer_image(build, entry, true);
			break;
		case GPU_OP_CMD_COPY_IMAGE_TO_BUFFER:
			run_buffer_image(build, entry, false);
			break;
		case GPU_OP_CMD_CLEAR_COLOR_IMAGE:
			run_clear_image(build, entry);
			break;
		case GPU_OP_CMD_BEGIN_RENDER_PASS:
			error = mali_vk_draw_pass(build, command, &index);
			if (error != 0 && build->error == 0)
				build->error = error;
			break;
		default:
			/* Draws and clears outside a pass are not valid Vulkan; they are left out. */
			break;
		}
	}
}

/*
 * Runs a dispatch: the program's descriptors, the staging registers of
 * a compute run (the tables, the FAU, the program, the storage, the
 * workgroup size and count), the run, and a wait for it.
 *
 * Procedure referenced:
 * Mesa 26.2.6 src/panfrost/vulkan/csf/panvk_vX_cmd_dispatch.c
 * (cmd_dispatch_shader: the staging registers a compute run reads).
 */
static void
run_dispatch(
	struct mali_vk_build *build,
	const struct mali_vk_record *entry)
{
	const struct mali_shader_program *program;
	volatile uint8_t *view;
	volatile uint32_t *counts;
	uint32_t storage_words[8];
	uint64_t fau;
	uint64_t table;
	uint64_t shader;
	uint64_t storage;
	uint64_t counts_address;
	uint32_t index;
	int error;

	/* A bound compute pipeline without workgroup memory. */
	if (build->compute == NULL || build->compute->program == NULL) {
		build->error = EINVAL;
		return;
	}

	program = build->compute->program;
	if (program->shared_bytes != 0U) {
		kern_logf("mali: vk: a dispatch with workgroup memory is not run\n");
		build->error = ENOTSUP;
		return;
	}

	/* gl_NumWorkGroups, when the program reads it. */
	counts_address = 0;
	for (index = 0; index < program->buffer_count; index++) {
		if (program->buffers[index].set != DRV_GPU_IR_SYSTEM_SET)
			continue;
		view = mali_vk_build_take(build, 16U, 16U, &counts_address);
		if (view == NULL)
			return;
		counts = (volatile uint32_t *)(volatile void *)view;
		counts[0] = entry->values[0];
		counts[1] = entry->values[1];
		counts[2] = entry->values[2];
	}

	/* The FAU and the tables, the program descriptor and the storage. */
	error = mali_vk_build_resources(build, program, build->compute_sets, NULL, NULL, counts_address, &fau, &table);
	if (error != 0) {
		build->error = error;
		return;
	}

	shader = mali_vk_build_shader(build, MALI_DESC_STAGE_COMPUTE, program->work_registers, program->preload, build->compute->code_address);
	view = mali_vk_build_take(build, MALI_DESC_BYTES, 64U, &storage);
	if (view == NULL || shader == 0U)
		return;
	mali_desc_local_storage(storage_words);
	store_words((volatile uint32_t *)(volatile void *)view, storage_words, 8);

	/* The staging registers. */
	mali_cs_move64(&build->cs, MALI_CS_COMPUTE_SRT, table);
	mali_cs_move64(&build->cs, MALI_CS_COMPUTE_FAU, fau);
	mali_cs_move64(&build->cs, MALI_CS_COMPUTE_SPD, shader);
	mali_cs_move64(&build->cs, MALI_CS_COMPUTE_TSD, storage);
	mali_cs_emit(&build->cs, mali_cs_move32(MALI_CS_COMPUTE_GLOBAL_OFFSET, 0));
	mali_cs_emit(&build->cs, mali_cs_move32(MALI_CS_COMPUTE_WORKGROUP, mali_desc_workgroup(program->local_size[0], program->local_size[1], program->local_size[2])));
	mali_cs_emit(&build->cs, mali_cs_move32(MALI_CS_COMPUTE_OFFSET_X, 0));
	mali_cs_emit(&build->cs, mali_cs_move32(MALI_CS_COMPUTE_OFFSET_Y, 0));
	mali_cs_emit(&build->cs, mali_cs_move32(MALI_CS_COMPUTE_OFFSET_Z, 0));
	mali_cs_emit(&build->cs, mali_cs_move32(MALI_CS_COMPUTE_SIZE_X, entry->values[0]));
	mali_cs_emit(&build->cs, mali_cs_move32(MALI_CS_COMPUTE_SIZE_Y, entry->values[1]));
	mali_cs_emit(&build->cs, mali_cs_move32(MALI_CS_COMPUTE_SIZE_Z, entry->values[2]));

	/* One workgroup a task along x, then the wait for the run. */
	mali_cs_emit(&build->cs, mali_cs_run_compute(1, MALI_CS_AXIS_X, 0, 0, 0, 0));
	mali_cs_emit(&build->cs, mali_cs_wait(MALI_VK_WAIT_ALL));
}

/* Runs vkCmdCopyBuffer: each region as a span. */
static void
run_copy_buffer(
	struct mali_vk_build *build,
	const struct mali_vk_record *entry)
{
	const struct mali_vk_buffer *source;
	const struct mali_vk_buffer *destination;
	const VkBufferCopy *regions;
	uint32_t index;

	/* Both buffers, bound. */
	if (entry->objects[0] == NULL || entry->objects[1] == NULL)
		return;
	source = entry->objects[0]->payload;
	destination = entry->objects[1]->payload;
	regions = entry->data;
	if (mali_vk_buffer_address(source) == 0U || mali_vk_buffer_address(destination) == 0U)
		return;

	/* Each region inside both buffers. */
	for (index = 0; index < entry->values[7] && regions != NULL; index++) {
		if (regions[index].srcOffset + regions[index].size > source->size || regions[index].dstOffset + regions[index].size > destination->size)
			continue;
		copy_span(build,
			  mali_vk_buffer_address(destination) + regions[index].dstOffset,
			  mali_vk_buffer_address(source) + regions[index].srcOffset,
			  regions[index].size);
	}
}

/* Runs vkCmdFillBuffer: the range filled with the word. */
static void
run_fill_buffer(
	struct mali_vk_build *build,
	const struct mali_vk_record *entry)
{
	const struct mali_vk_buffer *buffer;
	uint64_t bytes;

	/* A bound buffer. */
	if (entry->objects[0] == NULL)
		return;
	buffer = entry->objects[0]->payload;
	if (mali_vk_buffer_address(buffer) == 0U || entry->wide[0] >= buffer->size)
		return;

	/* VK_WHOLE_SIZE fills to the end, rounded down to words. */
	bytes = entry->wide[1];
	if (bytes == VK_WHOLE_SIZE || bytes > buffer->size - entry->wide[0])
		bytes = (buffer->size - entry->wide[0]) & ~3ULL;

	/* One row. */
	mali_cs_fill_rows(&build->cs, MALI_VK_DATA_REGISTER, MALI_VK_SCRATCH_REGISTER, mali_vk_buffer_address(buffer) + entry->wide[0], 0, entry->values[0], (uint32_t)bytes, 1, MALI_VK_SLOT_LOAD_STORE);
}

/*
 * Runs vkCmdCopyImage, and vkCmdBlitImage of the same size and format:
 * the rows of each region copied between the linear images.
 */
static void
run_copy_image(
	struct mali_vk_build *build,
	const struct mali_vk_record *entry)
{
	const struct mali_vk_image *source;
	const struct mali_vk_image *destination;
	const VkImageCopy *copies;
	const VkImageBlit *blits;
	uint32_t index;
	int32_t sx;
	int32_t sy;
	int32_t dx;
	int32_t dy;
	uint32_t width;
	uint32_t height;
	uint64_t from;
	uint64_t to;

	/* Both images, bound, of one texel size. */
	if (entry->objects[0] == NULL || entry->objects[1] == NULL)
		return;
	source = entry->objects[0]->payload;
	destination = entry->objects[1]->payload;
	if (mali_vk_image_address(source) == 0U || mali_vk_image_address(destination) == 0U || source->format->bytes != destination->format->bytes)
		return;

	/* Each region: a blit only when it neither scales nor flips. */
	for (index = 0; index < entry->values[7] && entry->data != NULL; index++) {
		if (entry->opcode == GPU_OP_CMD_COPY_IMAGE) {
			copies = entry->data;
			sx = copies[index].srcOffset.x;
			sy = copies[index].srcOffset.y;
			dx = copies[index].dstOffset.x;
			dy = copies[index].dstOffset.y;
			width = copies[index].extent.width;
			height = copies[index].extent.height;
		} else {
			blits = entry->data;
			sx = blits[index].srcOffsets[0].x;
			sy = blits[index].srcOffsets[0].y;
			dx = blits[index].dstOffsets[0].x;
			dy = blits[index].dstOffsets[0].y;
			width = (uint32_t)(blits[index].srcOffsets[1].x - sx);
			height = (uint32_t)(blits[index].srcOffsets[1].y - sy);
			if (blits[index].dstOffsets[1].x - dx != (int32_t)width || blits[index].dstOffsets[1].y - dy != (int32_t)height ||
			    source->format != destination->format) {
				if (!build->load_warned)
					kern_logf("mali: vk: a scaling or converting blit is not run\n");
				build->load_warned = true;
				continue;
			}
		}

		/* Inside both images. */
		if (sx < 0 || sy < 0 || dx < 0 || dy < 0 || width == 0U || height == 0U)
			continue;
		if ((uint32_t)sx + width > source->width || (uint32_t)sy + height > source->height)
			continue;
		if ((uint32_t)dx + width > destination->width || (uint32_t)dy + height > destination->height)
			continue;

		/* The rows. */
		from = mali_vk_image_address(source) + (uint64_t)sy * source->row_pitch + (uint64_t)sx * source->format->bytes;
		to = mali_vk_image_address(destination) + (uint64_t)dy * destination->row_pitch + (uint64_t)dx * destination->format->bytes;
		mali_cs_copy_rows(&build->cs,
				  MALI_VK_DATA_REGISTER,
				  MALI_VK_SCRATCH_REGISTER,
				  to,
				  (int32_t)destination->row_pitch,
				  from,
				  (int32_t)source->row_pitch,
				  width * source->format->bytes,
				  height,
				  MALI_VK_SLOT_LOAD_STORE);
	}
}

/* Runs vkCmdCopyBufferToImage or vkCmdCopyImageToBuffer: the rows of each region. */
static void
run_buffer_image(
	struct mali_vk_build *build,
	const struct mali_vk_record *entry,
	bool to_image)
{
	const struct mali_vk_buffer *buffer;
	const struct mali_vk_image *image;
	const VkBufferImageCopy *regions;
	uint64_t buffer_pitch;
	uint64_t image_place;
	uint64_t buffer_place;
	uint32_t row_texels;
	uint32_t index;
	uint32_t width;
	uint32_t height;

	/* The buffer and the image, bound. */
	if (entry->objects[0] == NULL || entry->objects[1] == NULL)
		return;
	if (to_image) {
		buffer = entry->objects[0]->payload;
		image = entry->objects[1]->payload;
	} else {
		image = entry->objects[0]->payload;
		buffer = entry->objects[1]->payload;
	}

	if (mali_vk_buffer_address(buffer) == 0U || mali_vk_image_address(image) == 0U)
		return;

	/* Each region inside the image and the buffer. */
	regions = entry->data;
	for (index = 0; index < entry->values[7] && regions != NULL; index++) {
		width = regions[index].imageExtent.width;
		height = regions[index].imageExtent.height;
		if (regions[index].imageOffset.x < 0 || regions[index].imageOffset.y < 0 || width == 0U || height == 0U)
			continue;
		if ((uint32_t)regions[index].imageOffset.x + width > image->width || (uint32_t)regions[index].imageOffset.y + height > image->height)
			continue;

		/* The buffer's rows are as long as said, or as the region. */
		row_texels = regions[index].bufferRowLength;
		if (row_texels == 0U)
			row_texels = width;
		buffer_pitch = (uint64_t)row_texels * image->format->bytes;
		if (regions[index].bufferOffset + buffer_pitch * (height - 1U) + (uint64_t)width * image->format->bytes > buffer->size)
			continue;

		/* The rows one way or the other. */
		image_place = mali_vk_image_address(image) + (uint64_t)regions[index].imageOffset.y * image->row_pitch + (uint64_t)regions[index].imageOffset.x * image->format->bytes;
		buffer_place = mali_vk_buffer_address(buffer) + regions[index].bufferOffset;
		if (to_image) {
			mali_cs_copy_rows(&build->cs, MALI_VK_DATA_REGISTER, MALI_VK_SCRATCH_REGISTER, image_place, (int32_t)image->row_pitch, buffer_place, (int32_t)buffer_pitch, width * image->format->bytes, height, MALI_VK_SLOT_LOAD_STORE);
		} else {
			mali_cs_copy_rows(&build->cs, MALI_VK_DATA_REGISTER, MALI_VK_SCRATCH_REGISTER, buffer_place, (int32_t)buffer_pitch, image_place, (int32_t)image->row_pitch, width * image->format->bytes, height, MALI_VK_SLOT_LOAD_STORE);
		}
	}
}

/* Runs vkCmdClearColorImage: the image's rows filled with the packed colour (one level, one layer). */
static void
run_clear_image(
	struct mali_vk_build *build,
	const struct mali_vk_record *entry)
{
	const struct mali_vk_image *image;
	uint32_t pattern;

	/* A bound image of a texel size a 32-bit pattern repeats in. */
	if (entry->objects[0] == NULL)
		return;
	image = entry->objects[0]->payload;
	if (mali_vk_image_address(image) == 0U || image->format->bytes > 4U || image->format->bytes == 3U)
		return;

	/* The colour, then the rows. */
	pattern = pack_color(image->format, entry->values);
	mali_cs_fill_rows(&build->cs,
			  MALI_VK_DATA_REGISTER,
			  MALI_VK_SCRATCH_REGISTER,
			  mali_vk_image_address(image),
			  (int32_t)image->row_pitch,
			  pattern,
			  (uint32_t)(((uint64_t)image->width * image->format->bytes + 3U) & ~3ULL),
			  image->height,
			  MALI_VK_SLOT_LOAD_STORE);
}

/* Copies one span: its whole words as one row; a span of other bytes is left out once with a line. */
static void
copy_span(
	struct mali_vk_build *build,
	uint64_t destination,
	uint64_t source,
	uint64_t bytes)
{
	/* Whole words at word addresses. */
	if ((bytes & 3U) != 0U || (destination & 3U) != 0U || (source & 3U) != 0U || bytes > 0xffffffffULL) {
		kern_logf("mali: vk: a copy of %llu bytes not in words is not run\n", (unsigned long long)bytes);
		return;
	}

	/* One row. */
	mali_cs_copy_rows(&build->cs, MALI_VK_DATA_REGISTER, MALI_VK_SCRATCH_REGISTER, destination, 0, source, 0, (uint32_t)bytes, 1, MALI_VK_SLOT_LOAD_STORE);
}

/*
 * Packs a clear colour (the float bits of the union) into the texel of
 * a format of 8-bit components, repeated to 32 bits: R, G, B, A in
 * memory order (B and R exchanged for a BGRA format).
 */
static uint32_t
pack_color(
	const struct mali_vk_format *format,
	const uint32_t *words)
{
	uint32_t red;
	uint32_t green;
	uint32_t blue;
	uint32_t alpha;
	uint32_t texel;

	/* The components as 8-bit unsigned normalized. */
	red = unorm8(words[0]);
	green = unorm8(words[1]);
	blue = unorm8(words[2]);
	alpha = unorm8(words[3]);

	/* The memory order of the format. */
	if (format->vk == VK_FORMAT_B8G8R8A8_UNORM || format->vk == VK_FORMAT_B8G8R8A8_SRGB) {
		texel = blue | (green << 8) | (red << 16) | (alpha << 24);
	} else if (format->bytes == 1U) {
		texel = red * 0x01010101U;
	} else if (format->bytes == 2U) {
		texel = (red | (green << 8)) * 0x00010001U;
	} else {
		texel = red | (green << 8) | (blue << 16) | (alpha << 24);
	}

	/* Succeeded: the pattern. */
	return texel;
}

/*
 * Converts a float's bits to an 8-bit unsigned normalized value, rounded
 * to nearest, without floating-point instructions: zero for a negative
 * value or NaN, 255 for one and more.
 */
static uint32_t
unorm8(
	uint32_t bits)
{
	uint64_t mantissa;
	uint32_t exponent;
	uint32_t shift;

	/* A negative value, a NaN or zero. */
	exponent = (bits >> 23) & 0xffU;
	if ((bits & 0x80000000U) != 0U || exponent == 0U)
		return 0;
	if (exponent == 0xffU && (bits & 0x7fffffU) != 0U)
		return 0;

	/* One and more. */
	if (exponent >= 127U)
		return 255;

	/* (1.m * 2^(e - 127)) * 255, rounded. */
	mantissa = (uint64_t)((bits & 0x7fffffU) | 0x800000U) * 255U;
	shift = 23U + (127U - exponent);
	if (shift >= 63U)
		return 0;
	return (uint32_t)((mantissa + (1ULL << (shift - 1U))) >> shift);
}

/* Finds the descriptor of a (set, binding) among the bound sets, or NULL. */
static const struct mali_vk_descriptor *
find_slot(
	const struct mali_vk_descriptor_set *const *sets,
	uint32_t set,
	uint32_t binding)
{
	const struct mali_vk_descriptor_set *bound;
	uint32_t index;

	/* A bound set. */
	if (set >= MALI_VK_SETS_MAX || sets[set] == NULL)
		return NULL;
	bound = sets[set];

	/* The binding's slot. */
	for (index = 0; index < bound->layout.count; index++) {
		if (bound->layout.bindings[index].binding == binding)
			return &bound->slots[index];
	}

	/* No such binding. */
	return NULL;
}

/* Writes a combined image sampler's texture (with its plane after it) and sampler descriptors. */
static int
write_texture(
	struct mali_vk_build *build,
	const struct mali_vk_descriptor *slot,
	volatile uint32_t *texture,
	volatile uint32_t *sampler)
{
	const struct mali_vk_image_view *view;
	const struct mali_vk_sampler *state;
	const struct mali_vk_image *image;
	uint32_t words[8];
	volatile uint8_t *plane;
	uint64_t plane_address;
	uint64_t address;

	/* A view of a bound image, and its sampler; a missing one leaves null descriptors. */
	if (slot == NULL || slot->view == NULL || slot->sampler == NULL)
		return 0;
	view = slot->view->payload;
	state = slot->sampler->payload;
	image = view->image->payload;
	address = mali_vk_image_address(image);
	if (address == 0U)
		return 0;

	/* The plane. */
	plane = mali_vk_build_take(build, MALI_DESC_BYTES, MALI_DESC_BYTES, &plane_address);
	if (plane == NULL)
		return build->error;
	mali_desc_plane(words, view->format->bytes, address, (uint32_t)image->row_pitch, image->layer_bytes, image->width, image->height);
	store_words((volatile uint32_t *)(volatile void *)plane, words, 8);

	/* The texture, naming the plane, and the sampler. */
	mali_desc_texture(words, view->format->pixel, image->width, image->height, view->swizzle, plane_address);
	store_words(texture, words, 8);
	store_words(sampler, state->words, 8);

	/* Succeeded: both are written. */
	return 0;
}

/* Stores words through an uncached view. */
static void
store_words(
	volatile uint32_t *destination,
	const uint32_t *source,
	unsigned count)
{
	unsigned index;

	for (index = 0; index < count; index++)
		destination[index] = source[index];
}
