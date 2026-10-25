/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Vulkan executor of a GHCI device: a checking relay.
 *
 * libvulkan sends the Kei GPU command protocol (uapi/gpu-op.h) to the
 * node.  The GPU's command processor firmware owns the meaning of the
 * Vulkan objects; the executor decodes every command, checks its wire
 * against the shape libvulkan encodes, and sorts it into one of four:
 *
 *  - D: the driver answers it (the instance, the physical device and its
 *    limits, memory requirements and layouts, shader modules, fences);
 *  - R: it is relayed to the firmware as it came;
 *  - R': the driver rewrites it into a GHCI gpu-op of its own (an
 *    allocation becomes GHCI_MEMORY, each stage of a pipeline is compiled
 *    and handed over as GHCI_PROGRAM before the create is relayed);
 *  - X: it is refused, which ends the frame.
 *
 * The relayed commands of one frame are staged in GPU memory as records
 * [number][flags][result index][arguments] and the ring carries a single
 * GHCI_FRAME packet that points at them.  The replies are all the
 * driver's: a D reply is written while the frame is decoded, a relayed
 * command's reply is laid down at once with its VkResult word left to be
 * filled from the firmware's result record, and the trailer libvulkan
 * polls is stored last, once every result is in.  That happens on the
 * device's service thread when the firmware raises RESULT; the submitting
 * ioctl never waits for the GPU.
 *
 * Locks.  Everything here runs under the device's lock (a mutex), which
 * serializes the decode of every session's frames, the staging ring, the
 * list of frames in flight and the object tables.  The job markers are
 * under the same lock.
 */

#ifndef DRIVERS_GPU_GHCI_VK_VK_H
#define DRIVERS_GPU_GHCI_VK_VK_H

#include <stddef.h>
#include <stdint.h>

#include "drivers/gpu/i915/render/codec.h"

#include "../ghci.h"
#include "../layout.h"
#include "vk-node.h"

struct ghci_vk_session;
struct ghci_vk_frame;

/* The buckets a session's objects are found by. */
#define GHCI_VK_BUCKETS			256U

/* The most objects the driver keeps for one session. */
#define GHCI_VK_OBJECTS_MAX		65536U

/* The scratch one command's records are decoded into. */
#define GHCI_VK_ARENA_BYTES		(256U * 1024U)

/* The staging ring of the device, which holds the frames in flight and their result pages. */
#define GHCI_VK_STAGING_BYTES		(4U << 20)

/* The most bytes one frame stages, and the most records it carries. */
#define GHCI_VK_FRAME_BYTES_MAX		(2U << 20)
#define GHCI_VK_FRAME_RECORDS_MAX	16384U

/* The heap the device lends the firmware for its objects and recordings. */
#define GHCI_VK_HEAP_BYTES		(16U << 20)

/* How deep command streams in blobs nest, and how much of them one frame copies. */
#define GHCI_VK_STREAM_DEPTH		4U
#define GHCI_VK_STREAM_COPY_BYTES	(16U << 20)

/* The shader stages of one pipeline the device runs: vertex and fragment, or compute. */
#define GHCI_VK_STAGES			2U

/* The attachments and subpasses of a render pass the driver keeps for the compiler. */
#define GHCI_VK_PASS_ATTACHMENTS	16U
#define GHCI_VK_PASS_SUBPASSES		8U

/* The timelines a queue may complete jobs on (1 to 63; 0 is the context's own). */
#define GHCI_VK_TIMELINES		64U

/* The memory types: device local only, and device local, host visible and coherent. */
#define GHCI_VK_MEMORY_DEVICE		0U
#define GHCI_VK_MEMORY_HOST		1U
#define GHCI_VK_MEMORY_TYPES		2U

/* The VkFormat values of Vulkan 1.0's core formats, 0 to 184. */
#define GHCI_VK_FORMATS			185U

/* The VkResult word of a staged record the firmware has not written yet. */
#define GHCI_VK_RESULT_PENDING		0x7fffffffU

/*
 * The kinds of objects the driver keeps.  Every other object (views,
 * layouts, pools, sets, framebuffers, command buffers, semaphores) is the
 * firmware's alone and is relayed without a record here.
 */
enum ghci_vk_kind {
	GHCI_VK_DEVICE = 1,
	GHCI_VK_MEMORY,
	GHCI_VK_BUFFER,
	GHCI_VK_IMAGE,
	GHCI_VK_SHADER,
	GHCI_VK_PIPELINE,
	GHCI_VK_FENCE,
	GHCI_VK_EVENT,
	GHCI_VK_QUERY_POOL,
	GHCI_VK_RENDER_PASS
};

