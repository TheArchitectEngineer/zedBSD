/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Vulkan executor of a GHCI device.
 *
 * libvulkan sends the Kei GPU command protocol (uapi/gpu-op.h) to the
 * node.  The executor keeps the Vulkan objects in the kernel: it decodes
 * every command, answers the instance, the physical device and its
 * limits from the GPU's capability registers, keeps memory, buffers,
 * images, command pools and buffers, fences, semaphores and events, and
 * turns a submission into GHCI packets in the ring of the device.
 *
 * One VkDevice is one GHCI session (a number 1 to 15) with its own GPU
 * addresses, and one executor heap in them for what the driver itself
 * writes for the GPU.  The objects of a node session live in a table of
 * the session's, found by their kind and the identity libvulkan chose.
 *
 * Locks.  The GPU core admits one ordinary command of a node session at
 * a time, so the object table and the arena need no lock of their own.
 * What the service thread, the job ioctls and the GPU core's recovery
 * read besides (the sessions of the executor, each session's devices,
 * timelines, job markers, queued frames and last tickets, and the stop
 * marks) changes only under the device's irq_lock.  The executor calls
 * the core's session and memory functions without the device's lock.
 */

#ifndef DRIVERS_GPU_GHCI_VK_VK_H
#define DRIVERS_GPU_GHCI_VK_VK_H

#include <stddef.h>
#include <stdint.h>

#include <libc/vulkan/vulkan_core.h>

#include "drivers/gpu/i915/vk/vk-codec.h"

#include "../ghci.h"
#include "../layout.h"

/* The buckets of a session's object table, and the most objects a session keeps. */
#define GHCI_VK_BUCKETS			256U
#define GHCI_VK_OBJECTS_MAX		65536U

/* The scratch one command decodes its records into. */
#define GHCI_VK_ARENA_BYTES		(256U * 1024U)

/* The timelines a queue may name (1 to 63; 0 is the node session's own). */
#define GHCI_VK_TIMELINES		64U

/* The VkFormat values of Vulkan 1.0 (0 to VK_FORMAT_ASTC_12x12_SRGB_BLOCK). */
#define GHCI_VK_FORMATS			185U

/* The completions the service retires in one pass under the interrupt's lock. */
#define GHCI_VK_DONE_BATCH		32U

/* The size of the memory heap the device reports, and the largest allocation. */
#define GHCI_VK_HEAP_SIZE		(256ULL << 20)

/* The alignment of every buffer and image in memory (the tiled subresource's 256 bytes). */
#define GHCI_VK_ALIGN			256U

/* The first block of a device's executor heap, the largest, and the runs one block keeps. */
#define GHCI_VK_HEAP_FIRST_BYTES	(64U * 1024U)
#define GHCI_VK_HEAP_BLOCK_MAX		(16U << 20)
#define GHCI_VK_HEAP_EXTENTS		256U

/* The most bytes of recorded commands one command buffer keeps. */
#define GHCI_VK_RECORD_MAX		(16U << 20)

/* The kinds of object the executor keeps; a lookup names the kind it expects. */
enum ghci_vk_kind {
	GHCI_VK_DEVICE = 1,
	GHCI_VK_QUEUE,
	GHCI_VK_MEMORY,
	GHCI_VK_BUFFER,
	GHCI_VK_IMAGE,
	GHCI_VK_FENCE,
	GHCI_VK_SEMAPHORE,
	GHCI_VK_EVENT,
	GHCI_VK_COMMAND_POOL,
	GHCI_VK_COMMAND_BUFFER
};

/* The life of a command buffer: new or reset, recording, and ended (ready to submit). */
enum ghci_vk_recording {
	GHCI_VK_RECORD_INITIAL = 0,
	GHCI_VK_RECORD_RECORDING,
	GHCI_VK_RECORD_EXECUTABLE
};

struct ghci_vk_session;
struct ghci_vk_object;

/*
 * The Vulkan format features of each VkFormat on optimal and linear
 * tiling and in buffers, made once at the attach from the FMT block (or
 * the table of the specification without it) and not changed after.
 * from_registers says that FMT was read.
 */
