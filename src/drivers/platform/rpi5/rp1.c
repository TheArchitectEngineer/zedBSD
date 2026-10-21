/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Raspberry Pi 5's RP1 south bridge, as the firmware leaves it.
 *
 * The device tree puts an rp1 node, a bus of RP1's peripherals, under the
 * PCIe x4 root complex.  The root complex's registers give the link state
 * and the configuration space of the bus behind the root port, where RP1
 * is device 0.  RP1's second BAR is the window of its peripherals; an
 * address in the rp1 node's space is an offset into that window.
 *
 * The firmware trains the link and places RP1's BARs; this module sets
 * the root complex's inbound windows from the device tree again, because
 * RP1's DMA and its interrupt messages travel through them and the
 * firmware has no use of its own for the latter.
 */

#include <drivers/platform/rpi5/rp1.h>
#include <kern/device-io.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/pmem.h>
#include <uapi/errno.h>

/* The most bytes of the firmware's device tree the module reads. */
#define RP1_FDT_READ_LIMIT		(2U * 1024U * 1024U)

/*
 * The root complex's registers: the link status, the window that reaches
 * the configuration space of a function behind the root port, which the
 * index register selects by bus, device and function, and the inbound
 * windows (an RC BAR's PCI base and size, and where it lands on the CPU's
 * bus).  The root port's own configuration space is at the start of the
 * registers.
 *
 * Values referenced: OpenBSD sys/dev/fdt/bcm2711_pcie.c rev 1.20
 * (PCIE_MISC_PCIE_STATUS and its PHYLINKUP and DL_ACTIVE bits,
 * PCIE_EXT_CFG_DATA, PCIE_EXT_CFG_INDEX, bcmpcie_make_tag(),
 * PCIE_MISC_RC_BAR1_CONFIG_LO/HI and the size code of
 * bcmpcie_setup_inbound(), PCIE_MISC_UBUS_BAR1_CONFIG_REMAP_LO/HI and
 * its enable bit).
 */
#define PCIE_MISC_PCIE_STATUS		0x4068U
#define PCIE_STATUS_PHYLINKUP		(1U << 4)
#define PCIE_STATUS_DL_ACTIVE		(1U << 5)
#define PCIE_EXT_CFG_DATA		0x8000U
#define PCIE_EXT_CFG_INDEX		0x9000U
#define PCIE_EXT_CFG_BUS_SHIFT		20U
#define PCIE_RC_BAR_CONFIG_LO		0x402cU
#define PCIE_RC_BAR_CONFIG_HI		0x4030U
#define PCIE_UBUS_BAR_REMAP_LO		0x40acU
#define PCIE_UBUS_BAR_REMAP_HI		0x40b0U
#define PCIE_INBOUND_STRIDE		8U
#define PCIE_INBOUND_COUNT		3U
#define PCIE_RC_BAR_SIZE_MASK		0x1fU
#define PCIE_UBUS_REMAP_ENABLE		(1U << 0)
#define PCIE_RC_BAR_SIZE_SMALL_BASE	0x1cU
#define PCIE_RC_BAR_SIZE_SMALL_FIRST	12U
#define PCIE_RC_BAR_SIZE_SMALL_LAST	15U
#define PCIE_RC_BAR_SIZE_LARGE_LAST	36U
#define PCIE_RC_BAR_SIZE_LARGE_BIAS	15U

/* The bytes of the root complex's register block the module maps. */
#define PCIE_REGISTER_BYTES		0xa000U

/* The configuration registers the module reads and writes. */
#define PCI_ID				0x00U
#define PCI_COMMAND			0x04U
#define PCI_BUS_NUMBERS			0x18U
#define PCI_COMMAND_MEMORY		(1U << 1)
#define PCI_COMMAND_MASTER		(1U << 2)
#define PCI_BAR0			0x10U

/*
 * RP1's identity and its two BARs: the MSI-X table's and the peripherals'.
 *
 * Values referenced: OpenBSD sys/dev/pci/pcidevs (vendor RPI 0x1de4,
 * product RP1 0x0001) and sys/arch/arm64/dev/rpone.c rev 1.1
 * (RP1_MSIX_BAR, the first BAR, and RP1_PERIPH_BAR, the second).
 */
#define RP1_IDENTITY			0x00011de4U
#define RP1_BAR_COUNT			2U
#define RP1_PERIPHERAL_BAR		1U

/* A BAR's type bits: a 64-bit memory BAR carries its high half in the next register. */
#define PCI_BAR_TYPE_MASK		0x6U
#define PCI_BAR_TYPE_64BIT		0x4U
#define PCI_BAR_ADDRESS_MASK		0xfffffff0U

