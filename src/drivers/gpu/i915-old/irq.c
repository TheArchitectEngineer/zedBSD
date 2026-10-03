/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Gen11 graphics interrupts: one MSI vector, GT bank scanning and
 * identity decode.
 *
 * The master control register is cleared while sources are collected and
 * restored afterwards so a new event raises a fresh interrupt. Each pending
 * bank bit is resolved through the selector and identity registers into an
 * engine class, instance and 16-bit interrupt word.
 *
 * References: Linux i915_irq.c gen11_irq_handler and
 * gt/intel_gt_irq.c gen11_gt_irq_postinstall, gen11_gt_irq_reset,
 * gen11_gt_bank_handler and gen11_gt_engine_identity, re-expressed
 * for this driver.
 */

#include "internal.h"

#include <kern/device-io.h>
#include <kern/klog.h>

#include <errno.h>

/* Interrupt sources this driver enables on RCS0 and BCS0. */
#define I915_ENGINE_IRQS	(GT_RENDER_USER_INTERRUPT | GT_CS_MASTER_ERROR_INTERRUPT | \
				 GT_CONTEXT_SWITCH_INTERRUPT | GT_WAIT_SEMAPHORE_INTERRUPT)

static void i915_irq_enable(struct i915_device *device);
static void i915_irq_bank(struct i915_device *device, unsigned bank);
static uint32_t i915_irq_identity(struct i915_device *device, unsigned bank, unsigned bit);
static void i915_irq_dispatch(struct i915_device *device, uint32_t identity);
static void i915_irq_engine(struct i915_device *device, unsigned engine, uint16_t sources);

/*
 * Allocates one MSI vector, installs the handler and enables the GT sources.
 */
int
drv_i915_irq_start(
	struct i915_device *device)
{
	unsigned count;
	int error;

	/* All sources stay disabled until the handler is installed. */
	drv_i915_irq_reset(device);

	/* One message-signalled vector serves both engines through the bank registers. */
	count = 0U;
	error = drv_pci_device_allocate_irqs(
		device->pci,
		DRV_PCI_IRQ_ALLOW_MSI,
		1U,
		1U,
		&device->interrupt,
		&count);
	if (error != 0)
		return error;

	/* A partial allocation remains recorded so stop can release it. */
	device->interrupt_count = count;
	if (count != 1U)
		return EIO;

	/* The handler must be reachable before any enable register is written. */
	error = drv_pci_device_establish_irq(
		device->pci,
		&device->interrupt,
		drv_i915_irq_handler,
		device,
		"i915",
		&device->interrupt_cookie);
	if (error != 0)
		return error;

	/* Engine sources and the master bit are enabled only now. */
	i915_irq_enable(device);
	device->irq_enabled = 1U;
	kern_logf("i915: MSI vector %u enabled for RCS0/BCS0\n", device->interrupt.vector);

	/* Succeeded: user interrupts and context switches reach the handler. */
	return 0;
}

/*
 * Disables every source, removes the handler and frees the vector.
 */
int
drv_i915_irq_stop(
	struct i915_device *device)
{
	int error;

	/* Hardware stops raising events before the handler is withdrawn. */
	if (device->regs.address != NULL)
		drv_i915_irq_reset(device);
	device->irq_enabled = 0U;

	/* Checked removal leaves the cookie in place while a dispatch is still running. */
	if (device->interrupt_cookie != NULL) {
		error = drv_pci_device_disestablish_irq_checked(device->pci, device->interrupt_cookie);
		if (error != 0)
			return error;

		/* A null cookie proves no handler invocation can reach the device state. */
		device->interrupt_cookie = NULL;
	}

	/* The vector returns to PCI only after the handler is gone. */
	if (device->interrupt_count != 0U) {
		drv_pci_device_free_irqs(device->pci, &device->interrupt, device->interrupt_count);
		device->interrupt_count = 0U;
	}

	/* Succeeded: no interrupt path references the device any more. */
	return 0;
}

/*
 * Masks every GT source and clears the master enable.
 */
void
drv_i915_irq_reset(
	struct i915_device *device)
{
	/* The master bit gates all banks; clearing it first stops new deliveries. */
	drv_i915_write32(device, GEN11_GFX_MSTR_IRQ, 0U);

	/* Class enables are cleared for every engine class, used or not. */
	drv_i915_write32(device, GEN11_RENDER_COPY_INTR_ENABLE, 0U);
	drv_i915_write32(device, GEN11_VCS_VECS_INTR_ENABLE, 0U);