struct ghci_vk_formats {
	uint32_t optimal[GHCI_VK_FORMATS];
	uint32_t linear[GHCI_VK_FORMATS];
	uint32_t buffer[GHCI_VK_FORMATS];
	int from_registers;
};

/*
 * The executor of one GHCI device, from the attach to the detach.
 *
 * sessions lists the node sessions with the executor's side open
 * (irq_lock), for the service thread's retirement.  refusals counts the
 * refused commands the log has shown, so that a client cannot flood it;
 * it is counted without a lock (two sessions at once may show one more).
 */
struct ghci_vk {
	struct ghci_device *device;
	struct ghci_vk_formats formats;
	struct ghci_vk_session *sessions;
	unsigned refusals;
};

/*
 * One block of a device's executor heap: host memory mapped once into
 * the device's GHCI session, and the allocator of its bytes.  The block
 * lives until its device is destroyed.
 */
struct ghci_vk_heap_block {
	struct ghci_vk_heap_block *next;
	struct ghci_allocation *allocation;
	struct ghci_mapping *mapping;
	struct ghci_range_map map;
	struct ghci_range_extent extents[GHCI_VK_HEAP_EXTENTS];
};

/*
 * One VkDevice: its GHCI session, its executor heap, and the ticket of
 * the last work it placed in the ring.  It lives from vkCreateDevice to
 * vkDestroyDevice (or the node session's close).  next, last and
 * submitted change under irq_lock; the rest belongs to the session's
 * commands.
 */
struct ghci_vk_device {
	struct ghci_vk_session *session;
	struct ghci_vk_device *next;
	struct ghci_vm *vm;
	struct ghci_vk_heap_block *heap;
	uint64_t heap_bytes;
	struct ghci_ticket last;
	int submitted;
	int robust;
};

/*
 * A VkDeviceMemory: an allocation of its own (or the allocation of an
 * imported resource), which it holds, and its mapping in the device's
 * GHCI session.
 */
struct ghci_vk_memory {
	struct ghci_allocation *allocation;
	struct ghci_mapping *mapping;
	uint64_t bytes;
	uint32_t type;
	int imported;
};

/*
 * A VkBuffer: its size and use, and where it is bound: the identity of
 * the memory object (0 until then; the memory may be freed before the
 * buffer, so it is found again by its identity) and the offset.
 */
struct ghci_vk_buffer {
	uint64_t size;
	uint32_t usage;
	uint32_t flags;
	uint64_t memory;
	uint64_t offset;
};

/*
 * A VkImage: what it was made as, its linear layout (section 8.5.2 of
 * the specification) and its size, and where it is bound (the memory
 * object's identity, as a buffer's).
 */
struct ghci_vk_image {
	uint32_t format;
	uint32_t type;
	uint32_t tiling;
	uint32_t usage;
	uint32_t flags;
	struct ghci_layout_image layout;
	uint64_t bytes;
	uint64_t memory;
	uint64_t offset;
};

/* A VkFence: signaled on the host, or by the ticket of the submission that signals it. */
struct ghci_vk_fence {
	int signaled;
	int submitted;
	struct ghci_ticket ticket;
};

/* A VkEvent: one 64-bit word in the device's executor heap that the host and the GPU set. */
struct ghci_vk_event {
	struct ghci_vk_heap_block *block;
	uint32_t offset;
	volatile uint64_t *word;
};

/* A VkQueue: the timeline its jobs complete on. */
struct ghci_vk_queue {
	uint32_t timeline;
	uint32_t family;
};

/* A VkCommandPool: its flags and queue family. */
struct ghci_vk_command_pool {
	uint32_t flags;
	uint32_t family;
};

/*
 * A VkCommandBuffer: its pool, its level, its recording's state, and the
 * recorded commands as they came on the wire ([number][0][buffer] and
 * the arguments, one after another), which a submission executes.
 * failed says that a command could not be kept (the end of the
 * recording reports it).
 */
struct ghci_vk_command_buffer {
	uint64_t pool;
	uint32_t level;
	enum ghci_vk_recording state;
	int failed;
	uint8_t *bytes;
	uint32_t used;
	uint32_t capacity;
	uint32_t commands;
};

