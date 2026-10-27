/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the Raspberry Pi 5's MIP module (rpi5-mip.c).
 *
 * The module is compiled unchanged against the Pi 5's device trees and a
 * fake interrupt layer that records each line's trigger, handler, mask and
 * end of interrupt.  The MIP's registers are host memory.
 *
 * Usage: rpi5-mip-host-test C1.dtb D0.dtb
 */

#include <drivers/platform/rpi5/rpi5-mip.h>
#include <kern/clock.h>
#include <kern/device-io.h>
#include <kern/irq.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/lock.h>
#include <kern/pmem.h>
#include <uapi/errno.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;

#define CHECK(expression)                                                  \
	do {                                                                \
		checks++;                                                   \
		if (!(expression)) {                                       \
			fprintf(stderr, "rpi5-mip: failed at %s:%d: %s\n", \
			    __FILE__, __LINE__, #expression);                 \
			exit(EXIT_FAILURE);                                  \
		}                                                           \
	} while (0)

#define LINES 1024U

/* The MIP's registers, as host memory. */
static uint32_t mip_registers[0xc0U / 4U];
static uint64_t mapped_address;

/* What the fake interrupt layer recorded, per line. */
static kern_irq_handler_t handlers[LINES];
static void *arguments[LINES];
static int edge[LINES];
static int masked[LINES];
static int failing_line;
static unsigned eois;
static unsigned lock_depth;

/* What a message's handler did. */
static unsigned runs;
static unsigned empty_runs;

static unsigned char *read_file(const char *path, size_t *size);
static uint32_t find_mip(const struct drv_fdt *fdt, uint64_t base);
static int message_run(void *argument);
static void test_tree(const char *path);

int
main(
	int argc,
	char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: %s C1.dtb D0.dtb\n", argv[0]);
		return EXIT_FAILURE;
	}

	test_tree(argv[1]);
	test_tree(argv[2]);
	printf("rpi5-mip: %u checks passed\n", checks);
	return EXIT_SUCCESS;
}

