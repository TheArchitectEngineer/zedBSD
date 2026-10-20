/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the PowerVR Vulkan executor (vk-*.c): frames written by
 * libvulkan's own encoders (frame-host.c) go through the executor with
 * the render node's memory, mappings and kick replaced by host stand-ins
 * below.  The check reads the replies with libvulkan's decoders, and the
 * compute stream the submission handed the core: its kernels, their
 * programs (compared with the shader compiled for the workgroup with the
 * expected address), the fence after the dispatch and the end.
 *
 *   vk-host-test DIR    (DIR holds fill.spv and reject-size.spv)
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uapi/errno.h>

#include "drivers/gpu/powervr/render-private.h"
#include "drivers/gpu/powervr/interface.h"
#include "drivers/gpu/powervr/compute-program.h"
#include "drivers/gpu/powervr/shader.h"
#include "drivers/gpu/powervr/render-job.h"
#include "drivers/gpu/powervr/transfer-job.h"
#include "drivers/gpu/powervr/vk.h"
#include "drivers/gpu/powervr/vk-draw.h"
#include "frame-host.h"

/* Where the stand-in hands out device addresses, and how far apart. */
#define HOST_ADDRESS_BASE	0x8000100000ULL
#define HOST_ADDRESS_STEP	0x100000ULL

/* The stand-in blobs, and the most compute commands recorded. */
#define HOST_BLOBS		8U
#define HOST_KICKS		4U

/* The workgroups the compute frame dispatches. */
#define HOST_GROUPS		3U

static int failures;
static int passes;
static uint64_t next_address = HOST_ADDRESS_BASE;
static uint32_t kicked[HOST_KICKS][POWERVR_COMPUTE_COMMAND_WORDS];
static unsigned kicks;
static struct powervr_resource *resources[64];
static unsigned resource_count;
/* Whether each resource was made shareable (its mapping then goes around the SLC, powervr_gpu_buffer_flags; p006's test). */
static int shareables[64];
static struct powervr_buffer *buffers[64];
static unsigned buffer_count;
static uint64_t mapped_address[16];
static uint64_t mapped_physical[16];
static unsigned mapped_count;

static void
check(
	int condition,
	const char *what)
{
	if (condition) {
		passes++;
		return;
	}

	failures++;
	printf("FAIL: executor: %s\n", what);
}

/* The kernel services the executor calls, on the host. */
void *kern_malloc(size_t size) { return malloc(size); }
void *kern_calloc(size_t count, size_t size) { return calloc(count, size); }
void kern_free(void *pointer) { free(pointer); }
void kern_logf(const char *format, ...) { va_list list; va_start(list, format); printf("  log: "); vprintf(format, list); va_end(list); }
void mutex_lock(struct mutex *mutex) { (void)mutex; }
void mutex_unlock(struct mutex *mutex) { (void)mutex; }
unsigned long spin_lock_irqsave(struct spinlock *lock) { (void)lock; return 0; }
void spin_unlock_irqrestore(struct spinlock *lock, unsigned long enabled) { (void)lock; (void)enabled; }

/* The render node's memory and mappings, on the host: a buffer is calloc'd memory, its view the memory itself. */
int
powervr_buffer_create(
	struct powervr_render *render,
	uint64_t bytes,
	struct powervr_buffer **result)
{
	struct powervr_buffer *buffer;
	uint64_t size;

	(void)render;
	size = (bytes + 4095U) & ~(uint64_t)4095U;
	buffer = calloc(1, sizeof(*buffer));
	buffer->view = calloc(1, (size_t)size);
	buffer->bytes = bytes;
	buffer->run.size = (size_t)size;
	buffer->run.paddr = 0x40000000ULL + (uint64_t)buffer_count * 0x1000000ULL;
	buffer->references = 1;
	buffers[buffer_count++] = buffer;
	*result = buffer;
	return 0;
}

void
powervr_buffer_retain(
	struct powervr_render *render,
	struct powervr_buffer *buffer)
{
	(void)render;
	buffer->references++;
}

void
powervr_buffer_put(
	struct powervr_render *render,
	struct powervr_buffer *buffer)
{
	(void)render;
	buffer->references--;
	if (buffer->references == 0) {
		free((void *)buffer->view);
		free(buffer);
	}
}

int
powervr_resource_make(
	struct powervr_session *session,
	struct powervr_buffer *buffer,
	int mappable,
	int shareable,
	struct powervr_resource **result)
{
	struct powervr_resource *resource;

