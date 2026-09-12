/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Exercises production allocation sharing and blob scanout through independent
 * renderer contexts; the host peer checks protocol messages and lifetime edges.
 */

#define VENUS_CONSOLE_ENTRY venus_console_unused_main
#include "../../ws030/tests/venus-console.c"
#undef VENUS_CONSOLE_ENTRY

static void sharing_lifetime(void);
static void sharing_failure(void);
static void sharing_create(struct venus_controller *controller, void *session, void **resource, void **shared, struct gpu_image_descriptor *image);

/*
 * Runs bounded ownership and GPU-only scanout scenarios against production code.
 */
int
main(void)
{
	/* Verify independent contexts, source exit, repeated imports and native scanout retention. */
	sharing_lifetime();

	/* Verify uncertain attachment never permits premature host allocation reuse. */
	sharing_failure();

	/* Every software object and coherent backing must retire at a known ownership boundary. */
	assert(fixture_allocations == 0U);
	assert(fixture_dma == 0U);
	assert(fixture_mappings == 0U);
	puts("Venus sharing: independent context aliases, producer exit, same-context imports, immutable metadata, GPU-only blob scanout, retained display, timeout quarantine PASS");

	/* Succeeded: the peer observed allocation sharing without a pixel readback or upload. */
	return 0;
}

/* Creates a GPU-only allocation and exports its immutable linear image contract. */
static void
sharing_create(
	struct venus_controller *controller,
	void *session,
	void **resource,
	void **shared,
	struct gpu_image_descriptor *image)
{
	struct gpu_blob_create create;
	uint32_t identifier;
	unsigned mappings;
	int error;

	/* Non-mappable sharing must not consume the host-visible aperture. */
	mappings = fixture_commands[0x208U];
	memset(&create, 0, sizeof(create));
	create.bytes = 8192U;
	create.blob_id = 99U;
	create.flags = GPU_BLOB_SHAREABLE | GPU_BLOB_CROSS_DEVICE;
	error = venus_blob_create(controller, session, &create, resource, &identifier);
	assert(error == 0);
	assert(identifier != 0U);
	assert(fixture_commands[0x208U] == mappings);
	assert(((struct venus_resource *)*resource)->mapping.address == NULL);

	/* A padded allocation still exposes only the bounded complete linear image. */
	memset(image, 0, sizeof(*image));
	image->version = GPU_ABI_VERSION;
	image->size = sizeof(*image);
	image->width = 32U;
	image->height = 32U;
	image->format = GPU_PIXEL_RGBA8888;
	image->stride = 128U;
	image->offset = 128U;
	image->allocation_bytes = create.bytes;
	image->memory_type = 1U;
	image->usage = 6U;
	image->tiling = GPU_IMAGE_LINEAR;
	image->device_id = 73U;
	error = share_export(controller, session, *resource, image, shared);
	assert(error == 0);
	assert(*shared != NULL);

	/* Succeeded: one source alias and one independent capability retain the allocation. */
	return;
}

