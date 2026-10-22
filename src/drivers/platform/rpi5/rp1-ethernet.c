/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Raspberry Pi 5's Ethernet port: RP1's Cadence GEM and the board's
 * BCM54213PE PHY on the GEM's MDIO bus.
 *
 * Both descriptor rings and every packet slot live in memory the platform
 * owns for as long as the kernel runs, so the controller never reaches
 * freed memory across close and reopen.  A receive descriptor points at
 * its slot for good; a frame is copied out of the slot into the network
 * packet pool and the descriptor is handed back.  A transmitted frame is
 * copied into its slot.  The descriptors are 64-bit, as RP1's DMA
 * addresses are above 4 GiB.  No checksum offload is used.
 */

#include <drivers/platform/rpi5/rp1.h>
#include <drivers/platform/rpi5/rp1-ethernet.h>
#include <drivers/platform/rpi5/rp1-gpio.h>
#include <drivers/platform/rpi5/rp1-irq.h>
#include <drivers/ethernet/bcm54213pe.h>
#include <drivers/generic/dma.h>
#include <kern/clock.h>
#include <kern/device-io.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/lock.h>
#include <kern/net/net-device.h>
#include <kern/net/packet-buf.h>
#include <kern/pmem.h>
#include <kern/sched.h>
#include <kern/thread.h>
#include <uapi/errno.h>

/*
 * The GEM's registers and their bits.
 *
 * Values referenced: OpenBSD sys/dev/fdt/if_cad.c rev 1.17 (GEM_NETCTL,
 * GEM_NETCFG, GEM_NETSR, GEM_DMACR, GEM_TXSR, GEM_RXQBASE, GEM_TXQBASE,
 * GEM_RXSR, GEM_ISR, GEM_IER, GEM_IDR, GEM_PHYMNTNC, GEM_HASHL, GEM_HASHH,
 * GEM_LADDRL(), GEM_LADDRH(), GEM_MID, GEM_CFG6, GEM_CFG8, GEM_TXQ1BASE(),
 * GEM_RXQ1BASE(), GEM_TXQBASEHI, GEM_RXQBASEHI, GEM_SCR_TYPE1(),
 * GEM_SCR_TYPE2(), GEM_RXQ8BASE() and the bits of each).
 */
#define GEM_NETCTL			0x0000U
#define GEM_NETCTL_STARTTX		(1U << 9)
#define GEM_NETCTL_STATCLR		(1U << 5)
#define GEM_NETCTL_MDEN			(1U << 4)
#define GEM_NETCTL_TXEN			(1U << 3)
#define GEM_NETCTL_RXEN			(1U << 2)
#define GEM_NETCFG			0x0004U
#define GEM_NETCFG_SGMIIEN		(1U << 27)
#define GEM_NETCFG_RXCSUMEN		(1U << 24)
#define GEM_NETCFG_MDCCLKDIV_MASK	(0x7U << 18)
#define GEM_NETCFG_MDCCLKDIV_SHIFT	18U
#define GEM_NETCFG_FCSREM		(1U << 17)
#define GEM_NETCFG_RXOFFS_MASK		(0x3U << 14)
#define GEM_NETCFG_PCSSEL		(1U << 11)
#define GEM_NETCFG_1000			(1U << 10)
#define GEM_NETCFG_1536RXEN		(1U << 8)
#define GEM_NETCFG_UCASTHASHEN		(1U << 7)
#define GEM_NETCFG_MCASTHASHEN		(1U << 6)
#define GEM_NETCFG_BCASTDI		(1U << 5)
#define GEM_NETCFG_COPYALL		(1U << 4)
#define GEM_NETCFG_FDEN			(1U << 1)
#define GEM_NETCFG_100			(1U << 0)
#define GEM_NETSR			0x0008U
#define GEM_NETSR_PHY_MGMT_IDLE		(1U << 2)
#define GEM_DMACR			0x0010U
#define GEM_DMACR_DMA64			(1U << 30)
#define GEM_DMACR_AHBDISC		(1U << 24)
#define GEM_DMACR_RXBUF_MASK		(0xffU << 16)
#define GEM_DMACR_RXBUF_SHIFT		16U
#define GEM_DMACR_TXCSUMEN		(1U << 11)
#define GEM_DMACR_TXSIZE		(1U << 10)
#define GEM_DMACR_RXSIZE_MASK		(0x3U << 8)
#define GEM_DMACR_RXSIZE_8K		(0x3U << 8)
#define GEM_DMACR_ES_PDATA		(1U << 7)
#define GEM_DMACR_ES_DESCR		(1U << 6)
#define GEM_DMACR_BLEN_MASK		(0x1fU << 0)
#define GEM_DMACR_BLEN_16		(0x10U << 0)
#define GEM_TXSR			0x0014U
#define GEM_TXSR_TXGO			(1U << 3)
#define GEM_RXQBASE			0x0018U
#define GEM_TXQBASE			0x001cU
#define GEM_RXSR			0x0020U
#define GEM_ISR				0x0024U
#define GEM_IER				0x0028U
#define GEM_IDR				0x002cU
#define GEM_IXR_HRESP			(1U << 11)
#define GEM_IXR_RXOVR			(1U << 10)
#define GEM_IXR_TXDONE			(1U << 7)
#define GEM_IXR_RXDONE			(1U << 1)
#define GEM_PHYMNTNC			0x0034U
#define GEM_PHYMNTNC_CLAUSE_22		(1U << 30)
#define GEM_PHYMNTNC_OP_READ		(0x2U << 28)
#define GEM_PHYMNTNC_OP_WRITE		(0x1U << 28)
#define GEM_PHYMNTNC_ADDR_SHIFT		23U
#define GEM_PHYMNTNC_REG_SHIFT		18U
#define GEM_PHYMNTNC_MUST_10		(0x2U << 16)
#define GEM_PHYMNTNC_DATA_MASK		0xffffU
#define GEM_HASHL			0x0080U
#define GEM_HASHH			0x0084U
#define GEM_LADDRL			0x0088U
#define GEM_LADDRH			0x008cU
#define GEM_LADDR_STRIDE		8U
#define GEM_LADDRNUM			4U
#define GEM_MID				0x00fcU
#define GEM_MID_VERSION_SHIFT		16U
#define GEM_MID_VERSION_MASK		0xfffU
#define GEM_CFG6			0x0294U
#define GEM_CFG6_DMA64			(1U << 23)
#define GEM_CFG6_PRIQ_MASK		0xffffU
#define GEM_CFG8			0x029cU
#define GEM_CFG8_TYPE1_SHIFT		24U
#define GEM_CFG8_TYPE2_SHIFT		16U
#define GEM_TXQ1BASE			0x0440U
#define GEM_RXQ1BASE			0x0480U
#define GEM_QUEUE_BASE_DISABLE		(1U << 0)
#define GEM_TXQBASEHI			0x04c8U
#define GEM_RXQBASEHI			0x04d4U
#define GEM_SCR_TYPE1			0x0500U
#define GEM_SCR_TYPE2			0x0540U
#define GEM_RXQ8BASE			0x05c0U
#define GEM_MAX_PRIQ			16U

/* The first GEM version with the configuration registers CFG6 to CFG10. */
#define GEM_VERSION_WITH_CFG		0x7U

/*
 * A 64-bit descriptor's words and their bits.
 *
 * Values referenced: OpenBSD sys/dev/fdt/if_cad.c rev 1.17 (struct
 * cad_desc64, GEM_RXD_ADDR_WRAP, GEM_RXD_ADDR_USED, GEM_RXD_EOF,
 * GEM_RXD_SOF, GEM_RXD_LEN_MASK, GEM_TXD_USED, GEM_TXD_WRAP,
 * GEM_TXD_RLIMIT, GEM_TXD_CORRUPT, GEM_TXD_LCOLL, GEM_TXD_LAST,
 * GEM_TXD_LEN_MASK).
 */
