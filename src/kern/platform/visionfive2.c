/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The binding of the StarFive VisionFive 2 board.
 *
 * The console is a teletype whose output the hardware layer writes to the
 * firmware's console; the board's UART does not type into it yet.  The
 * board publishes one boot device, the initial ramdisk the loader read,
 * when there is one.  It holds a disk image laid out like a Raspberry Pi
 * 4's SD card, a FAT boot partition first and the UFS root beside it, so
 * the kernel's root search finds the root as it does there.  Halting,
 * rebooting and powering off go through the hardware layer.
 *
 * The screen the firmware left on, when the loader handed one over, is
 * the hardware layer's: it draws the console there.  The binding only
 * reads the screen's named values and checks them, for the graphics
 * driver that will take the screen over.
 */

#include <stddef.h>
#include <stdint.h>

#include <hal/hal.h>
#include <drivers/disklabel/disklabel.h>
#include <drivers/generic/ramdisk.h>
#include <drivers/generic/serial-console.h>
#include <kern/boot.h>
#include <kern/disk.h>
#include <kern/klog.h>
#include <kern/partition.h>
#include <kern/platform.h>
#include <kern/pmem.h>

/* The block size the ramdisk is cut into; a disk image is laid out in it. */
#define VF2_BLOCK_SIZE		512U

/* The BIOS-style id the boot device has, the one the handoff names. */
#define VF2_BOOT_ID		0x80U

/* The bytes of one pixel of the screen the hardware layer names. */
#define VF2_PIXEL_BYTES		4U

/*
 * The ramdisk's disk, published by kern_platform_init() and kept for the
 * life of the system; NULL when the loader read no ramdisk.
 */
static struct disk *ramdisk;

static int publish_ramdisk(void);
static void describe_framebuffer(void);

/*
 * Publishes the boot devices of the VisionFive 2: the ramdisk, if any.
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

	/* Reads and checks the screen the firmware left on, for the boot log. */
	describe_framebuffer();

	/* Selects the MBR partition scheme the ramdisk's image uses. */
	partition_set_scheme(&drv_partition_scheme_mbr);
	disk_registry_reset();

	/* Publishes the ramdisk; without one the board has no boot device. */
	error = publish_ramdisk();
	if (error != 0)
		return 0;

	/* Describes the ramdisk as the boot device the handoff names. */
	device = &devices[0];
	device->device_class = KERN_DEV_SD;
	device->display_index = 0;
	device->bios_id = VF2_BOOT_ID;
	device->flags = KERN_DEV_PRESENT;
	if (handoff->boot_bios_id == VF2_BOOT_ID)
		device->flags |= KERN_DEV_BOOT_ORIGIN;
	device->sector_size = VF2_BLOCK_SIZE;
	device->cylinders = 0;
	device->heads = 0;
	device->sectors = 0;
	device->controller_location = 0;
	for (index = 0; index < sizeof(device->reserved); index++)
		device->reserved[index] = 0;

	/* Succeeded: the single published device. */
	return 1;
}

/*
 * Finishes device discovery once interrupts are enabled; the board has no
 * device that waits for them yet.
 */
void
kern_platform_refresh_devices(
	const struct kern_boot_device *devices,
	size_t count)
{
	(void)devices;
	(void)count;
}

/*
 * Starts the console's input; the board's UART does not type into it yet.
 */
int
kern_platform_input_init(
	void)
{
	/* Succeeded: the console is output only. */
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

	/* Succeeded: the ramdisk, or none when the loader read none. */
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
 * Turns the power off through the firmware, halting if that returns.
 */
int
kern_platform_poweroff(
	void)
{
	/* The hardware layer's power-off does not return when the firmware can turn the power off. */
	hal_poweroff();
	for (;;)
		hal_halt();
}

/*
 * Reboots the machine through the firmware.
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
 * Publishes the initial ramdisk the loader read as a disk.
 *
 * Returns 0, ENOENT when the loader read none, or another errno.
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
	initrd_range = hal_get_arch_handoff("riscv64.initrd");
	if (initrd_range == NULL)
		return ENOENT;
	physical = initrd_range[0];
	size = initrd_range[1];

	/* Uses whole blocks only; a tail shorter than a block holds no data of the image. */
	size -= size % VF2_BLOCK_SIZE;
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
		kern_logf("visionfive2: ramdisk unavailable (%d)\n", error);
		return error;
	}

	/* Succeeded: the ramdisk is a disk. */
	kern_logf("visionfive2: ramdisk ram0 %llu KiB at %llx\n",
		  (unsigned long long)(size / 1024U),
		  (unsigned long long)physical);
	return 0;
}

/*
 * Reads the screen the hardware layer names under "riscv64.framebuffer"
 * and writes it to the boot log, refusing values that do not describe a
 * screen: a format other than the two four-byte ones, rows shorter than
 * the width, or memory too small for every row.
 */
static void
describe_framebuffer(
	void)
{
	struct kern_boot_framebuffer framebuffer;
	uint64_t needed;
	int error;

	/* Reads the named values; a boot without the loader's screen has none. */
	error = kern_boot_framebuffer("riscv64.framebuffer", &framebuffer);
	if (error != 0) {
		kern_logf("visionfive2: no framebuffer (%d)\n", error);
		return;
	}

	/* Refuses a pixel format the hardware layer does not name. */
	if (framebuffer.format != KERN_BOOT_FRAMEBUFFER_RGBX8888 &&
	    framebuffer.format != KERN_BOOT_FRAMEBUFFER_BGRX8888) {
		kern_logf("visionfive2: framebuffer of unknown format %u\n", framebuffer.format);
		return;
	}

	/* Refuses rows shorter than the width, the pitch being in bytes. */
	if ((uint64_t)framebuffer.pitch < (uint64_t)framebuffer.width * VF2_PIXEL_BYTES) {
		kern_logf("visionfive2: framebuffer pitch %u shorter than its width %u\n",
			  framebuffer.pitch,
			  framebuffer.width);
		return;
	}

	/* Refuses memory too small for every row. */
	needed = (uint64_t)framebuffer.pitch * framebuffer.height;
	if (framebuffer.size < needed) {
		kern_logf("visionfive2: framebuffer of %llu bytes for %llu\n",
			  (unsigned long long)framebuffer.size,
			  (unsigned long long)needed);
		return;
	}

	/* Succeeded: the screen as the hardware layer named it. */
	kern_logf("visionfive2: framebuffer %ux%u pitch %u format %u at %llx size %llx\n",
		  framebuffer.width,
		  framebuffer.height,
		  framebuffer.pitch,
		  framebuffer.format,
		  (unsigned long long)framebuffer.physical_base,
		  (unsigned long long)framebuffer.size);
}