/* The space code of a PCI address's first cell: memory, 32-bit or 64-bit. */
#define PCI_SPACE_SHIFT			24U
#define PCI_SPACE_MASK			0x3U
#define PCI_SPACE_MEMORY32		0x2U
#define PCI_SPACE_MEMORY64		0x3U

/* The cells of a PCI address in the device tree: the space code and two address cells. */
#define PCI_ADDRESS_CELLS		3U

/*
 * What the module found of RP1.
 *
 * Filled once by drv_rp1_init() during the platform's start-up, before
 * any peripheral driver asks, and not changed afterwards; valid is zero
 * until everything is found.
 */
struct rp1_state {
	int valid;
	struct drv_fdt fdt;
	uint32_t pcie_node;
	uint32_t rp1_node;
	volatile uint8_t *pcie_registers;
	uint8_t bus;
	uint64_t window_child_base;
	uint64_t window_size;
	uint64_t bar_pci_base[RP1_BAR_COUNT];
	uint64_t dma_bus_offset;
	uint64_t dma_limit;
	int dma_coherent;
};

/* The one RP1 a Pi 5 has. */
static struct rp1_state rp1;

static int find_rp1(void);
static int is_bus_of_rp1(uint32_t node);
static int read_window(void);
static int map_root_complex(void);
static int check_link(void);
static uint32_t config_read32(unsigned offset);
static void config_write32(unsigned offset, uint32_t value);
static int read_bars(void);
static int read_inbound(unsigned index, uint64_t *pci_base, uint64_t *cpu_base, uint64_t *size);
static int inbound_to_cpu(uint64_t pci_address, uint64_t *cpu_address, uint64_t *remaining);
static int program_inbound_windows(void);
static uint32_t inbound_size_code(uint64_t size);
static int read_dma_window(void);

/*
 * Finds RP1 in the device tree at fdt_phys and checks it is reachable.
 */
int
drv_rp1_init(
	uint64_t fdt_phys)
{
	const void *blob;
	uint32_t identity;
	uint32_t command;
	int error;

	/* Opens the device tree through the direct map. */
	blob = kern_pmem_to_kernel((hal_physaddr_t)fdt_phys);
	if (blob == NULL)
		return ENODEV;
	error = drv_fdt_open(&rp1.fdt, blob, RP1_FDT_READ_LIMIT);
	if (error != 0)
		return error;

	/* Finds the rp1 node and the root complex above it. */
	error = find_rp1();
	if (error != 0) {
		kern_logf("rp1: no RP1 in the device tree\n");
		return ENODEV;
	}

	/* Reads where the rp1 node's addresses lie. */
	error = read_window();
	if (error != 0) {
		kern_logf("rp1: unusable ranges (%d)\n", error);
		return error;
	}

	/* Maps the root complex's registers. */
	error = map_root_complex();
	if (error != 0) {
		kern_logf("rp1: root complex registers unavailable (%d)\n", error);
		return error;
	}

	/* Refuses a link the firmware took down; RP1 cannot be reached then. */
	error = check_link();
	if (error != 0)
		return error;

	/* Checks that the function behind the root port is RP1. */
	identity = config_read32(PCI_ID);
	if (identity != RP1_IDENTITY) {
		kern_logf("rp1: bus %u device 0 is %08x, not RP1\n", (unsigned)rp1.bus, identity);
		return ENODEV;
	}

	/* Finds where the firmware placed the MSI-X table and the peripheral window. */
	error = read_bars();
	if (error != 0)
		return error;

	/* Opens the paths RP1's DMA and interrupt messages take into memory. */
	error = program_inbound_windows();
	if (error != 0) {
		kern_logf("rp1: unusable dma-ranges (%d)\n", error);
		return error;
	}

	/* Finds the addresses RP1's peripherals use to reach memory. */
	error = read_dma_window();
	if (error != 0) {
		kern_logf("rp1: no DMA window for the peripherals (%d)\n", error);
		return error;
	}

	/* Lets RP1 decode its BARs and reach memory. */
	command = config_read32(PCI_COMMAND) & 0xffffU;
	command |= PCI_COMMAND_MEMORY;
	command |= PCI_COMMAND_MASTER;
	config_write32(PCI_COMMAND, command);

	/* Reports what was found. */
	kern_logf("rp1: bus %u, peripherals at PCI %llx (%llx bytes), DMA offset %llx\n",
		  (unsigned)rp1.bus,
		  (unsigned long long)rp1.bar_pci_base[RP1_PERIPHERAL_BAR],
		  (unsigned long long)rp1.window_size,
		  (unsigned long long)rp1.dma_bus_offset);

	/* Succeeded: RP1's peripherals can be reached. */
	rp1.valid = 1;
	return 0;
}

/*
 * Reports the device tree RP1 was found in.
 */