#define GEM_DESC_ADDRESS_LOW		0U
#define GEM_DESC_STATUS			1U
#define GEM_DESC_ADDRESS_HIGH		2U
#define GEM_DESC_WORDS			4U
#define GEM_DESC_BYTES			16U
#define GEM_RXD_ADDR_WRAP		(1U << 1)
#define GEM_RXD_ADDR_USED		(1U << 0)
#define GEM_RXD_EOF			(1U << 15)
#define GEM_RXD_SOF			(1U << 14)
#define GEM_RXD_LEN_MASK		0x1fffU
#define GEM_TXD_USED			(1U << 31)
#define GEM_TXD_WRAP			(1U << 30)
#define GEM_TXD_RLIMIT			(1U << 29)
#define GEM_TXD_CORRUPT			(1U << 27)
#define GEM_TXD_LCOLL			(1U << 26)
#define GEM_TXD_LAST			(1U << 15)
#define GEM_TXD_LEN_MASK		0x3fffU

/*
 * The MDIO clock dividers the configuration's field selects, in order.
 * The MDIO clock must not exceed 2.5 MHz.
 *
 * Values referenced: OpenBSD sys/dev/fdt/if_cad.c rev 1.17 (the
 * mdcclk_divs table of cad_reset()).
 */
#define GEM_MDC_DIVIDER_COUNT		8U
#define GEM_MDC_MAX_HZ			2500000U

/*
 * RP1's clock controller: the Ethernet and timestamp clocks' control
 * registers and their enable bit, the system clock's number in the
 * clocks' binding, and the system clock's rate when the tree gives none.
 *
 * Values referenced: Raspberry Pi Linux rpi-6.12.y drivers/clk/clk-rp1.c
 * (CLK_ETH_CTRL, CLK_ETH_TSU_CTRL, CLK_CTRL_ENABLE) and
 * include/dt-bindings/clock/rp1.h (RP1_CLK_SYS); the rate is the tree's
 * assigned-clock-rates entry for it.
 */
#define RP1_CLK_ETH_CTRL		0x00064U
#define RP1_CLK_ETH_TSU_CTRL		0x00134U
#define RP1_CLK_CTRL_ENABLE		(1U << 11)
#define RP1_CLK_SYS			12U
#define RP1_CLK_SYS_DEFAULT_HZ		200000000U

/* Ring geometry, slot size, and frame bounds. */
#define GEM_RING_COUNT			256U
#define GEM_PACKET_BYTES		2048U
#define GEM_MIN_FRAME			60U
#define GEM_MAX_FRAME			1514U
#define GEM_HEADER_BYTES		14U
#define GEM_RING_ALIGN			4096U
#define GEM_PACKET_ALIGN		2048U

/*
 * Bounded waits and the periodic service.  The MDIO and transmit-idle
 * waits run under the device lock and spin on the monotonic counter; a
 * clause-22 frame at the 2.5 MHz MDIO clock takes about 26 us.
 */
#define GEM_MDIO_TIMEOUT_US		2000U
#define GEM_TX_IDLE_TIMEOUT_US		10000U
#define GEM_WAIT_MAX_READS		1000000U
#define GEM_PHY_RESET_SETTLE_MS		1U
#define GEM_PHY_RESET_MAX_MS		1000U
#define GEM_POLL_MS			20U
#define GEM_LINK_MS			250U

/* The interrupts the driver serves. */
#define GEM_INTERRUPTS			(GEM_IXR_HRESP | GEM_IXR_RXOVR | GEM_IXR_RXDONE | GEM_IXR_TXDONE)

/*
 * The one Ethernet controller of RP1.
 *
 * It lives for as long as the kernel runs once attached: the descriptor
 * rings, the slots and the interface are never given back.  The lock
 * guards the rings, the counters and the controller's registers, and is
 * taken with interrupts off; opened says whether the rings belong to the
 * controller.
 */
struct gem_device {
	struct spinlock lock;
	volatile uint8_t *registers;
	uint64_t register_size;
	uint32_t node;
	uint32_t netctl;
	unsigned phy_address;
	unsigned queue_mask;
	unsigned type1_screeners;
	unsigned type2_screeners;
	unsigned mdc_divider;
	struct drv_bcm54213pe phy;
	struct drv_dma_device *dma;
	struct drv_dma_buffer rings;
	struct drv_dma_buffer rx;
	struct drv_dma_buffer tx;
	volatile uint32_t *rx_descriptors;
	volatile uint32_t *tx_descriptors;
	struct net_device *net;
	struct thread *poll_thread;
	unsigned rx_next;
	unsigned tx_producer;
	unsigned tx_consumer;
	uint64_t next_link_check;
	unsigned opened;
	unsigned link;
	unsigned speed;
	unsigned mdio_fault;
	uint8_t mac[6];
};

static int gem_open(struct net_device *net);
static void gem_close(struct net_device *net);
static int gem_transmit(struct net_device *net, struct packet_buf *packet);
static unsigned gem_poll(struct net_device *net, unsigned budget);

/* The network callbacks; the slots outlive every close. */
static const struct net_device_ops gem_operations = {
	gem_open,
	gem_close,
	gem_transmit,
	gem_poll,
	NULL,
	NULL
};

/*
 * The MDIO clock dividers.
 *
 * Values referenced: OpenBSD sys/dev/fdt/if_cad.c rev 1.17 (mdcclk_divs).
 */
static const unsigned gem_mdc_dividers[GEM_MDC_DIVIDER_COUNT] = {
	8U, 16U, 32U, 48U, 64U, 96U, 128U, 224U
};

/* The Pi 5's one GEM; attachment keeps it until the kernel stops. */
static struct gem_device board_gem;

static int describe_controller(struct gem_device *device);
static int describe_phy(struct gem_device *device);
static int read_mac(struct gem_device *device);
static void enable_clocks(void);
static unsigned clock_rate(unsigned clock);
static int reset_phy(struct gem_device *device);
static void read_configuration(struct gem_device *device);
static int allocate_buffers(struct gem_device *device);
static void reset_controller(struct gem_device *device);
static void initialize_rings(struct gem_device *device);
static void start_controller(struct gem_device *device);
static int stop_transmitter(struct gem_device *device);
static int wait_register(struct gem_device *device, unsigned offset, uint32_t mask, uint32_t wanted, unsigned microseconds);
static uint32_t read_register(struct gem_device *device, unsigned offset);
static void write_register(struct gem_device *device, unsigned offset, uint32_t bits);
static int wait_mdio(struct gem_device *device);
static int mdio_operate(struct gem_device *device, uint32_t operation);
static int mdio_read(void *context, unsigned reg, uint16_t *data);
static int mdio_write(void *context, unsigned reg, uint16_t data);
static void update_link(struct gem_device *device);
static void reclaim_transmitted(struct gem_device *device);
static unsigned receive_packets(struct net_device *net, unsigned budget);
static void gem_interrupt(void *argument);
static void start_polling(void);
static void poll_worker(void *argument);
static int delay_milliseconds(unsigned milliseconds);

/*
 * Starts the controller and the PHY and publishes the interface.
 */
