/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the 16550 receive driver and the serial console's byte
 * translation on a fake UART: the registers are an array reached through
 * the kernel's MMIO accessors, which the test supplies, and the interrupt
 * is called by hand.
 *
 *   serial-host-test virt.dtb dw-uart.dtb
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <drivers/generic/fdt.h>
#include <drivers/generic/serial-console.h>
#include <drivers/serial/ns16550.h>
#include <hal/hal.h>
#include <kern/irq.h>
#include <kern/pmem.h>
#include <kern/text-display.h>
#include <kern/tty.h>

#define WINDOW_BYTES	0x10000U
#define RX_MAX		256U

static uint8_t window[WINDOW_BYTES];
static uint64_t mapped_physical;
static size_t mapped_size;
static unsigned shift;
static unsigned width_seen;
static uint8_t rx[RX_MAX];
static unsigned rx_head;
static unsigned rx_tail;
static uint32_t ier;
static uint32_t iir = 0x01U;
static unsigned usr_reads;
static int registered_irq = -1;
static kern_irq_handler_t registered_handler;
static void *registered_argument;
static int unmasked_irq = -1;
static unsigned eoi_count;
static uint8_t typed[RX_MAX];
static unsigned typed_count;
static uint8_t received[RX_MAX];
static unsigned received_count;
static int failures;

static void
check(int condition, const char *what)
{
	if (condition) {
		printf("PASS %s\n", what);
	} else {
		printf("FAIL %s\n", what);
		failures++;
	}
}

static uint8_t *
read_file(const char *path, size_t *length)
{
	FILE *file;
	uint8_t *data;
	long size;

	file = fopen(path, "rb");
	if (file == NULL) {
		perror(path);
		exit(2);
	}
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)size);
	if (data == NULL || fread(data, 1, (size_t)size, file) != (size_t)size) {
		fprintf(stderr, "%s: read failed\n", path);
		exit(2);
	}
	fclose(file);
	*length = (size_t)size;
	return data;
}

static unsigned
register_number(const volatile void *address)
{
	return (unsigned)(((const uint8_t *)address - window) >> shift);
}

static uint32_t
fake_read(const volatile void *address, unsigned width)
{
	unsigned number;

	width_seen = width;
	number = register_number(address);
	switch (number) {
	case 0:
		if (rx_head != rx_tail)
			return rx[rx_head++ % RX_MAX];
		return 0;
	case 1:
		return ier;
	case 2:
		return iir;
	case 5:
		return rx_head != rx_tail ? 0x61U : 0x60U;
	case 0x1f:
		usr_reads++;
		iir = 0x01U;
		return 0;
	default:
		return 0;
	}
}

static void
fake_write(volatile void *address, uint32_t value, unsigned width)
{
	width_seen = width;
	if (register_number(address) == 1)
		ier = value;
}

uint8_t kern_mmio_read8(const volatile void *address) { return (uint8_t)fake_read(address, 1); }
uint32_t kern_mmio_read32(const volatile void *address) { return fake_read(address, 4); }
void kern_mmio_write8(volatile void *address, uint8_t value) { fake_write(address, value, 1); }
void kern_mmio_write32(volatile void *address, uint32_t value) { fake_write(address, value, 4); }

int
kern_device_map(uint64_t address, size_t size, unsigned attributes, void **mapped)
{
	(void)attributes;
	mapped_physical = address;
	mapped_size = size;
	*mapped = window;
	return 0;
}

int
kern_irq_register(int irq, kern_irq_handler_t handler, void *argument)
{
	registered_irq = irq;
	registered_handler = handler;
	registered_argument = argument;
	return 0;
}

void kern_irq_unmask(int irq) { unmasked_irq = irq; }
void kern_irq_send_eoi(kern_irq_ack_t acknowledge) { (void)acknowledge; eoi_count++; }
void tty_console_input_byte(uint8_t byte) { typed[typed_count++ % RX_MAX] = byte; }
void hal_putc(int character) { (void)character; }
static const struct kern_text_ops *published_ops;
void kern_text_register(const struct kern_text_ops *ops) { published_ops = ops; }

static void
record(uint8_t byte)
{
	received[received_count++ % RX_MAX] = byte;
}

static void
queue(const char *text)
{
	while (*text != '\0')
		rx[rx_tail++ % RX_MAX] = (uint8_t)*text++;
}

