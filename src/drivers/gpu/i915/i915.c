/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Intel i915 GPU driver: PCI lifecycle and the complete drv_gpu glue.
 *
 * Attach brings the device from PCI enable through forcewake,
 * graphics reset, the global GTT, the interrupt vector and both
 * engines, then stages publication. Sessions own a private address
 * space, one context per engine, contiguous storage objects and a
 * pool of batch objects. Native command streams are validated here
 * and run through the request code.
 */

#include "internal.h"

#include <drivers/gpu.h>
#include <drivers/i915.h>
#include "vk/vk.h"
#include <uapi/gpu.h>
#include <uapi/gpu-job.h>
#include <kern/device-io.h>
#include <kern/klog.h>
#include <kern/kmem.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "linux/i915-commands.inc"
#include "linux/i915-ids.inc"

/* Builds one exact-match identity row for an Intel graphics product. */
#define I915_ID(product)	{ 0x8086U, (product), DRV_PCI_ANY_ID, DRV_PCI_ANY_ID, 0U, 0U, 0U }

/* Storage with CPU copies, native streams, queued completion and supervised jobs. */
#define I915_CAPABILITIES	(GPU_CAP_RESOURCE | GPU_CAP_TRANSFER | GPU_CAP_COMMAND | \
				 GPU_CAP_NOTIFICATION | GPU_CAP_JOB | GPU_CAP_JOB_CAPACITY | GPU_CAP_CAPSET | GPU_CAP_BLOB | GPU_CAP_MAPPING)

/* The pool never holds more batch objects than requests can be in flight. */
#define I915_BATCH_POOL_MAX	I915_REQUEST_SLOTS

static int i915_attach(struct drv_pci_device *pci, const struct drv_pci_id *id);
static int i915_start(struct i915_device *device);
static int i915_stop(struct i915_device *device);
static int i915_detach(struct drv_pci_device *pci, unsigned flags);
static int i915_publish(struct drv_pci_device *pci, void *argument);
static int i915_unpublish(struct drv_pci_device *pci, void *argument);
static int i915_open(void *opaque, void **result);
static void i915_close(void *opaque, void *private_session);
static int i915_get_info(void *opaque, void *private_session, struct gpu_info *info);
static int i915_resource_create(void *opaque, void *private_session, const struct gpu_resource_create *request, void **result);
static void i915_resource_destroy(void *opaque, void *private_session, void *object);
static int i915_resource_read(void *opaque, void *private_session, void *object, uint64_t offset, void *buffer, uint32_t bytes);
static int i915_resource_write(void *opaque, void *private_session, void *object, uint64_t offset, const void *buffer, uint32_t bytes);
static int i915_get_capset(void *opaque, void *private_session, struct gpu_capset *capset);
static int i915_blob_create(void *opaque, void *private_session, const struct gpu_blob_create *request, void **result, uint32_t *resource_id);
static int i915_resource_map(void *opaque, void *private_session, void *object, struct drv_gpu_mapping *mapping);
static int i915_command(void *opaque, void *private_session, const void *buffer, uint32_t bytes);
static int i915_command_submit(void *opaque, void *private_session, const void *buffer, uint32_t bytes, uint32_t flags, uint32_t timeline, struct drv_gpu_completion *completion);
static void i915_command_drain(void *opaque, void *private_session);
static int i915_job_reserve(void *opaque, void *private_session, uint32_t timeline, struct drv_gpu_completion *completion, void **reservation);
static int i915_job_commit(void *opaque, void *private_session, void *reservation, struct drv_gpu_completion *completion);
static int i915_job_cancel(void *opaque, void *private_session, void *reservation, struct drv_gpu_completion *completion, unsigned fault);
static int i915_job_capacity(void *opaque, void *private_session, uint32_t timeline, unsigned *available);
static int i915_stop_begin(void *opaque, void *private_session, int error);
static int i915_stop_poll(void *opaque, void *private_session);
static void i915_fault(void *opaque, int error);
static int i915_reset_device(void *opaque);
static int i915_isolate(void *opaque, void *private_session);
static void i915_session_contexts_destroy(struct i915_device *device, struct i915_session *session);
static void i915_session_batches_destroy(struct i915_device *device, struct i915_session *session);
static int i915_engine_for_timeline(struct i915_device *device, uint32_t timeline, struct i915_engine **engine);
static int i915_submit_stream(struct i915_device *device, struct i915_session *session, const void *buffer, uint32_t bytes, struct drv_gpu_completion *completion);
static int i915_submit_marker(struct i915_device *device, struct i915_session *session, uint32_t timeline, struct drv_gpu_completion *completion);
static int i915_batch_acquire(struct i915_device *device, struct i915_session *session, uint32_t bytes, struct i915_gem_object **result);
static struct i915_gem_object *i915_session_object(struct i915_session *session, uint64_t handle);
static struct i915_request *i915_reservation(struct i915_engine *engine, void *reservation, struct drv_gpu_completion *completion);

/*
 * Registers the i915 PCI backend for Alder Lake-P class graphics devices.
 */
int
drv_i915_pci_driver_register(void)
{
	static const struct drv_pci_id identifiers[] = {
		INTEL_ADLP_IDS(I915_ID),
		INTEL_ADLN_IDS(I915_ID),
		INTEL_RPLU_IDS(I915_ID),
		INTEL_RPLP_IDS(I915_ID)
	};
	static struct drv_pci_driver driver = {
		"i915", identifiers, sizeof(identifiers) / sizeof(identifiers[0]), NULL,
		i915_attach, i915_detach, NULL, NULL, NULL, { 0U, 0U, 0U, 0U }
	};
	int error;

	/* PCI owns probing, binding and the complete device lifecycle. */
	error = drv_pci_driver_register(&driver);
	if (error != 0)
		return error;

	/* Succeeded: matching devices are attached during PCI scanning. */
	return 0;
}

/*
 * Validates a native command stream and locates its relocations and batch.
 */
int
drv_i915_stream_parse(
	const void *buffer,
	uint32_t bytes,
	struct i915_stream *stream)
{
	const uint8_t *header;
	uint32_t magic;
	uint32_t version;
	uint32_t flags;
	uint32_t reserved_low;
	uint32_t reserved_high;
	uint32_t expected;
	uint32_t index;
	uint32_t dword_offset;

	/* Nothing of a rejected stream is reported. */
	memset(stream, 0, sizeof(*stream));
	if (bytes < I915_STREAM_HEADER_BYTES)
		return EINVAL;

	/* The header fields are little-endian words at fixed offsets. */
	header = buffer;
	memcpy(&magic, header, 4U);
	memcpy(&version, header + 4U, 4U);
	memcpy(&stream->engine, header + 8U, 4U);
	memcpy(&stream->relocation_count, header + 12U, 4U);
	memcpy(&stream->batch_dwords, header + 16U, 4U);
	memcpy(&flags, header + 20U, 4U);
	memcpy(&reserved_low, header + 24U, 4U);
	memcpy(&reserved_high, header + 28U, 4U);

	/* Only version 1 streams for the two supported engines are accepted. */
	if (magic != I915_STREAM_MAGIC || version != I915_STREAM_VERSION)
		return EINVAL;
	if (stream->engine != I915_STREAM_ENGINE_RCS0 && stream->engine != I915_STREAM_ENGINE_BCS0)
		return EINVAL;
	if (flags != 0U || reserved_low != 0U || reserved_high != 0U)
		return EINVAL;

