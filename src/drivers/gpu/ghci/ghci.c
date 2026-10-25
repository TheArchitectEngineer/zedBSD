/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GHCI device: what every GHCI GPU has in common, whatever its bus.
 *
 * The start identifies the GPU from its registers, resets it, sets up its
 * memory and ring, finds its display controller and registers the device
 * with the GPU core, which publishes /dev/gpuN named
 * "ghci:<vendor>:<device>".  The sessions of the device allocate GPU
 * memory, copy to and from it, map it, and on a test kernel submit
 * checked native packets.
 */

#include <stddef.h>
#include <stdint.h>

#include <drivers/gpu/gpu.h>
#include <drivers/gpu/gpu-share.h>
#include <kern/clock.h>
#include <kern/cred.h>
#include <kern/device-io.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/sched.h>
#include <uapi/errno.h>
#include <uapi/gpu.h>
#include <uapi/gpu-allocation.h>
#include <uapi/gpu-display.h>
#include <uapi/gpu-op.h>

#include "ghci.h"
#include "hcr.h"
#include "packets.h"
#include "vk/vk-node.h"

/* The major version of GHCI the driver knows. */
#define KNOWN_MAJOR			1U

/* The smallest memory line the driver aligns to (2^5 bytes, the smallest GHCI defines). */
#define LINE_LOG2_MIN			5U

/* CPFW_STATUS from the CPFW capability's registers, whose first is CPFW_CTRL. */
#define CPFW_STATUS_OFFSET		(GHCI_CPFW_STATUS - GHCI_CPFW_CTRL)

/*
 * The capabilities of every device: storage, blobs, transfers, processor
 * mappings, and the sharing of memory between its sessions.
 */
#define BASE_CAPABILITIES		(GPU_CAP_RESOURCE | GPU_CAP_BLOB | GPU_CAP_TRANSFER | GPU_CAP_MAPPING | \
					 GPU_CAP_SHARE | GPU_CAP_ALLOCATION_SHARE)

/* The blob flags a device takes: mapping and sharing within the device (not with another device). */
#define BLOB_FLAGS			(GPU_BLOB_MAPPABLE | GPU_BLOB_SHAREABLE)

static int identify(struct ghci_device *device);
static void identify_memory(struct ghci_device *device);
static void name_device(struct ghci_device *device);
static void put_fourcc(char *out, uint32_t fourcc);
static int reset_hardware(struct ghci_device *device);
static int register_device(struct ghci_device *device);
static int tag_take(struct ghci_device *device, uint32_t *tag);
static void tag_give(struct ghci_device *device, uint32_t tag);
static int gpu_open(void *private_data, void **result);
static void gpu_close(void *private_data, void *opaque);
static int gpu_get_info(void *private_data, void *opaque, struct gpu_info *info);
static int gpu_resource_create(void *private_data, void *opaque, const struct gpu_resource_create *request, void **result);
static void gpu_resource_destroy(void *private_data, void *opaque, void *object);
static int gpu_blob_create(void *private_data, void *opaque, const struct gpu_blob_create *request, void **result, uint32_t *resource_id);
static int gpu_resource_read(void *private_data, void *opaque, void *object, uint64_t offset, void *bytes, uint32_t length);
static int gpu_resource_write(void *private_data, void *opaque, void *object, uint64_t offset, const void *bytes, uint32_t length);
static int gpu_resource_map(void *private_data, void *opaque, void *object, struct drv_gpu_mapping *mapping);
static int gpu_command(void *private_data, void *opaque, const void *stream, uint32_t bytes);
static int allocation_make(struct ghci_device *device, struct ghci_session *session, enum ghci_memory_use use, uint64_t bytes, int blob, struct ghci_allocation **result);
static int blob_of_memory(struct ghci_device *device, struct ghci_session *session, uint64_t identity, struct ghci_allocation **result);
static void session_give_back(struct ghci_device *device, struct ghci_session *session);
static int allocation_range(const struct ghci_allocation *allocation, uint64_t offset, uint32_t length);
static void allocation_unlist(struct ghci_session *session, struct ghci_allocation *allocation);

/*
 * Starts a device whose bus fields are filled.
 */
int
ghci_device_start(
	struct ghci_device *device)
{
	int error;

	/* The locks and wait queues the rest of the start already uses. */
	error = mutex_init(&device->lock, LOCK_RANK_DEVICE, "ghci device");
	if (error != 0)
		return error;

	error = mutex_init(&device->cp_lock, LOCK_RANK_DEVICE, "ghci ring");
	if (error != 0)
		return error;

	spin_init(&device->irq_lock, LOCK_RANK_DEVICE, "ghci interrupt");
	waitq_init(&device->fence_wake, "ghci fence");
	waitq_init(&device->service_wake, "ghci service");
	waitq_init(&device->display.flip_wake, "ghci flip");

