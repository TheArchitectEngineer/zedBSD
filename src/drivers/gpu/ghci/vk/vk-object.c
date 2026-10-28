/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's side of a node session: its open and close, the table
 * of its Vulkan objects by kind and identity, what each object lets go
 * of when it is destroyed, and the scratch a command decodes into.
 *
 * A VkDevice's objects go before the device; the device's GHCI session
 * closes last, so that the memory its mappings hold is unmapped first.
 */

#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/kmem.h>
#include <uapi/errno.h>

#include "vk.h"

/* The multiplier that spreads the identities over the buckets (2^64 / the golden ratio). */
#define OBJECT_HASH			0x9e3779b97f4a7c15ULL

static uint32_t bucket_of(enum ghci_vk_kind kind, uint64_t identity);
static void destroy_matching(struct ghci_vk_session *session, struct ghci_vk_device *device, int devices);
static void device_release(struct ghci_vk_session *session, struct ghci_vk_object *object);

/*
 * Opens the executor's side of a node session: the object table, the
 * arena, the job markers, and the session on the executor's list.
 * Returns 0, or ENOMEM with nothing left behind.
 */
int
ghci_vk_session_open(
	struct ghci_device *device,
	struct ghci_session *owner)
{
	struct ghci_vk_session *session;
	struct ghci_vk *vk;
	unsigned long irq;

	/* The record. */
	vk = device->vk;
	session = kern_calloc(1U, sizeof(*session));
	if (session == NULL)
		return ENOMEM;

	/* The scratch each command decodes its records into. */
	session->arena.base = kern_malloc(GHCI_VK_ARENA_BYTES);
	if (session->arena.base == NULL) {
		kern_free(session);
		return ENOMEM;
	}
	session->arena.size = GHCI_VK_ARENA_BYTES;
	session->device = device;
	session->owner = owner;
	ghci_jobs_init(&session->jobs);

	/* The service thread sees the session from here. */
	irq = spin_lock_irqsave(&device->irq_lock);

	session->next = vk->sessions;
	vk->sessions = session;

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* Succeeded: the node session runs commands. */
	owner->vk = session;
	return 0;
}

/*
 * Closes the executor's side of a node session; it cannot fail.  Its
 * queued completions end first (the core drained them; any left end
 * now), then every object goes, the devices last.
 */
void
ghci_vk_session_close(
	struct ghci_device *device,
	struct ghci_session *owner)
{
	struct ghci_vk_session *session;
	struct ghci_vk_session **position;
	struct ghci_vk *vk;
	unsigned long irq;

	/* The session's side. */
	vk = device->vk;
	session = owner->vk;

	/* Every completion of the session ends. */
	ghci_vk_drain(device, session);

	/* Every object of a device, then the devices with their GHCI sessions. */
	destroy_matching(session, NULL, 0);
	destroy_matching(session, NULL, 1);

	/* The session leaves the executor's list. */
	irq = spin_lock_irqsave(&device->irq_lock);

	position = &vk->sessions;
	while (*position != NULL && *position != session)
		position = &(*position)->next;
	if (*position == session)
		*position = session->next;

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* The record and its scratch go. */
	kern_free(session->arena.base);
	kern_free(session);
	owner->vk = NULL;
}

/* Finds an object of a kind by its identity; NULL when the session has none. */
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

	/* The bucket of the kind and identity, walked to the match. */
	bucket = bucket_of(kind, identity);
	for (object = session->buckets[bucket]; object != NULL; object = object->next) {
		if (object->kind == kind && object->identity == identity)
			return object;
	}

	/* Succeeded: none. */
	return NULL;
}

/*
 * Adds a cleared object of a kind with an identity libvulkan chose, made
 * on device.  Returns 0, EINVAL for an identity of that kind the session
 * already has (libvulkan never reuses one), EBUSY when the session keeps
 * as many objects as it may, or ENOMEM.
 */
int
ghci_vk_object_add(
	struct ghci_vk_session *session,
	enum ghci_vk_kind kind,
	uint64_t identity,
	struct ghci_vk_device *device,
	struct ghci_vk_object **result)
{
	struct ghci_vk_object *object;
	uint32_t bucket;

	/* A second object of one identity, or one past the bound. */
	object = ghci_vk_object_find(session, kind, identity);
	if (object != NULL || identity == 0U)
		return EINVAL;
	if (session->object_count >= GHCI_VK_OBJECTS_MAX)
		return EBUSY;

	/* The cleared record. */
	object = kern_calloc(1U, sizeof(*object));
	if (object == NULL)
		return ENOMEM;

	/* Its name, its device and its bucket. */
	object->kind = kind;
	object->identity = identity;
	object->device = device;
	bucket = bucket_of(kind, identity);
	object->next = session->buckets[bucket];
	session->buckets[bucket] = object;
	session->object_count++;

	/* Succeeded: the object is the session's. */
	*result = object;
	return 0;
}

/*
 * Destroys an object: it lets go of what it holds (a device first
 * destroys its own objects and closes its GHCI session) and leaves the
 * table.
 */
