/* -*- mode: c; c-file-style: "linux"; tab-width: 8; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Tests real context framing against a bounded fake kernel and shared resources. */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/mman.h>
#include <uapi/gpu.h>
#include "internal.h"

/* Owns one fake kernel allocation until the production context releases it. */
struct storage {
	uint64_t handle;
	uint32_t resource;
	size_t bytes;
	uint8_t *data;
	unsigned mapped;
};

/* Tracks bounded peer state independently of the production writer and decoder. */
static struct storage allocations[8];
static uint32_t next_identity;
static struct storage *reply_storage;
static unsigned requests;
static unsigned map_calls;
static unsigned unmap_calls;
static unsigned transfer_calls;
static unsigned notification_waits;
static int notifications = 1;
static uint64_t notification_sequence;
static int fail_mapping;
static int malformed_notification;
/* Selects independent cut-channel, wrong-opcode and native-result response cases. */
static unsigned response_fault;
static unsigned trailer_reads;
static unsigned sleeps;
static unsigned closes;
static uint64_t monotonic_time;
static uint32_t pending_opcode;
static uint32_t pending_payload;
static int pending;
static int withhold_reply;
static int frozen_clock;
static int incompatible_wire;
static int exported_stream_seen;

/* Supplies independent capset bytes, including unadvertised suffix storage. */
static uint32_t advertised_bytes = 156;
static uint32_t advertised_magic = 0x5a424453U;
static uint32_t advertised_flags = 1;

/* An optional independent command peer lets the combined fixture exercise actual context framing. */
static void (*context_command_peer)(const uint8_t *bytes, size_t count, uint8_t *reply, size_t capacity);

int vulkan_test_open(const char *path, int flags, ...);
int vulkan_test_close(int fd);
int vulkan_test_ioctl(int fd, unsigned long operation, ...);
int vulkan_test_clock_gettime(clockid_t clock, struct timespec *stamp);
int vulkan_test_nanosleep(const struct timespec *requested, struct timespec *remaining);
void *vulkan_test_mmap(void *address, size_t bytes, int protection, int flags, int fd, off_t offset);
int vulkan_test_munmap(void *address, size_t bytes);
static uint32_t word_at(const uint8_t *bytes);
static uint64_t long_at(const uint8_t *bytes);
static void store_word(uint8_t *bytes, uint32_t word);
static struct storage *by_handle(uint64_t handle);
static struct storage *by_resource(uint32_t resource);
static void submit_peer(const struct gpu_command *command);
static void complete_peer(void);
static void test_context(void);
static void test_vendor_capset(void);
static void test_external_types(struct vulkan_context *context, uint32_t expected);

/*
 * Opens the exact fake node selected by the production context.
 */
int
vulkan_test_open(
	const char *path,
	int flags,
	...)
{
	int comparison;

	/* Verifies that node selection and access flags reach the OS boundary unchanged. */
	comparison = strcmp(path, "/dev/gpu-test");
	assert(comparison == 0);
	assert(flags == O_RDWR);

	/* Succeeded: transfers the fixture's one descriptor identity. */
	return 21;
}

/*
 * Reclaims every remaining resource when the production descriptor is consumed.
 */
int
vulkan_test_close(
	int fd)
{
	unsigned index;

	/* Reclaims even resources whose last command timed out before completion. */
	assert(fd == 21);
	for (index = 0; index < 8; index++) {
		assert(allocations[index].mapped == 0);
		free(allocations[index].data);
		memset(&allocations[index], 0, sizeof(allocations[index]));
	}

	/* Makes a repeated production close observable without retaining stale peer state. */
	closes++;
	reply_storage = NULL;
	pending = 0;

	/* Succeeded: no shared storage survives descriptor teardown. */
	return 0;
}

/*
 * Validates fixed-width kernel requests and advances the independent peer.
 */