	/* Counts are bounded so the copies below stay within one batch object. */
	if (stream->relocation_count > I915_STREAM_MAX_RELOCATIONS)
		return EINVAL;
	if (stream->batch_dwords == 0U || stream->batch_dwords > I915_STREAM_MAX_DWORDS)
		return EINVAL;

	/* The byte count must match the header exactly; no trailing bytes are allowed. */
	expected = I915_STREAM_HEADER_BYTES + stream->relocation_count * I915_STREAM_RELOCATION_BYTES + stream->batch_dwords * 4U;
	if (bytes != expected)
		return EINVAL;

	/* The relocation table follows the header, the batch follows the table. */
	stream->relocations = header + I915_STREAM_HEADER_BYTES;
	stream->batch = (const uint32_t *)(header + I915_STREAM_HEADER_BYTES + stream->relocation_count * I915_STREAM_RELOCATION_BYTES);

	/* A batch must end so the engine returns to the ring. */
	if (stream->batch[stream->batch_dwords - 1U] != MI_BATCH_BUFFER_END)
		return EINVAL;

	/* Every relocation patches a 64-bit address that lies inside the batch. */
	for (index = 0U; index < stream->relocation_count; index++) {
		memcpy(&dword_offset, stream->relocations + index * I915_STREAM_RELOCATION_BYTES, 4U);
		memcpy(&reserved_low, stream->relocations + index * I915_STREAM_RELOCATION_BYTES + 4U, 4U);
		if (reserved_low != 0U)
			return EINVAL;
		if (dword_offset + 1U >= stream->batch_dwords)
			return EINVAL;
	}

	/* Succeeded: the stream has a sound shape; handles are resolved at submission. */
	return 0;
}

/* Allocates the device state and runs the attach sequence. */
static int
i915_attach(
	struct drv_pci_device *pci,
	const struct drv_pci_id *id)
{
	struct i915_device *device;
	int error;
	int cleanup;

	/* Matching already consumed the identity row. */
	(void)id;

	/* Private state exists before any hardware lease is taken. */
	device = kern_calloc(1U, sizeof(*device));
	if (device == NULL)
		return ENOMEM;

	/* The mutex serializes sessions; the IRQ lock guards engine queues and counters. */
	device->pci = pci;
	device->stage = "locks";
	error = mutex_init(&device->mutex, LOCK_RANK_DEVICE, "i915");
	if (error != 0) {
		kern_free(device);
		return error;
	}

	spin_init(&device->irq_lock, LOCK_RANK_DEVICE, "i915 irq");
	waitq_init(&device->retire_waitq, "i915 retire");

	/* Session zero is reserved so a logged identifier of zero is always a bug. */
	device->next_session = 1U;

	/* PCI gains a retryable owner before the first hardware acquisition. */
	device->stage = "driver-data";
	error = drv_pci_device_set_driver_data(pci, device);
	if (error != 0) {
		kern_free(device);
		return error;
	}

	/* Hardware initialization stages publication; a failure names its stage. */
	error = i915_start(device);
	if (error != 0) {
		kern_logf("i915: attach stopped at %s: %d\n", device->stage, error);

		/* A retained lease keeps an unpublished owner for a later detach retry. */
		cleanup = i915_stop(device);
		if (cleanup != 0) {
			kern_logf("i915: attach cleanup retained %d\n", cleanup);
			return 0;
		}

		/* PCI's pointer is removed only after every lease retired. */
		cleanup = drv_pci_device_set_driver_data(pci, NULL);
		if (cleanup != 0)
			return 0;

		kern_free(device);
		return error;
	}

	/* Succeeded: PCI may publish this backend after binding it. */
	return 0;
}

/* Brings the hardware from PCI enable to a staged GPU publication. */
static int
i915_start(
	struct i915_device *device)
{
	static const struct drv_pci_service_interface service = {
		i915_publish, i915_unpublish
	};
	struct drv_pci_bar bar;
	int error;

	/* Identity is logged first so a stopped attach still names the device. */
	device->product = drv_pci_device_product(device->pci);
	device->revision = drv_pci_device_revision(device->pci);
	kern_logf("i915: device 8086:%04x rev %02x (experimental native driver, no display)\n",
		(unsigned)device->product, (unsigned)device->revision);

	/* The DMA provider constrains later object allocations. */
	device->stage = "dma-provider";
	device->dma = drv_pci_device_dma(device->pci);
	if (device->dma == NULL)
		return ENODEV;

	/* The saved command bits are restored by the final owner cleanup. */
	device->stage = "save-pci-state";
	error = drv_pci_device_save_enable_state(device->pci, &device->enable_state);
	if (error != 0)
		return error;

	/* Saved ownership survives every later attach or detach retry. */
	device->saved = 1U;

	/* Register decoding is required before BAR0 can be touched. */
	device->stage = "enable-pci-memory";
	error = drv_pci_device_enable_memory(device->pci);
	if (error != 0)
		return error;

	/* BAR0 holds the registers in its lower half and the GGTT in its upper half. */
	device->stage = "bar0";
	error = drv_pci_device_bar(device->pci, GEN4_GTTMMADR_BAR, &bar);
	if (error != 0)
		return error;

	/* A memory BAR that is not a power of two at least two pages long is not this device. */
	if (bar.type == DRV_PCI_BAR_IO ||
	    bar.size < 2U * I915_PAGE_BYTES ||
	    (bar.size & (bar.size - 1U)) != 0U)
		return ENODEV;

	/* PCI hands out BAR windows only to the driver that claimed them. */
	device->stage = "claim-bar0";
	error = drv_pci_device_claim_bar(device->pci, GEN4_GTTMMADR_BAR);
	if (error != 0)
		return error;

	device->bar0_claimed = 1U;

	/* Registers are mapped uncached so every access reaches the device in order. */
	device->stage = "map-registers";
	error = drv_pci_device_map_bar_region(
		device->pci,
		GEN4_GTTMMADR_BAR,
		0U,
		(size_t)(bar.size / 2U),
		DRV_PCI_MAP_READ | DRV_PCI_MAP_WRITE | DRV_PCI_MAP_NOCACHE,
		&device->regs);
	if (error != 0)
		return error;

	/* Forcewake accounting starts from a fully released device. */
	device->stage = "uncore";
	error = drv_i915_uncore_init(device);
	if (error != 0)
		return error;

	/* A full graphics reset discards whatever firmware left running. */
	device->stage = "gt-reset";
	error = drv_i915_gt_reset(device);
	if (error != 0)
		return error;

	/* The GGTT is sized, mapped and pointed at scratch before engines exist. */
	error = drv_i915_ggtt_start(device);
	if (error != 0)
		return error;

	/* Bus mastering is needed for message-signalled interrupts and later DMA. */
	device->stage = "enable-bus-master";
	error = drv_pci_device_set_bus_master(device->pci, true);
	if (error != 0)
		return error;

	device->bus_master = 1U;

	/* The vector is enabled before engines so their first breadcrumb is not lost. */
	device->stage = "irq";
	error = drv_i915_irq_start(device);
	if (error != 0)
		return error;

	/* Engines, cache policy tables and kernel contexts come up together. */
	device->stage = "engines";
	error = drv_i915_engines_start(device);
	if (error != 0)
		return error;

#if CONFIG_DRIVER_PCI_I915_SELFTEST
	/* The selftest must prove execution before any session can submit. */
	device->stage = "selftest";
	error = drv_i915_selftest(device);
	if (error != 0)
		return error;
	error = drv_i915_clear_selftest(device);
	if (error != 0)
		return error;
	error = drv_i915_rcs_selftest(device);
	if (error != 0)
		return error;
	error = drv_i915_rt_selftest(device);
	if (error != 0)
		return error;
	error = drv_i915_draw_selftest(device);
	if (error != 0)
		return error;
#endif