/*
 * What a patch of a reply does with a record's result once the firmware
 * wrote it: store the VkResult in the reply, or remember the GPU address
 * the firmware gave an event or a query pool (its aux word), or copy a
 * query pool's results once the work before them has finished.
 */
enum ghci_vk_patch_kind {
	GHCI_VK_PATCH_RESULT = 1,
	GHCI_VK_PATCH_EVENT,
	GHCI_VK_PATCH_QUERY_POOL,
	GHCI_VK_PATCH_QUERY_RESULTS
};

/* A VkDevice: whether it was made with robustBufferAccess, which changes how shaders are compiled. */
struct ghci_vk_device {
	int robust;
};

/*
 * A VkDeviceMemory: the allocation that holds it, its type and the bytes
 * libvulkan asked for.  The allocation is on the session's list, so the
 * session's close gives it back even when the memory was never freed.
 * An imported memory object is another session's memory, which the object
 * holds (imported) instead of owning it.
 */
struct ghci_vk_memory {
	struct ghci_allocation *allocation;
	uint64_t bytes;
	uint32_t type;
	int imported;
};

/* A VkBuffer: its size and usage, which decide its memory requirements. */
struct ghci_vk_buffer {
	uint64_t size;
	uint32_t usage;
};

/*
 * A VkImage: what its layout (layout.h) and its memory requirements are
 * computed from.
 */
struct ghci_vk_image {
	uint32_t format;
	uint32_t type;
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	uint32_t levels;
	uint32_t layers;
	uint32_t samples;
	uint32_t tiling;
	uint32_t usage;
};

/* A VkShaderModule: its SPIR-V words, compiled when a pipeline is made of it. */
struct ghci_vk_shader {
	uint32_t *words;
	uint32_t word_count;
};

/*
 * A VkPipeline: the GPU memory of its programs, one per stage, given
 * back after the session's last fence when the pipeline is destroyed.
 */
struct ghci_vk_pipeline {
	struct ghci_allocation *programs[GHCI_VK_STAGES];
	unsigned program_count;
};

/*
 * A VkFence: signaled says it was made or last left signaled by the host;
 * pending says a submission signals it with ticket, the device fence that
 * follows that submission.
 */
struct ghci_vk_fence {
	int signaled;
	int pending;
	struct ghci_ticket ticket;
};

/*
 * A VkEvent or a VkQueryPool: the GPU address of its words in the
 * firmware's heap, which the firmware reports when it makes the object
 * (known says that report came), and a pool's query count.
 */
struct ghci_vk_heap_object {
	uint32_t address;
	uint32_t count;
	int known;
};

/*
 * A VkRenderPass, as far as the compiler needs it: the format and the
 * samples of each attachment, and each subpass's input attachments by
 * their index (VK_ATTACHMENT_UNUSED for none).
 */
struct ghci_vk_render_pass {
	uint32_t attachment_count;
	uint32_t formats[GHCI_VK_PASS_ATTACHMENTS];
	uint32_t samples[GHCI_VK_PASS_ATTACHMENTS];
	uint32_t subpass_count;
	uint32_t input_count[GHCI_VK_PASS_SUBPASSES];
	uint32_t inputs[GHCI_VK_PASS_SUBPASSES][GHCI_GSI1_INPUT_ATTACHMENTS];
};

/*
 * One object the driver keeps, found by its kind and wire identity in
 * the session's buckets.  It lives from the command that made it to the
 * one that destroyed it, or to the session's close.
 */
struct ghci_vk_object {
	struct ghci_vk_object *next;
	enum ghci_vk_kind kind;
	uint64_t identity;
	union {
		struct ghci_vk_device device;
		struct ghci_vk_memory memory;
		struct ghci_vk_buffer buffer;
		struct ghci_vk_image image;
		struct ghci_vk_shader shader;
		struct ghci_vk_pipeline pipeline;
		struct ghci_vk_fence fence;
		struct ghci_vk_heap_object heap;
		struct ghci_vk_render_pass pass;
	} as;
};

/*
 * One place in a reply that waits: the staged record whose result it
 * takes, what the result is used for, the reply offset of a VkResult
 * word (or of the results a query copy writes), and the object an
 * address belongs to.  A query copy also keeps its first query, its
 * count, its stride, its flags and its bytes.
 */
struct ghci_vk_patch {
	enum ghci_vk_patch_kind kind;
	uint32_t index;
	uint32_t offset;
	uint64_t identity;
	uint32_t first;
	uint32_t count;
	uint32_t stride;
	uint32_t flags;
	uint32_t bytes;
};

