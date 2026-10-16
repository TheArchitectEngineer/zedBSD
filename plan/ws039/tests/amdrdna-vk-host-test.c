/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws039-p008: host test of the amdrdna Vulkan executor.  The commands are
 * written as libvulkan writes them: its wire writer, its command prefixes
 * (resources-generated.inc, memory.c) and its own codec's structure
 * encoders, through the executor's real dispatcher.  The node's side is
 * faked: the blobs, the storage it attaches, the executor's GPU memory and
 * the runs, whose indirect buffers are decoded packet by packet into the
 * registers they set.  Every draw is then replayed on the GFX10
 * interpreter of the compiler's tests (gfx10-machine.c): its programs and
 * resource tables as the registers name them, the pixels it would write
 * compared with what the copy, the clear or the sample should give.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uapi/errno.h>
#include <uapi/gpu-op.h>

#include "drivers/gpu/amdrdna/vk/amdrdna-vk-objects.h"
#include "drivers/gpu/amdrdna/vk/amdrdna-vk-exec.h"
#include "userland/desktop/libvulkan/internal.h"
#include "userland/desktop/libvulkan/codec.h"
#include "userland/desktop/wayland/shaders.h"
#include "plan/ws039/tests/gfx10-machine.h"

/* The reply blob's id, where its replies start, and the device's identity. */
#define TEST_REPLY_BLOB		1U
#define TEST_REPLY_AT		16U
#define TEST_DEVICE		0x102U

static unsigned failures;
static unsigned checks;
static uint32_t reply_words[2048];
static unsigned allocations;
static unsigned detached;
static void *detached_backing;
static uint8_t storage[0x20000];
static int backing_token;
static int import_token;

static void check(int ok, const char *what);
static void frame_begin(struct vulkan_writer *writer);
static int frame_run(struct amdrdna_vk_session *session, struct vulkan_writer *writer);
static uint32_t reply_u32(unsigned *at);
static uint64_t reply_u64(unsigned *at);
static void command_begin(struct vulkan_writer *writer, uint32_t opcode);
static void write_tail(struct vulkan_writer *writer, uint64_t identity);
static void write_allocate(struct vulkan_writer *writer, uint64_t identity, uint64_t bytes, uint32_t imported);
static void write_image(struct vulkan_writer *writer, uint64_t identity, uint32_t format, uint32_t width, uint32_t height, uint32_t levels);
static void write_buffer(struct vulkan_writer *writer, uint64_t identity, uint64_t bytes);
static void write_pair(struct vulkan_writer *writer, uint32_t opcode, uint64_t first, uint64_t second);
static void write_bind(struct vulkan_writer *writer, uint32_t opcode, uint64_t resource, uint64_t memory, uint64_t offset);
static void test_memory(void);
static void test_views(void);
static uint32_t *read_spirv(const char *directory, const char *name, size_t *bytes);
static void write_create(struct vulkan_writer *writer, uint32_t opcode);
static void test_pipeline(const char *directory);
static void write_quad_pipeline(struct vulkan_writer *client, struct vulkan_object *handles, uint64_t identity, unsigned vertex, unsigned fragment, uint64_t layout, uint64_t pass);
static void record_begin(struct vulkan_writer *writer, uint32_t opcode, uint64_t command);
static void test_submit(const char *directory);
static void test_compositor_shaders(void);
static void test_units(void);
static void test_kernel(const char *directory);

void *
kern_malloc(size_t bytes)
{
	allocations++;
	return malloc(bytes);
}

void *
kern_calloc(size_t count, size_t bytes)
{
	allocations++;
	return calloc(count, bytes);
}

void
kern_free(void *pointer)
{
	if (pointer != NULL)
		allocations--;
	free(pointer);
}

void
kern_logf(const char *format, ...)
{
	(void)format;
}

/* libvulkan's handles are its objects' addresses (objects.c). */
uint64_t
vulkan_object_wire_id(const struct vulkan_object *object)
{
	if (object == NULL)
		return 0;
	return object->wire_id;
}

struct vulkan_object *
vulkan_nondispatchable_object(uint64_t handle)
{
	return (struct vulkan_object *)(uintptr_t)handle;
}

/* The one blob: the reply array. */
int
amdrdna_vk_blob(void *owner, uint32_t resource_id, uint8_t **cpu, uint64_t *bytes)
{
	(void)owner;
	if (resource_id != TEST_REPLY_BLOB)
		return ENOENT;
	*cpu = (uint8_t *)reply_words;
	*bytes = sizeof(reply_words);
	return 0;
}

uint32_t
amdrdna_vk_device_id(void *owner)
{
	(void)owner;
	return 0x1f02U;
}

uint64_t
amdrdna_vk_heap_bytes(void *owner)
{
	(void)owner;
	return 240ULL << 20;
}

void
amdrdna_vk_memory_detach(void *owner, void *backing, uint64_t va, uint64_t bytes)
{
	(void)owner;
	(void)va;
	(void)bytes;
	detached++;
	detached_backing = backing;
}

/* Resource 7 is a blob of 0x10000 bytes the session can import. */
int
amdrdna_vk_memory_import(void *owner, uint32_t resource_id, uint64_t bytes, uint8_t **cpu, uint64_t *va, void **backing)
{
	(void)owner;
	if (resource_id != 7U)
		return ENOENT;
	if (bytes > 0x10000U)
		return EINVAL;
	*cpu = storage + 0x10000U;
	*va = 0x31000000ULL;
	*backing = &import_token;
	return 0;
}

/* The executor's GPU memory: each run calloc'd, at its own GPU address. */
struct test_run {
	uint64_t va;
	uint8_t *cpu;
	uint64_t bytes;
};

static struct test_run runs[16];
static unsigned run_count;
static unsigned gpu_runs;
static unsigned gpu_draws;
static unsigned gpu_quads;
static unsigned gpu_copies;
static uint64_t gpu_copied_bytes;

/*
 * What the runs' indirect buffers did, decoded: every packet in order (its
 * operation, its body's words and the first of them), the registers as the
 * setters left them (by space: context from 0x28000, shader from 0xb000,
 * user configuration from 0x30000), and at each draw and dispatch a copy of
 * the registers then.
 */
#define LOG_PACKETS		16384U
#define LOG_BODY		8U
#define LOG_REGISTERS		0x400U
#define LOG_DRAWS		16U

struct packet_entry {
	uint32_t op;
	uint32_t words;
	uint32_t compute;
	uint32_t run;
	uint32_t body[LOG_BODY];
};

struct state_entry {
	uint32_t context[LOG_REGISTERS];
	uint32_t shader[LOG_REGISTERS];
	uint32_t user[LOG_REGISTERS];
	uint32_t vertices;
	uint32_t instances;
	uint32_t groups[3];
};

struct replay;
static void replay_draw(const struct state_entry *state, struct replay *out);
static struct replay *replays[16];

static struct packet_entry packet_log[LOG_PACKETS];
static unsigned packet_count;
static struct state_entry registers;
static struct state_entry draw_log[LOG_DRAWS];
static unsigned draw_count;
static struct state_entry dispatch_log[2];
static unsigned dispatch_count;
static unsigned decode_errors;
static uint32_t ib_words_total;

int
amdrdna_vk_gpu_alloc(void *owner, uint64_t bytes, void **token, uint8_t **cpu, uint64_t *va)
{
	(void)owner;
	if (run_count >= 16U)
		return ENOMEM;
	*cpu = calloc(1U, (size_t)bytes);
	if (*cpu == NULL)
		return ENOMEM;
	runs[run_count].va = 0x40000000ULL + (uint64_t)run_count * 0x01000000ULL;
	runs[run_count].cpu = *cpu;
	runs[run_count].bytes = bytes;
	*token = *cpu;
	*va = runs[run_count].va;
	run_count++;
	return 0;
}

void
amdrdna_vk_gpu_free(void *owner, void *token)
{
	unsigned index;

	(void)owner;
	for (index = 0; index < run_count; index++) {
		if (runs[index].cpu == token)
			runs[index].cpu = NULL;
	}
	free(token);
}

/* Counts the CPU's copies the executor reports. */
void
amdrdna_vk_gpu_copied(void *owner, uint64_t bytes)
{
	(void)owner;
	gpu_copies++;
	gpu_copied_bytes += bytes;
}

/* Counts the draws and the executor's own quads of the runs. */
void
amdrdna_vk_gpu_drawn(void *owner, uint32_t draws, uint32_t quads)
{
	(void)owner;
	gpu_draws += draws;
	gpu_quads += quads;
}

/* Copies a command stream out of a blob, as the node does through its window. */
void
amdrdna_vk_blob_read(void *owner, uint8_t *to, const uint8_t *from, size_t length)
{
	(void)owner;
	memcpy(to, from, length);
}

/* Reports the cpu of a GPU address the fakes handed out, and the bytes from it to its run's end. */
static uint8_t *
gpu_cpu_room(uint64_t va, uint64_t *room)
{
	unsigned index;

	for (index = 0; index < run_count; index++) {
		if (runs[index].cpu != NULL && va >= runs[index].va && va < runs[index].va + runs[index].bytes) {
			if (room != NULL)
				*room = runs[index].va + runs[index].bytes - va;
			return runs[index].cpu + (va - runs[index].va);
		}
	}
	return NULL;
}

static uint8_t *
gpu_cpu(uint64_t va)
{
	return gpu_cpu_room(va, NULL);
}

/* Applies a setter's values to a register space, counting a register outside it. */
static void
decode_set(uint32_t *space, uint32_t first, const uint32_t *values, uint32_t count)
{
	uint32_t index;

	for (index = 0; index < count; index++) {
		if (first + index >= LOG_REGISTERS) {
			decode_errors++;
			continue;
		}
		space[first + index] = values[index];
	}
}

/*
 * Decodes an indirect buffer as the command processor reads it: type-3
 * packets of known operations, the setters applied, each draw and
 * dispatch logged with the registers then.  Anything else is EIO.
 */
static int
decode_buffer(const uint32_t *words, uint32_t count, uint32_t run)
{
	struct packet_entry *entry;
	uint32_t header;
	uint32_t at;
	uint32_t op;
	uint32_t body;
	uint32_t index;
	uint32_t instances;

	at = 0;
	instances = 1;
	while (at < count) {
		header = words[at];
		if ((header >> 30) != 3U) {
			decode_errors++;
			return EIO;
		}
		op = (header >> 8) & 0xffU;
		body = ((header >> 16) & 0x3fffU) + 1U;
		if (op == 0x10U && body == 0x4000U) {
			at++;
			continue;
		}
		if (at + 1U + body > count) {
			decode_errors++;
			return EIO;
		}
		if (packet_count < LOG_PACKETS) {
			entry = &packet_log[packet_count++];
			entry->op = op;
			entry->words = body;
			entry->compute = (header >> 1) & 1U;
			entry->run = run;
			for (index = 0; index < LOG_BODY; index++)
				entry->body[index] = index < body ? words[at + 1U + index] : 0U;
		}
		switch (op) {
		case 0x69U:
			decode_set(registers.context, words[at + 1U], &words[at + 2U], body - 1U);
			break;
		case 0x76U:
			decode_set(registers.shader, words[at + 1U], &words[at + 2U], body - 1U);
			break;
		case 0x79U:
			decode_set(registers.user, words[at + 1U], &words[at + 2U], body - 1U);
			break;
		case 0x7aU:
			if ((words[at + 1U] >> 28) != 1U)
				decode_errors++;
			decode_set(registers.user, words[at + 1U] & 0xffffU, &words[at + 2U], body - 1U);
			break;
		case 0x2fU:
			instances = words[at + 1U];
			break;
		case 0x2dU:
			if (body != 2U || words[at + 2U] != 2U)
				decode_errors++;
			if (draw_count < LOG_DRAWS) {
				draw_log[draw_count] = registers;
				draw_log[draw_count].vertices = words[at + 1U];
				draw_log[draw_count].instances = instances;
				draw_count++;
			}
			break;
		case 0x15U:
			if (body != 4U || ((header >> 1) & 1U) == 0U)
				decode_errors++;
			if (dispatch_count < 2U) {
				dispatch_log[dispatch_count] = registers;
				dispatch_log[dispatch_count].groups[0] = words[at + 1U];
				dispatch_log[dispatch_count].groups[1] = words[at + 2U];
				dispatch_log[dispatch_count].groups[2] = words[at + 3U];
				dispatch_count++;
			}
			break;
		case 0x10U:
		case 0x12U:
		case 0x28U:
		case 0x3cU:
		case 0x42U:
		case 0x46U:
		case 0x49U:
		case 0x58U:
			break;
		default:
			decode_errors++;
			return EIO;
		}
		at += 1U + body;
	}
	return 0;
}

/* Decodes a run's indirect buffer (a whole number of eight words) into the logs. */
int
amdrdna_vk_gpu_run(void *owner, uint64_t va, uint32_t bytes)
{
	const uint8_t *buffer;
	uint64_t room;
	unsigned first;
	int error;

	(void)owner;
	buffer = gpu_cpu_room(va, &room);
	if (buffer == NULL || bytes > room || (bytes % 32U) != 0U || bytes == 0U)
		return EIO;
	gpu_runs++;
	ib_words_total += bytes / 4U;
	first = draw_count;
	error = decode_buffer((const uint32_t *)(const void *)buffer, bytes / 4U, gpu_runs);
	if (error != 0)
		return error;

	/* Each draw replayed now, while the run's tables hold what it wrote (the next run writes them anew). */
	for (; first < draw_count; first++)
		replay_draw(&draw_log[first], replays[first]);
	return 0;
}

/* A register of a logged state, by its byte offset. */
static uint32_t
reg(const struct state_entry *state, uint32_t offset)
{
	if (offset >= 0x30000U)
		return state->user[(offset - 0x30000U) / 4U];
	if (offset >= 0x28000U)
		return state->context[(offset - 0x28000U) / 4U];
	return state->shader[(offset - 0xb000U) / 4U];
}

/* Counts the logged packets of an operation whose first body word is a value (any value when every is set). */
static unsigned
packets_with(uint32_t op, uint32_t first, int every)
{
	unsigned index;
	unsigned found;

	found = 0;
	for (index = 0; index < packet_count; index++) {
		if (packet_log[index].op == op && (every || packet_log[index].body[0] == first))
			found++;
	}
	return found;
}

/* Resets the logs before a submission. */
static void
log_reset(void)
{
	packet_count = 0;
	draw_count = 0;
	dispatch_count = 0;
	decode_errors = 0;
	ib_words_total = 0;
	memset(&registers, 0, sizeof(registers));
}

/*
 * The replay of a logged draw on the GFX10 interpreter: the programs the
 * registers name, their resource tables from the user SGPRs, all in the
 * fake GPU memory the executor wrote.  The vertex wave runs the draw's
 * vertices; its positions go through the viewport the registers hold;
 * eight 2x2 quads of pixels spread over the scissor are interpolated from
 * its parameters (the two triangles share one plane, as a quad's do) and
 * run as one pixel wave.  The replay gives each lane's pixel and colour.
 */
#define REPLAY_LANES		32U

struct replay {
	int32_t x[REPLAY_LANES];
	int32_t y[REPLAY_LANES];
	float colour[REPLAY_LANES][4];
	float position[6][4];
	float screen[6][2];
	uint8_t vertex_table[2048];
	uint8_t fragment_table[2048];
	const char *error;
};

static float replay_attributes[2][32][4][3];
static struct replay replay_store[LOG_DRAWS];
static struct machine_graphics replay_graphics;
static uint8_t scene[0x40000];

static float
bits_float(uint32_t bits)
{
	float value;

	memcpy(&value, &bits, 4);
	return value;
}

/* The fake GPU memory as the interpreter's regions: every live run and the scene's memory at 0x30000000. */
static void
replay_memory(struct machine_memory *memory)
{
	unsigned index;

	memset(memory, 0, sizeof(*memory));
	for (index = 0; index < run_count && memory->count < MACHINE_REGIONS - 1U; index++) {
		if (runs[index].cpu == NULL)
			continue;
		memory->regions[memory->count].address = runs[index].va;
		memory->regions[memory->count].bytes = runs[index].cpu;
		memory->regions[memory->count].size = (uint32_t)runs[index].bytes;
		memory->count++;
	}
	memory->regions[memory->count].address = 0x30000000ULL;
	memory->regions[memory->count].bytes = scene;
	memory->regions[memory->count].size = sizeof(scene);
	memory->count++;
}