const struct drv_fdt *
drv_rp1_fdt(
	void)
{
	/* No tree before RP1 was found. */
	if (!rp1.valid)
		return NULL;

	/* Succeeded: the opened tree. */
	return &rp1.fdt;
}

/*
 * Reports the rp1 node.
 */
uint32_t
drv_rp1_node(
	void)
{
	/* No node before RP1 was found. */
	if (!rp1.valid)
		return DRV_FDT_NO_NODE;

	/* Succeeded: the bus of RP1's peripherals. */
	return rp1.rp1_node;
}

/*
 * Reports the root complex above RP1.
 */
uint32_t
drv_rp1_root_complex_node(
	void)
{
	/* No node before RP1 was found. */
	if (!rp1.valid)
		return DRV_FDT_NO_NODE;

	/* Succeeded: the PCIe x4 root complex. */
	return rp1.pcie_node;
}

/*
 * Finds the next child of the rp1 node with a compatible string.
 */
int
drv_rp1_find_child(
	const char *compatible,
	uint32_t after,
	uint32_t *node)
{
	uint32_t child;
	unsigned index;
	int error;

	/* Refuses a request before RP1 was found, or with nowhere to put the answer. */
	if (!rp1.valid)
		return ENODEV;
	if (compatible == NULL || node == NULL)
		return EINVAL;

	/* Walks the rp1 node's children from the one after the last found. */
	child = after;
	for (;;) {
		error = drv_fdt_child(&rp1.fdt, rp1.rp1_node, child, &child);
		if (error != 0)
			return error;

		/* A child whose compatible list names the string is the answer. */
		error = drv_fdt_string_index(&rp1.fdt, child, "compatible", compatible, &index);
		if (error == 0)
			break;
	}

	/* Succeeded: the child. */
	*node = child;
	return 0;
}

/*
 * Reads one address and size pair of an rp1 child's reg, in CPU terms.
 */
int
drv_rp1_reg(
	uint32_t node,
	unsigned index,
	uint64_t *cpu_address,
	uint64_t *size)
{
	const uint8_t *value;
	uint64_t rp1_address;
	uint64_t pci_address;
	uint32_t length;
	uint32_t address_cells;
	uint32_t size_cells;
	uint32_t pair_cells;
	int error;

	/* Refuses a request before RP1 was found, or with nowhere to put the answer. */
	if (!rp1.valid)
		return ENODEV;
	if (cpu_address == NULL || size == NULL)
		return EINVAL;

	/* The rp1 node gives its children its own cell counts. */
	address_cells = drv_fdt_node_cells(&rp1.fdt, rp1.rp1_node, "#address-cells", 2U);
	size_cells = drv_fdt_node_cells(&rp1.fdt, rp1.rp1_node, "#size-cells", 1U);
	if (address_cells == 0 || address_cells > 2U || size_cells > 2U)
		return ERANGE;

	/* Finds the requested pair inside the reg property. */
	error = drv_fdt_property(&rp1.fdt, node, "reg", &value, &length);
	if (error != 0)
		return error;
	pair_cells = address_cells + size_cells;
	if ((uint64_t)(index + 1U) * pair_cells * 4U > length)
		return ENOENT;
	rp1_address = drv_fdt_cells_value(value, index * pair_cells, address_cells);
	*size = drv_fdt_cells_value(value, index * pair_cells + address_cells, size_cells);

	/* Moves the address into RP1's window on the PCI bus. */
	error = drv_rp1_to_pci(rp1_address, &pci_address);
	if (error != 0)
		return error;

	/* Moves it on through the root complex's outbound window. */
	error = drv_rp1_pci_to_cpu(pci_address, cpu_address);
	if (error != 0)
		return error;

	/* Succeeded: the pair in CPU terms. */
	return 0;
}

/*
 * Maps one address and size pair of an rp1 child's reg as device memory.
 */
int
drv_rp1_map(
	uint32_t node,
	unsigned index,
	volatile uint8_t **mapped,
	uint64_t *size)
{
	uint64_t cpu_address;
	void *address;
	int error;

	/* Refuses a request with nowhere to put the answer. */
	if (mapped == NULL || size == NULL)
		return EINVAL;

	/* Finds the pair on the CPU's bus. */
	error = drv_rp1_reg(node, index, &cpu_address, size);
	if (error != 0)
		return error;

	/* Refuses an empty pair. */
	if (*size == 0)
		return ERANGE;

	/* Maps it uncached, as every register block is. */
	error = kern_device_map(cpu_address, (size_t)*size, KERN_DEVICE_UNCACHED, &address);
	if (error != 0)
		return error;

	/* Succeeded: the registers can be reached. */
	*mapped = address;
	return 0;
}

/*
 * Translates an address of the rp1 node's own space to the PCI bus.
 */
