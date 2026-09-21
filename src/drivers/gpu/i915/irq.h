/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Device interrupts: install, uninstall and the top-level handler.
 *
 * This is the Gen11+ path of the reference's intel_irq_install() and
 * intel_irq_uninstall():
 *
 *   install    mark interrupts enabled (before any source is enabled),
 *              reset every source, attach the handler to the MSI vector,
 *              then enable the sources and the master control
 *   uninstall  reset every source, clear the enabled marks, then detach the
 *              handler and wait until no invocation is running
 *
 * The MSI vector itself is allocated and freed by the PCI part; this part
 * only attaches and detaches its handler, which maps the HAL's split MSI
 * contract onto the reference's request_irq() and free_irq().
 *
 * The HAL handler returns nothing and sends the EOI itself, where Linux
 * returns IRQ_HANDLED or IRQ_NONE.  With MSI, which is never shared, that
 * answer only feeds spurious-interrupt detection, so the outcome is counted
 * in the device instead.
 *
 * The GT half (the engine interrupt banks) lives here.  The display half
 * (display engine pipes, ports and misc, the south display block, vblank)
 * is reached through a separate operations table that the display part
 * binds; the top-level flow calls it at exactly the points where the
 * reference calls the display functions.
 */

#ifndef DRIVERS_GPU_I915_IRQ_H
#define DRIVERS_GPU_I915_IRQ_H

#include <stdint.h>

struct i915_mmio;
struct i915_gt_info;

/*
 * Which arm of gen11_gt_irq_postinstall() the submission backend selects.
 *
 * With execlists the driver owns the context switch and needs the command
 * streamer interrupts; with GuC submission those are left to the GuC.
 */
enum i915_gt_submission {
	I915_SUBMISSION_EXECLISTS = 0,
	I915_SUBMISSION_GUC
};

/*
 * The display half of the interrupt flow.
 *
 * The display part binds one table and its context to the interrupt device
 * before install.  Every entry is called with that context.  A missing
 * table means display interrupts are not connected: no display source is
 * reset, enabled or acknowledged.
 */
struct i915_irq_display_ops {
	/*
	 * Tells the display power wells whether interrupts are enabled, so
	 * that a well brought up later enables its pipe interrupts only while
	 * they are.  Called with 1 before the reset at install, with 0 when
	 * the handler attach fails, and with 0 after the reset at uninstall.
	 */
	void (*set_irqs_enabled)(void *context, int enabled);

	/*
	 * Reports display state that should have been released before
	 * uninstall, such as held vblank references.  Called first in
	 * uninstall, before any register is touched.
	 */
	void (*uninstall_check)(void *context);

	/*
	 * Masks, disables and clears every display source, including the
	 * south display block (gen11_display_irq_reset()).  Called after the
	 * GT reset and before the GU_MISC and PCU resets.
	 */
	void (*reset)(void *context);

	/*
	 * Enables the display sources and the display interrupt control
	 * (gen11_de_irq_postinstall(), which also covers the south display
	 * block).  Called after the GT postinstall and before the GU_MISC
	 * enable.
	 */
	void (*postinstall)(void *context);

	/*
	 * Reads and acknowledges every pending display source
	 * (gen11_display_irq_handler()).  Called from the handler with the
	 * master control off, after the GT banks and before the GU_MISC
	 * acknowledge, only when the master control reports a display
	 * interrupt.
	 */
	void (*handle)(void *context, uint32_t master_ctl);

	/*
	 * Delivers a graphics system event to the OpRegion
	 * (intel_opregion_asle_intr()).  Called from the handler after the
	 * master control has been turned back on.
	 */
	void (*gse)(void *context);
};

/*
 * The interrupt state of one device.
 *
 * It lives inside the device from before install to after uninstall.  The
 * configuration fields are set by the device start before install; the
 * counters are written by the handler and read by the diagnostics, which
 * is why the handler-side ones are volatile.
 */
struct i915_irq_dev {
	/* The register access of the device. */
	struct i915_mmio *m;