int
drv_rp1_ethernet_init(
	void)
{
	struct gem_device *device;
	const char *stage;
	unsigned vector;
	unsigned trigger;
	uint32_t identity;
	int error;

	/* Refuses a second attachment. */
	device = &board_gem;
	if (device->registers != NULL)
		return EALREADY;

	/* Prepares the lock of the rings and the registers. */
	spin_init(&device->lock, LOCK_RANK_DEVICE, "RP1 GEM");

	/* Finds the controller and its PHY in the tree. */
	stage = "device tree";
	error = describe_controller(device);
	if (error != 0)
		goto fail;

	/* Makes sure the controller's clocks run. */
	enable_clocks();

	/* Maps the registers. */
	stage = "registers";
	error = drv_rp1_map(device->node, 0, &device->registers, &device->register_size);
	if (error != 0)
		goto fail;

	/* Reads the controller's version, queues and DMA width. */
	read_configuration(device);

	/* Takes the board's address before the reset clears the filters. */
	stage = "MAC address";
	error = read_mac(device);
	if (error != 0)
		goto fail;

	/* Resets the controller and turns the MDIO bus on. */
	reset_controller(device);

	/* Resets the PHY through its GPIO. */
	stage = "PHY reset";
	error = reset_phy(device);
	if (error != 0)
		goto fail;

	/* Starts the PHY over MDIO. */
	stage = "PHY";
	device->phy.context = device;
	device->phy.read = mdio_read;
	device->phy.write = mdio_write;
	error = drv_bcm54213pe_initialize(&device->phy, &identity);
	if (error != 0)
		goto fail;

	/* Records the PHY found on the bus. */
	kern_logf("gem: PHY %u id %08x\n", device->phy_address, identity);

	/* Allocates the rings and the slots before anything is published. */
	stage = "DMA buffers";
	error = allocate_buffers(device);
	if (error != 0)
		goto fail;

	/* Finds RP1's interrupt path. */
	stage = "interrupt";
	error = drv_rp1_irq_init();
	if (error != 0)
		goto fail;

	/* Reads the controller's vector. */
	error = drv_rp1_irq_of(device->node, 0, &vector, &trigger);
	if (error != 0)
		goto fail;

	/* Routes it; the controller's sources stay disabled until open. */
	error = drv_rp1_irq_establish(vector, trigger, gem_interrupt, device);
	if (error != 0)
		goto fail;

	/* Obtains the interface's registry slot. */
	stage = "network allocation";
	device->net = net_device_alloc();
	if (device->net == NULL) {
		error = ENOMEM;
		goto fail;
	}

	/* Describes the interface: en0, the board's address, broadcast and multicast. */
	kern_memcpy(device->net->name, "en0", 4U);
	device->net->mtu = 1500U;
	device->net->hwaddr_len = 6U;
	kern_memcpy(device->net->hwaddr, device->mac, 6U);
	device->net->flags = NET_DEVICE_BROADCAST | NET_DEVICE_MULTICAST;
	device->net->ops = &gem_operations;
	device->net->driver_data = device;

	/* Publishes it. */
	stage = "network registration";
	error = net_device_create(device->net);
	if (error != 0)
		goto fail;

	/* Reports the interface. */
	kern_logf("gem: en0 ready MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
		  device->mac[0],
		  device->mac[1],
		  device->mac[2],
		  device->mac[3],
		  device->mac[4],
		  device->mac[5]);

	/* Succeeded: networkd can open the interface. */
	return 0;

fail:
	/* Reports where the attachment stopped; what was taken is kept, as nothing reaches it. */
	kern_logf("gem: initialization failed at %s (%d)\n", stage, error);
	return error;
}

/*
 * Starts the periodic service of the link and of missed interrupts.
 */
void
drv_rp1_ethernet_refresh(
	void)
{
	/* Starts the service of an attached interface. */
	start_polling();
}

/*
 * Finds the GEM among RP1's peripherals and its PHY.
 */
static int
describe_controller(
	struct gem_device *device)
{
	const struct drv_fdt *fdt;
	uint32_t node;
	bool enabled;
	int error;

	/* Finds the controller. */
	error = drv_rp1_find_child("cdns,macb", DRV_FDT_NO_NODE, &node);
	if (error != 0) {
		kern_logf("gem: no Ethernet controller in the device tree\n");
		return ENODEV;
	}

	/* Leaves a controller the tree turned off alone. */
	fdt = drv_rp1_fdt();
	enabled = drv_fdt_node_enabled(fdt, node);
	if (!enabled)
		return ENODEV;

	/* Keeps the controller's node. */
	device->node = node;

	/* Finds the PHY and its delays. */
	error = describe_phy(device);
	if (error != 0)
		return error;

	/* Succeeded: the controller and its PHY are known. */
	return 0;
}

/*
 * Reads the PHY's address and the RGMII delays the PHY must insert.
 */
static int
describe_phy(
	struct gem_device *device)
{
	const struct drv_fdt *fdt;
	const uint8_t *property;
	uint32_t length;
	uint32_t phandle;
	uint32_t phy;
	int comparison;
	int error;

	/* Requires a terminated interface-mode string. */
	fdt = drv_rp1_fdt();
	error = drv_fdt_property(fdt, device->node, "phy-mode", &property, &length);
	if (error != 0)
		return error;
	if (length == 0 || property[length - 1U] != 0)
		return EINVAL;

	/* Refuses another mode: the Pi 5 has the PHY insert both delays. */
	comparison = kern_strcmp((const char *)property, "rgmii-id");
	if (comparison != 0) {
		kern_logf("gem: PHY mode %s not supported\n", (const char *)property);
		return ENOTSUP;
	}

	/* The PHY inserts both delays. */
	device->phy.rx_delay = 1;
	device->phy.tx_delay = 1;

	/* Follows the phy-handle to the PHY node. */
	error = drv_fdt_property(fdt, device->node, "phy-handle", &property, &length);
	if (error != 0)
		return error;
	if (length != 4U)
		return EINVAL;
	phandle = (uint32_t)drv_fdt_cells_value(property, 0, 1U);
	error = drv_fdt_find_phandle(fdt, phandle, &phy);
	if (error != 0)
		return error;

	/* Reads the PHY's clause-22 address. */
	error = drv_fdt_property(fdt, phy, "reg", &property, &length);
	if (error != 0)
		return error;
	if (length != 4U)
		return EINVAL;
	device->phy_address = (unsigned)drv_fdt_cells_value(property, 0, 1U);
	if (device->phy_address > 31U)
		return EINVAL;

	/* Succeeded: MDIO can reach the PHY. */
	return 0;
}

/*
 * Takes the board's Ethernet address: the one the firmware wrote into the
 * tree, or else one the firmware left in the controller's filters.
 */
static int
read_mac(
	struct gem_device *device)
{
	const struct drv_fdt *fdt;
	const uint8_t *property;
	uint32_t length;
	uint32_t low;
	uint32_t high;
	unsigned nonzero;
	unsigned index;
	int error;

	/* Reads the tree's address. */
	kern_memset(device->mac, 0, sizeof(device->mac));
	fdt = drv_rp1_fdt();
	error = drv_fdt_property(fdt, device->node, "local-mac-address", &property, &length);
	if (error == 0 && length == sizeof(device->mac))
		kern_memcpy(device->mac, property, sizeof(device->mac));

	/* Keeps a unicast address that is not all zeros. */
	nonzero = 0;
	for (index = 0; index < sizeof(device->mac); index++)
		nonzero |= device->mac[index];
	if (nonzero != 0 && (device->mac[0] & 1U) == 0)
		return 0;

	/* Looks in the controller's filters for a unicast address the firmware left. */
	for (index = 0; index < GEM_LADDRNUM; index++) {
		low = read_register(device, GEM_LADDRL + index * GEM_LADDR_STRIDE);
		high = read_register(device, GEM_LADDRH + index * GEM_LADDR_STRIDE);

		/* Skips an empty filter and a multicast one. */
		if (low == 0 && (high & 0xffffU) == 0)
			continue;
		if ((low & 1U) != 0)
			continue;

		/* Unpacks the filter, the first octet in the low byte. */
		device->mac[0] = (uint8_t)low;
		device->mac[1] = (uint8_t)(low >> 8);
		device->mac[2] = (uint8_t)(low >> 16);
		device->mac[3] = (uint8_t)(low >> 24);
		device->mac[4] = (uint8_t)high;
		device->mac[5] = (uint8_t)(high >> 8);
		break;
	}

	/* Refuses to invent an address. */
	if (index == GEM_LADDRNUM) {
		kern_logf("gem: no Ethernet address from the firmware\n");
		return ENODEV;
	}

	/* Succeeded: a unicast address from the filters. */
	return 0;
}

/*
 * Turns RP1's Ethernet and timestamp clocks on when the firmware left them
 * off, without changing their sources or dividers.
 */
static void
enable_clocks(
	void)
{
	volatile uint8_t *clocks;
	uint64_t size;
	uint32_t node;
	uint32_t control;
	int error;

	/* Finds and maps RP1's clock controller. */
	error = drv_rp1_find_child("raspberrypi,rp1-clocks", DRV_FDT_NO_NODE, &node);
	if (error != 0)
		return;
	error = drv_rp1_map(node, 0, &clocks, &size);
	if (error != 0)
		return;
	if (size < RP1_CLK_ETH_TSU_CTRL + 4U)
		return;

	/* Turns the Ethernet clock on. */
	control = kern_mmio_read32(clocks + RP1_CLK_ETH_CTRL);
	if ((control & RP1_CLK_CTRL_ENABLE) == 0) {
		kern_logf("gem: Ethernet clock was off (%08x)\n", control);
		kern_mmio_write32(clocks + RP1_CLK_ETH_CTRL, control | RP1_CLK_CTRL_ENABLE);
	}

	/* Turns the timestamp clock on. */
	control = kern_mmio_read32(clocks + RP1_CLK_ETH_TSU_CTRL);
	if ((control & RP1_CLK_CTRL_ENABLE) == 0) {
		kern_logf("gem: timestamp clock was off (%08x)\n", control);
		kern_mmio_write32(clocks + RP1_CLK_ETH_TSU_CTRL, control | RP1_CLK_CTRL_ENABLE);
	}
}

