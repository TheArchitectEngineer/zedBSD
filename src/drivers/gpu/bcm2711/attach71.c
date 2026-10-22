/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The attachment of the BCM2712 graphics driver (the Raspberry Pi 5's V3D
 * 7.1).
 *
 * The platform calls the driver once, late in device discovery.  At boot
 * the driver honors rpi4gpu.off=1, runs discovery (stage V0), which reads
 * nothing behind the engine's power domain, and then holds the engine:
 * nothing powers it until root writes hw.gpu.start=1.  The start then runs
 * the power-up (V1), identification, MMU and interrupts (V2-V4) and the
 * self-test jobs (V5-V9) on the writer's thread, logs one line per stage
 * and returns the first error.  A board where the engine hangs the bus is
 * therefore recovered by a power cycle, and the next boot comes up with the
 * engine untouched.
 */

#include <stdbool.h>
#include <stdint.h>

#include <drivers/generic/fdt.h>
#include <kern/atomic.h>
#include <kern/klog.h>
#include <kern/pmem.h>
#include <kern/sysctl.h>
#include <uapi/errno.h>

#include "drivers/gpu/bcm2711/bcm2712-gpu.h"
#include "drivers/gpu/bcm2711/v3d71-private.h"

/*
 * How much of memory at the device tree's address may be read.
 *
 * The HAL checks the tree against the same bound before the kernel starts,
 * and the tree's header states its real size inside it.
 */
#define ATTACH_FDT_READ_LIMIT	(2U * 1024U * 1024U)

/*
 * The V3D 7.1 engine, as discovery found it.
 *
 * Filled once during attachment on the boot thread, before user space
 * runs; the start, which runs at most once (attach_starting), fills the
 * rest.  engine.present is false until discovery succeeds.
 */
static struct bcm2712_v3d attach_v3d;

/*
 * Whether the start has been claimed: zero until the first hw.gpu.start=1.
 *
 * Taken with an atomic test-and-set, so two writers cannot run the bring-up
 * at once, and never given back: the bring-up runs once per boot.
 */
static volatile unsigned attach_starting;

static uint64_t start_held(void);
static int start_engine(void);

/*
 * The operations behind hw.gpu.start on this board, installed once by the
 * attachment and kept for the kernel's lifetime.
 */
static const struct kern_gpu_start_ops attach_start_ops = {
	start_held,
	start_engine
};

/*
 * Finds the V3D 7.1 engine and holds its bring-up for root.
 */
int
drv_bcm2712_gpu_attach(
	uint64_t fdt_phys)
{
	struct drv_fdt fdt;
	const void *blob;
	bool off;
	int error;

	/* Stays out entirely when the boot asked for it. */
	off = bcm2711_stage_driver_off();
	if (off) {
		bcm2711_stage_mark(BCM2711_FAMILY_V3D, "off by boot parameter");
		return ENODEV;
	}

	/* Finds the device tree in the direct map. */
	blob = kern_pmem_to_kernel((hal_physaddr_t)fdt_phys);
	if (fdt_phys == 0 || blob == NULL) {
		bcm2711_stage_mark(BCM2711_FAMILY_V3D, "no device tree");
		return ENODEV;
	}

	/* Refuses a tree whose header does not check out. */
	error = drv_fdt_open(&fdt, blob, ATTACH_FDT_READ_LIMIT);
	if (error != 0) {
		bcm2711_stage_mark(BCM2711_FAMILY_V3D, "unreadable device tree (%d)", error);
		return ENODEV;
	}

	/* Prepares the firmware mailbox the clock questions go through; the stages report a missing one. */
	error = bcm2712_firmware_init(&fdt);
	if (error != 0)
		bcm2711_stage_mark(BCM2711_FAMILY_V3D, "firmware mailbox unavailable (%d)", error);

	/* Runs discovery, which reads nothing behind the engine's power domain. */
	error = bcm2712_v3d_discover(&fdt, &attach_v3d);
	if (error != 0)
		return ENODEV;

	/* Holds the bring-up until root asks for it. */
	kern_gpu_start_ops_set(&attach_start_ops);
	bcm2711_stage_mark(BCM2711_FAMILY_V3D, "held; root starts it with: sysctl hw.gpu.start=1");

	/* Succeeded: the engine was found and waits for its start. */
	return 0;
}

/* Reports whether the engine is still held: one until its start was claimed. */
static uint64_t
start_held(void)
{
	unsigned claimed;

	/* An engine that was not found is never held. */
	if (!attach_v3d.engine.present)
		return 0;

	/* A claimed start no longer holds the engine. */
	claimed = atomic_raw_load_acquire(&attach_starting);
	if (claimed != 0)
		return 0;

	/* The engine waits for its start. */
	return 1;
}

/*
 * Powers the engine and runs its self-test, once.
 *
 * Returns ENODEV when nothing is held, and otherwise the first stage's
 * error, after logging which stage it was.
 */
static int
start_engine(void)
{
	struct bcm2711_v3d *engine;
	int taken;
	int error;

	/* Refuses a start when no engine was found or the start already ran. */
	if (!attach_v3d.engine.present)
		return ENODEV;
	taken = atomic_raw_try_acquire_zero(&attach_starting);
	if (!taken)
		return ENODEV;
	engine = &attach_v3d.engine;
	attach_v3d.started = true;
	bcm2711_stage_mark(BCM2711_FAMILY_V3D, "start requested");

	/* Powers the engine and wakes its sequencer (V1). */
	error = bcm2712_v3d_power_start(&attach_v3d);
	if (error != 0) {
		attach_v3d.stage_failed = "V1";
		attach_v3d.error = error;
		bcm2711_stage_mark(BCM2711_FAMILY_V3D, "V1 not ready (%d); engine left off", error);
		return error;
	}

	/* Identifies the engine and starts its MMU and interrupts (V2-V4). */
	error = bcm2711_v3d_hardware_start(engine);
	if (error != 0) {
		attach_v3d.stage_failed = "V2-V4";
		attach_v3d.error = error;
		bcm2711_stage_mark(BCM2711_FAMILY_V3D, "V2-V4 not ready (%d)", error);
		return error;
	}

	/* Runs the self-test jobs (V5-V9). */
	error = bcm2711_v3d_diagnostic(engine);
	if (error != 0) {
		attach_v3d.stage_failed = "V5-V9";
		attach_v3d.error = error;
		bcm2711_stage_mark(BCM2711_FAMILY_V3D, "self-test failed (%d)", error);
		return error;
	}

	/* Succeeded: the engine is up and every self-test job passed. */
	bcm2711_stage_mark(BCM2711_FAMILY_V3D, "ready: V3D %u.%u.%u, %u QPUs, %u Hz",
			   attach_v3d.version / 10U,
			   attach_v3d.version % 10U,
			   attach_v3d.revision,
			   attach_v3d.qpus,
			   attach_v3d.clock_hz);
	return 0;
}
