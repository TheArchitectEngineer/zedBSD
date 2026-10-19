/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the PLDA PCIe root complex driver (ws256-p020).
 *
 * The real driver (src/drivers/pci/pci-plda.c), the real PCI core and the
 * real device tree reader run against fake registers for the VisionFive
 * 2's two root complexes: a register block whose interrupt status clears
 * on a write of ones, a configuration window with the root port on bus 0
 * and one endpoint on bus 1 whose BAR keeps only its address bits, and a
 * board whose PERST# drops the link and resets the endpoint.  The firmware
 * is modelled as having left the link up and the endpoints running.
 */

#include <drivers/generic/dma.h>
#include <drivers/generic/fdt.h>
#include <drivers/pci/pci-plda.h>
#include <drivers/pci/pci.h>
#include <kern/clock.h>
#include <kern/device-io.h>
#include <kern/irq.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/lock.h>
#include <kern/pmem.h>

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The register block, the two configured buses and an endpoint BAR's bytes. */
#define FAKE_APB_SIZE		0x1000U
#define FAKE_CONFIG_SIZE	0x200000U
#define FAKE_BAR_SIZE		0x4000U

/* Where the registers the driver programs sit in the block. */
#define REG_GEN_SETTINGS	0x080U
#define REG_CLASS_WORD		0x09cU
#define REG_FUNCTION_WORD	0x0b4U
#define REG_BAR_WINDOW		0x0fcU
#define REG_MASK		0x180U
#define REG_STATUS		0x184U
#define REG_MESSAGE		0x3f0U
#define REG_ATR(n, word)	(0x800U + (n) * 0x20U + (word) * 4U)

/* The pins' status bits, and one that is an event. */
#define STATUS_INTA		(1U << 24)
#define STATUS_INTB		(1U << 25)
#define STATUS_EVENT		(1U << 20)

/* How many polls the fake link takes to come up after PERST# is released. */
#define LINK_POLLS		3U

/*
 * One fake root complex: its registers, its configuration space, the
 * firmware's endpoint BAR contents and the board's PERST# and link.
 */
struct fake_rc {
	uint64_t apb_phys;
	uint64_t config_phys;
	uint64_t bar_phys;
	uint8_t apb[FAKE_APB_SIZE];
	uint8_t *config;
	uint8_t bar[FAKE_BAR_SIZE];
	uint32_t endpoint_bar_size;
	int perst;
	int link;
	unsigned polls;
	unsigned perst_asserts;
	unsigned perst_releases;
	unsigned selected;
	unsigned selections[8];
	unsigned selection_count;
	unsigned function_writes;
	unsigned function_writes_selected;
	unsigned root_bar_writes;
	unsigned acknowledges;
	uint64_t acknowledge_sequence;
	uint32_t acknowledged;
	unsigned irq;
	kern_irq_handler_t handler;
	void *argument;
	unsigned unmasks;
};

/* A device's handler as the check sees it. */
struct fixture {
	struct drv_pci_device *device;
	void *cookie;
	unsigned calls;
	uint64_t sequence;
};

/* The two fake root complexes, set up by main(). */
static struct fake_rc rcs[2];

/* An ordering stamp for handler calls and acknowledgements. */
static uint64_t sequence;

/* Spin locks held right now, and ends of interrupt with the last token. */
static unsigned locks_held;
static unsigned eoi_calls;
static kern_irq_ack_t last_acknowledge;
static unsigned handler_with_lock;

/* The DMA constraints the driver asked for, and how many devices. */
static struct drv_dma_constraints dma_constraints;
static unsigned dma_creates;
static int dma_token;

/* The kernel log, kept to look for lines. */
static char log_text[16384];
static size_t log_length;

/* Microseconds the driver waited. */
static unsigned long long waited_us;

static unsigned failures;
static unsigned passes;