	resource = calloc(1, sizeof(*resource));
	resource->session = session;
	resource->buffer = buffer;
	resource->mappable = mappable;
	resource->address = next_address;
	next_address += HOST_ADDRESS_STEP;
	buffer->references++;
	shareables[resource_count] = shareable;
	resources[resource_count++] = resource;
	*result = resource;
	return 0;
}

void
powervr_resource_unmake(
	struct powervr_session *session,
	struct powervr_resource *resource)
{
	struct powervr_resource **link;

	for (link = &session->blobs; *link != NULL; link = &(*link)->next_blob) {
		if (*link == resource) {
			*link = resource->next_blob;
			break;
		}
	}

	powervr_buffer_put(session->render, resource->buffer);
	resource->buffer = NULL;
}

struct powervr_resource *
powervr_session_blob(
	struct powervr_session *session,
	uint32_t identifier)
{
	struct powervr_resource *resource;

	for (resource = session->blobs; resource != NULL; resource = resource->next_blob) {
		if (resource->identifier == identifier)
			return resource;
	}

	return NULL;
}

int
powervr_gpu_mmu_map(
	struct powervr_gpu_mmu *mmu,
	uint64_t address,
	uint64_t physical,
	uint64_t bytes,
	unsigned flags)
{
	(void)mmu;
	(void)bytes;
	(void)flags;
	if (mapped_count < 16) {
		mapped_address[mapped_count] = address;
		mapped_physical[mapped_count] = physical;
		mapped_count++;
	}
	return 0;
}

/* Finds the buffer the executor mapped at a fixed place. */
static const volatile uint8_t *
mapped_view(
	uint64_t address)
{
	unsigned index;
	unsigned buffer;

	for (index = 0; index < mapped_count; index++) {
		if (mapped_address[index] != address)
			continue;
		for (buffer = 0; buffer < buffer_count; buffer++) {
			if (buffers[buffer]->run.paddr == mapped_physical[index])
				return buffers[buffer]->view;
		}
	}

	return NULL;
}
void powervr_gpu_mmu_unmap(struct powervr_gpu_mmu *mmu, uint64_t address, uint64_t bytes) { (void)mmu; (void)address; (void)bytes; }
unsigned powervr_gpu_buffer_flags(int mappable, int shareable) { (void)mappable; (void)shareable; return 0; }
int powervr_command_mmu_flush(struct powervr_core *core) { (void)core; return 0; }

/* The stage lines are not part of this test. */
void powervr_stage_ok(enum powervr_stage stage) { (void)stage; }
void powervr_stage_fail(enum powervr_stage stage, enum powervr_stage_reason reason) { (void)stage; (void)reason; }
enum powervr_stage_reason powervr_stage_reason_of(int error) { (void)error; return POWERVR_REASON_ERROR; }

/* Render pass instances and transfers: none in this test's command buffers, so the stand-ins are not reached. */
/*
 * The kicks of transfers and render jobs, recorded in order when the
 * recording is on (the Keiland frame), refused (ENOTSUP) otherwise.  A
 * render job is recorded with its draws and its colour's load and store.
 */
struct kick_entry {
	char kind;
	uint32_t draws;
	uint32_t load_op;
	uint32_t store_op;
	int continued;
	uint32_t width;
	uint32_t height;
	uint32_t clear_blue;
	int depth;
	uint32_t depth_load_op;
	uint32_t depth_store_op;
	uint32_t clear_depth;
	uint32_t count;
	int set;
	uint32_t push_bytes;
	uint32_t push_first;
	float viewport_width;
};
static struct kick_entry jobs_run[64];
static unsigned kick_count;
static int kick_recording;

int
powervr_transfer_run(
	struct powervr_render *render,
	struct powervr_session *session,
	const struct powervr_transfer_request *request)
{
	(void)render;
	(void)session;
	(void)request;
	if (!kick_recording)
		return ENOTSUP;
	if (kick_count < 64U) {
		memset(&jobs_run[kick_count], 0, sizeof(jobs_run[0]));
		jobs_run[kick_count++].kind = 'T';
	}
	return 0;
}

