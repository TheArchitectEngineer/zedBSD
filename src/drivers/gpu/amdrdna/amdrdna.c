/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The amdrdna PCI driver: registration, attach and the stage machine.
 *
 * amdrdna drives AMD GPUs of the RDNA architecture.  The attach runs the
 * stages that only read (A0 identifies the GPU, A1 reads what the
 * firmware put on the display) on every AMD display function; a function
 * that is not a first-generation RDNA GPU the firmware posted is given
 * back to the firmware untouched.  A kept function gets a control device
 * and a bring-up thread, which runs the later stages: the ones the boot
 * line names (amdrdna.auto=, A1v by default) and the ones root asks for
 * through the control device, one at a time and never again after one
 * failed.  A GPU whose A1 failed is kept too, with its failure recorded,
 * so the control device can still report what A0 and A1 read; no stage
 * runs on it.  The kept function refuses a system suspend, unless its A1
 * failed and nothing ever ran on it, and is never detached.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <drivers/pci/pci-amdrdna.h>
#include <drivers/pci/pci.h>
#include <kern/clock.h>
#include <kern/kcrt.h>
#include <kern/kmem.h>
#include <kern/lock.h>
#include <kern/sched.h>
#include <kern/thread.h>
#include <kern/waitq.h>
#include <uapi/amdrdna.h>
#include <uapi/errno.h>

#include "drivers/gpu/amdrdna/amdrdna-private.h"
#include "drivers/gpu/amdrdna/amdrdna-regs.h"

/* The last stage this driver has; asking for a later one is refused. */
#define AMDRDNA_LAST_STAGE		AMDRDNA_STAGE_A2

/* How long the shutdown waits for the bring-up thread to end, and its step. */
#define AMDRDNA_SHUTDOWN_WAIT_US	5000000U
#define AMDRDNA_SHUTDOWN_STEP_US	10000U

static int amdrdna_attach(struct drv_pci_device *pci, const struct drv_pci_id *id);
static int amdrdna_detach(struct drv_pci_device *pci, unsigned flags);
static void amdrdna_shutdown(struct drv_pci_device *pci);
static int amdrdna_suspend(struct drv_pci_device *pci);
static int amdrdna_keep(struct amdrdna_device *device);
static void amdrdna_thread(void *argument);
static int amdrdna_run_stage(struct amdrdna_device *device, unsigned stage);
static void amdrdna_give_back(struct amdrdna_device *device);

/*
 * Registers the amdrdna PCI driver for AMD display-class functions.
 *
 * Every AMD function of the display class is offered to the attach, which
 * decides from the device ID and the GPU's own block table.  The audio
 * function of the same card is of another class and is not matched.
 */
