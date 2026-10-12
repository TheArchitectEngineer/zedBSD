/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the bcm2711 Vulkan session's identity index (ws211-p010).
 *
 * The real vulkan-object.c publishes, finds, removes and closes typed wire
 * identities; the test checks that the hashed index and the session's list
 * stay the same set through every order of removal, and measures a lookup
 * of the oldest identity against a walk of the list (the former lookup).
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <uapi/errno.h>

#include "drivers/gpu/bcm2711/vulkan-private.h"

/* The session's limit of live identities (VULKAN_SESSION_OBJECT_LIMIT in vulkan-object.c). */
#define TEST_OBJECT_LIMIT 4096U

/* The lookups each timing repeats. */
#define TEST_LOOKUPS 20000U

/*
 * One payload the test publishes.
 *
 * The registry owns it from publication; the destructor counts it, so the
 * test sees every payload retire exactly once.
 */
struct test_payload {
	unsigned destroyed;
};

/* Host allocations still held by the code under test; zero at the end means nothing leaked. */
static unsigned test_allocations;

/* Payloads destroyed since the last count was read. */
static unsigned test_destroyed;

static int test_destroy(struct bcm2711_vulkan_session *session, void *payload);
static struct bcm2711_vulkan_session *test_session(void);
static struct bcm2711_vulkan_object *test_publish(struct bcm2711_vulkan_session *session, enum i915_vk_object_kind kind, uint64_t identity);
static void test_check_list(struct bcm2711_vulkan_session *session, uint32_t expected);
static struct bcm2711_vulkan_object *test_walk(struct bcm2711_vulkan_session *session, enum i915_vk_object_kind kind, uint64_t identity);
static double test_now(void);
static void test_lookup_cases(void);
static void test_remove_cases(void);
static void test_timing(uint32_t count);

/*
 * Supplies ordinary host allocation for the registry entries.
 */
void *
kern_calloc(
	size_t count,
	size_t bytes)
{
	void *pointer;

	/* The test counts what the code under test holds. */
	pointer = calloc(count, bytes);
	if (pointer != NULL)
		test_allocations++;

	/* Succeeded: zeroed storage, or NULL as the kernel would report it. */
	return pointer;
}

/*
 * Releases a registry entry's host storage.
 */
void
kern_free(
	void *pointer)
{
	/* A missing pointer owns nothing. */
	if (pointer == NULL)
		return;

	/* The entry no longer counts as held. */
	assert(test_allocations != 0U);
	test_allocations--;
	free(pointer);
}

/*
 * Runs the index's cases and the timing.
 */
int
main(
	void)
{
	/* Lookups, kinds, duplicates and the limit. */
	test_lookup_cases();

	/* Removal from every place in the list, during a walk, and close. */
	test_remove_cases();

	/* A lookup of the oldest identity, which the former list walked to the end for. */
	test_timing(300U);
	test_timing(1000U);
	test_timing(4000U);

	/* Nothing the registry allocated is left. */
	assert(test_allocations == 0U);
	puts("ws211-p010 object index host test: PASS");

	/* Succeeded: every case held. */
	return 0;
}

/* Counts a destroyed payload and frees it. */
static int
test_destroy(
	struct bcm2711_vulkan_session *session,
	void *payload)
{
	struct test_payload *destroyed;

	/* A payload retires only once. */
	(void)session;
	destroyed = payload;
	assert(destroyed->destroyed == 0U);
	destroyed->destroyed = 1U;
	test_destroyed++;
	free(destroyed);

	/* Succeeded: the payload is gone. */
	return 0;
}

/* Makes an empty session, as bcm2711_vulkan_session_open leaves one. */
static struct bcm2711_vulkan_session *
test_session(
	void)
{
	struct bcm2711_vulkan_session *session;

	/* The session's namespace starts empty. */
	session = calloc(1, sizeof(*session));
	assert(session != NULL);

	/* Succeeded: an open, empty namespace. */
	return session;
}

/* Publishes one identity with a fresh payload and checks the result. */
static struct bcm2711_vulkan_object *
test_publish(
	struct bcm2711_vulkan_session *session,
	enum i915_vk_object_kind kind,
	uint64_t identity)
{
	struct bcm2711_vulkan_object *object;
	struct test_payload *payload;
	int error;

	/* The payload the registry will own. */
	payload = calloc(1, sizeof(*payload));
	assert(payload != NULL);

	/* The identity is published and found at once. */
	error = bcm2711_vulkan_object_publish(session, kind, identity, payload, test_destroy, &object);
	assert(error == 0);
	assert(object != NULL);
	assert(bcm2711_vulkan_object_find(session, kind, identity) == object);

	/* Succeeded: the published entry. */
	return object;
}

