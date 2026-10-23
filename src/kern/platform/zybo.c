/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The binding of the Digilent ZYBO (Zynq-7010), and of the emulator's
 * xilinx-zynq-a9 machine, which runs the same kernel.
 *
 * The console is a teletype on the PS's UART1, a Cadence UART: the
 * hardware layer writes its output to the UART, and the UART's receive
 * interrupt types what arrives into it.  The board's loader (the JTAG
 * boot, or the emulator's) passes a device tree that describes no
 * device, so the UART is found at the address and interrupt the SoC
 * fixes.
 *
 * The board publishes one boot device, the initial ramdisk the loader
 * placed, when there is one: a disk image with an MBR, so the kernel's
 * root search finds the root on it as on a Raspberry Pi 4's SD card.
 * Halting and rebooting go through the hardware layer (the SLCR's system
 * reset); the board is powered from USB and has no power-off.  Once
 * interrupts are on, the programmable logic is published as /dev/fpga0.
 *
 * Values referenced: Linux v6.12 arch/arm/boot/dts/xilinx/zynq-7000.dtsi,
 * uart1's reg (0xe0001000) and its shared peripheral interrupt 50, GIC
 * interrupt 82.
 */

#include <stddef.h>
#include <stdint.h>

#include <hal/hal.h>
#include <drivers/disklabel/disklabel.h>
#include <drivers/generic/ramdisk.h>
#include <drivers/generic/serial-console.h>
#include <drivers/platform/zybo/zynq-devcfg.h>
#include <drivers/serial/cdns-uart.h>
#include <kern/boot.h>
#include <kern/disk.h>
#include <kern/klog.h>
#include <kern/partition.h>
#include <kern/platform.h>
#include <kern/pmem.h>
#include <uapi/errno.h>

/* The console's UART, UART1 of the PS: its registers and its GIC interrupt. */
#define ZYBO_UART1_PHYS			0xe0001000U
#define ZYBO_UART1_IRQ			82

/* The block size the ramdisk is cut into; a disk image is laid out in it. */
#define ZYBO_BLOCK_SIZE			512U

/* The BIOS-style id the boot device has, the one the handoff names. */
#define ZYBO_BOOT_ID			0x80U

/* Marks a parameter that the kernel's platform interface passes but this board does not need. */
#define UNUSED_PARAMETER(parameter)	((void)(parameter))

/*
 * The UART the console reads, filled by kern_platform_init() before any
 * interrupt is on and kept for the life of the system, since its interrupt
 * handler reads it.
 */
static struct drv_cdns_uart uart;

/*
 * Nonzero when the UART was mapped and uart was filled; without it the
 * console is output only.  Set with uart.
 */
static int uart_present;

/*
 * The ramdisk's disk, published by kern_platform_init() and kept for the
 * life of the system; NULL when the loader placed no ramdisk.
 */
static struct disk *ramdisk;

/*
 * Nonzero once kern_platform_refresh_devices() has tried to publish the
 * programmable logic, so that it is tried only once.
 */
static int fpga_attached;

static void attach_uart(void);
static int publish_ramdisk(void);

/*
 * Publishes the boot devices of the ZYBO: the ramdisk, if any.
 *
 * Returns the number of devices published.
 */
size_t
kern_platform_init(
	const struct kern_boot_handoff *handoff,
	struct kern_boot_device *devices,
	size_t capacity)
{
	struct kern_boot_device *device;
	size_t index;
	int error;

	/* Rejects a handoff that is not the common record. */
	if (handoff == NULL || devices == NULL || capacity == 0)
		return 0;
	if (handoff->magic != KERN_HANDOFF_MAGIC)
		return 0;
	if (handoff->version != KERN_HANDOFF_VERSION_MULTIBOOT)
		return 0;
	if (handoff->size < sizeof(*handoff))
		return 0;

	/* Publishes the teletype as /dev/console's output. */
	drv_serial_console_init();

	/* Maps the UART the console will read once input starts. */
	attach_uart();

	/* Reads the ramdisk's partitions as an MBR disk's. */
	partition_set_scheme(&drv_partition_scheme_mbr);

	/* Starts the disk registry empty, before any disk is published. */
	disk_registry_reset();

	/* Publishes the ramdisk; without one the board has no boot device. */
	error = publish_ramdisk();
	if (error != 0)
		return 0;

	/* Describes the ramdisk as the boot device the handoff names. */
	device = &devices[0];
	device->device_class = KERN_DEV_SD;
	device->display_index = 0;
	device->bios_id = ZYBO_BOOT_ID;
	device->flags = KERN_DEV_PRESENT;
	if (handoff->boot_bios_id == ZYBO_BOOT_ID)
		device->flags |= KERN_DEV_BOOT_ORIGIN;
	device->sector_size = ZYBO_BLOCK_SIZE;
	device->cylinders = 0;
	device->heads = 0;
	device->sectors = 0;
	device->controller_location = 0;

	/* Clears the record's reserved bytes. */
	for (index = 0; index < sizeof(device->reserved); index++)
		device->reserved[index] = 0;

	/* Succeeded: the single published device. */
	return 1;
}

