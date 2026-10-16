/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Vulkan executor of the amdrdna GPU device.
 *
 * libvulkan sends the Kei GPU command protocol (uapi/gpu-op.h) to a GPU
 * node; the executor decodes it in the kernel and does the work on the
 * GPU.  The wire's readers and writers and the generated record codec are
 * the i915 executor's, used unchanged (as the bcm2711 and nvrtx executors
 * use them); the decoding of the commands is the nvrtx executor's, and the
 * work on the GPU (the command packets, the descriptors, the programs of
 * the amdrdna compiler) is the amdrdna driver's own.
 *
 * One executor session belongs to one open of the node.  It holds the
 * session's typed objects by their wire identity (an index of hashed
 * identities over a list), and the scratch one command decodes its records
 * into.  The node's lock serializes everything here.
 */

#ifndef DRIVERS_GPU_AMDRDNA_VK_AMDRDNA_VK_H
#define DRIVERS_GPU_AMDRDNA_VK_AMDRDNA_VK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/gpu/i915/render/codec.h"

/* Names a parameter a function does not use (as the driver's other files do). */
#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(parameter) ((void)(parameter))
#endif

/* The buckets of a session's identity index (a power of two). */
#define AMDRDNA_VK_OBJECT_BUCKETS		1024U

/* The most live objects one session holds. */
#define AMDRDNA_VK_OBJECT_LIMIT		4096U

/* The scratch one command decodes its records into. */
#define AMDRDNA_VK_ARENA_BYTES		(256U * 1024U)

struct amdrdna_vk_object;
struct amdrdna_vk_execute;

/*
 * One executor session.
 *
 * owner is the node's session the executor reads blobs through.  objects
 * lists every published identity, newest first, and buckets indexes them
 * by hashed identity; object_count counts the published ones, live_objects
 * the ones whose payload has not been destroyed yet (an unpublished object
 * lives on while something still holds it).  closing is set once the
 * session's close has begun: nothing is published after it.  last_opcode
 * and failed_opcode name the commands the decoder last took and refused.
 */
struct amdrdna_vk_session {
	void *owner;
	struct amdrdna_vk_object *objects;
	struct amdrdna_vk_object *buckets[AMDRDNA_VK_OBJECT_BUCKETS];
	uint32_t object_count;
	uint32_t live_objects;
	bool closing;
	uint32_t last_opcode;
	uint32_t failed_opcode;
	struct i915_wire_arena arena;
	/* The timelines the session's queues complete jobs on (bit n: timeline n), set by vkGetDeviceQueue2. */
	uint64_t timelines;
	/* The run its submissions are written into and the executor's programs (vk-execute.c), made at the first one. */
	struct amdrdna_vk_execute *execute;
};

/*
 * One published wire identity of a kind, and the payload it stands for.
 *
 * next and link chain it into the session's list (link is the pointer that
 * points at it, so it unlinks at once), bucket_next into its bucket.
 * references counts the holders: the registry while published, and every
 * dependent object or submitted work that retained it; the payload is
 * destroyed when the count reaches zero.
 */
struct amdrdna_vk_object {
	struct amdrdna_vk_object *next;
	struct amdrdna_vk_object **link;
	struct amdrdna_vk_object *bucket_next;
	struct amdrdna_vk_session *session;
	enum i915_vk_object_kind kind;
	uint64_t identity;
	uint32_t references;
	bool published;
	void *payload;
	int (*destroy)(struct amdrdna_vk_session *session, void *payload);
};

/* A command's handler: takes the command's body from reader and writes its reply; sets *handled when the opcode is its own. */
typedef int (*amdrdna_vk_route)(struct amdrdna_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply, int *handled);

/* Runs one decoded command (the transport's opcodes excepted). */
typedef int (*amdrdna_vk_dispatcher)(struct amdrdna_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply);

int amdrdna_vk_session_open(void *owner, struct amdrdna_vk_session **session);
int amdrdna_vk_session_close(struct amdrdna_vk_session **session);
void amdrdna_vk_execute_close(struct amdrdna_vk_session *session);
void *amdrdna_vk_arena_take(struct amdrdna_vk_session *session, size_t bytes);

int amdrdna_vk_object_publish(struct amdrdna_vk_session *session, enum i915_vk_object_kind kind, uint64_t identity, void *payload, int (*destroy)(struct amdrdna_vk_session *, void *), struct amdrdna_vk_object **object);
struct amdrdna_vk_object *amdrdna_vk_object_find(struct amdrdna_vk_session *session, enum i915_vk_object_kind kind, uint64_t identity);
int amdrdna_vk_object_retain(struct amdrdna_vk_object *object);
int amdrdna_vk_object_release(struct amdrdna_vk_object *object);
int amdrdna_vk_object_remove(struct amdrdna_vk_session *session, enum i915_vk_object_kind kind, uint64_t identity);
int amdrdna_vk_objects_close(struct amdrdna_vk_session *session);