/* A program the registers name: its code (to its run's end) and VGPRs. */
static int
replay_program(const struct state_entry *state, uint32_t low, uint32_t high, uint32_t resources, struct machine_dispatch *dispatch)
{
	uint64_t va;
	uint64_t room;
	const uint8_t *code;

	va = ((uint64_t)reg(state, low) << 8) | ((uint64_t)(reg(state, high) & 0xffU) << 40);
	code = gpu_cpu_room(va, &room);
	if (code == NULL || (va & 0xffU) != 0U)
		return 0;
	dispatch->code = (const uint32_t *)(const void *)code;
	dispatch->code_words = (uint32_t)(room / 4U);
	dispatch->vgpr_count = ((reg(state, resources) & 0x3fU) + 1U) * 8U;
	return 1;
}

/* Copies the resource table a program's user SGPRs name (its first 2048 bytes, as the run holds them now). */
static void
replay_table(const struct state_entry *state, uint32_t user, uint8_t *copy)
{
	const uint8_t *table;
	uint64_t room;

	table = gpu_cpu_room(reg(state, user) | ((uint64_t)reg(state, user + 4U) << 32), &room);
	if (table != NULL)
		memcpy(copy, table, room < 2048U ? (size_t)room : 2048U);
}

/* Replays one logged draw of six vertices (a quad); out->error says why it could not. */
static void
replay_draw(const struct state_entry *state, struct replay *out)
{
	struct machine_memory memory;
	struct machine_dispatch dispatch;
	struct machine_result result;
	int32_t x0;
	int32_t y0;
	int32_t x1;
	int32_t y1;
	int32_t qx;
	int32_t qy;
	uint32_t lane;
	uint32_t quad;
	uint32_t vertex;
	uint32_t location;
	uint32_t channel;
	uint32_t triangle;
	float ax;
	float ay;
	float bx;
	float by;
	float cx;
	float cy;
	float det;
	float px;
	float py;
	float i;
	float j;

	memset(out, 0, sizeof(*out));
	replay_table(state, 0xb130U, out->vertex_table);
	replay_table(state, 0xb030U, out->fragment_table);
	if (state->vertices != 6U) {
		out->error = "not a quad's six vertices";
		return;
	}
	replay_memory(&memory);

	/* The vertex wave. */
	memset(&dispatch, 0, sizeof(dispatch));
	if (!replay_program(state, 0xb120U, 0xb124U, 0xb128U, &dispatch)) {
		out->error = "the vertex program's address";
		return;
	}
	dispatch.table = reg(state, 0xb130U) | ((uint64_t)reg(state, 0xb134U) << 32);
	memset(&replay_graphics, 0, sizeof(replay_graphics));
	replay_graphics.user[0] = reg(state, 0xb138U);
	replay_graphics.user[1] = reg(state, 0xb13cU);
	replay_graphics.user[2] = reg(state, 0xb140U);
	replay_graphics.lanes = 6U;
	machine_run_graphics(&dispatch, &memory, 0, &replay_graphics, &result);
	if (result.error != NULL) {
		out->error = result.error;
		return;
	}

	/* The positions through the viewport; the parameters as the triangles' attributes. */
	for (vertex = 0; vertex < 6U; vertex++) {
		for (channel = 0; channel < 4U; channel++)
			out->position[vertex][channel] = bits_float(replay_graphics.exported[12][vertex][channel]);
		out->screen[vertex][0] = out->position[vertex][0] / out->position[vertex][3] * bits_float(reg(state, 0x2843cU)) +
					 bits_float(reg(state, 0x28440U));
		out->screen[vertex][1] = out->position[vertex][1] / out->position[vertex][3] * bits_float(reg(state, 0x28444U)) +
					 bits_float(reg(state, 0x28448U));
		for (location = 0; location < 32U; location++) {
			for (channel = 0; channel < 4U; channel++)
				replay_attributes[vertex / 3U][location][channel][vertex % 3U] =
				    bits_float(replay_graphics.exported[32U + location][vertex][channel]);
		}
	}

	/* Eight quads over the scissor, on its anti-diagonal. */
	x0 = (int32_t)(reg(state, 0x28240U) & 0x7fffU);
	y0 = (int32_t)((reg(state, 0x28240U) >> 16) & 0x7fffU);
	x1 = (int32_t)(reg(state, 0x28244U) & 0x7fffU);
	y1 = (int32_t)((reg(state, 0x28244U) >> 16) & 0x7fffU);
	if (x1 - x0 < 2 || y1 - y0 < 2) {
		out->error = "a scissor too small for a quad of pixels";
		return;
	}
	memset(&replay_graphics, 0, sizeof(replay_graphics));
	replay_graphics.attribute = (const float (*)[32][4][3])replay_attributes;
	for (quad = 0; quad < 8U; quad++) {
		qx = x0 + ((x1 - x0 - 2) * (int32_t)quad) / 7;
		qy = y0 + ((y1 - y0 - 2) * (int32_t)(7U - quad)) / 7;
		for (lane = quad * 4U; lane < quad * 4U + 4U; lane++) {
			out->x[lane] = qx + (int32_t)(lane & 1U);
			out->y[lane] = qy + (int32_t)((lane >> 1) & 1U);
			px = (float)out->x[lane] + 0.5f;
			py = (float)out->y[lane] + 0.5f;

			/* The barycentrics in the first triangle's plane (the second's is the same for a quad). */
			triangle = 0;
			ax = out->screen[0][0];
			ay = out->screen[0][1];
			bx = out->screen[1][0];
			by = out->screen[1][1];
			cx = out->screen[2][0];
			cy = out->screen[2][1];
			det = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
			if (det == 0.0f) {
				out->error = "a degenerate triangle";
				return;
			}
			i = ((px - ax) * (cy - ay) - (cx - ax) * (py - ay)) / det;
			j = ((bx - ax) * (py - ay) - (px - ax) * (by - ay)) / det;
			replay_graphics.perspective[lane][0] = i;
			replay_graphics.perspective[lane][1] = j;
			replay_graphics.linear[lane][0] = i;
			replay_graphics.linear[lane][1] = j;
			replay_graphics.position[lane][0] = px;
			replay_graphics.position[lane][1] = py;
			replay_graphics.position[lane][3] = 1.0f;
			replay_graphics.face[lane] = 1.0f;
			replay_graphics.primitive[lane] = triangle;
		}
	}
	replay_graphics.coverage = 0xffffffffU;

	/* The pixel wave. */
	memset(&dispatch, 0, sizeof(dispatch));
	if (!replay_program(state, 0xb020U, 0xb024U, 0xb028U, &dispatch)) {
		out->error = "the pixel program's address";
		return;
	}
	dispatch.table = reg(state, 0xb030U) | ((uint64_t)reg(state, 0xb034U) << 32);
	dispatch.pixel_inputs = reg(state, 0x286ccU);
	machine_run_graphics(&dispatch, &memory, 1, &replay_graphics, &result);
	if (result.error != NULL) {
		out->error = result.error;
		return;
	}
	for (lane = 0; lane < REPLAY_LANES; lane++) {
		for (channel = 0; channel < 4U; channel++)
			out->colour[lane][channel] = bits_float(replay_graphics.exported[0][lane][channel]);
	}
}

/* Reports whether two colours are within a tolerance, channel by channel. */
static int
colour_near(const float *got, const float *expected, float tolerance)
{
	unsigned channel;

	for (channel = 0; channel < 4U; channel++) {
		if (fabsf(got[channel] - expected[channel]) > tolerance)
			return 0;
	}
	return 1;
}

/* An RGBA colour from four bytes in memory in R, G, B, A order, or in B, G, R, A order when blue_first. */
static void
bytes_colour(const uint8_t *bytes, int blue_first, float *colour)
{
	colour[0] = (float)bytes[blue_first ? 2 : 0] / 255.0f;
	colour[1] = (float)bytes[1] / 255.0f;
	colour[2] = (float)bytes[blue_first ? 0 : 2] / 255.0f;
	colour[3] = (float)bytes[3] / 255.0f;
}

/* The bilinear sample of an RGBA image of 32x32 (rows of 256 bytes) at normalized (u, v), wrapping. */
static void
bilinear_reference(const uint8_t *image, float u, float v, float *colour)
{
	float x;
	float y;
	float fx;
	float fy;
	int32_t ix;
	int32_t iy;
	int32_t sx;
	int32_t sy;
	float texel[4][4];
	unsigned corner;
	unsigned channel;

	x = u * 32.0f - 0.5f;
	y = v * 32.0f - 0.5f;
	ix = (int32_t)floorf(x);
	iy = (int32_t)floorf(y);
	fx = x - (float)ix;
	fy = y - (float)iy;
	for (corner = 0; corner < 4U; corner++) {
		sx = (ix + (int32_t)(corner & 1U) + 32) % 32;
		sy = (iy + (int32_t)(corner >> 1) + 32) % 32;
		bytes_colour(image + sy * 256 + sx * 4, 0, texel[corner]);
	}
	for (channel = 0; channel < 4U; channel++) {
		colour[channel] = (texel[0][channel] * (1.0f - fx) + texel[1][channel] * fx) * (1.0f - fy) +
				  (texel[2][channel] * (1.0f - fx) + texel[3][channel] * fx) * fy;
	}
}

int
main(int argc, char **argv)
{
	unsigned index;

	setvbuf(stdout, NULL, _IONBF, 0);
	for (index = 0; index < LOG_DRAWS; index++)
		replays[index] = &replay_store[index];
	if (argc != 2) {
		printf("usage: amdrdna-vk-host-test spirv-directory\n");
		return 2;
	}
	test_memory();
	test_views();
	test_pipeline(argv[1]);
	test_submit(argv[1]);
	test_units();
	test_kernel(argv[1]);
	test_compositor_shaders();
	printf("%s amdrdna vk: %u checks, %u failures\n", failures == 0 ? "PASS" : "FAIL", checks, failures);
	return failures == 0 ? 0 : 1;
}

static void
check(int ok, const char *what)
{
	checks++;
	if (!ok) {
		printf("FAIL %s\n", what);
		failures++;
	}
}

/* Starts a frame: the reply stream is the reply blob from TEST_REPLY_AT. */
static void
frame_begin(struct vulkan_writer *writer)
{
	vulkan_writer_init(writer);
	memset(reply_words, 0, sizeof(reply_words));
	vulkan_write_u32(writer, GPU_OP_SET_REPLY_STREAM);
	vulkan_write_u32(writer, 0);
	vulkan_write_u64(writer, 1);
	vulkan_write_u32(writer, TEST_REPLY_BLOB);
	vulkan_write_u64(writer, TEST_REPLY_AT);
	vulkan_write_u64(writer, sizeof(reply_words) - TEST_REPLY_AT);
}

/* Runs the frame through the transport and the real dispatcher. */
static int
frame_run(struct amdrdna_vk_session *session, struct vulkan_writer *writer)
{
	int error;

	error = amdrdna_vk_stream_execute(session, writer->data, (uint32_t)writer->bytes, amdrdna_vk_dispatch);
	vulkan_writer_finish(writer);
	return error;
}

static uint32_t
reply_u32(unsigned *at)
{
	uint32_t word;

	word = reply_words[*at];
	(*at)++;
	return word;
}

static uint64_t
reply_u64(unsigned *at)
{
	uint64_t low;
	uint64_t high;

	low = reply_u32(at);
	high = reply_u32(at);
	return low | (high << 32);
}

/* A command's header as vulkan_command_begin() writes it, appended to the frame (that one starts the writer over). */
static void
command_begin(struct vulkan_writer *writer, uint32_t opcode)
{
	vulkan_write_u32(writer, opcode);
	vulkan_write_u32(writer, GPU_OP_FLAG_REPLY);
}

/* The create's tail (vulkan_object_create_complete): no allocator, the output present, the identity. */
static void
write_tail(struct vulkan_writer *writer, uint64_t identity)
{
	vulkan_write_u64(writer, 0);
	vulkan_write_u64(writer, 1);
	vulkan_write_u64(writer, identity);
}