	/* An all-ones mask silences each engine instance. */
	drv_i915_write32(device, GEN11_RCS0_RSVD_INTR_MASK, 0xffffffffU);
	drv_i915_write32(device, GEN11_BCS_RSVD_INTR_MASK, 0xffffffffU);
	drv_i915_write32(device, GEN11_VCS0_VCS1_INTR_MASK, 0xffffffffU);
	drv_i915_write32(device, GEN11_VCS2_VCS3_INTR_MASK, 0xffffffffU);
	drv_i915_write32(device, GEN11_VECS0_VECS1_INTR_MASK, 0xffffffffU);

	/* Power management, GuC and crypto sources are never used by this driver. */
	drv_i915_write32(device, GEN11_GPM_WGBOXPERF_INTR_ENABLE, 0U);
	drv_i915_write32(device, GEN11_GPM_WGBOXPERF_INTR_MASK, 0xffffffffU);
	drv_i915_write32(device, GEN11_GUC_SG_INTR_ENABLE, 0U);
	drv_i915_write32(device, GEN11_GUC_SG_INTR_MASK, 0xffffffffU);
	drv_i915_write32(device, GEN11_CRYPTO_RSVD_INTR_ENABLE, 0U);
	drv_i915_write32(device, GEN11_CRYPTO_RSVD_INTR_MASK, 0xffffffffU);

	/* Reading the master register completes the posted writes. */
	(void)drv_i915_read32(device, GEN11_GFX_MSTR_IRQ);
}

/*
 * Serves one device interrupt: collects every pending bank source and re-arms.
 */
int
drv_i915_irq_handler(
	void *argument)
{
	struct i915_device *device;
	uint32_t master;
	unsigned bank;

	/* Clearing the master bit holds further deliveries while sources are read. */
	device = argument;
	drv_i915_write32(device, GEN11_GFX_MSTR_IRQ, 0U);
	master = drv_i915_read32(device, GEN11_GFX_MSTR_IRQ);

	/* A vector with nothing pending belongs to nobody; the master bit is restored. */
	if ((master & ~GEN11_MASTER_IRQ) == 0U) {
		drv_i915_write32(device, GEN11_GFX_MSTR_IRQ, GEN11_MASTER_IRQ);
		return 0;
	}

	/* Counts every dispatch so the selftest can prove the vector fires. */
	spin_lock(&device->irq_lock);

	device->irq_total++;

	spin_unlock(&device->irq_lock);

	/* Each GT bank is scanned independently; display and GU-misc bits are ignored. */
	for (bank = 0U; bank < I915_IRQ_BANKS; bank++) {
		/* Only banks with a pending summary bit have sources to resolve. */
		if ((master & GEN11_GT_DW_IRQ(bank)) != 0U)
			i915_irq_bank(device, bank);
	}

	/* Re-arming lets any event raised during the scan deliver a new interrupt. */
	drv_i915_write32(device, GEN11_GFX_MSTR_IRQ, GEN11_MASTER_IRQ);

	/* Succeeded: this interrupt belonged to the graphics device. */
	return 1;
}

/* Enables user, error, context-switch and semaphore sources on RCS0 and BCS0. */
static void
i915_irq_enable(
	struct i915_device *device)
{
	uint32_t enable;
	uint32_t mask;

	/* The class enable carries the sources for both instances of a class pair. */
	enable = (I915_ENGINE_IRQS << 16) | I915_ENGINE_IRQS;
	drv_i915_write32(device, GEN11_RENDER_COPY_INTR_ENABLE, enable);

	/* Unmasking clears the upper-half bits for instance zero of each used class. */
	mask = I915_ENGINE_IRQS << 16;
	drv_i915_write32(device, GEN11_RCS0_RSVD_INTR_MASK, ~mask);
	drv_i915_write32(device, GEN11_BCS_RSVD_INTR_MASK, ~mask);

	/* The master bit is the last write so no source fires half-configured. */
	kern_io_write_barrier();
	drv_i915_write32(device, GEN11_GFX_MSTR_IRQ, GEN11_MASTER_IRQ);
	(void)drv_i915_read32(device, GEN11_GFX_MSTR_IRQ);
}

