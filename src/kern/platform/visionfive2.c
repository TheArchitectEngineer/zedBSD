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
 * board publishes one boot device: the initial ramdisk the loader read,
 * when there is one, and otherwise the SD card.  The ramdisk holds a disk
 * image laid out like a Raspberry Pi 4's SD card, a FAT boot partition
 * first and the UFS root beside it, so the kernel's root search finds the
 * root as it does there.  An SD card booted without a ramdisk carries its
 * root as a UFS partition of its own, which the boot parameters name.
 * Halting, rebooting and powering off go through the hardware layer.
 *
 * The screen the firmware left on, when the loader handed one over, is
 * the hardware layer's: it draws the console there.  The binding only
 * reads the screen's named values and checks them, for the graphics
 * driver that will take the screen over.
 *
 * The SoC's system control blocks, clock and reset generators and pin
 * controllers are brought up from the device tree before any device, so
 * the devices' drivers can switch their own clocks, resets and pins.  The
 * boot log shows the PLLs' and the SD card clock's rates and the card
 * detect pin, for comparing with what the firmware set.
 *
 * The SD card slot's card is published as the disk mmcblk0 beside the
 * ramdisk, and is the boot device when there is no ramdisk.  Once
 * interrupts are on, the boot log lists the card's partitions.
 *
 * The first Ethernet port is published as the network interface en0.
 *
 * The GPU core is brought up as far as reading its identity, from the
 * board's own description of it unless the device tree enables a node.
 */

#include <stddef.h>
#include <stdint.h>

#include <hal/hal.h>
#include <drivers/disklabel/disklabel.h>
#include <drivers/generic/fdt.h>
#include <drivers/generic/ramdisk.h>
#include <drivers/generic/serial-console.h>
#include <drivers/platform/visionfive2/vf2-crg.h>
#include <drivers/platform/visionfive2/vf2-ethernet.h>
#if CONFIG_DRIVER_POWERVR
#include <drivers/platform/visionfive2/vf2-gpu.h>
#endif
#include <drivers/platform/visionfive2/vf2-mmc.h>
#include <drivers/platform/visionfive2/vf2-pcie.h>
#include <drivers/platform/visionfive2/vf2-pinctrl.h>
#include <drivers/platform/visionfive2/vf2-pwmdac.h>
#ifdef CONFIG_VF2_DMA_TEST
#include <drivers/platform/visionfive2/vf2-dmatest.h>
#endif
#include <drivers/platform/visionfive2/vf2-syscon.h>
#include <kern/boot.h>
#include <kern/disk.h>
#include <kern/klog.h>
#include <kern/partition.h>
#include <kern/platform.h>
#include <kern/pmem.h>
#include <uapi/errno.h>

/* The block size the ramdisk is cut into; a disk image is laid out in it. */
#define VF2_BLOCK_SIZE		512U

/* The BIOS-style id the boot device has, the one the handoff names. */
#define VF2_BOOT_ID		0x80U

/* The bytes of one pixel of the screen the hardware layer names. */
#define VF2_PIXEL_BYTES		4U

/* Marks a parameter that the kernel's platform interface passes but this board does not need. */
#define UNUSED_PARAMETER(parameter)	((void)(parameter))

/*
 * Whether the board's devices snoop the CPU caches when the device tree
 * does not say.
 *
 * The JH7110's device tree marks no device non-coherent, and its L2 cache
 * controller offers no maintenance for device memory, which points to
 * coherent DMA; the board's DMA glue passes this to drv_fdt_dma_coherent()
 * as the default.
 */
#define VF2_DMA_DEFAULT_COHERENT	1

/* The SD card slot's controller, whose clock and card detect are logged. */
#define VF2_SD_REGISTERS	0x16020000ULL

/* The AXI configuration bus clock's number in the SYS clock generator. */
#define VF2_AXI_CONFIG_CLOCK	7U

/* The SD card clock's number in the SYS clock generator. */
#define VF2_SD_CARD_CLOCK	94U