int
drv_rp1_to_pci(
	uint64_t rp1_address,
	uint64_t *pci_address)
{
	uint64_t offset;

	/* Refuses a request before the window was read, or with nowhere to put the answer. */
	if (rp1.window_size == 0)
		return ENODEV;
	if (pci_address == NULL)
		return EINVAL;

	/* Refuses an address outside RP1's window. */
	if (rp1_address < rp1.window_child_base)
		return ENOENT;
	offset = rp1_address - rp1.window_child_base;
	if (offset >= rp1.window_size)
		return ENOENT;

	/* Succeeded: the same offset into the peripheral BAR. */
	*pci_address = rp1.bar_pci_base[RP1_PERIPHERAL_BAR] + offset;
	return 0;
}

/*
 * Reports how RP1's peripherals reach memory when they master.
 */
int
drv_rp1_dma_window(
	uint64_t *bus_offset,
	uint64_t *physical_limit,
	int *coherent)
{
	/* Refuses a request before RP1 was found, or with nowhere to put the answer. */
	if (!rp1.valid)
		return ENODEV;
	if (bus_offset == NULL || physical_limit == NULL || coherent == NULL)
		return EINVAL;

	/* Succeeded: the window read from the rp1 node's dma-ranges. */
	*bus_offset = rp1.dma_bus_offset;
	*physical_limit = rp1.dma_limit;
	*coherent = rp1.dma_coherent;
	return 0;
}

/*
 * Reads one 32-bit register of RP1's PCI configuration space.
 */
int
drv_rp1_config_read32(
	unsigned offset,
	uint32_t *value)
{
	/* Refuses a request before RP1 was found, or with nowhere to put the answer. */
	if (!rp1.valid)
		return ENODEV;
	if (value == NULL)
		return EINVAL;

	/* Succeeded: the register's value. */
	*value = config_read32(offset);
	return 0;
}

/*
 * Writes one 32-bit register of RP1's PCI configuration space.
 */
int
drv_rp1_config_write32(
	unsigned offset,
	uint32_t value)
{
	/* Refuses a request before RP1 was found. */
	if (!rp1.valid)
		return ENODEV;

	/* Writes the register through the configuration window. */
	config_write32(offset, value);

	/* Succeeded: the register holds the value. */
	return 0;
}

/*
 * Reports the address on the PCI bus of one of RP1's own BARs.
 */
int
drv_rp1_bar_pci_address(
	unsigned bar,
	uint64_t *pci_address)
{
	/* Refuses a request before RP1 was found, a BAR it does not have, or nowhere to put the answer. */
	if (!rp1.valid)
		return ENODEV;
	if (bar >= RP1_BAR_COUNT || pci_address == NULL)
		return EINVAL;

	/* Succeeded: where the firmware placed the BAR. */
	*pci_address = rp1.bar_pci_base[bar];
	return 0;
}

/*
 * Translates a PCI memory address to the CPU's through the root complex's
 * ranges, then through the buses above it.
 */
int
drv_rp1_pci_to_cpu(
	uint64_t pci_address,
	uint64_t *cpu_address)
{
	const uint8_t *value;
	uint64_t child_base;
	uint64_t parent_base;
	uint64_t window_size;
	uint32_t length;
	uint32_t above;
	uint32_t child_cells;
	uint32_t parent_cells;
	uint32_t size_cells;
	uint32_t entry_cells;
	uint32_t entry;
	uint32_t space;
	int error;

	/* Refuses a request with nowhere to put the answer. */
	if (cpu_address == NULL)
		return EINVAL;

	/* Reads the root complex's ranges and the widths of their entries. */
	error = drv_fdt_property(&rp1.fdt, rp1.pcie_node, "ranges", &value, &length);
	if (error != 0)
		return error;
	error = drv_fdt_parent(&rp1.fdt, rp1.pcie_node, &above);
	if (error != 0)
		return error;
	child_cells = drv_fdt_node_cells(&rp1.fdt, rp1.pcie_node, "#address-cells", PCI_ADDRESS_CELLS);
	parent_cells = drv_fdt_node_cells(&rp1.fdt, above, "#address-cells", 2U);
	size_cells = drv_fdt_node_cells(&rp1.fdt, rp1.pcie_node, "#size-cells", 2U);

	/* A PCI address has the space code and two address cells. */
	if (child_cells != PCI_ADDRESS_CELLS || parent_cells == 0 || parent_cells > 2U)
		return ERANGE;
	if (size_cells == 0 || size_cells > 2U)
		return ERANGE;

	/* Finds the memory window that holds the address. */
	entry_cells = child_cells + parent_cells + size_cells;
	for (entry = 0; (entry + 1U) * entry_cells * 4U <= length; entry++) {
		space = (uint32_t)(drv_fdt_cells_value(value, entry * entry_cells, 1U) >> PCI_SPACE_SHIFT) & PCI_SPACE_MASK;
		child_base = drv_fdt_cells_value(value, entry * entry_cells + 1U, 2U);
		parent_base = drv_fdt_cells_value(value, entry * entry_cells + child_cells, parent_cells);
		window_size = drv_fdt_cells_value(value, entry * entry_cells + child_cells + parent_cells, size_cells);

		/* Only a memory window carries a BAR. */
		if (space != PCI_SPACE_MEMORY32 && space != PCI_SPACE_MEMORY64)
			continue;

		/* Skips a window that does not hold the address. */
		if (pci_address < child_base)
			continue;
		if (pci_address - child_base >= window_size)
			continue;

		/* Moves the address up through the buses above the root complex. */
		error = drv_fdt_translate(&rp1.fdt, rp1.pcie_node, parent_base + (pci_address - child_base), cpu_address);
		if (error != 0)
			return error;

		/* Succeeded: the CPU address. */
		return 0;
	}

	/* Failed: no window of the root complex holds the address. */
	return ENOENT;
}