/* Keeps host memory live across producer exit and a receiver's final descriptor close. */
static void
sharing_lifetime(void)
{
	struct venus_controller controller;
	struct gpu_image_descriptor image;
	struct gpu_image_descriptor changed;
	struct gpu_display_claim claim;
	struct gpu_display_info information;
	struct gpu_display_present present;
	struct venus_share *share;
	uint8_t configuration[16];
	uint8_t byte;
	void *producer;
	void *consumer;
	void *late;
	void *source;
	void *capability;
	void *received;
	void *duplicate;
	void *last;
	void *rejected;
	uint32_t identifier;
	unsigned attaches;
	unsigned unrefs;
	unsigned uploads;
	int error;

	/* Each open owns its own renderer context instead of sharing an entire GPU session. */
	fixture_controller(&controller, configuration);
	error = venus_open(&controller, &producer);
	assert(error == 0);
	error = venus_open(&controller, &consumer);
	assert(error == 0);
	assert(((struct venus_session *)producer)->context != ((struct venus_session *)consumer)->context);
	sharing_create(&controller, producer, &source, &capability, &image);
	share = capability;
	assert(share->storage != source);
	assert(share->storage->context == 0U);

	/* Explicitly GPU-only storage refuses copied I/O rather than silently manufacturing pixels. */
	error = venus_resource_read(&controller, producer, source, 0U, &byte, 1U);
	assert(error == EOPNOTSUPP);
	error = venus_resource_write(&controller, producer, source, 0U, &byte, 1U);
	assert(error == EOPNOTSUPP);

	/* Re-export cannot change the interpretation already retained with this allocation. */
	changed = image;
	changed.offset++;
	error = share_export(&controller, producer, source, &changed, &rejected);
	assert(error == EINVAL);
	assert(rejected == NULL);

	/* Two aliases in one destination acquire only one native context attachment. */
	attaches = fixture_commands[0x202U];
	error = share_import(&controller, consumer, capability, &received, &identifier);
	assert(error == 0);
	error = share_import(&controller, consumer, capability, &duplicate, &identifier);
	assert(error == 0);
	assert(fixture_commands[0x202U] == attaches + 1U);
	venus_resource_destroy(&controller, consumer, duplicate);

	/* Source destruction and full process-context close do not unref the retained allocation. */
	unrefs = fixture_commands[0x102U];
	venus_resource_destroy(&controller, producer, source);
	venus_close(&controller, producer);
	assert(fixture_commands[0x102U] == unrefs);
	assert(controller.resources == share->storage);

	/* A receiver arriving after producer exit can still attach the same native allocation. */
	error = venus_open(&controller, &late);
	assert(error == 0);
	error = share_import(&controller, late, capability, &last, &identifier);
	assert(error == 0);
	assert(((struct venus_resource *)last)->identifier == ((struct venus_resource *)received)->identifier);
	share_release(&controller, capability);

	/* Discover the real output generation before the helper claims its existing inventory. */
	memset(&information, 0, sizeof(information));
	error = display_query(&controller, consumer, &information);
	assert(error == 0);
	assert((information.flags & GPU_DISPLAY_BLOB) != 0U);

	/* Select the imported allocation directly on the real native display callback. */
	console_test_claim(&controller, consumer, &claim);
	memset(&present, 0, sizeof(present));
	present.lease = claim.lease;
	present.generation = claim.generation;
	present.width = image.width;
	present.height = image.height;
	present.stride = image.stride;
	present.format = image.format;
	present.offset = image.offset;
	present.flags = GPU_DISPLAY_PRESENT_FIFO | GPU_DISPLAY_PRESENT_BLOB;
	present.refresh_millihz = VENUS_DISPLAY_REFRESH;
	uploads = fixture_commands[0x105U];
	error = display_present(&controller, consumer, received, &present);
	assert(error == 0);
	assert(present.sequence != 0U);
	assert(fixture_scanout == identifier);
	assert(fixture_commands[0x105U] == uploads);
	assert(controller.primary_scanout == share->storage);

	/* A live scanout keeps storage even after its presenting resource handle is destroyed. */
	venus_resource_destroy(&controller, consumer, received);
	assert(fixture_commands[0x102U] == unrefs);
	assert(controller.display->outputs[0].shared_front == share);
	venus_close(&controller, consumer);
	assert(controller.display->outputs[0].shared_front == NULL);
	assert(controller.primary_scanout == NULL);

	/* The final independent receiver controls terminal host release after display withdrawal. */
	venus_resource_destroy(&controller, late, last);
	venus_close(&controller, late);
	assert(controller.resources == NULL);
	assert(fixture_commands[0x102U] == unrefs + 1U);
	/* Reap the retained console worker before the existing reset-boundary teardown helper. */
	error = drv_venus_display_stop(&controller);
	assert(error == 0);
	console_test_finish(&controller);

	/* Succeeded: neither producer nor compositor close invalidated another live owner. */
	return;
}

/* Retains native state after an uncertain attachment until the checked reset boundary. */
static void
sharing_failure(void)
{
	struct venus_controller controller;
	struct gpu_image_descriptor image;
	uint8_t configuration[16];
	void *producer;
	void *consumer;
	void *source;
	void *capability;
	void *received;
	uint32_t identifier;
	unsigned unrefs;
	int error;

	/* Export one allocation without consuming any CPU mapping extent. */
	fixture_controller(&controller, configuration);
	error = venus_open(&controller, &producer);
	assert(error == 0);
	error = venus_open(&controller, &consumer);
	assert(error == 0);
	sharing_create(&controller, producer, &source, &capability, &image);

	/* An unacknowledged attach returns no alias and poisons the uncertain command stream. */
	unrefs = fixture_commands[0x102U];
	fixture_timeout = 0x202U;
	error = share_import(&controller, consumer, capability, &received, &identifier);
	assert(error == ETIMEDOUT);
	assert(received == NULL);
	assert(identifier == 0U);
	assert(controller.transport.failed != 0U);

	/* Closing every software owner must not reclaim hardware storage after that uncertainty. */
	venus_resource_destroy(&controller, producer, source);
	share_release(&controller, capability);
	venus_close(&controller, producer);
	venus_close(&controller, consumer);
	assert(controller.resources != NULL);
	assert(fixture_commands[0x102U] == unrefs);
	fixture_drain(&controller);
	assert(controller.resources == NULL);

	/* Succeeded: the fixture reset barrier was required for terminal storage retirement. */
	return;
}