/*
 * One frame being decoded.
 *
 * reply is the cursor in the reply region the frame selected, in the
 * blob reply_blob; trailer_offset is where the trailer goes once it was
 * asked for.  stage collects the records to relay (their bytes and their
 * count), patches what the results fill in, releases the allocations to
 * give back once the frame is in the ring, and fences the VkFences its
 * submissions signal.  copy_budget and depth bound the streams in blobs.
 * wait_fence says a reply waits for the session's work to finish.
 */
struct ghci_vk_build {
	struct ghci_vk_session *session;
	struct i915_wire_writer reply;
	struct ghci_allocation *reply_blob;
	int trailer;
	uint32_t trailer_offset;
	uint8_t *stage;
	uint32_t stage_bytes;
	uint32_t stage_capacity;
	uint32_t records;
	struct ghci_vk_patch *patches;
	unsigned patch_count;
	unsigned patch_capacity;
	struct ghci_allocation **releases;
	unsigned release_count;
	unsigned release_capacity;
	uint64_t *fences;
	unsigned fence_count;
	unsigned fence_capacity;
	uint64_t copy_budget;
	unsigned depth;
	int wait_fence;
};

/*
 * One frame in flight, from its submission to its completion.
 *
 * The frames of every session are on the device's list in ring order.  A
 * frame with records waits for the firmware's frame result at page +
 * 8 x records; one without (only driver answers, while frames are ahead
 * of it) waits for its turn.  wait_fence makes it also wait for the
 * session's last fence (an empty frame of a context fence).  reply_blob
 * is held until the frame completes.  staging_bytes is what the frame
 * uses up of the staging ring, given back when it retires.
 */
struct ghci_vk_frame {
	struct ghci_vk_frame *next;
	struct ghci_vk_session *session;
	struct drv_gpu_completion *completion;
	struct ghci_allocation *reply_blob;
	int trailer;
	uint32_t trailer_offset;
	uint32_t records;
	volatile uint32_t *page;
	struct ghci_vk_patch *patches;
	unsigned patch_count;
	int wait_fence;
	struct ghci_ticket ticket;
	uint32_t staging_bytes;
};

/*
 * The executor's part of a device: the firmware's heap, the page the
 * results of the driver's control packets go to, the staging ring and
 * the frames in flight.
 *
 * The staging ring holds each frame's result page and records from head
 * (where the next frame goes) on; used counts the bytes the frames in
 * flight take, with the end of the ring a frame skipped, and a frame
 * never wraps.  frames and frames_last are the list in ring order.
 * heap_ready says the firmware took the heap since its last start, and
 * warps is what the GPU says it runs at once.  faulted says the core
 * asked the device to take no new work.  The format table is read from
 * the GPU once at the start (fmt_present says it has one), with the
 * colour attachments its render output takes.  The device's lock
 * protects all.
 */
struct ghci_vk_engine {
	struct ghci_allocation *heap;
	struct ghci_allocation *control;
	struct ghci_allocation *staging;
	uint32_t head;
	uint32_t used;
	struct ghci_vk_frame *frames;
	struct ghci_vk_frame *frames_last;
	int heap_ready;
	uint32_t warps;
	int faulted;
	int fmt_present;
	uint32_t color_attachments;
	uint32_t fmt_optimal[GHCI_VK_FORMATS];
	uint32_t fmt_linear[GHCI_VK_FORMATS];
	uint32_t fmt_buffer[GHCI_VK_FORMATS];
};

/*
 * One open's executor session.
 *
 * owner is the device's session (its job tag names the firmware's
 * session).  opened says the firmware took SESSION_OPEN; lost says the
 * firmware's objects of the session are gone (a reset) and every later
 * frame fails.  pending counts its frames in flight.  timelines has bit n
 * once a queue completes jobs on timeline n.  jobs are the session's job
 * markers.  The device's lock protects every field.
 */
struct ghci_vk_session {
	struct ghci_device *device;
	struct ghci_session *owner;
	struct ghci_vk_object *buckets[GHCI_VK_BUCKETS];
	unsigned object_count;
	int opened;
	int lost;
	unsigned pending;
	uint64_t timelines;
	struct ghci_job_table jobs;
	struct i915_wire_arena arena;
	uint32_t last_opcode;
};

/* A command's handler: sets *handled when the number is its own. */
typedef int (*ghci_vk_route)(struct ghci_vk_build *build, uint32_t opcode, uint32_t flags, struct i915_wire_reader *reader, int *handled);

/*
 * The sessions (vk-session.c).  The device's lock is held.
 */

/* Finds an object of a kind by its identity, or NULL. */
struct ghci_vk_object *ghci_vk_object_find(struct ghci_vk_session *session, enum ghci_vk_kind kind, uint64_t identity);