#if CONFIG_DRIVER_POWERVR
/*
 * Where the GPU core is: the board's device tree usually has no node for
 * it, so the board describes it itself.
 *
 * Values referenced:
 * StarFive JH7110 TRM Preliminary V2, Table 2-4 System Memory Map (the
 * GPU at 0x1800_0000, 1 MB) and Table 2-3 System Interrupt Connections
 * (gpu_os_irq is global interrupt 77, which the PLIC numbers 82, five
 * above, as for the board's other devices); StarFive Linux
 * JH7110_VisionFive2_6.12.y_devel 4cecf169f38e
 * arch/riscv/boot/dts/starfive/jh7110.dtsi (gpu@18000000, interrupts 82).
 */
static const struct drv_vf2_gpu_place vf2_gpu_builtin = {
	0x18000000ULL,
	0x100000ULL,
	82U
};
#endif

/*
 * The ramdisk's disk, published by kern_platform_init() and kept for the
 * life of the system; NULL when the loader read no ramdisk.
 */
static struct disk *ramdisk;

/*
 * The device tree, kept once probe_soc() has brought the SoC's blocks up
 * from it.  The tree's blob is the hardware layer's and lives as long as
 * the system.
 */
static struct drv_fdt soc_tree;

/* Nonzero once probe_soc() has filled soc_tree; never cleared. */
static int soc_ready;

/*
 * The SD card's disk, published by kern_platform_init() and kept for the
 * life of the system; NULL when the slot is empty or the card failed.
 */
static struct disk *sd_card;

/*
 * The SD card's partitions as the boot log lists them.  Used only by
 * describe_sd_partitions(), once, during the boot; it is file-scope to
 * keep the table off the stack.
 */
static struct partition sd_partitions[PARTITION_MAX];

static int publish_ramdisk(void);
static void describe_framebuffer(void);
static void probe_soc(void);
static void describe_clocks(void);
static void describe_card_detect(const struct drv_fdt *fdt);
static void attach_sd_card(void);
static void attach_ethernet(void);
static void describe_sd_partitions(void);

/*
 * Publishes the boot device of the VisionFive 2: the ramdisk, or the SD
 * card when the loader read no ramdisk.
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

	/* Brings up the SoC's clocks, resets and pins for the devices. */
	probe_soc();

	/*
	 * Selects the partition scheme that tells GPT from MBR: the ramdisk's
	 * image is an MBR disk, the SD card's UEFI image a GPT disk.
	 */
	partition_set_scheme(&drv_partition_scheme_pcat_auto);

	/* Starts the disk registry empty, before any disk is published. */
	disk_registry_reset();

	/* Publishes the ramdisk, the boot device. */
	error = publish_ramdisk();

	/* Publishes the SD card beside it. */
	attach_sd_card();

	/* Publishes the Ethernet port. */
	attach_ethernet();

	/* Without the ramdisk the SD card is the boot device, if there is one. */
	if (error != 0 && sd_card == NULL)
		return 0;

	/* Says which disk the boot device is when there is no ramdisk. */
	if (error != 0)
		kern_logf("visionfive2: no ramdisk; the boot device is %s\n", sd_card->d_name);

	/* Describes the boot device the handoff names. */
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

	/* Clears the record's reserved bytes. */
	for (index = 0; index < sizeof(device->reserved); index++)
		device->reserved[index] = 0;

	/* Succeeded: the single published device. */
	return 1;
}

/*
 * Finishes device discovery once interrupts are enabled: lists the SD
 * card's partitions and looks behind the PCIe root ports.
 */
void
kern_platform_refresh_devices(
	const struct kern_boot_device *devices,
	size_t count)
{
	UNUSED_PARAMETER(devices);
	UNUSED_PARAMETER(count);

	/* Lists the SD card's partitions. */
	describe_sd_partitions();

	/* Looks behind the USB root ports and publishes the NVMe namespaces. */
	drv_vf2_pcie_refresh();
}

/*
 * Starts the console's input; the board's UART does not type into it yet.
 */