int
powervr_render_job_run(
	struct powervr_render *render,
	struct powervr_session *session,
	const struct powervr_render_job *job)
{
	const struct powervr_vk_graphics *graphics;

	(void)render;
	(void)session;
	if (!kick_recording)
		return ENOTSUP;
	if (kick_count < 64U) {
		memset(&jobs_run[kick_count], 0, sizeof(jobs_run[0]));
		jobs_run[kick_count].kind = 'R';
		jobs_run[kick_count].draws = job->draw_count;
		jobs_run[kick_count].load_op = job->colors[0].load_op;
		jobs_run[kick_count].store_op = job->colors[0].store_op;
		jobs_run[kick_count].continued = job->continued;
		jobs_run[kick_count].width = job->width;
		jobs_run[kick_count].height = job->height;
		jobs_run[kick_count].clear_blue = job->colors[0].clear[2];
		jobs_run[kick_count].depth = job->depth.present;
		jobs_run[kick_count].depth_load_op = job->depth.load_op;
		jobs_run[kick_count].depth_store_op = job->depth.store_op;
		jobs_run[kick_count].clear_depth = job->depth.clear_depth;
		if (job->draw_count != 0) {
			jobs_run[kick_count].count = job->draws[0].count;
			jobs_run[kick_count].set = job->draws[0].sets[0] != NULL;
			jobs_run[kick_count].push_bytes = job->draws[0].push_bytes;
			memcpy(&jobs_run[kick_count].push_first, job->draws[0].push, 4);
			graphics = job->draws[0].pipeline;
			jobs_run[kick_count].viewport_width = graphics->fixed.dynamic_viewport ? job->draws[0].viewport.width : graphics->fixed.viewport.width;
		}
		kick_count++;
	}
	return 0;
}

/* The kick records the compute command; the wait finds the work done. */
int
powervr_session_kick_compute(
	struct powervr_render *render,
	struct powervr_session *session,
	uint32_t value,
	const uint32_t *command,
	uint32_t words)
{
	(void)render;
	(void)session;
	(void)value;
	if (kicks < HOST_KICKS && words == POWERVR_COMPUTE_COMMAND_WORDS)
		memcpy(kicked[kicks], command, sizeof(kicked[0]));
	kicks++;
	return 0;
}

int
powervr_session_wait(
	struct powervr_session *session,
	uint32_t value,
	unsigned milliseconds)
{
	(void)session;
	(void)value;
	(void)milliseconds;
	return 0;
}

/* Says whether a resource was made shareable. */
static int
resource_shareable(
	const struct powervr_resource *resource)
{
	unsigned index;

	for (index = 0; index < resource_count; index++) {
		if (resources[index] == resource)
			return shareables[index];
	}
	return 0;
}

/* A blob of the session's, listed by its identifier as render.c lists one. */
static struct powervr_resource *
make_blob(
	struct powervr_session *session,
	struct powervr_buffer *buffer)
{
	struct powervr_resource *resource;

	powervr_resource_make(session, buffer, 1, 0, &resource);
	powervr_buffer_put(session->render, buffer);
	resource->identifier = ++session->next_resource;
	resource->next_blob = session->blobs;
	session->blobs = resource;
	return resource;
}

static uint32_t *
load(
	const char *directory,
	const char *name,
	size_t *count)
{
	char path[4096];
	uint32_t *words;
	FILE *file;
	long bytes;

	snprintf(path, sizeof(path), "%s/%s.spv", directory, name);
	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	fseek(file, 0, SEEK_END);
	bytes = ftell(file);
	fseek(file, 0, SEEK_SET);
	words = malloc((size_t)bytes);
	if (fread(words, 1, (size_t)bytes, file) != (size_t)bytes) {
		fclose(file);
		free(words);
		return NULL;
	}

	fclose(file);
	*count = (size_t)bytes / 4U;
	return words;
}

/* Finds the resource whose device address range holds an address. */
static struct powervr_resource *
resource_at(
	uint64_t address)
{
	unsigned index;

	for (index = 0; index < resource_count; index++) {
		if (resources[index]->buffer != NULL && address >= resources[index]->address && address < resources[index]->address + resources[index]->buffer->run.size)
			return resources[index];
	}

	return NULL;
}

static uint32_t
word_at(
	const volatile uint8_t *view,
	uint64_t offset)
{
	uint32_t word;

	memcpy(&word, (const void *)(view + offset), sizeof(word));
	return word;
}

/*
 * Reads the stream the submission handed the core: per workgroup a
 * kernel whose program writes its words with its values, the fence, the
 * end.
 */