/*
 * Finds the rp1 node: a bus that is an interrupt controller, under an
 * enabled BCM2712 root complex.
 */
static int
find_rp1(
	void)
{
	uint32_t pcie;
	uint32_t child;
	bool enabled;
	int error;
	int found;

	/* Looks at each BCM2712 root complex in turn. */
	pcie = DRV_FDT_NO_NODE;
	for (;;) {
		error = drv_fdt_find_compatible(&rp1.fdt, "brcm,bcm2712-pcie", pcie, &pcie);
		if (error != 0)
			return error;

		/* A disabled root complex has no RP1 the firmware set up. */
		enabled = drv_fdt_node_enabled(&rp1.fdt, pcie);
		if (!enabled)
			continue;

		/* Looks at the root complex's children for RP1's bus. */
		child = DRV_FDT_NO_NODE;
		for (;;) {
			error = drv_fdt_child(&rp1.fdt, pcie, child, &child);
			if (error != 0)
				break;

			/* RP1's bus is the child that is also an interrupt controller. */
			found = is_bus_of_rp1(child);
			if (found) {
				rp1.pcie_node = pcie;
				rp1.rp1_node = child;

				/* Succeeded: the rp1 node and its root complex. */
				return 0;
			}
		}
	}
}

/* Tells whether a node is RP1's bus: a simple bus that is an interrupt controller. */
static int
is_bus_of_rp1(
	uint32_t node)
{
	const uint8_t *value;
	uint32_t length;
	unsigned index;
	int error;

	/* RP1's bus is a simple bus. */
	error = drv_fdt_string_index(&rp1.fdt, node, "compatible", "simple-bus", &index);
	if (error != 0)
		return 0;

	/* It is also the interrupt controller of its peripherals. */
	error = drv_fdt_property(&rp1.fdt, node, "interrupt-controller", &value, &length);
	if (error != 0)
		return 0;

	/* Succeeded: the node is RP1's bus. */
	return 1;
}

/*
 * Reads the rp1 node's ranges: the base and size of the space its
 * children's addresses are in, which is RP1's peripheral window.
 */
static int
read_window(
	void)
{
	const uint8_t *value;
	uint32_t length;
	uint32_t child_cells;
	uint32_t parent_cells;
	uint32_t size_cells;
	uint32_t entry_cells;
	int error;

	/* Reads the first entry of the ranges. */
	error = drv_fdt_property(&rp1.fdt, rp1.rp1_node, "ranges", &value, &length);
	if (error != 0)
		return error;

	/* An entry is a child address, a PCI address and a size. */
	child_cells = drv_fdt_node_cells(&rp1.fdt, rp1.rp1_node, "#address-cells", 2U);
	parent_cells = drv_fdt_node_cells(&rp1.fdt, rp1.pcie_node, "#address-cells", PCI_ADDRESS_CELLS);
	size_cells = drv_fdt_node_cells(&rp1.fdt, rp1.rp1_node, "#size-cells", 1U);
	if (child_cells == 0 || child_cells > 2U || size_cells == 0 || size_cells > 2U)
		return ERANGE;
	entry_cells = child_cells + parent_cells + size_cells;
	if (entry_cells * 4U > length)
		return ERANGE;

	/* Keeps the child base and the size; the PCI side is the BAR the firmware placed. */
	rp1.window_child_base = drv_fdt_cells_value(value, 0, child_cells);
	rp1.window_size = drv_fdt_cells_value(value, child_cells + parent_cells, size_cells);
	if (rp1.window_size == 0)
		return ERANGE;

	/* Succeeded: the window is known. */
	return 0;
}

