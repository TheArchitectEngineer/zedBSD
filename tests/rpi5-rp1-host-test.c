/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Runs the Raspberry Pi 5's RP1 module (rp1.c, rp1-gpio.c) on the host
 * against a Pi 5 device tree and a root complex kept in memory, and checks
 * what the RP1 drivers are given: their registers on the CPU's bus, the
 * DMA window, the inbound windows the module sets, and a pin's function.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <drivers/platform/rpi5/rp1.h>
#include <drivers/platform/rpi5/rp1-gpio.h>
#include <kern/device-io.h>
#include <kern/klog.h>
#include <kern/lock.h>
#include <kern/pmem.h>

/* The largest blob the test reads. */
#define BLOB_LIMIT		(2U * 1024U * 1024U)

/* The most register blocks the module maps. */
#define MAPPING_LIMIT		16U

/* The root complex's registers in the trees, and the RP1 behind it. */
#define EXPECT_PCIE_REGISTERS	0x1000120000ULL
#define FAKE_RP1_BUS		1U
#define FAKE_MSIX_BAR		0x00410000U

/* Where the drivers find their registers. */
#define EXPECT_USB0		0x1f00200000ULL
#define EXPECT_USB1		0x1f00300000ULL
#define EXPECT_USB_SIZE		0x100000ULL
#define EXPECT_GEM		0x1f00100000ULL
#define EXPECT_GPIO		0x1f000d0000ULL

/* The DMA window: the CPU's memory at 64 GiB on RP1's side. */
#define EXPECT_DMA_OFFSET	0x1000000000ULL
#define EXPECT_DMA_LIMIT	0xfffffffffULL

/* A pin and its VBUS function, and where its control and pad registers are. */
#define VBUS_PIN		42U
#define VBUS_FUNCTION		2U
#define VBUS_CONTROL		0x8044U
#define VBUS_PAD		0x8024U

/* One block the module mapped, kept in host memory. */
struct mapping {
	uint64_t address;
	size_t size;
	uint8_t *bytes;
};

static struct mapping mappings[MAPPING_LIMIT];
static unsigned mapping_count;
static const void *tree;

static int check(int condition, const char *what);
static uint8_t *find_block(uint64_t address);
static uint32_t read_le32(const uint8_t *bytes, unsigned offset);
static void write_le32(uint8_t *bytes, unsigned offset, uint32_t value);
static int check_tree(const char *path);

/* The tree is the only memory the module asks for through the direct map. */
void *
kern_pmem_to_kernel(
	hal_physaddr_t address)
{
	(void)address;

	/* Succeeded: the blob read from the file. */
	return (void *)tree;
}

/*
 * Maps a block as zeroed host memory, except the root complex's, which
 * shows a trained link and RP1 behind it.
 */
int
kern_device_map(
	uint64_t address,
	size_t size,
	unsigned attributes,
	void **mapped)
{
	uint8_t *bytes;

	(void)attributes;

	/* Refuses more blocks than the test keeps. */
	if (mapping_count == MAPPING_LIMIT)
		return 12;

	/* Allocates the block. */
	bytes = calloc(1, size);
	if (bytes == NULL)
		return 12;

	/* A root complex with the link up, bus 1 behind it, and RP1 there. */
	if (address == EXPECT_PCIE_REGISTERS) {
		write_le32(bytes, 0x4068U, 0x30U);
		write_le32(bytes, 0x18U, FAKE_RP1_BUS << 8);
		write_le32(bytes, 0x8000U, 0x00011de4U);
		write_le32(bytes, 0x8010U, FAKE_MSIX_BAR);
		write_le32(bytes, 0x8014U, 0);
	}

	/* Succeeded: the block. */
	mappings[mapping_count].address = address;
	mappings[mapping_count].size = size;
	mappings[mapping_count].bytes = bytes;
	mapping_count++;
	*mapped = bytes;
	return 0;
}

/* Unmapping keeps the memory, which later checks read. */
int
kern_device_unmap(
	void *mapped,
	size_t size)
{
	(void)mapped;
	(void)size;

	/* Succeeded. */
	return 0;
}