/*
 * Reads a clock's rate from the clock controller's assigned-clock-rates;
 * zero when the tree does not assign one.
 */
static unsigned
clock_rate(
	unsigned clock)
{
	const struct drv_fdt *fdt;
	const uint8_t *clocks;
	const uint8_t *rates;
	uint64_t assigned;
	uint32_t clocks_length;
	uint32_t rates_length;
	uint32_t node;
	unsigned entry;
	unsigned rate;
	int found;
	int error;

	/* Reads the clock controller's assignments. */
	fdt = drv_rp1_fdt();
	error = drv_rp1_find_child("raspberrypi,rp1-clocks", DRV_FDT_NO_NODE, &node);
	if (error != 0)
		return 0;
	error = drv_fdt_property(fdt, node, "assigned-clocks", &clocks, &clocks_length);
	if (error != 0)
		return 0;
	error = drv_fdt_property(fdt, node, "assigned-clock-rates", &rates, &rates_length);
	if (error != 0)
		return 0;

	/* Each assignment is a phandle and a clock number, with one rate. */
	found = 0;
	for (entry = 0; (entry + 1U) * 8U <= clocks_length && (entry + 1U) * 4U <= rates_length; entry++) {
		/* Stops at the clock's assignment. */
		assigned = drv_fdt_cells_value(clocks, entry * 2U + 1U, 1U);
		if (assigned == clock) {
			found = 1;
			break;
		}
	}

	/* No assignment for the clock. */
	if (!found)
		return 0;

	/* Succeeded: the clock's assigned rate. */
	rate = (unsigned)drv_fdt_cells_value(rates, entry, 1U);
	return rate;
}

/*
 * Pulses the PHY's reset line, when the tree names one, for its duration.
 *
 * Procedure referenced: OpenBSD sys/dev/fdt/if_cad.c rev 1.17
 * (cad_attach(): assert the reset for the duration plus a millisecond,
 * release it, wait a millisecond).
 */
static int
reset_phy(
	struct gem_device *device)
{
	const struct drv_fdt *fdt;
	const uint8_t *property;
	uint32_t length;
	unsigned pin;
	unsigned active_low;
	unsigned duration;
	int error;

	/* A PHY without a reset line needs no pulse. */
	fdt = drv_rp1_fdt();
	error = drv_fdt_property(fdt, device->node, "phy-reset-gpios", &property, &length);
	if (error != 0)
		return 0;
	if (length != 12U)
		return EINVAL;
	pin = (unsigned)drv_fdt_cells_value(property, 1U, 1U);
	active_low = (unsigned)drv_fdt_cells_value(property, 2U, 1U) & 1U;

	/* Reads the pulse's length, a millisecond by default. */
	duration = drv_fdt_node_cells(fdt, device->node, "phy-reset-duration", 1U);
	if (duration > GEM_PHY_RESET_MAX_MS)
		duration = 1U;

	/* Asserts the reset. */
	error = drv_rp1_gpio_init();
	if (error != 0)
		return error;
	error = drv_rp1_gpio_set_output(pin, active_low == 0);
	if (error != 0)
		return error;
	error = delay_milliseconds(duration + 1U);
	if (error != 0)
		return error;

	/* Releases it and lets the PHY come out of reset. */
	error = drv_rp1_gpio_set_output(pin, active_low != 0);
	if (error != 0)
		return error;
	error = delay_milliseconds(GEM_PHY_RESET_SETTLE_MS);
	if (error != 0)
		return error;

	/* Succeeded: the PHY answers on MDIO. */
	return 0;
}

/*
 * Reads the controller's version and, on a version that has them, its
 * priority queues, screeners and DMA width, and chooses the MDIO divider.
 *
 * Procedure referenced: OpenBSD sys/dev/fdt/if_cad.c rev 1.17
 * (cad_attach(): CFG6 and CFG8 only from version 7 on; cad_reset(): the
 * smallest divider that keeps the MDIO clock at 2.5 MHz or below).
 */
static void
read_configuration(
	struct gem_device *device)
{
	uint32_t revision;
	uint32_t version;
	uint32_t config;
	unsigned rate;
	unsigned divider;

	/* Reads the version; queue 0 is always present. */
	revision = read_register(device, GEM_MID);
	version = (revision >> GEM_MID_VERSION_SHIFT) & GEM_MID_VERSION_MASK;
	device->queue_mask = 1U;
	device->type1_screeners = 0;
	device->type2_screeners = 0;

	/* Reads the queues and the screeners of a version that has CFG6 and CFG8. */
	if (version >= GEM_VERSION_WITH_CFG) {
		config = read_register(device, GEM_CFG6);
		device->queue_mask |= config & GEM_CFG6_PRIQ_MASK;
		if ((config & GEM_CFG6_DMA64) == 0)
			kern_logf("gem: no 64-bit DMA reported\n");
		config = read_register(device, GEM_CFG8);
		device->type1_screeners = (config >> GEM_CFG8_TYPE1_SHIFT) & 0xffU;
		device->type2_screeners = (config >> GEM_CFG8_TYPE2_SHIFT) & 0xffU;
	}

	/* Finds the system clock the MDIO clock divides. */
	rate = clock_rate(RP1_CLK_SYS);
	if (rate == 0)
		rate = RP1_CLK_SYS_DEFAULT_HZ;

	/* Takes the smallest divider that keeps MDIO slow enough. */
	for (divider = 0; divider < GEM_MDC_DIVIDER_COUNT - 1U; divider++) {
		/* Stops at the first divider that is slow enough. */
		if (rate / gem_mdc_dividers[divider] <= GEM_MDC_MAX_HZ)
			break;
	}

	/* Keeps the divider for every reset of the controller. */
	device->mdc_divider = divider;

	/* Reports the controller. */
	kern_logf("gem: revision %08x, queues %04x, MDIO divider %u\n",
		  revision,
		  device->queue_mask,
		  gem_mdc_dividers[divider]);
}

/*
 * Allocates the descriptor rings (each with a spare descriptor for the
 * unused queues) and the receive and transmit slots, in memory the
 * controller reaches.
 */
static int
allocate_buffers(
	struct gem_device *device)
{
	struct drv_dma_constraints constraints;
	uint64_t bus_offset;
	uint64_t physical_limit;
	int coherent;
	int error;

	/* Reads the addresses RP1's peripherals use for memory. */
	error = drv_rp1_dma_window(&bus_offset, &physical_limit, &coherent);
	if (error != 0)
		return error;

	/* Describes the controller's DMA. */
	kern_memset(&constraints, 0, sizeof(constraints));
	constraints.address_bits = 64U;
	constraints.max_segment_size = GEM_RING_COUNT * GEM_PACKET_BYTES;
	constraints.segment_boundary = 0;
	constraints.coherent = coherent;
	error = drv_dma_device_create_window(&constraints, bus_offset, physical_limit, &device->dma);
	if (error != 0)
		return error;

	/* Allocates both rings, receive first. */
	error = drv_dma_alloc_coherent(device->dma, 2U * (GEM_RING_COUNT + 1U) * GEM_DESC_BYTES, GEM_RING_ALIGN, &device->rings);
	if (error != 0)
		return error;
	device->rx_descriptors = device->rings.address;
	device->tx_descriptors = device->rx_descriptors + (GEM_RING_COUNT + 1U) * GEM_DESC_WORDS;

	/* Allocates the receive slots. */
	error = drv_dma_alloc_coherent(device->dma, GEM_RING_COUNT * GEM_PACKET_BYTES, GEM_PACKET_ALIGN, &device->rx);
	if (error != 0)
		return error;

	/* Allocates the transmit slots. */
	error = drv_dma_alloc_coherent(device->dma, GEM_RING_COUNT * GEM_PACKET_BYTES, GEM_PACKET_ALIGN, &device->tx);
	if (error != 0)
		return error;