/*
 * One object of a node session, in the session's table from its create to
 * its destroy.  device is the VkDevice it was made on (a device's own
 * object points at its record).
 */
struct ghci_vk_object {
	struct ghci_vk_object *next;
	enum ghci_vk_kind kind;
	uint64_t identity;
	struct ghci_vk_device *device;
	union {
		struct ghci_vk_memory memory;
		struct ghci_vk_buffer buffer;
		struct ghci_vk_image image;
		struct ghci_vk_fence fence;
		struct ghci_vk_event event;
		struct ghci_vk_queue queue;
		struct ghci_vk_command_pool pool;
		struct ghci_vk_command_buffer commands;
	} as;
};

/*
 * A queued frame of a node session whose completion waits for the
 * session's work placed until the frame ran (irq_lock).  A frame with
 * nothing to wait for has ticket.seq 0; failed ends it with an error.
 */
struct ghci_vk_frame {
	struct ghci_vk_frame *next;
	struct drv_gpu_completion *completion;
	struct ghci_ticket ticket;
	int failed;
};

/*
 * The executor's side of one node session, from its open to its close.
 *
 * The object table, the arena and the opcodes for the log belong to the
 * session's commands.  Under irq_lock: next (the executor's list), the
 * devices, the device of each timeline, the job markers (and the
 * timeline each was reserved on), the queued
 * frames, the ticket of the session's last work, and the marks the GPU
 * core's stop and isolation set.
 */
struct ghci_vk_session {
	struct ghci_device *device;
	struct ghci_session *owner;
	struct ghci_vk_object *buckets[GHCI_VK_BUCKETS];
	unsigned object_count;
	struct i915_wire_arena arena;
	uint32_t last_opcode;
	uint32_t failed_opcode;
	struct ghci_vk_session *next;
	struct ghci_vk_device *devices;
	struct ghci_vk_device *timeline_device[GHCI_VK_TIMELINES];
	struct ghci_job_table jobs;
	uint32_t marker_timeline[GHCI_JOB_MARKERS];
	struct ghci_vk_frame *frames;
	unsigned frame_count;
	struct ghci_ticket last;
	int submitted;
	int stopping;
	int isolated;
};

/*
 * A handler of a group of commands: it runs the command when it owns the
 * opcode (and sets *handled), with the reader after the command's header
 * and the reply writer after the echoed opcode (written only when
 * requested).  Returns 0, or the error that ends the frame.
 */
typedef int (*ghci_vk_route)(struct ghci_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply, int *handled);

/* vk-node.c: the executor's attach and the node's operations. */
int ghci_vk_attach(struct ghci_device *device);

/* vk-object.c: the session's side of the node, its objects and its arena. */
int ghci_vk_session_open(struct ghci_device *device, struct ghci_session *owner);
void ghci_vk_session_close(struct ghci_device *device, struct ghci_session *owner);
struct ghci_vk_object *ghci_vk_object_find(struct ghci_vk_session *session, enum ghci_vk_kind kind, uint64_t identity);
int ghci_vk_object_add(struct ghci_vk_session *session, enum ghci_vk_kind kind, uint64_t identity, struct ghci_vk_device *device, struct ghci_vk_object **result);
void ghci_vk_object_destroy(struct ghci_vk_session *session, struct ghci_vk_object *object);
void ghci_vk_objects_of_device(struct ghci_vk_session *session, struct ghci_vk_device *device);
void *ghci_vk_arena_take(struct ghci_vk_session *session, size_t bytes);

/* vk-reply.c: the forms every command shares, the results and the float words. */
uint64_t ghci_vk_create_tail(struct i915_wire_reader *reader);
uint64_t ghci_vk_destroy_form(struct i915_wire_reader *reader, uint64_t *device);
uint64_t ghci_vk_read_count(struct i915_wire_reader *reader);
void ghci_vk_skip(struct i915_wire_reader *reader, uint64_t bytes);
void ghci_vk_reply_created(struct i915_wire_writer *reply, uint32_t requested, uint32_t result, uint64_t identity);
void ghci_vk_reply_result(struct i915_wire_writer *reply, uint32_t requested, uint32_t result);
uint32_t ghci_vk_result(int error);
uint32_t ghci_vk_float_ratio(uint64_t numerator, uint64_t denominator);
void ghci_vk_set_float(float *destination, uint32_t bits);