/* Checks that the list holds each published identity once, its links are whole, and the index finds each. */
static void
test_check_list(
	struct bcm2711_vulkan_session *session,
	uint32_t expected)
{
	struct bcm2711_vulkan_object **link;
	struct bcm2711_vulkan_object *object;
	struct bcm2711_vulkan_object *found;
	uint32_t count;

	/* Every entry's link points at the pointer that leads to it, and the index finds the same entry. */
	count = 0U;
	link = &session->objects;
	for (object = session->objects; object != NULL; object = object->next) {
		assert(object->link == link);
		assert(object->published);
		found = bcm2711_vulkan_object_find(session, object->kind, object->identity);
		assert(found == object);
		link = &object->next;
		count++;
	}

	/* The count of the list is the registry's own count. */
	assert(count == expected);
	assert(session->object_count == expected);
}

/* Finds an identity the way the former registry did, by walking the whole list. */
static struct bcm2711_vulkan_object *
test_walk(
	struct bcm2711_vulkan_session *session,
	enum i915_vk_object_kind kind,
	uint64_t identity)
{
	struct bcm2711_vulkan_object *object;

	/* The first entry of the kind and identity, from the newest. */
	for (object = session->objects; object != NULL; object = object->next) {
		if (object->kind == kind && object->identity == identity)
			return object;
	}

	/* Absent. */
	return NULL;
}

/* Reads the monotonic clock in nanoseconds. */
static double
test_now(
	void)
{
	struct timespec now;

	/* The host's monotonic clock. */
	clock_gettime(CLOCK_MONOTONIC, &now);

	/* Succeeded: nanoseconds since an arbitrary start. */
	return (double)now.tv_sec * 1e9 + (double)now.tv_nsec;
}

/* Checks typed lookups, duplicates, absent identities and the live limit. */
static void
test_lookup_cases(
	void)
{
	struct bcm2711_vulkan_session *session;
	struct bcm2711_vulkan_object *command;
	struct bcm2711_vulkan_object *image;
	struct bcm2711_vulkan_object *object;
	struct test_payload *payload;
	uint64_t identity;
	int error;

	/* One identity names a command buffer and, separately, an image. */
	session = test_session();
	command = test_publish(session, I915_VK_OBJ_COMMAND_BUFFER, 7U);
	image = test_publish(session, I915_VK_OBJ_IMAGE, 7U);
	assert(command != image);
	assert(bcm2711_vulkan_object_find(session, I915_VK_OBJ_COMMAND_BUFFER, 7U) == command);
	assert(bcm2711_vulkan_object_find(session, I915_VK_OBJ_IMAGE, 7U) == image);
	assert(bcm2711_vulkan_object_find(session, I915_VK_OBJ_PIPELINE, 7U) == NULL);
	assert(bcm2711_vulkan_object_find(session, I915_VK_OBJ_IMAGE, 8U) == NULL);
	assert(bcm2711_vulkan_object_find(session, I915_VK_OBJ_IMAGE, 0U) == NULL);

	/* A live kind and identity cannot be published twice; the refusal owns nothing. */
	payload = calloc(1, sizeof(*payload));
	assert(payload != NULL);
	error = bcm2711_vulkan_object_publish(session, I915_VK_OBJ_IMAGE, 7U, payload, test_destroy, &object);
	assert(error == EEXIST);
	assert(object == NULL);

	/* Identities that share a bucket (the top ten bits of identity times the hash) all stay findable. */
	for (identity = 1U; identity <= TEST_OBJECT_LIMIT - 2U; identity++) {
		if (identity == 7U)
			continue;
		(void)test_publish(session, (enum i915_vk_object_kind)(1U + identity % (I915_VK_OBJ_KIND_COUNT - 1U)), identity + 1000000U);
	}
	test_check_list(session, TEST_OBJECT_LIMIT - 2U + 1U);

	/* The last free slot of the limit, then the refusal past it. */
	(void)test_publish(session, I915_VK_OBJ_FENCE, 99U);
	error = bcm2711_vulkan_object_publish(session, I915_VK_OBJ_FENCE, 100U, payload, test_destroy, &object);
	assert(error == ENOSPC);
	free(payload);

	/* Close retires every payload and empties the index. */
	test_destroyed = 0U;
	error = bcm2711_vulkan_objects_close(session);
	assert(error == 0);
	assert(test_destroyed == TEST_OBJECT_LIMIT);
	assert(session->objects == NULL);
	assert(bcm2711_vulkan_object_find(session, I915_VK_OBJ_COMMAND_BUFFER, 7U) == NULL);
	free(session);
	puts("ws211-p010 typed lookups, duplicates, limit and close: PASS");
}