static void check(int condition, const char *what);
static struct fake_rc *rc_of(const volatile void *address, unsigned *offset, int *config);
static uint32_t get32(const uint8_t *base, unsigned offset);
static void put32(uint8_t *base, unsigned offset, uint32_t value);
static uint8_t *config_of(struct fake_rc *rc, unsigned bus, unsigned offset);
static void reset_endpoint(struct fake_rc *rc);
static void board_perst(void *context, int asserted);
static int board_link_active(void *context);
static void board_select_function(void *context, unsigned function);
static int fixture_handler(void *argument);
static void rc_init(struct fake_rc *rc, uint64_t apb, uint64_t config, uint64_t bar, uint32_t class_code, uint32_t bar_size);
static void *read_file(const char *path, size_t *length);
static void attach(struct fixture *fixture, unsigned segment, unsigned expected_vector);
static void interrupt(struct fake_rc *rc, uint32_t status, kern_irq_ack_t token);

/* Allocates kernel memory. */
void *
kern_malloc(
	size_t size)
{
	/* The host's allocator stands in for the kernel's. */
	return malloc(size);
}

/* Allocates zeroed kernel memory. */
void *
kern_calloc(
	size_t count,
	size_t size)
{
	/* The host's allocator stands in for the kernel's. */
	return calloc(count, size);
}

/* Frees kernel memory. */
void
kern_free(
	void *pointer)
{
	/* The host's allocator stands in for the kernel's. */
	free(pointer);
}

/* Keeps a kernel log line. */
void
kern_logf(
	const char *format,
	...)
{
	va_list arguments;
	int written;

	/* Appends the line while there is room. */
	va_start(arguments, format);
	written = vsnprintf(log_text + log_length, sizeof(log_text) - log_length, format, arguments);
	va_end(arguments);
	if (written > 0 && (size_t)written < sizeof(log_text) - log_length)
		log_length += (size_t)written;
}

/* Reports interrupts as enabled. */
bool
kern_irq_disable(
	void)
{
	return true;
}

/* Enables interrupts, which the check does not model. */
void
kern_irq_enable(
	void)
{
}

/* Masks a controller line; the root complexes' lines are never masked. */
void
kern_irq_mask(
	int irq)
{
	(void)irq;
	check(0, "no controller line is masked");
}

/* Unmasks a root complex's own line. */
void
kern_irq_unmask(
	int irq)
{
	unsigned index;

	/* Counts the unmask on the root complex that owns the line. */
	for (index = 0; index < 2U; index++) {
		if (rcs[index].irq == (unsigned)irq)
			rcs[index].unmasks++;
	}
}

/* Counts an end of interrupt and keeps its token. */
void
kern_irq_send_eoi(
	kern_irq_ack_t acknowledge)
{
	eoi_calls++;
	last_acknowledge = acknowledge;
}

/* Stands in for a device-I/O barrier. */
void
kern_io_barrier(
	void)
{
}

/* Stands in for a device-I/O read barrier. */
void
kern_io_read_barrier(
	void)
{
}

/* Installs a root complex's line handler. */
int
kern_irq_register(
	int irq,
	kern_irq_handler_t handler,
	void *argument)
{
	unsigned index;

	/* Keeps the handler on the root complex that owns the line. */
	for (index = 0; index < 2U; index++) {
		if (rcs[index].irq != (unsigned)irq)
			continue;
		check(rcs[index].handler == NULL, "a line is taken once");
		rcs[index].handler = handler;
		rcs[index].argument = argument;
		return 0;
	}

	/* A pin never reaches the controller: the hooks take it. */
	check(0, "only the root complexes' own lines are registered");
	return EINVAL;
}

/* Removes a handler; nothing removes one in the check. */
int
kern_irq_unregister(
	int irq,
	kern_irq_handler_t handler,
	void *argument)
{
	(void)irq;
	(void)handler;
	(void)argument;
	check(0, "no line is unregistered");
	return EINVAL;
}

/* Refuses a message interrupt. */
int
kern_irq_register_msi(
	const char *source,
	kern_irq_handler_t handler,
	void *argument,
	int *irq,
	uint64_t *address,
	uint32_t *event)
{
	(void)source;
	(void)handler;
	(void)argument;
	(void)irq;
	(void)address;
	(void)event;
	return ENOTSUP;
}

/* Refuses a message interrupt removal. */
int
kern_irq_unregister_msi(
	int irq)
{
	(void)irq;
	return ENOTSUP;
}

/* Counts the wait. */
void
kern_usleep_range(
	unsigned min_us,
	unsigned max_us)
{
	(void)max_us;
	waited_us += min_us;
}

