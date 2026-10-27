/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the Broadcom STB root complex driver on the BCM2712.
 *
 * The production src/drivers/pci/pci-brcmstb.c is compiled unchanged.  The
 * kernel services it calls are replaced here, and its register accesses go
 * to a model of the Raspberry Pi 5's external PCIe controller: the bridge
 * held in reset by the board, PERST# in the PCIe control register, the MDIO
 * port of the PHY, the link that comes up once PERST# is released, the
 * root port's configuration space and, behind the index and data window, an
 * NVMe controller with a 64-bit BAR, MSI and MSI-X.  Waits advance a virtual
 * clock, so the order and the timing of the start sequence are checked as
 * well as the values it programs.
 *
 * Usage: brcmstb2712-host-test FIRMWARE.dtb ENABLED-C1.dtb ENABLED-D0.dtb
 */

#include <drivers/pci/pci-brcmstb.h>
#include <drivers/generic/dma.h>
#include <kern/clock.h>
#include <kern/device-io.h>
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

#define CHECK(expression)                                                     \
	do {                                                                   \
		checks++;                                                      \
		if (!(expression)) {                                          \
			fprintf(stderr, "brcmstb2712: failed at %s:%d: %s\n", \
			    __FILE__, __LINE__, #expression);                    \
			exit(EXIT_FAILURE);                                     \
		}                                                              \
	} while (0)

/* The registers the model gives meaning to. */
#define REG_LINK_STATUS		(0x00acU + 0x12U)
#define REG_LINK_CONTROL2	(0x00acU + 0x30U)
#define REG_VENDOR1		0x0188U
#define REG_ID_VAL3		0x043cU
#define REG_LINK_CAPABILITY	0x04dcU
#define REG_ROOT_CAP		0x04f8U
#define REG_MDIO_ADDRESS	0x1100U
#define REG_MDIO_WRITE_DATA	0x1104U
#define REG_PHY_CTL_15		0x184cU
#define REG_MISC_CTRL		0x4008U
#define REG_WIN0_LO		0x400cU
#define REG_WIN0_HI		0x4010U
#define REG_RC_BAR1_LO		0x402cU
#define REG_RC_BAR1_HI		0x4030U
#define REG_RC_BAR2_LO		0x4034U
#define REG_RC_BAR2_HI		0x4038U
#define REG_RC_BAR3_LO		0x403cU
#define REG_CONFIG_RETRY	0x405cU
#define REG_PCIE_CTRL		0x4064U
#define REG_STATUS		0x4068U
#define REG_REVISION		0x406cU
#define REG_WIN0_BASE_LIMIT	0x4070U
#define REG_WIN0_BASE_HI	0x4080U
#define REG_WIN0_LIMIT_HI	0x4084U
#define REG_MISC_CTRL_1		0x40a0U
#define REG_UBUS_CTRL		0x40a4U
#define REG_UBUS_TIMEOUT	0x40a8U
#define REG_REMAP1_LO		0x40acU
#define REG_REMAP1_HI		0x40b0U
#define REG_REMAP2_LO		0x40b4U
#define REG_REMAP2_HI		0x40b8U
#define REG_QOS_MAP0		0x4160U
#define REG_QOS_MAP7		0x4144U
#define REG_AXI_INTF		0x416cU
#define REG_AXI_READ_ERROR	0x4170U
#define REG_HARD_DEBUG		0x4304U
#define REG_INTR2_CLEAR		0x4408U
#define REG_INTR2_MASK_SET	0x4410U
#define REG_EXT_CFG_DATA	0x8000U
#define REG_EXT_CFG_INDEX	0x9000U

#define PERSTB			0x00000004U
#define HARD_DEBUG_CLKREQ	0x00000002U
#define HARD_DEBUG_PERST	0x00000008U
#define HARD_DEBUG_REFCLK_EN	0x00010000U
#define HARD_DEBUG_REFCLK_OUT	0x00100000U
#define HARD_DEBUG_L1SS		0x00200000U
#define HARD_DEBUG_IDDQ		0x08000000U
#define AXI_TIMING_FIX		0x00001000U

/* The Pi 5's pcie1 as its tree describes it. */
#define REGISTER_BASE		0x1000110000ULL
#define REGISTER_SIZE		0x9310ULL
#define WINDOW_CPU		0x1b80000000ULL
#define WINDOW_PCI		0x80000000ULL
#define WINDOW_SIZE		0x80000000ULL
#define MEMORY_PCI		0x1000000000ULL
#define MEMORY_SIZE		0x1000000000ULL
#define MIP_PCI			0xfffffff000ULL
#define MIP_CPU			0x1000131000ULL

/* The NVMe controller below the root port. */
#define NVME_IDENTITY		0xa808144dU
#define NVME_BAR_SIZE		0x4000U
#define NVME_MSI_CAPABILITY	0x50U
#define NVME_MSIX_CAPABILITY	0x70U

/* One write, stamped with the virtual time. */
struct model_event {
	unsigned offset;
	uint32_t value;
	unsigned long long time_us;
};

/* What a scenario changes about the modelled hardware. */
struct model_options {
	bool link_never_up;
	bool mdio_stuck;
	bool no_timing_fix;
	bool assert_fails;
	bool msi_32bit;
	unsigned retry_reads;
};

/* A bus and a device as the fake PCI core hands them to the driver. */
struct drv_pci_bus {
	struct drv_pci_bus *parent;
	struct drv_pci_device *bridge;
};

struct drv_pci_device {
	struct drv_pci_address address;
	struct drv_pci_bus *bus;
	unsigned msi_capability;
	unsigned msix_capability;
};

static struct model_options options;
static uint8_t registers[0x10000U];
static uint8_t endpoint[4096];
static uint32_t endpoint_bar_low;
static uint32_t endpoint_bar_high;
static uint32_t ext_index;
static unsigned long long now_us;
static unsigned long long perst_release_us;
static unsigned long long first_endpoint_us;
static bool bridge_in_reset;
static unsigned accesses_in_reset;
static unsigned register_accesses;
static unsigned bridge_asserts;
static unsigned bridge_releases;
static unsigned endpoint_reads;
static struct model_event events[1024];
static unsigned event_count;
static uint32_t mdio_registers[16];
static uint16_t mdio_data[16];
static unsigned mdio_count;
static uint8_t bar_memory[NVME_BAR_SIZE];

/* What the fake PCI core and DMA layer recorded. */
static const struct drv_pci_bus_ops *published_ops;
static void *published_host;
static uint16_t published_segment;
static struct drv_dma_constraints published_constraints;
static uint64_t published_bus_offset;
static uint64_t published_physical_limit;
static struct drv_pci_bus root_bus;
static int dma_token;

/* What the board's message operations were asked. */
static unsigned board_establishes;
static unsigned board_disestablishes;
static void *board_context_seen;

static int board_bridge_reset(void *context, int asserted);
static int board_msi_establish(void *context, drv_pci_msi_run_t run, void *argument, unsigned *handle, uint64_t *address, uint32_t *data);
static int board_msi_disestablish(void *context, unsigned handle);
static int message_run(void *argument);

static const struct drv_pci_brcmstb_board board_with_messages = {
	(void *)&board_context_seen,
	board_bridge_reset,
	board_msi_establish,
	board_msi_disestablish
};

static const struct drv_pci_brcmstb_board board_without_messages = {
	(void *)&board_context_seen,
	board_bridge_reset,
	NULL,
	NULL
};

static void model_reset(const struct model_options *scenario);
static uint32_t space_read(const uint8_t *space, unsigned offset, unsigned width);
static void space_write(uint8_t *space, unsigned offset, unsigned width, uint32_t value);
static uint32_t reg(unsigned offset);
static uint32_t model_read(unsigned offset, unsigned width);
static void model_write(unsigned offset, unsigned width, uint32_t value);
static const struct model_event *find_event(unsigned offset, uint32_t mask, uint32_t wanted, unsigned after);
static unsigned event_index(const struct model_event *event);
static bool written(unsigned offset);
static unsigned char *read_file(const char *path, size_t *size);
static void standard_config(struct drv_pci_brcmstb_config *config);
static void test_describe(const char *firmware, const char *enabled_c1, const char *enabled_d0);
static void test_start(void);
static void test_access_and_interrupts(void);
static void test_without_messages(void);
static void test_failures(void);
static void test_variations(void);

int
main(
	int argc,
	char **argv)
{
	if (argc != 4) {
		fprintf(stderr, "usage: %s FIRMWARE.dtb ENABLED-C1.dtb ENABLED-D0.dtb\n", argv[0]);
		return EXIT_FAILURE;
	}

	test_describe(argv[1], argv[2], argv[3]);
	test_start();
	test_access_and_interrupts();
	test_without_messages();
	test_failures();
	test_variations();
	printf("brcmstb2712: %u checks passed\n", checks);
	return EXIT_SUCCESS;
}

/* Finds pcie1 and pcie2 by their register base. */
static uint32_t
find_root_complex(
	const struct drv_fdt *fdt,
	uint64_t wanted)
{
	uint64_t base;
	uint64_t size;
	uint32_t node;

	node = DRV_FDT_NO_NODE;
	for (;;) {
		CHECK(drv_fdt_find_compatible(fdt, "brcm,bcm2712-pcie", node, &node) == 0);
		CHECK(drv_fdt_reg(fdt, node, 0, &base, &size) == 0);
		if (base == wanted)
			return node;
	}
}

/* Reads the controllers from the Pi 5's trees, as shipped and with pcie1 enabled. */
static void
test_describe(
	const char *firmware,
	const char *enabled_c1,
	const char *enabled_d0)
{
	struct drv_pci_brcmstb_config config;
	struct drv_fdt fdt;
	const char *paths[2];
	unsigned char *blob;
	size_t size;
	uint32_t node;
	unsigned index;

	/* The tree as shipped leaves pcie1 disabled. */
	blob = read_file(firmware, &size);
	CHECK(drv_fdt_open(&fdt, blob, size) == 0);
	node = find_root_complex(&fdt, REGISTER_BASE);
	CHECK(drv_pci_brcmstb_describe(&fdt, node, &config) == ENODEV);
	free(blob);

	/* Both steppings' trees with pcie1 enabled describe the same controller. */
	paths[0] = enabled_c1;
	paths[1] = enabled_d0;
	for (index = 0; index < 2U; index++) {
		blob = read_file(paths[index], &size);
		CHECK(drv_fdt_open(&fdt, blob, size) == 0);
		node = find_root_complex(&fdt, REGISTER_BASE);
		CHECK(drv_pci_brcmstb_describe(&fdt, node, &config) == 0);
		CHECK(config.variant == DRV_PCI_BRCMSTB_BCM2712);
		CHECK(config.register_base == REGISTER_BASE);
		CHECK(config.register_size == REGISTER_SIZE);
		CHECK(config.outbound_cpu_base == WINDOW_CPU);
		CHECK(config.outbound_pci_base == WINDOW_PCI);
		CHECK(config.outbound_size == WINDOW_SIZE);
		CHECK(config.inbound_count == 2U);
		CHECK(config.inbound[0].pci_base == MEMORY_PCI);
		CHECK(config.inbound[0].cpu_base == 0);
		CHECK(config.inbound[0].size == MEMORY_SIZE);
		CHECK(config.inbound[1].pci_base == MIP_PCI);
		CHECK(config.inbound[1].cpu_base == MIP_CPU);
		CHECK(config.inbound[1].size == 0x1000U);
		CHECK(config.inbound_pci_base == MEMORY_PCI);
		CHECK(config.inbound_cpu_base == 0);
		CHECK(config.inbound_size == MEMORY_SIZE);
		CHECK(config.intx_irq[0] == 32U + 219U);
		CHECK(config.intx_irq[3] == 32U + 222U);
		CHECK(config.max_link_speed == 2U);
		CHECK(config.tperst_ms == 0);
		CHECK(config.qos_valid == 1);
		CHECK(config.qos[0] == 3U && config.qos[3] == 3U);
		CHECK(config.segment == 0);
		CHECK(config.board == NULL);

		/* RP1's controller has three windows, the memory one second. */
		node = find_root_complex(&fdt, 0x1000120000ULL);
		CHECK(drv_pci_brcmstb_describe(&fdt, node, &config) == 0);
		CHECK(config.variant == DRV_PCI_BRCMSTB_BCM2712);
		CHECK(config.inbound_count == 3U);
		CHECK(config.inbound_pci_base == MEMORY_PCI);
		CHECK(config.inbound_size == MEMORY_SIZE);
		free(blob);
	}
}

/* Starts pcie1 with an NVMe controller behind it and checks the sequence. */
static void
test_start(void)
{
	static const struct model_options standard;
	static const uint8_t refclk_registers[7] = { 0x16, 0x17, 0x18, 0x19, 0x1b, 0x1c, 0x1e };
	static const uint16_t refclk_data[7] = { 0x50b9, 0xbda1, 0x0094, 0x97b4, 0x5030, 0x5030, 0x0007 };
	struct drv_pci_brcmstb_config config;
	struct drv_pci_brcmstb *host;
	const struct model_event *perst_hold;
	const struct model_event *serdes;
	const struct model_event *misc;
	const struct model_event *bar1;
	const struct model_event *clkreq_off;
	const struct model_event *perst_release;
	const struct model_event *safe;
	uint32_t value;
	unsigned index;

	model_reset(&standard);
	standard_config(&config);
	CHECK(drv_pci_brcmstb_start(&config, &host) == 0);

	/* The bridge was reset once, and nothing touched its registers while it was. */
	CHECK(bridge_asserts == 1U);
	CHECK(bridge_releases == 1U);
	CHECK(accesses_in_reset == 0);

	/* PERST# is held, then the SerDes powered, then the system bus opened. */
	perst_hold = find_event(REG_PCIE_CTRL, PERSTB, 0, 0);
	CHECK(perst_hold != NULL);
	serdes = find_event(REG_HARD_DEBUG, HARD_DEBUG_IDDQ, 0, event_index(perst_hold) + 1U);
	CHECK(serdes != NULL);
	misc = find_event(REG_MISC_CTRL, 0, 0, event_index(serdes) + 1U);
	CHECK(misc != NULL);
	CHECK(misc->time_us >= serdes->time_us + 100U);

	/* MISC_CTRL: access, all-ones, 512-byte bursts, RCB modes; the SCB size is kept. */
	value = reg(REG_MISC_CTRL);
	CHECK((value & 0x00001000U) != 0);
	CHECK((value & 0x00002000U) != 0);
	CHECK((value & 0x00300000U) == 0x00200000U);
	CHECK((value & 0x00000400U) != 0);
	CHECK((value & 0x00000080U) != 0);
	CHECK((value & 0xf8000000U) == 0x88000000U);

	/* The inbound windows: 64 GiB of memory and the MIP's page, each remapped. */
	bar1 = find_event(REG_RC_BAR1_LO, 0, 0, event_index(misc) + 1U);
	CHECK(bar1 != NULL);
	CHECK(reg(REG_RC_BAR1_LO) == 0x15U);
	CHECK(reg(REG_RC_BAR1_HI) == 0x10U);
	CHECK(reg(REG_REMAP1_LO) == 0x1U);
	CHECK(reg(REG_REMAP1_HI) == 0);
	CHECK(reg(REG_RC_BAR2_LO) == 0xfffff01cU);
	CHECK(reg(REG_RC_BAR2_HI) == 0xffU);
	CHECK(reg(REG_REMAP2_LO) == 0x00131001U);
	CHECK(reg(REG_REMAP2_HI) == 0x10U);
	CHECK(!written(REG_RC_BAR3_LO));

	/* Endianness and the controller's own interrupts. */
	CHECK((reg(REG_VENDOR1) & 0xcU) == 0);
	CHECK(reg(REG_INTR2_MASK_SET) == 0xffffffffU);
	CHECK(reg(REG_INTR2_CLEAR) == 0xffffffffU);

	/* The PHY got the 54 MHz reference clock through MDIO, block first. */
	CHECK(mdio_count == 8U);
	CHECK(mdio_registers[0] == 0x1fU);
	CHECK(mdio_data[0] == 0x1600U);
	for (index = 0; index < 7U; index++) {
		CHECK(mdio_registers[index + 1U] == refclk_registers[index]);
		CHECK(mdio_data[index + 1U] == refclk_data[index]);
	}

	/* The bridge's tuning. */
	CHECK((reg(REG_PHY_CTL_15) & 0xffU) == 0x12U);
	CHECK((reg(REG_UBUS_CTRL) & 0x00082000U) == 0x00082000U);
	CHECK(reg(REG_AXI_READ_ERROR) == 0xffffffffU);
	CHECK(reg(REG_UBUS_TIMEOUT) == 0x0b2d0000U);
	CHECK(reg(REG_CONFIG_RETRY) == 0x0aba0000U);
	value = reg(REG_AXI_INTF);
	CHECK((value & 0x80U) == 0);
	CHECK((value & 0x3800U) == 0x3800U);
	CHECK((value & 0x3fU) == 0x3fU);
	CHECK((reg(REG_MISC_CTRL_1) & 0x20U) == 0);
	CHECK(reg(REG_QOS_MAP0) == 0x3333U);
	CHECK(reg(REG_QOS_MAP7) == 0x3333U);

	/* Generation 2 at most, in the capability and the target. */
	CHECK((reg(REG_LINK_CAPABILITY) & 0xfU) == 2U);
	CHECK((space_read(registers, REG_LINK_CONTROL2, 2U) & 0xfU) == 2U);

	/* CLKREQ# is ignored before PERST# goes, and the endpoint waits 100 ms. */
	clkreq_off = find_event(REG_HARD_DEBUG, HARD_DEBUG_CLKREQ | HARD_DEBUG_REFCLK_EN | HARD_DEBUG_REFCLK_OUT | HARD_DEBUG_L1SS, 0, event_index(bar1) + 1U);
	CHECK(clkreq_off != NULL);
	perst_release = find_event(REG_PCIE_CTRL, PERSTB, PERSTB, event_index(clkreq_off) + 1U);
	CHECK(perst_release != NULL);
	CHECK(first_endpoint_us >= perst_release->time_us + 100000U);

	/* Afterwards the reference clock is driven all the time and L1 substates are off. */
	safe = find_event(REG_HARD_DEBUG, HARD_DEBUG_REFCLK_EN | HARD_DEBUG_REFCLK_OUT, HARD_DEBUG_REFCLK_EN | HARD_DEBUG_REFCLK_OUT, event_index(perst_release) + 1U);
	CHECK(safe != NULL);
	value = reg(REG_HARD_DEBUG);
	CHECK((value & (HARD_DEBUG_CLKREQ | HARD_DEBUG_L1SS | HARD_DEBUG_IDDQ | HARD_DEBUG_PERST)) == 0);
	CHECK((reg(REG_ROOT_CAP) & 0xf8U) == 0x10U);

	/* The outbound window, the bridge class and the NVMe BAR. */
	CHECK(reg(REG_WIN0_LO) == 0x80000000U);
	CHECK(reg(REG_WIN0_HI) == 0);
	CHECK(reg(REG_WIN0_BASE_LIMIT) == 0xfff08000U);
	CHECK((reg(REG_WIN0_BASE_HI) & 0xffU) == 0x1bU);
	CHECK((reg(REG_WIN0_LIMIT_HI) & 0xffU) == 0x1bU);
	CHECK((reg(REG_ID_VAL3) & 0xffffffU) == 0x060400U);
	CHECK((endpoint_bar_low & ~0xfU) == 0x80000000U);
	CHECK(endpoint_bar_high == 0);
	CHECK((space_read(registers, 0x18U, 4U) & 0xffffffU) == 0x010100U);
	CHECK(space_read(registers, 0x20U, 4U) == 0x80008000U);

	/* Publication: a 64-bit, non-coherent window at 0x10_0000_0000 on segment 1. */
	CHECK(drv_pci_brcmstb_publish(host) == 0);
	CHECK(published_segment == 1U);
	CHECK(published_ops->msi_establish != NULL);
	CHECK(published_ops->msi_disestablish != NULL);
	CHECK(published_constraints.address_bits == 64U);
	CHECK(published_constraints.coherent == 0);
	CHECK(published_bus_offset == MEMORY_PCI);
	CHECK(published_physical_limit == MEMORY_SIZE - 1U);
}

/* Uses the published host as the PCI core would. */
static void
test_access_and_interrupts(void)
{
	struct drv_pci_device device;
	struct drv_pci_irq irq;
	unsigned handle;
	uint64_t address;
	uint32_t data;
	uint32_t value;
	unsigned count;

	/* Segment 0 is not this host's. */
	memset(&device, 0, sizeof(device));
	device.bus = &root_bus;
	device.address.segment = 0;
	device.address.bus = 1U;
	CHECK(published_ops->config_read(published_host, &device.address, 0, 4U, &value) == EINVAL);

	/* Segment 1 reaches the NVMe controller. */
	device.address.segment = 1U;
	CHECK(published_ops->config_read(published_host, &device.address, 0, 4U, &value) == 0);
	CHECK(value == NVME_IDENTITY);

	/* MSI-X and 64-bit MSI are offered through the board, with their capability. */
	device.msi_capability = NVME_MSI_CAPABILITY;
	device.msix_capability = NVME_MSIX_CAPABILITY;
	CHECK(published_ops->allocate_irqs(published_host, &device, DRV_PCI_IRQ_MSIX, 1U, 1U, &irq, &count) == 0);
	CHECK(count == 1U);
	CHECK(irq.type == DRV_PCI_IRQ_MSIX);
	CHECK(irq.private_data[0] == NVME_MSIX_CAPABILITY);
	CHECK(published_ops->allocate_irqs(published_host, &device, DRV_PCI_IRQ_MSI, 1U, 1U, &irq, &count) == 0);
	CHECK(irq.type == DRV_PCI_IRQ_MSI);
	CHECK(irq.private_data[0] == NVME_MSI_CAPABILITY);

	/* MSI limited to 32-bit addresses cannot reach the MIP and is refused. */
	options.msi_32bit = true;
	CHECK(published_ops->allocate_irqs(published_host, &device, DRV_PCI_IRQ_MSI, 1U, 1U, &irq, &count) == ENOTSUP);
	options.msi_32bit = false;

	/* A function without MSI-X is sent on to the next kind. */
	device.msix_capability = 0;
	CHECK(published_ops->allocate_irqs(published_host, &device, DRV_PCI_IRQ_MSIX, 1U, 1U, &irq, &count) == ENOTSUP);

	/* The legacy pin INTA is the tree's first line. */
	space_write(endpoint, 0x3dU, 1U, 1U);
	CHECK(published_ops->allocate_irqs(published_host, &device, DRV_PCI_IRQ_INTX, 1U, 1U, &irq, &count) == 0);
	CHECK(irq.vector == 32U + 219U);

	/* The message hooks pass the request to the board and its answer back. */
	CHECK(published_ops->msi_establish(published_host, &device, message_run, &device, &handle, &address, &data) == 0);
	CHECK(board_establishes == 1U);
	CHECK(board_context_seen == (void *)&board_context_seen);
	CHECK(handle == 3U);
	CHECK(address == MIP_PCI);
	CHECK(data == 11U);
	CHECK(published_ops->msi_disestablish(published_host, handle) == 0);
	CHECK(board_disestablishes == 1U);
}

/* A board without a MIP gets the table without message hooks. */
static void
test_without_messages(void)
{
	static const struct model_options standard;
	struct drv_pci_brcmstb_config config;
	struct drv_pci_brcmstb *host;
	struct drv_pci_device device;
	struct drv_pci_irq irq;
	unsigned count;

	model_reset(&standard);
	standard_config(&config);
	config.board = &board_without_messages;
	CHECK(drv_pci_brcmstb_start(&config, &host) == 0);
	CHECK(drv_pci_brcmstb_publish(host) == 0);
	CHECK(published_ops->msi_establish == NULL);

	/* Only the pins are offered. */
	memset(&device, 0, sizeof(device));
	device.bus = &root_bus;
	device.address.segment = 1U;
	device.address.bus = 1U;
	device.msi_capability = NVME_MSI_CAPABILITY;
	device.msix_capability = NVME_MSIX_CAPABILITY;
	CHECK(published_ops->allocate_irqs(published_host, &device, DRV_PCI_IRQ_MSIX, 1U, 1U, &irq, &count) == ENOTSUP);
	CHECK(published_ops->allocate_irqs(published_host, &device, DRV_PCI_IRQ_MSI, 1U, 1U, &irq, &count) == ENOTSUP);
	CHECK(published_ops->allocate_irqs(published_host, &device, DRV_PCI_IRQ_INTX, 1U, 1U, &irq, &count) == 0);
}

/* Refusals and failed starts. */
static void
test_failures(void)
{
	static const struct model_options standard;
	struct drv_pci_brcmstb_board half;
	struct drv_pci_brcmstb_config config;
	struct drv_pci_brcmstb *host;
	struct model_options scenario;

	/* Configurations the controller or the board cannot carry out. */
	model_reset(&standard);
	standard_config(&config);
	config.board = NULL;
	CHECK(drv_pci_brcmstb_start(&config, &host) == EINVAL);
	standard_config(&config);
	config.register_size = 0x9000U;
	CHECK(drv_pci_brcmstb_start(&config, &host) == EINVAL);
	standard_config(&config);
	config.inbound[1].pci_base = MIP_PCI + 0x800U;
	CHECK(drv_pci_brcmstb_start(&config, &host) == EINVAL);
	standard_config(&config);
	config.inbound[1].cpu_base = MIP_CPU + 0x10U;
	CHECK(drv_pci_brcmstb_start(&config, &host) == EINVAL);
	standard_config(&config);
	config.inbound[0].size = MEMORY_SIZE * 2U;
	config.inbound[0].pci_base = 0;
	CHECK(drv_pci_brcmstb_start(&config, &host) == ERANGE);
	standard_config(&config);
	config.inbound_count = 0;
	CHECK(drv_pci_brcmstb_start(&config, &host) == EINVAL);
	standard_config(&config);
	config.max_link_speed = 4U;
	CHECK(drv_pci_brcmstb_start(&config, &host) == EINVAL);
	half = board_with_messages;
	half.msi_disestablish = NULL;
	standard_config(&config);
	config.board = &half;
	CHECK(drv_pci_brcmstb_start(&config, &host) == EINVAL);
	CHECK(register_accesses == 0);

	/* A board that cannot reset the bridge stops the start before any register is touched. */
	scenario = standard;
	scenario.assert_fails = true;
	model_reset(&scenario);
	standard_config(&config);
	CHECK(drv_pci_brcmstb_start(&config, &host) == EIO);
	CHECK(register_accesses == 0);

	/* A PHY that does not take its writes: the endpoint held, SerDes off, bridge in reset. */
	scenario = standard;
	scenario.mdio_stuck = true;
	model_reset(&scenario);
	standard_config(&config);
	CHECK(drv_pci_brcmstb_start(&config, &host) == ETIMEDOUT);
	CHECK((reg(REG_PCIE_CTRL) & PERSTB) == 0);
	CHECK((reg(REG_HARD_DEBUG) & HARD_DEBUG_IDDQ) != 0);
	CHECK(bridge_in_reset);
	CHECK(accesses_in_reset == 0);

	/* A link that never comes up: the same, after 200 ms. */
	scenario = standard;
	scenario.link_never_up = true;
	model_reset(&scenario);
	standard_config(&config);
	CHECK(drv_pci_brcmstb_start(&config, &host) == ETIMEDOUT);
	CHECK((reg(REG_PCIE_CTRL) & PERSTB) == 0);
	CHECK(bridge_in_reset);
	CHECK(now_us >= 200000U);
}

/* What the tree or the chip changes: no QoS map, no generation, an older AXI interface, retries, PERST# lead. */
static void
test_variations(void)
{
	static const struct model_options standard;
	struct drv_pci_brcmstb_config config;
	struct drv_pci_brcmstb *host;
	struct model_options scenario;
	const struct model_event *lead;
	const struct model_event *release;
	const struct model_event *lead_end;

	/* Without a QoS map or a generation the bridge keeps its own. */
	model_reset(&standard);
	standard_config(&config);
	config.qos_valid = 0;
	config.max_link_speed = 0;
	CHECK(drv_pci_brcmstb_start(&config, &host) == 0);
	CHECK(!written(REG_QOS_MAP0));
	CHECK(!written(REG_LINK_CAPABILITY));
	CHECK(!written(REG_LINK_CONTROL2));

	/* A chip where the timing fix does not stick limits outstanding requests to 15. */
	scenario = standard;
	scenario.no_timing_fix = true;
	model_reset(&scenario);
	standard_config(&config);
	CHECK(drv_pci_brcmstb_start(&config, &host) == 0);
	CHECK((reg(REG_AXI_INTF) & 0x3fU) == 15U);

	/* An endpoint that asks for retries for 50 ms is waited for and assigned. */
	scenario = standard;
	scenario.retry_reads = 5U;
	model_reset(&scenario);
	standard_config(&config);
	CHECK(drv_pci_brcmstb_start(&config, &host) == 0);
	CHECK((endpoint_bar_low & ~0xfU) == 0x80000000U);
	CHECK(now_us >= 100000U + 50000U);

	/* A PERST# lead time runs the clock with PERST# held at the connector first. */
	model_reset(&standard);
	standard_config(&config);
	config.tperst_ms = 5U;
	CHECK(drv_pci_brcmstb_start(&config, &host) == 0);
	lead = find_event(REG_HARD_DEBUG, HARD_DEBUG_PERST, HARD_DEBUG_PERST, 0);
	CHECK(lead != NULL);
	release = find_event(REG_PCIE_CTRL, PERSTB, PERSTB, event_index(lead) + 1U);
	CHECK(release != NULL);
	lead_end = find_event(REG_HARD_DEBUG, HARD_DEBUG_PERST, 0, event_index(release) + 1U);
	CHECK(lead_end != NULL);
	CHECK(lead_end->time_us >= release->time_us + 5000U);
}

/* The configuration of the Pi 5's pcie1, with a board that receives messages. */
static void
standard_config(
	struct drv_pci_brcmstb_config *config)
{
	memset(config, 0, sizeof(*config));
	config->register_base = REGISTER_BASE;
	config->register_size = REGISTER_SIZE;
	config->outbound_cpu_base = WINDOW_CPU;
	config->outbound_pci_base = WINDOW_PCI;
	config->outbound_size = WINDOW_SIZE;
	config->inbound_cpu_base = 0;
	config->inbound_pci_base = MEMORY_PCI;
	config->inbound_size = MEMORY_SIZE;
	config->intx_irq[0] = 32U + 219U;
	config->intx_irq[1] = 32U + 220U;
	config->intx_irq[2] = 32U + 221U;
	config->intx_irq[3] = 32U + 222U;
	config->variant = DRV_PCI_BRCMSTB_BCM2712;
	config->segment = 1U;
	config->inbound_count = 2U;
	config->inbound[0].pci_base = MEMORY_PCI;
	config->inbound[0].cpu_base = 0;
	config->inbound[0].size = MEMORY_SIZE;
	config->inbound[1].pci_base = MIP_PCI;
	config->inbound[1].cpu_base = MIP_CPU;
	config->inbound[1].size = 0x1000U;
	config->max_link_speed = 2U;
	config->qos_valid = 1;
	config->qos[0] = 3U;
	config->qos[1] = 3U;
	config->qos[2] = 3U;
	config->qos[3] = 3U;
	config->coherent = 0;
	config->board = &board_with_messages;
}

/* Puts the model in the state the firmware may leave the controller in. */
static void
model_reset(
	const struct model_options *scenario)
{
	options = *scenario;
	memset(registers, 0, sizeof(registers));
	memset(endpoint, 0, sizeof(endpoint));
	event_count = 0;
	now_us = 0;
	perst_release_us = 0;
	first_endpoint_us = 0;
	bridge_in_reset = false;
	accesses_in_reset = 0;
	register_accesses = 0;
	bridge_asserts = 0;
	bridge_releases = 0;
	endpoint_reads = 0;
	mdio_count = 0;
	ext_index = 0;

	/* The firmware's leftovers: PERST# released, SCB size and the SerDes off. */
	space_write(registers, REG_PCIE_CTRL, 4U, PERSTB);
	space_write(registers, REG_MISC_CTRL, 4U, 0x88000000U);
	space_write(registers, REG_HARD_DEBUG, 4U, HARD_DEBUG_IDDQ | HARD_DEBUG_CLKREQ);
	space_write(registers, REG_AXI_INTF, 4U, 0x0000003fU | 0x80U);
	space_write(registers, REG_ROOT_CAP, 4U, 0x000000f8U);
	space_write(registers, REG_LINK_CAPABILITY, 4U, 0x00000003U);
	space_write(registers, REG_LINK_CONTROL2, 2U, 0x0003U);
	space_write(registers, 0x0eU, 1U, 0x01U);

	/* The NVMe controller: identity, class, a 64-bit BAR of 16 KiB, INTA. */
	space_write(endpoint, 0x00U, 4U, NVME_IDENTITY);
	space_write(endpoint, 0x08U, 4U, 0x01080200U);
	space_write(endpoint, 0x3dU, 1U, 1U);
	endpoint_bar_low = 0x4U;
	endpoint_bar_high = 0;
}

static uint32_t
space_read(
	const uint8_t *space,
	unsigned offset,
	unsigned width)
{
	uint32_t value;
	unsigned index;

	value = 0;
	for (index = 0; index < width; index++)
		value |= (uint32_t)space[offset + index] << (index * 8U);
	return value;
}

static void
space_write(
	uint8_t *space,
	unsigned offset,
	unsigned width,
	uint32_t value)
{
	unsigned index;

	for (index = 0; index < width; index++)
		space[offset + index] = (uint8_t)(value >> (index * 8U));
}

/* Reads a model register without counting it as the driver's access. */
static uint32_t
reg(
	unsigned offset)
{
	return space_read(registers, offset, 4U);
}

static bool
link_up_now(void)
{
	if (options.link_never_up)
		return false;
	if ((reg(REG_PCIE_CTRL) & PERSTB) == 0)
		return false;
	return now_us >= perst_release_us + 1000U;
}

static uint32_t
model_read(
	unsigned offset,
	unsigned width)
{
	unsigned inner;

	register_accesses++;
	if (bridge_in_reset)
		accesses_in_reset++;

	/* The link, the root port mode and the endpoint behind the window. */
	if (offset == REG_STATUS) {
		if (link_up_now())
			return 0xb0U;
		return 0x80U;
	}
	if (offset == REG_LINK_STATUS)
		return 0x0012U;
	if (offset >= REG_EXT_CFG_DATA && offset < REG_EXT_CFG_DATA + 4096U) {
		inner = offset - REG_EXT_CFG_DATA;
		CHECK(link_up_now());
		CHECK(ext_index == (1U << 20));
		if (first_endpoint_us == 0)
			first_endpoint_us = now_us;

		/* Answers with all-ones while the endpoint still asks for retries. */
		if (inner == 0) {
			endpoint_reads++;
			if (endpoint_reads <= options.retry_reads)
				return 0xffffffffU;
		}
		if (inner == 0x10U)
			return endpoint_bar_low;
		if (inner == 0x14U)
			return endpoint_bar_high;
		if (inner >= 0x18U && inner < 0x28U)
			return 0;
		return space_read(endpoint, inner, width);
	}
	return space_read(registers, offset, width);
}

static void
model_write(
	unsigned offset,
	unsigned width,
	uint32_t value)
{
	unsigned inner;

	register_accesses++;
	if (bridge_in_reset)
		accesses_in_reset++;
	if (event_count < sizeof(events) / sizeof(events[0])) {
		events[event_count].offset = offset;
		events[event_count].value = value;
		events[event_count].time_us = now_us;
		event_count++;
	}

	/* The PHY takes a write at once unless it is stuck. */
	if (offset == REG_MDIO_WRITE_DATA) {
		CHECK((value & 0x80000000U) != 0);
		CHECK(mdio_count < 16U);
		mdio_registers[mdio_count] = reg(REG_MDIO_ADDRESS) & 0xffffU;
		mdio_data[mdio_count] = (uint16_t)value;
		CHECK((reg(REG_MDIO_ADDRESS) & 0x001f0000U) == 0);
		mdio_count++;
		if (!options.mdio_stuck)
			value &= 0x7fffffffU;
	}

	/* The release of PERST# starts the link's clock. */
	if (offset == REG_PCIE_CTRL && (value & PERSTB) != 0 && (reg(REG_PCIE_CTRL) & PERSTB) == 0)
		perst_release_us = now_us;

	/* An older chip does not keep the timing fix. */
	if (offset == REG_AXI_INTF && options.no_timing_fix)
		value &= ~AXI_TIMING_FIX;

	if (offset == REG_EXT_CFG_INDEX)
		ext_index = value;
	if (offset >= REG_EXT_CFG_DATA && offset < REG_EXT_CFG_DATA + 4096U) {
		inner = offset - REG_EXT_CFG_DATA;
		CHECK(link_up_now());
		if (inner == 0x10U) {
			endpoint_bar_low = (value & ~(NVME_BAR_SIZE - 1U)) | 0x4U;
		} else if (inner == 0x14U) {
			endpoint_bar_high = value;
		} else if (inner < 0x18U || inner >= 0x28U) {
			space_write(endpoint, inner, width, value);
		}
		return;
	}
	space_write(registers, offset, width, value);
}

static const struct model_event *
find_event(
	unsigned offset,
	uint32_t mask,
	uint32_t wanted,
	unsigned after)
{
	unsigned index;

	for (index = after; index < event_count; index++) {
		if (events[index].offset == offset && (events[index].value & mask) == wanted)
			return &events[index];
	}
	return NULL;
}

static unsigned
event_index(
	const struct model_event *event)
{
	return (unsigned)(event - events);
}

static bool
written(
	unsigned offset)
{
	return find_event(offset, 0, 0, 0) != NULL;
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

static int
board_bridge_reset(
	void *context,
	int asserted)
{
	CHECK(context == (void *)&board_context_seen);
	if (asserted) {
		if (options.assert_fails)
			return EIO;
		bridge_asserts++;
		bridge_in_reset = true;
		return 0;
	}
	bridge_releases++;
	bridge_in_reset = false;
	now_us += 100U;
	return 0;
}

static int
board_msi_establish(
	void *context,
	drv_pci_msi_run_t run,
	void *argument,
	unsigned *handle,
	uint64_t *address,
	uint32_t *data)
{
	board_context_seen = context;
	CHECK(run == message_run);
	CHECK(argument != NULL);
	board_establishes++;
	*handle = 3U;
	*address = MIP_PCI;
	*data = 11U;
	return 0;
}

static int
board_msi_disestablish(
	void *context,
	unsigned handle)
{
	CHECK(context == (void *)&board_context_seen);
	CHECK(handle == 3U);
	board_disestablishes++;
	return 0;
}

static int
message_run(
	void *argument)
{
	(void)argument;
	return 1;
}

uint8_t
kern_mmio_read8(
	const volatile void *address)
{
	return (uint8_t)model_read((unsigned)((const volatile uint8_t *)address - (const volatile uint8_t *)registers), 1U);
}

uint16_t
kern_mmio_read16(
	const volatile void *address)
{
	return (uint16_t)model_read((unsigned)((const volatile uint8_t *)address - (const volatile uint8_t *)registers), 2U);
}

uint32_t
kern_mmio_read32(
	const volatile void *address)
{
	return model_read((unsigned)((const volatile uint8_t *)address - (const volatile uint8_t *)registers), 4U);
}

void
kern_mmio_write8(
	volatile void *address,
	uint8_t value)
{
	model_write((unsigned)((volatile uint8_t *)address - (volatile uint8_t *)registers), 1U, value);
}

void
kern_mmio_write16(
	volatile void *address,
	uint16_t value)
{
	model_write((unsigned)((volatile uint8_t *)address - (volatile uint8_t *)registers), 2U, value);
}

void
kern_mmio_write32(
	volatile void *address,
	uint32_t value)
{
	model_write((unsigned)((volatile uint8_t *)address - (volatile uint8_t *)registers), 4U, value);
}

int
kern_device_map(
	uint64_t address,
	size_t size,
	unsigned attributes,
	void **mapped)
{
	CHECK(attributes == KERN_DEVICE_UNCACHED);
	if (address == REGISTER_BASE) {
		CHECK(size == REGISTER_SIZE);
		*mapped = registers;
		return 0;
	}
	CHECK(address >= WINDOW_CPU);
	CHECK(size <= sizeof(bar_memory));
	*mapped = bar_memory;
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
	now_us += min_us;
}

void
kern_logf(
	const char *format,
	...)
{
	va_list arguments;

	va_start(arguments, format);
	printf("  log: ");
	vprintf(format, arguments);
	va_end(arguments);
}

void *
kern_malloc(
	size_t size)
{
	return calloc(1, size);
}

void
kern_free(
	void *pointer)
{
	free(pointer);
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
	return 1;
}

void
spin_unlock_irqrestore(
	struct spinlock *lock,
	unsigned long enabled)
{
	(void)lock;
	CHECK(enabled == 1);
}

int
drv_dma_device_create_window(
	const struct drv_dma_constraints *constraints,
	uint64_t bus_offset,
	uint64_t physical_limit,
	struct drv_dma_device **result)
{
	published_constraints = *constraints;
	published_bus_offset = bus_offset;
	published_physical_limit = physical_limit;
	*result = (struct drv_dma_device *)&dma_token;
	return 0;
}

int
drv_dma_device_destroy(
	struct drv_dma_device *device)
{
	(void)device;
	return 0;
}

int
drv_pci_bus_create_root(
	uint16_t segment,
	uint8_t number,
	const struct drv_pci_bus_ops *ops,
	void *host,
	struct drv_dma_device *dma,
	struct drv_pci_bus **result)
{
	CHECK(number == 0);
	CHECK(dma == (struct drv_dma_device *)&dma_token);
	CHECK(ops->config_space_size == 4096U);
	published_segment = segment;
	published_ops = ops;
	published_host = host;
	*result = &root_bus;
	return 0;
}

int
drv_pci_bus_scan_tree(
	struct drv_pci_bus *bus)
{
	CHECK(bus == &root_bus);
	return 0;
}

int
drv_pci_device_config_read8(
	struct drv_pci_device *device,
	unsigned offset,
	uint8_t *value)
{
	uint32_t read;
	int error;

	error = published_ops->config_read(published_host, &device->address, offset, 1U, &read);
	*value = (uint8_t)read;
	return error;
}

int
drv_pci_device_config_read16(
	struct drv_pci_device *device,
	unsigned offset,
	uint16_t *value)
{
	CHECK(offset == device->msi_capability + 2U);
	*value = 0x0080U;
	if (options.msi_32bit)
		*value = 0;
	return 0;
}

int
drv_pci_device_find_capability(
	struct drv_pci_device *device,
	uint8_t id,
	unsigned *offset)
{
	if (id == 0x05U && device->msi_capability != 0) {
		*offset = device->msi_capability;
		return 0;
	}
	if (id == 0x11U && device->msix_capability != 0) {
		*offset = device->msix_capability;
		return 0;
	}
	return ENOENT;
}

struct drv_pci_bus *
drv_pci_device_bus(
	const struct drv_pci_device *device)
{
	return device->bus;
}

struct drv_pci_bus *
drv_pci_bus_parent(
	const struct drv_pci_bus *bus)
{
	return bus->parent;
}

struct drv_pci_device *
drv_pci_bus_bridge(
	const struct drv_pci_bus *bus)
{
	return bus->bridge;
}

void
drv_pci_device_address(
	const struct drv_pci_device *device,
	struct drv_pci_address *address)
{
	*address = device->address;
}