	/* Reads what the GPU is and has; a window that is not GHCI 1 is left alone. */
	error = identify(device);
	if (error != 0)
		return error;

	/* Resets the GPU and waits for its memory. */
	error = reset_hardware(device);
	if (error != 0)
		return error;

	/* Sets up the allocators of local memory. */
	error = ghci_memory_init(device);
	if (error != 0)
		return error;

	/* Starts the ring, the fences and the interrupt; the processor then runs. */
	error = ghci_cp_start(device);
	if (error != 0) {
		ghci_memory_fini(device);
		return error;
	}

	/* Starts the thread that recovers from faults and gives deferred memory back. */
	error = ghci_service_start(device);
	if (error != 0) {
		ghci_cp_stop(device);
		ghci_memory_fini(device);
		return error;
	}

	/*
	 * The Vulkan executor, when the command processor has firmware that
	 * decodes gpu-ops and the GPU has a shader unit.  A GPU without one
	 * still serves its memory, its native commands and its display.
	 */
	error = ghci_vk_engine_start(device);
	if (error != 0)
		kern_logf("ghci: %s: no Vulkan executor (%d)\n", device->name, error);

	/* Finds the display controller, if the GPU has one, and starts it. */
	error = ghci_display_attach(device);
	if (error != 0)
		kern_logf("ghci: %s: display controller not started (%d)\n", device->name, error);

	/* Publishes the device. */
	error = register_device(device);
	if (error != 0) {
		ghci_display_detach(device);
		ghci_cp_stop(device);
		ghci_service_stop(device);
		ghci_vk_engine_stop(device);
		ghci_memory_fini(device);
		return error;
	}

	/* Says what the device is: its version, capabilities, memory, display and executor. */
	kern_logf("ghci: %s: ready (GHCI %u.%u, caps %08x, local %u MiB, window %u MiB, display %u, vulkan %u)\n",
	    device->name,
	    (unsigned)(device->version >> 16),
	    (unsigned)(device->version & 0xffffU),
	    (unsigned)device->caps,
	    (unsigned)(device->local_size >> 20),
	    (unsigned)(device->local_window >> 20),
	    (unsigned)device->display.present,
	    (unsigned)(device->vk != NULL));

	/* Succeeded: the device is published. */
	return 0;
}

/*
 * Stops a started device for the detach.
 */
int
ghci_device_stop(
	struct ghci_device *device)
{
	int error;

	/* Refuses while a session is open; new opens are refused from here on. */
	mutex_lock(&device->lock);

	if (device->session_count != 0U) {
		mutex_unlock(&device->lock);
		return EBUSY;
	}

	device->withdrawing = 1;

	mutex_unlock(&device->lock);

	/* Withdraws the node; a session opened in between makes the detach wait for its close. */
	error = drv_gpu_unregister(device->gpu);
	if (error != 0)
		return error;

	/* Quiets the hardware: the display first, then the processor, then the thread. */
	device->gpu = NULL;
	ghci_display_detach(device);
	ghci_cp_stop(device);
	ghci_service_stop(device);

	/* The executor's heap and staging ring go back with the rest. */
	ghci_vk_engine_stop(device);

	/* Gives every allocation that waited for the GPU back, and the allocators' tables. */
	mutex_lock(&device->lock);

	(void)ghci_memory_reclaim(device);

	mutex_unlock(&device->lock);

	ghci_memory_fini(device);

	/* Succeeded: the bus glue may release the bus resources. */
	return 0;
}

/*
 * Reads one register of the window.
 */
uint32_t
ghci_read(
	struct ghci_device *device,
	uint32_t offset)
{
	uint32_t value;

	/* One aligned 32-bit access, which never waits for the GPU's memory. */
	value = kern_mmio_read32(device->registers + offset);

	/* Reports the register's value. */
	return value;
}

/*
 * Writes one register of the window.
 */
void
ghci_write(
	struct ghci_device *device,
	uint32_t offset,
	uint32_t value)
{
	/* One aligned 32-bit access. */
	kern_mmio_write32(device->registers + offset, value);
}

/*
 * Reads one register of the window through an opaque device pointer, for
 * the walk of the extended capability list.
 */
uint32_t
ghci_read_context(
	void *context,
	uint32_t offset)
{
	uint32_t word;

	/* The walk only reads registers. */
	word = ghci_read(context, offset);

	/* Reports the register's value. */
	return word;
}

/*
 * Sleeps about the given milliseconds.
 */
void
ghci_sleep_ms(
	unsigned milliseconds)
{
	uint64_t deadline;

	/* Sleeps until the tick the time ends at. */
	deadline = clock_ticks() + kern_ms_to_ticks(milliseconds);
	sched_sleep(deadline);
}

/*
 * Waits up to a time for a register's masked bits to hold a value.
 */