/* Starts a lock as free. */
void
spin_init(
	struct spinlock *lock,
	enum lock_rank rank,
	const char *name)
{
	(void)rank;
	lock->owner_valid = 0;
	lock->name = name;
}

/* Takes a lock; the check is single-threaded, so it must be free. */
unsigned long
spin_lock_irqsave(
	struct spinlock *lock)
{
	check(lock->owner_valid == 0, "a free lock is taken");
	lock->owner_valid = 1;
	locks_held++;
	return 1;
}

/* Releases a lock. */
void
spin_unlock_irqrestore(
	struct spinlock *lock,
	unsigned long enabled)
{
	(void)enabled;
	lock->owner_valid = 0;
	locks_held--;
}

/* Records what the driver asks of DMA. */
int
drv_dma_device_create(
	const struct drv_dma_constraints *constraints,
	struct drv_dma_device **result)
{
	dma_constraints = *constraints;
	dma_creates++;
	*result = (struct drv_dma_device *)&dma_token;
	return 0;
}

/* Destroys nothing. */
int
drv_dma_device_destroy(
	struct drv_dma_device *device)
{
	(void)device;
	return 0;
}

/* Hands out the fake register blocks by their physical addresses. */
int
kern_device_map(
	uint64_t address,
	size_t size,
	unsigned attributes,
	void **mapped)
{
	unsigned index;

	(void)attributes;

	/* Finds the fake memory the address names. */
	for (index = 0; index < 2U; index++) {
		if (address == rcs[index].apb_phys && size <= FAKE_APB_SIZE) {
			*mapped = rcs[index].apb;
			return 0;
		}
		if (address == rcs[index].config_phys && size <= FAKE_CONFIG_SIZE) {
			*mapped = rcs[index].config;
			return 0;
		}
		if (address == rcs[index].bar_phys && size <= FAKE_BAR_SIZE) {
			*mapped = rcs[index].bar;
			return 0;
		}
	}

	/* Refuses an address no fake block has. */
	check(0, "every mapped address is a fake block");
	return EINVAL;
}

/* Unmaps nothing. */
int
kern_device_unmap(
	void *mapped,
	size_t size)
{
	(void)mapped;
	(void)size;
	return 0;
}

/* Reads a byte. */
uint8_t
kern_mmio_read8(
	const volatile void *address)
{
	return *(const volatile uint8_t *)address;
}

/* Reads a halfword. */
uint16_t
kern_mmio_read16(
	const volatile void *address)
{
	uint16_t value;

	memcpy(&value, (const void *)address, sizeof(value));
	return value;
}

/* Reads a word. */
uint32_t
kern_mmio_read32(
	const volatile void *address)
{
	uint32_t value;

	memcpy(&value, (const void *)address, sizeof(value));
	return value;
}

/* Writes a byte. */
void
kern_mmio_write8(
	volatile void *address,
	uint8_t value)
{
	*(volatile uint8_t *)address = value;
}

/* Writes a halfword, which no BAR or status register takes. */
void
kern_mmio_write16(
	volatile void *address,
	uint16_t value)
{
	memcpy((void *)address, &value, sizeof(value));
}

/*
 * Writes a word with the fake hardware's meaning.
 *
 * The interrupt status clears the bits written as one; the root port's
 * BARs count every write that reaches them; the endpoint's BAR keeps only
 * its address bits and its 64-bit type.
 */
void
kern_mmio_write32(
	volatile void *address,
	uint32_t value)
{
	struct fake_rc *rc;
	unsigned offset;
	unsigned bus;
	unsigned reg;
	int config;

	/* Plain memory outside the fake blocks. */
	rc = rc_of(address, &offset, &config);
	if (rc == NULL) {
		memcpy((void *)address, &value, sizeof(value));
		return;
	}

	/* The register block. */
	if (!config) {
		if (offset == REG_STATUS) {
			put32(rc->apb, offset, get32(rc->apb, offset) & ~value);
			rc->acknowledges++;
			rc->acknowledged = value;
			rc->acknowledge_sequence = ++sequence;
			return;
		}
		if (offset == REG_FUNCTION_WORD) {
			rc->function_writes++;
			if (rc->selected != 0)
				rc->function_writes_selected++;
		}
		put32(rc->apb, offset, value);
		return;
	}

	/* The configuration window. */
	bus = offset >> 20;
	reg = offset & 0xfffU;
	if (bus == 0 && reg >= 0x10U && reg < 0x18U) {
		rc->root_bar_writes++;
		return;
	}
	if (bus == 1 && reg == 0x10U) {
		put32(rc->config, offset, (value & ~(rc->endpoint_bar_size - 1U)) | 0x4U);
		return;
	}
	put32(rc->config, offset, value);
}

