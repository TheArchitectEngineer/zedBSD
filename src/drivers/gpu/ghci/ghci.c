/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GHCI device: what every GHCI GPU has in common, whatever its bus.
 *
 * The start identifies the GPU from its registers, resets it, checks
 * SCRATCH, reads its capabilities, starts the ring (cp.c) and registers
 * the device with the GPU core, which publishes /dev/gpuN named
 * "ghci:<PCI vendor>:<PCI device>".  The detach undoes it in the reverse
 * order once no session is open.
 */

#include <stddef.h>
#include <stdint.h>

#include <drivers/gpu/gpu.h>
#include <kern/clock.h>
#include <kern/device-io.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/sched.h>
#include <uapi/errno.h>
#include <uapi/gpu.h>

#include "ghci.h"

static int identify(struct ghci_device *device);
static int reset_device(struct ghci_device *device);
static int check_scratch(struct ghci_device *device);
static int read_caps(struct ghci_device *device);
static void choose_policy(struct ghci_device *device);
static int register_device(struct ghci_device *device);
static void init_locks(struct ghci_device *device);
static void restart_service(struct ghci_device *device);
static int gpu_open(void *private_data, void **result);
static void gpu_close(void *private_data, void *opaque);
static int gpu_get_info(void *private_data, void *opaque, struct gpu_info *info);

/*
 * Starts a GHCI device whose bus fields the glue has filled.
 *
 * Returns 0 with the device registered, ENODEV for a window that is not
 * GHCI 1.0, EIO for a GPU that does not reset or run, or the error of
 * the step that failed; a failed start leaves the GPU reset and idle and
 * gives back what it took.
 */
int
ghci_device_start(
	struct ghci_device *device)
{
	int error;

	/* The locks and wait queues every later step uses. */
	init_locks(device);
	device->state = GHCI_STATE_ATTACHING;

	/* Reads what the GPU is; a window that is not GHCI 1.0 is left alone. */
	error = identify(device);
	if (error != 0)
		return error;

	/* Resets the GPU and waits for its memory; whatever a kernel before this one left running stops. */
	error = reset_device(device);
	if (error != 0)
		return error;

	/* The window answers writes as a register file should. */
	error = check_scratch(device);
	if (error != 0)
		return error;

	/* What the GPU can do, from the capability registers and the extended capability list. */
	error = read_caps(device);
	if (error != 0)
		return error;

	/* Whether the device gets the Vulkan executor, and what isolation its sessions have. */
	choose_policy(device);

	/* CP_HEAP, the ring, the fence page, the interrupt and the first fence. */
	error = ghci_cp_start(device);
	if (error != 0)
		return error;

	/* The service thread, which recovers from faults and hangs. */
	error = ghci_service_start(device);
	if (error != 0) {
		(void)ghci_cp_stop(device);
		return error;
	}

	/* The poll thread of a device without an interrupt. */
	error = ghci_poll_start(device);
	if (error != 0) {
		ghci_service_stop(device);
		(void)ghci_cp_stop(device);
		return error;
	}

	/* Publishes /dev/gpuN. */
	error = register_device(device);
	if (error != 0) {
		ghci_service_stop(device);
		(void)ghci_cp_stop(device);
		return error;
	}

	/* Succeeded: the device runs and is published. */
	kern_logf("ghci: %s: GHCI %u.%u, %s\n",
	    device->name,
	    (unsigned)(device->version >> GHCI_VERSION_MAJOR_SHIFT),
	    (unsigned)(device->version & 0xffffU),
	    device->polled ? "polled" : "interrupt");
	return 0;
}

/*
 * Stops a started device once no session is open.
 *
 * Returns 0 once the GPU is reset, its interrupt let go and its memory
 * given back, or EBUSY while a session is open or the interrupt cannot be
 * let go yet; the bus glue then keeps the device and may try again.
 */
int
ghci_device_stop(
	struct ghci_device *device)
{
	int error;

	/* Refuses while a session is open, and refuses new opens from here. */
	mutex_lock(&device->lock);

	error = 0;
	if (device->session_count != 0U)
		error = EBUSY;
	else
		device->withdrawing = 1;

	mutex_unlock(&device->lock);

	/* A session still open keeps the device. */
	if (error != 0)
		return error;

	/* A recovery in progress ends first, so that the detach and the recovery do not both drive CTRL. */
	ghci_recover_wait(device);

	/*
	 * The service thread leaves before the node goes: it reports a lost
	 * device to the core through the registration, which the core frees
	 * once it is unregistered.
	 */
	ghci_service_stop(device);