int
ghci_poll(
	struct ghci_device *device,
	uint32_t offset,
	uint32_t mask,
	uint32_t value,
	unsigned milliseconds)
{
	uint32_t word;
	unsigned waited;

	/* Reads the register every millisecond until it holds the value or the time is up. */
	for (waited = 0U; waited <= milliseconds; waited++) {
		word = ghci_read(device, offset);
		if ((word & mask) == value)
			return 0;

		ghci_sleep_ms(1U);
	}

	/* The register never held the value. */
	return ETIMEDOUT;
}

/*
 * Starts the command processor's firmware after a reset.
 *
 * CTRL.RESET stops the firmware's processor (CPFW_CTRL.RUN reads 0
 * after it), so the driver sets RUN after every reset and waits until
 * the image is ready and the processor runs.  A GPU with firmware but no
 * CPFW capability starts its firmware itself.
 */
int
ghci_firmware_start(
	struct ghci_device *device)
{
	uint32_t registers;
	uint32_t version;
	uint32_t status;
	int error;

	/* A command processor without firmware has nothing to start. */
	if ((device->caps & GHCI_CAPS_FIRMWARE) == 0U)
		return 0;

	/* Finds the CPFW capability; without one, the firmware runs on its own. */
	error = ghci_extcap_find(ghci_read_context, device, device->extcap, device->register_bytes, GHCI_XCAP_CPFW, &registers, &version);
	if (error != 0)
		return 0;

	/* RUN (CPFW_CTRL, the capability's first register) lets the processor out of its stop; the image stays as the reset left it. */
	ghci_write(device, registers, GHCI_CPFW_CTRL_RUN);

	/* The firmware runs once its image is ready, with no fault, and the processor is not held. */
	error = ghci_poll(
		device,
		registers + CPFW_STATUS_OFFSET,
		GHCI_CPFW_STATUS_READY | GHCI_CPFW_STATUS_FAULT | GHCI_CPFW_STATUS_STOPPED,
		GHCI_CPFW_STATUS_READY,
		GHCI_FIRMWARE_WAIT_MS);
	if (error != 0) {
		status = ghci_read(device, registers + CPFW_STATUS_OFFSET);
		kern_logf("ghci: %s: firmware did not start (CPFW_STATUS %08x)\n", device->name, (unsigned)status);
		return EIO;
	}

	/* Succeeded: the firmware runs. */
	return 0;
}

/* Reads the identification and capabilities, and refuses a window that is not GHCI 1 with a ring. */
static int
identify(
	struct ghci_device *device)
{
	uint32_t magic;
	uint32_t line_log2;

	/* The window must say GHCI. */
	magic = ghci_read(device, GHCI_MAGIC);
	if (magic != GHCI_MAGIC_VALUE)
		return ENODEV;

	/* Only a major version the driver knows; a minor version only adds. */
	device->version = ghci_read(device, GHCI_VERSION);
	if ((device->version >> 16) != KNOWN_MAJOR) {
		kern_logf("ghci: GHCI version %u.%u is not known\n",
		    (unsigned)(device->version >> 16),
		    (unsigned)(device->version & 0xffffU));
		return ENODEV;
	}

	/* The identification. */
	device->vendor_id = ghci_read(device, GHCI_VENDOR_ID);
	device->device_id = ghci_read(device, GHCI_DEVICE_ID);
	device->isa_id = ghci_read(device, GHCI_ISA_ID);
	device->device_version = ghci_read(device, GHCI_DEVICE_VERSION);
	device->extcap = ghci_read(device, GHCI_EXTCAP);
	device->gclk_hz = ghci_read(device, GHCI_GCLK_HZ);
	device->caps = ghci_read(device, GHCI_CAPS);
	device->caps2 = ghci_read(device, GHCI_CAPS2);
	name_device(device);

	/* The driver runs the ring; a window with only a display controller is not a GPU device. */
	if ((device->caps & GHCI_CAPS_CP) == 0U) {
		kern_logf("ghci: %s: no command processor, not taken\n", device->name);
		return ENODEV;
	}

	/* The memory line, never smaller than GHCI's smallest. */
	line_log2 = GHCI_CAPS_LINE_LOG2(device->caps);
	if (line_log2 < LINE_LOG2_MIN)
		line_log2 = LINE_LOG2_MIN;
	device->line_bytes = 1U << line_log2;

	/* The local memory and the session protection, from the extended capabilities. */
	identify_memory(device);

	/* The compiler of the shader ISA; a GPU without one still blits. */
	device->isa = ghci_isa_select(device->caps, device->isa_id);
	if ((device->caps & GHCI_CAPS_SHADER) != 0U && device->isa == NULL)
		kern_logf("ghci: %s: no compiler for its shader ISA\n", device->name);

	/* Succeeded: the device is a GHCI 1 GPU. */
	return 0;
}

/* Reads the MEM and PROT capabilities: the local memory, its window and the protection. */
static void
identify_memory(
	struct ghci_device *device)
{
	uint32_t registers;
	uint32_t version;
	uint64_t local_end;
	uint64_t window;
	int error;