static void
reset_fake(unsigned new_shift)
{
	memset(window, 0, sizeof(window));
	shift = new_shift;
	rx_head = rx_tail = 0;
	ier = 0;
	iir = 0x01U;
	usr_reads = 0;
	registered_irq = -1;
	unmasked_irq = -1;
	eoi_count = 0;
	received_count = 0;
	typed_count = 0;
}

static void
test_virt(const char *path)
{
	struct drv_ns16550 port;
	struct drv_fdt fdt;
	uint8_t *blob;
	size_t length;
	int error;

	reset_fake(0);
	published_ops = NULL;
	drv_serial_console_init();
	check(published_ops != NULL && published_ops == drv_serial_console_ops(),
	      "virt: the published table is the one drv_serial_console_ops reports");
	check(drv_serial_console_ops()->putc != NULL && drv_serial_console_ops()->reveal_fatal == NULL,
	      "virt: the teletype has putc and no fatal reveal of its own");
	blob = read_file(path, &length);
	check(drv_fdt_open(&fdt, blob, length) == 0, "virt: tree opens");
	error = drv_ns16550_probe(&fdt, &port);
	check(error == 0, "virt: ns16550a found");
	check(mapped_physical == 0x10000000ULL && mapped_size == 0x100U, "virt: registers mapped at 0x10000000, 0x100 bytes");
	check(port.reg_shift == 0 && port.io_width == 1, "virt: byte registers one byte apart");
	check(port.irq == 10, "virt: interrupt 10");
	check(!port.busy_detect, "virt: no busy detect");

	queue("stale");
	error = drv_ns16550_start_input(&port, record);
	check(error == 0, "virt: input starts");
	check(registered_irq == 10 && unmasked_irq == 10, "virt: handler registered and unmasked");
	check(received_count == 0 && rx_head == rx_tail, "virt: bytes before the start dropped");
	check((ier & 0x01U) != 0, "virt: received-data interrupt enabled");
	check(width_seen == 1, "virt: byte-wide accesses");
	check(drv_ns16550_start_input(&port, record) != 0, "virt: second start refused");

	queue("ab");
	iir = 0x04U;
	registered_handler(10, 1, registered_argument);
	check(received_count == 2 && received[0] == 'a' && received[1] == 'b', "virt: interrupt delivers ab");
	check(eoi_count == 1, "virt: interrupt ended once");
	check(usr_reads == 0, "virt: status register untouched on a plain 16550");

	free(blob);
}

static void
test_dw(const char *path)
{
	struct drv_ns16550 port;
	struct drv_fdt fdt;
	uint8_t *blob;
	size_t length;
	unsigned index;
	int error;

	reset_fake(2);
	blob = read_file(path, &length);
	check(drv_fdt_open(&fdt, blob, length) == 0, "dw: tree opens");
	error = drv_ns16550_probe(&fdt, &port);
	check(error == 0, "dw: DesignWare UART found");
	check(mapped_physical == 0x10000000ULL, "dw: disabled UART skipped, enabled one at 0x10000000");
	check(port.reg_shift == 2 && port.io_width == 4, "dw: word registers four bytes apart");
	check(port.irq == 32 && port.busy_detect, "dw: interrupt 32 with busy detect");

	error = drv_ns16550_start_input(&port, drv_serial_console_receive);
	check(error == 0, "dw: input starts");
	check(width_seen == 4, "dw: word-wide accesses");

	iir = 0x07U;
	queue("x\r\x7f");
	registered_handler(32, 1, registered_argument);
	check(usr_reads == 1, "dw: busy detect cleared through the status register");
	check(typed_count == 3 && typed[0] == 'x' && typed[1] == '\n' && typed[2] == 8U, "dw: Enter and delete reach the console as newline and backspace");

	for (index = 0; index < 100; index++)
		rx[rx_tail++ % RX_MAX] = 'z';
	typed_count = 0;
	registered_handler(32, 1, registered_argument);
	check(typed_count == 64, "dw: one interrupt takes at most 64 bytes");
	registered_handler(32, 1, registered_argument);
	check(typed_count == 100, "dw: the next interrupt takes the rest");

	free(blob);
}

int
main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: %s virt.dtb dw-uart.dtb\n", argv[0]);
		return 2;
	}
	test_virt(argv[1]);
	test_dw(argv[2]);
	if (failures != 0) {
		printf("%d FAILED\n", failures);
		return 1;
	}
	printf("all PASS\n");
	return 0;
}