/* Reads one 64-bit word, which nothing in the check uses. */
uint64_t
kern_mmio_read64(
	const volatile void *address)
{
	uint64_t value;

	memcpy(&value, (const void *)address, sizeof(value));
	return value;
}

/* Writes one 64-bit word, which nothing in the check uses. */
void
kern_mmio_write64(
	volatile void *address,
	uint64_t value)
{
	memcpy((void *)address, &value, sizeof(value));
}

/*
 * Runs every case and reports the result.
 */
int
main(
	int argc,
	char **argv)
{
	struct drv_pci_plda_config config;
	struct drv_pci_plda_board board;
	struct drv_pci_plda *hosts[2];
	struct fixture first;
	struct fixture second;
	struct drv_fdt fdt;
	uint32_t nodes[3];
	uint32_t node;
	size_t length;
	void *blob;
	unsigned index;
	int error;

	/* Reads the tree. */
	if (argc != 2)
		return 2;
	blob = read_file(argv[1], &length);
	if (blob == NULL)
		return 2;
	error = drv_fdt_open(&fdt, blob, length);
	check(error == 0, "the tree opens");

	/* Finds the three root complex nodes in order. */
	node = DRV_FDT_NO_NODE;
	for (index = 0; index < 3U; index++) {
		error = drv_fdt_find_compatible(&fdt, "starfive,jh7110-pcie", node, &node);
		check(error == 0, "each root complex node is found");
		nodes[index] = node;
	}

	/* An xHCI below the first root complex, an NVMe controller below the second. */
	rc_init(&rcs[0], 0x2b000000ULL, 0x940000000ULL, 0x30000000ULL, 0x0c033000U, 0x1000U);
	rc_init(&rcs[1], 0x2c000000ULL, 0x9c0000000ULL, 0x38000000ULL, 0x01080200U, 0x4000U);
	put32(rcs[0].bar, 0, 0x20U);
	put32(rcs[0].bar, 0x24U, 0x1U);
	put32(rcs[1].bar, 0x14U, 0x00460001U);
	put32(rcs[1].bar, 0x1cU, 0x1U);
	rcs[0].irq = 56;
	rcs[1].irq = 57;

	/* A disabled node is refused. */
	error = drv_pci_plda_describe(&fdt, nodes[2], &config);
	check(error == ENODEV, "a disabled root complex is ENODEV");

	/* The PCI core is up for both root complexes. */
	error = drv_pci_init();
	check(error == 0, "the PCI core starts");

	/* Describes, starts and publishes each root complex. */
	for (index = 0; index < 2U; index++) {
		/* Reads the node. */
		error = drv_pci_plda_describe(&fdt, nodes[index], &config);
		check(error == 0, "describe succeeds");
		check(config.config_base == rcs[index].config_phys, "the cfg reg is found by name");
		check(config.register_base == rcs[index].apb_phys, "the apb reg is found by name");
		check(config.irq == rcs[index].irq, "the shared line is the node's interrupt");
		check(config.segment == index, "the PCI domain is the segment");
		check(config.window_count == 2U, "two memory windows");
		check(config.windows[0].pci_base == rcs[index].bar_phys && !config.windows[0].wide, "the 32-bit window");
		check(config.windows[1].cpu_base == 0x900000000ULL + index * 0x80000000ULL &&
		      config.windows[1].wide && config.windows[1].prefetchable &&
		      config.windows[1].size == 0x40000000ULL,
		      "the 64-bit prefetchable window");
		check(config.intx_vector[0] == 1U && config.intx_vector[1] == 2U &&
		      config.intx_vector[2] == 3U && config.intx_vector[3] == 4U,
		      "pin n is the host's vector n");
		config.coherent = true;

		/* Starts it on the fake board. */
		board.context = &rcs[index];
		board.perst = board_perst;
		board.link_active = board_link_active;
		board.select_function = board_select_function;
		error = drv_pci_plda_start(&config, &board, &hosts[index]);
		check(error == 0, "start succeeds");

		/* Publishes it. */
		error = drv_pci_plda_publish(hosts[index]);
		check(error == 0, "publish succeeds");
	}

	/* The firmware's endpoint state was reported before the reset. */
	check(strstr(log_text, "pcie0: firmware left 1106:3483 class 0c0330, command 0006") != NULL, "the firmware's xHCI is named");
	check(strstr(log_text, "pcie0: xHCI USBSTS 00000001, HCHalted 1") != NULL, "HCHalted is read through the firmware's BAR");
	check(strstr(log_text, "pcie1: NVMe CC.EN 1, CSTS.RDY 1") != NULL, "CC.EN and CSTS.RDY are read");
	check(strstr(log_text, "pcie0: link up") != NULL && strstr(log_text, "pcie1: link up") != NULL, "both links come up");

	/* The register block of each root complex. */
	for (index = 0; index < 2U; index++) {
		struct fake_rc *rc = &rcs[index];
		uint32_t config_low = index == 0 ? 0x40000000U : 0xc0000000U;
		uint32_t wide_low = index == 0 ? 0x00000000U : 0x80000000U;

		/* The configuration window: base, implemented, 2^24 bytes, configuration requests. */
		check(get32(rc->apb, REG_ATR(0, 0)) == (config_low | 0x1U | (24U << 1)), "ATR 0 source and size");
		check(get32(rc->apb, REG_ATR(0, 1)) == 0x9U, "ATR 0 source high");
		check(get32(rc->apb, REG_ATR(0, 2)) == 0 && get32(rc->apb, REG_ATR(0, 3)) == 0, "ATR 0 translates to bus 0");
		check(get32(rc->apb, REG_ATR(0, 4)) == 1U, "ATR 0 carries configuration requests");

		/* The 32-bit window, 2^27 bytes, memory requests to the same address. */
		check(get32(rc->apb, REG_ATR(1, 0)) == ((uint32_t)rc->bar_phys | 0x1U | (27U << 1)), "ATR 1 source and size");
		check(get32(rc->apb, REG_ATR(1, 1)) == 0, "ATR 1 source high");
		check(get32(rc->apb, REG_ATR(1, 2)) == (uint32_t)rc->bar_phys && get32(rc->apb, REG_ATR(1, 3)) == 0, "ATR 1 target");
		check(get32(rc->apb, REG_ATR(1, 4)) == 0, "ATR 1 carries memory requests");

		/* The 64-bit window, 2^30 bytes, its own range (not the first one's). */
		check(get32(rc->apb, REG_ATR(2, 0)) == (wide_low | 0x1U | (30U << 1)), "ATR 2 source and size");
		check(get32(rc->apb, REG_ATR(2, 1)) == 0x9U, "ATR 2 source high");
		check(get32(rc->apb, REG_ATR(2, 2)) == wide_low && get32(rc->apb, REG_ATR(2, 3)) == 0x9U, "ATR 2 target");
		check(get32(rc->apb, REG_ATR(3, 0)) == 0, "no fourth ATR entry");

		/* The root port settings. */
		check((get32(rc->apb, REG_GEN_SETTINGS) & 0x1U) != 0, "the block is a root port");
		check(get32(rc->apb, REG_CLASS_WORD) == 0x06040000U, "the class is a PCI bridge");
		check((get32(rc->apb, REG_BAR_WINDOW) & 0x8U) != 0, "the prefetchable window is 64-bit");
		check(get32(rc->apb, REG_MESSAGE) == 0xfbU, "only the LTR bit is cleared");
		check(rc->function_writes == 3U && rc->function_writes_selected == 3U, "three extra functions disabled, each while selected");
		check(rc->selection_count == 4U && rc->selections[0] == 1U && rc->selections[1] == 2U &&
		      rc->selections[2] == 3U && rc->selections[3] == 0U,
		      "functions 1, 2, 3 selected, then 0 again");
		check((get32(rc->apb, REG_FUNCTION_WORD) & 0x8000U) != 0, "the function disable bit");

		/* PERST# held 100 ms, then released, and the link came back. */
		check(rc->perst_asserts == 1U && rc->perst_releases == 1U && rc->perst == 0, "PERST# asserted once and released");
		check(rc->link == 1, "the link is up");

		/* The tree below: bus numbers, the endpoint's BAR and the root port's window. */
		check(get32(rc->config, 0x18U) == 0x00010100U, "bus 1 is the root port's secondary and subordinate bus");
		check(get32(rc->config, (1U << 20) + 0x10U) == ((uint32_t)rc->bar_phys | 0x4U), "the endpoint's BAR is at the window's start");
		check(get32(rc->config, (1U << 20) + 0x14U) == 0, "the BAR's upper half is zero");
		check(get32(rc->config, 0x20U) == ((((uint32_t)rc->bar_phys >> 16) & 0xfff0U) * 0x10001U), "the root port forwards the used megabyte");
		check((get32(rc->config, 0x04U) & 0x6U) == 0x6U, "the root port forwards memory and DMA");
		check(rc->root_bar_writes == 0, "the root port's BARs are never written");
		check(get32(rc->config, 0x10U) == 0xdeadbee0U, "the root port's BAR is left as it was");

		/* The line is the host's, taken once, with every pin masked. */
		check(rc->handler != NULL && rc->unmasks == 1U, "the shared line is registered and unmasked");
		check(get32(rc->apb, REG_MASK) == 0, "every pin is masked before a driver asks");
	}

	/* DMA below 4 GiB, coherent as the board said, and the waits. */
	check(dma_creates == 2U && dma_constraints.address_bits == 32U && dma_constraints.coherent == 1, "DMA devices are 32-bit and coherent");
	check(waited_us >= 2U * 200000U, "PERST# and the settle time are waited for");

	/* 2 hosts, same pin: each endpoint uses INTA, the host's vector 1. */
	attach(&first, 0, 1);
	attach(&second, 1, 1);
	check(get32(rcs[0].apb, REG_MASK) == STATUS_INTA, "host 0 lets INTA through, nothing else");
	check(get32(rcs[1].apb, REG_MASK) == STATUS_INTA, "host 1 lets INTA through, nothing else");

	/* INTA with an event on host 0: one handler, acknowledged after it, one EOI. */
	interrupt(&rcs[0], STATUS_INTA | STATUS_EVENT, 0x101U);
	check(first.calls == 1U && second.calls == 0U, "host 0's interrupt runs only host 0's handler");
	check(rcs[0].acknowledged == (STATUS_INTA | STATUS_EVENT) && get32(rcs[0].apb, REG_STATUS) == 0, "the whole status is written back");
	check(rcs[0].acknowledge_sequence > first.sequence, "the status is acknowledged after the handler");
	check(eoi_calls == 1U && last_acknowledge == 0x101U, "one EOI with the line's token");
	check(handler_with_lock == 0, "handlers run with no host lock held");

	/* Host 1's INTA runs host 1's handler alone. */
	interrupt(&rcs[1], STATUS_INTA, 0x202U);
	check(first.calls == 1U && second.calls == 1U, "host 1's interrupt runs only host 1's handler");
	check(eoi_calls == 2U && last_acknowledge == 0x202U, "one EOI for host 1");

	/* A pin no driver asked for (INTB) is not served, but acknowledged and ended. */
	interrupt(&rcs[0], STATUS_INTB, 0x303U);
	check(first.calls == 1U, "a masked pin runs no handler");
	check(rcs[0].acknowledged == STATUS_INTB && eoi_calls == 3U, "a masked pin is acknowledged and ended once");

	/* An empty status, and an event alone, end the interrupt once each. */
	interrupt(&rcs[0], 0, 0x404U);
	check(eoi_calls == 4U && first.calls == 1U, "an empty status ends once");
	interrupt(&rcs[0], STATUS_EVENT, 0x505U);
	check(eoi_calls == 5U && first.calls == 1U, "an event alone ends once");

	/* Taking host 0's handler off masks host 0's pin only. */
	error = drv_pci_device_disestablish_irq_checked(first.device, first.cookie);
	check(error == 0, "host 0's handler comes off");
	check(get32(rcs[0].apb, REG_MASK) == 0, "host 0's INTA is masked again");
	check(get32(rcs[1].apb, REG_MASK) == STATUS_INTA, "host 1's INTA is untouched");
	interrupt(&rcs[0], STATUS_INTA, 0x606U);
	check(first.calls == 1U && eoi_calls == 6U, "a removed pin is not served, the interrupt still ends");
	interrupt(&rcs[1], STATUS_INTA, 0x707U);
	check(second.calls == 2U && eoi_calls == 7U, "host 1 still serves its pin");

	/* Reports the count. */
	printf("pci plda: %u PASS, %u FAIL\n", passes, failures);
	if (failures != 0)
		return 1;
	return 0;
}