/* Checks removal of the newest, the oldest and middle entries, removal during a walk, and a retained reference. */
static void
test_remove_cases(
	void)
{
	struct bcm2711_vulkan_session *session;
	struct bcm2711_vulkan_object *object;
	struct bcm2711_vulkan_object *next;
	struct bcm2711_vulkan_object *kept;
	uint64_t identity;
	uint32_t remaining;
	int error;

	/* Two hundred identities of two kinds. */
	session = test_session();
	for (identity = 1U; identity <= 200U; identity++)
		(void)test_publish(session, (identity & 1U) != 0U ? I915_VK_OBJ_DESCRIPTOR_SET : I915_VK_OBJ_IMAGE, identity);
	test_check_list(session, 200U);

	/* The newest (the list's head), the oldest (its tail) and one in the middle. */
	test_destroyed = 0U;
	error = bcm2711_vulkan_object_remove(session, I915_VK_OBJ_IMAGE, 200U);
	assert(error == 0);
	error = bcm2711_vulkan_object_remove(session, I915_VK_OBJ_DESCRIPTOR_SET, 1U);
	assert(error == 0);
	error = bcm2711_vulkan_object_remove(session, I915_VK_OBJ_DESCRIPTOR_SET, 101U);
	assert(error == 0);
	assert(test_destroyed == 3U);
	assert(bcm2711_vulkan_object_find(session, I915_VK_OBJ_IMAGE, 200U) == NULL);
	test_check_list(session, 197U);

	/* An absent identity, or the right identity of the wrong kind, removes nothing. */
	error = bcm2711_vulkan_object_remove(session, I915_VK_OBJ_IMAGE, 1U);
	assert(error == ENOENT);
	error = bcm2711_vulkan_object_remove(session, I915_VK_OBJ_IMAGE, 5000U);
	assert(error == ENOENT);
	test_check_list(session, 197U);

	/* A removed identity whose payload is still referenced stays alive until the reference goes. */
	kept = bcm2711_vulkan_object_find(session, I915_VK_OBJ_IMAGE, 50U);
	assert(kept != NULL);
	error = bcm2711_vulkan_object_retain(kept);
	assert(error == 0);
	error = bcm2711_vulkan_object_remove(session, I915_VK_OBJ_IMAGE, 50U);
	assert(error == 0);
	assert(test_destroyed == 3U);
	assert(!kept->published);
	assert(bcm2711_vulkan_object_find(session, I915_VK_OBJ_IMAGE, 50U) == NULL);
	error = bcm2711_vulkan_object_release(kept);
	assert(error == 0);
	assert(test_destroyed == 4U);
	test_check_list(session, 196U);

	/* The walk the descriptor pool makes: every descriptor set removed while the next one is held. */
	remaining = 196U;
	object = session->objects;
	while (object != NULL) {
		next = object->next;
		if (object->kind == I915_VK_OBJ_DESCRIPTOR_SET) {
			error = bcm2711_vulkan_object_remove(session, I915_VK_OBJ_DESCRIPTOR_SET, object->identity);
			assert(error == 0);
			remaining--;
		}
		object = next;
	}
	test_check_list(session, remaining);
	assert(remaining == 98U);

	/* A removed identity can be published again. */
	(void)test_publish(session, I915_VK_OBJ_DESCRIPTOR_SET, 1U);
	test_check_list(session, 99U);

	/* Close retires the rest. */
	error = bcm2711_vulkan_objects_close(session);
	assert(error == 0);
	free(session);
	puts("ws211-p010 removal from head, tail, middle, during a walk, with a reference, and close: PASS");
}

/* Times a lookup of the oldest identity in a namespace of count identities, against the former list walk. */
static void
test_timing(
	uint32_t count)
{
	struct bcm2711_vulkan_session *session;
	struct bcm2711_vulkan_object *oldest;
	struct bcm2711_vulkan_object *found;
	double started;
	double indexed;
	double walked;
	uint64_t identity;
	unsigned lookup;
	unsigned hits;
	int error;

	/* The command buffer first, as the compositor makes it at start, then the rest. */
	session = test_session();
	oldest = test_publish(session, I915_VK_OBJ_COMMAND_BUFFER, 1U);
	for (identity = 2U; identity <= count; identity++)
		(void)test_publish(session, (enum i915_vk_object_kind)(1U + identity % (I915_VK_OBJ_KIND_COUNT - 1U)), identity);

	/* The index's lookup. */
	hits = 0U;
	started = test_now();
	for (lookup = 0U; lookup < TEST_LOOKUPS; lookup++) {
		found = bcm2711_vulkan_object_find(session, I915_VK_OBJ_COMMAND_BUFFER, 1U);
		if (found == oldest)
			hits++;
	}
	indexed = (test_now() - started) / TEST_LOOKUPS;

	/* The former walk of the list. */
	started = test_now();
	for (lookup = 0U; lookup < TEST_LOOKUPS; lookup++) {
		found = test_walk(session, I915_VK_OBJ_COMMAND_BUFFER, 1U);
		if (found == oldest)
			hits++;
	}
	walked = (test_now() - started) / TEST_LOOKUPS;

	/* Both found the command buffer every time, and the index is faster once the namespace is large. */
	assert(hits == 2U * TEST_LOOKUPS);
	if (count >= 1000U)
		assert(indexed < walked);
	printf("ws211-p010 objects=%u oldest lookup: index %.1f ns, list walk %.1f ns\n", count, indexed, walked);

	/* The namespace closes. */
	error = bcm2711_vulkan_objects_close(session);
	assert(error == 0);
	free(session);
}