/* Maps the root complex's register block. */
static int
map_root_complex(
	void)
{
	uint64_t register_base;
	uint64_t register_size;
	void *mapped;
	int error;

	/* Finds the block on the CPU's bus. */
	error = drv_fdt_reg(&rp1.fdt, rp1.pcie_node, 0, &register_base, &register_size);
	if (error != 0)
		return error;

	/* Covers at least the configuration window and its index register. */
	if (register_size < PCIE_REGISTER_BYTES)
		register_size = PCIE_REGISTER_BYTES;

	/* Maps it uncached. */
	error = kern_device_map(register_base, (size_t)register_size, KERN_DEVICE_UNCACHED, &mapped);
	if (error != 0)
		return error;

	/* Succeeded: the registers can be reached. */
	rp1.pcie_registers = mapped;
	return 0;
}

/*
 * Checks that the link to RP1 is up and finds the bus behind the root port.
 */
static int
check_link(
	void)
{
	uint32_t status;
	uint32_t numbers;

	/* The physical link and the data link layer must both be up. */
	status = kern_mmio_read32(rp1.pcie_registers + PCIE_MISC_PCIE_STATUS);
	if ((status & PCIE_STATUS_PHYLINKUP) == 0 || (status & PCIE_STATUS_DL_ACTIVE) == 0) {
		kern_logf("rp1: PCIe link down (status %08x); config.txt needs pciex4_reset=0\n", status);
		return ENODEV;
	}

	/* The root port's secondary bus is where RP1 is; zero means the firmware numbered none. */
	numbers = kern_mmio_read32(rp1.pcie_registers + PCI_BUS_NUMBERS);
	rp1.bus = (uint8_t)(numbers >> 8);
	if (rp1.bus == 0) {
		kern_logf("rp1: the root port has no bus behind it\n");
		return ENODEV;
	}

	/* Succeeded: RP1 can be reached on the bus. */
	return 0;
}

/* Reads a 32-bit register of RP1's configuration space. */
static uint32_t
config_read32(
	unsigned offset)
{
	uint32_t value;

	/* Selects RP1, device 0 function 0 of the bus behind the root port. */
	kern_mmio_write32(rp1.pcie_registers + PCIE_EXT_CFG_INDEX, (uint32_t)rp1.bus << PCIE_EXT_CFG_BUS_SHIFT);

	/* Reads the register through the configuration window. */
	value = kern_mmio_read32(rp1.pcie_registers + PCIE_EXT_CFG_DATA + (offset & ~3U));

	/* Succeeded: the register's value. */
	return value;
}

/* Writes a 32-bit register of RP1's configuration space. */
static void
config_write32(
	unsigned offset,
	uint32_t value)
{
	/* Selects RP1, then writes the register through the configuration window. */
	kern_mmio_write32(rp1.pcie_registers + PCIE_EXT_CFG_INDEX, (uint32_t)rp1.bus << PCIE_EXT_CFG_BUS_SHIFT);
	kern_mmio_write32(rp1.pcie_registers + PCIE_EXT_CFG_DATA + (offset & ~3U), value);
}

/* Reads where the firmware placed RP1's two BARs on the PCI bus. */
static int
read_bars(
	void)
{
	unsigned bar;
	unsigned offset;
	uint32_t low;
	uint32_t high;

	/* Reads each BAR, the MSI-X table's first, from consecutive registers. */
	offset = PCI_BAR0;
	for (bar = 0; bar < RP1_BAR_COUNT; bar++) {
		/* The BAR's low half; a 64-bit BAR has its high half next. */
		low = config_read32(offset);
		high = 0;
		offset += 4U;
		if ((low & PCI_BAR_TYPE_MASK) == PCI_BAR_TYPE_64BIT) {
			high = config_read32(offset);
			offset += 4U;
		}

		/* Refuses a BAR that cannot be read. */
		if (low == 0xffffffffU) {
			kern_logf("rp1: BAR %u unreadable\n", bar);
			return EIO;
		}

		/* Keeps the BAR's address on the PCI bus. */
		rp1.bar_pci_base[bar] = ((uint64_t)high << 32) | (low & PCI_BAR_ADDRESS_MASK);
	}

	/* Succeeded: both BARs are known. */
	return 0;
}

/*
 * Reads one entry of the root complex's dma-ranges: the PCI base of an
 * inbound window, the CPU address it lands on and its size.  ENOENT past
 * the last entry.
 */