	/* The node goes; the core keeps the device while a session it opened lingers. */
	if (device->gpu != NULL) {
		error = drv_gpu_unregister(device->gpu);
		if (error != 0) {
			restart_service(device);
			return error;
		}
		device->gpu = NULL;
	}

	/*
	 * The processor halts and resets, the interrupt and bus mastering go,
	 * the poll thread leaves, and the memory goes with them; an interrupt
	 * that cannot be let go yet keeps the device for another try.
	 */
	error = ghci_cp_stop(device);
	if (error != 0)
		return error;

	/* Succeeded: the device holds nothing of the GPU. */
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
	uint32_t word;

	/* One aligned 32-bit access; reading a GHCI register has no side effect. */
	word = kern_mmio_read32(device->registers + offset);

	/* Succeeded: the register's value. */
	return word;
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
 *
 * Returns 0 once they do, or ETIMEDOUT.
 */
int
ghci_wait_register(
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

		/* Sleeps for a millisecond before the next read. */
		ghci_sleep_ms(1U);
	}

	/* The register never held the value. */
	return ETIMEDOUT;
}

/*
 * Reads the 64-bit fence the device writes into the fence page.
 */
uint64_t
ghci_fence_read(
	struct ghci_device *device)
{
	volatile uint32_t *word;
	uint64_t low;
	uint64_t high;
	uint64_t again;

	/*
	 * The two halves of the little-endian word: the high half is read
	 * around the low one, so that a write between the reads is seen.
	 */
	word = (volatile uint32_t *)(void *)device->fence_page->view;
	high = word[1];
	low = word[0];
	again = word[1];
	if (again != high)
		low = word[0];

	/* Succeeded: the fence as the device last wrote it. */
	return (again << 32) | low;
}

/*
 * Writes the 64-bit fence of the fence page, as the reset's completion does.
 */
void
ghci_fence_write(
	struct ghci_device *device,
	uint64_t value)
{
	volatile uint32_t *word;

	/* The low half, then the high half; the processor does not run while the driver writes it. */
	word = (volatile uint32_t *)(void *)device->fence_page->view;
	word[0] = (uint32_t)value;
	word[1] = (uint32_t)(value >> 32);
	kern_io_write_barrier();
}

/* Sets up the locks and wait queues of the device. */
static void
init_locks(
	struct ghci_device *device)
{
	/* The mutexes, in the order they are taken, and the interrupt's spinlock. */
	(void)mutex_init(&device->lock, LOCK_RANK_DEVICE, "ghci device");
	(void)mutex_init(&device->display_lock, LOCK_RANK_DEVICE, "ghci display");
	(void)mutex_init(&device->cp_lock, LOCK_RANK_DEVICE, "ghci ring");
	spin_init(&device->irq_lock, LOCK_RANK_DEVICE, "ghci interrupt");

	/* The wait queues: tickets, the ring's room, the gate, the service and the poll. */
	waitq_init(&device->fence_wake, "ghci fence");
	waitq_init(&device->ring_wake, "ghci room");
	waitq_init(&device->gate_wake, "ghci gate");
	waitq_init(&device->service_wake, "ghci service");
	waitq_init(&device->poll_wake, "ghci poll");
}

/*
 * Starts the service thread again and opens the device again after a
 * detach the core refused.
 */
static void
restart_service(
	struct ghci_device *device)
{
	int error;

	/* The thread that recovers from faults. */
	error = ghci_service_start(device);
	if (error != 0)
		kern_logf("ghci: %s: service thread not restarted (%d)\n", device->name, error);

	/* Opens are taken again. */
	mutex_lock(&device->lock);

	device->withdrawing = 0;

	mutex_unlock(&device->lock);
}

/* Refuses a window whose magic is not GHCI or whose major version is not 1. */
static int
identify(
	struct ghci_device *device)
{
	uint32_t window[2];
	int error;

	/* The two identification registers, read only. */
	window[0] = ghci_read(device, GHCI_REG_MAGIC);
	window[1] = ghci_read(device, GHCI_REG_VERSION);
	device->version = window[1];

	/* GHCI 1.0, any minor version. */
	error = ghci_caps_identify(window);
	if (error != 0) {
		kern_logf("ghci: %04x:%04x: not GHCI 1.0 (magic %08x, version %08x)\n",
		    (unsigned)device->pci_vendor,
		    (unsigned)device->pci_device,
		    (unsigned)window[0],
		    (unsigned)window[1]);
		return ENODEV;
	}