/* vkAllocateMemory as memory.c writes it, with an import when imported is nonzero. */
static void
write_allocate(struct vulkan_writer *writer, uint64_t identity, uint64_t bytes, uint32_t imported)
{
	command_begin(writer, GPU_OP_ALLOCATE_MEMORY);
	vulkan_write_u64(writer, TEST_DEVICE);
	vulkan_write_u64(writer, 1);
	vulkan_write_u32(writer, VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
	if (imported != 0U) {
		vulkan_write_u64(writer, 1);
		vulkan_write_u32(writer, GPU_OP_RECORD_IMPORT_RESOURCE);
		vulkan_write_u64(writer, 0);
		vulkan_write_u32(writer, imported);
	} else {
		vulkan_write_u64(writer, 0);
	}
	vulkan_write_u64(writer, bytes);
	vulkan_write_u32(writer, 0);
	write_tail(writer, identity);
}

/* vkCreateImage as resources-generated.inc writes it. */
static void
write_image(struct vulkan_writer *writer, uint64_t identity, uint32_t format, uint32_t width, uint32_t height, uint32_t levels)
{
	VkImageCreateInfo info;

	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = (VkFormat)format;
	info.extent.width = width;
	info.extent.height = height;
	info.extent.depth = 1;
	info.mipLevels = levels;
	info.arrayLayers = 1;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	command_begin(writer, GPU_OP_CREATE_IMAGE);
	vulkan_write_u64(writer, TEST_DEVICE);
	vulkan_write_u64(writer, 1);
	vulkan_encode_VkImageCreateInfo(writer, &info);
	write_tail(writer, identity);
}

/* vkCreateBuffer as resources-generated.inc writes it. */
static void
write_buffer(struct vulkan_writer *writer, uint64_t identity, uint64_t bytes)
{
	VkBufferCreateInfo info;

	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	info.size = bytes;
	info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	command_begin(writer, GPU_OP_CREATE_BUFFER);
	vulkan_write_u64(writer, TEST_DEVICE);
	vulkan_write_u64(writer, 1);
	vulkan_encode_VkBufferCreateInfo(writer, &info);
	write_tail(writer, identity);
}

/* A command of [device][first][second]: a destroy (second the allocator) or a requirements query (second the output). */
static void
write_pair(struct vulkan_writer *writer, uint32_t opcode, uint64_t first, uint64_t second)
{
	command_begin(writer, opcode);
	vulkan_write_u64(writer, TEST_DEVICE);
	vulkan_write_u64(writer, first);
	vulkan_write_u64(writer, second);
}

/* vkBindBufferMemory and vkBindImageMemory: [device][resource][memory][offset]. */
static void
write_bind(struct vulkan_writer *writer, uint32_t opcode, uint64_t resource, uint64_t memory, uint64_t offset)
{
	command_begin(writer, opcode);
	vulkan_write_u64(writer, TEST_DEVICE);
	vulkan_write_u64(writer, resource);
	vulkan_write_u64(writer, memory);
	vulkan_write_u64(writer, offset);
}

/*
 * Memory, its attached storage, images and buffers bound to it, their
 * requirements and layout, and the storage going back once the last
 * holder lets go.
 */
static void
test_memory(void)
{
	struct amdrdna_vk_session *session;
	struct vulkan_writer client;
	struct amdrdna_vk_image *image;
	struct amdrdna_vk_buffer *buffer;
	uint64_t va;
	uint8_t *cpu;
	unsigned before;
	unsigned at;
	int error;

	before = allocations;
	check(amdrdna_vk_session_open(NULL, &session) == 0, "memory: session opened");

	/* An allocation, an image of 100x10 (rows of 512 bytes), its requirements, a buffer of 1000 bytes and its requirements. */
	frame_begin(&client);
	write_allocate(&client, 0x200, 0x10000, 0);
	write_image(&client, 0x201, VK_FORMAT_B8G8R8A8_UNORM, 100, 10, 1);
	write_pair(&client, GPU_OP_GET_IMAGE_MEMORY_REQUIREMENTS, 0x201, 1);
	write_buffer(&client, 0x202, 1000);
	write_pair(&client, GPU_OP_GET_BUFFER_MEMORY_REQUIREMENTS, 0x202, 1);
	write_image(&client, 0x203, VK_FORMAT_B8G8R8A8_UNORM, 100, 10, 2);
	write_image(&client, 0x204, VK_FORMAT_R16G16B16A16_SFLOAT, 100, 10, 1);
	error = frame_run(session, &client);
	check(error == 0, "memory: the creates ran");
	at = TEST_REPLY_AT / 4U;
	check(reply_u32(&at) == GPU_OP_ALLOCATE_MEMORY && reply_u32(&at) == VK_SUCCESS && reply_u64(&at) == 1 && reply_u64(&at) == 0x200,
	    "memory: allocation reply");
	check(reply_u32(&at) == GPU_OP_CREATE_IMAGE && reply_u32(&at) == VK_SUCCESS && reply_u64(&at) == 1 && reply_u64(&at) == 0x201,
	    "memory: image reply");
	check(reply_u32(&at) == GPU_OP_GET_IMAGE_MEMORY_REQUIREMENTS && reply_u64(&at) == 1 && reply_u64(&at) == 5120 &&
	    reply_u64(&at) == 256 && reply_u32(&at) == 1, "memory: image requirements (rows of 512, 256 aligned)");
	check(reply_u32(&at) == GPU_OP_CREATE_BUFFER && reply_u32(&at) == VK_SUCCESS && reply_u64(&at) == 1 && reply_u64(&at) == 0x202,
	    "memory: buffer reply");
	check(reply_u32(&at) == GPU_OP_GET_BUFFER_MEMORY_REQUIREMENTS && reply_u64(&at) == 1 && reply_u64(&at) == 1024 &&
	    reply_u64(&at) == 256 && reply_u32(&at) == 1, "memory: buffer requirements (rounded to 256)");
	check(reply_u32(&at) == GPU_OP_CREATE_IMAGE && reply_u32(&at) == (uint32_t)VK_ERROR_FEATURE_NOT_PRESENT,
	    "memory: an image of two levels is refused by name");
	check(reply_u32(&at) == GPU_OP_CREATE_IMAGE && reply_u32(&at) == (uint32_t)VK_ERROR_FEATURE_NOT_PRESENT,
	    "memory: an image of a float format is refused by name");
	image = amdrdna_vk_payload(session, I915_VK_OBJ_IMAGE, 0x201);
	check(image != NULL && image->pitch == 512 && image->bytes == 5120, "memory: the image's layout");

	/* The node attaches the blob's storage once, of the allocation's size. */
	check(amdrdna_vk_memory_attach(session, 0x299, 0x10000, storage, 0x30000000ULL, &backing_token) == EINVAL,
	    "memory: no attach to an unknown allocation");
	check(amdrdna_vk_memory_attach(session, 0x200, 0x8000, storage, 0x30000000ULL, &backing_token) == EINVAL,
	    "memory: no attach of another size");
	check(amdrdna_vk_memory_attach(session, 0x200, 0x10000, storage, 0x30000000ULL, &backing_token) == 0,
	    "memory: the blob's storage attached");
	check(amdrdna_vk_memory_attach(session, 0x200, 0x10000, storage, 0x30000000ULL, &backing_token) == EINVAL,
	    "memory: attached once");

	/* Binds: the image at 0, the buffer at 5120; again, off the boundary, past the end, and to unknown memory fail. */
	frame_begin(&client);
	write_bind(&client, GPU_OP_BIND_IMAGE_MEMORY, 0x201, 0x200, 0);
	write_bind(&client, GPU_OP_BIND_IMAGE_MEMORY, 0x201, 0x200, 0);
	write_bind(&client, GPU_OP_BIND_BUFFER_MEMORY, 0x202, 0x200, 100);
	write_bind(&client, GPU_OP_BIND_BUFFER_MEMORY, 0x202, 0x200, 0x10000 - 512);
	write_bind(&client, GPU_OP_BIND_BUFFER_MEMORY, 0x202, 0x299, 0);
	write_bind(&client, GPU_OP_BIND_BUFFER_MEMORY, 0x202, 0x200, 5120);
	command_begin(&client, GPU_OP_GET_IMAGE_SUBRESOURCE_LAYOUT);
	vulkan_write_u64(&client, TEST_DEVICE);
	vulkan_write_u64(&client, 0x201);
	vulkan_write_u64(&client, 1);
	vulkan_write_u32(&client, VK_IMAGE_ASPECT_COLOR_BIT);
	vulkan_write_u32(&client, 0);
	vulkan_write_u32(&client, 0);
	vulkan_write_u64(&client, 1);
	command_begin(&client, GPU_OP_GET_DEVICE_MEMORY_COMMITMENT);
	vulkan_write_u64(&client, TEST_DEVICE);
	vulkan_write_u64(&client, 0x200);
	vulkan_write_u64(&client, 1);
	error = frame_run(session, &client);
	check(error == 0, "memory: the binds ran");
	at = TEST_REPLY_AT / 4U;
	check(reply_u32(&at) == GPU_OP_BIND_IMAGE_MEMORY && reply_u32(&at) == VK_SUCCESS, "memory: image bound");
	check(reply_u32(&at) == GPU_OP_BIND_IMAGE_MEMORY && reply_u32(&at) != VK_SUCCESS, "memory: an image is bound once");
	check(reply_u32(&at) == GPU_OP_BIND_BUFFER_MEMORY && reply_u32(&at) != VK_SUCCESS, "memory: off the boundary");
	check(reply_u32(&at) == GPU_OP_BIND_BUFFER_MEMORY && reply_u32(&at) != VK_SUCCESS, "memory: past the end");
	check(reply_u32(&at) == GPU_OP_BIND_BUFFER_MEMORY && reply_u32(&at) != VK_SUCCESS, "memory: unknown memory");
	check(reply_u32(&at) == GPU_OP_BIND_BUFFER_MEMORY && reply_u32(&at) == VK_SUCCESS, "memory: buffer bound");
	check(reply_u32(&at) == GPU_OP_GET_IMAGE_SUBRESOURCE_LAYOUT && reply_u64(&at) == 1 && reply_u64(&at) == 0 &&
	    reply_u64(&at) == 5120 && reply_u64(&at) == 512, "memory: the image's layout (offset, size, row pitch)");
	at += 4U;
	check(reply_u32(&at) == GPU_OP_GET_DEVICE_MEMORY_COMMITMENT && reply_u64(&at) == 1 && reply_u64(&at) == 0x10000,
	    "memory: committed once attached");
	buffer = amdrdna_vk_payload(session, I915_VK_OBJ_BUFFER, 0x202);
	check(buffer != NULL && amdrdna_vk_buffer_address(buffer, &va, &cpu) == 0 && va == 0x30000000ULL + 5120 && cpu == storage + 5120,
	    "memory: the buffer's addresses");
	check(image != NULL && amdrdna_vk_image_address(image, &va, &cpu) == 0 && va == 0x30000000ULL && cpu == storage,
	    "memory: the image's addresses");

	/* The allocation freed first lives on while the image and the buffer hold it. */
	frame_begin(&client);
	write_pair(&client, GPU_OP_FREE_MEMORY, 0x200, 0);
	write_pair(&client, GPU_OP_DESTROY_IMAGE, 0x201, 0);
	error = frame_run(session, &client);
	check(error == 0 && detached == 0, "memory: freed memory still held by the buffer");
	frame_begin(&client);
	write_pair(&client, GPU_OP_DESTROY_BUFFER, 0x202, 0);
	error = frame_run(session, &client);
	check(error == 0 && detached == 1 && detached_backing == &backing_token, "memory: the storage goes back with the last holder");

	/* An import takes the blob's storage at once; an unknown resource is refused. */
	frame_begin(&client);
	write_allocate(&client, 0x210, 0x10000, 7);
	write_allocate(&client, 0x211, 0x10000, 9);
	error = frame_run(session, &client);
	check(error == 0, "memory: the imports ran");
	at = TEST_REPLY_AT / 4U;
	check(reply_u32(&at) == GPU_OP_ALLOCATE_MEMORY && reply_u32(&at) == VK_SUCCESS && reply_u64(&at) == 1 && reply_u64(&at) == 0x210,
	    "memory: import accepted");
	check(reply_u32(&at) == GPU_OP_ALLOCATE_MEMORY && reply_u32(&at) != VK_SUCCESS, "memory: import of an unknown resource refused");
	check(amdrdna_vk_memory_attach(session, 0x210, 0x10000, storage, 0x30000000ULL, &backing_token) == EINVAL,
	    "memory: an import has its storage already");

	/* Closing the session gives the import's storage back and frees everything. */
	check(amdrdna_vk_session_close(&session) == 0 && detached == 2 && detached_backing == &import_token, "memory: session closed");
	check(allocations == before, "memory: nothing leaked");
}

/* Views of images and samplers. */
static void
test_views(void)
{
	struct amdrdna_vk_session *session;
	struct vulkan_writer client;
	struct vulkan_object image;
	struct vulkan_object unknown;
	struct amdrdna_vk_view *view;
	VkImageViewCreateInfo info;
	VkSamplerCreateInfo sampler;
	unsigned before;
	unsigned at;
	int error;

	before = allocations;
	check(amdrdna_vk_session_open(NULL, &session) == 0, "views: session opened");
	memset(&image, 0, sizeof(image));
	image.wire_id = 0x301;
	memset(&unknown, 0, sizeof(unknown));
	unknown.wire_id = 0x399;

	/* An image, a view of it (blue and red swapped), a view of an unknown image, a sampler. */
	frame_begin(&client);
	write_image(&client, 0x301, VK_FORMAT_R8G8B8A8_UNORM, 64, 64, 1);
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	info.image = (VkImage)(uintptr_t)&image;
	info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	info.format = VK_FORMAT_R8G8B8A8_UNORM;
	info.components.r = VK_COMPONENT_SWIZZLE_B;
	info.components.b = VK_COMPONENT_SWIZZLE_R;
	info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	info.subresourceRange.levelCount = 1;
	info.subresourceRange.layerCount = 1;
	command_begin(&client, GPU_OP_CREATE_IMAGE_VIEW);
	vulkan_write_u64(&client, TEST_DEVICE);
	vulkan_write_u64(&client, 1);
	vulkan_encode_VkImageViewCreateInfo(&client, &info);
	write_tail(&client, 0x302);
	info.image = (VkImage)(uintptr_t)&unknown;
	command_begin(&client, GPU_OP_CREATE_IMAGE_VIEW);
	vulkan_write_u64(&client, TEST_DEVICE);
	vulkan_write_u64(&client, 1);
	vulkan_encode_VkImageViewCreateInfo(&client, &info);
	write_tail(&client, 0x303);
	memset(&sampler, 0, sizeof(sampler));
	sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler.magFilter = VK_FILTER_LINEAR;
	sampler.minFilter = VK_FILTER_LINEAR;
	sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	command_begin(&client, GPU_OP_CREATE_SAMPLER);
	vulkan_write_u64(&client, TEST_DEVICE);
	vulkan_write_u64(&client, 1);
	vulkan_encode_VkSamplerCreateInfo(&client, &sampler);
	write_tail(&client, 0x304);
	error = frame_run(session, &client);
	check(error == 0, "views: the creates ran");
	at = TEST_REPLY_AT / 4U;
	check(reply_u32(&at) == GPU_OP_CREATE_IMAGE && reply_u32(&at) == VK_SUCCESS, "views: image");
	at += 4U;
	check(reply_u32(&at) == GPU_OP_CREATE_IMAGE_VIEW && reply_u32(&at) == VK_SUCCESS && reply_u64(&at) == 1 && reply_u64(&at) == 0x302,
	    "views: view");
	check(reply_u32(&at) == GPU_OP_CREATE_IMAGE_VIEW && reply_u32(&at) != VK_SUCCESS, "views: view of an unknown image refused");
	check(reply_u32(&at) == GPU_OP_CREATE_SAMPLER && reply_u32(&at) == VK_SUCCESS && reply_u64(&at) == 1 && reply_u64(&at) == 0x304,
	    "views: sampler");
	view = amdrdna_vk_payload(session, I915_VK_OBJ_IMAGE_VIEW, 0x302);
	check(view != NULL && view->swizzle[0] == VK_COMPONENT_SWIZZLE_B && view->swizzle[1] == VK_COMPONENT_SWIZZLE_G &&
	    view->swizzle[2] == VK_COMPONENT_SWIZZLE_R && view->swizzle[3] == VK_COMPONENT_SWIZZLE_A, "views: the swizzle, identity resolved");

	/* The image destroyed first lives on in its view. */
	frame_begin(&client);
	write_pair(&client, GPU_OP_DESTROY_IMAGE, 0x301, 0);
	error = frame_run(session, &client);
	check(error == 0 && view->image != NULL && ((struct amdrdna_vk_image *)view->image->payload)->width == 64, "views: the view holds its image");
	frame_begin(&client);
	write_pair(&client, GPU_OP_DESTROY_IMAGE_VIEW, 0x302, 0);
	write_pair(&client, GPU_OP_DESTROY_SAMPLER, 0x304, 0);
	write_pair(&client, GPU_OP_DESTROY_SAMPLER, 0x399, 0);
	error = frame_run(session, &client);
	check(error == 0, "views: destroyed (an unknown identity too)");

	check(amdrdna_vk_session_close(&session) == 0, "views: session closed");
	check(allocations == before, "views: nothing leaked");
}

/* Reads a SPIR-V module glslc wrote into the directory. */
static uint32_t *
read_spirv(const char *directory, const char *name, size_t *bytes)
{
	char path[512];
	FILE *file;
	uint32_t *words;
	long length;

	snprintf(path, sizeof(path), "%s/%s.spv", directory, name);
	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	words = malloc((size_t)length);
	if (words != NULL && fread(words, 1, (size_t)length, file) != (size_t)length) {
		free(words);
		words = NULL;
	}
	fclose(file);
	*bytes = (size_t)length;
	return words;
}

/* A create's head: [device][present]. */
static void
write_create(struct vulkan_writer *writer, uint32_t opcode)
{
	command_begin(writer, opcode);
	vulkan_write_u64(writer, TEST_DEVICE);
	vulkan_write_u64(writer, 1);
}

/*
 * The compositor's quad as its objects: shader modules of quad.vert and
 * quad.frag, a set layout of one combined image sampler, a pipeline layout
 * with push constants, a pool and a set updated with a view and a sampler,
 * a render pass and a framebuffer, and the graphics pipeline, compiled.
 */
static void
test_pipeline(const char *directory)
{
	struct amdrdna_vk_session *session;
	struct vulkan_writer client;
	struct vulkan_object handles[16];
	struct amdrdna_vk_pipeline *pipeline;
	struct amdrdna_vk_descriptor_set *set;
	struct amdrdna_vk_render_pass *pass;
	struct amdrdna_vk_framebuffer *framebuffer;
	VkShaderModuleCreateInfo module;
	VkDescriptorSetLayoutBinding binding;
	VkDescriptorSetLayoutCreateInfo set_layout;
	VkPushConstantRange range;
	VkPipelineLayoutCreateInfo pipeline_layout;
	VkDescriptorPoolSize size;
	VkDescriptorPoolCreateInfo pool;
	VkDescriptorSetAllocateInfo allocate;
	VkImageViewCreateInfo view;
	VkSamplerCreateInfo sampler;
	VkAttachmentDescription attachment;
	VkAttachmentReference reference;
	VkSubpassDescription subpass;
	VkRenderPassCreateInfo render_pass;
	VkFramebufferCreateInfo framebuffer_info;
	VkImageView attachments[1];
	VkPipelineShaderStageCreateInfo stages[2];
	VkVertexInputBindingDescription vertex_binding;
	VkVertexInputAttributeDescription vertex_attribute;
	VkPipelineVertexInputStateCreateInfo vertex_input;
	VkPipelineInputAssemblyStateCreateInfo assembly;
	VkPipelineViewportStateCreateInfo viewport;
	VkPipelineRasterizationStateCreateInfo raster;
	VkPipelineMultisampleStateCreateInfo multisample;
	VkPipelineColorBlendAttachmentState blend_attachment;
	VkPipelineColorBlendStateCreateInfo blend;
	VkDynamicState dynamic_states[2];
	VkPipelineDynamicStateCreateInfo dynamic;
	VkDescriptorSetLayout layouts[1];
	uint32_t *vertex_words;
	uint32_t *fragment_words;
	size_t vertex_bytes;
	size_t fragment_bytes;
	unsigned before;
	unsigned at;
	unsigned index;
	int error;

	vertex_words = read_spirv(directory, "quad-vert", &vertex_bytes);
	fragment_words = read_spirv(directory, "quad-frag", &fragment_bytes);
	check(vertex_words != NULL && fragment_words != NULL, "pipeline: the quad's SPIR-V");
	if (vertex_words == NULL || fragment_words == NULL)
		return;

	before = allocations;
	check(amdrdna_vk_session_open(NULL, &session) == 0, "pipeline: session opened");
	memset(handles, 0, sizeof(handles));
	for (index = 0; index < 16; index++)
		handles[index].wire_id = 0x400 + index;

	/* 0x401 0x402 the modules, 0x403 the set layout, 0x404 the pipeline layout, 0x405 the pool, 0x406 the set. */
	frame_begin(&client);
	memset(&module, 0, sizeof(module));
	module.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	module.codeSize = vertex_bytes;
	module.pCode = vertex_words;
	write_create(&client, GPU_OP_CREATE_SHADER_MODULE);
	vulkan_encode_VkShaderModuleCreateInfo(&client, &module);
	write_tail(&client, 0x401);
	module.codeSize = fragment_bytes;
	module.pCode = fragment_words;
	write_create(&client, GPU_OP_CREATE_SHADER_MODULE);
	vulkan_encode_VkShaderModuleCreateInfo(&client, &module);
	write_tail(&client, 0x402);
	memset(&binding, 0, sizeof(binding));
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	memset(&set_layout, 0, sizeof(set_layout));
	set_layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set_layout.bindingCount = 1;
	set_layout.pBindings = &binding;
	write_create(&client, GPU_OP_CREATE_DESCRIPTOR_SET_LAYOUT);
	vulkan_encode_VkDescriptorSetLayoutCreateInfo(&client, &set_layout);
	write_tail(&client, 0x403);
	layouts[0] = (VkDescriptorSetLayout)(uintptr_t)&handles[3];
	memset(&range, 0, sizeof(range));
	range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	range.size = 32;
	memset(&pipeline_layout, 0, sizeof(pipeline_layout));
	pipeline_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeline_layout.setLayoutCount = 1;
	pipeline_layout.pSetLayouts = layouts;
	pipeline_layout.pushConstantRangeCount = 1;
	pipeline_layout.pPushConstantRanges = &range;
	write_create(&client, GPU_OP_CREATE_PIPELINE_LAYOUT);
	vulkan_encode_VkPipelineLayoutCreateInfo(&client, &pipeline_layout);
	write_tail(&client, 0x404);
	memset(&size, 0, sizeof(size));
	size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	size.descriptorCount = 4;
	memset(&pool, 0, sizeof(pool));
	pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool.maxSets = 4;
	pool.poolSizeCount = 1;
	pool.pPoolSizes = &size;
	write_create(&client, GPU_OP_CREATE_DESCRIPTOR_POOL);
	vulkan_encode_VkDescriptorPoolCreateInfo(&client, &pool);
	write_tail(&client, 0x405);
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocate.descriptorPool = (VkDescriptorPool)(uintptr_t)&handles[5];
	allocate.descriptorSetCount = 1;
	allocate.pSetLayouts = layouts;
	command_begin(&client, GPU_OP_ALLOCATE_DESCRIPTOR_SETS);
	vulkan_write_u64(&client, TEST_DEVICE);
	vulkan_write_u64(&client, 1);
	vulkan_encode_VkDescriptorSetAllocateInfo(&client, &allocate);
	vulkan_write_u64(&client, 1);
	vulkan_write_u64(&client, 0x406);
	error = frame_run(session, &client);
	check(error == 0, "pipeline: modules, layouts, pool and set made");
	at = TEST_REPLY_AT / 4U;
	for (index = 1; index <= 5; index++) {
		at++;
		check(reply_u32(&at) == VK_SUCCESS && reply_u64(&at) == 1 && reply_u64(&at) == 0x400 + index, "pipeline: an object made");
	}
	check(reply_u32(&at) == GPU_OP_ALLOCATE_DESCRIPTOR_SETS && reply_u32(&at) == VK_SUCCESS && reply_u64(&at) == 1 && reply_u64(&at) == 0x406,
	    "pipeline: the set allocated");

	/* 0x407 an image, 0x408 its view, 0x409 a sampler; the set's binding 0 takes the view and the sampler. */
	frame_begin(&client);
	write_image(&client, 0x407, VK_FORMAT_B8G8R8A8_UNORM, 64, 64, 1);
	memset(&view, 0, sizeof(view));
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.image = (VkImage)(uintptr_t)&handles[7];
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = VK_FORMAT_B8G8R8A8_UNORM;
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.levelCount = 1;
	view.subresourceRange.layerCount = 1;
	write_create(&client, GPU_OP_CREATE_IMAGE_VIEW);
	vulkan_encode_VkImageViewCreateInfo(&client, &view);
	write_tail(&client, 0x408);
	memset(&sampler, 0, sizeof(sampler));
	sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	write_create(&client, GPU_OP_CREATE_SAMPLER);
	vulkan_encode_VkSamplerCreateInfo(&client, &sampler);
	write_tail(&client, 0x409);
	command_begin(&client, GPU_OP_UPDATE_DESCRIPTOR_SETS);
	vulkan_write_u64(&client, TEST_DEVICE);
	vulkan_write_u32(&client, 1);
	vulkan_write_u64(&client, 1);
	vulkan_write_u32(&client, VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
	vulkan_write_u64(&client, 0);
	vulkan_write_u64(&client, 0x406);
	vulkan_write_u32(&client, 0);
	vulkan_write_u32(&client, 0);
	vulkan_write_u32(&client, 1);
	vulkan_write_u32(&client, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	vulkan_write_u64(&client, 1);
	vulkan_write_u64(&client, 0x409);
	vulkan_write_u64(&client, 0x408);
	vulkan_write_u32(&client, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	vulkan_write_u64(&client, 0);
	vulkan_write_u64(&client, 0);
	vulkan_write_u32(&client, 0);
	vulkan_write_u64(&client, 0);

	/* 0x40a a render pass of one B8G8R8A8 attachment cleared, 0x40b a framebuffer of the view. */
	memset(&attachment, 0, sizeof(attachment));
	attachment.format = VK_FORMAT_B8G8R8A8_UNORM;
	attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
	memset(&reference, 0, sizeof(reference));
	reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	memset(&subpass, 0, sizeof(subpass));
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &reference;
	memset(&render_pass, 0, sizeof(render_pass));
	render_pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass.attachmentCount = 1;
	render_pass.pAttachments = &attachment;
	render_pass.subpassCount = 1;
	render_pass.pSubpasses = &subpass;
	write_create(&client, GPU_OP_CREATE_RENDER_PASS);
	vulkan_encode_VkRenderPassCreateInfo(&client, &render_pass);
	write_tail(&client, 0x40a);
	attachments[0] = (VkImageView)(uintptr_t)&handles[8];
	memset(&framebuffer_info, 0, sizeof(framebuffer_info));
	framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebuffer_info.renderPass = (VkRenderPass)(uintptr_t)&handles[10];
	framebuffer_info.attachmentCount = 1;
	framebuffer_info.pAttachments = attachments;
	framebuffer_info.width = 64;
	framebuffer_info.height = 64;
	framebuffer_info.layers = 1;
	write_create(&client, GPU_OP_CREATE_FRAMEBUFFER);
	vulkan_encode_VkFramebufferCreateInfo(&client, &framebuffer_info);
	write_tail(&client, 0x40b);
	error = frame_run(session, &client);
	check(error == 0, "pipeline: image, view, sampler, update, pass and framebuffer made");
	set = amdrdna_vk_payload(session, I915_VK_OBJ_DESCRIPTOR_SET, 0x406);
	check(set != NULL && set->slots[0].view != NULL && set->slots[0].view->identity == 0x408 && set->slots[0].sampler != NULL &&
	    set->slots[0].sampler->identity == 0x409, "pipeline: the set's binding 0 names the view and the sampler");
	pass = amdrdna_vk_payload(session, I915_VK_OBJ_RENDER_PASS, 0x40a);
	check(pass != NULL && pass->colour == 1 && pass->format == VK_FORMAT_B8G8R8A8_UNORM && pass->load_op == VK_ATTACHMENT_LOAD_OP_CLEAR,
	    "pipeline: the render pass");
	framebuffer = amdrdna_vk_payload(session, I915_VK_OBJ_FRAMEBUFFER, 0x40b);
	check(framebuffer != NULL && framebuffer->width == 64 && framebuffer->view != NULL && framebuffer->view->identity == 0x408,
	    "pipeline: the framebuffer");

	/* 0x40c the graphics pipeline as pipeline_encode_graphics() writes it. */
	frame_begin(&client);
	memset(stages, 0, sizeof(stages));
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = (VkShaderModule)(uintptr_t)&handles[1];
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = (VkShaderModule)(uintptr_t)&handles[2];
	stages[1].pName = "main";
	memset(&vertex_binding, 0, sizeof(vertex_binding));
	vertex_binding.stride = 8;
	memset(&vertex_attribute, 0, sizeof(vertex_attribute));
	vertex_attribute.format = VK_FORMAT_R32G32_SFLOAT;
	memset(&vertex_input, 0, sizeof(vertex_input));
	vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex_input.vertexBindingDescriptionCount = 1;
	vertex_input.pVertexBindingDescriptions = &vertex_binding;
	vertex_input.vertexAttributeDescriptionCount = 1;
	vertex_input.pVertexAttributeDescriptions = &vertex_attribute;
	memset(&assembly, 0, sizeof(assembly));
	assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	memset(&viewport, 0, sizeof(viewport));
	viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport.viewportCount = 1;
	viewport.scissorCount = 1;
	memset(&raster, 0, sizeof(raster));
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.cullMode = VK_CULL_MODE_NONE;
	raster.lineWidth = 1.0f;
	memset(&multisample, 0, sizeof(multisample));
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	memset(&blend_attachment, 0, sizeof(blend_attachment));
	blend_attachment.blendEnable = VK_TRUE;
	blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend_attachment.colorWriteMask = 0xf;
	memset(&blend, 0, sizeof(blend));
	blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend.attachmentCount = 1;
	blend.pAttachments = &blend_attachment;
	dynamic_states[0] = VK_DYNAMIC_STATE_VIEWPORT;
	dynamic_states[1] = VK_DYNAMIC_STATE_SCISSOR;
	memset(&dynamic, 0, sizeof(dynamic));
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.dynamicStateCount = 2;
	dynamic.pDynamicStates = dynamic_states;
	command_begin(&client, GPU_OP_CREATE_GRAPHICS_PIPELINES);
	vulkan_write_u64(&client, TEST_DEVICE);
	vulkan_write_u64(&client, 0);
	vulkan_write_u32(&client, 1);
	vulkan_write_u64(&client, 1);
	vulkan_write_u32(&client, VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
	vulkan_write_u64(&client, 0);
	vulkan_write_u32(&client, 0);
	vulkan_write_u32(&client, 2);
	vulkan_write_u64(&client, 2);
	vulkan_encode_VkPipelineShaderStageCreateInfo(&client, &stages[0]);
	vulkan_encode_VkPipelineShaderStageCreateInfo(&client, &stages[1]);
	vulkan_write_u64(&client, 1);
	vulkan_encode_VkPipelineVertexInputStateCreateInfo(&client, &vertex_input);
	vulkan_write_u64(&client, 1);
	vulkan_encode_VkPipelineInputAssemblyStateCreateInfo(&client, &assembly);
	vulkan_write_u64(&client, 0);
	vulkan_write_u64(&client, 1);
	vulkan_encode_VkPipelineViewportStateCreateInfo(&client, &viewport);
	vulkan_write_u64(&client, 1);
	vulkan_encode_VkPipelineRasterizationStateCreateInfo(&client, &raster);
	vulkan_write_u64(&client, 1);
	vulkan_encode_VkPipelineMultisampleStateCreateInfo(&client, &multisample);
	vulkan_write_u64(&client, 0);
	vulkan_write_u64(&client, 1);
	vulkan_encode_VkPipelineColorBlendStateCreateInfo(&client, &blend);
	vulkan_write_u64(&client, 1);
	vulkan_encode_VkPipelineDynamicStateCreateInfo(&client, &dynamic);
	vulkan_write_u64(&client, 0x404);
	vulkan_write_u64(&client, 0x40a);
	vulkan_write_u32(&client, 0);
	vulkan_write_u64(&client, 0);
	vulkan_write_u32(&client, (uint32_t)-1);
	vulkan_write_u64(&client, 0);
	vulkan_write_u64(&client, 1);
	vulkan_write_u64(&client, 0x40c);
	error = frame_run(session, &client);
	check(error == 0, "pipeline: the create ran");
	at = TEST_REPLY_AT / 4U;
	check(reply_u32(&at) == GPU_OP_CREATE_GRAPHICS_PIPELINES && reply_u32(&at) == VK_SUCCESS && reply_u64(&at) == 1 && reply_u64(&at) == 0x40c,
	    "pipeline: compiled and published");
	pipeline = amdrdna_vk_payload(session, I915_VK_OBJ_PIPELINE, 0x40c);
	check(pipeline != NULL && pipeline->vertex.compiled != NULL && pipeline->fragment.compiled != NULL &&
	    pipeline->fragment.compiled->texture_used == 1 && pipeline->fragment.compiled->textures[0].set == 0 &&
	    pipeline->fragment.compiled->textures[0].binding == 0 && pipeline->vertex.compiled->texture_used == 0 &&
	    pipeline->vertex.compiled->fetched_locations == 1U, "pipeline: two programs, the fragment's texture from set 0 binding 0");
	check(pipeline != NULL && pipeline->fragment.va > pipeline->vertex.va && (pipeline->vertex.va % 256) == 0 && (pipeline->fragment.va % 256) == 0 &&
	    pipeline->push_bytes == 32, "pipeline: the programs' places and the push bytes");
	if (pipeline != NULL && pipeline->vertex.compiled != NULL && pipeline->fragment.compiled != NULL) {
		struct amdrdna_gfx10_registers expected;

		check(gpu_cpu(pipeline->vertex.va) != NULL &&
		    memcmp(gpu_cpu(pipeline->vertex.va), pipeline->vertex.compiled->code, pipeline->vertex.compiled->code_words * 4U) == 0 &&
		    memcmp(gpu_cpu(pipeline->fragment.va), pipeline->fragment.compiled->code, pipeline->fragment.compiled->code_words * 4U) == 0,
		    "pipeline: the code copied to the programs' places");
		amdrdna_gfx10_program_registers(pipeline->fragment.compiled, &expected);
		check(memcmp(&expected, &pipeline->fragment.registers, sizeof(expected)) == 0 && pipeline->vertex.registers.primitive_id_enable == 0U,
		    "pipeline: the register values kept with the programs");
	}
	check(pipeline != NULL && pipeline->binding_count == 1 && pipeline->bindings[0].stride == 8 && pipeline->attribute_count == 1 &&
	    pipeline->attributes[0].format == VK_FORMAT_R32G32_SFLOAT && pipeline->blend_enable == 1 &&
	    pipeline->blend_dst_colour == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA && pipeline->dynamic_viewport == 1 && pipeline->dynamic_scissor == 1 &&
	    pipeline->write_mask == 0xf, "pipeline: the vertex input, blend and dynamic state");

	/* The pool's destruction takes its set; closing frees everything. */
	frame_begin(&client);
	write_pair(&client, GPU_OP_DESTROY_DESCRIPTOR_POOL, 0x405, 0);
	error = frame_run(session, &client);
	check(error == 0 && amdrdna_vk_payload(session, I915_VK_OBJ_DESCRIPTOR_SET, 0x406) == NULL, "pipeline: the pool took its set");
	check(amdrdna_vk_session_close(&session) == 0, "pipeline: session closed");
	check(allocations == before, "pipeline: nothing leaked");
	free(vertex_words);
	free(fragment_words);
}

/* A recorded command's header as libvulkan's command_record_begin() writes it: no reply asked for. */
static void
record_begin(struct vulkan_writer *writer, uint32_t opcode, uint64_t command)
{
	vulkan_write_u32(writer, opcode);
	vulkan_write_u32(writer, 0);
	vulkan_write_u64(writer, command);
}

/* The compositor's quad pipeline as pipeline_encode_graphics() writes it: modules handles[vertex] and handles[fragment]. */
static void
write_quad_pipeline(struct vulkan_writer *client, struct vulkan_object *handles, uint64_t identity, unsigned vertex, unsigned fragment, uint64_t layout, uint64_t pass)
{
	VkPipelineShaderStageCreateInfo stages[2];
	VkVertexInputBindingDescription vertex_binding;
	VkVertexInputAttributeDescription vertex_attribute;
	VkPipelineVertexInputStateCreateInfo vertex_input;
	VkPipelineInputAssemblyStateCreateInfo assembly;
	VkPipelineViewportStateCreateInfo viewport;
	VkPipelineRasterizationStateCreateInfo raster;
	VkPipelineMultisampleStateCreateInfo multisample;
	VkPipelineColorBlendAttachmentState blend_attachment;
	VkPipelineColorBlendStateCreateInfo blend;
	VkDynamicState dynamic_states[2];
	VkPipelineDynamicStateCreateInfo dynamic;

	memset(stages, 0, sizeof(stages));
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = (VkShaderModule)(uintptr_t)&handles[vertex];
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = (VkShaderModule)(uintptr_t)&handles[fragment];
	stages[1].pName = "main";
	memset(&vertex_binding, 0, sizeof(vertex_binding));
	vertex_binding.stride = 8;
	memset(&vertex_attribute, 0, sizeof(vertex_attribute));
	vertex_attribute.format = VK_FORMAT_R32G32_SFLOAT;
	memset(&vertex_input, 0, sizeof(vertex_input));
	vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex_input.vertexBindingDescriptionCount = 1;
	vertex_input.pVertexBindingDescriptions = &vertex_binding;
	vertex_input.vertexAttributeDescriptionCount = 1;
	vertex_input.pVertexAttributeDescriptions = &vertex_attribute;
	memset(&assembly, 0, sizeof(assembly));
	assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	memset(&viewport, 0, sizeof(viewport));
	viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport.viewportCount = 1;
	viewport.scissorCount = 1;
	memset(&raster, 0, sizeof(raster));
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.lineWidth = 1.0f;
	memset(&multisample, 0, sizeof(multisample));
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	memset(&blend_attachment, 0, sizeof(blend_attachment));
	blend_attachment.blendEnable = VK_TRUE;
	blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend_attachment.colorWriteMask = 0xf;
	memset(&blend, 0, sizeof(blend));
	blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend.attachmentCount = 1;
	blend.pAttachments = &blend_attachment;
	dynamic_states[0] = VK_DYNAMIC_STATE_VIEWPORT;
	dynamic_states[1] = VK_DYNAMIC_STATE_SCISSOR;
	memset(&dynamic, 0, sizeof(dynamic));
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.dynamicStateCount = 2;
	dynamic.pDynamicStates = dynamic_states;
	command_begin(client, GPU_OP_CREATE_GRAPHICS_PIPELINES);
	vulkan_write_u64(client, TEST_DEVICE);
	vulkan_write_u64(client, 0);
	vulkan_write_u32(client, 1);
	vulkan_write_u64(client, 1);
	vulkan_write_u32(client, VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
	vulkan_write_u64(client, 0);
	vulkan_write_u32(client, 0);
	vulkan_write_u32(client, 2);
	vulkan_write_u64(client, 2);
	vulkan_encode_VkPipelineShaderStageCreateInfo(client, &stages[0]);
	vulkan_encode_VkPipelineShaderStageCreateInfo(client, &stages[1]);
	vulkan_write_u64(client, 1);
	vulkan_encode_VkPipelineVertexInputStateCreateInfo(client, &vertex_input);
	vulkan_write_u64(client, 1);
	vulkan_encode_VkPipelineInputAssemblyStateCreateInfo(client, &assembly);
	vulkan_write_u64(client, 0);
	vulkan_write_u64(client, 1);
	vulkan_encode_VkPipelineViewportStateCreateInfo(client, &viewport);
	vulkan_write_u64(client, 1);
	vulkan_encode_VkPipelineRasterizationStateCreateInfo(client, &raster);
	vulkan_write_u64(client, 1);
	vulkan_encode_VkPipelineMultisampleStateCreateInfo(client, &multisample);
	vulkan_write_u64(client, 0);
	vulkan_write_u64(client, 1);
	vulkan_encode_VkPipelineColorBlendStateCreateInfo(client, &blend);
	vulkan_write_u64(client, 1);
	vulkan_encode_VkPipelineDynamicStateCreateInfo(client, &dynamic);
	vulkan_write_u64(client, layout);
	vulkan_write_u64(client, pass);
	vulkan_write_u32(client, 0);
	vulkan_write_u64(client, 0);
	vulkan_write_u32(client, (uint32_t)-1);
	vulkan_write_u64(client, 0);
	vulkan_write_u64(client, 1);
	vulkan_write_u64(client, identity);
}

/*
 * A command buffer as the compositor records one, submitted: a pass that
 * clears and draws the textured quad, an image copy, a copy from a buffer
 * drawn and one done by the CPU, a clear; the packets checked and every
 * draw replayed on the interpreter.
 */
static void
test_submit(const char *directory)
{
	struct amdrdna_vk_session *session;
	struct vulkan_writer client;
	struct vulkan_object handles[32];
	struct amdrdna_vk_pipeline *pipeline;
	VkShaderModuleCreateInfo module;
	VkDescriptorSetLayoutBinding binding;
	VkDescriptorSetLayoutCreateInfo set_layout;
	VkPushConstantRange range;
	VkPipelineLayoutCreateInfo pipeline_layout;
	VkDescriptorPoolSize size;
	VkDescriptorPoolCreateInfo pool;
	VkDescriptorSetAllocateInfo allocate;
	VkImageViewCreateInfo view;
	VkSamplerCreateInfo sampler;
	VkAttachmentDescription attachment;
	VkAttachmentReference reference;
	VkSubpassDescription subpass;
	VkRenderPassCreateInfo render_pass;
	VkFramebufferCreateInfo framebuffer_info;
	VkCommandPoolCreateInfo command_pool;
	VkCommandBufferAllocateInfo command_allocate;
	VkCommandBufferBeginInfo begin;
	VkViewport vp;
	VkRect2D rect;
	VkImageCopy copy;
	VkBufferImageCopy upload;
	VkBufferImageCopy skewed;
	VkBufferImageCopy download;
	VkImageSubresourceRange whole;
	VkImageView attachments[1];
	VkDescriptorSetLayout layouts[1];
	uint32_t *vertex_words;
	uint32_t *fragment_words;
	uint32_t corners[12] = { 0, 0, 0x3f800000, 0, 0, 0x3f800000, 0x3f800000, 0, 0x3f800000, 0x3f800000, 0, 0x3f800000 };
	uint32_t push[8] = { 0xbf800000, 0xbf800000, 0x3f800000, 0x3f800000, 0, 0, 0x3f800000, 0x3f800000 };
	const struct state_entry *quad_draw;
	struct replay replayed;
	const uint8_t *table;
	uint32_t descriptor[12];
	float expected[4];
	unsigned starts;
	unsigned acquires;
	unsigned releases;
	unsigned lane;
	uint32_t last;
	int ordered;
	int good;
	size_t vertex_bytes;
	size_t fragment_bytes;
	unsigned before;
	unsigned at;
	unsigned index;
	int error;

	vertex_words = read_spirv(directory, "quad-vert", &vertex_bytes);
	fragment_words = read_spirv(directory, "quad-frag", &fragment_bytes);
	if (vertex_words == NULL || fragment_words == NULL) {
		check(0, "submit: the quad's SPIR-V");
		return;
	}

	before = allocations;
	check(amdrdna_vk_session_open(NULL, &session) == 0, "submit: session opened");
	memset(handles, 0, sizeof(handles));
	for (index = 0; index < 32; index++)
		handles[index].wire_id = 0x500 + index;

	/* Memory 0x501 at 0x30000000; images 0x502 (64x64 BGRA, rows 256) and 0x503 (32x32 RGBA); buffers 0x504 (corners) and 0x505 (staging). */
	frame_begin(&client);
	write_allocate(&client, 0x501, 0x40000, 0);
	write_image(&client, 0x502, VK_FORMAT_B8G8R8A8_UNORM, 64, 64, 1);
	write_image(&client, 0x503, VK_FORMAT_R8G8B8A8_UNORM, 32, 32, 1);
	write_buffer(&client, 0x504, sizeof(corners));
	write_buffer(&client, 0x505, 0x4000);
	error = frame_run(session, &client);
	check(error == 0, "submit: memory, images and buffers made");
	check(amdrdna_vk_memory_attach(session, 0x501, 0x40000, scene, 0x30000000ULL, &backing_token) == 0, "submit: storage attached");
	frame_begin(&client);
	write_bind(&client, GPU_OP_BIND_IMAGE_MEMORY, 0x502, 0x501, 0);
	write_bind(&client, GPU_OP_BIND_IMAGE_MEMORY, 0x503, 0x501, 0x4000);
	write_bind(&client, GPU_OP_BIND_BUFFER_MEMORY, 0x504, 0x501, 0x6000);
	write_bind(&client, GPU_OP_BIND_BUFFER_MEMORY, 0x505, 0x501, 0x8000);
	error = frame_run(session, &client);
	check(error == 0, "submit: bound");
	memcpy(scene + 0x6000, corners, sizeof(corners));

	/* Views 0x506 (of 0x502) and 0x507 (of 0x503), sampler 0x508, modules 0x509 0x50a, layouts 0x50b 0x50c, pool 0x50d, set 0x50e. */
	frame_begin(&client);
	memset(&view, 0, sizeof(view));
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.levelCount = 1;
	view.subresourceRange.layerCount = 1;
	view.image = (VkImage)(uintptr_t)&handles[2];
	view.format = VK_FORMAT_B8G8R8A8_UNORM;
	write_create(&client, GPU_OP_CREATE_IMAGE_VIEW);
	vulkan_encode_VkImageViewCreateInfo(&client, &view);
	write_tail(&client, 0x506);
	view.image = (VkImage)(uintptr_t)&handles[3];
	view.format = VK_FORMAT_R8G8B8A8_UNORM;
	write_create(&client, GPU_OP_CREATE_IMAGE_VIEW);
	vulkan_encode_VkImageViewCreateInfo(&client, &view);
	write_tail(&client, 0x507);
	memset(&sampler, 0, sizeof(sampler));
	sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler.magFilter = VK_FILTER_LINEAR;
	sampler.minFilter = VK_FILTER_LINEAR;
	write_create(&client, GPU_OP_CREATE_SAMPLER);
	vulkan_encode_VkSamplerCreateInfo(&client, &sampler);
	write_tail(&client, 0x508);
	memset(&module, 0, sizeof(module));
	module.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	module.codeSize = vertex_bytes;
	module.pCode = vertex_words;
	write_create(&client, GPU_OP_CREATE_SHADER_MODULE);
	vulkan_encode_VkShaderModuleCreateInfo(&client, &module);
	write_tail(&client, 0x509);
	module.codeSize = fragment_bytes;
	module.pCode = fragment_words;
	write_create(&client, GPU_OP_CREATE_SHADER_MODULE);
	vulkan_encode_VkShaderModuleCreateInfo(&client, &module);
	write_tail(&client, 0x50a);
	memset(&binding, 0, sizeof(binding));
	binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	binding.descriptorCount = 1;
	memset(&set_layout, 0, sizeof(set_layout));
	set_layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set_layout.bindingCount = 1;
	set_layout.pBindings = &binding;
	write_create(&client, GPU_OP_CREATE_DESCRIPTOR_SET_LAYOUT);
	vulkan_encode_VkDescriptorSetLayoutCreateInfo(&client, &set_layout);
	write_tail(&client, 0x50b);
	layouts[0] = (VkDescriptorSetLayout)(uintptr_t)&handles[11];
	memset(&range, 0, sizeof(range));
	range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	range.size = 32;
	memset(&pipeline_layout, 0, sizeof(pipeline_layout));
	pipeline_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeline_layout.setLayoutCount = 1;
	pipeline_layout.pSetLayouts = layouts;
	pipeline_layout.pushConstantRangeCount = 1;
	pipeline_layout.pPushConstantRanges = &range;
	write_create(&client, GPU_OP_CREATE_PIPELINE_LAYOUT);
	vulkan_encode_VkPipelineLayoutCreateInfo(&client, &pipeline_layout);
	write_tail(&client, 0x50c);
	memset(&size, 0, sizeof(size));
	size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	size.descriptorCount = 1;
	memset(&pool, 0, sizeof(pool));
	pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool.maxSets = 1;
	pool.poolSizeCount = 1;
	pool.pPoolSizes = &size;
	write_create(&client, GPU_OP_CREATE_DESCRIPTOR_POOL);
	vulkan_encode_VkDescriptorPoolCreateInfo(&client, &pool);
	write_tail(&client, 0x50d);
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocate.descriptorPool = (VkDescriptorPool)(uintptr_t)&handles[13];
	allocate.descriptorSetCount = 1;
	allocate.pSetLayouts = layouts;
	command_begin(&client, GPU_OP_ALLOCATE_DESCRIPTOR_SETS);
	vulkan_write_u64(&client, TEST_DEVICE);
	vulkan_write_u64(&client, 1);
	vulkan_encode_VkDescriptorSetAllocateInfo(&client, &allocate);
	vulkan_write_u64(&client, 1);
	vulkan_write_u64(&client, 0x50e);
	command_begin(&client, GPU_OP_UPDATE_DESCRIPTOR_SETS);
	vulkan_write_u64(&client, TEST_DEVICE);
	vulkan_write_u32(&client, 1);
	vulkan_write_u64(&client, 1);
	vulkan_write_u32(&client, VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
	vulkan_write_u64(&client, 0);
	vulkan_write_u64(&client, 0x50e);
	vulkan_write_u32(&client, 0);
	vulkan_write_u32(&client, 0);
	vulkan_write_u32(&client, 1);
	vulkan_write_u32(&client, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	vulkan_write_u64(&client, 1);
	vulkan_write_u64(&client, 0x508);
	vulkan_write_u64(&client, 0x507);
	vulkan_write_u32(&client, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	vulkan_write_u64(&client, 0);
	vulkan_write_u64(&client, 0);
	vulkan_write_u32(&client, 0);
	vulkan_write_u64(&client, 0);

	/* Render pass 0x50f (clear), framebuffer 0x510 of 0x506, pipeline 0x511, command pool 0x512 and buffer 0x513, fence 0x514. */
	memset(&attachment, 0, sizeof(attachment));
	attachment.format = VK_FORMAT_B8G8R8A8_UNORM;
	attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	memset(&reference, 0, sizeof(reference));
	reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	memset(&subpass, 0, sizeof(subpass));
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &reference;
	memset(&render_pass, 0, sizeof(render_pass));
	render_pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass.attachmentCount = 1;
	render_pass.pAttachments = &attachment;
	render_pass.subpassCount = 1;
	render_pass.pSubpasses = &subpass;
	write_create(&client, GPU_OP_CREATE_RENDER_PASS);
	vulkan_encode_VkRenderPassCreateInfo(&client, &render_pass);
	write_tail(&client, 0x50f);
	attachments[0] = (VkImageView)(uintptr_t)&handles[6];
	memset(&framebuffer_info, 0, sizeof(framebuffer_info));
	framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebuffer_info.renderPass = (VkRenderPass)(uintptr_t)&handles[15];
	framebuffer_info.attachmentCount = 1;
	framebuffer_info.pAttachments = attachments;
	framebuffer_info.width = 64;
	framebuffer_info.height = 64;
	framebuffer_info.layers = 1;
	write_create(&client, GPU_OP_CREATE_FRAMEBUFFER);
	vulkan_encode_VkFramebufferCreateInfo(&client, &framebuffer_info);
	write_tail(&client, 0x510);
	write_quad_pipeline(&client, handles, 0x511, 9, 10, 0x50c, 0x50f);
	memset(&command_pool, 0, sizeof(command_pool));
	command_pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	write_create(&client, GPU_OP_CREATE_COMMAND_POOL);
	vulkan_encode_VkCommandPoolCreateInfo(&client, &command_pool);
	write_tail(&client, 0x512);
	memset(&command_allocate, 0, sizeof(command_allocate));
	command_allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	command_allocate.commandPool = (VkCommandPool)(uintptr_t)&handles[18];
	command_allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command_allocate.commandBufferCount = 1;
	command_begin(&client, GPU_OP_ALLOCATE_COMMAND_BUFFERS);
	vulkan_write_u64(&client, TEST_DEVICE);
	vulkan_write_u64(&client, 1);
	vulkan_encode_VkCommandBufferAllocateInfo(&client, &command_allocate);
	vulkan_write_u64(&client, 1);
	vulkan_write_u64(&client, 0x513);
	command_begin(&client, GPU_OP_CREATE_FENCE);
	vulkan_write_u64(&client, TEST_DEVICE);
	vulkan_write_u64(&client, 1);
	vulkan_write_u32(&client, VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
	vulkan_write_u64(&client, 0);
	vulkan_write_u32(&client, 0);
	write_tail(&client, 0x514);
	error = frame_run(session, &client);
	check(error == 0, "submit: the scene's objects made");
	pipeline = amdrdna_vk_payload(session, I915_VK_OBJ_PIPELINE, 0x511);
	check(pipeline != NULL && amdrdna_vk_payload(session, I915_VK_OBJ_COMMAND_BUFFER, 0x513) != NULL &&
	    amdrdna_vk_payload(session, I915_VK_OBJ_FENCE, 0x514) != NULL && amdrdna_vk_payload(session, I915_VK_OBJ_DESCRIPTOR_SET, 0x50e) != NULL,
	    "submit: pipeline, command buffer, fence and set exist");
	if (pipeline == NULL)
		return;

	/* The recording: Begin, the pass with its draw, a barrier, the copies and the clear, End. */
	frame_begin(&client);
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	command_begin(&client, GPU_OP_BEGIN_COMMAND_BUFFER);
	vulkan_write_u64(&client, 0x513);
	vulkan_write_u64(&client, 1);
	vulkan_encode_VkCommandBufferBeginInfo(&client, &begin);
	record_begin(&client, GPU_OP_CMD_BEGIN_RENDER_PASS, 0x513);
	vulkan_write_u64(&client, 1);
	vulkan_write_u32(&client, VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
	vulkan_write_u64(&client, 0);
	vulkan_write_u64(&client, 0x50f);
	vulkan_write_u64(&client, 0x510);
	memset(&rect, 0, sizeof(rect));
	rect.extent.width = 64;
	rect.extent.height = 64;
	vulkan_encode_VkRect2D(&client, &rect);
	vulkan_write_u32(&client, 1);
	vulkan_write_u64(&client, 1);
	vulkan_write_u32(&client, 0);
	vulkan_write_u32(&client, 2);
	vulkan_write_u64(&client, 4);
	vulkan_write_u32(&client, 0);
	vulkan_write_u32(&client, 0);
	vulkan_write_u32(&client, 0x3f000000);
	vulkan_write_u32(&client, 0x3f800000);
	vulkan_write_u32(&client, VK_SUBPASS_CONTENTS_INLINE);
	record_begin(&client, GPU_OP_CMD_BIND_PIPELINE, 0x513);
	vulkan_write_u32(&client, VK_PIPELINE_BIND_POINT_GRAPHICS);
	vulkan_write_u64(&client, 0x511);
	record_begin(&client, GPU_OP_CMD_SET_VIEWPORT, 0x513);
	vulkan_write_u32(&client, 0);
	vulkan_write_u32(&client, 1);
	vulkan_write_u64(&client, 1);
	memset(&vp, 0, sizeof(vp));
	vp.width = 64.0f;
	vp.height = 64.0f;
	vp.maxDepth = 1.0f;
	vulkan_encode_VkViewport(&client, &vp);
	record_begin(&client, GPU_OP_CMD_SET_SCISSOR, 0x513);
	vulkan_write_u32(&client, 0);
	vulkan_write_u32(&client, 1);
	vulkan_write_u64(&client, 1);
	vulkan_encode_VkRect2D(&client, &rect);
	record_begin(&client, GPU_OP_CMD_BIND_DESCRIPTOR_SETS, 0x513);
	vulkan_write_u32(&client, VK_PIPELINE_BIND_POINT_GRAPHICS);
	vulkan_write_u64(&client, 0x50c);
	vulkan_write_u32(&client, 0);
	vulkan_write_u32(&client, 1);
	vulkan_write_u64(&client, 1);
	vulkan_write_u64(&client, 0x50e);
	vulkan_write_u32(&client, 0);
	vulkan_write_u64(&client, 0);
	record_begin(&client, GPU_OP_CMD_BIND_VERTEX_BUFFERS, 0x513);
	vulkan_write_u32(&client, 0);
	vulkan_write_u32(&client, 1);
	vulkan_write_u64(&client, 1);
	vulkan_write_u64(&client, 0x504);
	vulkan_write_u64(&client, 1);
	vulkan_write_u64(&client, 0);
	record_begin(&client, GPU_OP_CMD_PUSH_CONSTANTS, 0x513);
	vulkan_write_u64(&client, 0x50c);
	vulkan_write_u32(&client, VK_SHADER_STAGE_VERTEX_BIT);
	vulkan_write_u32(&client, 0);
	vulkan_write_u32(&client, sizeof(push));
	vulkan_write_u64(&client, sizeof(push));
	vulkan_write_bytes(&client, push, sizeof(push));
	record_begin(&client, GPU_OP_CMD_DRAW, 0x513);
	vulkan_write_u32(&client, 6);
	vulkan_write_u32(&client, 1);
	vulkan_write_u32(&client, 0);
	vulkan_write_u32(&client, 0);
	record_begin(&client, GPU_OP_CMD_END_RENDER_PASS, 0x513);
	record_begin(&client, GPU_OP_CMD_PIPELINE_BARRIER, 0x513);
	vulkan_write_u32(&client, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
	vulkan_write_u32(&client, VK_PIPELINE_STAGE_TRANSFER_BIT);
	vulkan_write_u32(&client, 0);
	vulkan_write_u32(&client, 0);
	vulkan_write_u64(&client, 0);
	vulkan_write_u32(&client, 0);
	vulkan_write_u64(&client, 0);
	vulkan_write_u32(&client, 0);
	vulkan_write_u64(&client, 0);
	memset(&copy, 0, sizeof(copy));
	copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.srcSubresource.layerCount = 1;
	copy.dstSubresource = copy.srcSubresource;
	copy.dstOffset.x = 10;
	copy.dstOffset.y = 20;
	copy.extent.width = 16;
	copy.extent.height = 8;
	copy.extent.depth = 1;
	record_begin(&client, GPU_OP_CMD_COPY_IMAGE, 0x513);
	vulkan_write_u64(&client, 0x503);
	vulkan_write_u32(&client, VK_IMAGE_LAYOUT_GENERAL);
	vulkan_write_u64(&client, 0x502);
	vulkan_write_u32(&client, VK_IMAGE_LAYOUT_GENERAL);
	vulkan_write_u32(&client, 1);
	vulkan_write_u64(&client, 1);
	vulkan_encode_VkImageCopy(&client, &copy);
	memset(&upload, 0, sizeof(upload));
	upload.bufferRowLength = 64;
	upload.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	upload.imageSubresource.layerCount = 1;
	upload.imageExtent.width = 32;
	upload.imageExtent.height = 8;
	upload.imageExtent.depth = 1;
	record_begin(&client, GPU_OP_CMD_COPY_BUFFER_TO_IMAGE, 0x513);
	vulkan_write_u64(&client, 0x505);
	vulkan_write_u64(&client, 0x502);
	vulkan_write_u32(&client, VK_IMAGE_LAYOUT_GENERAL);
	vulkan_write_u32(&client, 1);
	vulkan_write_u64(&client, 1);
	vulkan_encode_VkBufferImageCopy(&client, &upload);
	/*
	 * Two regions: 4x3 pixels from offset 0x100 with rows of 256 bytes
	 * (drawn: a linear image of 64 pixels a row), then 3x2 pixels from
	 * offset 4 with rows of 12 bytes (copied by the CPU).
	 */
	skewed = upload;
	skewed.bufferOffset = 0x100;
	skewed.bufferRowLength = 64;
	skewed.imageOffset.x = 10;
	skewed.imageOffset.y = 50;
	skewed.imageExtent.width = 4;
	skewed.imageExtent.height = 3;
	upload.bufferOffset = 4;
	upload.bufferRowLength = 3;
	upload.imageOffset.x = 5;
	upload.imageOffset.y = 40;
	upload.imageExtent.width = 3;
	upload.imageExtent.height = 2;
	record_begin(&client, GPU_OP_CMD_COPY_BUFFER_TO_IMAGE, 0x513);
	vulkan_write_u64(&client, 0x505);
	vulkan_write_u64(&client, 0x502);
	vulkan_write_u32(&client, VK_IMAGE_LAYOUT_GENERAL);
	vulkan_write_u32(&client, 2);
	vulkan_write_u64(&client, 2);
	vulkan_encode_VkBufferImageCopy(&client, &skewed);
	vulkan_encode_VkBufferImageCopy(&client, &upload);
	/* Image 0x503's 8x4 pixels at (4, 2) into the staging buffer at 0x1000, rows of 256 bytes (drawn: the buffer is the target). */
	memset(&download, 0, sizeof(download));
	download.bufferOffset = 0x1000;
	download.bufferRowLength = 64;
	download.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	download.imageSubresource.layerCount = 1;
	download.imageOffset.x = 4;
	download.imageOffset.y = 2;
	download.imageExtent.width = 8;
	download.imageExtent.height = 4;
	download.imageExtent.depth = 1;
	record_begin(&client, GPU_OP_CMD_COPY_IMAGE_TO_BUFFER, 0x513);
	vulkan_write_u64(&client, 0x503);
	vulkan_write_u32(&client, VK_IMAGE_LAYOUT_GENERAL);
	vulkan_write_u64(&client, 0x505);
	vulkan_write_u32(&client, 1);
	vulkan_write_u64(&client, 1);
	vulkan_encode_VkBufferImageCopy(&client, &download);
	memset(&whole, 0, sizeof(whole));
	whole.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	whole.levelCount = 1;
	whole.layerCount = 1;
	record_begin(&client, GPU_OP_CMD_CLEAR_COLOR_IMAGE, 0x513);
	vulkan_write_u64(&client, 0x503);
	vulkan_write_u32(&client, VK_IMAGE_LAYOUT_GENERAL);
	vulkan_write_u64(&client, 1);
	vulkan_write_u32(&client, 2);
	vulkan_write_u64(&client, 4);
	vulkan_write_u32(&client, 0x3f800000);
	vulkan_write_u32(&client, 0);
	vulkan_write_u32(&client, 0);
	vulkan_write_u32(&client, 0x3f800000);
	vulkan_write_u32(&client, 1);
	vulkan_write_u64(&client, 1);
	vulkan_encode_VkImageSubresourceRange(&client, &whole);
	command_begin(&client, GPU_OP_END_COMMAND_BUFFER);
	vulkan_write_u64(&client, 0x513);
	error = frame_run(session, &client);
	check(error == 0, "submit: the recording ran");
	at = TEST_REPLY_AT / 4U;
	check(reply_u32(&at) == GPU_OP_BEGIN_COMMAND_BUFFER && reply_u32(&at) == VK_SUCCESS, "submit: begun");
	check(reply_u32(&at) == GPU_OP_END_COMMAND_BUFFER && reply_u32(&at) == VK_SUCCESS, "submit: ended executable");
	check(((struct amdrdna_vk_command_buffer *)amdrdna_vk_payload(session, I915_VK_OBJ_COMMAND_BUFFER, 0x513))->record_count == 14,
	    "submit: fourteen records kept (the barrier is not)");

	/* The staging buffer's bytes the CPU copies: 3x2 pixels from offset 4, rows of 12 bytes. */
	for (index = 0; index < 24; index++)
		scene[0x8000 + 4 + index] = (uint8_t)(0x40 + index);

	/* The textures' bytes: image 0x503 (rows of 256) and the rest of the staging buffer. */
	for (index = 0; index < 32U * 256U; index++)
		scene[0x4000 + index] = (uint8_t)((index % 256U) * 7U + (index / 256U) * 13U + 1U);
	for (index = 0x100; index < 0x4000; index++)
		scene[0x8000 + index] = (uint8_t)(index * 3U + 5U);

	/* The submission with the fence. */
	gpu_runs = 0;
	gpu_draws = 0;
	gpu_quads = 0;
	gpu_copies = 0;
	gpu_copied_bytes = 0;
	log_reset();
	frame_begin(&client);
	command_begin(&client, GPU_OP_QUEUE_SUBMIT);
	vulkan_write_u64(&client, 0x103);
	vulkan_write_u32(&client, 1);
	vulkan_write_u64(&client, 1);
	vulkan_write_u32(&client, VK_STRUCTURE_TYPE_SUBMIT_INFO);
	vulkan_write_u64(&client, 0);
	vulkan_write_u32(&client, 0);
	vulkan_write_u64(&client, 0);
	vulkan_write_u64(&client, 0);
	vulkan_write_u32(&client, 1);
	vulkan_write_u64(&client, 1);
	vulkan_write_u64(&client, 0x513);
	vulkan_write_u32(&client, 0);
	vulkan_write_u64(&client, 0);
	vulkan_write_u64(&client, 0x514);
	command_begin(&client, GPU_OP_GET_FENCE_STATUS);
	vulkan_write_u64(&client, TEST_DEVICE);
	vulkan_write_u64(&client, 0x514);
	error = frame_run(session, &client);
	check(error == 0, "submit: the submission ran");
	at = TEST_REPLY_AT / 4U;
	check(reply_u32(&at) == GPU_OP_QUEUE_SUBMIT && reply_u32(&at) == VK_SUCCESS, "submit: success");
	check(reply_u32(&at) == GPU_OP_GET_FENCE_STATUS && reply_u32(&at) == VK_SUCCESS, "submit: the fence is signaled");
	check(gpu_runs == 2 && decode_errors == 0, "submit: two runs of known packets (the CPU copy between them)");

	/* The CPU copy: image 0x502 (at 0, rows of 256) at (5, 40). */
	check(memcmp(scene + 40 * 256 + 5 * 4, scene + 0x8004, 12) == 0 && memcmp(scene + 41 * 256 + 5 * 4, scene + 0x8004 + 12, 12) == 0,
	    "submit: the region off 256 bytes copied by the CPU");
	check(gpu_copies == 1 && gpu_copied_bytes == 24, "submit: the CPU's copy counted with its 24 bytes");

	/* Each run starts with the context control, the defaults and the caches; every barrier writes a new value and waits for it. */
	starts = 0;
	acquires = 0;
	releases = 0;
	last = 0;
	ordered = 1;
	for (index = 0; index < packet_count; index++) {
		if (packet_log[index].op == 0x28U && index + 1U < packet_count && packet_log[index + 1U].op == 0x12U &&
		    (index == 0 || packet_log[index - 1U].run != packet_log[index].run))
			starts++;
		if (packet_log[index].op == 0x58U && packet_log[index].body[6] == 0xc3b1U)
			acquires++;
		if (packet_log[index].op == 0x49U) {
			releases++;
			if (index + 1U >= packet_count || packet_log[index + 1U].op != 0x3cU ||
			    packet_log[index + 1U].body[1] != packet_log[index].body[2] || packet_log[index + 1U].body[3] != packet_log[index].body[4] ||
			    packet_log[index].body[4] <= last || packet_log[index].body[0] != (20U | (5U << 8) | 0x70f000U))
				ordered = 0;
			last = packet_log[index].body[4];
		}
	}
	check(starts == gpu_runs && acquires == gpu_runs, "submit: each run starts with the context control, the defaults and every cache");
	check(releases == 13 && ordered, "submit: thirteen barriers (pass, five transfers, two run ends), each waited for with a new value");
	check(packets_with(0x46U, 16U | (4U << 8), 0) == releases && packets_with(0x46U, 46U, 0) == releases,
	    "submit: each barrier drains the pixel programs and flushes the colour metadata");
	check((ib_words_total % 8U) == 0, "submit: the buffers are whole numbers of eight words");

	/* Seven draws of six vertices: the pass's clear, the quad, the image copy, two uploads drawn, the download, the image's clear. */
	check(draw_count == 7 && gpu_draws == 1 && gpu_quads == 6, "submit: one draw and six quads counted for the node");
	for (index = 0; index < draw_count; index++) {
		check(draw_log[index].vertices == 6 && draw_log[index].instances == 1 && reg(&draw_log[index], 0x30908U) == 4U &&
		    reg(&draw_log[index], 0x28b54U) == ((2U << 15) | (1U << 23)) && reg(&draw_log[index], 0x2880cU) == ((1U << 4) | (1U << 15)),
		    "submit: a draw of six vertices as a triangle list, legacy vertex program in wave32, early depth");
	}
	if (draw_count != 7)
		return;

	/* The targets: image 0x502 (blue first) for the first five, image 0x503 for the last. */
	check(reg(&draw_log[0], 0x28c60U) == 0x300000U && reg(&draw_log[0], 0x28c70U) == ((10U << 2) | (1U << 11) | (1U << 15) | (1U << 17)) &&
	    reg(&draw_log[0], 0x28ec0U) == (63U | (63U << 14)), "submit: the pass's target is image 0x502");
	check(reg(&draw_log[6], 0x28c60U) == 0x300040U && reg(&draw_log[6], 0x28c70U) == ((10U << 2) | (1U << 15) | (1U << 17)) &&
	    reg(&draw_log[6], 0x28ec0U) == (31U | (31U << 14)), "submit: the clear's target is image 0x503");
	check(reg(&draw_log[5], 0x28c60U) == 0x300090U && reg(&draw_log[5], 0x28c70U) == ((10U << 2) | (1U << 15) | (1U << 17)) &&
	    reg(&draw_log[5], 0x28ec0U) == (3U | (63U << 14)), "submit: the download's target is the buffer at 0x1000, 64 pixels a row");

	/* The quad: the pipeline's programs and their registers, its blend, its viewport and scissor. */
	quad_draw = &draw_log[1];
	check(reg(quad_draw, 0xb120U) == (uint32_t)(pipeline->vertex.va >> 8) && reg(quad_draw, 0xb020U) == (uint32_t)(pipeline->fragment.va >> 8) &&
	    reg(quad_draw, 0xb128U) == pipeline->vertex.registers.counts && reg(quad_draw, 0xb12cU) == pipeline->vertex.registers.inputs &&
	    reg(quad_draw, 0xb028U) == pipeline->fragment.registers.counts && reg(quad_draw, 0xb02cU) == pipeline->fragment.registers.inputs,
	    "submit: the pipeline's programs and their counts");
	check(reg(quad_draw, 0x286ccU) == pipeline->fragment.compiled->pixel_inputs && reg(quad_draw, 0x286d0U) == reg(quad_draw, 0x286ccU) &&
	    reg(quad_draw, 0x28714U) == 9U && reg(quad_draw, 0x2823cU) == 0xfU && (reg(quad_draw, 0x286d8U) & 0x3fU) == 1U &&
	    reg(quad_draw, 0x28644U) == 0U && reg(quad_draw, 0x286c4U) == 0U && reg(quad_draw, 0x2870cU) == 4U,
	    "submit: one parameter exported and read as attribute 0, colour 0 of four floats");
	check(reg(quad_draw, 0x28780U) == 0x40000501U && reg(quad_draw, 0x28238U) == 0xfU, "submit: the quad blends one, one minus source alpha");
	check(reg(quad_draw, 0x2843cU) == 0x42000000U && reg(quad_draw, 0x28440U) == 0x42000000U && reg(quad_draw, 0x28444U) == 0x42000000U &&
	    reg(quad_draw, 0x28448U) == 0x42000000U && reg(quad_draw, 0x2844cU) == 0x3f800000U && reg(quad_draw, 0x28450U) == 0U,
	    "submit: the quad's viewport (64x64)");
	check(reg(quad_draw, 0x28240U) == 0x80000000U && reg(quad_draw, 0x28244U) == (64U | (64U << 16)), "submit: the quad's scissor");
	check(reg(quad_draw, 0xb138U) == 0U && reg(quad_draw, 0xb13cU) == 0U && reg(quad_draw, 0xb140U) == 0U,
	    "submit: the quad's base vertex, first instance and instance mask");

	/* The quad's tables: the push constants and the corners' buffer in the vertex program's, image 0x503 and its sampler in the fragment program's. */
	table = replay_store[1].vertex_table;
	check(memcmp(table, push, sizeof(push)) == 0, "submit: the quad's push constants in its vertex table");
	{
		memcpy(descriptor, table + pipeline->vertex.compiled->vertex_table_offset, 16);
		check(descriptor[0] == 0x30006000U && descriptor[1] == (8U << 16) && descriptor[2] == 6U && ((descriptor[3] >> 12) & 0x7fU) == 64U,
		    "submit: the corners' buffer: 0x30006000, stride 8, six records of two floats");
	}
	table = replay_store[1].fragment_table;
	{
		memcpy(descriptor, table + pipeline->fragment.compiled->textures[0].table_offset, 48);
		check(descriptor[0] == 0x300040U && ((descriptor[1] >> 20) & 0x1ffU) == 56U && ((descriptor[2] >> 14) & 0x3fffU) == 31U &&
		    (descriptor[3] & 0xfffU) == (4U | (5U << 3) | (6U << 6) | (7U << 9)), "submit: image 0x503's descriptor (RGBA, 32x32)");
		check(descriptor[8] == 0U && descriptor[10] == ((1U << 20) | (1U << 22) | (1U << 26)), "submit: sampler 0x508 (linear, repeating)");
	}

	/* The copy's viewport and scissor: (10, 20) to (26, 28). */
	check(reg(&draw_log[2], 0x2843cU) == 0x41000000U && reg(&draw_log[2], 0x28440U) == 0x41900000U &&
	    reg(&draw_log[2], 0x28444U) == 0x40800000U && reg(&draw_log[2], 0x28448U) == 0x41c00000U,
	    "submit: the copy's viewport (x 8 about 18, y 4 about 24)");
	check(reg(&draw_log[2], 0x28240U) == (10U | (20U << 16) | 0x80000000U) && reg(&draw_log[2], 0x28244U) == (26U | (28U << 16)) &&
	    reg(&draw_log[2], 0x28250U) == (10U | (20U << 16) | 0x80000000U) && reg(&draw_log[2], 0x28254U) == (26U | (28U << 16)),
	    "submit: the copy's scissor and viewport scissor");
	check(reg(&draw_log[2], 0x28780U) == 0U && reg(&draw_log[2], 0x28760U) == ((6U << 8) | (6U << 24)), "submit: the copy does not blend");

	/* Each draw replayed on the interpreter: the pixels it writes. */
	replayed = replay_store[0];
	check(replayed.error == NULL, "submit: the pass's clear replays");
	expected[0] = 0.0f;
	expected[1] = 0.0f;
	expected[2] = 0.5f;
	expected[3] = 1.0f;
	good = replayed.error == NULL;
	for (lane = 0; good && lane < REPLAY_LANES; lane++)
		good = colour_near(replayed.colour[lane], expected, 0.0f);
	check(good, "submit: the pass's clear writes its colour");
	check(replayed.error == NULL && replayed.screen[0][0] == 0.0f && replayed.screen[0][1] == 0.0f && replayed.screen[4][0] == 64.0f &&
	    replayed.screen[4][1] == 64.0f, "submit: the pass's clear covers the target");

	replayed = replay_store[1];
	if (replayed.error != NULL)
		printf("  replay of the quad: %s\n", replayed.error);
	good = replayed.error == NULL;
	for (lane = 0; good && lane < REPLAY_LANES; lane++) {
		bilinear_reference(scene + 0x4000, ((float)replayed.x[lane] + 0.5f) / 64.0f, ((float)replayed.y[lane] + 0.5f) / 64.0f, expected);
		good = colour_near(replayed.colour[lane], expected, 0.002f);
		if (!good)
			printf("  quad (%d, %d): %f %f %f %f, expected %f %f %f %f\n", replayed.x[lane], replayed.y[lane], replayed.colour[lane][0],
			       replayed.colour[lane][1], replayed.colour[lane][2], replayed.colour[lane][3], expected[0], expected[1], expected[2], expected[3]);
	}
	check(good, "submit: the quad samples image 0x503 bilinearly over the target");

	replayed = replay_store[2];
	if (replayed.error != NULL)
		printf("  replay of the copy: %s\n", replayed.error);
	good = replayed.error == NULL;
	for (lane = 0; good && lane < REPLAY_LANES; lane++) {
		bytes_colour(scene + 0x4000 + (replayed.y[lane] - 20) * 256 + (replayed.x[lane] - 10) * 4, 0, expected);
		good = colour_near(replayed.colour[lane], expected, 0.0f) && replayed.x[lane] >= 10 && replayed.x[lane] < 26;
	}
	check(good, "submit: the copy writes image 0x503's pixels one to one");

	replayed = replay_store[3];
	good = replayed.error == NULL;
	for (lane = 0; good && lane < REPLAY_LANES; lane++) {
		bytes_colour(scene + 0x8000 + replayed.y[lane] * 256 + replayed.x[lane] * 4, 1, expected);
		good = colour_near(replayed.colour[lane], expected, 0.0f) && replayed.y[lane] < 8;
	}
	check(good, "submit: the upload of 32x8 writes the buffer's pixels (blue first)");

	replayed = replay_store[4];
	good = replayed.error == NULL;
	for (lane = 0; good && lane < REPLAY_LANES; lane++) {
		bytes_colour(scene + 0x8100 + (replayed.y[lane] - 50) * 256 + (replayed.x[lane] - 10) * 4, 1, expected);
		good = colour_near(replayed.colour[lane], expected, 0.0f) && replayed.x[lane] >= 10 && replayed.x[lane] < 14 &&
		       replayed.y[lane] >= 50 && replayed.y[lane] < 53;
	}
	check(good, "submit: the upload from 0x100 writes its 4x3 pixels at (10, 50)");

	replayed = replay_store[5];
	good = replayed.error == NULL;
	for (lane = 0; good && lane < REPLAY_LANES; lane++) {
		bytes_colour(scene + 0x4000 + (replayed.y[lane] + 2) * 256 + (replayed.x[lane] + 4) * 4, 0, expected);
		good = colour_near(replayed.colour[lane], expected, 0.0f) && replayed.x[lane] < 8 && replayed.y[lane] < 4;
	}
	check(good, "submit: the download writes image 0x503's 8x4 pixels from (4, 2) into the buffer");

	replayed = replay_store[6];
	expected[0] = 1.0f;
	expected[1] = 0.0f;
	expected[2] = 0.0f;
	expected[3] = 1.0f;
	good = replayed.error == NULL;
	for (lane = 0; good && lane < REPLAY_LANES; lane++)
		good = colour_near(replayed.colour[lane], expected, 0.0f);
	check(good, "submit: the clear of image 0x503 writes red");

	check(amdrdna_vk_session_close(&session) == 0, "submit: session closed");
	check(allocations == before, "submit: nothing leaked");
	free(vertex_words);
	free(fragment_words);
}

/*
 * The hardware pieces on their own: the descriptors of images, samplers
 * and vertex formats, the viewport's arithmetic, the blend, the target,
 * the draw's topology, and the packet writer's overflow.
 */
static void
test_units(void)
{
	static const uint32_t identity[4] = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
	static const uint32_t swapped[4] = { VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_ONE };
	static uint32_t words[256];
	struct amdrdna_vk_pm4 pm4;
	struct amdrdna_vk_sampler sampler;
	struct amdrdna_vk_blend blend;
	struct amdrdna_vk_target target;
	uint32_t image[8];
	uint32_t sampled[4];
	uint32_t viewport[6];
	uint32_t code;
	uint32_t components;
	uint32_t bytes;
	uint32_t layout;
	bool alternate;

	/* Rows on 256 bytes. */
	check(amdrdna_vk_pitch(1) == 256 && amdrdna_vk_pitch(64) == 256 && amdrdna_vk_pitch(65) == 512 && amdrdna_vk_pitch(100) == 512,
	    "units: the rows of a linear image");

	/* An RGBA image of 32x32 at 0x123456700. */
	check(amdrdna_vk_image_descriptor(0x12345678900ULL, 32, 32, VK_FORMAT_R8G8B8A8_UNORM, identity, image) == 0, "units: an image descriptor");
	check(image[0] == 0x23456789U && (image[1] & 0xffU) == 0x01U && ((image[1] >> 20) & 0x1ffU) == 56U && (image[1] >> 30) == 3U &&
	    (image[2] & 0xfffU) == 7U && ((image[2] >> 14) & 0x3fffU) == 31U && (image[2] >> 31) == 1U,
	    "units: the image's address, format and size");
	check((image[3] & 0xfffU) == (4U | (5U << 3) | (6U << 6) | (7U << 9)) && ((image[3] >> 25) & 7U) == 0U && (image[3] >> 28) == 9U &&
	    image[4] == 0U && ((image[5] >> 20) & 7U) == 4U && image[6] == 0U && image[7] == 0U,
	    "units: the image's channels, type and modulation");

	/* Blue first in memory: red reads the third byte; a view's swap and a constant one compose with it. */
	check(amdrdna_vk_image_descriptor(0x30000000ULL, 64, 16, VK_FORMAT_B8G8R8A8_SRGB, NULL, image) == 0 &&
	    ((image[1] >> 20) & 0x1ffU) == 130U && (image[3] & 0xfffU) == (6U | (5U << 3) | (4U << 6) | (7U << 9)) &&
	    ((image[3] >> 25) & 7U) == 4U, "units: a blue-first sRGB image");
	check(amdrdna_vk_image_descriptor(0x30000000ULL, 64, 16, VK_FORMAT_B8G8R8A8_UNORM, swapped, image) == 0 &&
	    (image[3] & 0xfffU) == (4U | (5U << 3) | (6U << 6) | (1U << 9)), "units: a view's swizzle over a blue-first image");
	check(amdrdna_vk_image_descriptor(0x30000080ULL, 64, 16, VK_FORMAT_R8G8B8A8_UNORM, NULL, image) == EINVAL &&
	    amdrdna_vk_image_descriptor(0x30000000ULL, 0, 16, VK_FORMAT_R8G8B8A8_UNORM, NULL, image) == EINVAL &&
	    amdrdna_vk_image_descriptor(0x30000000ULL, 64, 16, VK_FORMAT_R16G16B16A16_SFLOAT, NULL, image) == EINVAL &&
	    !amdrdna_vk_image_format(VK_FORMAT_R16G16B16A16_SFLOAT, &code, &alternate),
	    "units: an address off 256 bytes, an empty image and a float format are refused");

	/* Samplers: linear and repeating; nearest, clamped to a white border, comparing less or equal, in pixels. */
	memset(&sampler, 0, sizeof(sampler));
	sampler.mag_filter = VK_FILTER_LINEAR;
	sampler.min_filter = VK_FILTER_LINEAR;
	sampler.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	amdrdna_vk_sampler_descriptor(&sampler, sampled);
	check(sampled[0] == 0U && sampled[1] == (0xf00U << 12) && sampled[2] == ((1U << 20) | (1U << 22) | (2U << 26)) && sampled[3] == 0U,
	    "units: a linear sampler");
	memset(&sampler, 0, sizeof(sampler));
	sampler.address_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	sampler.address_v = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	sampler.address_w = VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
	sampler.border_color = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	sampler.compare_enable = 1;
	sampler.compare_op = VK_COMPARE_OP_LESS_OR_EQUAL;
	sampler.unnormalized = 1;
	amdrdna_vk_sampler_descriptor(&sampler, sampled);
	check(sampled[0] == (6U | (1U << 3) | (3U << 6) | (3U << 12) | (1U << 15) | (1U << 27)) && sampled[2] == (1U << 26) &&
	    sampled[3] == (2U << 30), "units: a nearest comparing sampler in pixels with a white border");

	/* Vertex formats. */
	check(amdrdna_vk_vertex_format(VK_FORMAT_R32G32_SFLOAT, &code, &components, &bytes) && code == 64U && components == 2U && bytes == 8U &&
	    amdrdna_vk_vertex_format(VK_FORMAT_R8G8B8A8_UNORM, &code, &components, &bytes) && code == 56U && bytes == 4U &&
	    amdrdna_vk_vertex_format(VK_FORMAT_R32G32B32A32_UINT, &code, &components, &bytes) && code == 75U && bytes == 16U &&
	    !amdrdna_vk_vertex_format(VK_FORMAT_R16G16_SFLOAT, &code, &components, &bytes), "units: the vertex formats");

	/* A viewport flipped upward (y 64, height -64) and one off whole pixels (x 0.5, width 10.25). */
	log_reset();
	amdrdna_vk_pm4_start(&pm4, words, 256);
	viewport[0] = 0;
	viewport[1] = 0x42800000U;
	viewport[2] = 0x42800000U;
	viewport[3] = 0xc2800000U;
	viewport[4] = 0x3e800000U;
	viewport[5] = 0x3f400000U;
	amdrdna_vk_state_viewport(&pm4, viewport);
	viewport[0] = 0x3f000000U;
	viewport[1] = 0;
	viewport[2] = 0x41240000U;
	viewport[3] = 0x41000000U;
	viewport[4] = 0x3f800000U;
	viewport[5] = 0;
	amdrdna_vk_state_viewport(&pm4, viewport);
	check(!pm4.full && decode_buffer(words, pm4.count, 0) == 0, "units: the viewports decode");
	check(packet_count == 8 && packet_log[0].body[1] == 0x42000000U && packet_log[0].body[2] == 0x42000000U &&
	    packet_log[0].body[3] == 0xc2000000U && packet_log[0].body[4] == 0x42000000U && packet_log[0].body[5] == 0x3f000000U &&
	    packet_log[0].body[6] == 0x3e800000U, "units: the flipped viewport's transform (x 32 32, y -32 32, z 0.5 0.25)");
	check(packet_log[1].body[1] == 0x3e800000U && packet_log[1].body[2] == 0x3f400000U && packet_log[2].body[1] == (0x80000000U | (0U << 16)) &&
	    packet_log[3].body[1] == (64U | (64U << 16)), "units: the flipped viewport's depth range and scissor");
	check(packet_log[3 + 1].body[1] == 0x40a40000U && packet_log[4].body[2] == 0x40b40000U && packet_log[4].body[5] == 0xbf800000U &&
	    packet_log[5].body[1] == 0U && packet_log[5].body[2] == 0x3f800000U, "units: x 5.125 about 5.625; the reversed depths' range");
	check(registers.context[(0x28250U - 0x28000U) / 4U] == 0x80000000U && registers.context[(0x28254U - 0x28000U) / 4U] == (11U | (8U << 16)),
	    "units: the second viewport's scissor rounded out to 0 .. 11");

	/* Blends: one and one minus source alpha; separate alpha; a minimum takes ones; a factor that is not a factor. */
	log_reset();
	amdrdna_vk_pm4_start(&pm4, words, 256);
	memset(&blend, 0, sizeof(blend));
	blend.enable = 1;
	blend.colour_source = VK_BLEND_FACTOR_ONE;
	blend.colour_destination = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend.alpha_source = VK_BLEND_FACTOR_ONE;
	blend.alpha_destination = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend.write_mask = 0xf;
	check(amdrdna_vk_state_blend(&pm4, &blend) == 0 && decode_buffer(words, pm4.count, 0) == 0 &&
	    registers.context[(0x28780U - 0x28000U) / 4U] == 0x40000501U && registers.context[(0x28760U - 0x28000U) / 4U] == 0U &&
	    registers.context[(0x28238U - 0x28000U) / 4U] == 0xfU, "units: premultiplied blending (0x40000501)");
	log_reset();
	amdrdna_vk_pm4_start(&pm4, words, 256);
	blend.alpha_op = VK_BLEND_OP_MIN;
	blend.alpha_source = VK_BLEND_FACTOR_DST_ALPHA;
	blend.colour_op = VK_BLEND_OP_REVERSE_SUBTRACT;
	check(amdrdna_vk_state_blend(&pm4, &blend) == 0 && decode_buffer(words, pm4.count, 0) == 0 &&
	    registers.context[(0x28780U - 0x28000U) / 4U] == (1U | (4U << 5) | (5U << 8) | ((1U | (2U << 5) | (1U << 8)) << 16) | (1U << 29) | (1U << 30)),
	    "units: a reverse subtract with a separate minimum of alpha");
	blend.colour_source = 99;
	check(amdrdna_vk_state_blend(&pm4, &blend) == EINVAL, "units: an unknown factor is refused");
	blend.enable = 0;
	log_reset();
	amdrdna_vk_pm4_start(&pm4, words, 256);
	check(amdrdna_vk_state_blend(&pm4, &blend) == 0 && decode_buffer(words, pm4.count, 0) == 0 &&
	    registers.context[(0x28780U - 0x28000U) / 4U] == 0U && registers.context[(0x28760U - 0x28000U) / 4U] == ((6U << 8) | (6U << 24)),
	    "units: no blending turns the shortcut off");

	/* Targets: a blue-first sRGB target of 100x10; refused: a float format, an address off 256 bytes. */
	log_reset();
	amdrdna_vk_pm4_start(&pm4, words, 256);
	target.address = 0x12345678900ULL;
	target.width = 100;
	target.height = 10;
	target.format = VK_FORMAT_B8G8R8A8_SRGB;
	check(amdrdna_vk_state_target(&pm4, &target) == 0 && decode_buffer(words, pm4.count, 0) == 0, "units: a target");
	check(amdrdna_vk_target_format(VK_FORMAT_B8G8R8A8_SRGB, &layout) &&
	    registers.context[(0x28c60U - 0x28000U) / 4U] == 0x23456789U && registers.context[(0x28e40U - 0x28000U) / 4U] == 0x01U &&
	    registers.context[(0x28c70U - 0x28000U) / 4U] == ((10U << 2) | (6U << 8) | (1U << 11) | (1U << 15) | (1U << 17)) &&
	    registers.context[(0x28ec0U - 0x28000U) / 4U] == (9U | (99U << 14)) &&
	    registers.context[(0x28ee0U - 0x28000U) / 4U] == ((1U << 24) | (1U << 27)), "units: the target's registers");
	target.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	check(amdrdna_vk_state_target(&pm4, &target) == EINVAL, "units: a float target is refused");
	target.format = VK_FORMAT_R8G8B8A8_UNORM;
	target.address = 0x30000040ULL;
	check(amdrdna_vk_state_target(&pm4, &target) == EINVAL, "units: a target off 256 bytes is refused");

	/* Topologies: a fan is type 5, a strip 6; a patch list is refused. */
	log_reset();
	amdrdna_vk_pm4_start(&pm4, words, 256);
	check(amdrdna_vk_state_draw(&pm4, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN, 5, 2) == 0 && decode_buffer(words, pm4.count, 0) == 0 &&
	    draw_count == 1 && draw_log[0].vertices == 5 && draw_log[0].instances == 2 && reg(&draw_log[0], 0x30908U) == 5U &&
	    reg(&draw_log[0], 0x3096cU) == (128U | (256U << 9)), "units: a fan of five vertices, two instances");
	check(amdrdna_vk_state_draw(&pm4, VK_PRIMITIVE_TOPOLOGY_PATCH_LIST, 3, 1) == EINVAL, "units: a patch list is refused");

	/* A buffer too small: the packet that does not fit is not written, nor any after it. */
	amdrdna_vk_pm4_start(&pm4, words, 5);
	amdrdna_vk_pm4_set_context(&pm4, 0x28238U, 0xf);
	amdrdna_vk_pm4_set_context(&pm4, 0x28238U, 0xf);
	amdrdna_vk_pm4_event(&pm4, 16, 4);
	check(pm4.full && pm4.count == 3, "units: a full buffer holds whole packets only");
	amdrdna_vk_pm4_start(&pm4, words, 16);
	amdrdna_vk_pm4_event(&pm4, 16, 4);
	amdrdna_vk_pm4_pad(&pm4, 8);
	check(!pm4.full && pm4.count == 8 && words[2] == 0xffff1000U, "units: the padding of one-word packets");
}

/* A compute program's registers and its dispatch, as the executor writes them for a compiled module. */
static void
test_kernel(const char *directory)
{
	static uint32_t words[256];
	struct amdrdna_vk_pm4 pm4;
	struct amdrdna_vk_program kernel;
	struct amdrdna_gfx10_diagnostic diagnostic;
	uint32_t *module;
	size_t bytes;
	int error;

	module = read_spirv(directory, "ids", &bytes);
	check(module != NULL, "kernel: the module's SPIR-V");
	if (module == NULL)
		return;
	memset(&kernel, 0, sizeof(kernel));
	memset(&diagnostic, 0, sizeof(diagnostic));
	error = amdrdna_gfx10_compile(module, bytes / 4U, DRV_GPU_STAGE_COMPUTE, NULL, &kernel.compiled, &diagnostic);
	check(error == 0, "kernel: the module compiles");
	free(module);
	if (error != 0)
		return;
	amdrdna_gfx10_program_registers(kernel.compiled, &kernel.registers);
	kernel.va = 0x12345678900ULL;

	/* The program, then a dispatch of 3x2x1 groups. */
	log_reset();
	amdrdna_vk_pm4_start(&pm4, words, 256);
	amdrdna_vk_state_kernel(&pm4, &kernel, 0x60000040ULL);
	amdrdna_vk_pm4_dispatch(&pm4, 3, 2, 1);
	check(!pm4.full && decode_buffer(words, pm4.count, 0) == 0 && dispatch_count == 1, "kernel: the packets decode to one dispatch");
	if (dispatch_count == 1) {
		check(reg(&dispatch_log[0], 0xb830U) == 0x23456789U && reg(&dispatch_log[0], 0xb834U) == 0x01U &&
		    reg(&dispatch_log[0], 0xb848U) == kernel.registers.counts && reg(&dispatch_log[0], 0xb84cU) == kernel.registers.inputs &&
		    (reg(&dispatch_log[0], 0xb848U) & (1U << 30)) != 0U, "kernel: the code, the counts (vector memory in order) and the inputs");
		check(reg(&dispatch_log[0], 0xb81cU) == kernel.compiled->local_size[0] && reg(&dispatch_log[0], 0xb820U) == kernel.compiled->local_size[1] &&
		    reg(&dispatch_log[0], 0xb824U) == kernel.compiled->local_size[2] && reg(&dispatch_log[0], 0xb810U) == 0U &&
		    reg(&dispatch_log[0], 0xb900U) == 0x60000040U && reg(&dispatch_log[0], 0xb904U) == 0U,
		    "kernel: the workgroup's size, the start and the table");
		check(dispatch_log[0].groups[0] == 3 && dispatch_log[0].groups[1] == 2 && dispatch_log[0].groups[2] == 1 &&
		    packet_log[packet_count - 1U].compute == 1 && packet_log[packet_count - 1U].body[3] == 0x8045U,
		    "kernel: 3x2x1 groups, the compute bit and the start word (shader, start at 0, order, wave32)");
	}
	amdrdna_gfx10_program_free(kernel.compiled);
}

/* Every SPIR-V module the compositor embeds (wayland/shaders.h) compiles for GFX10 within what a draw binds. */
static void
test_compositor_shaders(void)
{
	static const struct {
		const char *name;
		const uint32_t *words;
		size_t bytes;
		enum drv_gpu_shader_stage stage;
	} modules[] = {
		{ "quad.vert", kwl_quad_vert, sizeof(kwl_quad_vert), DRV_GPU_STAGE_VERTEX },
		{ "quad.frag", kwl_quad_frag, sizeof(kwl_quad_frag), DRV_GPU_STAGE_FRAGMENT },
		{ "panel.vert", kwl_panel_vert, sizeof(kwl_panel_vert), DRV_GPU_STAGE_VERTEX },
		{ "panel.frag", kwl_panel_frag, sizeof(kwl_panel_frag), DRV_GPU_STAGE_FRAGMENT },
		{ "panel-batch.vert", kwl_batch_vert, sizeof(kwl_batch_vert), DRV_GPU_STAGE_VERTEX },
		{ "panel-batch.frag", kwl_batch_frag, sizeof(kwl_batch_frag), DRV_GPU_STAGE_FRAGMENT },
		{ "turn.vert", kwl_turn_vert, sizeof(kwl_turn_vert), DRV_GPU_STAGE_VERTEX },
		{ "panel.frag mode 0", kwl_panel_frag_0, sizeof(kwl_panel_frag_0), DRV_GPU_STAGE_FRAGMENT },
		{ "panel.frag mode 1", kwl_panel_frag_1, sizeof(kwl_panel_frag_1), DRV_GPU_STAGE_FRAGMENT },
		{ "panel.frag mode 2", kwl_panel_frag_2, sizeof(kwl_panel_frag_2), DRV_GPU_STAGE_FRAGMENT },
		{ "panel.frag mode 3", kwl_panel_frag_3, sizeof(kwl_panel_frag_3), DRV_GPU_STAGE_FRAGMENT },
		{ "panel.frag mode 4", kwl_panel_frag_4, sizeof(kwl_panel_frag_4), DRV_GPU_STAGE_FRAGMENT },
		{ "panel.frag mode 5", kwl_panel_frag_5, sizeof(kwl_panel_frag_5), DRV_GPU_STAGE_FRAGMENT },
		{ "panel.frag mode 6", kwl_panel_frag_6, sizeof(kwl_panel_frag_6), DRV_GPU_STAGE_FRAGMENT },
		{ "panel.frag mode 7", kwl_panel_frag_7, sizeof(kwl_panel_frag_7), DRV_GPU_STAGE_FRAGMENT },
	};
	struct amdrdna_gfx10_program *program;
	struct amdrdna_gfx10_diagnostic diagnostic;
	char what[96];
	unsigned index;
	int error;

	for (index = 0; index < sizeof(modules) / sizeof(modules[0]); index++) {
		program = NULL;
		memset(&diagnostic, 0, sizeof(diagnostic));
		error = amdrdna_gfx10_compile(modules[index].words, modules[index].bytes / 4U, modules[index].stage, NULL, &program, &diagnostic);
		snprintf(what, sizeof(what), "compositor: %s compiles (%d %s)", modules[index].name, error,
			 diagnostic.reason != NULL ? diagnostic.reason : "");
		check(error == 0 && program != NULL && program->texture_used <= AMDRDNA_VK_TEXTURES_MAX && program->push_bytes <= AMDRDNA_VK_PUSH_BYTES &&
		    program->table_bytes + 64U <= 2048U, what);
		if (program != NULL)
			amdrdna_gfx10_program_free(program);
	}
}