	/* The native Vulkan executor attaches once execution is proven. */
	device->stage = "vk";
	error = drv_i915_vk_attach(device, &device->vk);
	if (error != 0)
		return error;

	/* PCI publishes the GPU node after attach and withdraws it before detach. */
	device->stage = "gpu-publication";
	error = drv_pci_device_set_service(device->pci, &service, device);
	if (error != 0)
		return error;

	/* Succeeded: the backend and its publication contract are initialized. */
	return 0;
}

/* Releases hardware leases in reverse order; a retained lease is reported. */
static int
i915_stop(
	struct i915_device *device)
{
	struct i915_gem_object *object;
	struct i915_ppgtt *vm;
	int error;

	/* The Vulkan executor releases its software state before hardware teardown. */
	drv_i915_vk_detach(device->vk);
	device->vk = NULL;

	/* Interrupts stop before any state they touch is released. */
	error = drv_i915_irq_stop(device);
	if (error != 0)
		return error;

	/* A full reset ends every GPU access before objects are freed underneath it. */
	if (device->regs.address != NULL) {
		error = drv_i915_gt_reset(device);
		if (error != 0)
			kern_logf("i915: reset before stop failed: %d\n", error);
	}

	/* Engines release their kernel contexts and status pages after the reset. */
	drv_i915_engines_stop(device);

	/* Objects still on the list belong to closed sessions or quarantine; the GPU is reset. */
	while (device->objects != NULL) {
		object = device->objects;
		drv_i915_gem_unbind_ggtt(device, object);
		object->vm = NULL;
		object->quarantined = 0U;
		drv_i915_gem_destroy(device, object);
	}

	device->quarantined_objects = 0U;

	/* Address spaces of quarantined sessions are released after their objects. */
	while (device->quarantined_vms != NULL) {
		vm = device->quarantined_vms;
		device->quarantined_vms = vm->next;
		drv_i915_ppgtt_destroy(vm);
		kern_free(vm);
	}

	/* The GGTT and its scratch page retire after interrupts are gone. */
	if (device->ggtt.entries != 0U)
		drv_i915_ggtt_stop(device);

	/* Bus mastering ends before the register mappings disappear. */
	if (device->bus_master != 0U) {
		error = drv_pci_device_set_bus_master(device->pci, false);
		if (error != 0)
			return error;

		device->bus_master = 0U;
	}

	/* The table window and the register window are separate mappings. */
	if (device->gtt.address != NULL) {
		drv_pci_device_unmap_bar(device->pci, &device->gtt);
		memset(&device->gtt, 0, sizeof(device->gtt));
	}

	if (device->regs.address != NULL) {
		drv_pci_device_unmap_bar(device->pci, &device->regs);
		memset(&device->regs, 0, sizeof(device->regs));
	}

	/* The BAR claim returns to PCI after both windows are unmapped. */
	if (device->bar0_claimed != 0U) {
		drv_pci_device_release_bar(device->pci, GEN4_GTTMMADR_BAR);
		device->bar0_claimed = 0U;
	}

	/* The saved command state is restored only after every access ended. */
	if (device->saved != 0U) {
		error = drv_pci_device_restore_enable_state(device->pci, &device->enable_state);
		if (error != 0)
			return error;

		device->saved = 0U;
	}

	/* Succeeded: no hardware lease remains owned. */
	return 0;
}

/* Detaches an unpublished device and frees its state. */
static int
i915_detach(
	struct drv_pci_device *pci,
	unsigned flags)
{
	struct i915_device *device;
	int error;

	/* PCI applied its detach policy before calling the owner. */
	(void)flags;

	/* An already-cleaned attach has nothing left to detach. */
	device = drv_pci_device_driver_data(pci);
	if (device == NULL)
		return 0;

	/* Publication must be withdrawn before the hardware disappears. */
	if (device->gpu != NULL)
		return EBUSY;

	/* Every lease retires before PCI's pointer is removed. */
	error = i915_stop(device);
	if (error != 0)
		return error;

	error = drv_pci_device_set_driver_data(pci, NULL);
	if (error != 0)
		return error;

	/* No session, service or interrupt path retains the device. */
	kern_free(device);

	/* Succeeded: PCI may clear the driver binding. */
	return 0;
}

/* Registers the GPU node with the framework. */
static int
i915_publish(
	struct drv_pci_device *pci,
	void *argument)
{
	static const struct drv_gpu_command_ops commands = {
		i915_command_submit, i915_command_drain
	};
	static const struct drv_gpu_job_ops jobs = {
		i915_job_reserve,
		i915_job_commit,
		i915_job_cancel,
		i915_job_capacity
	};
	static const struct drv_gpu_recovery_ops recovery = {
		i915_stop_begin,
		i915_stop_poll,
		i915_fault,
		i915_reset_device,
		i915_isolate
	};
	static const struct drv_gpu_ops operations = {
		DRV_GPU_INTERFACE_VERSION,
		sizeof(struct drv_gpu_ops),
		I915_CAPABILITIES,
		0U,
		i915_open,
		i915_close,
		i915_get_info,
		i915_resource_create,
		i915_resource_destroy,
		i915_get_capset,
		i915_blob_create,
		i915_resource_read,
		i915_resource_write,
		i915_command,
		NULL,
		i915_resource_map,
		NULL,
		NULL,
		NULL,
		&commands,
		NULL,
		&jobs,
		&recovery
	};
	struct i915_device *device;
	int error;

	/* PCI passes the device staged during attach. */
	(void)pci;
	device = argument;

	/* Only publication ownership transfers; PCI keeps the hardware. */
	error = drv_gpu_register(&operations, device, &device->gpu);
	if (error != 0)
		return error;

	/* The log names the actual backend without claiming any rendering works. */
	kern_logf("i915: registered native GPU node (storage, native streams, jobs)\n");

	/* Succeeded: the GPU node accepts sessions. */
	return 0;
}

/* Withdraws the GPU node; EBUSY keeps the handle for a retry. */
static int
i915_unpublish(
	struct drv_pci_device *pci,
	void *argument)
{
	struct i915_device *device;
	int error;

	/* Publication identity is the staged device. */
	(void)pci;
	device = argument;

	/* A failed publication has no handle to consume. */
	if (device->gpu == NULL)
		return 0;

	/* Sessions still open keep the registration alive. */
	error = drv_gpu_unregister(device->gpu);
	if (error != 0)
		return error;

	device->gpu = NULL;

	/* Succeeded: PCI may now stop and detach the hardware. */
	return 0;
}

