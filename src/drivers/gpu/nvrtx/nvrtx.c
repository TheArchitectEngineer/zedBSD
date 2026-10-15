/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The nvrtx PCI driver: registration and attach.
 *
 * nvrtx drives NVIDIA GPUs from Turing on through the GSP firmware
 * (plan/ws037/nvrtx-design.md).  The attach finds the GPU, reads its chip
 * identity (N0) and records where the firmware's picture lies (N1).  With
 * nvrtx.gsp=0 on the boot line, or for a chip without a GSP path yet, it then
 * gives the function back to the firmware by refusing the bind: the
 * firmware's console keeps working, the PCI core never suspends the
 * function, and a machine without an NVIDIA GPU is not touched at all.
 *
 * Otherwise (the default) on a TU10x chip that shows the firmware's picture
 * (or any TU10x chip with nvrtx.gsp=1) the attach keeps the function and
 * starts a worker thread that runs the GSP boot (gsp.c) once the root file
 * system holds the firmware.  The kept function refuses a system suspend.
 *
 * At the reboot or the shutdown of the machine the PCI shutdown hook stops
 * a GSP kept up (the GPU device, then P7), so the hub's state of this boot
 * and the protected region are not carried into the next boot (2026-10-16,
 * the user's decision: the GSP is stopped at the reboot).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <drivers/pci/pci-nvrtx.h>
#include <drivers/pci/pci.h>
#include <kern/clock.h>
#include <kern/kcrt.h>
#include <kern/kmem.h>
#include <kern/sched.h>
#include <kern/sysctl.h>
#include <kern/thread.h>
#include <uapi/errno.h>

#include "drivers/gpu/nvrtx/nvrtx-gpu.h"
#include "drivers/gpu/nvrtx/nvrtx-private.h"

/* How long the shutdown waits for a GSP boot still running (P2b's wait is the longest stage), and its step. */
#define NVRTX_SHUTDOWN_BOOT_WAIT_US	12000000U
#define NVRTX_SHUTDOWN_STEP_US		10000U

static int nvrtx_attach(struct drv_pci_device *pci, const struct drv_pci_id *id);
static int nvrtx_detach(struct drv_pci_device *pci, unsigned flags);
static void nvrtx_shutdown(struct drv_pci_device *pci);
static int nvrtx_suspend(struct drv_pci_device *pci);
static int nvrtx_keep(struct nvrtx_device *device);
static void nvrtx_worker(void *argument);
static void nvrtx_give_back(struct nvrtx_gpu *gpu);

/*
 * Registers the nvrtx PCI driver for NVIDIA display-class functions.
 *
 * Every NVIDIA function of the display class is offered to the attach, which
 * names the chip from its identity register; the device ID only names the
 * product.  The audio, USB and USB-C functions of the same card are of other
 * classes and are not matched.
 */
int
drv_pci_nvrtx_driver_register(void)
{
	static const struct drv_pci_id identifiers[] = {
		{ NVRTX_PCI_VENDOR, DRV_PCI_ANY_ID, DRV_PCI_ANY_ID, DRV_PCI_ANY_ID,
		  NVRTX_PCI_CLASS_DISPLAY, NVRTX_PCI_CLASS_DISPLAY_MASK, 0U }
	};
	static struct drv_pci_driver driver = {
		"nvrtx",
		identifiers,
		sizeof(identifiers) / sizeof(identifiers[0]),
		NULL,
		nvrtx_attach,
		nvrtx_detach,
		nvrtx_shutdown,
		nvrtx_suspend,
		NULL,
		{ 0U, 0U, 0U, 0U }
	};
	int error;

	/* Hands probing and binding to PCI. */
	error = drv_pci_driver_register(&driver);
	if (error != 0)
		return error;

	/* Succeeded: matching functions are offered to the attach during PCI scanning. */
	return 0;
}

/*
 * Runs the read-only stages on one function, then keeps it or gives it back.
 *
 * The function is kept on a chip whose GSP path exists and which shows
 * the firmware's picture, unless the boot says nvrtx.gsp=0, or on such a
 * chip without the picture when the boot says nvrtx.gsp=1; otherwise the
 * bind is refused and it stays with the firmware.
 */
static int
nvrtx_attach(
	struct drv_pci_device *pci,
	const struct drv_pci_id *id)
{
	struct nvrtx_device device;
	bool off;
	bool allowed;
	bool gsp_wanted;
	bool gsp_forced;
	int error;

	UNUSED_PARAMETER(id);

	/* Stays away from the GPU when the boot says so. */
	off = nvrtx_stage_driver_off();
	if (off)
		return ENODEV;

	/* Starts with nothing taken. */
	kern_memset(&device, 0, sizeof(device));
	device.pci = pci;
	drv_pci_device_address(pci, &device.address);
	device.product = drv_pci_device_product(pci);

	/* Stops before N0 when the boot asks. */
	allowed = nvrtx_stage_allowed("N0");
	if (!allowed) {
		nvrtx_stage_mark("N0 skipped (nvrtx.stop=N0)");
		return ENODEV;
	}

	/* Finds the GPU and reads its identity. */
	error = nvrtx_n0_discover(&device);
	if (error != 0) {
		nvrtx_n0_release(&device);
		return ENODEV;
	}

	/* Records the firmware's picture unless the boot stops before N1. */
	allowed = nvrtx_stage_allowed("N1");
	if (allowed) {
		nvrtx_n1_readout(&device);
	} else {
		nvrtx_stage_mark("N1 skipped (nvrtx.stop=N1)");
	}

	/*
	 * Keeps the function for the GSP boot when the chip has a path and
	 * the firmware's picture is on it (the default), or the boot forces
	 * it (nvrtx.gsp=1); a GPU the firmware did not show anything on has
	 * nothing the default boot would show, so it is left alone.
	 */
	gsp_wanted = nvrtx_stage_gsp_wanted();
	gsp_forced = nvrtx_stage_gsp_forced();
	if (gsp_wanted && device.chip.gsp_tu102 != 0 && (device.gop_on_gpu != 0 || gsp_forced)) {
		error = nvrtx_keep(&device);
		if (error == 0)
			return 0;

		nvrtx_stage_mark("N1 done; the GSP boot not started (%d)", error);
	} else if (!gsp_wanted) {
		nvrtx_stage_mark("N1 done; %s left to the firmware (nvrtx.gsp=0)", device.chip.name);
	} else if (device.chip.gsp_tu102 == 0) {
		nvrtx_stage_mark("N1 done; no GSP path for %s yet", device.chip.name);
	} else {
		nvrtx_stage_mark("N1 done; no firmware picture on %s, left to the firmware (nvrtx.gsp=1 boots it)", device.chip.name);
	}

	/* Gives the registers and the command bits back. */
	nvrtx_n0_release(&device);

	/* Refuses the bind: the function keeps its firmware state and is never suspended by PCI. */
	return ENODEV;
}

/* Gives a kept function back once its worker has ended. */
static int
nvrtx_detach(
	struct drv_pci_device *pci,
	unsigned flags)
{
	struct nvrtx_gpu *gpu;
	int error;

	UNUSED_PARAMETER(flags);

	/* A function the attach did not keep has nothing to give back. */
	gpu = drv_pci_device_driver_data(pci);
	if (gpu == NULL)
		return 0;

	/* The worker owns the GPU while it runs. */
	if (gpu->worker_running != 0)
		return EBUSY;

	/* Memory a falcon may still read stays until the power is cut. */
	if (gpu->state == NVRTX_GSP_FAILED_HOLD)
		return EBUSY;

	/* A GPU device keeps the GSP up until power-off (stage P8c). */
	if (gpu->resident != 0)
		return EBUSY;

	/* Removes PCI's pointer before the state goes away. */
	error = drv_pci_device_set_driver_data(pci, NULL);
	if (error != 0)
		return error;

	/* Gives back the registers, the DMA memory and the state. */
	nvrtx_give_back(gpu);

	/* Succeeded: PCI may clear the binding. */
	return 0;
}

/*
 * Stops a kept GPU for the reboot or the shutdown of the machine (the PCI
 * shutdown hook, after the volumes are synced and the network stopped).
 *
 * A boot still running is waited for, for a bounded time; then the GPU
 * device, if one was registered, and the GSP are stopped (P7).  Nothing
 * here stops the reboot: a failure is shown and the reset follows.
 */
static void
nvrtx_shutdown(
	struct drv_pci_device *pci)
{
	struct nvrtx_gpu *gpu;
	uint64_t start;
	uint64_t waited;

	/* A function the attach did not keep is the firmware's already. */
	gpu = drv_pci_device_driver_data(pci);
	if (gpu == NULL)
		return;

	/* Waits for a boot still running: it ends by itself, with the GSP kept up or stopped. */
	start = nvrtx_now_us();
	for (;;) {
		if (gpu->worker_running == 0)
			break;

		waited = nvrtx_now_us() - start;
		if (waited > NVRTX_SHUTDOWN_BOOT_WAIT_US) {
			nvrtx_stage_mark("P7 shutdown: the GSP boot still runs after %u ms; left to the reset", (unsigned)(waited / 1000U));
			return;
		}

		kern_usleep_range(NVRTX_SHUTDOWN_STEP_US, NVRTX_SHUTDOWN_STEP_US * 2U);
	}

	/* The worker's last writes to the state are seen before it is read here. */
	__atomic_thread_fence(__ATOMIC_SEQ_CST);

	/* A GSP the worker already stopped, or never started, leaves nothing to do. */
	if (gpu->state != NVRTX_GSP_RUNNING)
		return;

	/* Stops the GPU device with the GSP, or the GSP alone when it was kept up for the console. */
	if (gpu->node != NULL) {
		nvrtx_node_stop(gpu->node);
	} else {
		(void)nvrtx_gsp_shutdown(gpu);
	}
}

/* Refuses a system suspend while the driver keeps the function: the GSP has no suspend path yet. */
static int
nvrtx_suspend(
	struct drv_pci_device *pci)
{
	UNUSED_PARAMETER(pci);

	/* Refused: the system stays up rather than lose the GSP's state. */
	return EBUSY;
}

/*
 * Keeps a function for the GSP boot and starts its worker.
 *
 * The N0 mapping of one page is replaced by the whole register window; the
 * claim and the saved command bits pass to the kept state.  On a failure
 * nothing is kept and the caller releases what N0 took.
 */
static int
nvrtx_keep(
	struct nvrtx_device *device)
{
	struct nvrtx_gpu *gpu;
	struct thread *thread;
	bool resident;
	bool display;
	int error;

	/* Allocates the kept state. */
	gpu = kern_calloc(1U, sizeof(*gpu));
	if (gpu == NULL)
		return ENOMEM;

	/* The locks of the VRAM's register window, of the VRAM allocator and of the work channel. */
	(void)mutex_init(&gpu->window_lock, LOCK_RANK_DEVICE, "nvrtx window");
	(void)mutex_init(&gpu->memory_state.lock, LOCK_RANK_DEVICE, "nvrtx memory");
	(void)mutex_init(&gpu->work.lock, LOCK_RANK_DEVICE, "nvrtx work");

	/* Maps the whole register window; the later stages reach far into it. */
	error = drv_pci_device_map_bar(device->pci,
	    device->windows[0].bar,
	    DRV_PCI_MAP_READ | DRV_PCI_MAP_WRITE | DRV_PCI_MAP_NOCACHE,
	    &gpu->registers);
	if (error != 0) {
		kern_free(gpu);
		return error;
	}

	/* Finds the DMA device the GSP's memory comes from. */
	gpu->dma = drv_pci_device_dma(device->pci);
	if (gpu->dma == NULL) {
		drv_pci_device_unmap_bar(device->pci, &gpu->registers);
		kern_free(gpu);
		return ENODEV;
	}

	/* Takes over N0's state; its one-page mapping is no longer needed. */
	drv_pci_device_unmap_bar(device->pci, &device->registers);
	device->registers.address = NULL;
	gpu->identity = *device;

	/* Makes the state PCI's to hand back at detach. */
	error = drv_pci_device_set_driver_data(device->pci, gpu);
	if (error != 0) {
		drv_pci_device_unmap_bar(device->pci, &gpu->registers);
		kern_free(gpu);
		return error;
	}

	/* Creates the worker that runs the GSP boot. */
	error = kthread_create(nvrtx_worker, gpu, SCHED_PRIORITY_DEFAULT, &thread);
	if (error != 0) {
		(void)drv_pci_device_set_driver_data(device->pci, NULL);
		drv_pci_device_unmap_bar(device->pci, &gpu->registers);
		kern_free(gpu);
		return error;
	}

	/*
	 * worker_running tells detach that the worker owns the GPU; the worker
	 * clears it last.  Nobody joins the thread; its end releases it.
	 */
	gpu->worker_running = 1;
	thread->detached = 1;

	/*
	 * A boot that keeps the GPU up as a GPU device with its display (the
	 * default) publishes /dev/gpuN only at the worker's last stage (P8c):
	 * until the worker ends, the GPU counts as attaching, so the graphical
	 * login waits for its display instead of giving the console back.
	 */
	resident = nvrtx_stage_resident_wanted();
	display = nvrtx_stage_disp_wanted();
	if (resident && display) {
		gpu->attach_counted = 1;
		kern_gpu_attach_begin();
	}

	thread_start(thread);

	/* Succeeded: the function is kept and the GSP boot runs in the background. */
	nvrtx_stage_mark("N1 done; %s kept for the GSP boot", gpu->identity.chip.name);
	return 0;
}

/* Runs the GSP boot on its own thread. */
static void
nvrtx_worker(
	void *argument)
{
	struct nvrtx_gpu *gpu;

	gpu = argument;

	/* Runs the boot as far as this revision goes; it reports its own result. */
	(void)nvrtx_gsp_start(gpu);

	/* The device is published by now, or never will be: the GPU no longer counts as attaching. */
	if (gpu->attach_counted != 0) {
		gpu->attach_counted = 0;
		kern_gpu_attach_end();
	}

	/* Hands the GPU back to detach. */
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	gpu->worker_running = 0;
}

/* Gives back the kept registers, the DMA memory, N0's claim and command bits, and the state. */
static void
nvrtx_give_back(
	struct nvrtx_gpu *gpu)
{
	/* Frees whatever the boot still holds. */
	nvrtx_gsp_release(gpu);

	/* Unmaps the register window. */
	drv_pci_device_unmap_bar(gpu->identity.pci, &gpu->registers);

	/* Releases N0's claim and restores the command bits. */
	nvrtx_n0_release(&gpu->identity);

	/* Frees the state. */
	kern_free(gpu);
}