static int
read_inbound(
	unsigned index,
	uint64_t *pci_base,
	uint64_t *cpu_base,
	uint64_t *size)
{
	const uint8_t *value;
	uint64_t parent_base;
	uint32_t length;
	uint32_t above;
	uint32_t parent_cells;
	uint32_t size_cells;
	uint32_t entry_cells;
	int error;

	/* Reads the dma-ranges and the widths of their entries. */
	error = drv_fdt_property(&rp1.fdt, rp1.pcie_node, "dma-ranges", &value, &length);
	if (error != 0)
		return error;
	error = drv_fdt_parent(&rp1.fdt, rp1.pcie_node, &above);
	if (error != 0)
		return error;
	parent_cells = drv_fdt_node_cells(&rp1.fdt, above, "#address-cells", 2U);
	size_cells = drv_fdt_node_cells(&rp1.fdt, rp1.pcie_node, "#size-cells", 2U);
	if (parent_cells == 0 || parent_cells > 2U || size_cells == 0 || size_cells > 2U)
		return ERANGE;

	/* Refuses an entry past the last. */
	entry_cells = PCI_ADDRESS_CELLS + parent_cells + size_cells;
	if ((uint64_t)(index + 1U) * entry_cells * 4U > length)
		return ENOENT;

	/* Reads the PCI side, skipping the space code, and the size. */
	*pci_base = drv_fdt_cells_value(value, index * entry_cells + 1U, 2U);
	parent_base = drv_fdt_cells_value(value, index * entry_cells + PCI_ADDRESS_CELLS, parent_cells);
	*size = drv_fdt_cells_value(value, index * entry_cells + PCI_ADDRESS_CELLS + parent_cells, size_cells);

	/* Moves the CPU side up through the buses above the root complex. */
	error = drv_fdt_translate(&rp1.fdt, rp1.pcie_node, parent_base, cpu_base);
	if (error != 0)
		return error;

	/* Succeeded: the window's three values. */
	return 0;
}

/*
 * Finds the inbound window that holds a PCI address and reports the CPU
 * address it reaches and how many bytes of the window follow it.
 */
static int
inbound_to_cpu(
	uint64_t pci_address,
	uint64_t *cpu_address,
	uint64_t *remaining)
{
	uint64_t pci_base;
	uint64_t cpu_base;
	uint64_t size;
	unsigned index;
	int error;

	/* Looks at each inbound window in turn. */
	for (index = 0;
	     ;
	     index++) {
		error = read_inbound(index, &pci_base, &cpu_base, &size);
		if (error != 0)
			return error;

		/* Skips a window that does not hold the address. */
		if (pci_address < pci_base)
			continue;
		if (pci_address - pci_base >= size)
			continue;

		/* Succeeded: the address on the CPU's bus. */
		*cpu_address = cpu_base + (pci_address - pci_base);
		*remaining = size - (pci_address - pci_base);
		return 0;
	}
}

/*
 * Sets the root complex's inbound windows from its dma-ranges, as the
 * first three RC BARs with their UBUS remaps, logging any the firmware
 * had left different.
 *
 * Procedure referenced: OpenBSD sys/dev/fdt/bcm2711_pcie.c rev 1.20
 * (bcmpcie_setup_inbound(), the BCM2712 branch: each dma-ranges entry is
 * one RC BAR, its size as a power-of-two code, and its CPU base in the
 * matching UBUS remap register with the enable bit).
 */
static int
program_inbound_windows(
	void)
{
	uint64_t pci_base;
	uint64_t cpu_base;
	uint64_t size;
	uint32_t config_low;
	uint32_t config_high;
	uint32_t remap_low;
	uint32_t remap_high;
	uint32_t old_config_low;
	uint32_t old_remap_low;
	unsigned index;
	unsigned offset;
	int error;

	/* Sets one RC BAR for each entry, as far as the controller has them. */
	for (index = 0; index < PCIE_INBOUND_COUNT; index++) {
		error = read_inbound(index, &pci_base, &cpu_base, &size);
		if (error == ENOENT)
			break;
		if (error != 0)
			return error;

		/* Encodes the window: its PCI base with the size code, and its CPU base. */
		config_low = ((uint32_t)pci_base & ~PCIE_RC_BAR_SIZE_MASK) | inbound_size_code(size);
		config_high = (uint32_t)(pci_base >> 32);
		remap_low = (uint32_t)cpu_base | PCIE_UBUS_REMAP_ENABLE;
		remap_high = (uint32_t)(cpu_base >> 32);

		/* Reads what the firmware left, to report a difference. */
		offset = index * PCIE_INBOUND_STRIDE;
		old_config_low = kern_mmio_read32(rp1.pcie_registers + PCIE_RC_BAR_CONFIG_LO + offset);
		old_remap_low = kern_mmio_read32(rp1.pcie_registers + PCIE_UBUS_BAR_REMAP_LO + offset);

		/* Reports a window the firmware had set otherwise. */
		if (old_config_low != config_low || old_remap_low != remap_low) {
			kern_logf("rp1: inbound window %u was %08x/%08x, now %08x/%08x\n",
				  index,
				  old_config_low,
				  old_remap_low,
				  config_low,
				  remap_low);
		}

		/* Writes the window. */
		kern_mmio_write32(rp1.pcie_registers + PCIE_RC_BAR_CONFIG_LO + offset, config_low);
		kern_mmio_write32(rp1.pcie_registers + PCIE_RC_BAR_CONFIG_HI + offset, config_high);
		kern_mmio_write32(rp1.pcie_registers + PCIE_UBUS_BAR_REMAP_LO + offset, remap_low);
		kern_mmio_write32(rp1.pcie_registers + PCIE_UBUS_BAR_REMAP_HI + offset, remap_high);
	}

	/* Refuses a root complex without any inbound window: nothing could reach memory. */
	if (index == 0)
		return ENOENT;

	/* Succeeded: RP1's writes reach memory and the interrupt receiver. */
	return 0;
}