/* Counts one check and names a failure. */
static void
check(
	int condition,
	const char *what)
{
	if (condition) {
		passes++;
		return;
	}
	failures++;
	printf("FAIL: %s\n", what);
}

/* Finds the fake root complex an address falls in. */
static struct fake_rc *
rc_of(
	const volatile void *address,
	unsigned *offset,
	int *config)
{
	const uint8_t *byte;
	unsigned index;

	/* Compares the address with each block. */
	byte = (const uint8_t *)address;
	for (index = 0; index < 2U; index++) {
		if (byte >= rcs[index].apb && byte < rcs[index].apb + FAKE_APB_SIZE) {
			*offset = (unsigned)(byte - rcs[index].apb);
			*config = 0;
			return &rcs[index];
		}
		if (rcs[index].config != NULL &&
		    byte >= rcs[index].config && byte < rcs[index].config + FAKE_CONFIG_SIZE) {
			*offset = (unsigned)(byte - rcs[index].config);
			*config = 1;
			return &rcs[index];
		}
	}

	/* Not a fake block. */
	return NULL;
}

/* Reads a little-endian word. */
static uint32_t
get32(
	const uint8_t *base,
	unsigned offset)
{
	uint32_t value;

	memcpy(&value, base + offset, sizeof(value));
	return value;
}

