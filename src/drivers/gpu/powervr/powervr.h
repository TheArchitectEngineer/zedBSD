/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Imagination PowerVR Rogue GPU core (the JH7110's BXE-4-32).
 *
 * The SoC's glue describes the core -- where its registers are, its
 * interrupt, how it reaches memory -- and supplies the steps that power
 * it, start its clocks and release its resets, which differ by SoC.  The
 * driver itself only talks to the core's registers.
 */

#ifndef KERN_DRIVERS_GPU_POWERVR_H
#define KERN_DRIVERS_GPU_POWERVR_H

#include <stdint.h>

/*
 * The SoC's steps around the core.
 *
 * Power_up powers the core's domain, starts its clocks at their rates and
 * releases its resets, and reports the core clock's real rate in hertz;
 * on failure it leaves everything off.  Power_down undoes it in reverse.
 */
struct drv_powervr_power {
	int (*power_up)(void *context, uint64_t *core_rate);
	void (*power_down)(void *context);
};

/*
 * One core as the SoC's glue describes it.
 *
 * Coherent says the core snoops the processor's caches; the JH7110's does
 * not, as far as is known, so its glue passes zero.  DMA limit is the
 * highest physical address the core is let name.  The description is
 * copied by drv_powervr_attach(); the power steps and their context must
 * live as long as the system.
 */
struct drv_powervr_description {
	uint64_t registers;
	uint64_t register_size;
	uint32_t interrupt;
	int coherent;
	uint64_t dma_limit;
	const struct drv_powervr_power *power;
	void *context;
};

/*
 * Brings the core up as far as reading its identity, and checks the
 * uncached view of memory its firmware's structures will need.
 *
 * ENODEV means the core is not the one the driver knows; it is then
 * powered down again.  Each outcome is one line of the boot log.
 */
int drv_powervr_attach(const struct drv_powervr_description *description);

/*
 * Publishes the loader's screen as the driver's display node.
 *
 * Runs before drv_powervr_attach(), so the display node is the first GPU
 * device and the render node the second; it does not depend on the core.
 * ENOENT means the loader left no screen.  Each outcome is one line of
 * the boot log.
 */
int drv_powervr_display_attach(void);

/*
 * Where the board's display controller is.
 *
 * Control holds the identity and interrupt registers; display holds the
 * planes and panels, at the controller's offsets from control's base.
 */
struct drv_powervr_dc_place {
	uint64_t control;
	uint64_t control_size;
	uint64_t display;
	uint64_t display_size;
	uint32_t interrupt;
};

/*
 * Reads the display controller the loader left on and decides whether its
 * screen can be taken over, without changing any of its registers.
 *
 * The decision, the controller's identity and the plane's settings are one
 * line of the boot log, which the screen shows.  The controller's power
 * domain and clocks must be on.  ENOENT means the loader left no screen.
 */
int drv_powervr_dc_probe(const struct drv_powervr_dc_place *place);

#endif