/*
 * Encodes an inbound window's size as the RC BAR's size code: the power
 * of two that covers it, 4 KiB to 32 KiB as 0x1c to 0x1f and 64 KiB to
 * 64 GiB as 1 to 21; zero turns the window off.
 */
static uint32_t
inbound_size_code(
	uint64_t size)
{
	unsigned shift;

	/* Finds the smallest power of two that covers the window. */
	shift = 0;
	while (shift < 63U && ((uint64_t)1 << shift) < size)
		shift++;

	/* A small window takes the codes at the top of the field. */
	if (shift >= PCIE_RC_BAR_SIZE_SMALL_FIRST && shift <= PCIE_RC_BAR_SIZE_SMALL_LAST)
		return PCIE_RC_BAR_SIZE_SMALL_BASE + (shift - PCIE_RC_BAR_SIZE_SMALL_FIRST);

	/* A large one counts up from 64 KiB. */
	if (shift > PCIE_RC_BAR_SIZE_SMALL_LAST && shift <= PCIE_RC_BAR_SIZE_LARGE_LAST)
		return shift - PCIE_RC_BAR_SIZE_LARGE_BIAS;

	/* Succeeded: no code fits, so the window is off. */
	return 0;
}

/*
 * Reads the rp1 node's dma-ranges for the window its peripherals reach
 * memory through: the first entry whose PCI side lands on the start of
 * the CPU's memory.
 */
static int
read_dma_window(
	void)
{
	const uint8_t *value;
	uint64_t child_base;
	uint64_t pci_base;
	uint64_t size;
	uint64_t cpu_base;
	uint64_t remaining;
	uint32_t length;
	uint32_t child_cells;
	uint32_t size_cells;
	uint32_t entry_cells;
	uint32_t entry;
	int error;

	/* Reads the dma-ranges and the widths of their entries. */
	error = drv_fdt_property(&rp1.fdt, rp1.rp1_node, "dma-ranges", &value, &length);
	if (error != 0)
		return error;
	child_cells = drv_fdt_node_cells(&rp1.fdt, rp1.rp1_node, "#address-cells", 2U);
	size_cells = drv_fdt_node_cells(&rp1.fdt, rp1.rp1_node, "#size-cells", 1U);
	if (child_cells == 0 || child_cells > 2U || size_cells == 0 || size_cells > 2U)
		return ERANGE;

	/* Looks for the entry that reaches the start of memory. */
	entry_cells = child_cells + PCI_ADDRESS_CELLS + size_cells;
	for (entry = 0; (entry + 1U) * entry_cells * 4U <= length; entry++) {
		child_base = drv_fdt_cells_value(value, entry * entry_cells, child_cells);
		pci_base = drv_fdt_cells_value(value, entry * entry_cells + child_cells + 1U, 2U);
		size = drv_fdt_cells_value(value, entry * entry_cells + child_cells + PCI_ADDRESS_CELLS, size_cells);

		/* Skips an entry no inbound window of the root complex holds. */
		error = inbound_to_cpu(pci_base, &cpu_base, &remaining);
		if (error != 0)
			continue;

		/* Skips an entry that does not start at the start of memory. */
		if (cpu_base != 0)
			continue;

		/* The window ends at the shorter of the two. */
		if (remaining < size)
			size = remaining;
		if (size == 0)
			continue;

		/* Keeps the window and whether it is coherent. */
		rp1.dma_bus_offset = child_base;
		rp1.dma_limit = size - 1U;
		rp1.dma_coherent = drv_fdt_dma_coherent(&rp1.fdt, rp1.rp1_node, 0);

		/* Succeeded: the peripherals' addresses for memory. */
		return 0;
	}

	/* Failed: no entry reaches memory. */
	return ENOENT;
}