	/* Succeeded: the rings and the slots are ready. */
	return 0;
}

/*
 * Stops the controller, empties its queue bases and screeners, and turns
 * the MDIO bus on at its divider.
 *
 * Procedure referenced: OpenBSD sys/dev/fdt/if_cad.c rev 1.17
 * (cad_reset()).
 */
static void
reset_controller(
	struct gem_device *device)
{
	uint32_t netcfg;
	unsigned queue;
	unsigned index;

	/* Stops traffic, masks every interrupt and clears the status. */
	write_register(device, GEM_NETCTL, 0);
	write_register(device, GEM_IDR, 0xffffffffU);
	write_register(device, GEM_RXSR, 0);
	write_register(device, GEM_TXSR, 0);

	/* Empties queue 0's bases. */
	write_register(device, GEM_RXQBASEHI, 0);
	write_register(device, GEM_TXQBASEHI, 0);
	write_register(device, GEM_RXQBASE, 0);
	write_register(device, GEM_TXQBASE, 0);

	/* Empties the priority queues' bases. */
	for (queue = 1; queue < GEM_MAX_PRIQ; queue++) {
		/* Skips a queue the controller does not have. */
		if ((device->queue_mask & (1U << queue)) == 0)
			continue;

		/* Empties the queue's receive base, in its bank. */
		if (queue < 8U) {
			write_register(device, GEM_RXQ1BASE + (queue - 1U) * 4U, 0);
		} else {
			write_register(device, GEM_RXQ8BASE + (queue - 8U) * 4U, 0);
		}

		/* Empties the queue's transmit base. */
		write_register(device, GEM_TXQ1BASE + (queue - 1U) * 4U, 0);
	}

	/* Turns every screener off so all frames arrive on queue 0. */
	for (index = 0; index < device->type1_screeners; index++)
		write_register(device, GEM_SCR_TYPE1 + index * 4U, 0);
	for (index = 0; index < device->type2_screeners; index++)
		write_register(device, GEM_SCR_TYPE2 + index * 4U, 0);

	/* Sets the MDIO divider. */
	netcfg = read_register(device, GEM_NETCFG);
	netcfg &= ~GEM_NETCFG_MDCCLKDIV_MASK;
	netcfg |= device->mdc_divider << GEM_NETCFG_MDCCLKDIV_SHIFT;
	write_register(device, GEM_NETCFG, netcfg);

	/* Turns the MDIO bus on. */
	device->netctl = GEM_NETCTL_MDEN;
	write_register(device, GEM_NETCTL, device->netctl);
}

/*
 * Fills both rings: every receive descriptor owned by the controller and
 * pointing at its slot, every transmit descriptor owned by the driver,
 * and the spare descriptors closing the unused queues.  The caller holds
 * the lock with the controller stopped.
 */
static void
initialize_rings(
	struct gem_device *device)
{
	volatile uint32_t *descriptor;
	uint64_t address;
	uint32_t wrap;
	unsigned index;

	/* Points each receive descriptor at its slot, handed to the controller. */
	for (index = 0; index < GEM_RING_COUNT; index++) {
		descriptor = device->rx_descriptors + index * GEM_DESC_WORDS;
		address = device->rx.device_address + (uint64_t)index * GEM_PACKET_BYTES;
		wrap = 0;
		if (index == GEM_RING_COUNT - 1U)
			wrap = GEM_RXD_ADDR_WRAP;
		descriptor[GEM_DESC_ADDRESS_HIGH] = (uint32_t)(address >> 32);
		descriptor[GEM_DESC_STATUS] = 0;
		descriptor[3] = 0;
		descriptor[GEM_DESC_ADDRESS_LOW] = (uint32_t)address | wrap;
	}

	/* Closes the receive ring's spare descriptor for the unused queues. */
	descriptor = device->rx_descriptors + GEM_RING_COUNT * GEM_DESC_WORDS;
	descriptor[GEM_DESC_ADDRESS_HIGH] = 0;
	descriptor[GEM_DESC_STATUS] = 0;
	descriptor[3] = 0;
	descriptor[GEM_DESC_ADDRESS_LOW] = GEM_RXD_ADDR_USED | GEM_RXD_ADDR_WRAP;

	/* Marks each transmit descriptor as the driver's. */
	for (index = 0; index < GEM_RING_COUNT; index++) {
		descriptor = device->tx_descriptors + index * GEM_DESC_WORDS;
		wrap = 0;
		if (index == GEM_RING_COUNT - 1U)
			wrap = GEM_TXD_WRAP;
		descriptor[GEM_DESC_ADDRESS_LOW] = 0;
		descriptor[GEM_DESC_ADDRESS_HIGH] = 0;
		descriptor[3] = 0;
		descriptor[GEM_DESC_STATUS] = GEM_TXD_USED | wrap;
	}

	/* Closes the transmit ring's spare descriptor. */
	descriptor = device->tx_descriptors + GEM_RING_COUNT * GEM_DESC_WORDS;
	descriptor[GEM_DESC_ADDRESS_LOW] = 0;
	descriptor[GEM_DESC_ADDRESS_HIGH] = 0;
	descriptor[3] = 0;
	descriptor[GEM_DESC_STATUS] = GEM_TXD_USED | GEM_TXD_WRAP;

	/* Starts the software cursors at the start of the rings. */
	device->rx_next = 0;
	device->tx_producer = 0;
	device->tx_consumer = 0;

	/* Publishes the descriptors before the controller is given them. */
	kern_io_write_barrier();
}

/*
 * Gives the controller its rings, its address, its frame format and its
 * DMA settings, then starts receive and transmit.  The caller holds the
 * lock.
 *
 * Procedure referenced: OpenBSD sys/dev/fdt/if_cad.c rev 1.17 (cad_up():
 * the queue bases, the spare descriptor for the unused queues, the
 * address filter, NETCFG and DMACR, the statistics clear, then RXEN and
 * TXEN and the interrupt enables).
 */
static void
start_controller(
	struct gem_device *device)
{
	uint64_t rx_base;
	uint64_t tx_base;
	uint64_t rx_spare;
	uint64_t tx_spare;
	uint32_t netcfg;
	uint32_t dmacr;
	uint32_t low;
	uint32_t high;
	unsigned queue;
	unsigned index;

	/* Gives queue 0 its rings. */
	rx_base = device->rings.device_address;
	tx_base = rx_base + (uint64_t)(GEM_RING_COUNT + 1U) * GEM_DESC_BYTES;
	write_register(device, GEM_RXQBASEHI, (uint32_t)(rx_base >> 32));
	write_register(device, GEM_RXQBASE, (uint32_t)rx_base);
	write_register(device, GEM_TXQBASEHI, (uint32_t)(tx_base >> 32));
	write_register(device, GEM_TXQBASE, (uint32_t)tx_base);

	/* Points every priority queue at the spare descriptors and disables it. */
	rx_spare = rx_base + (uint64_t)GEM_RING_COUNT * GEM_DESC_BYTES;
	tx_spare = tx_base + (uint64_t)GEM_RING_COUNT * GEM_DESC_BYTES;
	for (queue = 1; queue < GEM_MAX_PRIQ; queue++) {
		/* Skips a queue the controller does not have. */
		if ((device->queue_mask & (1U << queue)) == 0)
			continue;

		/* Closes the queue's receive base, in its bank. */
		if (queue < 8U) {
			write_register(device, GEM_RXQ1BASE + (queue - 1U) * 4U, (uint32_t)rx_spare | GEM_QUEUE_BASE_DISABLE);
		} else {
			write_register(device, GEM_RXQ8BASE + (queue - 8U) * 4U, (uint32_t)rx_spare | GEM_QUEUE_BASE_DISABLE);
		}

		/* Closes the queue's transmit base. */
		write_register(device, GEM_TXQ1BASE + (queue - 1U) * 4U, (uint32_t)tx_spare | GEM_QUEUE_BASE_DISABLE);
	}

	/* Writes the station address into the first filter, the first octet in the low byte. */
	low = device->mac[0];
	low |= (uint32_t)device->mac[1] << 8;
	low |= (uint32_t)device->mac[2] << 16;
	low |= (uint32_t)device->mac[3] << 24;
	high = device->mac[4];
	high |= (uint32_t)device->mac[5] << 8;
	write_register(device, GEM_LADDRL, low);
	write_register(device, GEM_LADDRH, high);

