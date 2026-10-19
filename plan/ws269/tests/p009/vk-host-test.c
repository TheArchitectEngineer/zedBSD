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
 * programs (compared with the fill program for the expected address and
 * values), the fence after the dispatch and the end.
 *
 *   vk-host-test DIR    (DIR holds fill.spv and reject-read.spv)
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
#include "drivers/gpu/powervr/vk.h"
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

	(void)shareable;
	resource = calloc(1, sizeof(*resource));
	resource->session = session;
	resource->buffer = buffer;
	resource->mappable = mappable;
	resource->address = next_address;
	next_address += HOST_ADDRESS_STEP;
	buffer->references++;
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
	uint64_t memory_address)
{
	const volatile uint8_t *usc;
	const volatile uint8_t *pds;
	struct powervr_resource *stream;
	struct powervr_compute_kernel expected_kernel;
	uint8_t expected[0x200];
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

	/* fill.comp: data[x] = 3x + 7, local size 32. */
	powervr_compute_temporaries_field(POWERVR_COMPUTE_FILL_TEMPORARIES, 2, &field);
	for (group = 0; group < HOST_GROUPS; group++) {
		/* The kernel's words. */
		memset(&expected_kernel, 0, sizeof(expected_kernel));
		expected_kernel.data_offset = 0x10000U + (group + 1U) * 0x40U;
		expected_kernel.code_offset = expected_kernel.data_offset + 0x10U;
		expected_kernel.data_words = POWERVR_COMPUTE_PDS_DATA_WORDS;
		expected_kernel.pds_temporaries = 3;
		expected_kernel.usc_temporaries = POWERVR_COMPUTE_FILL_TEMPORARIES;
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

		/* Its shading cluster program: the group's 32 words, value 3(32g + i) + 7. */
		powervr_compute_fill_program(memory_address + (uint64_t)group * 32U * 4U, 32, 3, 3U * 32U * group + 7U, expected, sizeof(expected), &length);
		check(memcmp((const void *)(usc + (size_t)group * 0x200U), expected, length) == 0, "a workgroup's program writes its words with its values");

		/* The data sequencer program that issues it. */
		powervr_compute_kick_program(0x10000U + group * 0x200U, field, data, code, &count);
		same = word_at(pds, (group + 1U) * 0x40U) == data[0] && word_at(pds, (group + 1U) * 0x40U + 4U) == data[1];
		for (index = 0; index < count; index++) {
			if (word_at(pds, (group + 1U) * 0x40U + 0x10U + index * 4U) != code[index])
				same = 0;
		}
		check(same, "a workgroup's issuing program");
	}

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
	struct powervr_vk_memory *memory;
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
	bad_spirv = load(argv[1], "reject-read", &bad_words);
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
		check_stream(memory->storage->address);

	/* A frame cut in the middle of a command is refused. */
	bytes = frame_setup(reply->identifier, &frame);
	error = powervr_vk_execute(session.vk, frame, (uint32_t)(bytes / 2U) & ~3U);
	free(frame);
	check(error != 0, "a cut frame is refused");

	/* Close: every object and the executor's memory go. */
	powervr_vk_close(session.vk);
	session.vk = NULL;

	frame_counts(&frame_passed, &frame_failed);
	passes += frame_passed;
	failures += frame_failed;
	printf("powervr executor host test: %d passed, %d failed\n", passes, failures);
	return failures == 0 ? 0 : 1;
}