	/* Names the device after its function, for the log and the node. */
	(void)kern_snprintf(device->name, sizeof(device->name), "ghci:%04x:%04x",
	    (unsigned)device->pci_vendor,
	    (unsigned)device->pci_device);

	/* Succeeded: the window is GHCI 1.0. */
	return 0;
}

/*
 * Resets the GPU and waits for the reset to end and the memory to be
 * ready.  The wait covers the quiesce, which may take four watchdog
 * periods of whatever WDT_CYCLES a kernel before this one left.
 */
static int
reset_device(
	struct ghci_device *device)
{
	uint32_t gclk_hz;
	uint32_t wdt_cycles;
	uint32_t status;
	int error;

	/*
	 * Values referenced: amaterrace-gpu d9252e4 docs/ghci.md (section
	 * 10, quiesce within 4 x WDT_CYCLES; Appendix A step 3, MEM_READY
	 * before the ring is set).
	 */
	gclk_hz = ghci_read(device, GHCI_REG_GCLK_HZ);
	wdt_cycles = ghci_read(device, GHCI_REG_WDT_CYCLES);
	device->reset_wait_ms = ghci_reset_wait_ms(gclk_hz, wdt_cycles);

	/* RESET is self-clearing and falls when the reset is done. */
	ghci_write(device, GHCI_REG_CTRL, GHCI_CTRL_RESET);
	error = ghci_wait_register(device, GHCI_REG_CTRL, GHCI_CTRL_RESET, 0U, device->reset_wait_ms);
	status = ghci_read(device, GHCI_REG_STATUS);
	if (error != 0 || (status & GHCI_STATUS_AXI_STUCK) != 0U) {
		kern_logf("ghci: %s: reset did not end (status %08x)\n", device->name, (unsigned)status);
		return EIO;
	}

	/* The memory must be ready before the ring is set. */
	error = ghci_wait_register(device, GHCI_REG_STATUS, GHCI_STATUS_MEM_READY, GHCI_STATUS_MEM_READY, GHCI_MEMORY_WAIT_MS);
	if (error != 0) {
		status = ghci_read(device, GHCI_REG_STATUS);
		kern_logf("ghci: %s: memory not ready (status %08x)\n", device->name, (unsigned)status);
		return EIO;
	}

	/* Succeeded: the GPU is idle with its memory ready. */
	return 0;
}

/* Writes two patterns to SCRATCH and reads each back. */
static int
check_scratch(
	struct ghci_device *device)
{
	uint32_t first;
	uint32_t second;

	/* Values referenced: amaterrace-gpu d9252e4 docs/ghci.md (section 14, the SCRATCH check). */
	ghci_write(device, GHCI_REG_SCRATCH, GHCI_SCRATCH_FIRST);
	first = ghci_read(device, GHCI_REG_SCRATCH);
	ghci_write(device, GHCI_REG_SCRATCH, GHCI_SCRATCH_SECOND);
	second = ghci_read(device, GHCI_REG_SCRATCH);

	/* A window that does not keep what is written is not a working GPU. */
	if (first != GHCI_SCRATCH_FIRST || second != GHCI_SCRATCH_SECOND) {
		kern_logf("ghci: %s: SCRATCH reads %08x and %08x\n", device->name, (unsigned)first, (unsigned)second);
		return EIO;
	}

	/* Succeeded: the window answers. */
	return 0;
}

/*
 * Reads the capabilities from a copy of the first 1 KiB of the window:
 * the capability registers, the extended capability list and the blocks
 * it names.
 */
static int
read_caps(
	struct ghci_device *device)
{
	uint32_t *window;
	uint32_t offset;
	int error;

	/* A copy of the window the decode reads; reading a register has no side effect. */
	window = kern_calloc(GHCI_WINDOW_WORDS, sizeof(*window));
	if (window == NULL)
		return ENOMEM;

	/* Every word of the first 1 KiB, the window GHCI 1.0 uses. */
	for (offset = 0U; offset < GHCI_WINDOW_BYTES; offset += 4U)
		window[offset / 4U] = ghci_read(device, offset);

	/* The capabilities, with DEVICE_CAPS above its mirrors. */
	error = ghci_caps_decode(window, &device->caps);
	kern_free(window);
	if (error != 0) {
		kern_logf("ghci: %s: capabilities not read (%d)\n", device->name, error);
		return error;
	}

	/* A mirror that disagrees with DEVICE_CAPS is only logged. */
	if (device->caps.mismatches != 0U)
		kern_logf("ghci: %s: EXT_CAP_FEATURE disagrees with DEVICE_CAPS (bits %08x); DEVICE_CAPS used\n",
		    device->name,
		    (unsigned)device->caps.mismatches);