	/* Empties the other filters. */
	for (index = 1; index < GEM_LADDRNUM; index++) {
		write_register(device, GEM_LADDRL + index * GEM_LADDR_STRIDE, 0);
		write_register(device, GEM_LADDRH + index * GEM_LADDR_STRIDE, 0);
	}

	/*
	 * Takes every frame, because the interface has no multicast list to
	 * filter by, and removes the FCS; broadcast is received, frames start
	 * at the slot's first byte, and the speed waits for the link.
	 */
	write_register(device, GEM_HASHL, 0xffffffffU);
	write_register(device, GEM_HASHH, 0xffffffffU);
	netcfg = read_register(device, GEM_NETCFG);
	netcfg |= GEM_NETCFG_FCSREM;
	netcfg |= GEM_NETCFG_1536RXEN;
	netcfg |= GEM_NETCFG_FDEN;
	netcfg |= GEM_NETCFG_COPYALL;
	netcfg &= ~GEM_NETCFG_RXCSUMEN;
	netcfg &= ~GEM_NETCFG_UCASTHASHEN;
	netcfg &= ~GEM_NETCFG_MCASTHASHEN;
	netcfg &= ~GEM_NETCFG_BCASTDI;
	netcfg &= ~GEM_NETCFG_RXOFFS_MASK;
	netcfg &= ~GEM_NETCFG_SGMIIEN;
	netcfg &= ~GEM_NETCFG_PCSSEL;
	write_register(device, GEM_NETCFG, netcfg);

	/*
	 * Sets 64-bit descriptors in the CPU's byte order, 2 KiB receive
	 * slots, full-size packet memories and 16-beat bursts, without
	 * checksum offload.
	 */
	dmacr = read_register(device, GEM_DMACR);
	dmacr |= GEM_DMACR_DMA64;
	dmacr &= ~GEM_DMACR_ES_DESCR;
	dmacr &= ~GEM_DMACR_ES_PDATA;
	dmacr &= ~GEM_DMACR_TXCSUMEN;
	dmacr |= GEM_DMACR_AHBDISC;
	dmacr |= GEM_DMACR_TXSIZE;
	dmacr &= ~GEM_DMACR_RXSIZE_MASK;
	dmacr |= GEM_DMACR_RXSIZE_8K;
	dmacr &= ~GEM_DMACR_RXBUF_MASK;
	dmacr |= (GEM_PACKET_BYTES / 64U) << GEM_DMACR_RXBUF_SHIFT;
	dmacr &= ~GEM_DMACR_BLEN_MASK;
	dmacr |= GEM_DMACR_BLEN_16;
	write_register(device, GEM_DMACR, dmacr);

	/* Clears the statistics. */
	write_register(device, GEM_NETCTL, device->netctl | GEM_NETCTL_STATCLR);

	/* Starts receive and transmit. */
	device->netctl |= GEM_NETCTL_RXEN;
	device->netctl |= GEM_NETCTL_TXEN;
	write_register(device, GEM_NETCTL, device->netctl);

	/* Clears what is pending and enables the interrupts the driver serves. */
	write_register(device, GEM_ISR, 0xffffffffU);
	write_register(device, GEM_IER, GEM_INTERRUPTS);
}

/*
 * Waits for the transmitter to go idle after transmit was turned off.
 * The caller holds the lock.
 */
static int
stop_transmitter(
	struct gem_device *device)
{
	/* Lets the frame on the wire finish. */
	return wait_register(device, GEM_TXSR, GEM_TXSR_TXGO, 0, GEM_TX_IDLE_TIMEOUT_US);
}

/*
 * Waits until the masked bits of a register read as wanted, spinning on
 * the monotonic counter.  It never yields: the callers hold the device
 * lock with interrupts off, and a thread that slept here would leave the
 * lock held while another thread on the same CPU spun on it for ever.
 */
static int
wait_register(
	struct gem_device *device,
	unsigned offset,
	uint32_t mask,
	uint32_t wanted,
	unsigned microseconds)
{
	uint64_t start;
	uint64_t frequency;
	uint64_t now;
	uint64_t current_frequency;
	uint64_t duration;
	unsigned reads;

	/* Fixes the deadline once, from one time base. */
	if (!kern_rtc_read_counter(&start, &frequency) || frequency == 0)
		return EIO;
	duration = frequency / 1000000U * microseconds;
	duration += (frequency % 1000000U * microseconds + 999999U) / 1000000U;

	/* Bounds the reads as well, in case the counter stops. */
	for (reads = 0; reads < GEM_WAIT_MAX_READS; reads++) {
		/* Stops once the register shows the wanted state. */
		if ((read_register(device, offset) & mask) == wanted)
			return 0;

		/* Refuses a changed or unavailable time base. */
		if (!kern_rtc_read_counter(&now, &current_frequency) || current_frequency != frequency)
			return EIO;

		/* Reports a register that kept its state past the deadline. */
		if (now - start >= duration)
			return ETIMEDOUT;
	}

	/* Reports a wait that ran out of reads. */
	return ETIMEDOUT;
}

/* Reads one controller register. */
static uint32_t
read_register(
	struct gem_device *device,
	unsigned offset)
{
	uint32_t bits;

	/* Reads it through the uncached mapping. */
	bits = kern_mmio_read32(device->registers + offset);

	/* Succeeded: the register's contents. */
	return bits;
}

/* Writes one controller register. */
static void
write_register(
	struct gem_device *device,
	unsigned offset,
	uint32_t bits)
{
	/* Writes it through the uncached mapping, in order with other device accesses. */
	kern_mmio_write32(device->registers + offset, bits);
}

/*
 * Waits for the MDIO bus to finish its operation without yielding: the
 * link check runs it under the device lock.
 */
static int
wait_mdio(
	struct gem_device *device)
{
	/* Waits for the idle bit. */
	return wait_register(device, GEM_NETSR, GEM_NETSR_PHY_MGMT_IDLE, GEM_NETSR_PHY_MGMT_IDLE, GEM_MDIO_TIMEOUT_US);
}

/*
 * Runs one clause-22 operation on the PHY's address.
 *
 * Procedure referenced: OpenBSD sys/dev/fdt/if_cad.c rev 1.17
 * (cad_mii_oper(): wait for idle, write the operation, wait for idle).
 */
static int
mdio_operate(
	struct gem_device *device,
	uint32_t operation)
{
	int error;

	/* Joins the operation before. */
	error = wait_mdio(device);
	if (error != 0)
		return error;

	/* Starts this one. */
	operation |= device->phy_address << GEM_PHYMNTNC_ADDR_SHIFT;
	operation |= GEM_PHYMNTNC_CLAUSE_22;
	operation |= GEM_PHYMNTNC_MUST_10;
	write_register(device, GEM_PHYMNTNC, operation);

	/* Waits for it to end. */
	error = wait_mdio(device);
	if (error != 0)
		return error;

	/* Succeeded: the operation ended. */
	return 0;
}

/* Reads one PHY register. */
static int
mdio_read(
	void *context,
	unsigned reg,
	uint16_t *data)
{
	struct gem_device *device;
	int error;

	/* Runs the read. */
	device = context;
	error = mdio_operate(device, GEM_PHYMNTNC_OP_READ | ((reg & 0x1fU) << GEM_PHYMNTNC_REG_SHIFT));
	if (error != 0)
		return error;

	/* Succeeded: the register's value. */
	*data = (uint16_t)(read_register(device, GEM_PHYMNTNC) & GEM_PHYMNTNC_DATA_MASK);
	return 0;
}

/* Writes one PHY register. */
static int
mdio_write(
	void *context,
	unsigned reg,
	uint16_t data)
{
	struct gem_device *device;
	int error;

	/* Runs the write. */
	device = context;
	error = mdio_operate(device, GEM_PHYMNTNC_OP_WRITE | ((reg & 0x1fU) << GEM_PHYMNTNC_REG_SHIFT) | data);
	if (error != 0)
		return error;

	/* Succeeded: the PHY holds the value. */
	return 0;
}