/* Opens one session with its own private address space and contexts. */
static int
i915_open(
	void *opaque,
	void **result)
{
	struct i915_device *device;
	struct i915_session *session;
	uint32_t sw_id;
	unsigned index;
	int error;

	/* A failed open transfers nothing to the caller. */
	device = opaque;
	*result = NULL;

	session = kern_calloc(1U, sizeof(*session));
	if (session == NULL)
		return ENOMEM;

	/* The identifier and the address space are created under the controller mutex. */
	mutex_lock(&device->mutex);

	/* A faulted device refuses new sessions until it is reset. */
	if (device->failed != 0U) {
		mutex_unlock(&device->mutex);
		kern_free(session);
		return ENODEV;
	}

	/* Identifiers are never reused so log lines stay unambiguous. */
	if (device->next_session == 0U) {
		mutex_unlock(&device->mutex);
		kern_free(session);
		return EOVERFLOW;
	}

	/* The address space is its own allocation so quarantine can keep it past close. */
	session->vm = kern_calloc(1U, sizeof(*session->vm));
	if (session->vm == NULL) {
		mutex_unlock(&device->mutex);
		kern_free(session);
		return ENOMEM;
	}

	/* The address space starts empty; every object binds into it at creation. */
	error = drv_i915_ppgtt_create(session->vm);
	if (error != 0) {
		mutex_unlock(&device->mutex);
		kern_free(session->vm);
		kern_free(session);
		return error;
	}

	session->device = device;
	session->identifier = device->next_session;
	device->next_session++;
	session->next_slot = 1U;
	session->vm->owner = session->identifier;

	/* One context per engine lets either engine run this session's work. */
	sw_id = (session->identifier % (I915_CONTEXT_ID_MODULUS - 1U)) + 1U;
	for (index = 0U; index < I915_ENGINE_COUNT; index++) {
		error = drv_i915_lrc_create(device, &device->engines[index], session->vm, sw_id, &session->contexts[index]);
		if (error != 0) {
			i915_session_contexts_destroy(device, session);
			drv_i915_ppgtt_destroy(session->vm);
			mutex_unlock(&device->mutex);
			kern_free(session->vm);
			kern_free(session);
			return error;
		}
	}

	/* The executor session wraps this session address space and lifetime. */
	if (device->vk != NULL) {
		error = drv_i915_vk_open(device->vk, session, &session->vk);
		if (error != 0) {
			i915_session_contexts_destroy(device, session);
			drv_i915_ppgtt_destroy(session->vm);
			mutex_unlock(&device->mutex);
			kern_free(session->vm);
			kern_free(session);
			return error;
		}
	}

	mutex_unlock(&device->mutex);

	*result = session;

	/* Succeeded: the framework owns the session until close. */
	return 0;
}

/* Closes a session after the framework drained its callbacks and retired its resources. */
static void
i915_close(
	void *opaque,
	void *private_session)
{
	struct i915_device *device;
	struct i915_session *session;
	unsigned index;

	device = opaque;
	session = private_session;

	/* The executor session is software state, released first. */
	if (session->vk != NULL) {
		drv_i915_vk_close(session->vk);
		session->vk = NULL;
	}

	/* The address space is released after every object left it. */
	mutex_lock(&device->mutex);

	/* Contexts and batches of a quarantined session are retained on the device object list. */
	if (session->quarantined != 0U) {
		for (index = 0U; index < I915_ENGINE_COUNT; index++) {
			if (session->contexts[index].image != NULL)
				session->contexts[index].image->quarantined = 1U;
			if (session->contexts[index].ring != NULL)
				session->contexts[index].ring->quarantined = 1U;
		}
	}

	i915_session_batches_destroy(device, session);
	i915_session_contexts_destroy(device, session);

	/* A quarantined session's tables stay device-owned until the checked reset frees them. */
	if (session->quarantined == 0U) {
		drv_i915_ppgtt_destroy(session->vm);
		kern_free(session->vm);
	} else {
		session->vm->next = device->quarantined_vms;
		device->quarantined_vms = session->vm;
		kern_logf("i915: session %u closed while quarantined; tables retained\n", session->identifier);
	}

	mutex_unlock(&device->mutex);

	kern_free(session);
}

/* Describes the backend to userspace. */
static int
i915_get_info(
	void *opaque,
	void *private_session,
	struct gpu_info *info)
{
	/* The reply depends on the device, not the session. */
	(void)opaque;
	(void)private_session;

	/* Limits describe the contiguous objects this backend allocates. */
	info->capabilities = I915_CAPABILITIES;
	info->max_resources = UINT32_MAX;
	info->max_resource_bytes = I915_MAX_RESOURCE_BYTES;
	memset(info->driver_name, 0, sizeof(info->driver_name));
	memcpy(info->driver_name, "i915", 5U);

	/* Succeeded: userspace can identify the native backend. */
	return 0;
}

/* Allocates a storage object and binds it into the session's address space. */
static int
i915_resource_create(
	void *opaque,
	void *private_session,
	const struct gpu_resource_create *request,
	void **result)
{
	struct i915_device *device;
	struct i915_session *session;
	struct i915_gem_object *object;
	int error;

	/* A failed creation stays invisible to the core's resource table. */
	device = opaque;
	session = private_session;
	*result = NULL;

	/* Only plain storage exists; other usages have no backend meaning yet. */
	if (request->usage != GPU_RESOURCE_USAGE_STORAGE)
		return EINVAL;

	/* Allocation and binding are serialized with every other controller operation. */
	mutex_lock(&device->mutex);

	/* A quarantined session may not add hardware-visible state. */
	if (session->quarantined != 0U || device->failed != 0U) {
		mutex_unlock(&device->mutex);
		return ENODEV;
	}

	error = drv_i915_gem_create(device, request->bytes, &object);
	if (error != 0) {
		mutex_unlock(&device->mutex);
		return error;
	}

	/* The object becomes addressable by this session's context only. */
	error = drv_i915_gem_bind_vm(session->vm, object);
	if (error != 0) {
		drv_i915_gem_destroy(device, object);
		mutex_unlock(&device->mutex);
		return error;
	}

	/* The public handle lets a native stream name this object in a relocation. */
	object->handle = request->handle;
	object->session_next = session->objects;
	session->objects = object;

	/* The slot numbers objects per session for the log line the harness parses. */
	object->slot = session->next_slot;
	session->next_slot++;
	session->resources++;
	kern_logf("i915: resource session=%u slot=%u bytes=%llu phys=0x%llx va=0x%llx handle=%llu\n",
		session->identifier,
		object->slot,
		(unsigned long long)object->bytes,
		(unsigned long long)object->run.paddr,
		(unsigned long long)object->va,
		(unsigned long long)object->handle);

	mutex_unlock(&device->mutex);

	*result = object;

	/* Succeeded: the session owns zeroed storage the GPU can address. */
	return 0;
}

/* Releases a storage object; a quarantined session's object is retained. */
static void
i915_resource_destroy(
	void *opaque,
	void *private_session,
	void *private_object)
{
	struct i915_device *device;
	struct i915_session *session;
	struct i915_gem_object *object;
	struct i915_gem_object **position;

	device = opaque;
	session = private_session;
	object = private_object;

	/* The core only destroys an object it created, so a null handle is a bug, not a case. */
	if (object == NULL)
		return;

	/* Unbinding and freeing are serialized with submissions of the same session. */
	mutex_lock(&device->mutex);

	/* The object leaves the session's handle list whatever happens to its backing. */
	position = &session->objects;
	while (*position != NULL && *position != object)
		position = &(*position)->session_next;
	if (*position == object)
		*position = object->session_next;

	/* The GPU may still name a quarantined object; the checked reset frees it. */
	if (session->quarantined != 0U)
		object->quarantined = 1U;
	else
		drv_i915_gem_unbind_vm(object);

	session->resources--;
	drv_i915_gem_destroy(device, object);

	mutex_unlock(&device->mutex);
}