/* Writes a little-endian word. */
static void
put32(
	uint8_t *base,
	unsigned offset,
	uint32_t value)
{
	memcpy(base + offset, &value, sizeof(value));
}

/* Finds a register of bus 0's or bus 1's device 0. */
static uint8_t *
config_of(
	struct fake_rc *rc,
	unsigned bus,
	unsigned offset)
{
	return rc->config + (bus << 20) + offset;
}

/* Clears what a reset clears in the endpoint: its decoding and its BAR. */
static void
reset_endpoint(
	struct fake_rc *rc)
{
	put32(config_of(rc, 1, 0), 0x04U, 0);
	put32(config_of(rc, 1, 0), 0x10U, 0x4U);
	put32(config_of(rc, 1, 0), 0x14U, 0);
}

/* Drives the fake PERST#: asserting it drops the link and resets the endpoint. */
static void
board_perst(
	void *context,
	int asserted)
{
	struct fake_rc *rc;

	rc = context;
	rc->perst = asserted;
	if (asserted) {
		rc->perst_asserts++;
		rc->link = 0;
		rc->polls = 0;
		reset_endpoint(rc);
	} else {
		rc->perst_releases++;
	}
}

/* Reports the fake link, which comes up a few polls after PERST# is released. */
static int
board_link_active(
	void *context)
{
	struct fake_rc *rc;

	rc = context;
	if (rc->link)
		return 1;
	if (rc->perst)
		return 0;
	rc->polls++;
	if (rc->polls >= LINK_POLLS)
		rc->link = 1;
	return rc->link;
}