	/* No local memory unless the MEM capability says so: the GPU is unified. */
	device->local_base = 0U;
	device->local_size = 0U;
	device->local_window = 0U;

	/* Finds the MEM capability. */
	error = ghci_extcap_find(ghci_read_context, device, device->extcap, device->register_bytes, GHCI_XCAP_MEM, &registers, &version);
	if (error == 0) {
		device->local_base = ghci_read(device, registers + GHCI_MEM_LOCAL_BASE);
		device->local_size = ghci_read(device, registers + GHCI_MEM_LOCAL_SIZE);
		window = ghci_read(device, registers + GHCI_MEM_WINDOW_SIZE);

		/* The window shows no more than local memory, and no more than the bus mapped. */
		if (window > device->local_size)
			window = device->local_size;
		if (window > device->window_bytes)
			window = device->window_bytes;
		device->local_window = window;
	}

	/* Local memory that runs past the 32-bit GPU address space is cut at its end. */
	local_end = (uint64_t)device->local_base + device->local_size;
	if (local_end > 0x100000000ULL) {
		kern_logf("ghci: %s: local memory past 4 GiB is not used\n", device->name);
		device->local_size = 0x100000000ULL - device->local_base;
	}

	/* The session protection is only recorded: the driver does not ask for protected sessions yet. */
	error = ghci_extcap_find(ghci_read_context, device, device->extcap, device->register_bytes, GHCI_XCAP_PROT, &registers, &version);
	device->protection = 0;
	if (error == 0)
		device->protection = 1;
}

/* Names the device "ghci:<vendor>:<device>" from its FourCCs. */
static void
name_device(
	struct ghci_device *device)
{
	char vendor[5];
	char product[5];

	/* The FourCCs as text, with anything unprintable shown as '?'. */
	put_fourcc(vendor, device->vendor_id);
	put_fourcc(product, device->device_id);
	(void)kern_snprintf(device->name, sizeof(device->name), "ghci:%s:%s", vendor, product);
}

/* Writes a FourCC (its first letter in bits 31:24) as four characters and a terminator. */
static void
put_fourcc(
	char *out,
	uint32_t fourcc)
{
	unsigned index;
	char letter;

	/* The letters from the high byte down. */
	for (index = 0U; index < 4U; index++) {
		letter = (char)((fourcc >> (24U - 8U * index)) & 0xffU);

		/* Only printable ASCII is kept in the name. */
		if (letter < 0x20 || letter > 0x7e)
			letter = '?';
		out[index] = letter;
	}

	/* The terminator. */
	out[4] = '\0';
}

/* Resets the GPU and waits for the reset to end and the memory to be ready. */
static int
reset_hardware(
	struct ghci_device *device)
{
	int error;

	/* RESET is write-1-to-set, and falls when the reset is done. */
	ghci_write(device, GHCI_CTRL, GHCI_CTRL_RESET);
	error = ghci_poll(device, GHCI_CTRL, GHCI_CTRL_RESET, 0U, GHCI_RESET_WAIT_MS);
	if (error != 0) {
		kern_logf("ghci: %s: reset did not end (status %08x)\n", device->name, (unsigned)ghci_read(device, GHCI_STATUS));
		return EIO;
	}

	/* The memory controller's calibration must be done before the ring is set. */
	error = ghci_poll(device, GHCI_STATUS, GHCI_STATUS_MEM_READY, GHCI_STATUS_MEM_READY, GHCI_MEMORY_WAIT_MS);
	if (error != 0) {
		kern_logf("ghci: %s: memory not ready (status %08x)\n", device->name, (unsigned)ghci_read(device, GHCI_STATUS));
		return EIO;
	}

	/* The firmware of the command processor, which the reset stopped. */
	error = ghci_firmware_start(device);
	if (error != 0)
		return error;

	/* Succeeded: the GPU is idle with its memory ready and its firmware running. */
	return 0;
}

/* Fills the operations table and registers the device with the GPU core. */
static int
register_device(
	struct ghci_device *device)
{
	int native;
	int error;

	/* The table every device has. */
	kern_memset(&device->ops, 0, sizeof(device->ops));
	device->ops.version = DRV_GPU_INTERFACE_VERSION;
	device->ops.size = sizeof(device->ops);
	device->ops.capabilities = BASE_CAPABILITIES;
	device->ops.open = gpu_open;
	device->ops.close = gpu_close;
	device->ops.get_info = gpu_get_info;
	device->ops.resource_create = gpu_resource_create;
	device->ops.resource_destroy = gpu_resource_destroy;
	device->ops.blob_create = gpu_blob_create;
	device->ops.resource_read = gpu_resource_read;
	device->ops.resource_write = gpu_resource_write;
	device->ops.resource_map = gpu_resource_map;
	device->ops.share = &ghci_share_ops;