/* Runs every case against one stepping's tree. */
static void
test_tree(
	const char *path)
{
	struct drv_rpi5_mip *mip;
	struct drv_rpi5_mip *mip0;
	struct drv_fdt fdt;
	unsigned char *blob;
	unsigned handles[9];
	unsigned index;
	uint64_t address;
	uint32_t data;
	uint32_t node;
	size_t size;
	int irq;

	blob = read_file(path, &size);
	CHECK(drv_fdt_open(&fdt, blob, size) == 0);
	memset(handlers, 0, sizeof(handlers));
	memset(edge, 0, sizeof(edge));
	memset(masked, 0, sizeof(masked));
	failing_line = -1;

	/* A node that is not a MIP is refused. */
	CHECK(drv_fdt_find_compatible(&fdt, "brcm,bcm2712-pcie", DRV_FDT_NO_NODE, &node) == 0);
	CHECK(drv_rpi5_mip_start(&fdt, node, &mip) == ENODEV);

	/* MIP1: lines 287 to 294, made edges and masked, the VPU masked out. */
	memset(mip_registers, 0x5a, sizeof(mip_registers));
	node = find_mip(&fdt, 0x1000131000ULL);
	CHECK(drv_rpi5_mip_start(&fdt, node, &mip) == 0);
	CHECK(mapped_address == 0x1000131000ULL);
	for (irq = 287; irq <= 294; irq++) {
		CHECK(edge[irq] == 1);
		CHECK(masked[irq] == 1);
	}
	CHECK(edge[286] == 0);
	CHECK(edge[295] == 0);
	CHECK(mip_registers[0x60U / 4U] == 0xffffffffU);
	CHECK(mip_registers[0x70U / 4U] == 0xffffffffU);
	CHECK(mip_registers[0x20U / 4U] == 0xffffffffU);
	CHECK(mip_registers[0x30U / 4U] == 0xffffffffU);
	CHECK(mip_registers[0x40U / 4U] == 0);
	CHECK(mip_registers[0x50U / 4U] == 0);

	/* Eight messages: numbers 8 to 15 to the doorbell, each on its line, unmasked. */
	for (index = 0; index < 8U; index++) {
		CHECK(drv_rpi5_mip_establish(mip, message_run, &runs, &handles[index], &address, &data) == 0);
		CHECK(handles[index] == index);
		CHECK(address == 0xfffffff000ULL);
		CHECK(data == 8U + index);
		CHECK(handlers[287 + index] != NULL);
		CHECK(masked[287 + index] == 0);
	}
	CHECK(drv_rpi5_mip_establish(mip, message_run, &runs, &handles[8], &address, &data) == ENOSPC);

	/* An interrupt runs the handler once and ends the interrupt once. */
	runs = 0;
	empty_runs = 0;
	eois = 0;
	handlers[287](287, (kern_irq_ack_t)1, arguments[287]);
	CHECK(runs == 1U);
	CHECK(eois == 1U);
	CHECK(lock_depth == 0);

	/* A handler that finds its work late is run again until it does. */
	runs = 0;
	empty_runs = 2U;
	handlers[288](288, (kern_irq_ack_t)2, arguments[288]);
	CHECK(runs == 3U);
	CHECK(eois == 2U);

	/* A handler that never finds work is run nine times, then the interrupt ends. */
	runs = 0;
	empty_runs = 100U;
	handlers[289](289, (kern_irq_ack_t)3, arguments[289]);
	CHECK(runs == 9U);
	CHECK(eois == 3U);

	/* Freeing a message masks and empties its line, which can be taken again. */
	CHECK(drv_rpi5_mip_disestablish(mip, 2U) == 0);
	CHECK(handlers[289] == NULL);
	CHECK(masked[289] == 1);
	CHECK(drv_rpi5_mip_disestablish(mip, 2U) == EINVAL);
	CHECK(drv_rpi5_mip_disestablish(mip, 8U) == EINVAL);
	CHECK(drv_rpi5_mip_establish(mip, message_run, &runs, &handles[8], &address, &data) == 0);
	CHECK(handles[8] == 2U);
	CHECK(data == 10U);

	/* A line the GIC does not have keeps the MIP from starting. */
	node = find_mip(&fdt, 0x1000131000ULL);
	failing_line = 292;
	CHECK(drv_rpi5_mip_start(&fdt, node, &mip) == EINVAL);
	failing_line = -1;

	/* MIP0 has 64 lines from 160; the module hands out the first 32. */
	node = find_mip(&fdt, 0x1000130000ULL);
	CHECK(drv_rpi5_mip_start(&fdt, node, &mip0) == 0);
	CHECK(edge[160] == 1);
	CHECK(edge[191] == 1);
	CHECK(edge[192] == 0);
	CHECK(drv_rpi5_mip_establish(mip0, message_run, &runs, &handles[0], &address, &data) == 0);
	CHECK(data == 0);
	CHECK(handlers[160] != NULL);

	free(blob);
}

/* Finds the MIP whose registers are at base. */
static uint32_t
find_mip(
	const struct drv_fdt *fdt,
	uint64_t base)
{
	uint64_t address;
	uint64_t size;
	uint32_t node;

	node = DRV_FDT_NO_NODE;
	for (;;) {
		CHECK(drv_fdt_find_compatible(fdt, "brcm,bcm2712-mip", node, &node) == 0);
		CHECK(drv_fdt_reg(fdt, node, 0, &address, &size) == 0);
		if (address == base)
			return node;
	}
}

/* A message handler: no work while empty_runs lasts, then work. */
static int
message_run(
	void *argument)
{
	CHECK(argument == &runs);
	CHECK(lock_depth == 0);
	runs++;
	if (empty_runs != 0) {
		empty_runs--;
		return 0;
	}
	return 1;
}