/* Reads a register from host memory. */
uint32_t
kern_mmio_read32(
	const volatile void *address)
{
	uint32_t value;

	/* Copies the bytes. */
	memcpy(&value, (const void *)address, sizeof(value));

	/* Succeeded: the register. */
	return value;
}

/* Writes a register to host memory. */
void
kern_mmio_write32(
	volatile void *address,
	uint32_t value)
{
	/* Copies the bytes. */
	memcpy((void *)address, &value, sizeof(value));
}

/* Prints the module's log. */
void
kern_logf(
	const char *format,
	...)
{
	va_list arguments;

	/* Prints the message with the test's prefix. */
	va_start(arguments, format);
	printf("  log: ");
	vprintf(format, arguments);
	va_end(arguments);
}

/* The lock has no other user on the host. */
void
spin_init(
	struct spinlock *lock,
	enum lock_rank rank,
	const char *name)
{
	(void)lock;
	(void)rank;
	(void)name;
}

/* Takes the lock, which nothing contends on the host. */
unsigned long
spin_lock_irqsave(
	struct spinlock *lock)
{
	(void)lock;

	/* Succeeded: interrupts were off. */
	return 0;
}

/* Releases the lock. */
void
spin_unlock_irqrestore(
	struct spinlock *lock,
	unsigned long enabled)
{
	(void)lock;
	(void)enabled;
}

/*
 * Runs the test on the device tree blob named on the command line.
 */
int
main(
	int argc,
	char **argv)
{
	int failures;

	/* Requires the blob's path. */
	if (argc != 2) {
		fprintf(stderr, "usage: rpi5-rp1-host-test bcm2712-rpi-5-b.dtb\n");
		return 2;
	}

	/* Checks the tree. */
	failures = check_tree(argv[1]);
	if (failures != 0) {
		printf("rpi5-rp1-host-test: FAIL (%s, %d)\n", argv[1], failures);
		return 1;
	}

	/* Succeeded. */
	printf("rpi5-rp1-host-test: PASS (%s)\n", argv[1]);
	return 0;
}