/* Opens the interface with fresh rings. */
static int
gem_open(
	struct net_device *net)
{
	struct gem_device *device;
	unsigned long irq;
	unsigned link;

	/* Serializes the start against the interrupt and the transmit path. */
	device = net->driver_data;
	irq = spin_lock_irqsave(&device->lock);

	/* A repeated open keeps the rings as they are. */
	if (device->opened != 0) {
		spin_unlock_irqrestore(&device->lock, irq);
		return 0;
	}

	/* Resets the controller, fills the rings and starts it. */
	reset_controller(device);
	initialize_rings(device);
	start_controller(device);

	/* Publishes opened once the rings belong to the controller, and reads the link. */
	device->opened = 1;
	device->link = 0;
	device->speed = 0;
	device->next_link_check = 0;
	update_link(device);
	link = device->link;

	spin_unlock_irqrestore(&device->lock, irq);

	/* Announces the carrier outside the lock. */
	(void)net_device_set_carrier(net, (int)link);
	kern_logf("gem: en0 RX/TX enabled\n");

	/* Succeeded: frames move by interrupt and the periodic service. */
	return 0;
}

/* Stops the interface: no interrupt, no transmit, the controller idle. */
static void
gem_close(
	struct net_device *net)
{
	struct gem_device *device;
	unsigned long irq;
	int error;

	/* Closes the producer gate and stops receive and transmit. */
	device = net->driver_data;
	irq = spin_lock_irqsave(&device->lock);

	device->opened = 0;
	device->link = 0;
	device->speed = 0;
	write_register(device, GEM_IDR, 0xffffffffU);
	device->netctl &= ~GEM_NETCTL_RXEN;
	device->netctl &= ~GEM_NETCTL_TXEN;
	write_register(device, GEM_NETCTL, device->netctl);
	error = stop_transmitter(device);
	write_register(device, GEM_ISR, 0xffffffffU);

	spin_unlock_irqrestore(&device->lock, irq);

	/* Withdraws the carrier and reports a transmitter that did not stop. */
	(void)net_device_set_carrier(net, 0);
	if (error != 0)
		kern_logf("gem: transmitter not idle at close (%d); slots kept\n", error);
}

/* Copies one frame into the next transmit slot and starts the transmitter. */
static int
gem_transmit(
	struct net_device *net,
	struct packet_buf *packet)
{
	struct gem_device *device;
	volatile uint32_t *descriptor;
	uint8_t *payload;
	uint64_t address;
	uint32_t status;
	unsigned long irq;
	unsigned slot;
	size_t length;
	int error;

	/* Refuses a missing packet. */
	if (packet == NULL)
		return EINVAL;

	/* Consumes a frame that is too short or too long for one slot. */
	length = packet->length;
	if (length < GEM_HEADER_BYTES || length > GEM_MAX_FRAME) {
		packet_buf_free(packet);
		return EMSGSIZE;
	}

	/* Serializes transmit with completion and close. */
	device = net->driver_data;
	error = 0;
	irq = spin_lock_irqsave(&device->lock);

	/* Takes back the slots the controller has sent. */
	if (device->opened != 0)
		reclaim_transmitted(device);

	/* Refuses a closed interface, no link, or a full ring (one slot always stays the driver's). */
	if (device->opened == 0 || device->link == 0) {
		error = ENETDOWN;
	} else if (device->tx_producer - device->tx_consumer >= GEM_RING_COUNT - 1U) {
		error = ENOBUFS;
	}

	/* Hands one complete frame to the controller. */
	if (error == 0) {
		/* Copies the frame and pads a short one with zeros. */
		slot = device->tx_producer % GEM_RING_COUNT;
		payload = (uint8_t *)device->tx.address + (size_t)slot * GEM_PACKET_BYTES;
		kern_memcpy(payload, packet->data, length);
		if (length < GEM_MIN_FRAME) {
			kern_memset(payload + length, 0, GEM_MIN_FRAME - length);
			length = GEM_MIN_FRAME;
		}

		/* Points the descriptor at the slot, then gives it to the controller in one write. */
		descriptor = device->tx_descriptors + slot * GEM_DESC_WORDS;
		address = device->tx.device_address + (uint64_t)slot * GEM_PACKET_BYTES;
		descriptor[GEM_DESC_ADDRESS_LOW] = (uint32_t)address;
		descriptor[GEM_DESC_ADDRESS_HIGH] = (uint32_t)(address >> 32);
		status = ((uint32_t)length & GEM_TXD_LEN_MASK) | GEM_TXD_LAST;
		if (slot == GEM_RING_COUNT - 1U)
			status |= GEM_TXD_WRAP;
		kern_io_write_barrier();
		descriptor[GEM_DESC_STATUS] = status;
		kern_io_write_barrier();

		/* Moves the producer and starts the transmitter. */
		device->tx_producer++;
		write_register(device, GEM_NETCTL, device->netctl | GEM_NETCTL_STARTTX);
	}

	spin_unlock_irqrestore(&device->lock, irq);

	/* Releases the stack's packet whatever happened. */
	packet_buf_free(packet);

	/* Reports a refused frame. */
	if (error != 0)
		return error;

	/* Succeeded: the slot holds the frame. */
	return 0;
}

/*
 * Takes back the transmit slots the controller marked as sent, counting
 * the frames it could not send.  The caller holds the lock.
 */
static void
reclaim_transmitted(
	struct gem_device *device)
{
	volatile uint32_t *descriptor;
	uint32_t status;
	unsigned slot;

	/* Walks the slots from the oldest in flight. */
	while (device->tx_consumer != device->tx_producer) {
		/* Stops at a slot the controller still owns. */
		slot = device->tx_consumer % GEM_RING_COUNT;
		descriptor = device->tx_descriptors + slot * GEM_DESC_WORDS;
		status = descriptor[GEM_DESC_STATUS];
		if ((status & GEM_TXD_USED) == 0)
			break;

		/* Counts a frame the controller gave up on. */
		if ((status & (GEM_TXD_RLIMIT | GEM_TXD_CORRUPT | GEM_TXD_LCOLL)) != 0)
			device->net->tx_errors++;

		/* Moves past the slot. */
		device->tx_consumer++;
	}
}

/*
 * Reads the PHY's link at a bounded rate and sets the controller's speed
 * and duplex.  The caller holds the lock.
 *
 * Procedure referenced: OpenBSD sys/dev/fdt/if_cad.c rev 1.17
 * (cad_mii_statchg(): full duplex, 100 for 100 Mbit/s, 100 and 1000 for
 * 1000 Mbit/s; RP1 sets the transmit clock itself).
 */
static void
update_link(
	struct gem_device *device)
{
	uint32_t netcfg;
	uint64_t now;
	unsigned speed;
	int error;

	/* Samples the PHY at most once per interval. */
	now = clock_ticks();
	if (device->next_link_check != 0 && now < device->next_link_check)
		return;
	device->next_link_check = now + KERN_MS_TO_TICKS(GEM_LINK_MS);

	/* Reads the negotiated speed. */
	error = drv_bcm54213pe_read_link(&device->phy, &speed);
	if (error != 0) {
		/* Reports a failing MDIO bus once, and stops traffic until it answers. */
		if (device->mdio_fault == 0)
			kern_logf("gem: PHY status failed (%d)\n", error);
		device->mdio_fault = 1;
		device->link = 0;
		device->speed = 0;
		device->net->link_mbps = 0;
		return;
	}

	/* The PHY answered: a later failure is reported again. */
	device->mdio_fault = 0;

	/* Reports a change of the link. */
	if (speed != device->speed) {
		/* Tells a pulled cable from a negotiated speed. */
		if (speed == 0) {
			kern_logf("gem: en0 link down\n");
		} else {
			kern_logf("gem: en0 link up %u Mbps full duplex\n", speed);
		}
	}

	/* Publishes the speed. */
	device->speed = speed;
	device->net->link_mbps = speed;
	device->link = 0;
	if (speed != 0)
		device->link = 1;

	/* Sets the controller's speed: 100 for 100 Mbit/s, both bits for gigabit. */
	netcfg = read_register(device, GEM_NETCFG);
	netcfg |= GEM_NETCFG_FDEN;
	netcfg &= ~GEM_NETCFG_100;
	netcfg &= ~GEM_NETCFG_1000;
	if (speed == 1000U) {
		netcfg |= GEM_NETCFG_100;
		netcfg |= GEM_NETCFG_1000;
	} else if (speed == 100U) {
		netcfg |= GEM_NETCFG_100;
	}