/* Records the selected function. */
static void
board_select_function(
	void *context,
	unsigned function)
{
	struct fake_rc *rc;

	rc = context;
	rc->selected = function;
	if (rc->selection_count < 8U)
		rc->selections[rc->selection_count++] = function;
}

/* Counts a device's interrupt and checks that no host lock is held. */
static int
fixture_handler(
	void *argument)
{
	struct fixture *fixture;

	fixture = argument;
	if (locks_held != 0)
		handler_with_lock++;
	fixture->calls++;
	fixture->sequence = ++sequence;
	return 1;
}

/*
 * Fills a fake root complex as the firmware leaves it: link up, the root
 * port numbered, the endpoint decoding its BAR at the window's start.
 */
static void
rc_init(
	struct fake_rc *rc,
	uint64_t apb,
	uint64_t config,
	uint64_t bar,
	uint32_t class_code,
	uint32_t bar_size)
{
	uint32_t identity;

	/* Places the blocks. */
	memset(rc, 0, sizeof(*rc));
	rc->apb_phys = apb;
	rc->config_phys = config;
	rc->bar_phys = bar;
	rc->config = calloc(1, FAKE_CONFIG_SIZE);
	rc->endpoint_bar_size = bar_size;
	rc->link = 1;

	/* The message register with every bit set, so a cleared bit shows. */
	put32(rc->apb, REG_MESSAGE, 0xffU);

	/* The root port: a bridge whose BAR holds a value that must stay. */
	put32(config_of(rc, 0, 0), 0x00U, 0x0100abcdU);
	put32(config_of(rc, 0, 0), 0x08U, 0x06040000U);
	put32(config_of(rc, 0, 0), 0x0cU, 0x00010000U);
	put32(config_of(rc, 0, 0), 0x10U, 0xdeadbee0U);
	put32(config_of(rc, 0, 0), 0x18U, 0x00010100U);

	/* The endpoint: an xHCI (VL805's ids) or an NVMe controller, pin INTA. */
	identity = class_code == 0x0c033000U ? 0x34831106U : 0x50121e0fU;
	put32(config_of(rc, 1, 0), 0x00U, identity);
	put32(config_of(rc, 1, 0), 0x04U, 0x0006U);
	put32(config_of(rc, 1, 0), 0x08U, class_code);
	put32(config_of(rc, 1, 0), 0x0cU, 0);
	put32(config_of(rc, 1, 0), 0x10U, (uint32_t)bar | 0x4U);
	put32(config_of(rc, 1, 0), 0x3cU, 0x00000100U);
}