/* Runs the module on one tree and counts the failed checks. */
static int
check_tree(
	const char *path)
{
	static unsigned char blob[BLOB_LIMIT];
	uint8_t *pcie;
	uint8_t *gpio;
	uint8_t *pads;
	uint64_t address;
	uint64_t size;
	uint64_t offset;
	uint64_t limit;
	uint32_t usb0;
	uint32_t usb1;
	uint32_t node;
	uint32_t pad;
	int coherent;
	int failures;
	int error;
	FILE *file;

	/* Reads the blob. */
	file = fopen(path, "rb");
	if (file == NULL) {
		perror(path);
		return 1;
	}
	(void)fread(blob, 1, sizeof(blob), file);
	fclose(file);
	tree = blob;

	/* Finds RP1 as the boot does. */
	failures = 0;
	error = drv_rp1_init(0x2000000U);
	failures += check(error == 0, "RP1 is found behind the root complex");
	if (error != 0)
		return failures;

	/* The two USB controllers, in the tree's order. */
	error = drv_rp1_find_child("snps,dwc3", DRV_FDT_NO_NODE, &usb0);
	failures += check(error == 0, "the first USB controller is found");
	error = drv_rp1_find_child("snps,dwc3", usb0, &usb1);
	failures += check(error == 0, "the second USB controller is found");
	error = drv_rp1_find_child("snps,dwc3", usb1, &node);
	failures += check(error != 0, "there is no third USB controller");
	error = drv_rp1_reg(usb0, 0, &address, &size);
	failures += check(error == 0 && address == EXPECT_USB0 && size == EXPECT_USB_SIZE, "the first controller's registers");
	error = drv_rp1_reg(usb1, 0, &address, &size);
	failures += check(error == 0 && address == EXPECT_USB1 && size == EXPECT_USB_SIZE, "the second controller's registers");

	/* The Ethernet controller. */
	error = drv_rp1_find_child("cdns,macb", DRV_FDT_NO_NODE, &node);
	failures += check(error == 0, "the Ethernet controller is found");
	error = drv_rp1_reg(node, 0, &address, &size);
	failures += check(error == 0 && address == EXPECT_GEM, "the Ethernet controller's registers");

	/* The DMA window. */
	error = drv_rp1_dma_window(&offset, &limit, &coherent);
	failures += check(error == 0 && offset == EXPECT_DMA_OFFSET, "RP1's DMA sees memory at 64 GiB");
	failures += check(limit == EXPECT_DMA_LIMIT, "RP1's DMA reaches 64 GiB of memory");
	failures += check(coherent == 0, "RP1's DMA does not snoop");

	/* The inbound windows: RP1's own window, memory, and the MSI receiver. */
	pcie = find_block(EXPECT_PCIE_REGISTERS);
	failures += check(pcie != NULL, "the root complex was mapped");
	if (pcie == NULL)
		return failures;
	failures += check(read_le32(pcie, 0x402cU) == 0x00000007U, "window 1 covers 4 MiB at PCI 0");
	failures += check(read_le32(pcie, 0x40acU) == 0x00000001U && read_le32(pcie, 0x40b0U) == 0x1fU, "window 1 lands on RP1's own window");
	failures += check(read_le32(pcie, 0x4034U) == 0x00000015U && read_le32(pcie, 0x4038U) == 0x10U, "window 2 covers 64 GiB at PCI 64 GiB");
	failures += check(read_le32(pcie, 0x40b4U) == 0x00000001U && read_le32(pcie, 0x40b8U) == 0, "window 2 lands on memory");
	failures += check(read_le32(pcie, 0x403cU) == 0xfffff01cU && read_le32(pcie, 0x4040U) == 0xffU, "window 3 covers the MSI page");
	failures += check(read_le32(pcie, 0x40bcU) == 0x00130001U && read_le32(pcie, 0x40c0U) == 0x10U, "window 3 lands on the MSI receiver");
	failures += check((read_le32(pcie, 0x8004U) & 0x6U) == 0x6U, "RP1 decodes memory and masters");

	/* A pin's function. */
	error = drv_rp1_gpio_init();
	failures += check(error == 0, "the GPIO block is mapped");
	error = drv_rp1_gpio_set_function(VBUS_PIN, VBUS_FUNCTION);
	failures += check(error == 0, "the VBUS pin is connected");
	gpio = find_block(EXPECT_GPIO);
	pads = find_block(EXPECT_GPIO + 0x20000U);
	failures += check(gpio != NULL && pads != NULL, "the GPIO blocks were mapped");
	if (gpio == NULL || pads == NULL)
		return failures;
	failures += check(read_le32(gpio, VBUS_CONTROL) == VBUS_FUNCTION, "the pin's selector is its VBUS function");
	pad = read_le32(pads, VBUS_PAD);
	failures += check((pad & 0x40U) != 0 && (pad & 0x80U) == 0, "the pin's pad is enabled");
	error = drv_rp1_gpio_set_function(54U, 0);
	failures += check(error != 0, "a pin RP1 does not have is refused");

	/* Succeeded: the failures counted. */
	return failures;
}

/* Finds the host memory of a block mapped at an address. */
static uint8_t *
find_block(
	uint64_t address)
{
	unsigned index;

	/* Looks at each mapping. */
	for (index = 0; index < mapping_count; index++) {
		/* The block mapped at the address. */
		if (mappings[index].address == address)
			return mappings[index].bytes;
	}

	/* No such block. */
	return NULL;
}

/* Reads a little-endian word of a block. */
static uint32_t
read_le32(
	const uint8_t *bytes,
	unsigned offset)
{
	uint32_t value;

	/* Copies the word. */
	memcpy(&value, bytes + offset, sizeof(value));

	/* Succeeded: the word. */
	return value;
}

/* Writes a little-endian word of a block. */
static void
write_le32(
	uint8_t *bytes,
	unsigned offset,
	uint32_t value)
{
	/* Copies the word. */
	memcpy(bytes + offset, &value, sizeof(value));
}

/* Reports a failed check and counts it. */
static int
check(
	int condition,
	const char *what)
{
	/* A passed check counts nothing. */
	if (condition)
		return 0;

	/* Succeeded: one failure counted. */
	printf("  FAIL: %s\n", what);
	return 1;
}