int amdrdna_vk_device_route(struct amdrdna_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply, int *handled);

int amdrdna_vk_stream_execute(struct amdrdna_vk_session *session, const void *wire, uint32_t bytes, amdrdna_vk_dispatcher dispatch);
int amdrdna_vk_dispatch(struct amdrdna_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply);

/*
 * Attaches the storage of a blob created for a VkDeviceMemory (the blob's
 * blob_id is the memory's identity): the CPU's view, the GPU's address and
 * the node's backing, which the memory holds until it is destroyed (vk-memory.c).
 */
int amdrdna_vk_memory_attach(struct amdrdna_vk_session *session, uint64_t identity, uint64_t bytes, uint8_t *cpu, uint64_t va, void *backing);

/*
 * What the executor's own check of its draws on the GPU found (stage A9,
 * vk-execute.c): the pixels of each of its two images, how many of the
 * drawn image's and of its copy's are as expected, the quads drawn, the
 * words of the indirect buffer that drew them, and the first wrong pixel
 * (its index over both images, pixels * 2 when none) and its value.
 */
struct amdrdna_vk_check {
	uint32_t pixels;
	uint32_t drawn;
	uint32_t copied;
	uint32_t quads;
	uint32_t words;
	uint32_t bad_at;
	uint32_t bad_value;
};

int amdrdna_vk_execute_check(struct amdrdna_vk_session *session, struct amdrdna_vk_check *check);

/*
 * Finds a blob of the owner's (the node's session) by its resource id and
 * gives its CPU view: the first byte and the length.  Provided by the
 * node (node.c); the transport reads command streams from blobs and writes
 * replies into them.
 */
int amdrdna_vk_blob(void *owner, uint32_t resource_id, uint8_t **cpu, uint64_t *bytes);

/* The owner's GPU: its PCI product, and the bytes of VRAM the CPU's window gives the executor's memory. */
uint32_t amdrdna_vk_device_id(void *owner);
uint64_t amdrdna_vk_heap_bytes(void *owner);

/*
 * Gives back the storage of a VkDeviceMemory once nothing holds it: the
 * GPU's mapping of the run at va and the reference to the blob's backing
 * the memory took when the blob was attached.  Provided by the node; the
 * caller holds the node's lock.
 */
void amdrdna_vk_memory_detach(void *owner, void *backing, uint64_t va, uint64_t bytes);

/*
 * Takes a blob of the owner's as the storage of an imported VkDeviceMemory
 * of bytes (provided by the node): maps it for the GPU, holds its backing,
 * and gives the CPU's view, the GPU's address and the backing, which
 * amdrdna_vk_memory_detach() gives back.  The caller holds the node's lock.
 */
int amdrdna_vk_memory_import(void *owner, uint32_t resource_id, uint64_t bytes, uint8_t **cpu, uint64_t *va, void **backing);

/*
 * The executor's own GPU memory (programs, command buffers, resource
 * tables and descriptors), provided by the node: a run of bytes the CPU
 * writes at *cpu and the GPU reads at *va in the session's address space,
 * and its release.  The token names the run to the node.  The node makes
 * what the CPU wrote visible to the GPU before each run.
 */
int amdrdna_vk_gpu_alloc(void *owner, uint64_t bytes, void **token, uint8_t **cpu, uint64_t *va);
void amdrdna_vk_gpu_free(void *owner, void *token);

/*
 * Runs an indirect buffer of command packets of bytes at va on the
 * graphics queue in the session's address space, and waits until the GPU
 * has done it (provided by the node: the queue, the address space's
 * identifier, the fence and its wait are the node's).  Returns 0, or EIO
 * when the work did not finish.
 */
int amdrdna_vk_gpu_run(void *owner, uint64_t va, uint32_t bytes);

/*
 * Counts a transfer the CPU did through the window instead of the GPU, for
 * the node's counts (provided by the node).
 */
void amdrdna_vk_gpu_copied(void *owner, uint64_t bytes);

/*
 * Counts the draws of a command buffer about to run: the application's
 * draws and the executor's own quads, for the node's counts (provided by
 * the node).
 */
void amdrdna_vk_gpu_drawn(void *owner, uint32_t draws, uint32_t quads);

/*
 * Copies a command stream of length bytes out of a blob's bytes in the
 * CPU's window, counting the copy and its time (provided by the node).
 */
void amdrdna_vk_blob_read(void *owner, uint8_t *to, const uint8_t *from, size_t length);

#endif