/* Resolves every pending bit of one bank through its identity register. */
static void
i915_irq_bank(
	struct i915_device *device,
	unsigned bank)
{
	uint32_t pending;
	uint32_t identity;
	unsigned bit;

	/* The bank word lists which of its 32 sources are pending. */
	pending = drv_i915_read32(device, GEN11_GT_INTR_DW(bank));

	/* Each source is decoded and acknowledged individually. */
	for (bit = 0U; bit < 32U; bit++) {
		/* Sources without a pending bit have no identity to read. */
		if ((pending & (1U << bit)) == 0U)
			continue;

		/* An identity that never became valid is counted and skipped. */
		identity = i915_irq_identity(device, bank, bit);
		if (identity == 0U)
			continue;

		i915_irq_dispatch(device, identity);
	}

	/* Clearing the bank word after the sources were served ends this delivery. */
	drv_i915_write32(device, GEN11_GT_INTR_DW(bank), pending);
}

/* Selects one bank bit and waits for its identity register to become valid. */
static uint32_t
i915_irq_identity(
	struct i915_device *device,
	unsigned bank,
	unsigned bit)
{
	uint32_t identity;
	unsigned poll;

	/* The selector chooses which pending source the identity register describes. */
	drv_i915_write32(device, GEN11_IIR_REG_SELECTOR(bank), 1U << bit);

	/* Hardware needs a short while to latch the identity; the bound is about 100 µs. */
	identity = 0U;
	for (poll = 0U; poll < I915_IRQ_IDENTITY_POLLS; poll++) {
		/* The valid bit means class, instance and sources can be trusted. */
		identity = drv_i915_read32(device, GEN11_INTR_IDENTITY_REG(bank));
		if ((identity & GEN11_INTR_DATA_VALID) != 0U)
			break;

		kern_compiler_barrier();
	}

	/* A source that never presented an identity is left for the next interrupt. */
	if ((identity & GEN11_INTR_DATA_VALID) == 0U) {
		spin_lock(&device->irq_lock);

		device->irq_identity_timeouts++;

		spin_unlock(&device->irq_lock);
		return 0U;
	}

	/* Writing the valid bit back acknowledges the identity and frees the register. */
	drv_i915_write32(device, GEN11_INTR_IDENTITY_REG(bank), GEN11_INTR_DATA_VALID);

	return identity;
}

/* Routes a decoded identity to the engine it names. */
static void
i915_irq_dispatch(
	struct i915_device *device,
	uint32_t identity)
{
	unsigned class;
	unsigned instance;
	uint16_t sources;

	/* The identity packs class, instance and the pending source word. */
	class = GEN11_INTR_ENGINE_CLASS(identity);
	instance = GEN11_INTR_ENGINE_INSTANCE(identity);
	sources = (uint16_t)GEN11_INTR_ENGINE_INTR(identity);

	/* Only RCS0 and BCS0 are initialized; every other identity is counted as unknown. */
	if (class == RENDER_CLASS && instance == 0U) {
		i915_irq_engine(device, I915_ENGINE_RCS0, sources);
	} else if (class == COPY_ENGINE_CLASS && instance == 0U) {
		i915_irq_engine(device, I915_ENGINE_BCS0, sources);
	} else {
		spin_lock(&device->irq_lock);

		device->irq_unknown++;

		spin_unlock(&device->irq_lock);
	}
}

/* Records the sources one engine raised and hands them to the engine code. */
static void
i915_irq_engine(
	struct i915_device *device,
	unsigned engine,
	uint16_t sources)
{
	uint32_t error_status;
	uint32_t base;

	/* Each engine slot owns one register block. */
	base = I915_BCS0_BASE;
	if (engine == I915_ENGINE_RCS0)
		base = I915_RCS0_BASE;

	/* A command streamer error is read now so the value survives the acknowledgment. */
	error_status = 0U;
	if ((sources & GT_CS_MASTER_ERROR_INTERRUPT) != 0U)
		error_status = drv_i915_read32(device, RING_EIR(base));

	/* The counters are the observable evidence until request retirement exists. */
	spin_lock(&device->irq_lock);

	/* A user interrupt follows every breadcrumb a request writes. */
	if ((sources & GT_RENDER_USER_INTERRUPT) != 0U)
		device->user_interrupts[engine]++;

	/* A context switch means the CSB has a new entry to consume. */
	if ((sources & GT_CONTEXT_SWITCH_INTERRUPT) != 0U)
		device->context_switches[engine]++;

	/* The last error value lets a later reset report what the parser rejected. */
	if ((sources & GT_CS_MASTER_ERROR_INTERRUPT) != 0U) {
		device->error_interrupts[engine]++;
		device->last_error[engine] = error_status;
	}

	spin_unlock(&device->irq_lock);

	/* Retirement and the next submission are the engine's business. */
	drv_i915_engine_interrupt(device, engine, sources);
}