/* Makes and lists an object (zeroed payload).  Returns 0, EEXIST, ENOSPC or ENOMEM. */
int ghci_vk_object_add(struct ghci_vk_session *session, enum ghci_vk_kind kind, uint64_t identity, struct ghci_vk_object **result);

/* Unlists an object and frees its record (not what it holds). */
void ghci_vk_object_remove(struct ghci_vk_session *session, struct ghci_vk_object *object);

/* Takes cleared scratch for the current command, or NULL when the arena is used up. */
void *ghci_vk_arena_take(struct ghci_vk_session *session, size_t bytes);

/*
 * The frame (vk-stream.c).  The device's lock is held.
 */

/* Decodes, stages and submits one frame of commands of a session. */
int ghci_vk_submit(struct ghci_device *device, struct ghci_session *owner, const void *wire, uint32_t bytes, uint32_t flags, struct drv_gpu_completion *completion);

/* Waits, a bounded time, for every frame of a session to complete. */
void ghci_vk_drain(struct ghci_device *device, struct ghci_session *owner);

/* Appends a record to the frame's stage and reports its result index. */
int ghci_vk_stage(struct ghci_vk_build *build, uint32_t opcode, uint32_t flags, const void *arguments, uint32_t bytes, uint32_t *index);

/* Relays the span of the reader from start as one record. */
int ghci_vk_relay(struct ghci_vk_build *build, uint32_t opcode, uint32_t flags, struct i915_wire_reader *reader, size_t start, uint32_t *index);

/* Writes a VkResult word to the reply that the record's result fills in later. */
int ghci_vk_reply_result(struct ghci_vk_build *build, uint32_t index);

/* Asks for a record's aux word to be kept as the GPU address of an event or a query pool. */
int ghci_vk_patch_address(struct ghci_vk_build *build, enum ghci_vk_patch_kind kind, uint32_t index, uint64_t identity);

/* Adds a patch to the frame and reports it, cleared, for its caller to fill. */
int ghci_vk_patch_add(struct ghci_vk_build *build, struct ghci_vk_patch **patch);

/* Writes a query copy's results into a reply from the pool's words in the firmware's heap. */
void ghci_vk_query_copy(struct ghci_device *device, struct ghci_vk_session *session, const struct ghci_vk_patch *patch, uint8_t *view);

/* Gives an allocation back once the frame is in the ring (after its fence). */
int ghci_vk_release_later(struct ghci_vk_build *build, struct ghci_allocation *allocation);

/* Binds a VkFence to the fence that follows the frame. */
int ghci_vk_fence_later(struct ghci_vk_build *build, uint64_t identity);

/*
 * The handlers of each group of commands.
 */

/* The instance, the physical device, the device and the queue (vk-device.c). */
int ghci_vk_device_route(struct ghci_vk_build *build, uint32_t opcode, uint32_t flags, struct i915_wire_reader *reader, int *handled);

/* Memory, buffers, images, their requirements and layouts, fences, events and queries (vk-memory.c). */
int ghci_vk_memory_route(struct ghci_vk_build *build, uint32_t opcode, uint32_t flags, struct i915_wire_reader *reader, int *handled);

/* Shader modules, pipeline caches and pipelines (vk-program.c). */
int ghci_vk_program_route(struct ghci_vk_build *build, uint32_t opcode, uint32_t flags, struct i915_wire_reader *reader, int *handled);

/* The other objects, the command buffers, the recorded commands and the queue (vk-relay.c). */
int ghci_vk_relay_route(struct ghci_vk_build *build, uint32_t opcode, uint32_t flags, struct i915_wire_reader *reader, int *handled);

/*
 * The parts the handlers share.
 */

/* Reads the tail of the create form, [u64 0][u64 1][identity]: the identity, or 0 with the reader's error. */
uint64_t ghci_vk_create_tail(struct i915_wire_reader *reader);

/* Reads the destroy form, [device][identity][u64 0]: the identity. */
uint64_t ghci_vk_destroy_form(struct i915_wire_reader *reader);

/* Reports the bytes of a texel (or of a 4x4 block) of a VkFormat, 0 for a format the device does not have. */
uint32_t ghci_vk_format_bytes(uint32_t format, uint32_t *block);

/* Reports the VkFormatFeatureFlags of a format for optimal and linear tiling and for buffers. */
void ghci_vk_format_features(struct ghci_device *device, uint32_t format, uint32_t *optimal, uint32_t *linear, uint32_t *buffer);

/* Reports the VkResult of an executor errno. */
uint32_t ghci_vk_result(int error);

#endif /* DRIVERS_GPU_GHCI_VK_VK_H */