/* Reads a whole file. */
static void *
read_file(
	const char *path,
	size_t *length)
{
	FILE *file;
	void *data;
	long size;

	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)size);
	if (data == NULL || fread(data, 1, (size_t)size, file) != (size_t)size) {
		fclose(file);
		free(data);
		return NULL;
	}
	fclose(file);
	*length = (size_t)size;
	return data;
}

/* Finds a root complex's endpoint, takes its pin and puts the handler on it. */
static void
attach(
	struct fixture *fixture,
	unsigned segment,
	unsigned expected_vector)
{
	struct drv_pci_address address;
	struct drv_pci_irq irq;
	unsigned count;
	int error;

	/* Finds the endpoint on bus 1. */
	memset(fixture, 0, sizeof(*fixture));
	memset(&address, 0, sizeof(address));
	address.segment = (uint16_t)segment;
	address.bus = 1;
	fixture->device = drv_pci_find_device(&address);
	check(fixture->device != NULL, "the endpoint is enumerated below the root port");
	if (fixture->device == NULL)
		exit(1);

	/* Takes its pin: swizzled across the root port, then the interrupt map. */
	count = 0;
	error = drv_pci_device_allocate_irqs(fixture->device, DRV_PCI_IRQ_ALLOW_INTX, 1, 1, &irq, &count);
	check(error == 0 && count == 1U && irq.type == DRV_PCI_IRQ_INTX, "a pin is allocated");
	check(irq.vector == expected_vector, "the pin is the host's vector");

	/* Puts the handler on it. */
	error = drv_pci_device_establish_irq(fixture->device, &irq, fixture_handler, fixture, "plda-test", &fixture->cookie);
	check(error == 0, "the handler is established");
}

/* Raises a status on a root complex and delivers its line's interrupt. */
static void
interrupt(
	struct fake_rc *rc,
	uint32_t status,
	kern_irq_ack_t token)
{
	put32(rc->apb, REG_STATUS, status);
	rc->handler((int)rc->irq, token, rc->argument);
}