/* Copies bytes out of a storage object. */
static int
i915_resource_read(
	void *opaque,
	void *private_session,
	void *private_object,
	uint64_t offset,
	void *buffer,
	uint32_t bytes)
{
	struct i915_device *device;
	struct i915_gem_object *object;
	int error;

	/* The core resolved the object; the session is implied by its handle. */
	device = opaque;
	(void)private_session;
	object = private_object;

	/* The copy is serialized so no concurrent destroy can free the backing. */
	mutex_lock(&device->mutex);

	error = drv_i915_gem_read(object, offset, buffer, bytes);

	mutex_unlock(&device->mutex);

	/* Reports a range outside the object. */
	if (error != 0)
		return error;

	/* Succeeded: the kernel buffer holds the object bytes. */
	return 0;
}

/* Copies bytes into a storage object. */
static int
i915_resource_write(
	void *opaque,
	void *private_session,
	void *private_object,
	uint64_t offset,
	const void *buffer,
	uint32_t bytes)
{
	struct i915_device *device;
	struct i915_gem_object *object;
	int error;

	/* The core already copied the caller's bytes into a bounded kernel buffer. */
	device = opaque;
	(void)private_session;
	object = private_object;

	/* The copy is serialized with submissions that may read the object. */
	mutex_lock(&device->mutex);

	error = drv_i915_gem_write(object, offset, buffer, bytes);

	mutex_unlock(&device->mutex);

	/* Reports a range outside the object. */
	if (error != 0)
		return error;

	/* Succeeded: the object holds the new bytes for the GPU's next access. */
	return 0;
}

/* Allocates host storage for a Vulkan memory blob and binds it to the session. */
static int
i915_blob_create(
	void *opaque,
	void *private_session,
	const struct gpu_blob_create *request,
	void **result,
	uint32_t *resource_id)
{
	struct i915_device *device;
	struct i915_session *session;
	struct i915_gem_object *object;
	int error;

	/* A failed creation stays invisible to the core resource table. */
	device = opaque;
	session = private_session;
	*result = NULL;
	*resource_id = 0U;

	/* Only mappable, optionally shareable, host storage is backed. */
	if (request->flags == 0U)
		return EOPNOTSUPP;
	if ((request->flags & ~(GPU_BLOB_MAPPABLE | GPU_BLOB_SHAREABLE)) != 0U)
		return EOPNOTSUPP;

	/* Allocation and binding are serialized with every other controller operation. */
	mutex_lock(&device->mutex);

	/* A quarantined session may not add hardware-visible state. */
	if (session->quarantined != 0U || device->failed != 0U) {
		mutex_unlock(&device->mutex);
		return ENODEV;
	}

	error = drv_i915_gem_create(device, request->bytes, &object);
	if (error != 0) {
		mutex_unlock(&device->mutex);
		return error;
	}

	/* The blob becomes addressable by this session's context only. */
	error = drv_i915_gem_bind_vm(session->vm, object);
	if (error != 0) {
		drv_i915_gem_destroy(device, object);
		mutex_unlock(&device->mutex);
		return error;
	}

	/* The public handle lets the executor name this blob in its commands. */
	object->handle = request->handle;
	object->session_next = session->objects;
	session->objects = object;
	object->slot = session->next_slot;
	session->next_slot++;
	session->resources++;

	mutex_unlock(&device->mutex);

	/* The core tracks the blob as a resource the executor and map reference. */
	*result = object;
	*resource_id = object->slot;

	/* Succeeded: the session owns zeroed storage the GPU can address. */
	return 0;
}

/* Exposes a blob's system-RAM backing as a CPU mapping for the client. */
static int
i915_resource_map(
	void *opaque,
	void *private_session,
	void *object,
	struct drv_gpu_mapping *mapping)
{
	struct i915_device *device;
	struct i915_gem_object *backing;
	void *address;

	/* The mapping is of the blob's own pages, independent of the session. */
	device = opaque;
	(void)private_session;
	backing = object;

	mutex_lock(&device->mutex);

	/* Managed RAM is direct-mapped, so the blob has a kernel alias. */
	address = kern_pmem_to_kernel(backing->run.paddr);
	if (address == NULL) {
		mutex_unlock(&device->mutex);
		return EFAULT;
	}

	/* The client receives the exact page-aligned extent of the blob. */
	memset(mapping, 0, sizeof(*mapping));
	mapping->physical = (uint64_t)backing->run.paddr;
	mapping->address = address;
	mapping->bytes = backing->bytes;
	mapping->attributes = 0U;

	mutex_unlock(&device->mutex);

	/* Succeeded: the core can map and pin this blob's storage. */
	return 0;
}

/* Reports the executor capset so libvulkan accepts the node as a backend. */
/*
 * Resolves the reply target a Vulkan command stream selects up front.  The
 * stream opens with vkSetReplyCommandStreamMESA (opcode 178) naming a session
 * blob by resource id; its kernel alias is where the executor writes replies,
 * since the submit ioctl returns none inline.  A stream without it replies
 * nowhere.
 */
static void *
i915_vk_command_reply(
	struct i915_session *session,
	const void *buffer,
	uint32_t bytes,
	size_t *capacity)
{
	const uint8_t *stream;
	struct i915_gem_object *object;
	uint32_t opcode;
	uint32_t resource_id;
	uint64_t offset;

	*capacity = 0U;
	stream = buffer;

	/* The selector is a fixed 36-byte record: [178][flag][present][id][off][cap]. */
	if (bytes < 36U)
		return NULL;
	memcpy(&opcode, stream, 4U);
	if (opcode != 178U)
		return NULL;
	memcpy(&resource_id, stream + 16U, 4U);
	memcpy(&offset, stream + 20U, 8U);

	/* The named blob is one of the session's resources, keyed by its slot. */
	for (object = session->objects; object != NULL; object = object->session_next) {
		if (object->slot != resource_id)
			continue;
		if (offset >= object->bytes)
			return NULL;
		*capacity = (size_t)(object->bytes - offset);
		return (uint8_t *)kern_pmem_to_kernel(object->run.paddr) + offset;
	}
	return NULL;
}

static int
i915_get_capset(
	void *opaque,
	void *private_session,
	struct gpu_capset *capset)
{
	struct i915_device *device;

	/* The capset belongs to the device executor, not to any session. */
	(void)private_session;
	device = opaque;

	/* A device without the executor reports no capset. */
	if (device->vk == NULL)
		return ENOTSUP;

	/* The reply must fit the requested capacity. */
	if (device->vk->capset_bytes > capset->capacity)
		return EINVAL;

	/* The executor speaks one capset the client reads before it opens. */
	capset->bytes = device->vk->capset_bytes;
	memcpy(capset->data, device->vk->capset, device->vk->capset_bytes);

	/* Succeeded: the client can open the node as a Vulkan backend. */
	return 0;
}

/* Accepts a synchronous native stream; receipt does not wait for execution. */
static int
i915_command(
	void *opaque,
	void *private_session,
	const void *buffer,
	uint32_t bytes)
{
	struct i915_device *device;
	struct i915_session *session;
	uint32_t magic;
	int error;

	device = opaque;
	session = private_session;

	/* A stream that is not a native batch is a Vulkan command for the executor. */
	if (session->vk != NULL) {
		if (bytes >= 4U) {
			memcpy(&magic, buffer, 4U);
			if (magic != I915_STREAM_MAGIC) {
				void *reply_base;
				size_t reply_bytes;

				reply_base = i915_vk_command_reply(session, buffer, bytes, &reply_bytes);
				return drv_i915_vk_command(session->vk, buffer, bytes,
					reply_base, reply_base != NULL ? &reply_bytes : NULL);
			}
		}
	}

	/* The stream is validated, copied and queued under the controller mutex. */
	mutex_lock(&device->mutex);

	error = i915_submit_stream(device, session, buffer, bytes, NULL);

	mutex_unlock(&device->mutex);

	/* Reports a malformed stream or a full queue. */
	if (error != 0)
		return error;

	/* Succeeded: the engine will run the batch; no completion is reported. */
	return 0;
}