	/* The GPU's own identification, when it has the VEND block. */
	if (device->caps.vendor[0] != 0U || device->caps.vendor[1] != 0U)
		kern_logf("ghci: %s: vendor %08x device %08x\n",
		    device->name,
		    (unsigned)device->caps.vendor[0],
		    (unsigned)device->caps.vendor[1]);

	/* Succeeded: the capabilities are read. */
	return 0;
}

/*
 * Decides whether the device gets the Vulkan executor, and says what
 * isolation its sessions have.
 */
static void
choose_policy(
	struct ghci_device *device)
{
	int walker;
	int protection;
	int shader;
	int units;

	/* The capabilities the decision reads. */
	walker = ghci_caps_has(&device->caps, GHCI_HAS_HW_WALKER);
	protection = ghci_caps_has(&device->caps, GHCI_HAS_PROT);
	shader = ghci_caps_has(&device->caps, GHCI_HAS_GSI1);
	units = ghci_caps_has(&device->caps, GHCI_HAS_SHADER_UNITS);

	/* A GPU without a shader unit or GSI1 runs the display only. */
	device->executor = 0;
	if (shader && units)
		device->executor = 1;
	if (!device->executor)
		kern_logf("ghci: %s: no shader unit or no GSI1; display only\n", device->name);

	/*
	 * Neither a GPU without a walker (every session reaches all of
	 * memory) nor one without PROT (the sessions share one address
	 * space) keeps the sessions apart; the executor is offered all the
	 * same, and the log says so.
	 */
	if (!walker)
		kern_logf("ghci: %s: GPU memory isolation: none (flat)\n", device->name);
	else if (!protection)
		kern_logf("ghci: %s: GPU memory isolation: shared VA (PROT=0)\n", device->name);
}

/* Fills the operations table and registers the device with the GPU core. */
static int
register_device(
	struct ghci_device *device)
{
	int error;

	/*
	 * The table every device has: the sessions and the information.
	 * No optional capability is offered until its callbacks are all
	 * there.
	 */
	kern_memset(&device->ops, 0, sizeof(device->ops));
	device->ops.version = DRV_GPU_INTERFACE_VERSION;
	device->ops.size = sizeof(device->ops);
	device->ops.capabilities = 0U;
	device->ops.open = gpu_open;
	device->ops.close = gpu_close;
	device->ops.get_info = gpu_get_info;

	/* Publishes the node. */
	error = drv_gpu_register(&device->ops, device, &device->gpu);
	if (error != 0) {
		kern_logf("ghci: %s: not registered (%d)\n", device->name, error);
		return error;
	}

	/* Succeeded: /dev/gpuN is the device's. */
	return 0;
}

/* Opens a session of the device. */
static int
gpu_open(
	void *private_data,
	void **result)
{
	struct ghci_device *device;
	struct ghci_session *session;
	enum ghci_device_state state;
	unsigned long irq;
	int error;

	/* The session's record. */
	device = private_data;
	session = kern_calloc(1U, sizeof(*session));
	if (session == NULL)
		return ENOMEM;

	/* The device's life, as the interrupt and the recovery last set it. */
	irq = spin_lock_irqsave(&device->irq_lock);

	state = device->state;

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* Refuses an open once the detach began or the hardware is lost, and lists the session. */
	mutex_lock(&device->lock);

	error = 0;
	if (device->withdrawing) {
		error = ENODEV;
	} else if (state == GHCI_STATE_LOST) {
		error = EIO;
	} else {
		session->device = device;
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

/* Closes a session; it holds nothing of the GPU yet, so nothing waits. */
static void
gpu_close(
	void *private_data,
	void *opaque)
{
	struct ghci_device *device;
	struct ghci_session *session;
	struct ghci_session **position;

	/* The device and the session. */
	device = private_data;
	session = opaque;

	/* The session leaves the device's list. */
	mutex_lock(&device->lock);

	position = &device->sessions;
	while (*position != NULL && *position != session)
		position = &(*position)->next;
	if (*position == session) {
		*position = session->next;
		device->session_count--;
	}

	mutex_unlock(&device->lock);

	/* The record goes. */
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
	info->max_resources = 0U;
	info->max_resource_bytes = 0U;
	kern_memset(info->driver_name, 0, sizeof(info->driver_name));
	kern_memcpy(info->driver_name, device->name, sizeof(device->name) - 1U);

	/* Succeeded: the snapshot is filled. */
	return 0;
}