static unsigned char *
read_file(
	const char *path,
	size_t *size)
{
	unsigned char *buffer;
	FILE *file;
	long length;

	file = fopen(path, "rb");
	CHECK(file != NULL);
	CHECK(fseek(file, 0, SEEK_END) == 0);
	length = ftell(file);
	CHECK(length > 0);
	CHECK(fseek(file, 0, SEEK_SET) == 0);
	buffer = malloc((size_t)length);
	CHECK(buffer != NULL);
	CHECK(fread(buffer, 1, (size_t)length, file) == (size_t)length);
	fclose(file);
	*size = (size_t)length;
	return buffer;
}

int
kern_irq_register(
	int irq,
	kern_irq_handler_t handler,
	void *argument)
{
	CHECK(irq >= 0 && (unsigned)irq < LINES);
	CHECK(handlers[irq] == NULL);
	CHECK(lock_depth == 0);
	handlers[irq] = handler;
	arguments[irq] = argument;
	return 0;
}

int
kern_irq_unregister(
	int irq,
	kern_irq_handler_t handler,
	void *argument)
{
	CHECK(handlers[irq] == handler);
	CHECK(arguments[irq] == argument);
	handlers[irq] = NULL;
	arguments[irq] = NULL;
	return 0;
}

int
kern_irq_set_mode(
	int irq,
	unsigned trigger,
	unsigned polarity)
{
	CHECK(masked[irq] == 1);
	CHECK(lock_depth == 0);
	if (irq == failing_line)
		return EINVAL;
	CHECK(trigger == KERN_IRQ_TRIGGER_EDGE);
	CHECK(polarity == KERN_IRQ_POLARITY_HIGH);
	edge[irq] = 1;
	return 0;
}

void
kern_irq_mask(
	int irq)
{
	masked[irq] = 1;
}

void
kern_irq_unmask(
	int irq)
{
	CHECK(handlers[irq] != NULL);
	masked[irq] = 0;
}

void
kern_irq_send_eoi(
	kern_irq_ack_t acknowledge)
{
	CHECK(acknowledge != KERN_IRQ_ACK_NONE);
	CHECK(lock_depth == 0);
	eois++;
}

uint32_t
kern_mmio_read32(
	const volatile void *address)
{
	return *(const volatile uint32_t *)address;
}

void
kern_mmio_write32(
	volatile void *address,
	uint32_t value)
{
	*(volatile uint32_t *)address = value;
}

int
kern_device_map(
	uint64_t address,
	size_t size,
	unsigned attributes,
	void **mapped)
{
	CHECK(attributes == KERN_DEVICE_UNCACHED);
	CHECK(size == sizeof(mip_registers));
	mapped_address = address;
	*mapped = mip_registers;
	return 0;
}

int
kern_device_unmap(
	void *mapped,
	size_t size)
{
	(void)mapped;
	(void)size;
	return 0;
}

void
kern_usleep_range(
	unsigned min_us,
	unsigned max_us)
{
	CHECK(min_us <= max_us);
}

void
kern_logf(
	const char *format,
	...)
{
	va_list arguments_list;

	va_start(arguments_list, format);
	printf("  log: ");
	vprintf(format, arguments_list);
	va_end(arguments_list);
}

void *
kern_malloc(
	size_t size)
{
	return malloc(size);
}

void
kern_free(
	void *pointer)
{
	free(pointer);
}

void *
kern_memset(
	void *destination,
	int value,
	size_t size)
{
	return memset(destination, value, size);
}

void
spin_init(
	struct spinlock *lock,
	enum lock_rank rank,
	const char *name)
{
	memset(lock, 0, sizeof(*lock));
	lock->rank = rank;
	lock->name = name;
}

unsigned long
spin_lock_irqsave(
	struct spinlock *lock)
{
	(void)lock;
	CHECK(lock_depth == 0);
	lock_depth++;
	return 1;
}

void
spin_unlock_irqrestore(
	struct spinlock *lock,
	unsigned long enabled)
{
	(void)lock;
	CHECK(enabled == 1);
	CHECK(lock_depth == 1);
	lock_depth--;
}