static void
check_stream(
	uint64_t memory_address,
	const uint32_t *spirv,
	size_t spirv_words)
{
	static struct powervr_shader_program program;
	struct powervr_shader_workspace *workspace;
	struct powervr_ir_shader *shader;
	uint32_t values[32];
	unsigned request;
	const volatile uint8_t *usc;
	const volatile uint8_t *pds;
	struct powervr_resource *stream;
	struct powervr_compute_kernel expected_kernel;
	uint8_t expected[0x400];
	uint32_t expected_words[POWERVR_COMPUTE_KERNEL_WORDS];
	uint32_t data[POWERVR_COMPUTE_PDS_DATA_WORDS];
	uint32_t code[POWERVR_COMPUTE_PDS_CODE_MAX];
	uint64_t stream_address;
	uint64_t at;
	size_t length;
	size_t index;
	unsigned count;
	unsigned field;
	uint32_t group;
	uint32_t word;
	int same;

	check(kicks == 1, "one compute command for the submission");
	usc = mapped_view(POWERVR_USC_HEAP_BASE + 0x10000U);
	pds = mapped_view(POWERVR_PDS_HEAP_BASE + 0x10000U);
	check(usc != NULL && pds != NULL, "the program buffers are mapped at their places");
	if (kicks != 1 || usc == NULL || pds == NULL)
		return;

	/* The command names the stream after its resume area, and the resume area. */
	stream_address = (uint64_t)kicked[0][10] | ((uint64_t)kicked[0][11] << 32);
	check(stream_address == ((uint64_t)kicked[0][12] | ((uint64_t)kicked[0][13] << 32)) + 0x1000U, "the stream follows its resume area");
	stream = resource_at(stream_address);
	check(stream != NULL, "the stream lies in a resource of the session");
	if (stream == NULL)
		return;
	at = stream_address - stream->address;

	/* fill.comp: data[x] = 3x + 7, local size 32, its requests' shared words. */
	workspace = calloc(1, powervr_shader_workspace_size());
	if (workspace == NULL || powervr_shader_read(spirv, spirv_words, POWERVR_SHADER_COMPUTE, NULL, workspace, &program, &shader) != 0 || program.shareds > 32) {
		check(0, "the fill shader reads");
		free(workspace);
		return;
	}
	for (group = 0; group < HOST_GROUPS; group++) {
		/* The buffer's address and the workgroup's id as the executor gives them. */
		memset(values, 0, sizeof(values));
		for (request = 0; request < program.request_count; request++) {
			if (program.requests[request].kind == POWERVR_SHADER_REQUEST_DESCRIPTOR) {
				values[program.requests[request].shared] = (uint32_t)memory_address;
				values[program.requests[request].shared + 1U] = (uint32_t)(memory_address >> 32);
			} else if (program.requests[request].kind == POWERVR_SHADER_REQUEST_WORKGROUP_ID) {
				values[program.requests[request].shared] = group;
			}
		}
		check(powervr_shader_compile_inline(spirv, spirv_words, POWERVR_SHADER_COMPUTE, NULL, values, program.shareds, workspace, &program, expected, sizeof(expected)) == 0, "the fill shader compiles for a workgroup");
		length = program.length;
		powervr_compute_temporaries_field(program.temps, 2, &field);

		/* The kernel's words. */
		memset(&expected_kernel, 0, sizeof(expected_kernel));
		expected_kernel.data_offset = 0x10000U + (group + 1U) * 0x40U;
		expected_kernel.code_offset = expected_kernel.data_offset + 0x10U;
		expected_kernel.data_words = POWERVR_COMPUTE_PDS_DATA_WORDS;
		expected_kernel.pds_temporaries = 3;
		expected_kernel.usc_temporaries = program.temps;
		expected_kernel.instances = 32;
		expected_kernel.any_cluster = 1;
		powervr_compute_kernel_words(&expected_kernel, expected_words);
		same = 1;
		for (index = 0; index < POWERVR_COMPUTE_KERNEL_WORDS; index++) {
			word = word_at(stream->buffer->view, at + index * 4U);
			if (word != expected_words[index])
				same = 0;
		}
		check(same, "a workgroup's kernel words");
		at += POWERVR_COMPUTE_KERNEL_WORDS * 4U;

		/* Its shading cluster program: the shader compiled with the group's id and the buffer's address. */
		check(memcmp((const void *)(usc + (size_t)group * 0x400U), expected, length) == 0, "a workgroup's program is the shader compiled for it");

		/* The data sequencer program that issues it. */
		powervr_compute_kick_program(0x10000U + group * 0x400U, field, data, code, &count);
		same = word_at(pds, (group + 1U) * 0x40U) == data[0] && word_at(pds, (group + 1U) * 0x40U + 4U) == data[1];
		for (index = 0; index < count; index++) {
			if (word_at(pds, (group + 1U) * 0x40U + 0x10U + index * 4U) != code[index])
				same = 0;
		}
		check(same, "a workgroup's issuing program");
	}

	free(workspace);

	/* The fence kernel after the dispatch, then the end. */
	memset(&expected_kernel, 0, sizeof(expected_kernel));
	expected_kernel.data_offset = 0x10000U;
	expected_kernel.code_offset = 0x10010U;
	expected_kernel.data_words = POWERVR_COMPUTE_PDS_DATA_WORDS;
	expected_kernel.instances = 1;
	expected_kernel.max_instances = 8;
	expected_kernel.fence = 1;
	expected_kernel.shared_from_pds = 1;
	expected_kernel.common_shared = 1;
	expected_kernel.any_cluster = 1;
	powervr_compute_kernel_words(&expected_kernel, expected_words);
	same = 1;
	for (index = 0; index < POWERVR_COMPUTE_KERNEL_WORDS; index++) {
		if (word_at(stream->buffer->view, at + index * 4U) != expected_words[index])
			same = 0;
	}
	check(same, "the fence kernel after the dispatch");
	at += POWERVR_COMPUTE_KERNEL_WORDS * 4U;
	check(word_at(stream->buffer->view, at) == powervr_compute_terminate_word(), "the end of the stream");

	/* The fence program in slot 0. */
	powervr_compute_fence_program(data, code, &count);
	same = word_at(pds, 0) == data[0] && word_at(pds, 4) == data[1];
	for (index = 0; index < count; index++) {
		if (word_at(pds, 0x10U + index * 4U) != code[index])
			same = 0;
	}
	check(same, "the fence program");
}