	/*
	 * The command operation: a test kernel's native command, for root
	 * only.  libvulkan needs the capability too, but sends its frames
	 * through the queued submission of the executor.
	 */
	native = 0;
	if (CONFIG_GHCI_NATIVE && ghci_native_command != NULL)
		native = 1;
	if (native || device->vk != NULL) {
		device->ops.capabilities |= GPU_CAP_COMMAND;
		device->ops.command = gpu_command;
	}

	/* The Vulkan executor's capset, frames, jobs and recovery, when the device has one. */
	if (device->vk != NULL)
		ghci_vk_node_bind(device);

	/* The display controller's operations, when the GPU has one. */
	if (device->display.present) {
		device->ops.capabilities |= GPU_CAP_DISPLAY | GPU_CAP_DISPLAY_EVENTS;
		device->ops.display = &ghci_display_ops;
	}

	/* Publishes the node. */
	error = drv_gpu_register(&device->ops, device, &device->gpu);
	if (error != 0) {
		kern_logf("ghci: %s: not registered (%d)\n", device->name, error);
		return error;
	}

	/* Succeeded: /dev/gpuN is the device's. */
	return 0;
}

/* Takes the lowest free job tag of a session (1 to 255). */
static int
tag_take(
	struct ghci_device *device,
	uint32_t *tag)
{
	uint32_t candidate;
	uint32_t word;
	uint32_t bit;

	/* Looks for a tag no session holds. */
	for (candidate = 1U; candidate <= GHCI_TAG_MAX; candidate++) {
		word = candidate / 32U;
		bit = 1U << (candidate % 32U);
		if ((device->tags[word] & bit) == 0U) {
			device->tags[word] |= bit;
			*tag = candidate;
			return 0;
		}
	}

	/* Every tag is held: as many sessions as GHCI can name are open. */
	return EBUSY;
}

/* Gives a job tag back. */
static void
tag_give(
	struct ghci_device *device,
	uint32_t tag)
{
	/* The tag's bit is clear again. */
	device->tags[tag / 32U] &= ~(1U << (tag % 32U));
}

/* Opens a session with a job tag of its own. */
static int
gpu_open(
	void *private_data,
	void **result)
{
	struct ghci_device *device;
	struct ghci_session *session;
	int error;

	/* The session's record. */
	device = private_data;
	session = kern_calloc(1U, sizeof(*session));
	if (session == NULL)
		return ENOMEM;

	/* Refuses an open once the detach began or the hardware is lost, and takes a tag. */
	mutex_lock(&device->lock);

	error = 0;
	if (device->withdrawing) {
		error = ENODEV;
	} else if (device->lost) {
		error = EIO;
	} else {
		error = tag_take(device, &session->tag);
	}

	/* The session's executor, which opens the firmware's session of the tag. */
	session->device = device;
	if (error == 0 && device->vk != NULL) {
		error = ghci_vk_session_open(device, session);
		if (error != 0)
			tag_give(device, session->tag);
	}

	/* Lists the session with the device. */
	if (error == 0) {
		session->next = device->sessions;
		device->sessions = session;
		device->session_count++;
	}

	mutex_unlock(&device->lock);

	/* An open that was refused leaves nothing behind. */
	if (error != 0) {
		kern_free(session);
		return error;
	}

	/* Succeeded: the session is the caller's. */
	*result = session;
	return 0;
}

/* Closes a session: its lease ends, its memory goes back once the GPU is done with it. */
static void
gpu_close(
	void *private_data,
	void *opaque)
{
	struct ghci_device *device;
	struct ghci_session *session;
	struct ghci_session **position;

	device = private_data;
	session = opaque;

	/* Everything of the session is given back under the device's lock. */
	mutex_lock(&device->lock);

	/* The display lease ends first, so its frame is no longer scanned from the session's memory. */
	ghci_display_session_close(device, session);

	/* The executor closes the firmware's session and forgets its objects. */
	if (session->vk != NULL)
		ghci_vk_session_close(device, session);

	/* Each allocation goes back now, or after the session's last fence. */
	session_give_back(device, session);

	/* Finds the session on the device's list. */
	position = &device->sessions;
	while (*position != NULL && *position != session)
		position = &(*position)->next;

	/* The session leaves the list. */
	if (*position == session)
		*position = session->next;

	/* Its tag is free for the next open. */
	tag_give(device, session->tag);
	device->session_count--;

	mutex_unlock(&device->lock);

	/* The deferred memory goes back from the service thread. */
	ghci_service_wake(device, GHCI_WORK_RECLAIM);
	kern_free(session);
}

/* Reports the device's capabilities, limits and name. */
static int
gpu_get_info(
	void *private_data,
	void *opaque,
	struct gpu_info *info)
{
	struct ghci_device *device;

	(void)opaque;