int
kern_platform_input_init(
	void)
{
	/* Lets USB keyboards type into the console. */
	drv_vf2_pcie_input_ready();

	/* Succeeded: the UART does not type yet; USB keyboards do. */
	return 0;
}

/*
 * Resolves the published boot device to the ramdisk, or to the SD card
 * when there is no ramdisk.
 */
struct disk *
kern_platform_block_device(
	const struct kern_boot_device *device)
{
	/* Only the boot device has a disk. */
	if (device == NULL || device->device_class != KERN_DEV_SD)
		return NULL;

	/* The ramdisk, when the loader read one. */
	if (ramdisk != NULL)
		return ramdisk;

	/* Succeeded: the SD card, or none when the slot is empty. */
	return sd_card;
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

	/* Reports the ramdisk's size and place. */
	kern_logf("visionfive2: ramdisk ram0 %llu KiB at %llx\n",
		  (unsigned long long)(size / 1024U),
		  (unsigned long long)physical);

	/* Succeeded: the ramdisk is a disk. */
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

/*
 * Finds the system control blocks, clock and reset generators and pin
 * controllers in the device tree, and logs the rates and the card detect
 * pin.
 *
 * A tree that cannot be read, or one without the blocks, leaves the board
 * without its devices' clocks; the boot goes on with the ramdisk.
 */
static void
probe_soc(
	void)
{
	const uint64_t *fdt_range;
	struct drv_fdt fdt;
	const void *blob;
	int error;

	/* Finds the device tree's range the hardware layer kept. */
	fdt_range = hal_get_arch_handoff("riscv64.fdt");
	if (fdt_range == NULL) {
		kern_logf("visionfive2: no device tree\n");
		return;
	}

	/* Finds the tree's bytes in the direct map, refusing a tree outside RAM. */
	blob = kern_pmem_to_kernel((hal_physaddr_t)fdt_range[0]);
	if (blob == NULL) {
		kern_logf("visionfive2: device tree outside RAM\n");
		return;
	}

	/* Refuses a tree whose header does not check out. */
	error = drv_fdt_open(&fdt, blob, (size_t)fdt_range[1]);
	if (error != 0) {
		kern_logf("visionfive2: unreadable device tree (%d)\n", error);
		return;
	}

	/* Maps the system control blocks, which hold the PLL settings. */
	error = drv_vf2_syscon_init(&fdt);
	if (error != 0) {
		kern_logf("visionfive2: system control blocks unavailable (%d)\n", error);
		return;
	}

	/* Maps the clock and reset generators. */
	error = drv_vf2_crg_init(&fdt);
	if (error != 0) {
		kern_logf("visionfive2: clock generators unavailable (%d)\n", error);
		return;
	}

	/* Maps the pin controllers. */
	error = drv_vf2_pinctrl_init(&fdt);
	if (error != 0) {
		kern_logf("visionfive2: pin controllers unavailable (%d)\n", error);
		return;
	}

	/* Logs what the firmware left the clocks and the card slot at. */
	describe_clocks();
	describe_card_detect(&fdt);

	/* Keeps the tree for the devices brought up after the disk registry is reset. */
	soc_tree = fdt;
	soc_ready = 1;

	/* Starts the PCIe root complexes and the USB and NVMe drivers behind them. */
	error = drv_vf2_pcie_init(&fdt, VF2_DMA_DEFAULT_COHERENT);
	if (error != 0 && error != ENODEV)
		kern_logf("visionfive2: PCIe unavailable (%d)\n", error);

	/* Publishes the headphone jack; the board boots without sound when it fails. */
	error = drv_vf2_pwmdac_attach(&fdt, VF2_DMA_DEFAULT_COHERENT);
	if (error != 0 && error != ENODEV)
		kern_logf("visionfive2: sound unavailable (%d)\n", error);
#if CONFIG_DRIVER_POWERVR

	/* Brings the GPU core up as far as its identity; the board boots without it when it fails. */
	error = drv_vf2_gpu_attach(&fdt, &vf2_gpu_builtin);
	if (error != 0)
		kern_logf("visionfive2: GPU unavailable (%d)\n", error);
#endif
#ifdef CONFIG_VF2_DMA_TEST

	/* The test kernel's DMA coherency check. */
	error = drv_vf2_dmatest_publish(&soc_tree);
	if (error != 0)
		kern_logf("visionfive2: DMA test unavailable (%d)\n", error);
#endif
}

/*
 * Writes the PLLs' rates and the SD card clock's rate to the boot log.
 *
 * The rates are computed from the registers, for comparing with the
 * values the firmware's memory dump shows.  A rate that cannot be
 * computed is logged as its error.
 */
static void
describe_clocks(
	void)
{
	struct drv_vf2_clock clock;
	uint64_t pll_rates[3];
	uint64_t bus_rate;
	uint64_t card_rate;
	int pll_errors[3];
	int bus_error;
	int card_error;
	unsigned pll;

	/* Computes the three PLLs' rates. */
	for (pll = 0; pll < 3U; pll++) {
		pll_errors[pll] = drv_vf2_pll_get_rate(pll, &pll_rates[pll]);
		if (pll_errors[pll] != 0)
			pll_rates[pll] = 0;
	}

	/* Logs them, each with its error, zero when computed. */
	kern_logf("visionfive2: pll0 %llu Hz (%d), pll1 %llu Hz (%d), pll2 %llu Hz (%d)\n",
		  (unsigned long long)pll_rates[0],
		  pll_errors[0],
		  (unsigned long long)pll_rates[1],
		  pll_errors[1],
		  (unsigned long long)pll_rates[2],
		  pll_errors[2]);

	/* Computes the AXI configuration bus's rate, which the card clock divides. */
	clock.crg = DRV_VF2_CRG_SYS;
	clock.index = VF2_AXI_CONFIG_CLOCK;
	bus_error = drv_vf2_clock_get_rate(&clock, &bus_rate);
	if (bus_error != 0)
		bus_rate = 0;

	/* Computes the SD card clock's rate. */
	clock.index = VF2_SD_CARD_CLOCK;
	card_error = drv_vf2_clock_get_rate(&clock, &card_rate);
	if (card_error != 0)
		card_rate = 0;

	/* Logs both, each with its error, zero when computed. */
	kern_logf("visionfive2: axi_cfg0 %llu Hz (%d), sdcard %llu Hz (%d)\n",
		  (unsigned long long)bus_rate,
		  bus_error,
		  (unsigned long long)card_rate,
		  card_error);
}

/*
 * Writes whether a card is in the SD slot to the boot log.
 *
 * The slot's card detect pin is made an input and read once.
 */
static void
describe_card_detect(
	const struct drv_fdt *fdt)
{
	struct drv_vf2_gpio card_detect;
	uint64_t address;
	uint64_t size;
	uint32_t node;
	int present;
	int error;

	/* Finds the SD controller the slot is wired to among the controllers. */
	node = DRV_FDT_NO_NODE;
	for (;;) {
		/* Moves to the next controller; none left means no slot. */
		error = drv_fdt_find_compatible(fdt, "starfive,jh7110-mmc", node, &node);
		if (error != 0) {
			kern_logf("visionfive2: no SD card slot (%d)\n", error);
			return;
		}

		/* Stops at the controller whose registers are the slot's. */
		error = drv_fdt_reg(fdt, node, 0, &address, &size);
		if (error == 0 && address == VF2_SD_REGISTERS)
			break;
	}

	/* Finds the card detect pin. */
	error = drv_vf2_gpio_lookup(fdt, node, "cd-gpios", &card_detect);
	if (error != 0) {
		kern_logf("visionfive2: no card detect pin (%d)\n", error);
		return;
	}

	/* Makes the pin an input. */
	error = drv_vf2_gpio_configure_input(&card_detect);
	if (error != 0) {
		kern_logf("visionfive2: card detect pin unusable (%d)\n", error);
		return;
	}

	/* Reads whether a card is in. */
	error = drv_vf2_gpio_read(&card_detect, &present);
	if (error != 0) {
		kern_logf("visionfive2: card detect unreadable (%d)\n", error);
		return;
	}

	/* Succeeded: logs the slot's state. */
	if (present != 0) {
		kern_logf("visionfive2: SD card present (gpio %u)\n", card_detect.pin);
	} else {
		kern_logf("visionfive2: SD card absent (gpio %u)\n", card_detect.pin);
	}
}

/*
 * Brings up the SD card slot and publishes its card as mmcblk0.
 *
 * A board whose SoC blocks did not come up, an empty slot or a card that
 * fails leaves the board without the disk; the boot goes on.
 */
static void
attach_sd_card(
	void)
{
	int error;

	/* Needs the clocks, resets and pins the slot's glue switches. */
	if (!soc_ready) {
		kern_logf("visionfive2: SD card slot not brought up: SoC blocks unavailable\n");
		return;
	}

	/* Brings the slot and its card up. */
	error = drv_vf2_mmc_attach(&soc_tree, &sd_card);
	if (error != 0) {
		sd_card = NULL;
		kern_logf("visionfive2: no SD card disk (%d)\n", error);
		return;
	}

	/* Succeeded: the card is a disk. */
	kern_logf("visionfive2: SD card is %s\n", sd_card->d_name);
}

/*
 * Brings up the first Ethernet port and publishes it as en0.
 *
 * A board whose SoC blocks did not come up, or a port that fails, leaves
 * the board without the interface; the boot goes on.
 */
static void
attach_ethernet(
	void)
{
	int error;

	/* Needs the clocks, resets and pins the port's glue switches. */
	if (!soc_ready) {
		kern_logf("visionfive2: Ethernet not brought up: SoC blocks unavailable\n");
		return;
	}

	/* Brings the port and its PHY up. */
	error = drv_vf2_ethernet_attach(&soc_tree, VF2_DMA_DEFAULT_COHERENT);
	if (error != 0) {
		kern_logf("visionfive2: no Ethernet (%d)\n", error);
		return;
	}

	/* Succeeded: the port is a network interface. */
	kern_logf("visionfive2: Ethernet is en0\n");
}

/*
 * Writes the SD card's partitions to the boot log: each one's number,
 * first block and size, its label when the table gives one, and whether
 * it is the EFI system partition.
 */
static void
describe_sd_partitions(
	void)
{
	const struct partition *entry;
	const char *label;
	const char *kind;
	int count;
	int index;

	/* A board without the card has nothing to list. */
	if (sd_card == NULL)
		return;

	/* Reads the card's partition table. */
	count = partition_scan(sd_card, sd_partitions, PARTITION_MAX);
	if (count < 0) {
		kern_logf("visionfive2: %s partition table unreadable (%d)\n", sd_card->d_name, count);
		return;
	}

	/* Writes how many partitions there are. */
	kern_logf("visionfive2: %s has %d partitions\n", sd_card->d_name, count);

	/* Writes each partition. */
	for (index = 0; index < count; index++) {
		/* Takes the partition's label, empty when the table gives none. */
		entry = &sd_partitions[index];
		label = "";
		if ((entry->p_flags & PARTITION_HAS_LABEL) != 0)
			label = entry->p_label;

		/* Marks the EFI system partition. */
		kind = "";
		if ((entry->p_flags & PARTITION_EFI_SYSTEM) != 0)
			kind = ", EFI system";

		/* Writes the partition's line. */
		kern_logf("visionfive2: %s partition %u start %llu blocks %llu (%llu MiB) \"%s\"%s\n",
			  sd_card->d_name,
			  entry->p_index + 1U,
			  (unsigned long long)entry->p_start_block,
			  (unsigned long long)entry->p_block_count,
			  (unsigned long long)(entry->p_block_count / 2048ULL),
			  label,
			  kind);
	}
}