/*
 * Checks that the executor's program places, mapped at fixed offsets of
 * the data sequencer and shading cluster heaps, lie past the session's
 * fixed area at the start of each heap (BUG-342), and that its other
 * memory lies past the general heap's.
 */
static void
check_fixed_area(void)
{
	unsigned index;
	unsigned programs;
	int clear;

	programs = 0;
	clear = 1;
	for (index = 0; index < mapped_count; index++) {
		if (mapped_address[index] >= POWERVR_USC_HEAP_BASE) {
			programs++;
			if (mapped_address[index] < POWERVR_USC_HEAP_BASE + POWERVR_FIXED_USC_BYTES)
				clear = 0;
		} else if (mapped_address[index] >= POWERVR_PDS_HEAP_BASE) {
			programs++;
			if (mapped_address[index] < POWERVR_PDS_HEAP_BASE + POWERVR_FIXED_PDS_BYTES)
				clear = 0;
		} else if (mapped_address[index] < POWERVR_GENERAL_HEAP_BASE + POWERVR_FIXED_GENERAL_BYTES) {
			clear = 0;
		}
	}
	check(programs == 2, "the executor maps its two program buffers");
	check(clear, "the executor's mappings lie past the fixed area of each heap");
	for (index = 0; index < resource_count; index++) {
		if (resources[index]->address < POWERVR_GENERAL_HEAP_BASE + POWERVR_FIXED_GENERAL_BYTES)
			clear = 0;
	}
	check(clear, "the executor's resources lie past the general heap's fixed area");
}