/* vk-dispatch.c and vk-stream.c: the command stream. */
int ghci_vk_dispatch(struct ghci_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
int ghci_vk_stream_execute(struct ghci_vk_session *session, const void *wire, uint32_t bytes);

/* vk-caps.c: the answers the capability registers give. */
void ghci_vk_caps_features(const struct ghci_caps *caps, VkPhysicalDeviceFeatures *features);
void ghci_vk_caps_limits(const struct ghci_caps *caps, VkPhysicalDeviceLimits *limits);
void ghci_vk_caps_memory(const struct ghci_caps *caps, VkPhysicalDeviceMemoryProperties *memory);
void ghci_vk_caps_family(const struct ghci_caps *caps, VkQueueFamilyProperties *family);
uint32_t ghci_vk_caps_sample_counts(const struct ghci_caps *caps);
void ghci_vk_caps_format(const struct ghci_vk_formats *formats, uint32_t format, VkFormatProperties *properties);
uint32_t ghci_vk_caps_image_format(const struct ghci_caps *caps, const struct ghci_vk_formats *formats, uint32_t format, uint32_t type, uint32_t tiling, uint32_t usage, uint32_t flags, VkImageFormatProperties *properties);
int ghci_vk_caps_features_within(const struct ghci_caps *caps, const VkPhysicalDeviceFeatures *asked);
unsigned ghci_vk_caps_shortfalls(const struct ghci_caps *caps, const struct ghci_vk_formats *formats, const char **names, unsigned capacity);

/* vk-format.c: the formats the hardware takes, and their features. */
int ghci_vk_format_size(uint32_t format, uint32_t *bytes, uint32_t *block);
void ghci_vk_formats_table(const struct ghci_caps *caps, struct ghci_vk_formats *formats);
void ghci_vk_formats_from_bits(const struct ghci_caps *caps, uint32_t format, uint32_t optimal, uint32_t linear, uint32_t buffer, struct ghci_vk_formats *formats);
void ghci_vk_formats_read(struct ghci_device *device, struct ghci_vk_formats *formats);

/* The routes of the command groups. */
int ghci_vk_device_route(struct ghci_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply, int *handled);
int ghci_vk_memory_route(struct ghci_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply, int *handled);
int ghci_vk_command_route(struct ghci_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply, int *handled);
int ghci_vk_queue_route(struct ghci_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply, int *handled);

/* vk-memory.c: memory, buffers, images and the executor heap. */
struct ghci_allocation *ghci_vk_memory_blob(struct ghci_device *device, struct ghci_session *owner, uint64_t blob_id);
int ghci_vk_heap_take(struct ghci_vk_session *session, struct ghci_vk_device *device, uint32_t bytes, uint32_t alignment, struct ghci_vk_heap_block **block, uint32_t *offset);
void ghci_vk_heap_give(struct ghci_vk_heap_block *block, uint32_t offset);
void ghci_vk_heap_fini(struct ghci_vk_session *session, struct ghci_vk_device *device);
void ghci_vk_memory_release(struct ghci_vk_session *session, struct ghci_vk_object *object);
void ghci_vk_image_layout(uint32_t width, uint32_t height, uint32_t depth, uint32_t levels, uint32_t layers, uint32_t samples, uint32_t texel_bytes, uint32_t block, struct ghci_layout_image *layout);

/* vk-command.c: what a command buffer lets go of. */
void ghci_vk_commands_release(struct ghci_vk_object *object);

/* vk-queue.c: the completions, the drain and the stop. */
void ghci_vk_service(struct ghci_device *device);
void ghci_vk_drain(struct ghci_device *device, struct ghci_vk_session *session);
int ghci_vk_frame_queue(struct ghci_vk_session *session, uint32_t timeline, struct drv_gpu_completion *completion);
void ghci_vk_submitted(struct ghci_vk_device *device, const struct ghci_ticket *ticket);
int ghci_vk_session_pending(struct ghci_vk_session *session);
void ghci_vk_session_fail(struct ghci_vk_session *session);

#endif