int
vulkan_test_ioctl(
	int fd,
	unsigned long operation,
	...)
{
	va_list arguments;
	void *argument;
	struct gpu_info *information;
	struct gpu_capset *capset;
	struct gpu_blob_create *blob;
	struct gpu_resource_destroy *destroy;
	struct gpu_transfer *transfer;
	struct gpu_resource_map *map;
	struct gpu_command_submit *submission;
	struct gpu_command_wait *wait;
	struct gpu_command command;
	struct storage *storage;
	unsigned index;

	/* Reads only the explicit ioctl pointer after confirming descriptor ownership. */
	assert(fd == 21);
	va_start(arguments, operation);
	argument = va_arg(arguments, void *);
	va_end(arguments);

	/* Supplies complete capabilities without depending on renderer bootstrap state. */
	if (operation == GPU_GET_INFO) {
		information = argument;
		assert(information->version == GPU_ABI_VERSION);
		assert(information->size == sizeof(*information));
		information->capabilities = GPU_CAP_CAPSET | GPU_CAP_BLOB | GPU_CAP_TRANSFER | GPU_CAP_COMMAND | GPU_CAP_MAPPING;
		if (notifications)
			information->capabilities |= GPU_CAP_NOTIFICATION;
		information->max_resource_bytes = 4 * 1024 * 1024;
		information->max_resources = UINT32_MAX;
		return 0;
	}

	/* Supplies the pinned protocol prefix and the mandatory queue timeline capability. */
	if (operation == GPU_GET_CAPSET) {
		capset = argument;
		assert(capset->capset_id == 4);
		capset->bytes = advertised_bytes;
		store_word(capset->data, 1);
		if (incompatible_wire)
			store_word(capset->data, 2);

		/* Keeps XML and queue capabilities independent of the selected Vulkan API version. */
		store_word(capset->data + 4, VK_MAKE_VERSION(1, 3, 269));
		store_word(capset->data + 152, 1);
		store_word(capset->data + 160, advertised_magic);
		store_word(capset->data + 164, advertised_flags);
		return 0;
	}

	/* Allocates real host bytes while using distinct kernel and renderer identities. */
	if (operation == GPU_BLOB_CREATE) {
		blob = argument;
		assert(operation == _IOWR('G', 4, uint8_t[40]));
		assert(blob->version == 1U && blob->size == 40U);
		assert(blob->handle == 0U && blob->resource_id == 0U);
		assert(blob->blob_id == 0);
		assert(blob->flags == GPU_BLOB_MAPPABLE);
		storage = NULL;

		/* Finds a free fixture slot without constraining the production object registry. */
		for (index = 0; index < 8; index++) {
			if (allocations[index].data == NULL) {
				storage = &allocations[index];
				break;
			}
		}

		/* Creates checked storage for the exact byte extent requested by the context. */
		assert(storage != NULL);
		storage->bytes = (size_t)blob->bytes;
		storage->data = calloc(1, storage->bytes);
		assert(storage->data != NULL);
		next_identity++;
		storage->handle = 1000 + next_identity;
		storage->resource = 500 + next_identity;
		blob->handle = storage->handle;
		blob->resource_id = storage->resource;
		return 0;
	}

	/* Returns only a live resource named in the kernel handle namespace. */
	if (operation == GPU_RESOURCE_DESTROY) {
		destroy = argument;
		storage = by_handle(destroy->handle);
		assert(storage != NULL);
		assert(storage->mapped == 0);
		free(storage->data);
		memset(storage, 0, sizeof(*storage));
		return 0;
	}

	/* Publishes an opaque page offset for exactly one live fixture resource. */
	if (operation == GPU_RESOURCE_MAP) {
		map = argument;
		storage = by_handle(map->handle);
		assert(storage != NULL);
		map->offset = storage->handle * 4096;
		map->bytes = storage->bytes;
		return 0;
	}

	/* Accepts decoder submission without completing its reply inline. */
	if (operation == GPU_COMMAND_SUBMIT) {
		submission = argument;
		assert(submission->version == GPU_ABI_VERSION);
		assert(submission->size == sizeof(*submission));
		assert(submission->flags == GPU_COMMAND_CONTEXT_FENCE);
		assert(submission->timeline == 0 && submission->reserved == 0);
		memset(&command, 0, sizeof(command));
		command.version = GPU_ABI_VERSION;
		command.size = sizeof(command);
		command.address = submission->address;
		command.bytes = submission->bytes;
		submit_peer(&command);
		notification_sequence++;
		submission->sequence = notification_sequence;
		return 0;
	}

	/* Notification is an independent event whose wake does not encode VkResult. */
	if (operation == GPU_COMMAND_WAIT) {
		wait = argument;
		assert(wait->version == GPU_ABI_VERSION && wait->size == sizeof(*wait));
		assert(wait->sequence == notification_sequence);
		assert(wait->flags == GPU_WAIT_CONSUME);
		assert(wait->timeout_ns > 0 && wait->timeout_ns <= UINT64_C(10000000000));
		notification_waits++;

		/* A cut completion channel cannot authorize reuse of prior reply bytes. */
		if (response_fault == 1U) {
			errno = EPIPE;
			return -1;
		}

		if (withhold_reply) {
			errno = ETIMEDOUT;
			return -1;
		}

		if (!malformed_notification)
			complete_peer();

		wait->status = 0;
		return 0;
	}

	/* Copies only the exact bounded interval described by the public GPU ABI. */
	if (operation == GPU_RESOURCE_READ || operation == GPU_RESOURCE_WRITE) {
		transfer_calls++;
		transfer = argument;
		storage = by_handle(transfer->handle);
		assert(storage != NULL);
		assert(transfer->bytes <= GPU_COPY_MAX);
		assert(transfer->offset <= storage->bytes);
		assert(transfer->bytes <= storage->bytes - transfer->offset);
		assert(transfer->reserved == 0);

		/* Produces completion only after two independent trailer reads. */
		if (operation == GPU_RESOURCE_READ && storage == reply_storage && pending) {
			trailer_reads++;
			if (trailer_reads >= 2 && !withhold_reply)
				complete_peer();
		}

		/* Uses host memory copies as an independent model of kernel resource access. */
		if (operation == GPU_RESOURCE_WRITE) {
			memcpy(storage->data + transfer->offset, (void *)(uintptr_t)transfer->address, transfer->bytes);
		} else {
			memcpy((void *)(uintptr_t)transfer->address, storage->data + transfer->offset, transfer->bytes);
		}

		/* Succeeded: this bounded kernel request copied exactly its stated span. */
		return 0;
	}

	/* Parses the complete control stream without executing Vulkan operations. */
	if (operation == GPU_COMMAND) {
		submit_peer(argument);
		return 0;
	}

	/* Fails requests outside the explicitly modeled existing transport ABI. */
	errno = EINVAL;
	return -1;
}