int
main(
	int argc,
	char **argv)
{
	static struct powervr_core core;
	static struct powervr_render render;
	static struct powervr_session session;
	struct powervr_buffer *buffer;
	struct powervr_resource *reply;
	struct powervr_resource *memory_blob;
	struct powervr_resource *import_blob;
	struct powervr_vk_memory *imported;
	struct powervr_vk_memory *memory;
	struct powervr_vk_descriptor_set *set;
	const struct powervr_vk_buffer *bound;
	uint64_t set_address;
	uint64_t descriptor_address;
	uint32_t descriptor_size;
	struct powervr_vk_memory *image_memory;
	struct powervr_vk_image_view *view;
	struct powervr_vk_sampler *sampler;
	uint32_t *spirv;
	uint32_t *bad_spirv;
	uint8_t *frame;
	size_t spirv_words;
	size_t bad_words;
	size_t bytes;
	int frame_passed;
	int frame_failed;
	int error;

	if (argc != 2) {
		fprintf(stderr, "usage: vk-host-test DIR\n");
		return 2;
	}

	spirv = load(argv[1], "fill", &spirv_words);
	bad_spirv = load(argv[1], "reject-size", &bad_words);
	if (spirv == NULL || bad_spirv == NULL) {
		fprintf(stderr, "vk-host-test: shaders missing\n");
		return 2;
	}

	/* The node, the session, its executor and its reply blob. */
	render.core = &core;
	session.render = &render;
	error = powervr_vk_open(&session, &session.vk);
	check(error == 0, "executor opens");
	powervr_buffer_create(&render, FRAME_REPLY_BYTES, &buffer);
	reply = make_blob(&session, buffer);

	/* The first frame: instance, physical device, device, queue. */
	bytes = frame_setup(reply->identifier, &frame);
	error = powervr_vk_execute(session.vk, frame, (uint32_t)bytes);
	free(frame);
	check(error == 0, "setup frame runs");
	replies_setup((uint8_t *)reply->buffer->view);
	check((session.vk->timelines & 2U) != 0, "the queue's timeline 1 is the session's");

	/* The memory, then the blob libvulkan makes for it (its blob id the memory's identity). */
	bytes = frame_memory(reply->identifier, FRAME_BUFFER_BYTES, &frame);
	error = powervr_vk_execute(session.vk, frame, (uint32_t)bytes);
	free(frame);
	check(error == 0, "memory frame runs");
	replies_memory((uint8_t *)reply->buffer->view);
	error = powervr_vk_memory_buffer(session.vk, 10, FRAME_BUFFER_BYTES, &buffer);
	check(error == 0, "the memory's blob takes its buffer");
	error = powervr_vk_memory_buffer(session.vk, 10, FRAME_BUFFER_BYTES * 2U, &buffer);
	check(error == EINVAL, "a blob of another size is refused");
	powervr_vk_memory_buffer(session.vk, 10, FRAME_BUFFER_BYTES, &buffer);
	memory_blob = make_blob(&session, buffer);
	memory = powervr_vk_payload(session.vk, I915_VK_OBJ_MEMORY, 10);
	check(memory != NULL && memory_blob->buffer == memory->storage->buffer, "the blob maps the memory's own buffer");

	/* The compute frame. */
	bytes = frame_compute(reply->identifier, spirv, spirv_words, bad_spirv, bad_words, HOST_GROUPS, &frame);
	error = powervr_vk_execute(session.vk, frame, (uint32_t)bytes);
	free(frame);
	check(error == 0, "compute frame runs");
	replies_compute((uint8_t *)reply->buffer->view);
	if (memory != NULL)
		check_stream(memory->storage->address, spirv, spirv_words);

	/* The set's memory: binding 0's buffer descriptor holds the buffer's address and whole size (p010h). */
	set = powervr_vk_payload(session.vk, I915_VK_OBJ_DESCRIPTOR_SET, 17);
	bound = powervr_vk_payload(session.vk, I915_VK_OBJ_BUFFER, 11);
	check(set != NULL && bound != NULL && set->layout.bytes == 16 && set->bytes != NULL, "the set has 16 bytes of memory");
	if (set != NULL && bound != NULL && set->bytes != NULL && powervr_vk_buffer_address(bound, &set_address) == 0) {
		memcpy(&descriptor_address, &set->bytes[0], 8);
		memcpy(&descriptor_size, &set->bytes[8], 4);
		check(descriptor_address == set_address && descriptor_size == bound->size, "the buffer descriptor's address and size");
	}

	/* The image frame: the shared path refused, the linear and optimal images, the view's words. */
	powervr_buffer_create(&render, 16384, &buffer);
	import_blob = make_blob(&session, buffer);
	bytes = frame_image(reply->identifier, import_blob->identifier, &frame);
	error = powervr_vk_execute(session.vk, frame, (uint32_t)bytes);
	free(frame);
	check(error == 0, "image frame runs");
	replies_image((uint8_t *)reply->buffer->view);
	imported = powervr_vk_payload(session.vk, I915_VK_OBJ_MEMORY, 25);
	check(imported != NULL && imported->imported && imported->storage->buffer == import_blob->buffer, "the import maps the blob's own buffer");
	memory = powervr_vk_payload(session.vk, I915_VK_OBJ_MEMORY, 24);
	check(memory != NULL && memory->exported && !memory->imported, "the export is marked exported");
	check(memory != NULL && imported != NULL && resource_shareable(memory->storage) && resource_shareable(imported->storage), "the export and the import are mapped shareable (around the SLC)");
	memory = powervr_vk_payload(session.vk, I915_VK_OBJ_MEMORY, 27);
	check(memory != NULL && !resource_shareable(memory->storage), "a plain allocation is not made shareable");
	image_memory = powervr_vk_payload(session.vk, I915_VK_OBJ_MEMORY, 27);
	view = powervr_vk_payload(session.vk, I915_VK_OBJ_IMAGE_VIEW, 29);
	sampler = powervr_vk_payload(session.vk, I915_VK_OBJ_SAMPLER, 30);
	check(image_memory != NULL && view != NULL && sampler != NULL, "the image memory, view and sampler are published");
	if (image_memory != NULL && view != NULL) {
		check(view->words[0] == 0x0013007460000a61ULL, "the view's first word: 2D, RGBA8, edges 30x20");
		check(((view->words[1] >> 16) & 0x3FFFFFFFFFULL) << 2 == image_memory->storage->address + 4096U, "the view's address is the memory's plus the bind offset");
		check((view->words[1] & 0xF) == 1 && ((view->words[1] >> 4) & 0x7FF) == 0, "one level, one layer");
	}
	if (sampler != NULL)
		check(sampler->words[0] == 0x0200245008000fffULL, "the sampler's word: linear, clamp to edge, levels 0 to 0.25");
	check_fixed_area();

	/* A frame cut in the middle of a command is refused. */
	bytes = frame_setup(reply->identifier, &frame);
	error = powervr_vk_execute(session.vk, frame, (uint32_t)(bytes / 2U) & ~3U);
	free(frame);
	check(error != 0, "a cut frame is refused");

	/*
	 * The Keiland-like frame (ws269-p011): command buffer A has the copy,
	 * the pass of 20 draws (jobs of 16 and 4), the pass of 17 (16 and 1;
	 * each continuation loads what the job before it stored), then the
	 * clear.  Today vk-queue.c runs a begin's job only when there are as
	 * many jobs as begins, so with continuations every job runs after the
	 * command buffer's other records: copy, clear, then the four jobs.
	 * That is the order checked here until P1 runs a begin's job and its
	 * continued ones in place (then: T R16 R4c R16 R1c T).  Command buffer
	 * B's 200 draws need 13 jobs, more than QUEUE_JOBS_MAX (8): its submit
	 * fails (E2BIG, INITIALIZATION_FAILED) with no kick.
	 */
	{
		uint32_t submit_b;
		unsigned index;
		int ok;

		kick_recording = 1;
		bytes = frame_keiland(reply->identifier, &frame);
		error = powervr_vk_execute(session.vk, frame, (uint32_t)bytes);
		free(frame);
		check(error == 0, "keiland frame runs");
		submit_b = 0;
		replies_keiland((uint8_t *)reply->buffer->view, &submit_b);
		printf("  keiland kicks:");
		for (index = 0; index < kick_count; index++) {
			if (jobs_run[index].kind == 'R')
				printf(" R%u%s(load %u store %u)", jobs_run[index].draws, jobs_run[index].continued ? "c" : "", jobs_run[index].load_op, jobs_run[index].store_op);
			else
				printf(" T");
		}
		printf("; submit B %d\n", (int)submit_b);
		ok = kick_count == 6;
		ok = ok && jobs_run[0].kind == 'T' && jobs_run[1].kind == 'T';
		ok = ok && jobs_run[2].kind == 'R' && jobs_run[2].draws == 16 && !jobs_run[2].continued && jobs_run[2].load_op == VK_ATTACHMENT_LOAD_OP_CLEAR && jobs_run[2].store_op == VK_ATTACHMENT_STORE_OP_STORE;
		ok = ok && jobs_run[3].kind == 'R' && jobs_run[3].draws == 4 && jobs_run[3].continued && jobs_run[3].load_op == VK_ATTACHMENT_LOAD_OP_LOAD && jobs_run[3].store_op == VK_ATTACHMENT_STORE_OP_STORE;
		ok = ok && jobs_run[4].kind == 'R' && jobs_run[4].draws == 16 && !jobs_run[4].continued && jobs_run[4].load_op == VK_ATTACHMENT_LOAD_OP_CLEAR && jobs_run[4].store_op == VK_ATTACHMENT_STORE_OP_STORE;
		ok = ok && jobs_run[5].kind == 'R' && jobs_run[5].draws == 1 && jobs_run[5].continued && jobs_run[5].load_op == VK_ATTACHMENT_LOAD_OP_LOAD && jobs_run[5].store_op == VK_ATTACHMENT_STORE_OP_STORE;
		check(ok, "keiland A today: copy, clear, then jobs 16, 4c, 16, 1c (continuations load what was stored)");
		check(submit_b == (uint32_t)VK_ERROR_INITIALIZATION_FAILED, "keiland B: 200 draws (13 jobs > QUEUE_JOBS_MAX 8) are refused (E2BIG, until P1 raises the room)");
		kick_recording = 0;
	}

	/*
	 * The scene frames, as vkstage and vkdemo call libvulkan: each
	 * submission's render job is checked for its draw (the vertices, the
	 * set, the push area (the whole of it; vkdemo's time 0.0 then 1.0),
	 * the static viewport), its colour's clear
	 * and store, and with depth the D32 cleared to 1.0 and not stored
	 * (the ZLS loads nothing and stores nothing); the copy out of the
	 * colour is a transfer after it.
	 */
	{
		static const char *names[FRAME_SCENES] = { "vkstage draw", "vkstage sample", "vkstage depth", "vkdemo" };
		static const uint32_t widths[FRAME_SCENES] = { 64, 64, 64, 320 };
		static const uint32_t heights[FRAME_SCENES] = { 64, 64, 64, 240 };
		static const uint32_t counts[FRAME_SCENES] = { 3, 4, 6, 36 };
		static const uint32_t blues[FRAME_SCENES] = { 0x3f800000U, 0x3f800000U, 0x3f800000U, 0x3e20a0a1U };
		static const int depths[FRAME_SCENES] = { 0, 0, 1, 1 };
		static const int sets[FRAME_SCENES] = { 0, 1, 0, 1 };
		static const unsigned uploads[FRAME_SCENES] = { 0, 1, 0, 1 };
		static const unsigned frames[FRAME_SCENES] = { 1, 1, 1, 2 };
		uint32_t results[8];
		unsigned scene;
		unsigned submits;
		unsigned index;
		unsigned at;
		int ok;
		char what[128];

		for (scene = 0; scene < FRAME_SCENES; scene++) {
			kick_recording = 1;
			kick_count = 0;
			bytes = frame_scene(reply->identifier, scene, &frame);
			error = powervr_vk_execute(session.vk, frame, (uint32_t)bytes);
			free(frame);
			snprintf(what, sizeof(what), "%s frame runs", names[scene]);
			check(error == 0, what);
			memset(results, 0, sizeof(results));
			submits = replies_scene((uint8_t *)reply->buffer->view, results, 8);
			ok = submits == uploads[scene] + frames[scene];
			for (index = 0; index < submits && index < 8; index++)
				ok = ok && results[index] == VK_SUCCESS;
			snprintf(what, sizeof(what), "%s: every submission succeeds", names[scene]);
			check(ok, what);
			printf("  %s kicks:", names[scene]);
			for (index = 0; index < kick_count; index++) {
				if (jobs_run[index].kind == 'R')
					printf(" R%u(%ux%u, %u vertices, load %u store %u, depth %d load %u store %u, set %d, push %u from %08x, blue %08x, viewport %g, clear depth %08x)", jobs_run[index].draws, jobs_run[index].width, jobs_run[index].height, jobs_run[index].count, jobs_run[index].load_op, jobs_run[index].store_op, jobs_run[index].depth, jobs_run[index].depth_load_op, jobs_run[index].depth_store_op, jobs_run[index].set, jobs_run[index].push_bytes, jobs_run[index].push_first, jobs_run[index].clear_blue, (double)jobs_run[index].viewport_width, jobs_run[index].clear_depth);
				else
					printf(" T");
			}
			printf("\n");
			ok = kick_count == uploads[scene] + 2U * frames[scene];
			at = 0;
			if (uploads[scene] != 0)
				ok = ok && jobs_run[at++].kind == 'T';
			for (index = 0; ok && index < frames[scene]; index++) {
				const struct kick_entry *job = &jobs_run[at];

				ok = ok && job->kind == 'R' && job->draws == 1 && !job->continued;
				ok = ok && job->width == widths[scene] && job->height == heights[scene];
				ok = ok && job->load_op == VK_ATTACHMENT_LOAD_OP_CLEAR && job->store_op == VK_ATTACHMENT_STORE_OP_STORE && job->clear_blue == blues[scene];
				ok = ok && job->count == counts[scene] && job->set == sets[scene] && job->push_bytes == POWERVR_DRAW_PUSH_BYTES && job->viewport_width == (float)widths[scene];
				ok = ok && job->depth == depths[scene];
				if (scene == FRAME_SCENE_VKDEMO)
					ok = ok && job->push_first == (index == 0 ? 0U : 0x3f800000U);
				if (depths[scene])
					ok = ok && job->depth_load_op == VK_ATTACHMENT_LOAD_OP_CLEAR && job->depth_store_op == VK_ATTACHMENT_STORE_OP_DONT_CARE && job->clear_depth == 0x3f800000U;
				ok = ok && jobs_run[at + 1].kind == 'T';
				at += 2;
			}
			snprintf(what, sizeof(what), "%s: %s%u render job(s) of one draw, each followed by the copy out", names[scene], uploads[scene] ? "the upload, then " : "", frames[scene]);
			check(ok, what);
			kick_recording = 0;
		}
	}

	/* Close: every object and the executor's memory go. */
	powervr_vk_close(session.vk);
	session.vk = NULL;

	frame_counts(&frame_passed, &frame_failed);
	passes += frame_passed;
	failures += frame_failed;
	printf("powervr executor host test: %d passed, %d failed\n", passes, failures);
	return failures == 0 ? 0 : 1;
}