	/* The capability snapshot of the device. */
	device = private_data;
	info->capabilities = device->ops.capabilities;
	info->max_resources = GHCI_SESSION_RESOURCES;
	info->max_resource_bytes = GHCI_RESOURCE_MAX_BYTES;
	kern_memset(info->driver_name, 0, sizeof(info->driver_name));
	kern_memcpy(info->driver_name, device->name, sizeof(device->name) - 1U);

	/* Succeeded: the snapshot is filled. */
	return 0;
}

/* Makes a storage resource: cleared memory the processor reaches and the display can scan. */
static int
gpu_resource_create(
	void *private_data,
	void *opaque,
	const struct gpu_resource_create *request,
	void **result)
{
	struct ghci_allocation *allocation;
	int error;

	/* Only storage, and no flags. */
	if (request->usage != GPU_RESOURCE_USAGE_STORAGE)
		return EINVAL;
	if (request->flags != 0U)
		return EINVAL;

	/* Memory the processor maps. */
	error = allocation_make(private_data, opaque, GHCI_USE_MAPPABLE, request->bytes, 0, &allocation);
	if (error != 0)
		return error;

	/* Succeeded: the resource is the session's. */
	*result = allocation;
	return 0;
}

/* Destroys a resource: its memory goes back once the GPU is done with it. */
static void
gpu_resource_destroy(
	void *private_data,
	void *opaque,
	void *object)
{
	struct ghci_device *device;
	struct ghci_allocation *allocation;

	device = private_data;
	allocation = object;

	/*
	 * Off the session's list, then back now or after the session's last
	 * fence.  The display never scans a session's storage (a presentation
	 * copies it into the driver's frames), so nothing else holds it.
	 */
	mutex_lock(&device->lock);

	ghci_allocation_drop(device, opaque, allocation);

	mutex_unlock(&device->lock);

	/* The deferred memory goes back from the service thread. */
	ghci_service_wake(device, GHCI_WORK_RECLAIM);
}

/*
 * Makes a blob: device-local memory, or memory the processor maps when
 * the blob is mappable.  Its resource id is its GPU address, which the
 * session's native packets name.
 */
static int
gpu_blob_create(
	void *private_data,
	void *opaque,
	const struct gpu_blob_create *request,
	void **result,
	uint32_t *resource_id)
{
	struct ghci_allocation *allocation;
	enum ghci_memory_use use;
	int error;

	/* Mapping and sharing within the device; another device cannot import this memory. */
	if ((request->flags & ~BLOB_FLAGS) != 0U)
		return ENOTSUP;

	/* A blob of a Vulkan memory object shows that object's memory. */
	if (request->blob_id != 0U) {
		error = blob_of_memory(private_data, opaque, request->blob_id, &allocation);
		if (error != 0)
			return error;

		/* Succeeded: the blob is the session's, named by the memory's GPU address. */
		*result = allocation;
		*resource_id = allocation->gpu_address;
		return 0;
	}

	/* A mappable blob is memory the processor reaches; another is the GPU's own. */
	use = GHCI_USE_DEVICE;
	if ((request->flags & GPU_BLOB_MAPPABLE) != 0U)
		use = GHCI_USE_MAPPABLE;

	/* The cleared memory, on the session's list. */
	error = allocation_make(private_data, opaque, use, request->bytes, 1, &allocation);
	if (error != 0)
		return error;

	/* Succeeded: the blob is the session's, named by its GPU address. */
	*result = allocation;
	*resource_id = allocation->gpu_address;
	return 0;
}

/* Copies bytes out of a resource. */
static int
gpu_resource_read(
	void *private_data,
	void *opaque,
	void *object,
	uint64_t offset,
	void *bytes,
	uint32_t length)
{
	struct ghci_device *device;
	struct ghci_allocation *allocation;
	int error;

	(void)opaque;

	/* Refuses a range outside the resource. */
	device = private_data;
	allocation = object;
	error = allocation_range(allocation, offset, length);
	if (error != 0)
		return error;

	/* Memory the processor reaches is copied directly. */
	if (allocation->view != NULL) {
		kern_memcpy(bytes, allocation->view + offset, length);
		return 0;
	}

	/* Other memory comes through the bounce buffer, by the blitter. */
	mutex_lock(&device->lock);

	error = ghci_blit_transfer(device, allocation, offset, bytes, length, 0);

	mutex_unlock(&device->lock);

	/* Reports a copy the GPU did not finish. */
	if (error != 0)
		return error;

	/* Succeeded: the bytes are copied. */
	return 0;
}