/* Accepts an asynchronous native stream or an empty marker with a completion. */
static int
i915_command_submit(
	void *opaque,
	void *private_session,
	const void *buffer,
	uint32_t bytes,
	uint32_t flags,
	uint32_t timeline,
	struct drv_gpu_completion *completion)
{
	struct i915_device *device;
	struct i915_session *session;
	uint32_t magic;
	int error;

	/* Every accepted submission owes exactly one completion. */
	device = opaque;
	session = private_session;
	if (completion == NULL)
		return EINVAL;

	/* A non-native stream is a Vulkan command; it completes as it decodes. */
	if (session->vk != NULL) {
		if (bytes >= 4U) {
			memcpy(&magic, buffer, 4U);
			if (magic != I915_STREAM_MAGIC) {
				void *reply_base;
				size_t reply_bytes;

				reply_base = i915_vk_command_reply(session, buffer, bytes, &reply_bytes);
				error = drv_i915_vk_command(session->vk, buffer, bytes,
					reply_base, reply_base != NULL ? &reply_bytes : NULL);
				if (error != 0)
					return error;
				drv_gpu_complete(completion, 0);
				return 0;
			}
		}
	}

	/* Streams and markers share the serialized submission path. */
	mutex_lock(&device->mutex);

	/* An empty stream is a context marker on the selected timeline. */
	if (bytes == 0U) {
		/* The flag that names the marker is the core's; the timeline picks the engine. */
		(void)flags;
		error = i915_submit_marker(device, session, timeline, completion);
	} else {
		error = i915_submit_stream(device, session, buffer, bytes, completion);
	}

	mutex_unlock(&device->mutex);

	/* A refused submission retains no callback. */
	if (error != 0)
		return error;

	/* Succeeded: the completion is delivered when the request retires or fails. */
	return 0;
}

/* Waits until every request of the session retired and delivered its callback. */
static void
i915_command_drain(
	void *opaque,
	void *private_session)
{
	struct i915_device *device;
	struct i915_session *session;
	uint64_t observed;
	unsigned long irq;

	device = opaque;
	session = private_session;

	/* The pending count is decremented only after each callback returned. */
	irq = spin_lock_irqsave(&device->irq_lock);

	while (session->pending_requests != 0U) {
		observed = waitq_sequence(&device->retire_waitq);
		(void)waitq_sleep(&device->retire_waitq, &device->irq_lock, observed, 0U, 0U);
	}

	spin_unlock_irqrestore(&device->irq_lock, irq);
}

/* Reserves a request slot and its callback before any native submission. */
static int
i915_job_reserve(
	void *opaque,
	void *private_session,
	uint32_t timeline,
	struct drv_gpu_completion *completion,
	void **reservation)
{
	struct i915_device *device;
	struct i915_session *session;
	struct i915_engine *engine;
	struct i915_request *request;
	unsigned long irq;
	int error;

	/* Nothing is retained for a refused reservation. */
	device = opaque;
	session = private_session;
	if (completion == NULL || reservation == NULL)
		return EINVAL;
	*reservation = NULL;

	/* Jobs name an engine through a nonzero timeline. */
	if (timeline == I915_TIMELINE_DEFAULT)
		return EINVAL;
	error = i915_engine_for_timeline(device, timeline, &engine);
	if (error != 0)
		return error;

	/* The slot is taken under the IRQ lock like every queue change. */
	irq = spin_lock_irqsave(&device->irq_lock);

	/* A faulted device or a stopping session admits no new work. */
	if (device->failed != 0U || session->quarantined != 0U || session->stopping != 0U) {
		spin_unlock_irqrestore(&device->irq_lock, irq);
		return ENODEV;
	}

	error = drv_i915_request_alloc(engine, session, completion, &request);
	if (error != 0) {
		spin_unlock_irqrestore(&device->irq_lock, irq);
		return error;
	}

	/* A reserved slot holds the callback but is not queued until commit. */
	request->context = &session->contexts[engine->index];
	request->state = I915_REQUEST_RESERVED;
	request->supervised = 1U;
	session->pending_requests++;
	*reservation = request;

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* Succeeded: the framework supervises this reservation until commit or cancel. */
	return 0;
}

/* Publishes a reserved slot as a marker request. */
static int
i915_job_commit(
	void *opaque,
	void *private_session,
	void *reservation,
	struct drv_gpu_completion *completion)
{
	struct i915_device *device;
	struct i915_session *session;
	struct i915_engine *engine;
	struct i915_request *request;
	unsigned long irq;

	device = opaque;
	session = private_session;
	if (reservation == NULL || completion == NULL)
		return EINVAL;

	/* The token is resolved against the engine slots without dereferencing it first. */
	irq = spin_lock_irqsave(&device->irq_lock);

	request = NULL;
	engine = &device->engines[I915_ENGINE_RCS0];
	request = i915_reservation(engine, reservation, completion);
	if (request == NULL) {
		engine = &device->engines[I915_ENGINE_BCS0];
		request = i915_reservation(engine, reservation, completion);
	}

	/* A stale or already published token cannot enter the queue. */
	if (request == NULL || request->state != I915_REQUEST_RESERVED || request->session != session) {
		spin_unlock_irqrestore(&device->irq_lock, irq);
		return ESTALE;
	}

	/* No commit follows a fault or a stop. */
	if (device->failed != 0U || session->quarantined != 0U) {
		spin_unlock_irqrestore(&device->irq_lock, irq);
		return ENODEV;
	}

	/* The marker runs like any request: it writes its seqno and raises the interrupt. */
	request->state = I915_REQUEST_QUEUED;
	session->pending_requests--;
	drv_i915_request_queue(engine, request);
	drv_i915_request_kick(engine);

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* Succeeded: the engine reports the marker through the reserved callback. */
	return 0;
}

/* Withdraws a reservation; a fault cancel keeps the callback for the framework. */
static int
i915_job_cancel(
	void *opaque,
	void *private_session,
	void *reservation,
	struct drv_gpu_completion *completion,
	unsigned fault)
{
	struct i915_device *device;
	struct i915_session *session;
	struct i915_engine *engine;
	struct i915_request *request;
	unsigned long irq;

	device = opaque;
	session = private_session;
	if (reservation == NULL || completion == NULL || fault > 1U)
		return EINVAL;

	/* The token must name a slot that still holds this callback. */
	irq = spin_lock_irqsave(&device->irq_lock);

	engine = &device->engines[I915_ENGINE_RCS0];
	request = i915_reservation(engine, reservation, completion);
	if (request == NULL) {
		engine = &device->engines[I915_ENGINE_BCS0];
		request = i915_reservation(engine, reservation, completion);
	}
	if (request == NULL || request->session != session) {
		spin_unlock_irqrestore(&device->irq_lock, irq);
		return ESTALE;
	}

	/* Definite rollback needs an unpublished slot; a fault may also end queued or active work. */
	if (request->state != I915_REQUEST_RESERVED && (fault == 0U || request->state == I915_REQUEST_FREE)) {
		spin_unlock_irqrestore(&device->irq_lock, irq);
		return ESTALE;
	}

	/* An uncertain outcome keeps the slot and its callback until the session is stopped or isolated. */
	if (fault != 0U) {
		if (request->state == I915_REQUEST_RESERVED)
			request->state = I915_REQUEST_RETAINED;
		spin_unlock_irqrestore(&device->irq_lock, irq);
		return 0;
	}

	/* Definite nonacceptance returns the slot with no future callback. */
	session->pending_requests--;
	request->completion = NULL;
	drv_i915_request_release(engine, request);

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* The freed slot is capacity for the next reservation. */
	if (device->gpu != NULL)
		drv_gpu_capacity_changed(device->gpu);

	/* Succeeded: the reservation is gone without inventing a completion. */
	return 0;
}

