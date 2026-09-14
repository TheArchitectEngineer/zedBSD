/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host fixture for the resource module (p003). Exercises the Vulkan memory,
 * buffer and image object lifetime against the real WS029 GEM allocator and
 * session address space, and checks the image surface-state structure.
 */

#include "../../ws029/tests/i915-fixture.inc"
#include "../../../src/drivers/gpu/i915/uncore.c"
#include "../../../src/drivers/gpu/i915/ggtt.c"
#include "../../../src/drivers/gpu/i915/ppgtt.c"
#include "../../../src/drivers/gpu/i915/gem.c"
#include "../../../src/drivers/gpu/i915/irq.c"
#include "../../../src/drivers/gpu/i915/engine.c"
#include "../../../src/drivers/gpu/i915/lrc.c"
#include "../../../src/drivers/gpu/i915/request.c"
#include "../../../src/drivers/gpu/i915/i915.c"
#include "../../../src/drivers/gpu/i915/vk/res.c"

static int fixture_pci_token;
#define fixture_pci_device	((struct drv_pci_device *)&fixture_pci_token)

/* Registers the driver, attaches the fake device and publishes the node. */
static struct i915_device *
attach(void)
{
	struct i915_device *device;
	int error;

	error = drv_i915_pci_driver_register();
	assert(error == 0);
	error = fixture_driver->attach(fixture_pci_device, &fixture_driver->ids[5]);
	assert(error == 0);
	device = fixture_driver_data;
	assert(device != NULL);
	error = fixture_service->publish(fixture_pci_device, fixture_service_argument);
	assert(error == 0);
	return device;
}

static void
test_memory_buffer_image(void)
{
	struct i915_device *device;
	struct i915_vk_device vk;
	struct i915_vk_session session;
	struct i915_vk_memory *memory;
	struct i915_vk_buffer *buffer;
	struct i915_vk_image *image;
	struct i915_vk_image_view *view;
	struct i915_vk_image_info info;
	const uint32_t *surface;
	void *gpu_session;
	void *cpu;
	int error;

	fixture_reset();
	device = attach();

	/* A session provides the PPGTT the resources bind into. */
	error = fixture_gpu_ops->open(device, &gpu_session);
	assert(error == 0);
	memset(&vk, 0, sizeof(vk));
	vk.i915 = device;
	memset(&session, 0, sizeof(session));
	session.vk = &vk;
	session.gpu = gpu_session;

	/* Memory allocates a GEM object with a host-visible mapping. */
	error = i915_vk_memory_alloc(&session, 65536U, 0U, &memory);
	assert(error == 0);
	error = i915_vk_memory_map(memory, &cpu);
	assert(error == 0 && cpu != NULL);

	/* A buffer binds a range of that memory. */
	error = i915_vk_buffer_create(&session, 4096U, 0U, &buffer);
	assert(error == 0);
	error = i915_vk_buffer_bind(buffer, memory, 0U);
	assert(error == 0);

	/* An image binds memory and encodes a Gen12 surface state. */
	info.width = 320U;
	info.height = 240U;
	info.format = 199U;	/* R8G8B8A8_UNORM */
	info.tiling = 0U;
	error = i915_vk_image_create(&session, &info, &image);
	assert(error == 0);
	error = i915_vk_image_bind(image, memory, 0U);
	assert(error == 0);
	error = i915_vk_image_view_create(&session, image, 199U, &view);
	assert(error == 0);
	surface = i915_vk_image_surface_state(view);

	/* Dword 0: a 2D, linear, R8G8B8A8_UNORM surface. */
	assert(((surface[0] >> 29) & 0x7U) == 1U);
	assert(((surface[0] >> 18) & 0x3FFU) == 199U);
	assert(((surface[0] >> 12) & 0x3U) == 0U);

	/* Dword 2: the extent minus one; dword 3: the pitch minus one. */
	assert((surface[2] & 0x3FFFU) == 319U);
	assert(((surface[2] >> 16) & 0x3FFFU) == 239U);
	assert((surface[3] & 0x3FFFFU) == (320U * 4U - 1U));

	/* Dwords 8 and 9 carry the bound base address. */
	assert(surface[8] != 0U || surface[9] != 0U);

	/* Every object releases and the session's memory returns to the pool. */
	i915_vk_image_view_destroy(view);
	i915_vk_image_destroy(image);
	i915_vk_buffer_destroy(buffer);
	i915_vk_memory_free(memory);
	fixture_gpu_ops->close(device, gpu_session);
}

static void
test_descriptor(void)
{
	struct i915_vk_dsl_binding bindings[1];
	struct i915_vk_write_dset writes[1];
	struct i915_vk_dsl *dsl;
	struct i915_vk_dpool *dpool;
	struct i915_vk_dset *first;
	struct i915_vk_dset *second;
	int error;

	/* A layout with one sampled-image binding. */
	bindings[0].binding = 0U;
	bindings[0].type = 1U;
	error = i915_vk_dsl_create(NULL, bindings, 1U, &dsl);
	assert(error == 0);

	/* A pool bounded to one set hands out exactly one before refusing more. */
	error = i915_vk_dpool_create(NULL, 1U, &dpool);
	assert(error == 0);
	error = i915_vk_dset_alloc(dpool, dsl, &first);
	assert(error == 0);
	error = i915_vk_dset_alloc(dpool, dsl, &second);
	assert(error == ENOSPC);

	/* An update records the binding; freeing the set reopens the pool slot. */
	writes[0].binding = 0U;
	writes[0].type = 1U;
	writes[0].view = NULL;
	writes[0].sampler = NULL;
	error = i915_vk_dset_update(first, writes, 1U);
	assert(error == 0);
	i915_vk_dset_free(first);
	error = i915_vk_dset_alloc(dpool, dsl, &second);
	assert(error == 0);
	i915_vk_dset_free(second);

	i915_vk_dpool_destroy(dpool);
	i915_vk_dsl_destroy(dsl);
}

int
main(void)
{
	test_memory_buffer_image();
	test_descriptor();
	printf("i915 vk res host test PASS\n");
	return 0;
}
