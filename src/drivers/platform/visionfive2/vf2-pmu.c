/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The JH7110's power management unit.
 *
 * Software switches a domain in two steps: it names the domain in the
 * turn-on or turn-off mode register, then writes a fixed three-byte
 * command to the encourage register.  The current power mode register
 * shows the outcome.  Hardware events can switch domains too; the GPU's
 * event is masked, so its firmware cannot switch its own domain off while
 * the driver does not manage the GPU's power.  The display's domain has
 * no hardware event, so only software switches it.
 */

#include "drivers/platform/visionfive2/vf2-pmu.h"
#include <kern/device-io.h>
#include <kern/lock.h>
#include <kern/pmem.h>
#include <uapi/errno.h>

/*
 * The unit's registers and the software command bytes.
 *
 * Values referenced:
 * StarFive JH7110 TRM Preliminary V2, section 3.6 PMU (Tables 3-12 to
 * 3-14, 3-19 and 3-26: the hardware event turn-on mask at 0x4 with the
 * GPU event in bit 7, the software turn-on and turn-off modes at 0xc and
 * 0x10, the software encourage at 0x44 and the current power mode at
 * 0x80; the command sequences 0xff, 0x05, 0x50 to turn on and 0xff, 0x0a,
 * 0xa0 to turn off); StarFive Linux JH7110_VisionFive2_6.12.y_devel
 * 4cecf169f38e drivers/pmdomain/starfive/jh71xx-pmu.c
 * (JH71XX_PMU_HW_EVENT_TURN_OFF 0x08, the turn-off mask the TRM does not
 * list).
 */
#define PMU_EVENT_ON_MASK	0x04U
#define PMU_EVENT_OFF_MASK	0x08U
#define PMU_SOFT_ON		0x0cU
#define PMU_SOFT_OFF		0x10U
#define PMU_ENCOURAGE		0x44U
#define PMU_POWER_STATE		0x80U
#define PMU_EVENT_GPU		0x80U
#define PMU_COMMAND_RESET	0xffU
#define PMU_COMMAND_ON_LOW	0x05U
#define PMU_COMMAND_ON_HIGH	0x50U
#define PMU_COMMAND_OFF_LOW	0x0aU
#define PMU_COMMAND_OFF_HIGH	0xa0U

/* The bytes of the unit's register block that are mapped. */
#define PMU_BLOCK_SIZE		0x1000U

/* How many state reads a switch waits before giving up. */
#define PMU_POLLS		100000U

/*
 * The unit's registers in the kernel's address space.
 *
 * Written only by drv_vf2_pmu_init(), which the board runs once on the
 * boot CPU before any driver attaches; NULL while the unit is absent.
 */
static volatile uint8_t *pmu_registers;

/*
 * Serializes the two-step command, whose second step must follow its own
 * first step.  A leaf lock, taken with interrupts held off.
 */
static struct spinlock pmu_lock = {
	{ 0 }, LOCK_RANK_DEVICE, "vf2 pmu", 0, 0
};

static volatile uint32_t *pmu_register(uint32_t offset);

/*
 * Finds the power management unit in the device tree and maps it.
 */
int
drv_vf2_pmu_init(
	const struct drv_fdt *fdt)
{
	uint64_t address;
	uint64_t size;
	uint32_t node;
	void *mapped;
	int error;

	/* Refuses a missing tree. */
	if (fdt == NULL)
		return EINVAL;

	/*
	 * Finds the unit's node.
	 *
	 * Value referenced:
	 * Linux v6.12 arch/riscv/boot/dts/starfive/jh7110.dtsi (pwrc,
	 * compatible "starfive,jh7110-pmu").
	 */
	error = drv_fdt_find_compatible(fdt, "starfive,jh7110-pmu", DRV_FDT_NO_NODE, &node);
	if (error != 0)
		return ENODEV;

	/* Reads where the registers are; the block must hold every register used. */
	error = drv_fdt_reg(fdt, node, 0, &address, &size);
	if (error != 0)
		return error;
	if (size < PMU_BLOCK_SIZE)
		return EINVAL;

	/* Maps the registers as device memory. */
	error = kern_device_map(address, PMU_BLOCK_SIZE, KERN_DEVICE_UNCACHED, &mapped);
	if (error != 0)
		return error;

	/* Succeeded: the unit can be driven. */
	pmu_registers = mapped;
	return 0;
}