/*
 * Finishes device discovery once interrupts are enabled: publishes the
 * programmable logic, once.
 */
void
kern_platform_refresh_devices(
	const struct kern_boot_device *devices,
	size_t count)
{
	int error;

	UNUSED_PARAMETER(devices);
	UNUSED_PARAMETER(count);

	/* The logic is published once; a later refresh finds it there. */
	if (fpga_attached)
		return;
	fpga_attached = 1;

	/* Publishes /dev/fpga0; without it the board runs without its logic. */
	error = drv_zynq_devcfg_attach();
	if (error != 0)
		kern_logf("zybo: programmable logic unavailable (%d)\n", error);
}

/*
 * Starts typing what the UART receives into the console.
 */
int
kern_platform_input_init(
	void)
{
	int error;

	/* A UART that could not be mapped gives no input. */
	if (!uart_present)
		return 0;

	/* Has the UART's receive interrupt feed the console. */
	error = drv_cdns_uart_start_input(&uart, drv_serial_console_receive);
	if (error != 0) {
		kern_logf("zybo: serial input unavailable (%d)\n", error);
		return error;
	}

	/* Succeeded: the serial line types into the console. */
	return 0;
}

/*
 * Resolves the published boot device to the ramdisk.
 */
struct disk *
kern_platform_block_device(
	const struct kern_boot_device *device)
{
	/* Only the ramdisk's device has a disk. */
	if (device == NULL || device->device_class != KERN_DEV_SD)
		return NULL;

	/* Succeeded: the ramdisk, or none when the loader placed none. */
	return ramdisk;
}

/*
 * Writes a debug string to the early console.
 */
void
kern_platform_debug_write(
	const char *text)
{
	/* Ignores a missing string. */
	if (text == NULL)
		return;

	/* Emits every byte through the hardware layer. */
	while (*text != '\0') {
		hal_putc((unsigned char)*text);
		text++;
	}
}

/*
 * Halts the machine permanently.
 */
void
kern_platform_halt(
	void)
{
	/* Halts with interrupts disabled. */
	(void)hal_irq_disable();
	for (;;)
		hal_halt();
}

/*
 * Reports that the ZYBO has no power-off; the caller halts.
 */
int
kern_platform_poweroff(
	void)
{
	/* The board is powered from USB, with nothing the PS can switch off. */
	return EOPNOTSUPP;
}

/*
 * Reboots the machine through the SLCR's system reset.
 */
void
kern_platform_reboot(
	void)
{
	/* Halts in case the reset request returns. */
	hal_reset();
	for (;;)
		hal_halt();
}

/*
 * Maps the UART at the address the SoC fixes, for the console's input.
 *
 * A UART that cannot be mapped leaves the console output only; the boot
 * goes on.
 */
static void
attach_uart(
	void)
{
	int error;

	/* Maps UART1, whose output the hardware layer writes. */
	error = drv_cdns_uart_attach(&uart, ZYBO_UART1_PHYS, ZYBO_UART1_IRQ);
	if (error != 0) {
		kern_logf("zybo: serial port unavailable (%d)\n", error);
		return;
	}

	/* Keeps the UART for the input start and reports where it is. */
	uart_present = 1;
	kern_logf("zybo: serial port at %llx, interrupt %d\n",
		  (unsigned long long)uart.physical,
		  uart.irq);
}

/*
 * Publishes the initial ramdisk the loader placed as a disk.
 *
 * Returns 0, ENOENT when the loader placed none, or another errno.
 */
static int
publish_ramdisk(
	void)
{
	const uint64_t *initrd_range;
	uint64_t physical;
	uint64_t size;
	uint8_t *first;
	uint8_t *last;
	int error;

	/* Finds the ramdisk's physical range; none means no boot device. */
	initrd_range = hal_get_arch_handoff("armv7.initrd");
	if (initrd_range == NULL)
		return ENOENT;
	physical = initrd_range[0];
	size = initrd_range[1];

	/* Uses whole blocks only; a tail shorter than a block holds no data of the image. */
	size -= size % ZYBO_BLOCK_SIZE;
	if (size == 0)
		return ENOENT;

	/*
	 * Finds the ramdisk in the direct map.  RAM is mapped in one piece,
	 * so the range is usable when both its ends are in managed RAM and
	 * their aliases are as far apart as the addresses.
	 */
	first = kern_pmem_to_kernel((hal_physaddr_t)physical);
	if (first == NULL)
		return EFAULT;
	last = kern_pmem_to_kernel((hal_physaddr_t)(physical + size - 1));
	if (last == NULL)
		return EFAULT;
	if ((uint64_t)(last - first) != size - 1)
		return EFAULT;

	/* Publishes the memory as the ramdisk's disk. */
	error = drv_ramdisk_create("ram0", first, size, &ramdisk);
	if (error != 0) {
		kern_logf("zybo: ramdisk unavailable (%d)\n", error);
		return error;
	}

	/* Reports the ramdisk's size and place. */
	kern_logf("zybo: ramdisk ram0 %llu KiB at %llx\n",
		  (unsigned long long)(size / 1024U),
		  (unsigned long long)physical);

	/* Succeeded: the ramdisk is a disk. */
	return 0;
}