	/*
	 * The GT engine objects.  The GT half of the handler maps an
	 * interrupt identity (class, instance) back to an engine through
	 * them; NULL means no engine is known yet.
	 */
	struct i915_gt_info *gt;

	/* The submission backend, one of enum i915_gt_submission. */
	int submission;

	/*
	 * The display half and its context.  NULL means display interrupts
	 * are not connected.
	 */
	const struct i915_irq_display_ops *display_ops;
	void *display_context;

	/*
	 * The reference's runtime_pm.irqs_enabled and i915->irq_enabled.
	 * Both are set before any source is enabled, so that the postinstall
	 * steps see interrupts as enabled.
	 */
	int irqs_enabled;
	int irq_enabled;

	/* The GT masks postinstall computed, kept for the diagnostics. */
	uint32_t gt_irqs, gt_dmask, gt_smask;

	/* The logical MSI vector the PCI part allocated. */
	int msi_irq;

	/* Nonzero while the handler is attached to the vector. */
	int handler_attached;

	/*
	 * What the handler saw.  An MSI handler cannot report IRQ_NONE
	 * upwards, so every outcome is counted here.
	 */
	volatile unsigned irq_count;
	volatile unsigned irq_none_count;
	volatile unsigned irq_handled_count;
	volatile uint32_t last_master_ctl;
	volatile uint32_t last_gu_misc_iir;

	/* GU_MISC graphics system events handed to the OpRegion. */
	volatile unsigned gse_count;

	/* Handler invocations that found GT and display work. */
	volatile unsigned gt_irq_count, display_irq_count;

	/*
	 * GT source accounting.  Every asserted bank must be acknowledged,
	 * and every identity handshake completed, or the master line stays
	 * asserted.  The engine bottom halves (breadcrumbs, the CSB tasklet)
	 * belong to the submission backend; what was acknowledged is counted.
	 */
	volatile unsigned gt_bank_acks[2];
	volatile unsigned gt_identity_reads;
	volatile unsigned gt_identity_invalid;
	volatile unsigned gt_engine_intrs;
	volatile unsigned gt_other_intrs;
	volatile unsigned gt_unknown_class;
	volatile unsigned gt_user_intr, gt_ctx_switch_intr;
	volatile unsigned gt_error_intr, gt_semaphore_intr;
	volatile uint32_t last_gt_intr_dw[2];
	volatile uint32_t last_gt_identity;

	/*
	 * Handler entries and exits.  drv_i915_synchronize_irq() waits until
	 * the exits reach the entries it saw.
	 */
	volatile unsigned handler_entries, handler_exits;

	/* drv_i915_synchronize_irq() calls and the ways they failed. */
	unsigned sync_calls, sync_timeouts, sync_time_faults;

	/* Register writes of the reset and postinstall steps. */
	unsigned reset_writes;
	unsigned postinstall_writes;

	/* Nonzero once postinstall and the master enable were reached. */
	int reached_postinstall;
	int reached_master_enable;
};

int drv_i915_irq_install(struct i915_irq_dev *irq);
void drv_i915_irq_uninstall(struct i915_irq_dev *irq);
int drv_i915_synchronize_irq(struct i915_irq_dev *irq);

void drv_i915_irq_reset(struct i915_irq_dev *irq);
void drv_i915_irq_postinstall(struct i915_irq_dev *irq);
void drv_i915_gen11_gt_irq_reset(struct i915_irq_dev *irq);
void drv_i915_gen11_gt_irq_postinstall(struct i915_irq_dev *irq);
void drv_i915_gen11_gt_irq_handler(struct i915_irq_dev *irq, uint32_t master_ctl);

void drv_i915_gen3_irq_reset(struct i915_irq_dev *irq, uint32_t imr, uint32_t iir, uint32_t ier);
void drv_i915_gen3_assert_iir_is_zero(struct i915_irq_dev *irq, uint32_t iir);
void drv_i915_gen3_irq_init(struct i915_irq_dev *irq, uint32_t imr, uint32_t imr_value, uint32_t ier, uint32_t ier_value, uint32_t iir);

#endif