/*
 * Turns a power domain on or off and waits until the unit reports it so.
 */
int
drv_vf2_pmu_set_power(
	unsigned domain,
	int on)
{
	unsigned long irq;
	uint32_t mask;
	uint32_t state;
	uint32_t events;
	unsigned poll;

	/*
	 * Refuses every domain but the GPU's and the display's, which their
	 * own drivers own.
	 */
	if (domain != DRV_VF2_PD_GPU && domain != DRV_VF2_PD_VOUT)
		return EPERM;

	/* Refuses the call before the unit is mapped. */
	if (pmu_registers == NULL)
		return ENODEV;

	/*
	 * Masks the GPU's hardware events when switching the GPU's domain,
	 * then names the domain and writes the command: the reset byte
	 * restarts the unit's command parser, the two bytes after it select
	 * turning on or off.  The display's domain has no hardware event
	 * (TRM Table 3-12 lists none for it), so nothing is masked for it.
	 *
	 * Procedure referenced:
	 * StarFive JH7110 TRM Preliminary V2, section 3.6.4 (SW encourage
	 * turn-on and turn-off sequences) and Tables 3-12 and 3-13 (the VOUT
	 * domain in bit 4 of the software modes).
	 */
	mask = 1U << domain;
	irq = spin_lock_irqsave(&pmu_lock);

	if (domain == DRV_VF2_PD_GPU) {
		events = kern_mmio_read32(pmu_register(PMU_EVENT_ON_MASK));
		kern_mmio_write32(pmu_register(PMU_EVENT_ON_MASK), events | PMU_EVENT_GPU);
		events = kern_mmio_read32(pmu_register(PMU_EVENT_OFF_MASK));
		kern_mmio_write32(pmu_register(PMU_EVENT_OFF_MASK), events | PMU_EVENT_GPU);
	}
	if (on) {
		kern_mmio_write32(pmu_register(PMU_SOFT_ON), mask);
		kern_mmio_write32(pmu_register(PMU_ENCOURAGE), PMU_COMMAND_RESET);
		kern_mmio_write32(pmu_register(PMU_ENCOURAGE), PMU_COMMAND_ON_LOW);
		kern_mmio_write32(pmu_register(PMU_ENCOURAGE), PMU_COMMAND_ON_HIGH);
	} else {
		kern_mmio_write32(pmu_register(PMU_SOFT_OFF), mask);
		kern_mmio_write32(pmu_register(PMU_ENCOURAGE), PMU_COMMAND_RESET);
		kern_mmio_write32(pmu_register(PMU_ENCOURAGE), PMU_COMMAND_OFF_LOW);
		kern_mmio_write32(pmu_register(PMU_ENCOURAGE), PMU_COMMAND_OFF_HIGH);
	}

	spin_unlock_irqrestore(&pmu_lock, irq);

	/* Waits for the domain's state bit to follow. */
	for (poll = 0; poll < PMU_POLLS; poll++) {
		/* Stops once the state matches the request. */
		state = kern_mmio_read32(pmu_register(PMU_POWER_STATE));
		if (on && (state & mask) != 0)
			return 0;
		if (!on && (state & mask) == 0)
			return 0;
	}

	/* Failed: the unit did not switch the domain. */
	return ETIMEDOUT;
}

/*
 * Reports whether a power domain is on.
 */
int
drv_vf2_pmu_powered(
	unsigned domain,
	int *on)
{
	uint32_t state;

	/* Refuses a missing result or a domain the register does not hold. */
	if (on == NULL || domain >= 32U)
		return EINVAL;

	/* Refuses the call before the unit is mapped. */
	if (pmu_registers == NULL)
		return ENODEV;

	/* Reads the domain's bit of the current power mode. */
	state = kern_mmio_read32(pmu_register(PMU_POWER_STATE));
	*on = 0;
	if ((state & (1U << domain)) != 0)
		*on = 1;

	/* Succeeded: the domain's state. */
	return 0;
}

/* Finds the kernel address of one of the unit's registers. */
static volatile uint32_t *
pmu_register(
	uint32_t offset)
{
	/* The registers are 32 bits wide at their byte offsets. */
	return (volatile uint32_t *)(pmu_registers + offset);
}