/*
 * Maps only the fixture's exact opaque resource token without allocating alias copies.
 */
void *
vulkan_test_mmap(
	void *address,
	size_t bytes,
	int protection,
	int flags,
	int fd,
	off_t offset)
{
	struct storage *storage;

	/* Mapping failure must preserve the prior valid transport allocation. */
	assert(address == NULL && fd == 21);
	assert(protection == (PROT_READ | PROT_WRITE) && flags == MAP_SHARED);
	assert((offset & 4095) == 0);
	if (fail_mapping) {
		errno = ENOMEM;
		return MAP_FAILED;
	}

	storage = by_handle((uint64_t)offset / 4096);
	assert(storage != NULL && storage->bytes == bytes && storage->mapped == 0);
	storage->mapped = 1;
	map_calls++;
	return storage->data;
}

/*
 * Requires every view to retire before its underlying resource can be reclaimed.
 */
int
vulkan_test_munmap(
	void *address,
	size_t bytes)
{
	unsigned index;

	/* The view and resource have separate lifetime counters. */
	for (index = 0; index < 8; index++) {
		if (allocations[index].data == address) {
			assert(allocations[index].bytes == bytes && allocations[index].mapped == 1);
			allocations[index].mapped = 0;
			unmap_calls++;
			return 0;
		}
	}

	assert(0);
	return -1;
}

/*
 * Supplies deterministic monotonic progress for finite deadline verification.
 */
int
vulkan_test_clock_gettime(
	clockid_t clock,
	struct timespec *stamp)
{
	/* Advances real-deadline inputs unless the fixture explicitly freezes the clock. */
	assert(clock == CLOCK_MONOTONIC);
	if (!frozen_clock)
		monotonic_time += 100000000;

	/* Produces a normalized timestamp without actually delaying the host test. */
	stamp->tv_sec = (time_t)(monotonic_time / 1000000000);
	stamp->tv_nsec = (long)(monotonic_time % 1000000000);

	/* Succeeded: the production deadline logic receives the controlled timestamp. */
	return 0;
}

/*
 * Records cooperative polling without introducing a real sleep into the fixture.
 */
int
vulkan_test_nanosleep(
	const struct timespec *requested,
	struct timespec *remaining)
{
	/* Verifies the production poll interval and records its bounded use. */
	assert(requested->tv_sec == 0);
	assert(requested->tv_nsec == 1000000);
	assert(remaining == NULL);
	sleeps++;
	trailer_reads++;
	if (pending && !withhold_reply && trailer_reads >= 2)
		complete_peer();

