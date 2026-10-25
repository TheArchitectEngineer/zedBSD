/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's sessions: one per open of the node, named to the
 * firmware by the open's job tag, and the objects the driver keeps for
 * each.
 *
 * The open sends GHCI_SESSION_OPEN in the ring and waits for the
 * firmware's answer; the close sends GHCI_SESSION_CLOSE, which gives the
 * firmware's objects of the tag back.  The driver's records of the
 * session's objects go with the close; the GPU memory they name is on the
 * session's list of allocations and is given back by the device's close.
 *
 * Session protection (docs/ghci.md section 11.6): on a GPU with the PROT
 * capability the sessions of other users than root are opened PROTECTED.
 * Their memory that no vkAllocateMemory names (the resources the core
 * makes, the programs' memory) is given to the firmware as the session's
 * own with a GHCI_MEMORY of a synthetic identity (GHCI_VK_PROT_IDENTITY
 * and the address), and an import of another session's memory is a
 * BUFFER_GRANT, revoked when the import goes.
 */

#include <stddef.h>
#include <stdint.h>

#include <kern/cred.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <uapi/errno.h>

#include "../frame.h"
#include "vk.h"
#include "vk-engine.h"

/* The flags of GHCI_MEMORY the synthetic identities use: read-only (a program). */
#define MEMORY_READ_ONLY		0x4U

/* VK_ERROR_OUT_OF_DEVICE_MEMORY: the firmware has no protected slot left. */
#define RESULT_OUT_OF_DEVICE_MEMORY	(-2)

static int prot_alive(const struct ghci_session *owner);
static uint32_t bucket_of(enum ghci_vk_kind kind, uint64_t identity);
static void object_free(struct ghci_vk_session *session, struct ghci_vk_object *object);

/*
 * Makes a session's executor and opens the firmware's session of its tag.
 */
int
ghci_vk_session_open(
	struct ghci_device *device,
	struct ghci_session *owner)
{
	struct ghci_vk_session *session;
	const struct ucred *credentials;
	uint32_t arguments[4];
	uint32_t flags;
	int32_t status;
	int error;

	/* Other users than root are protected from each other where the GPU can (root is not: the tools). */
	flags = 0U;
	credentials = cred_current();
	if (device->protection && !cred_is_superuser(credentials))
		flags = GHCI_SESSION_PROTECTED;

	/* The executor's record. */
	session = kern_calloc(1U, sizeof(*session));
	if (session == NULL)
		return ENOMEM;

	/* The scratch the records of one command are decoded into. */
	session->arena.base = kern_malloc(GHCI_VK_ARENA_BYTES);
	if (session->arena.base == NULL) {
		kern_free(session);
		return ENOMEM;
	}

	/* The record starts with no object, no frame and every job marker free. */
	session->arena.size = GHCI_VK_ARENA_BYTES;
	session->arena.used = 0U;
	session->device = device;
	session->owner = owner;
	ghci_jobs_init(&session->jobs);

	/* SESSION_OPEN [u32 tag][u32 flags][u64 heap limit]: the limit is the heap's whole size. */
	arguments[0] = owner->tag;
	arguments[1] = flags;
	arguments[2] = GHCI_VK_HEAP_BYTES;
	arguments[3] = 0U;
	error = ghci_vk_engine_control(device, owner, GPU_OP_GHCI_SESSION_OPEN, arguments, 4U, &status);
	if (error == 0 && status != 0) {
		kern_logf("ghci: %s: the firmware refused session %u (%d)\n", device->name, (unsigned)owner->tag, (int)status);
		error = EIO;

		/* No protected slot left: the open is refused, never made unprotected. */
		if (flags != 0U && status == RESULT_OUT_OF_DEVICE_MEMORY)
			error = ENOSPC;
	}

	/* A session the firmware did not open leaves nothing behind. */
	if (error != 0) {
		kern_free(session->arena.base);
		kern_free(session);
		return error;
	}

	/* Succeeded: the open has its executor. */
	session->opened = 1;
	owner->vk = session;
	owner->protected = flags != 0U;
	return 0;
}

/*
 * Gives a protected session's allocation to the firmware as the session's
 * own memory (read-only for a program), so that its work reaches it.
 * Returns 0 (also for a session that is not protected), or EIO when the
 * firmware refused it.  The device's lock is held.
 */
int
ghci_vk_prot_own(
	struct ghci_device *device,
	struct ghci_session *owner,
	struct ghci_allocation *allocation,
	int read_only)
{
	uint64_t identity;
	uint32_t arguments[7];
	int32_t status;
	int error;

	/* Only a protected session's work is checked. */
	if (!owner->protected || !prot_alive(owner))
		return 0;

	/* GHCI_MEMORY [u64 memory][u64 address][u64 bytes][u32 flags] of the synthetic identity. */
	identity = GHCI_VK_PROT_IDENTITY | allocation->gpu_address;
	arguments[0] = (uint32_t)identity;
	arguments[1] = (uint32_t)(identity >> 32);
	arguments[2] = allocation->gpu_address;
	arguments[3] = 0U;
	arguments[4] = allocation->bytes;
	arguments[5] = 0U;
	arguments[6] = read_only ? MEMORY_READ_ONLY : 0U;
	error = ghci_vk_engine_control(device, owner, GPU_OP_GHCI_MEMORY, arguments, 7U, &status);
	if (error == 0 && status != 0) {
		kern_logf("ghci: %s: session %u: memory at %08x not protected (%d)\n", device->name, (unsigned)owner->tag,
		    (unsigned)allocation->gpu_address, (int)status);
		error = EIO;
	}
	if (error != 0)
		return error;

	/* Succeeded: its release gives it back. */
	allocation->prot_owned = 1;
	return 0;
}

/*
 * Grants a protected session an import of another session's memory
 * (BUFFER_GRANT, read and write: an imported external memory is the
 * importer's to use).  Returns 0 (also when no grant is needed), ENOSPC
 * when the GPU has no grant entry left, or EIO.  The device's lock is held.
 */
int
ghci_vk_prot_grant(
	struct ghci_device *device,
	struct ghci_session *owner,
	struct ghci_allocation *alias)
{
	uint32_t arguments[6];
	int32_t status;
	int error;

	/* Only a protected session needs a grant, and not for its own memory. */
	if (!owner->protected || !prot_alive(owner))
		return 0;
	if (alias->backing != NULL && alias->backing->session == owner)
		return 0;

	/* BUFFER_GRANT [u32 session][u32 access][u64 address][u64 bytes], in the ring before the importer's frames. */
	arguments[0] = owner->tag;
	arguments[1] = GHCI_GRANT_READ | GHCI_GRANT_WRITE;
	arguments[2] = alias->gpu_address;
	arguments[3] = 0U;
	arguments[4] = alias->bytes;
	arguments[5] = 0U;
	error = ghci_vk_engine_control(device, NULL, GPU_OP_GHCI_BUFFER_GRANT, arguments, 6U, &status);
	if (error != 0)
		return error;
	if (status == RESULT_OUT_OF_DEVICE_MEMORY)
		return ENOSPC;
	if (status != 0)
		return EIO;

	/* Succeeded: the release revokes it. */
	alias->prot_granted = 1;
	return 0;
}

/*
 * Takes back what session protection gave an allocation of a session
 * that goes: its synthetic GHCI_MEMORY (FREE_MEMORY) or its grant
 * (BUFFER_REVOKE), in the ring after the session's work that used it.  A
 * session the firmware closed or lost has nothing left there.  The
 * device's lock is held.
 */
void
ghci_vk_prot_release(
	struct ghci_device *device,
	struct ghci_session *owner,
	struct ghci_allocation *allocation)
{
	uint64_t identity;
	uint32_t arguments[6];
	int error;

	/* Nothing given, or nothing left to take back. */
	if (!allocation->prot_owned && !allocation->prot_granted)
		return;
	if (owner == NULL || !prot_alive(owner)) {
		allocation->prot_owned = 0;
		allocation->prot_granted = 0;
		return;
	}

	/* FREE_MEMORY [u64 device][u64 memory][u64 0] of the synthetic identity (no drain: nothing uses it). */
	error = 0;
	if (allocation->prot_owned) {
		identity = GHCI_VK_PROT_IDENTITY | allocation->gpu_address;
		arguments[0] = 0U;
		arguments[1] = 0U;
		arguments[2] = (uint32_t)identity;
		arguments[3] = (uint32_t)(identity >> 32);
		arguments[4] = 0U;
		arguments[5] = 0U;
		error = ghci_vk_engine_post(device, owner, GPU_OP_FREE_MEMORY, arguments, 6U);
		allocation->prot_owned = 0;
	}

	/* BUFFER_REVOKE [u32 session][u64 address][u64 bytes]: after the work before it. */
	if (allocation->prot_granted) {
		arguments[0] = owner->tag;
		arguments[1] = allocation->gpu_address;
		arguments[2] = 0U;
		arguments[3] = allocation->bytes;
		arguments[4] = 0U;
		error = ghci_vk_engine_post(device, NULL, GPU_OP_GHCI_BUFFER_REVOKE, arguments, 5U);
		allocation->prot_granted = 0;
	}

	/* A packet that did not reach the ring leaves the session's close to take it back. */
	if (error != 0)
		kern_logf("ghci: %s: session %u: protection of %08x not taken back (%d)\n", device->name,
		    (unsigned)owner->tag, (unsigned)allocation->gpu_address, error);
}

/*
 * Closes the firmware's session and forgets every object.
 */
void
ghci_vk_session_close(
	struct ghci_device *device,
	struct ghci_session *owner)
{
	struct ghci_vk_session *session;
	struct ghci_vk_object *object;
	uint32_t arguments[1];
	uint32_t bucket;
	int32_t status;
	int error;

	/* An open without an executor has nothing here. */
	session = owner->vk;
	if (session == NULL)
		return;

	/* The firmware gives the tag's objects back; one that lost them in a reset has none. */
	if (session->opened && !session->lost) {
		arguments[0] = owner->tag;
		error = ghci_vk_engine_control(device, owner, GPU_OP_GHCI_SESSION_CLOSE, arguments, 1U, &status);
		if (error != 0)
			kern_logf("ghci: %s: session %u not closed by the firmware (%d)\n", device->name, (unsigned)owner->tag, error);
	}

	/* Forgets every object the driver kept for the session. */
	for (bucket = 0U; bucket < GHCI_VK_BUCKETS; bucket++) {
		while (session->buckets[bucket] != NULL) {
			object = session->buckets[bucket];
			ghci_vk_object_remove(session, object);
		}
	}

	/* The record and its scratch go. */
	owner->vk = NULL;
	kern_free(session->arena.base);
	kern_free(session);
}

/*
 * Finds an object of a kind by its identity.
 */
struct ghci_vk_object *
ghci_vk_object_find(
	struct ghci_vk_session *session,
	enum ghci_vk_kind kind,
	uint64_t identity)
{
	struct ghci_vk_object *object;
	uint32_t bucket;

	/* The null handle names nothing. */
	if (identity == 0U)
		return NULL;

	/* Walks the bucket of the kind and the identity. */
	bucket = bucket_of(kind, identity);
	for (object = session->buckets[bucket];
	     object != NULL;
	     object = object->next) {
		/* Another kind may have the same identity's bucket. */
		if (object->kind != kind)
			continue;

		/* The object the identity names. */
		if (object->identity == identity)
			return object;
	}

	/* The session has no such object. */
	return NULL;
}

/*
 * Makes and lists an object.
 */
int
ghci_vk_object_add(
	struct ghci_vk_session *session,
	enum ghci_vk_kind kind,
	uint64_t identity,
	struct ghci_vk_object **result)
{
	struct ghci_vk_object *object;
	uint32_t bucket;

	/* The null handle cannot be made, nor an identity the session already has. */
	if (identity == 0U)
		return EINVAL;
	object = ghci_vk_object_find(session, kind, identity);
	if (object != NULL)
		return EEXIST;

	/* Refuses a session that keeps as many objects as it may. */
	if (session->object_count >= GHCI_VK_OBJECTS_MAX)
		return ENOSPC;

	/* The record, with a zeroed payload. */
	object = kern_calloc(1U, sizeof(*object));
	if (object == NULL)
		return ENOMEM;

	/* Lists it at the head of its bucket. */
	object->kind = kind;
	object->identity = identity;
	bucket = bucket_of(kind, identity);
	object->next = session->buckets[bucket];
	session->buckets[bucket] = object;
	session->object_count++;

	/* Succeeded: the object is the session's. */
	*result = object;
	return 0;
}

/*
 * Unlists an object and frees its record.
 */
void
ghci_vk_object_remove(
	struct ghci_vk_session *session,
	struct ghci_vk_object *object)
{
	struct ghci_vk_object **position;
	uint32_t bucket;

	/* Finds the object in its bucket. */
	bucket = bucket_of(object->kind, object->identity);
	position = &session->buckets[bucket];
	while (*position != NULL && *position != object)
		position = &(*position)->next;

	/* Unlinks it, when it is listed. */
	if (*position == object) {
		*position = object->next;
		session->object_count--;
	}

	/* The record and what only it holds. */
	object_free(session, object);
}

/*
 * Takes cleared scratch for the current command.
 */
void *
ghci_vk_arena_take(
	struct ghci_vk_session *session,
	size_t bytes)
{
	struct i915_wire_arena *arena;
	uint8_t *taken;
	size_t rounded;

	/* Whole 8-byte units, so every piece is aligned for a 64-bit field. */
	arena = &session->arena;
	rounded = (bytes + 7U) & ~(size_t)7U;
	if (rounded < bytes)
		return NULL;

	/* Refuses a piece the arena has no room left for. */
	if (rounded > arena->size - arena->used)
		return NULL;

	/* Hands the piece out, cleared. */
	taken = arena->base + arena->used;
	arena->used += rounded;
	kern_memset(taken, 0, rounded);

	/* Succeeded: the piece lives until the next command. */
	return taken;
}

/* Reports the bucket of a kind and an identity: the identity's bits folded together with the kind. */
static uint32_t
bucket_of(
	enum ghci_vk_kind kind,
	uint64_t identity)
{
	uint64_t mixed;

	/* Folds the identity's words and the kind into a bucket index. */
	mixed = identity ^ (identity >> 32);
	mixed ^= mixed >> 16;
	mixed ^= mixed >> 8;
	mixed ^= (uint64_t)kind * 0x9dU;

	/* Reports the low bits as the bucket. */
	return (uint32_t)(mixed % GHCI_VK_BUCKETS);
}

/* Frees an object's record and what the record alone holds (a module's words, an import's hold). */
static void
object_free(
	struct ghci_vk_session *session,
	struct ghci_vk_object *object)
{
	/* A shader module holds its SPIR-V words. */
	if (object->kind == GHCI_VK_SHADER && object->as.shader.words != NULL)
		kern_free(object->as.shader.words);

	/* An imported memory object holds another session's memory. */
	if (object->kind == GHCI_VK_MEMORY && object->as.memory.imported)
		ghci_memory_unhold(session->device, object->as.memory.allocation);

	/* The record. */
	kern_free(object);
}

/* Reports whether the firmware still has a session's objects (opened, not closed or lost). */
static int
prot_alive(
	const struct ghci_session *owner)
{
	/* The executor's record says it. */
	if (owner->vk == NULL || !owner->vk->opened || owner->vk->lost)
		return 0;

	/* Succeeded: the firmware has the session. */
	return 1;
}