	/* Writes the speed and the duplex. */
	write_register(device, GEM_NETCFG, netcfg);
}

/* Delivers received frames within the network worker's budget. */
static unsigned
receive_packets(
	struct net_device *net,
	unsigned budget)
{
	struct gem_device *device;
	struct packet_buf *packet;
	volatile uint32_t *descriptor;
	uint64_t address;
	uint32_t address_low;
	uint32_t status;
	unsigned long irq;
	unsigned count;
	unsigned slot;
	size_t length;
	void *payload;
	int valid;

	/* Takes a finite batch so one interface cannot starve the stack. */
	device = net->driver_data;
	for (count = 0; count < budget; count++) {
		/* Takes a stack packet before the lock. */
		packet = packet_buf_alloc(PACKET_BUF_DEFAULT_HEADROOM);
		valid = 0;
		irq = spin_lock_irqsave(&device->lock);

		/* Stops at a slot the controller has not filled, or on a closed interface. */
		slot = device->rx_next;
		descriptor = device->rx_descriptors + slot * GEM_DESC_WORDS;
		address_low = descriptor[GEM_DESC_ADDRESS_LOW];
		if (device->opened == 0 || (address_low & GEM_RXD_ADDR_USED) == 0) {
			spin_unlock_irqrestore(&device->lock, irq);
			packet_buf_free(packet);
			break;
		}

		/* Reads the status only after the ownership. */
		kern_io_read_barrier();
		status = descriptor[GEM_DESC_STATUS];
		length = status & GEM_RXD_LEN_MASK;

		/* Copies a whole frame of a sensible length out of the slot. */
		if ((status & (GEM_RXD_SOF | GEM_RXD_EOF)) == (GEM_RXD_SOF | GEM_RXD_EOF) &&
		    length >= GEM_HEADER_BYTES &&
		    length <= GEM_MAX_FRAME) {
			/* Appends the frame to the stack's packet. */
			if (packet != NULL) {
				payload = packet_buf_append(packet, length);
				if (payload != NULL) {
					kern_memcpy(payload, (const uint8_t *)device->rx.address + (size_t)slot * GEM_PACKET_BYTES, length);
					valid = 1;
				}
			}
		} else {
			/* Counts a broken frame; its slot goes back all the same. */
			net->rx_errors++;
		}

		/* Counts a frame dropped for want of a packet. */
		if (valid == 0)
			net->rx_dropped++;

		/* Gives the slot back to the controller after the copy. */
		address = device->rx.device_address + (uint64_t)slot * GEM_PACKET_BYTES;
		descriptor[GEM_DESC_STATUS] = 0;
		kern_io_barrier();
		if (slot == GEM_RING_COUNT - 1U) {
			descriptor[GEM_DESC_ADDRESS_LOW] = (uint32_t)address | GEM_RXD_ADDR_WRAP;
		} else {
			descriptor[GEM_DESC_ADDRESS_LOW] = (uint32_t)address;
		}

		/* Moves to the next slot. */
		device->rx_next = (slot + 1U) % GEM_RING_COUNT;

		spin_unlock_irqrestore(&device->lock, irq);

		/* Hands the packet to the stack outside the lock. */
		if (valid != 0) {
			net_device_receive(net, packet);
		} else {
			packet_buf_free(packet);
		}
	}

	/* Succeeded: the slots consumed, broken and dropped frames included. */
	return count;
}

/* Serves the link, sent frames and received frames, then re-enables receive interrupts. */
static unsigned
gem_poll(
	struct net_device *net,
	unsigned budget)
{
	struct gem_device *device;
	unsigned long irq;
	unsigned link;
	unsigned count;

	/* Samples the link and takes back sent slots under the lock. */
	device = net->driver_data;
	irq = spin_lock_irqsave(&device->lock);

	if (device->opened != 0) {
		update_link(device);
		reclaim_transmitted(device);
	}

	/* Takes the link for the announcement outside the lock. */
	link = device->link;

	spin_unlock_irqrestore(&device->lock, irq);

	/* Announces the carrier, then delivers a batch of frames. */
	(void)net_device_set_carrier(net, (int)link);
	count = receive_packets(net, budget);

	/* Re-enables the receive interrupt after the batch. */
	irq = spin_lock_irqsave(&device->lock);

	if (device->opened != 0)
		write_register(device, GEM_IER, GEM_IXR_RXDONE);

	spin_unlock_irqrestore(&device->lock, irq);

	/* Succeeded: the frames handled. */
	return count;
}

/*
 * Serves the controller's interrupt: clears what it raised, holds receive
 * interrupts until the network worker has drained the ring, and stops the
 * interface on a DMA error.
 */
static void
gem_interrupt(
	void *argument)
{
	struct gem_device *device;
	unsigned long enabled;
	uint32_t pending;
	unsigned schedule;

	/* Reads and clears the raised causes. */
	device = argument;
	schedule = 0;
	enabled = spin_lock_irqsave(&device->lock);

	pending = read_register(device, GEM_ISR);
	write_register(device, GEM_ISR, pending);

	/* Holds receive interrupts until the worker drains a batch. */
	if (device->opened != 0 && (pending & GEM_IXR_RXDONE) != 0) {
		write_register(device, GEM_IDR, GEM_IXR_RXDONE);
		schedule = 1;
	}

	/* Has the worker take back sent slots. */
	if (device->opened != 0 && (pending & GEM_IXR_TXDONE) != 0)
		schedule = 1;

	/* Counts a frame lost to a full ring. */
	if ((pending & GEM_IXR_RXOVR) != 0 && device->net != NULL)
		device->net->rx_errors++;

	/* Stops the interface after a DMA error, to limit the damage. */
	if ((pending & GEM_IXR_HRESP) != 0) {
		device->netctl &= ~GEM_NETCTL_RXEN;
		device->netctl &= ~GEM_NETCTL_TXEN;
		write_register(device, GEM_NETCTL, device->netctl);
		write_register(device, GEM_IDR, 0xffffffffU);
		kern_logf("gem: DMA error, interface stopped\n");
	}

	spin_unlock_irqrestore(&device->lock, enabled);

	/* Wakes the network worker outside the lock. */
	if (schedule != 0)
		net_device_schedule_poll(device->net);
}

/* Starts the periodic service of the link and of missed interrupts. */
static void
start_polling(
	void)
{
	struct gem_device *device;
	int error;

	/* Leaves an absent interface or a running service alone. */
	device = &board_gem;
	if (device->net == NULL || device->poll_thread != NULL)
		return;

	/* Creates the service thread. */
	error = kthread_create(poll_worker, device, SCHED_PRIORITY_DEFAULT, &device->poll_thread);
	if (error != 0) {
		kern_logf("gem: periodic poll unavailable (%d)\n", error);
		return;
	}

	/* Holds the interface for the thread, which runs for as long as the kernel. */
	net_device_ref(device->net);

	/* Makes the thread runnable. */
	thread_start(device->poll_thread);
}

/* Schedules a service pass at the polling interval while the interface is open. */
static void
poll_worker(
	void *argument)
{
	struct gem_device *device;
	unsigned long irq;
	unsigned opened;
	uint64_t deadline;

	/* Serves the board's interface for as long as the kernel runs. */
	device = argument;
	for (;;) {
		/* Samples whether the interface is open. */
		irq = spin_lock_irqsave(&device->lock);

		opened = device->opened;

		spin_unlock_irqrestore(&device->lock, irq);

		/* Wakes the network worker while the interface has users. */
		if (opened != 0)
			net_device_schedule_poll(device->net);

		/* Sleeps on scheduler ticks until the next pass. */
		deadline = sched_ticks() + KERN_MS_TO_TICKS(GEM_POLL_MS);
		sched_sleep(deadline);
	}
}

/* Waits a number of milliseconds in thread context. */
static int
delay_milliseconds(
	unsigned milliseconds)
{
	unsigned step;

	/* Waits a millisecond at a time. */
	for (step = 0; step < milliseconds; step++)
		kern_usleep_range(1000U, 1000U);

	/* Succeeded: the time has passed. */
	return 0;
}