	/* Succeeded: the next poll may run immediately in this deterministic fixture. */
	return 0;
}

/*
 * Runs small and shared-stream transactions, stale-reply, and timeout checks.
 */
int
main(
	void)
{
	/* Exercises the real context's session ownership and reply transaction paths. */
	test_context();

	/* Rejects accidental vendor negotiation while preserving explicit private WSI formats. */
	test_vendor_capset();
	puts("libvulkan context: direct/shared streams, independent replies, stale markers and finite deadlines PASS");

	/* Succeeded: the bounded kernel peer accepted every expected production request. */
	return 0;
}

/* Decodes an independent peer's little-endian control word. */
static uint32_t
word_at(
	const uint8_t *bytes)
{
	/* Succeeded: returns one protocol word using the fixture's byte view. */
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

/* Decodes a peer field that has a fixed 64-bit protocol extent. */
static uint64_t
long_at(
	const uint8_t *bytes)
{
	uint64_t low;
	uint64_t high;

	/* Combines two independently decoded peer words without host alignment assumptions. */
	low = word_at(bytes);
	high = word_at(bytes + 4);

	/* Succeeded: returns the full field observed by the kernel peer. */
	return low | (high << 32);
}

/* Stores independent renderer response bytes rather than using the production writer. */
static void
store_word(
	uint8_t *bytes,
	uint32_t word)
{
	/* Writes the exact little-endian representation expected at the kernel boundary. */
	bytes[0] = (uint8_t)word;
	bytes[1] = (uint8_t)(word >> 8);
	bytes[2] = (uint8_t)(word >> 16);
	bytes[3] = (uint8_t)(word >> 24);

	/* Succeeded: this fixture response word is ready for a production read. */
	return;
}

/* Resolves an existing fixture allocation in the kernel handle namespace. */
static struct storage *
by_handle(
	uint64_t handle)
{
	unsigned index;

	/* Finds only a live resource matching the whole owned kernel handle. */
	for (index = 0; index < 8; index++) {
		if (allocations[index].data != NULL && allocations[index].handle == handle)
			return &allocations[index];
	}

	/* The requested identity does not name a live fake kernel resource. */
	return NULL;
}

/* Resolves a fixture allocation in the separate renderer resource namespace. */
static struct storage *
by_resource(
	uint32_t resource)
{
	unsigned index;

	/* Finds only a live resource matching the entire protocol resource identity. */
	for (index = 0; index < 8; index++) {
		if (allocations[index].data != NULL && allocations[index].resource == resource)
			return &allocations[index];
	}

	/* The requested protocol identity is absent from the shared-resource set. */
	return NULL;
}

/* Validates control framing and references the application's actual shared bytes. */
static void
submit_peer(
	const struct gpu_command *command)
{
	const uint8_t *bytes;
	const uint8_t *application;
	const uint8_t *tail;
	struct storage *stream;
	uint32_t opcode;
	uint32_t resource;
	uint64_t scalar;
	size_t application_bytes;
	unsigned index;

	/* Bounds the entire direct ioctl before parsing its fixed transport prefix. */
	assert(command->version == GPU_ABI_VERSION);
	assert(command->size == sizeof(*command));
	assert(command->bytes <= GPU_COMMAND_MAX);
	assert(command->bytes >= 76);
	bytes = (const uint8_t *)(uintptr_t)command->address;
	opcode = word_at(bytes);
	assert(opcode == 178);
	resource = word_at(bytes + 16);
	reply_storage = by_resource(resource);
	assert(reply_storage != NULL);
	scalar = long_at(bytes + 20);
	assert(scalar == 0);
	scalar = long_at(bytes + 28);
	assert(scalar == reply_storage->bytes);

	/* Requires the production context to clear every previous completion byte. */
	for (index = 0; index < 20; index++) {
		assert(reply_storage->data[reply_storage->bytes - 20 + index] == 0);
	}

	/* Resolves shared commands through the resource and byte extent actually encoded. */
	application = bytes + 36;
	application_bytes = command->bytes - 68;
	opcode = word_at(application);
	if (opcode == 180) {
		scalar = long_at(application + 12);
		assert(scalar == 1);
		resource = word_at(application + 20);
		stream = by_resource(resource);
		assert(stream != NULL);
		scalar = long_at(application + 24);
		assert(scalar == 0);
		scalar = long_at(application + 32);
		assert(scalar <= stream->bytes);
		application_bytes = (size_t)scalar;
		application = stream->data;
		exported_stream_seen = 1;
	}

	/* Records application identity and payload independently of transport framing. */
	assert(application_bytes >= 12);
	pending_opcode = word_at(application);
	pending_payload = word_at(application + 8);
	if (context_command_peer != NULL) {
		/* The combined fixture decodes the complete transported Vulkan stream independently. */
		context_command_peer(application, application_bytes, reply_storage->data, reply_storage->bytes - 20);
	} else if (application_bytes > GPU_COMMAND_MAX) {
		/* Verifies the final large input byte also reached the shared kernel resource. */
		assert(application[application_bytes - 1] == 0xa5);
	}

	/* Verifies the independent completion trailer targets the owned response tail. */
	tail = bytes + command->bytes - 32;
	opcode = word_at(tail);
	assert(opcode == 179);
	scalar = long_at(tail + 8);
	assert(scalar == reply_storage->bytes - 20);
	opcode = word_at(tail + 16);
	assert(opcode == 137);

	/* Defers completion until a later resource read rather than returning it with receipt. */
	pending = 1;
	trailer_reads = 0;
	requests++;

	/* Succeeded: the fixture retained a valid command and its independent completion job. */
	return;
}

/* Completes the pending reply and its final trailer in the simulated shared memory. */
static void
complete_peer(
	void)
{
	uint8_t *tail;

	/* Produces the application response before the final decoder-completion store. */
	if (context_command_peer == NULL) {
		/* Ordinary transport cases supply a minimal independent application response. */
		store_word(reply_storage->data, pending_opcode);
		store_word(reply_storage->data + 4, 0);
		store_word(reply_storage->data + 8, pending_payload);
	}

	/* Corrupt application framing independently from the otherwise valid completion trailer. */
	if (response_fault == 2U)
		store_word(reply_storage->data, pending_opcode + 1U);

	/* A valid native failure remains distinguishable from transport and framing failure. */
	if (response_fault == 3U)
		store_word(reply_storage->data + 4, (uint32_t)VK_ERROR_DEVICE_LOST);

	if (response_fault == 4U)
		store_word(reply_storage->data + 4, (uint32_t)VK_ERROR_OUT_OF_DEVICE_MEMORY);

	tail = reply_storage->data + reply_storage->bytes - 20;
	store_word(tail, 137);
	store_word(tail + 4, 0);
	store_word(tail + 8, 1);
	store_word(tail + 12, 0);
	store_word(tail + 16, VK_MAKE_VERSION(1, 3, 269));
	pending = 0;

	/* Succeeded: the production poll can now prove this specific response completed. */
	return;
}

/* Verifies ownership, dynamic transport, and terminal timeout behavior. */
static void
test_context(
	void)
{
	struct vulkan_context context;
	struct vulkan_writer writer;
	struct vulkan_reader first;
	struct vulkan_reader second;
	uint8_t *large;
	VkResult error;
	uint32_t payload;
	unsigned before;
	unsigned variant;

	/* Rejects incompatible protocol data while consuming its descriptor once. */
	incompatible_wire = 1;
	error = vulkan_context_open(&context, "/dev/gpu-test");
	assert(error == VK_ERROR_INCOMPATIBLE_DRIVER);
	assert(context.fd == -1);
	assert(closes == 1);
	incompatible_wire = 0;

	/* Receives a normal reply only after independent trailer progress. */
	error = vulkan_context_open(&context, "/dev/gpu-test");
	assert(error == VK_SUCCESS);
	vulkan_writer_init(&writer);
	vulkan_command_begin(&writer, 42);
	vulkan_write_u32(&writer, 0x12345678);
	error = vulkan_command_execute(&context, &writer, 12, &first, VK_TRUE);
	assert(error == VK_SUCCESS);
	assert(notification_waits == 1);
	assert(sleeps == 0);
	assert(transfer_calls == 0);

	/* A failed larger mapping preserves the first live view for a later successful retry. */
	fail_mapping = 1;
	error = vulkan_command_execute(&context, &writer, 8192, &second, VK_TRUE);
	assert(error == VK_ERROR_OUT_OF_DEVICE_MEMORY);
	assert(context.error == VK_SUCCESS);
	vulkan_reader_finish(&second);
	fail_mapping = 0;

	/* Executes input beyond the direct ioctl limit with a larger independently owned reply. */
	large = malloc(80 * 1024);
	assert(large != NULL);
	memset(large, 0xa5, 80 * 1024);
	vulkan_command_begin(&writer, 43);
	vulkan_write_u32(&writer, 0x10203040);
	vulkan_write_bytes(&writer, large, 80 * 1024);
	free(large);
	error = vulkan_command_execute(&context, &writer, 8192, &second, VK_TRUE);
	assert(error == VK_SUCCESS);
	assert(exported_stream_seen);
	payload = vulkan_read_u32(&second);
	assert(payload == 0x10203040);
	payload = vulkan_read_u32(&first);
	assert(payload == 0x12345678);
	vulkan_reader_finish(&first);
	vulkan_reader_finish(&second);

	/* Withholds new completion after a prior valid marker to reject stale-response success. */
	withhold_reply = 1;
	before = requests;
	error = vulkan_command_execute(&context, &writer, 8192, &first, VK_TRUE);
	assert(error == VK_ERROR_DEVICE_LOST);
	assert(requests == before + 1);
	vulkan_reader_finish(&first);
	error = vulkan_command_execute(&context, &writer, 8192, &first, VK_TRUE);
	assert(error == VK_ERROR_DEVICE_LOST);
	assert(requests == before + 1);
	vulkan_reader_finish(&first);
	error = vulkan_context_close(&context);
	assert(error == VK_SUCCESS);

	/* Verifies a frozen deadline clock still reaches the finite stagnant-poll boundary. */
	frozen_clock = 1;
	notifications = 0;
	sleeps = 0;
	error = vulkan_context_open(&context, "/dev/gpu-test");
	assert(error == VK_SUCCESS);
	error = vulkan_command_execute(&context, &writer, 12, &first, VK_TRUE);
	assert(error == VK_ERROR_DEVICE_LOST);
	assert(sleeps < 10000);
	assert(sleeps == 9999);
	assert(transfer_calls == 0);
	vulkan_reader_finish(&first);
	vulkan_writer_finish(&writer);
	error = vulkan_context_close(&context);
	assert(error == VK_SUCCESS);
	before = closes;
	error = vulkan_context_close(&context);
	assert(error == VK_SUCCESS);
	assert(closes == before);

	/* A notification without a valid trailer cannot fabricate a completed response. */
	frozen_clock = 0;
	notifications = 1;
	withhold_reply = 0;
	malformed_notification = 1;
	error = vulkan_context_open(&context, "/dev/gpu-test");
	assert(error == VK_SUCCESS);
	vulkan_writer_init(&writer);
	vulkan_command_begin(&writer, 42);
	vulkan_write_u32(&writer, 17);
	error = vulkan_command_execute(&context, &writer, 12, &first, VK_TRUE);
	assert(error == VK_ERROR_DEVICE_LOST);
	vulkan_reader_finish(&first);
	vulkan_writer_finish(&writer);
	error = vulkan_context_close(&context);
	assert(error == VK_SUCCESS);
	assert(map_calls == unmap_calls);
	assert(transfer_calls == 0);

	/* Channel, opcode and native-result failures retain their distinct terminal or retryable contract. */
	malformed_notification = 0;
	for (variant = 1U; variant <= 4U; variant++) {
		error = vulkan_context_open(&context, "/dev/gpu-test");
		assert(error == VK_SUCCESS);
		vulkan_writer_init(&writer);
		vulkan_command_begin(&writer, 42);
		vulkan_write_u32(&writer, 17);
		response_fault = variant;
		before = requests;
		error = vulkan_command_execute(&context, &writer, 12, &first, VK_TRUE);
		assert(requests == before + 1U);

		/* Only a definite native allocation refusal permits another ordinary transaction. */
		if (variant == 4U) {
			assert(error == VK_ERROR_OUT_OF_DEVICE_MEMORY && context.error == VK_SUCCESS);
		} else {
			assert(error == VK_ERROR_DEVICE_LOST && context.error == VK_ERROR_DEVICE_LOST);
		}

		vulkan_reader_finish(&first);
		response_fault = 0U;
		error = vulkan_command_execute(&context, &writer, 12, &first, VK_TRUE);
		if (variant == 4U) {
			assert(error == VK_SUCCESS && requests == before + 2U);
		} else {
			assert(error == VK_ERROR_DEVICE_LOST && requests == before + 1U);
		}

		vulkan_reader_finish(&first);
		vulkan_writer_finish(&writer);
		error = vulkan_context_close(&context);
		assert(error == VK_SUCCESS);
	}

	assert(map_calls == unmap_calls && transfer_calls == 0);

	/* Succeeded: every submitted or failed transaction released its local and session ownership. */
	return;
}

/* Verifies that only the exact agreed vendor suffix enables raw native OPAQUE. */
static void
test_vendor_capset(
	void)
{
	struct vulkan_context context;
	uint32_t lengths[9];
	uint32_t magics[9];
	uint32_t flags[9];
	uint32_t expected;
	unsigned index;
	VkResult status;

	/* Cases include stock, truncated, unfamiliar and future extended records. */
	lengths[0] = 156;
	lengths[1] = 160;
	lengths[2] = 167;
	lengths[3] = 168;
	lengths[4] = 168;
	lengths[5] = 168;
	lengths[6] = 168;
	lengths[7] = 172;
	lengths[8] = 168;

	/* Assigns the independent protocol magic and exact supported feature bit. */
	for (index = 0; index < 9; index++) {
		magics[index] = 0x5a424453U;
		flags[index] = 1;
	}

	/* Unknown magic or any unknown flags must leave the old native type selected. */
	magics[3] = 0x5a424452U;
	flags[4] = 0;
	flags[5] = 2;
	flags[6] = 3;

	/* Each open independently chooses a profile from its actual returned capset extent. */
	for (index = 0; index < 9; index++) {
		advertised_bytes = lengths[index];
		advertised_magic = magics[index];
		advertised_flags = flags[index];
		status = vulkan_context_open(&context, "/dev/gpu-test");
		assert(status == VK_SUCCESS);
		expected = 0x200U;
		if (index == 6 || index == 8)
			expected = 1U;

		assert(context.external_memory_type == expected);
		assert(context.strict_queue == (index == 6));
		test_external_types(&context, expected);
		status = vulkan_context_close(&context);
		assert(status == VK_SUCCESS);
	}

	/* Succeeded: no malformed or future record accidentally enabled raw OPAQUE. */
	return;
}

/* Independently checks only native type words while preserving private WSI requests. */
static void
test_external_types(
	struct vulkan_context *context,
	uint32_t expected)
{
	struct vulkan_object object;
	struct vulkan_writer writer;
	VkExternalMemoryBufferCreateInfo buffer;
	VkExternalMemoryImageCreateInfo image;
	uint32_t input;
	uint32_t output;
	unsigned mode;

	/* Bare writers retain the stock encoding without an owning renderer context. */
	vulkan_writer_init(&writer);
	assert(writer.external_memory_type == 0x200U);
	vulkan_writer_finish(&writer);

	/* Each application object obtains immutable profile selection from its own context. */
	memset(&object, 0, sizeof(object));
	object.context = context;
	memset(&buffer, 0, sizeof(buffer));
	buffer.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
	memset(&image, 0, sizeof(image));
	image.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;

	/* Public OPAQUE is translated; explicit private DMA-BUF bypasses that translation. */
	for (mode = 0; mode < 2; mode++) {
		input = 1U;
		output = expected;
		if (mode != 0) {
			input = 0x200U;
			output = 0x200U;
		}

		/* Buffer and image create chains must agree with allocation's selected native type. */
		buffer.handleTypes = input;
		vulkan_writer_init_for_object(&writer, &object);
		vulkan_encode_buffer_external(&writer, &buffer);
		assert(writer.bytes == 24 && writer.error == VK_SUCCESS);
		assert(long_at(writer.data) == 1);
		assert(word_at(writer.data + 8) == 1000072000U);
		assert(long_at(writer.data + 12) == 0);
		assert(word_at(writer.data + 20) == output);
		vulkan_writer_finish(&writer);

		/* A private display image remains DMA-BUF even on the paired raw OPAQUE renderer. */
		image.handleTypes = input;
		vulkan_writer_init_for_object(&writer, &object);
		vulkan_encode_image_external(&writer, &image);
		assert(writer.bytes == 24 && writer.error == VK_SUCCESS);
		assert(long_at(writer.data) == 1);
		assert(word_at(writer.data + 8) == 1000072001U);
		assert(long_at(writer.data + 12) == 0);
		assert(word_at(writer.data + 20) == output);
		vulkan_writer_finish(&writer);
	}

	/* Succeeded: transport negotiation changes only standard public OPAQUE declarations. */
	return;
}