/* Reports the free request slots of the engine a timeline names. */
static int
i915_job_capacity(
	void *opaque,
	void *private_session,
	uint32_t timeline,
	unsigned *available)
{
	struct i915_device *device;
	struct i915_engine *engine;
	unsigned index;
	unsigned long irq;
	int error;

	/* A refused snapshot reports no capacity. */
	device = opaque;
	(void)private_session;
	if (available == NULL)
		return EINVAL;
	*available = 0U;
	if (timeline == I915_TIMELINE_DEFAULT)
		return EINVAL;
	error = i915_engine_for_timeline(device, timeline, &engine);
	if (error != 0)
		return error;

	/* The snapshot counts free slots under the IRQ lock without sleeping. */
	irq = spin_lock_irqsave(&device->irq_lock);

	if (device->failed != 0U) {
		spin_unlock_irqrestore(&device->irq_lock, irq);
		return ENODEV;
	}

	for (index = 0U; index < I915_REQUEST_SLOTS; index++) {
		if (engine->slots[index].state == I915_REQUEST_FREE)
			(*available)++;
	}

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* Succeeded: the framework combines this with its own admission ledger. */
	return 0;
}

/* Marks a session as stopping; the framework already refuses new admission. */
static int
i915_stop_begin(
	void *opaque,
	void *private_session,
	int error)
{
	struct i915_device *device;
	struct i915_session *session;
	unsigned long irq;

	/* The reason is the framework's; the backend only records the transition. */
	device = opaque;
	session = private_session;
	(void)error;

	irq = spin_lock_irqsave(&device->irq_lock);

	session->stopping = 1U;

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* Succeeded: polls report whether the session's native work has retired. */
	return 0;
}

/* Reports whether every request of a stopping session has retired. */
static int
i915_stop_poll(
	void *opaque,
	void *private_session)
{
	struct i915_device *device;
	struct i915_session *session;
	unsigned pending;
	unsigned long irq;

	device = opaque;
	session = private_session;

	/* The count covers queued, active, reserved and callback-in-progress requests. */
	irq = spin_lock_irqsave(&device->irq_lock);

	pending = session->pending_requests;
	if (session->stopping == 0U) {
		spin_unlock_irqrestore(&device->irq_lock, irq);
		return EINVAL;
	}

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* Work still on the engine keeps the session unconfirmed. */
	if (pending != 0U)
		return EAGAIN;

	/* Succeeded: no native work of this session remains. */
	return 0;
}

/* Ends every request on both engines with the reported error and refuses new work. */
static void
i915_fault(
	void *opaque,
	int error)
{
	struct i915_device *device;
	struct i915_request *retired;
	unsigned index;
	unsigned long irq;

	device = opaque;

	/* New sessions, submissions and reservations are refused from now on. */
	irq = spin_lock_irqsave(&device->irq_lock);

	device->failed = 1U;

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* Each engine's requests are failed and their callbacks delivered outside the lock. */
	for (index = 0U; index < I915_ENGINE_COUNT; index++) {
		retired = NULL;
		irq = spin_lock_irqsave(&device->irq_lock);

		drv_i915_request_fail(&device->engines[index], NULL, error, &retired);

		spin_unlock_irqrestore(&device->irq_lock, irq);

		drv_i915_request_complete_list(&device->engines[index], retired);
	}

	kern_logf("i915: device fault %d; objects retained for checked reset\n", error);
}

/* Reinitializes the hardware once no session owns it and frees quarantined state. */
static int
i915_reset_device(
	void *opaque)
{
	struct i915_device *device;
	struct i915_gem_object *object;
	struct i915_gem_object *next;
	struct i915_ppgtt *vm;
	unsigned index;
	int error;

	device = opaque;

	/* The reset runs under the controller mutex; no session can race it. */
	mutex_lock(&device->mutex);

	/* A full graphics reset ends every access before quarantined memory is freed. */
	error = drv_i915_gt_reset(device);
	if (error != 0) {
		mutex_unlock(&device->mutex);
		return error;
	}

	/* Each engine is reprogrammed from scratch with an empty queue. */
	for (index = 0U; index < I915_ENGINE_COUNT; index++) {
		error = drv_i915_engine_reset(&device->engines[index]);
		if (error != 0) {
			mutex_unlock(&device->mutex);
			return error;
		}
	}

	/* Quarantined objects are unmapped and freed now that the GPU cannot touch them. */
	object = device->objects;
	while (object != NULL) {
		next = object->next;
		if (object->quarantined != 0U) {
			drv_i915_gem_unbind_ggtt(device, object);
			if (object->vm != NULL)
				drv_i915_gem_unbind_vm(object);
			object->quarantined = 0U;
			drv_i915_gem_destroy(device, object);
		}

		object = next;
	}

	device->quarantined_objects = 0U;

	/* Quarantined address spaces follow their objects. */
	while (device->quarantined_vms != NULL) {
		vm = device->quarantined_vms;
		device->quarantined_vms = vm->next;
		drv_i915_ppgtt_destroy(vm);
		kern_free(vm);
	}

	device->failed = 0U;
	kern_logf("i915: checked reset complete\n");

	mutex_unlock(&device->mutex);

	/* Succeeded: fresh sessions may open on the reinitialized device. */
	return 0;
}

/* Quarantines one session: its work ends with EIO and a stuck engine is reset. */
static int
i915_isolate(
	void *opaque,
	void *private_session)
{
	struct i915_device *device;
	struct i915_session *session;
	unsigned index;
	unsigned long irq;
	int error;

	device = opaque;
	session = private_session;

	/* Later destroy and close calls keep this session's objects for the checked reset. */
	irq = spin_lock_irqsave(&device->irq_lock);

	session->quarantined = 1U;

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* Each engine fails the session's requests and resets itself if it was running one. */
	for (index = 0U; index < I915_ENGINE_COUNT; index++) {
		error = drv_i915_engine_recover(&device->engines[index], session, EIO);
		if (error != 0)
			return error;
	}

	kern_logf("i915: session %u quarantined; objects retained for reset\n", session->identifier);

	/* Succeeded: other sessions continue on both engines. */
	return 0;
}

/* Releases every context a session created; the caller holds the mutex. */
static void
i915_session_contexts_destroy(
	struct i915_device *device,
	struct i915_session *session)
{
	unsigned index;

	/* A context that was never created has no objects to release. */
	for (index = 0U; index < I915_ENGINE_COUNT; index++) {
		if (session->contexts[index].created != 0U)
			drv_i915_lrc_destroy(device, &session->contexts[index]);
	}
}

