/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host fixture for the synchronization module (p009). Exercises fence arming
 * against the engine seqno, status, the polling wait and reset, plus semaphore
 * and query lifetime, against the WS029 device.
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
#include "../../../src/drivers/gpu/i915/vk/sync.c"

static int fixture_pci_token;
#define fixture_pci_device	((struct drv_pci_device *)&fixture_pci_token)

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
	error = fixture_service->publish(fixture_pci_device, fixture_service_argument);
	assert(error == 0);
	return device;
}

static void
test_fence(void)
{
	struct i915_device *device;
	struct i915_vk_device vk;
	struct i915_vk_session session;
	struct i915_vk_fence *fence;
	struct i915_vk_semaphore *semaphore;
	struct i915_vk_query_pool *pool;
	void *gpu_session;
	int error;

	fixture_reset();
	device = attach();
	error = fixture_gpu_ops->open(device, &gpu_session);
	assert(error == 0);
	memset(&vk, 0, sizeof(vk));
	vk.i915 = device;
	memset(&session, 0, sizeof(session));
	session.vk = &vk;
	session.gpu = gpu_session;

	/* A fence created signaled reports success immediately. */
	error = i915_vk_fence_create(&session, 1, &fence);
	assert(error == 0);
	assert(i915_vk_fence_status(fence) == 0);
	i915_vk_fence_destroy(fence);

	/* An unsignaled, unarmed fence is never ready. */
	error = i915_vk_fence_create(&session, 0, &fence);
	assert(error == 0);
	assert(i915_vk_fence_status(fence) == EBUSY);

	/* Armed for seqno 5, it stays busy until the engine passes that seqno. */
	error = i915_vk_fence_arm(fence, 0U, 5U);
	assert(error == 0);
	device->engines[0].completed_seqno = 3U;
	assert(i915_vk_fence_status(fence) == EBUSY);
	assert(i915_vk_fence_wait(fence, 0U) == ETIMEDOUT);

	/* Once the engine retires past seqno 5 the fence signals and a wait returns. */
	device->engines[0].completed_seqno = 5U;
	assert(i915_vk_fence_status(fence) == 0);
	assert(i915_vk_fence_wait(fence, 1000U) == 0);

	/* Reset returns it to the unsignaled, unarmed state. */
	error = i915_vk_fence_reset(fence);
	assert(error == 0);
	assert(i915_vk_fence_status(fence) == EBUSY);
	i915_vk_fence_destroy(fence);

	/* Semaphores and query pools have a simple lifetime. */
	error = i915_vk_semaphore_create(&session, &semaphore);
	assert(error == 0);
	i915_vk_semaphore_destroy(semaphore);
	error = i915_vk_query_pool_create(&session, 1U, 4U, &pool);
	assert(error == 0);
	i915_vk_query_pool_destroy(pool);

	fixture_gpu_ops->close(device, gpu_session);
}

int
main(void)
{
	test_fence();
	printf("i915 vk sync host test PASS\n");
	return 0;
}
