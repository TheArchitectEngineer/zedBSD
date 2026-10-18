/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the riscv64 PLIC driver and the numbered interrupts on a
 * fake controller: the registers are an array, reached through the HAL's
 * MMIO accessors, which the test supplies.
 *
 *   plic-host-test virt.dtb
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hal/hal.h>
#include "defs.h"
#include "fdt.h"
#include "irq.h"
#include "percpu.h"
#include "plic.h"

#define FAKE_SIZE	0x600000U

int host_sie;
void (*kernel_putc)(int c);

static uint8_t fake[FAKE_SIZE];
static uint64_t fake_base;
static struct riscv64_fdt_info info;
static struct riscv64_percpu record;
static uint32_t pending[8];
static unsigned pending_count;
static uint32_t completed[8];
static unsigned completed_count;
static int failures;
static int handled_source;

static void check(const char *name, int ok) { printf("%s %s\n", ok ? "PASS" : "FAIL", name); if (!ok) failures++; }

/* The offset of an MMIO pointer in the fake register file. */
static uint32_t offset_of(const volatile void *address)
{
	uint64_t value = (uint64_t)(uintptr_t)address - RISCV64_DEVICE_BASE - fake_base;
	if (value >= FAKE_SIZE) { fprintf(stderr, "access outside the PLIC: %llx\n", (unsigned long long)value); exit(2); }
	return (uint32_t)value;
}

uint32_t hal_mmio_read32(const volatile void *address)
{
	uint32_t at = offset_of(address);
	uint32_t value;
	/* The claim register of context 1 hands out the pending sources in turn. */
	if (at == 0x200000U + 1U * 0x1000U + 4U) {
		if (pending_count == 0) return 0;
		value = pending[0];
		memmove(pending, pending + 1, sizeof(pending[0]) * (--pending_count));
		return value;
	}
	memcpy(&value, fake + at, 4);
	return value;
}

void hal_mmio_write32(volatile void *address, uint32_t value)
{
	uint32_t at = offset_of(address);
	/* A write to the claim register of context 1 completes the source. */
	if (at == 0x200000U + 1U * 0x1000U + 4U) { completed[completed_count++] = value; return; }
	memcpy(fake + at, &value, 4);
}

static uint32_t reg(uint32_t at) { uint32_t value; memcpy(&value, fake + at, 4); return value; }

const struct riscv64_fdt_info *riscv64_boot_fdt(void) { return &info; }
struct riscv64_percpu *riscv64_percpu_current(void) { return &record; }
void hal_fatal(const char *file, int line, const char *s) { fprintf(stderr, "fatal %s:%d %s\n", file, line, s); exit(3); }
void riscv64_halt_forever(void) { exit(4); }
int hal_printf(const char *format, ...) { (void)format; return 0; }
void hal_cpu_park(void) { exit(5); }

static void handler(int irq, hal_irq_ack_t acknowledge, void *argument)
{
	(void)argument;
	handled_source = irq;
	hal_irq_send_eoi(acknowledge);
}

int main(int argc, char **argv)
{
	FILE *file;
	static unsigned char blob[65536];
	size_t size;
	int error;
	int marker;

	/* Reads the virt-shaped tree. */
	if (argc != 2) return 2;
	file = fopen(argv[1], "rb");
	if (file == NULL) return 2;
	size = fread(blob, 1, sizeof(blob), file);
	fclose(file);
	error = riscv64_fdt_parse(blob, size, &info);
	check("tree parsed", error == 0);
	fake_base = info.plic_base;
	record.hart_id = 0;

	/* Dirty registers, as a firmware might leave them, and one source claimed. */
	memset(fake, 0xff, sizeof(fake));
	pending[0] = 7; pending_count = 1;
	riscv64_plic_init();
	check("context of hart 0 is 1", record.has_plic_context && record.plic_context == 1);
	check("every priority zero", reg(4 * 1) == 0 && reg(4 * 0x5f) == 0);
	check("context 1 enables zero", reg(0x2000 + 0x80) == 0 && reg(0x2000 + 0x80 + 8) == 0);
	check("context 1 threshold open", reg(0x200000 + 0x1000) == 0);
	check("the left-over claim was completed", completed_count == 1 && completed[0] == 7);
	completed_count = 0;

	/* Registration enables the source but leaves it masked. */
	error = hal_irq_register(10, handler, &marker);
	check("register", error == HAL_OK);
	check("register sets the enable bit", (reg(0x2000 + 0x80) & (1U << 10)) != 0);
	check("register leaves the priority zero", reg(4 * 10) == 0);
	hal_irq_unmask(10);
	check("unmask sets priority one", reg(4 * 10) == 1);
	hal_irq_mask(10);
	check("mask sets priority zero", reg(4 * 10) == 0);
	check("mask keeps the enable bit", (reg(0x2000 + 0x80) & (1U << 10)) != 0);

	/* A claimed source goes to its handler, whose EOI completes it. */
	hal_irq_unmask(10);
	pending[0] = 10; pending_count = 1;
	riscv64_irq_take_external();
	check("handler called", handled_source == 10);
	check("completed once", completed_count == 1 && completed[0] == 10);
	check("no acknowledgement held", record.active_ack == HAL_IRQ_ACK_NONE);

	/* A source nobody serves is masked and completed. */
	completed_count = 0;
	hal_irq_unmask(20);
	pending[0] = 20; pending_count = 1;
	riscv64_irq_take_external();
	check("unexpected source completed", completed_count == 1 && completed[0] == 20);
	check("unexpected source masked", reg(4 * 20) == 0);

	/* The local acknowledgement ends nothing at the controller. */
	completed_count = 0;
	record.active_ack = RISCV64_ACK_LOCAL;
	hal_irq_send_eoi(RISCV64_ACK_LOCAL);
	check("local EOI completes nothing", completed_count == 0 && record.active_ack == HAL_IRQ_ACK_NONE);

	/* Unregistering masks and disables the source. */
	error = hal_irq_unregister(10, handler, &marker);
	check("unregister", error == HAL_OK && reg(4 * 10) == 0 && (reg(0x2000 + 0x80) & (1U << 10)) == 0);
	check("unknown source refused", hal_irq_register(0x60, handler, NULL) == HAL_ERR_INVALID);
	check("level-high accepted, edge refused",
	      hal_irq_set_mode(10, HAL_IRQ_TRIGGER_LEVEL, HAL_IRQ_POLARITY_HIGH) == HAL_OK &&
	      hal_irq_set_mode(10, HAL_IRQ_TRIGGER_EDGE, HAL_IRQ_POLARITY_HIGH) == HAL_ERR_UNSUPPORTED);

	printf("%s %d failed\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