int
drv_pci_amdrdna_driver_register(void)
{
	static const struct drv_pci_id identifiers[] = {
		{ AMDRDNA_PCI_VENDOR, DRV_PCI_ANY_ID, DRV_PCI_ANY_ID, DRV_PCI_ANY_ID,
		  AMDRDNA_PCI_CLASS_DISPLAY, AMDRDNA_PCI_CLASS_DISPLAY_MASK, 0U }
	};
	static struct drv_pci_driver driver = {
		"amdrdna",
		identifiers,
		sizeof(identifiers) / sizeof(identifiers[0]),
		NULL,
		amdrdna_attach,
		amdrdna_detach,
		amdrdna_shutdown,
		amdrdna_suspend,
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
 * Raises the stage the bring-up thread runs up to.
 *
 * Returns 0 when the target is set (or already reached), EINVAL for a
 * stage that does not exist, ENOTSUP for one this driver does not have
 * yet, EIO after a stage failed, and EBUSY when a secure OS left by an
 * earlier driver keeps the stages that write from running.
 */
int
amdrdna_request_target(
	struct amdrdna_device *device,
	unsigned target)
{
	unsigned long irq;
	int error;

	/* A stage number outside the list is refused. */
	if (target == AMDRDNA_STAGE_NONE || target >= AMDRDNA_STAGE_COUNT)
		return EINVAL;

	/* A stage the driver does not have yet is refused. */
	if (target > AMDRDNA_LAST_STAGE)
		return ENOTSUP;

	/* A secure OS left running needs a cold power cycle before any stage that writes. */
	if (target > AMDRDNA_STAGE_A1V && device->secure_os_word != 0U)
		return EBUSY;

	/* Raises the target and wakes the thread, unless a stage failed. */
	error = 0;
	irq = spin_lock_irqsave(&device->state_lock);

	if (device->failed_stage != AMDRDNA_STAGE_NONE) {
		error = EIO;
	} else if (target > device->target) {
		device->target = target;
		waitq_wake_all(&device->state_waitq);
	}

	spin_unlock_irqrestore(&device->state_lock, irq);

	/* Reports a machine stopped by a failed stage. */
	if (error != 0)
		return error;

	/* Succeeded: the thread runs the stages up to the target. */
	return 0;
}

/*
 * Runs the read-only stages on one function, then keeps it or gives it
 * back.
 */
static int
amdrdna_attach(
	struct drv_pci_device *pci,
	const struct drv_pci_id *id)
{
	struct amdrdna_device *device;
	bool off;
	int error;

	UNUSED_PARAMETER(id);

	/* Stays away from the GPU when the boot says so. */
	off = amdrdna_parameter_off();
	if (off)
		return ENODEV;

	/* Allocates the record. */
	device = kern_calloc(1U, sizeof(*device));
	if (device == NULL)
		return ENOMEM;

	/* Records the function's identity and prepares the locks. */
	device->pci = pci;
	drv_pci_device_address(pci, &device->address);
	device->product = drv_pci_device_product(pci);
	device->revision = drv_pci_device_revision(pci);
	device->subvendor = drv_pci_device_subvendor(pci);
	device->subproduct = drv_pci_device_subproduct(pci);
	spin_init(&device->vram_window_lock, LOCK_RANK_DEVICE, "amdrdna vram window");
	spin_init(&device->far_lock, LOCK_RANK_DEVICE, "amdrdna far window");
	spin_init(&device->state_lock, LOCK_RANK_DEVICE, "amdrdna stage");
	waitq_init(&device->state_waitq, "amdrdna stage");
	(void)mutex_init(&device->memory_lock, LOCK_RANK_DEVICE, "amdrdna memory");
	spin_init(&device->interrupts.lock, LOCK_RANK_DEVICE, "amdrdna interrupts");
	waitq_init(&device->interrupts.waitq, "amdrdna interrupts");

	/* Stage A0: finds and identifies the GPU. */
	device->running = AMDRDNA_STAGE_A0;
	error = amdrdna_a0_identify(device);
	if (error != 0) {
		amdrdna_give_back(device);
		return ENODEV;
	}

	/* Records that A0 reported ok. */
	device->stage = AMDRDNA_STAGE_A0;

	/* Stage A1: reads the firmware's display and plans the VRAM. */
	device->running = AMDRDNA_STAGE_A1;
	error = amdrdna_a1_readout(device);

	/*
	 * Records how A1 ended.  A failed A1 wrote nothing to the GPU; the
	 * failure stops the stage machine before it starts, and the function
	 * is kept only so the control device can report what was read.
	 */
	device->running = AMDRDNA_STAGE_NONE;
	if (error != 0) {
		device->failed_stage = AMDRDNA_STAGE_A1;
		device->failed_error = error;
	} else {
		device->stage = AMDRDNA_STAGE_A1;
	}

	/* Keeps the function and starts the bring-up thread. */
	error = amdrdna_keep(device);
	if (error != 0) {
		amdrdna_mark("A1 done; the function is not kept (%d)", error);
		amdrdna_give_back(device);
		return ENODEV;
	}

	/* Succeeded: the function is kept and the later stages run on the thread. */
	return 0;
}

/* Refuses to detach a kept function: its control device and thread hold the record. */
static int
amdrdna_detach(
	struct drv_pci_device *pci,
	unsigned flags)
{
	struct amdrdna_device *device;

	UNUSED_PARAMETER(flags);

	/* A function the attach did not keep has nothing to give back. */
	device = drv_pci_device_driver_data(pci);
	if (device == NULL)
		return 0;

	/* The record lives as long as the control device does. */
	return EBUSY;
}

/*
 * Stops the bring-up thread for the reboot or the shutdown of the machine,
 * then undoes the stages that wrote.
 *
 * The thread is told to stop and waited for, for a bounded time.  Once it
 * has ended, stage A2's interrupt ring is stopped and the memory hubs'
 * contexts get back the words the firmware left; a thread still inside a
 * stage is left alone with the GPU, and so is everything it set up.
 */
static void
amdrdna_shutdown(
	struct drv_pci_device *pci)
{
	struct amdrdna_device *device;
	unsigned long irq;
	unsigned waited;
	bool running;

	/* A function the attach did not keep is the firmware's already. */
	device = drv_pci_device_driver_data(pci);
	if (device == NULL)
		return;

	/* Tells the thread to stop. */
	irq = spin_lock_irqsave(&device->state_lock);

	device->stopping = true;
	waitq_wake_all(&device->state_waitq);

	spin_unlock_irqrestore(&device->state_lock, irq);

	/* Waits for the thread to end, for a bounded time. */
	running = true;
	for (waited = 0U; waited < AMDRDNA_SHUTDOWN_WAIT_US; waited += AMDRDNA_SHUTDOWN_STEP_US) {
		/* Reads whether the thread still runs. */
		irq = spin_lock_irqsave(&device->state_lock);

		running = device->thread_running;

		spin_unlock_irqrestore(&device->state_lock, irq);

		/* Stops waiting once it has ended. */
		if (!running)
			break;

		/* Gives the thread a step to finish its stage. */
		kern_usleep_range(AMDRDNA_SHUTDOWN_STEP_US, AMDRDNA_SHUTDOWN_STEP_US * 2U);
	}

	/* The thread is still in a stage; the reset follows anyway. */
	if (running) {
		amdrdna_mark("shutdown: the bring-up thread still runs a stage; left to the reset");
		return;
	}

	/* Undoes stage A2, when it ran. */
	amdrdna_a2_stop(device);
}

/*
 * Refuses a system suspend while the driver keeps a function a stage may
 * run on: it has no suspend path yet.  A function whose A1 failed never
 * ran a stage after the attach and has nothing to keep across a suspend.
 */
static int
amdrdna_suspend(
	struct drv_pci_device *pci)
{
	struct amdrdna_device *device;
	unsigned long irq;
	unsigned failed_stage;

	/* A function the attach did not keep is the firmware's already. */
	device = drv_pci_device_driver_data(pci);
	if (device == NULL)
		return 0;

	/* Reads where the stage machine stopped. */
	irq = spin_lock_irqsave(&device->state_lock);

	failed_stage = device->failed_stage;

	spin_unlock_irqrestore(&device->state_lock, irq);

	/* A function stopped by its A1 lets the system sleep. */
	if (failed_stage == AMDRDNA_STAGE_A1)
		return 0;

	/* Refused: the system stays up. */
	return EBUSY;
}

/*
 * Keeps a function: publishes its control device and starts its bring-up
 * thread with the stages the boot names as the first target.
 */
static int
amdrdna_keep(
	struct amdrdna_device *device)
{
	struct thread *thread;
	unsigned target;
	int error;

	/* Makes the record PCI's to hand back at detach. */
	error = drv_pci_device_set_driver_data(device->pci, device);
	if (error != 0)
		return error;

	/* The first target: the stages the boot runs without being asked, as far as the driver has them. */
	target = amdrdna_parameter_auto();
	if (target > AMDRDNA_LAST_STAGE)
		target = AMDRDNA_LAST_STAGE;
	if (target > AMDRDNA_STAGE_A1V && device->secure_os_word != 0U)
		target = AMDRDNA_STAGE_A1V;
	device->target = target;

	/* A failed A1 leaves the target unreached; the thread only waits for the shutdown. */
	if (device->failed_stage != AMDRDNA_STAGE_NONE)
		device->target = device->stage;

	/* Creates the bring-up thread. */
	error = kthread_create(amdrdna_thread, device, SCHED_PRIORITY_DEFAULT, &thread);
	if (error != 0) {
		(void)drv_pci_device_set_driver_data(device->pci, NULL);
		return error;
	}

	/*
	 * thread_running tells the shutdown that the thread may still touch
	 * the GPU; the thread clears it last.  Nobody joins the thread; its
	 * end releases it.
	 */
	device->thread_running = true;
	thread->detached = 1;
	thread_start(thread);

	/* Publishes the control device; a failure leaves the boot line as the only way forward. */
	(void)amdrdna_control_create(device);

	/* Says how far the kept function goes: no further after a failed stage. */
	if (device->failed_stage != AMDRDNA_STAGE_NONE) {
		amdrdna_mark("kept %s for its status; %s failed (%d), no stage runs",
		    device->chip,
		    amdrdna_stage_name(device->failed_stage),
		    device->failed_error);
	} else {
		amdrdna_mark("kept %s; stages up to %s run now", device->chip, amdrdna_stage_name(target));
	}

	/* Succeeded: the function is kept. */
	return 0;
}

/* Runs the stages the target asks for, one at a time, until told to stop. */
static void
amdrdna_thread(
	void *argument)
{
	struct amdrdna_device *device;
	unsigned long irq;
	uint64_t sequence;
	unsigned next;
	int error;

	/* The GPU the thread brings up. */
	device = argument;

	/* Runs one stage per turn of the loop. */
	for (;;) {
		/* Sleeps until there is a stage to run or the machine stops. */
		irq = spin_lock_irqsave(&device->state_lock);

		while (!device->stopping &&
		    (device->stage >= device->target ||
		     device->failed_stage != AMDRDNA_STAGE_NONE)) {
			sequence = waitq_sequence(&device->state_waitq);
			(void)waitq_sleep(&device->state_waitq, &device->state_lock, sequence, 0U, 0U);
		}

		/* Leaves the loop, with the lock let go, when the machine stops. */
		if (device->stopping) {
			spin_unlock_irqrestore(&device->state_lock, irq);
			break;
		}

		/* Claims the next stage; the control device shows it as running. */
		next = device->stage + 1U;
		device->running = next;

		spin_unlock_irqrestore(&device->state_lock, irq);

		/* Runs the stage without the lock; it may sleep and take long. */
		error = amdrdna_run_stage(device, next);

		/* Records the result; a failed stage stops the machine. */
		irq = spin_lock_irqsave(&device->state_lock);

		device->running = AMDRDNA_STAGE_NONE;
		if (error != 0) {
			device->failed_stage = next;
			device->failed_error = error;
		} else {
			device->stage = next;
		}

		spin_unlock_irqrestore(&device->state_lock, irq);
	}

	/* Tells the shutdown that the thread no longer touches the GPU. */
	irq = spin_lock_irqsave(&device->state_lock);

	device->thread_running = false;

	spin_unlock_irqrestore(&device->state_lock, irq);
}

/* Runs one stage of the bring-up thread. */
static int
amdrdna_run_stage(
	struct amdrdna_device *device,
	unsigned stage)
{
	int error;

	/* Each stage the thread runs. */
	switch (stage) {
	case AMDRDNA_STAGE_A1V:
		error = amdrdna_a1v_vbios(device);
		break;
	case AMDRDNA_STAGE_A2:
		error = amdrdna_a2_memory(device);
		break;
	default:
		error = ENOTSUP;
		break;
	}

	/* Reports why the stage failed. */
	if (error != 0)
		return error;

	/* Succeeded: the stage reported ok. */
	return 0;
}

/* Gives back what the read-only stages took, and the record. */
static void
amdrdna_give_back(
	struct amdrdna_device *device)
{
	/* Frees the video BIOS image, if a stage kept one. */
	amdrdna_vbios_release(device);

	/* Unmaps the registers and restores the command bits. */
	amdrdna_a0_release(device);

	/* Frees the record. */
	kern_free(device);
}