/* Copies bytes into a resource. */
static int
gpu_resource_write(
	void *private_data,
	void *opaque,
	void *object,
	uint64_t offset,
	const void *bytes,
	uint32_t length)
{
	struct ghci_device *device;
	struct ghci_allocation *allocation;
	int error;

	(void)opaque;

	/* Refuses a range outside the resource. */
	device = private_data;
	allocation = object;
	error = allocation_range(allocation, offset, length);
	if (error != 0)
		return error;

	/* Memory the processor reaches is copied directly. */
	if (allocation->view != NULL) {
		kern_memcpy(allocation->view + offset, bytes, length);
		return 0;
	}

	/* Other memory goes through the bounce buffer, by the blitter. */
	mutex_lock(&device->lock);

	error = ghci_blit_transfer(device, allocation, offset, (void *)bytes, length, 1);

	mutex_unlock(&device->lock);

	/* Reports a copy the GPU did not finish. */
	if (error != 0)
		return error;

	/* Succeeded: the bytes are copied. */
	return 0;
}

/* Describes the processor's view of a resource for a user mapping. */
static int
gpu_resource_map(
	void *private_data,
	void *opaque,
	void *object,
	struct drv_gpu_mapping *mapping)
{
	struct ghci_allocation *allocation;

	(void)private_data;
	(void)opaque;

	/* Memory only the GPU reaches cannot be mapped. */
	allocation = object;
	if (allocation->view == NULL)
		return EINVAL;

	/* The local window is device memory; host memory is ordinary RAM. */
	mapping->physical = allocation->physical;
	mapping->address = allocation->view;
	mapping->bytes = allocation->bytes;
	mapping->attributes = 0U;
	if (allocation->kind == GHCI_MEMORY_VISIBLE)
		mapping->attributes = DRV_GPU_MAPPING_DEVICE;

	/* Succeeded: the view is described. */
	return 0;
}

/* Takes one native command of a test kernel, from root only. */
static int
gpu_command(
	void *private_data,
	void *opaque,
	const void *stream,
	uint32_t bytes)
{
	const struct ucred *credentials;
	const uint32_t *words;
	int superuser;
	int error;

	/* Only the native command comes this way; a kernel without it takes none. */
	words = stream;
	if (bytes < 4U)
		return EINVAL;
	if (words[0] != GPU_OP_GHCI_NATIVE)
		return ENOTSUP;
	if (!CONFIG_GHCI_NATIVE)
		return ENOTSUP;
	if (ghci_native_command == NULL)
		return ENOTSUP;

	/* Native packets reach memory directly, so only root sends them. */
	credentials = cred_current();
	superuser = cred_is_superuser(credentials);
	if (!superuser)
		return EPERM;

	/* Checks and submits the packets. */
	error = ghci_native_command(private_data, opaque, stream, bytes);
	if (error != 0)
		return error;

	/* Succeeded: the packets are in the ring. */
	return 0;
}

/* Makes a cleared allocation for a session and lists it, under the device's lock. */
static int
allocation_make(
	struct ghci_device *device,
	struct ghci_session *session,
	enum ghci_memory_use use,
	uint64_t bytes,
	int blob,
	struct ghci_allocation **result)
{
	int error;

	/* Everything is done under the device's lock. */
	mutex_lock(&device->lock);

	error = ghci_allocation_make(device, session, use, bytes, blob, result);

	mutex_unlock(&device->lock);

	/* Reports memory that could not be had. */
	if (error != 0)
		return error;

	/* Succeeded: the allocation is the session's. */
	return 0;
}

/*
 * Makes a cleared allocation for a session and lists it.
 */
int
ghci_allocation_make(
	struct ghci_device *device,
	struct ghci_session *session,
	enum ghci_memory_use use,
	uint64_t bytes,
	int blob,
	struct ghci_allocation **result)
{
	struct ghci_allocation *allocation;
	struct ghci_ticket ticket;
	int error;

	/* Refuses an empty resource and one past the limit. */
	if (bytes == 0U)
		return EINVAL;
	if (bytes > GHCI_RESOURCE_MAX_BYTES)
		return EINVAL;

	/* Refuses a session that holds as many resources as it may. */
	if (session->allocation_count >= GHCI_SESSION_RESOURCES)
		return ENOSPC;

	/* The memory. */
	error = ghci_memory_alloc(device, session, use, bytes, &allocation);
	if (error != 0)
		return error;

	/*
	 * The memory may hold what another session left: it is cleared
	 * before the session sees it, by the blitter in local memory and by
	 * the processor in host memory.
	 */
	error = 0;
	if (allocation->kind == GHCI_MEMORY_HOST) {
		kern_memset(allocation->view, 0, allocation->bytes);
	} else {
		error = ghci_blit_clear(device, allocation->gpu_address, allocation->bytes);
	}

	/*
	 * Memory that could not be cleared goes back unseen: at once when the
	 * blit failed to start, after the newest fence when it may still run.
	 */
	if (error != 0) {
		ghci_cp_last(device, &ticket);
		ghci_memory_defer(device, allocation, &ticket);
		ghci_service_wake(device, GHCI_WORK_RECLAIM);
		return error;
	}

	/* Lists the allocation with the session. */
	allocation->blob = blob;
	allocation->next = session->allocations;
	session->allocations = allocation;
	session->allocation_count++;