void
ghci_vk_object_destroy(
	struct ghci_vk_session *session,
	struct ghci_vk_object *object)
{
	struct ghci_vk_object **position;
	uint32_t bucket;

	/* What the object of each kind holds. */
	switch (object->kind) {
	case GHCI_VK_DEVICE:
		/* A device whose open failed has no record yet. */
		if (object->device != NULL)
			device_release(session, object);
		break;
	case GHCI_VK_MEMORY:
		ghci_vk_memory_release(session, object);
		break;
	case GHCI_VK_EVENT:
		ghci_vk_heap_give(object->as.event.block, object->as.event.offset);
		break;
	case GHCI_VK_COMMAND_BUFFER:
		ghci_vk_commands_release(object);
		break;
	case GHCI_VK_SET_LAYOUT:
	case GHCI_VK_PIPELINE_LAYOUT:
	case GHCI_VK_DESCRIPTOR_SET:
		ghci_vk_layout_release(session, object);
		break;
	case GHCI_VK_SHADER_MODULE:
	case GHCI_VK_PIPELINE:
	case GHCI_VK_RENDER_PASS:
		ghci_vk_pipeline_release(session, object);
		break;
	default:
		break;
	}

	/* The object leaves its bucket. */
	bucket = bucket_of(object->kind, object->identity);
	position = &session->buckets[bucket];
	while (*position != NULL && *position != object)
		position = &(*position)->next;
	if (*position == object)
		*position = object->next;
	session->object_count--;

	/* The record goes. */
	kern_free(object);
}

/* Destroys every object made on a device, but not the device itself. */
void
ghci_vk_objects_of_device(
	struct ghci_vk_session *session,
	struct ghci_vk_device *device)
{
	/* The objects whose device it is. */
	destroy_matching(session, device, 0);
}

/*
 * Takes cleared bytes of the scratch for the current command; NULL when
 * the scratch is used up.
 */
void *
ghci_vk_arena_take(
	struct ghci_vk_session *session,
	size_t bytes)
{
	struct i915_wire_reader none;
	void *taken;

	/* The codec's allocator, with a reader that only latches its own refusal. */
	kern_memset(&none, 0, sizeof(none));
	none.size = bytes;
	taken = i915_vkc_array(&none, &session->arena, 1U, bytes);

	/* Succeeded: the bytes, or NULL. */
	return taken;
}

/* Reports the bucket of a kind and identity. */
static uint32_t
bucket_of(
	enum ghci_vk_kind kind,
	uint64_t identity)
{
	uint64_t mixed;

	/* The identity and the kind, spread by the multiplier, the high bits taken. */
	mixed = (identity ^ (uint64_t)kind) * OBJECT_HASH;

	/* Succeeded: the bucket. */
	return (uint32_t)(mixed >> 56) % GHCI_VK_BUCKETS;
}

/*
 * Destroys the objects of a device (every device for NULL): the devices
 * themselves when devices is set, every other kind when it is not.
 */
static void
destroy_matching(
	struct ghci_vk_session *session,
	struct ghci_vk_device *device,
	int devices)
{
	struct ghci_vk_object **position;
	struct ghci_vk_object *object;
	uint32_t bucket;
	int match;

	/* Each bucket, walked again from where an object left it. */
	for (bucket = 0U; bucket < GHCI_VK_BUCKETS; bucket++) {
		position = &session->buckets[bucket];
		while (*position != NULL) {
			/* The kind the pass takes, and the device it names. */
			object = *position;
			match = 0;
			if (devices && object->kind == GHCI_VK_DEVICE)
				match = 1;
			else if (!devices && object->kind != GHCI_VK_DEVICE)
				match = 1;
			if (device != NULL && object->device != device)
				match = 0;

			/* A match leaves the bucket, so the same place is looked at again. */
			if (match) {
				ghci_vk_object_destroy(session, object);
				continue;
			}
			position = &object->next;
		}
	}
}

/*
 * Lets a VkDevice go: its objects, its executor heap, its place among
 * the session's devices and timelines, and its GHCI session, which
 * closes once the heap's unmaps are placed.
 */
static void
device_release(
	struct ghci_vk_session *session,
	struct ghci_vk_object *object)
{
	struct ghci_vk_device *device;
	struct ghci_vk_device **position;
	unsigned long irq;
	uint32_t timeline;

	/* The device's own objects, then its heap. */
	device = object->device;
	ghci_vk_objects_of_device(session, device);
	ghci_vk_heap_fini(session, device);

	/* The device leaves the session's list and its timelines, which the job ioctls read. */
	irq = spin_lock_irqsave(&session->device->irq_lock);

	position = &session->devices;
	while (*position != NULL && *position != device)
		position = &(*position)->next;
	if (*position == device)
		*position = device->next;
	for (timeline = 0U; timeline < GHCI_VK_TIMELINES; timeline++) {
		if (session->timeline_device[timeline] == device)
			session->timeline_device[timeline] = NULL;
	}

	spin_unlock_irqrestore(&session->device->irq_lock, irq);

	/* The GHCI session closes; its number goes back once the close's fence passes. */
	ghci_vm_close(session->device, device->vm);
	kern_free(device);
}