/* Releases the session's batch pool; quarantined sessions keep the objects. */
static void
i915_session_batches_destroy(
	struct i915_device *device,
	struct i915_session *session)
{
	struct i915_gem_object *object;

	/* Every batch has retired because the framework drained the session first. */
	while (session->batches != NULL) {
		object = session->batches;
		session->batches = object->session_next;
		if (session->quarantined != 0U) {
			object->quarantined = 1U;
		} else {
			drv_i915_gem_unbind_vm(object);
		}

		drv_i915_gem_destroy(device, object);
	}

	session->batch_count = 0U;
}

/* Maps a submission timeline to an engine. */
static int
i915_engine_for_timeline(
	struct i915_device *device,
	uint32_t timeline,
	struct i915_engine **engine)
{
	/* Zero and one name the copy engine; two names the render engine. */
	*engine = NULL;
	if (timeline == I915_TIMELINE_DEFAULT || timeline == I915_TIMELINE_BCS0) {
		*engine = &device->engines[I915_ENGINE_BCS0];
	} else if (timeline == I915_TIMELINE_RCS0) {
		*engine = &device->engines[I915_ENGINE_RCS0];
	} else {
		return EINVAL;
	}

	/* Succeeded: the engine is initialized when the device is published. */
	return 0;
}

/* Validates a stream, builds its batch object and queues the request; the caller holds the mutex. */
static int
i915_submit_stream(
	struct i915_device *device,
	struct i915_session *session,
	const void *buffer,
	uint32_t bytes,
	struct drv_gpu_completion *completion)
{
	struct i915_stream stream;
	struct i915_engine *engine;
	struct i915_gem_object *batch;
	struct i915_gem_object *target;
	struct i915_request *request;
	uint32_t *dwords;
	uint32_t index;
	uint32_t dword_offset;
	uint64_t handle;
	unsigned long irq;
	int error;

	/* Shape errors are reported before any object is touched. */
	error = drv_i915_stream_parse(buffer, bytes, &stream);
	if (error != 0)
		return error;

	/* A quarantined session or a faulted device runs nothing new. */
	if (session->quarantined != 0U || device->failed != 0U)
		return ENODEV;

	engine = &device->engines[I915_ENGINE_BCS0];
	if (stream.engine == I915_STREAM_ENGINE_RCS0)
		engine = &device->engines[I915_ENGINE_RCS0];

	/* The batch object is taken from the session pool or created. */
	error = i915_batch_acquire(device, session, stream.batch_dwords * 4U, &batch);
	if (error != 0)
		return error;

	/* The batch is copied, never executed from the caller's buffer. */
	dwords = batch->address;
	memcpy(dwords, stream.batch, (size_t)stream.batch_dwords * 4U);

	/* Each relocation writes the object's 64-bit address into the copy. */
	for (index = 0U; index < stream.relocation_count; index++) {
		memcpy(&dword_offset, stream.relocations + index * I915_STREAM_RELOCATION_BYTES, 4U);
		memcpy(&handle, stream.relocations + index * I915_STREAM_RELOCATION_BYTES + 8U, 8U);
		target = i915_session_object(session, handle);
		if (target == NULL) {
			batch->busy = 0U;
			return EINVAL;
		}

		dwords[dword_offset] = (uint32_t)target->va;
		dwords[dword_offset + 1U] = (uint32_t)(target->va >> 32);
	}

	kern_io_write_barrier();

	/* The request takes a slot, names the batch and joins the engine queue. */
	irq = spin_lock_irqsave(&device->irq_lock);

	error = drv_i915_request_alloc(engine, session, completion, &request);
	if (error != 0) {
		batch->busy = 0U;
		spin_unlock_irqrestore(&device->irq_lock, irq);
		return error;
	}

	request->context = &session->contexts[engine->index];
	request->batch = batch;
	request->batch_va = batch->va;
	drv_i915_request_queue(engine, request);
	drv_i915_request_kick(engine);

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* Succeeded: the batch runs when the engine reaches it. */
	return 0;
}

/* Queues a marker request without a batch; the caller holds the mutex. */
static int
i915_submit_marker(
	struct i915_device *device,
	struct i915_session *session,
	uint32_t timeline,
	struct drv_gpu_completion *completion)
{
	struct i915_engine *engine;
	struct i915_request *request;
	unsigned long irq;
	int error;

	/* The timeline names the engine whose ordering the marker observes. */
	error = i915_engine_for_timeline(device, timeline, &engine);
	if (error != 0)
		return error;
	if (session->quarantined != 0U || device->failed != 0U)
		return ENODEV;

	irq = spin_lock_irqsave(&device->irq_lock);

	error = drv_i915_request_alloc(engine, session, completion, &request);
	if (error != 0) {
		spin_unlock_irqrestore(&device->irq_lock, irq);
		return error;
	}

	/* A marker only writes its seqno and raises the interrupt. */
	request->context = &session->contexts[engine->index];
	drv_i915_request_queue(engine, request);
	drv_i915_request_kick(engine);

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* Succeeded: the completion follows every earlier request on this engine. */
	return 0;
}

/* Finds or creates a free batch object of at least the given size; the caller holds the mutex. */
static int
i915_batch_acquire(
	struct i915_device *device,
	struct i915_session *session,
	uint32_t bytes,
	struct i915_gem_object **result)
{
	struct i915_gem_object *object;
	unsigned long irq;
	int error;

	/* A pooled object is reused when it is idle and large enough. */
	*result = NULL;
	irq = spin_lock_irqsave(&device->irq_lock);

	object = session->batches;
	while (object != NULL) {
		/* The busy flag is cleared by retirement under the IRQ lock. */
		if (object->busy == 0U && object->bytes >= bytes)
			break;

		object = object->session_next;
	}
	if (object != NULL)
		object->busy = 1U;

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* A reused object is handed back at once. */
	if (object != NULL) {
		*result = object;
		return 0;
	}

	/* The pool is bounded by the request slots so it can never grow without limit. */
	if (session->batch_count >= I915_BATCH_POOL_MAX)
		return EAGAIN;

	/* A new batch object is bound into the session's space like a resource. */
	error = drv_i915_gem_create(device, bytes, &object);
	if (error != 0)
		return error;
	error = drv_i915_gem_bind_vm(session->vm, object);
	if (error != 0) {
		drv_i915_gem_destroy(device, object);
		return error;
	}

	object->busy = 1U;
	object->session_next = session->batches;
	session->batches = object;
	session->batch_count++;
	*result = object;

	/* Succeeded: the caller fills the batch before queuing it. */
	return 0;
}

/* Resolves a public resource handle to one of the session's objects. */
static struct i915_gem_object *
i915_session_object(
	struct i915_session *session,
	uint64_t handle)
{
	struct i915_gem_object *object;

	/* The list is short; a scan is enough for relocation resolution. */
	object = session->objects;
	while (object != NULL && object->handle != handle)
		object = object->session_next;

	return object;
}

/* Resolves a reservation token to an engine slot holding the same callback. */
static struct i915_request *
i915_reservation(
	struct i915_engine *engine,
	void *reservation,
	struct drv_gpu_completion *completion)
{
	unsigned index;

	/* Foreign addresses are compared only, never dereferenced. */
	for (index = 0U; index < I915_REQUEST_SLOTS; index++) {
		if (reservation == &engine->slots[index] && engine->slots[index].completion == completion)
			return &engine->slots[index];
	}

	return NULL;
}