	/* Succeeded: the allocation is the session's. */
	*result = allocation;
	return 0;
}

/*
 * Takes an allocation off its session's list and gives it back.
 */
void
ghci_allocation_drop(
	struct ghci_device *device,
	struct ghci_session *session,
	struct ghci_allocation *allocation)
{
	/* Off the list, then back now, after the session's last fence, or after its last user. */
	allocation_unlist(session, allocation);
	ghci_memory_release(device, allocation);
}

/*
 * Finds a blob of a session by its resource id.
 */
struct ghci_allocation *
ghci_allocation_blob(
	struct ghci_session *session,
	uint32_t resource_id)
{
	struct ghci_allocation *allocation;

	/* Zero names nothing. */
	if (resource_id == 0U)
		return NULL;

	/* Looks at each of the session's allocations for a blob of that address. */
	for (allocation = session->allocations;
	     allocation != NULL;
	     allocation = allocation->next) {
		/* Storage and Vulkan memory are not blobs. */
		if (!allocation->blob)
			continue;

		/* The blob the id names. */
		if (allocation->gpu_address == resource_id)
			return allocation;
	}

	/* No blob of the session has that id. */
	return NULL;
}

/*
 * Makes the blob of a Vulkan memory object: a record that shows the
 * memory's allocation and holds it, so the memory stays while the blob
 * is mapped even when the memory object is freed first.
 */
static int
blob_of_memory(
	struct ghci_device *device,
	struct ghci_session *session,
	uint64_t identity,
	struct ghci_allocation **result)
{
	struct ghci_allocation *backing;
	struct ghci_allocation *alias;
	int error;

	/* The blob's record. */
	alias = kern_calloc(1U, sizeof(*alias));
	if (alias == NULL)
		return ENOMEM;

	/* The memory object, under the device's lock. */
	mutex_lock(&device->lock);

	error = 0;
	backing = NULL;
	if (device->vk == NULL) {
		error = EINVAL;
	} else if (session->allocation_count >= GHCI_SESSION_RESOURCES) {
		error = ENOSPC;
	} else {
		error = ghci_vk_memory_blob(device, session, identity, &backing);
	}

	/* The blob shows the same memory and holds it; it is listed as the session's. */
	if (error == 0) {
		alias->session = session;
		alias->kind = backing->kind;
		alias->gpu_address = backing->gpu_address;
		alias->bytes = backing->bytes;
		alias->requested = backing->requested;
		alias->view = backing->view;
		alias->physical = backing->physical;
		alias->blob = 1;
		alias->backing = backing;
		ghci_memory_hold(backing);
		alias->next = session->allocations;
		session->allocations = alias;
		session->allocation_count++;
	}

	mutex_unlock(&device->lock);

	/* A memory object the session does not have. */
	if (error != 0) {
		kern_free(alias);
		return error;
	}

	/* Succeeded: the blob is the session's. */
	*result = alias;
	return 0;
}

/*
 * Gives every allocation of a closing session back: the blobs that show
 * Vulkan memory first, so the memory they hold is let go before it is
 * given back itself.  The device's lock is held.
 */
static void
session_give_back(
	struct ghci_device *device,
	struct ghci_session *session)
{
	struct ghci_allocation **position;
	struct ghci_allocation *allocation;

	/* The blobs of Vulkan memory. */
	position = &session->allocations;
	while (*position != NULL) {
		allocation = *position;
		if (allocation->backing == NULL) {
			position = &allocation->next;
			continue;
		}

		/* Unlinks it and lets the memory it shows go. */
		*position = allocation->next;
		session->allocation_count--;
		ghci_memory_release(device, allocation);
	}

	/* Then everything else, now or after the session's last fence. */
	while (session->allocations != NULL) {
		allocation = session->allocations;
		session->allocations = allocation->next;
		session->allocation_count--;
		ghci_memory_release(device, allocation);
	}
}

/* Checks that a copy stays inside what the caller asked the allocation for. */
static int
allocation_range(
	const struct ghci_allocation *allocation,
	uint64_t offset,
	uint32_t length)
{
	/* Refuses a start past the end. */
	if (offset > allocation->requested)
		return EINVAL;

	/* Refuses a length that runs past the end. */
	if (length > allocation->requested - offset)
		return EINVAL;

	/* Succeeded: the range is inside. */
	return 0;
}

/* Takes an allocation off its session's list; the device's lock is held. */
static void
allocation_unlist(
	struct ghci_session *session,
	struct ghci_allocation *allocation)
{
	struct ghci_allocation **position;

	/* Finds the allocation on the list. */
	position = &session->allocations;
	while (*position != NULL && *position != allocation)
		position = &(*position)->next;

	/* An allocation that is not listed is left as it is. */
	if (*position == NULL)
		return;

	/* Unlinks it. */
	*position = allocation->next;
	allocation->next = NULL;
	session->allocation_count--;
}
