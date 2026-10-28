/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Realtek RTL8125 family of PCI Express Ethernet controllers.
 *
 * The RTL8125 (2.5 Gbit/s), RTL8126 (5 Gbit/s) and RTL8127 (10 Gbit/s)
 * share one register layout: a memory BAR of byte, word and longword
 * registers, and behind two of them the windows through which the MAC's
 * and the PHY's internal registers (the OCP space) are read and written.
 * The chip's version is read from the transmit configuration register and
 * picks the per-chip steps.
 *
 * At attach the controller is taken out of the firmware's out-of-band
 * mode, reset with its interrupts masked, and published as a network
 * interface under the station address the chip holds.  The interface has
 * no carrier until the PHY has been brought up and the rings started.
 */

#include <drivers/ethernet/rtl8125.h>

#include <drivers/pci/pci.h>
#include <kern/clock.h>
#include <kern/device-io.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/lock.h>
#include <kern/net/net-device.h>
#include <kern/net/packet-buf.h>
#include <uapi/errno.h>

#include <stdbool.h>
#include <stdint.h>

/*
 * The PCI identities of the controllers.
 *
 * The RTL8127's was read from the device; the others follow the same
 * numbering of the parts.
 */
#define RTL_PCI_VENDOR_REALTEK		0x10ecU
#define RTL_PCI_DEVICE_8125		0x8125U
#define RTL_PCI_DEVICE_8126		0x8126U
#define RTL_PCI_DEVICE_8127		0x8127U

/* The memory BAR that holds the registers, and the smallest one the driver takes. */
#define RTL_REGISTER_BAR		2U
#define RTL_REGISTER_BAR_MINIMUM	0x10000U

/*
 * The controller's registers.
 *
 * Values referenced: OpenBSD sys/dev/pci/if_rgereg.h 1.20 (RGE_MAC0,
 * RGE_MAC4, RGE_CMD, RGE_IMR, RGE_ISR, RGE_TXCFG, RGE_RXCFG, RGE_EECMD,
 * RGE_PHYSTAT, RGE_MACOCP, RGE_PHYOCP, RGE_TWICMD, RGE_MCUCMD, RGE_IM,
 * RGE_PPSW, RGE_ADDR0, RGE_ADDR1).
 */
#define RTL_REG_STATION0		0x0000U
#define RTL_REG_STATION4		0x0004U
#define RTL_REG_COMMAND			0x0037U
#define RTL_REG_INTERRUPT_MASK		0x0038U
#define RTL_REG_INTERRUPT_STATUS	0x003cU
#define RTL_REG_TX_CONFIG		0x0040U
#define RTL_REG_RX_CONFIG		0x0044U
#define RTL_REG_CONFIG_LOCK		0x0050U
#define RTL_REG_PHY_STATUS		0x006cU
#define RTL_REG_MAC_OCP			0x00b0U
#define RTL_REG_PHY_OCP			0x00b8U
#define RTL_REG_TWI_COMMAND		0x00d2U
#define RTL_REG_MCU_COMMAND		0x00d3U
#define RTL_REG_IDLE_STATUS		0x00e2U
#define RTL_REG_POWER_SWITCH		0x00f2U
#define RTL_REG_PERMANENT0		0x19e0U
#define RTL_REG_PERMANENT4		0x19e4U

/*
 * The fields of those registers.
 *
 * Values referenced: OpenBSD sys/dev/pci/if_rgereg.h 1.20 (RGE_CMD_TXENB,
 * RGE_CMD_RXENB, RGE_CMD_RESET, RGE_CMD_STOPREQ, RGE_TXCFG_HWREV,
 * RGE_RXCFG_ALLPHYS, RGE_RXCFG_INDIV, RGE_RXCFG_MULTI, RGE_RXCFG_BROAD,
 * RGE_RXCFG_RUNT, RGE_RXCFG_ERRPKT, RGE_EECMD_WRITECFG,
 * RGE_MACOCP_DATA_MASK, RGE_MACOCP_BUSY, RGE_MACOCP_ADDR_SHIFT,
 * RGE_PHYOCP_DATA_MASK, RGE_PHYOCP_BUSY, RGE_PHYOCP_ADDR_SHIFT,
 * RGE_MCUCMD_RXFIFO_EMPTY, RGE_MCUCMD_TXFIFO_EMPTY, RGE_MCUCMD_IS_OOB).
 */
#define RTL_COMMAND_TX_ENABLE		0x04U
#define RTL_COMMAND_RX_ENABLE		0x08U
#define RTL_COMMAND_RESET		0x10U
#define RTL_COMMAND_STOP_REQUEST	0x80U
#define RTL_TX_CONFIG_VERSION_MASK	0x7cf00000U
#define RTL_RX_CONFIG_ACCEPT_MASK	0x0000003fU
#define RTL_CONFIG_LOCK_WRITABLE	0xc0U
#define RTL_OCP_DATA_MASK		0x0000ffffU
#define RTL_OCP_BUSY			0x80000000U
#define RTL_OCP_ADDRESS_SHIFT		16U
#define RTL_MCU_RX_FIFO_EMPTY		0x10U
#define RTL_MCU_TX_FIFO_EMPTY		0x20U
#define RTL_MCU_OUT_OF_BAND		0x80U

/*
 * The transmit configuration's version of each controller.
 *
 * Values referenced: OpenBSD sys/dev/pci/if_rge.c 1.48 (rge_attach, the
 * hardware revision switch).
 */
#define RTL_VERSION_8125		0x60900000U
#define RTL_VERSION_8125B		0x64100000U
#define RTL_VERSION_8126_1		0x64900000U
#define RTL_VERSION_8126_2		0x64a00000U
#define RTL_VERSION_8125D_1		0x68800000U
#define RTL_VERSION_8125D_2		0x68900000U
#define RTL_VERSION_8127		0x6c900000U

/*
 * The MAC OCP registers and values the out-of-band exit and the version
 * check use.
 *
 * Values referenced: OpenBSD sys/dev/pci/if_rge.c 1.48 (rge_exit_oob,
 * rge_attach: the RTL8127ATF check).
 */
#define RTL_MAC_OCP_REALWOW		0xc0bcU
#define RTL_MAC_OCP_REALWOW_OFF		0x00ffU
#define RTL_MAC_OCP_OOB_CONTROL		0xe8deU
#define RTL_MAC_OCP_OOB_CONTROL_BIT	0x4000U
#define RTL_MAC_OCP_HANDOFF_A		0xc0aaU
#define RTL_MAC_OCP_HANDOFF_A_VALUE	0x07d0U
#define RTL_MAC_OCP_HANDOFF_B		0xc0a6U
#define RTL_MAC_OCP_HANDOFF_B_VALUE	0x01b5U
#define RTL_MAC_OCP_HANDOFF_C		0xc01eU
#define RTL_MAC_OCP_HANDOFF_C_VALUE	0x5555U
#define RTL_MAC_OCP_UPS			0xd42cU
#define RTL_MAC_OCP_UPS_RESUME		0x0100U
#define RTL_MAC_OCP_PORT_TYPE		0xd006U
#define RTL_MAC_OCP_PORT_TYPE_FIBER	0x0007U
#define RTL_TWI_READY			0x0200U
#define RTL_IDLE_STATUS_READY		0x0103U
#define RTL_POWER_SWITCH_RX_GATE	0x08U

/*
 * The PHY OCP registers the out-of-band exit uses.
 *
 * Values referenced: OpenBSD sys/dev/pci/if_rge.c 1.48 (rge_exit_oob).
 */
#define RTL_PHY_OCP_STATE		0xa420U
#define RTL_PHY_OCP_STATE_MASK		0x0007U
#define RTL_PHY_OCP_STATE_HANDED_BACK	0x0002U
#define RTL_PHY_OCP_UPS_A		0xa466U
#define RTL_PHY_OCP_UPS_A_BIT		0x0001U
#define RTL_PHY_OCP_UPS_B		0xa468U
#define RTL_PHY_OCP_UPS_B_BITS		0x000aU

/*
 * The bounded waits, as a number of polls and the delay between two.
 *
 * Values referenced: OpenBSD sys/dev/pci/if_rge.c 1.48 (rge_reset,
 * rge_exit_oob, rge_write_phy_ocp, rge_read_phy_ocp) and
 * sys/dev/pci/if_rgereg.h 1.20 (RGE_TIMEOUT).
 */
#define RTL_OCP_POLLS			20000U
#define RTL_OCP_POLL_US			1U
#define RTL_TWI_POLLS			10U
#define RTL_TWI_POLL_US			100U
#define RTL_DRAIN_POLLS			3000U
#define RTL_DRAIN_POLL_US		50U
#define RTL_STOP_SETTLE_US		200U
#define RTL_RESET_POLLS			100U
#define RTL_RESET_POLL_US		100U
#define RTL_PHY_STATE_POLLS		100U
#define RTL_PHY_STATE_POLL_US		1000U

/*
 * The PCI Express capability's link control register and the power
 * management bits in it (PCI Express Base Specification, the Link Control
 * register: ASPM Control in bits 1:0, Enable Clock Power Management in
 * bit 8).
 */
#define RTL_PCI_CAPABILITY_EXPRESS	0x10U
#define RTL_PCI_EXPRESS_LINK_CONTROL	0x10U
#define RTL_PCI_LINK_ASPM_MASK		0x0003U
#define RTL_PCI_LINK_CLOCK_PM		0x0100U

/* The interface's MTU and how many controllers the driver serves. */
#define RTL_DEFAULT_MTU			1500U
#define RTL_MAX_DEVICES			4U

/* Marks a parameter that an interface passes but the callee does not need. */
#define UNUSED_PARAMETER(parameter)	((void)(parameter))

/* The members of the family whose steps differ. */
enum rtl_family {
	RTL_FAMILY_8125,
	RTL_FAMILY_8125B,
	RTL_FAMILY_8125D,
	RTL_FAMILY_8126,
	RTL_FAMILY_8127
};

/*
 * One version of the controller the driver knows.
 *
 * The version is the transmit configuration's masked value; the family
 * picks the steps, and the revision tells the two parts of one family
 * apart where their steps differ.
 */
struct rtl_chip {
	uint32_t version;
	enum rtl_family family;
	unsigned revision;
	const char *name;
};

/*
 * One attached controller.
 *
 * The lock serializes the register windows and every register write after
 * attach; the interrupt takes it too.  An attached controller is kept for
 * the life of the system.
 */
struct rtl_device {
	struct spinlock lock;
	struct drv_pci_device *pci;
	struct drv_pci_enable_state enable_state;
	struct drv_pci_mapping mapping;
	struct drv_pci_irq irq;
	void *irq_cookie;
	/*
	 * What attach has taken so far, which a failed attach gives back in
	 * the reverse order.
	 */
	unsigned bar_claimed;
	unsigned enable_saved;
	unsigned bar_mapped;
	volatile uint8_t *registers;
	const struct rtl_chip *chip;
	unsigned fiber;
	struct net_device *net;
	char name[NET_DEVICE_NAME_MAX];
	uint8_t mac[6];
};

/*
 * The versions the driver knows, in the order the parts appeared.
 *
 * Values referenced: OpenBSD sys/dev/pci/if_rge.c 1.48 (rge_attach, the
 * hardware revision switch).
 */
static const struct rtl_chip rtl_chips[] = {
	{ RTL_VERSION_8125, RTL_FAMILY_8125, 1U, "RTL8125" },
	{ RTL_VERSION_8125B, RTL_FAMILY_8125B, 1U, "RTL8125B" },
	{ RTL_VERSION_8126_1, RTL_FAMILY_8126, 1U, "RTL8126" },
	{ RTL_VERSION_8126_2, RTL_FAMILY_8126, 2U, "RTL8126" },
	{ RTL_VERSION_8125D_1, RTL_FAMILY_8125D, 1U, "RTL8125D" },
	{ RTL_VERSION_8125D_2, RTL_FAMILY_8125D, 2U, "RTL8125D" },
	{ RTL_VERSION_8127, RTL_FAMILY_8127, 1U, "RTL8127" },
};

/* The PCI identities the driver binds. */
static const struct drv_pci_id rtl_pci_ids[] = {
	{
		.vendor = RTL_PCI_VENDOR_REALTEK,
		.device = RTL_PCI_DEVICE_8125,
		.subvendor = DRV_PCI_ANY_ID,
		.subdevice = DRV_PCI_ANY_ID,
	},
	{
		.vendor = RTL_PCI_VENDOR_REALTEK,
		.device = RTL_PCI_DEVICE_8126,
		.subvendor = DRV_PCI_ANY_ID,
		.subdevice = DRV_PCI_ANY_ID,
	},
	{
		.vendor = RTL_PCI_VENDOR_REALTEK,
		.device = RTL_PCI_DEVICE_8127,
		.subvendor = DRV_PCI_ANY_ID,
		.subdevice = DRV_PCI_ANY_ID,
	},
};

static int rtl_attach(struct drv_pci_device *pci, const struct drv_pci_id *id);

/* The PCI driver of the family. */
static struct drv_pci_driver rtl_pci_driver = {
	.name = "rtl8125",
	.ids = rtl_pci_ids,
	.id_count = sizeof(rtl_pci_ids) / sizeof(rtl_pci_ids[0]),
	.attach = rtl_attach,
};

/*
 * The attached controllers.
 *
 * A slot whose pci is NULL is free.  Slots are taken by attach on the boot
 * thread and never given back once the interface is published.
 */
static struct rtl_device rtl_devices[RTL_MAX_DEVICES];

static int rtl_open(struct net_device *net);
static void rtl_close(struct net_device *net);
static int rtl_transmit(struct net_device *net, struct packet_buf *packet);
static unsigned rtl_poll(struct net_device *net, unsigned budget);

/* The network callbacks. */
static const struct net_device_ops rtl_operations = {
	rtl_open,
	rtl_close,
	rtl_transmit,
	rtl_poll,
	NULL,
	NULL,
	NULL
};

static uint8_t read8(const struct rtl_device *device, uint32_t offset);
static uint16_t read16(const struct rtl_device *device, uint32_t offset);
static uint32_t read32(const struct rtl_device *device, uint32_t offset);
static void write8(struct rtl_device *device, uint32_t offset, uint8_t value);
static void write32(struct rtl_device *device, uint32_t offset, uint32_t value);
static uint16_t mac_ocp_read(struct rtl_device *device, uint16_t reg);
static void mac_ocp_write(struct rtl_device *device, uint16_t reg, uint16_t value);
static void mac_ocp_clear(struct rtl_device *device, uint16_t reg, uint16_t bits);
static int phy_ocp_read(struct rtl_device *device, uint16_t reg, uint16_t *value);
static int phy_ocp_write(struct rtl_device *device, uint16_t reg, uint16_t value);
static int phy_ocp_clear(struct rtl_device *device, uint16_t reg, uint16_t bits);
static struct rtl_device *take_slot(void);
static void release_unpublished(struct rtl_device *device);
static int map_registers(struct rtl_device *device);
static int identify_chip(struct rtl_device *device);
static void disable_link_power_management(struct rtl_device *device);
static void mask_interrupts(struct rtl_device *device);
static int reset_controller(struct rtl_device *device);
static int exit_out_of_band(struct rtl_device *device);
static void wait_twi_ready(struct rtl_device *device);
static int hand_back_phy(struct rtl_device *device);
static void read_station_address(struct rtl_device *device);
static int setup_interrupt(struct rtl_device *device);
static int rtl_interrupt(void *argument);
static int publish_interface(struct rtl_device *device);

/*
 * Registers the driver for the RTL8125, RTL8126 and RTL8127 controllers.
 */
int
drv_pci_rtl8125_driver_register(
	void)
{
	int error;

	/* Offers the family's PCI identities to the bus. */
	error = drv_pci_driver_register(&rtl_pci_driver);
	if (error != 0)
		return error;

	/* Succeeded: the bus binds the controllers it finds. */
	return 0;
}

/*
 * Takes a controller out of the firmware's hands and publishes it.
 *
 * Procedure referenced: OpenBSD sys/dev/pci/if_rge.c 1.48 (rge_attach,
 * rge_chipinit, rge_hw_reset).
 */
static int
rtl_attach(
	struct drv_pci_device *pci,
	const struct drv_pci_id *id)
{
	struct rtl_device *device;
	int error;

	UNUSED_PARAMETER(id);

	/* Takes a free slot for the controller. */
	device = take_slot();
	if (device == NULL)
		return ENOSPC;
	device->pci = pci;
	spin_init(&device->lock, LOCK_RANK_DEVICE, "rtl8125");

	/* Maps the registers and turns on memory decoding and bus mastering. */
	error = map_registers(device);
	if (error != 0) {
		kern_logf("rtl8125: registers unavailable (%d)\n", error);
		release_unpublished(device);
		return error;
	}

	/* Learns which member of the family this is; an unknown one is left alone. */
	error = identify_chip(device);
	if (error != 0) {
		kern_logf("rtl8125: unknown version 0x%08x\n",
			  read32(device, RTL_REG_TX_CONFIG) & RTL_TX_CONFIG_VERSION_MASK);
		release_unpublished(device);
		return error;
	}

	/* Keeps the link out of the power states the controller mishandles. */
	disable_link_power_management(device);

	/* Masks the controller's interrupts before anything can raise one. */
	mask_interrupts(device);

	/* Takes the controller back from the firmware and resets it. */
	error = exit_out_of_band(device);
	if (error != 0) {
		kern_logf("rtl8125: %s did not leave out-of-band mode (%d)\n",
			  device->chip->name, error);
		release_unpublished(device);
		return error;
	}

	/* Masks the interrupts again, which the reset may have changed. */
	mask_interrupts(device);

	/* Takes the address the chip was given at manufacture. */
	read_station_address(device);

	/* Connects the controller's interrupt. */
	error = setup_interrupt(device);
	if (error != 0) {
		kern_logf("rtl8125: interrupt unavailable (%d)\n", error);
		release_unpublished(device);
		return error;
	}

	/* Publishes the interface; the interrupt stays connected but masked if it cannot be. */
	error = publish_interface(device);
	if (error != 0) {
		kern_logf("rtl8125: interface unavailable (%d)\n", error);
		return error;
	}

	/* Tells which part was found and under which address. */
	kern_logf("rtl8125: %s: %s%s rev %u, address %02x:%02x:%02x:%02x:%02x:%02x\n",
		  device->name,
		  device->chip->name,
		  device->fiber ? "ATF" : "",
		  device->chip->revision,
		  device->mac[0],
		  device->mac[1],
		  device->mac[2],
		  device->mac[3],
		  device->mac[4],
		  device->mac[5]);

	/* Keeps the controller with its PCI device. */
	(void)drv_pci_device_set_driver_data(pci, device);

	/* Succeeded: the controller is published. */
	return 0;
}

/* Reads one byte register. */
static uint8_t
read8(
	const struct rtl_device *device,
	uint32_t offset)
{
	uint8_t value;

	/* Reads it with device ordering. */
	value = kern_mmio_read8(device->registers + offset);

	/* Reports the register's contents. */
	return value;
}

/* Reads one word register. */
static uint16_t
read16(
	const struct rtl_device *device,
	uint32_t offset)
{
	uint16_t value;

	/* Reads it with device ordering. */
	value = kern_mmio_read16(device->registers + offset);

	/* Reports the register's contents. */
	return value;
}

/* Reads one longword register. */
static uint32_t
read32(
	const struct rtl_device *device,
	uint32_t offset)
{
	uint32_t value;

	/* Reads it with device ordering. */
	value = kern_mmio_read32(device->registers + offset);

	/* Reports the register's contents. */
	return value;
}

/* Writes one byte register. */
static void
write8(
	struct rtl_device *device,
	uint32_t offset,
	uint8_t value)
{
	/* Writes it after every earlier store. */
	kern_mmio_write8(device->registers + offset, value);
}

/* Writes one longword register. */
static void
write32(
	struct rtl_device *device,
	uint32_t offset,
	uint32_t value)
{
	/* Writes it after every earlier store. */
	kern_mmio_write32(device->registers + offset, value);
}

/*
 * Reads one register of the MAC's OCP space.
 *
 * The window takes the register's word address in its upper half and
 * returns the data in its lower half at once.
 *
 * Procedure referenced: OpenBSD sys/dev/pci/if_rge.c 1.48
 * (rge_read_mac_ocp).
 */
static uint16_t
mac_ocp_read(
	struct rtl_device *device,
	uint16_t reg)
{
	uint32_t window;

	/* Names the register. */
	window = (uint32_t)(reg >> 1) << RTL_OCP_ADDRESS_SHIFT;
	write32(device, RTL_REG_MAC_OCP, window);

	/* Reports what the window returned. */
	window = read32(device, RTL_REG_MAC_OCP);
	return (uint16_t)(window & RTL_OCP_DATA_MASK);
}

/*
 * Writes one register of the MAC's OCP space.
 *
 * Procedure referenced: OpenBSD sys/dev/pci/if_rge.c 1.48
 * (rge_write_mac_ocp).
 */
static void
mac_ocp_write(
	struct rtl_device *device,
	uint16_t reg,
	uint16_t value)
{
	uint32_t window;

	/* Names the register and the value with the write bit. */
	window = (uint32_t)(reg >> 1) << RTL_OCP_ADDRESS_SHIFT;
	window |= value;
	window |= RTL_OCP_BUSY;
	write32(device, RTL_REG_MAC_OCP, window);
}

/* Clears bits of one register of the MAC's OCP space. */
static void
mac_ocp_clear(
	struct rtl_device *device,
	uint16_t reg,
	uint16_t bits)
{
	uint16_t value;

	/* Writes the register back without the bits. */
	value = mac_ocp_read(device, reg);
	mac_ocp_write(device, reg, (uint16_t)(value & ~bits));
}

/*
 * Reads one register of the PHY's OCP space.
 *
 * The window sets its flag once the data has arrived.
 *
 * Procedure referenced: OpenBSD sys/dev/pci/if_rge.c 1.48
 * (rge_read_phy_ocp).
 */
static int
phy_ocp_read(
	struct rtl_device *device,
	uint16_t reg,
	uint16_t *value)
{
	uint32_t window;
	unsigned poll;

	/* Names the register. */
	window = (uint32_t)(reg >> 1) << RTL_OCP_ADDRESS_SHIFT;
	write32(device, RTL_REG_PHY_OCP, window);

	/* Waits for the PHY to answer. */
	for (poll = 0; poll < RTL_OCP_POLLS; poll++) {
		kern_usleep_range(RTL_OCP_POLL_US, RTL_OCP_POLL_US);

		/* The data is valid once the flag is set. */
		window = read32(device, RTL_REG_PHY_OCP);
		if ((window & RTL_OCP_BUSY) != 0) {
			*value = (uint16_t)(window & RTL_OCP_DATA_MASK);
			return 0;
		}
	}

	/* A PHY that never answers is reported. */
	return ETIMEDOUT;
}

/*
 * Writes one register of the PHY's OCP space.
 *
 * The window clears its flag once the PHY has taken the value.
 *
 * Procedure referenced: OpenBSD sys/dev/pci/if_rge.c 1.48
 * (rge_write_phy_ocp).
 */
static int
phy_ocp_write(
	struct rtl_device *device,
	uint16_t reg,
	uint16_t value)
{
	uint32_t window;
	unsigned poll;

	/* Names the register and the value with the write bit. */
	window = (uint32_t)(reg >> 1) << RTL_OCP_ADDRESS_SHIFT;
	window |= value;
	window |= RTL_OCP_BUSY;
	write32(device, RTL_REG_PHY_OCP, window);

	/* Waits for the PHY to take it. */
	for (poll = 0; poll < RTL_OCP_POLLS; poll++) {
		kern_usleep_range(RTL_OCP_POLL_US, RTL_OCP_POLL_US);

		/* The write is done once the flag is clear. */
		window = read32(device, RTL_REG_PHY_OCP);
		if ((window & RTL_OCP_BUSY) == 0)
			return 0;
	}

	/* A PHY that never takes the value is reported. */
	return ETIMEDOUT;
}

/* Clears bits of one register of the PHY's OCP space. */
static int
phy_ocp_clear(
	struct rtl_device *device,
	uint16_t reg,
	uint16_t bits)
{
	uint16_t value;
	int error;

	/* Reads the register. */
	error = phy_ocp_read(device, reg, &value);
	if (error != 0)
		return error;

	/* Writes it back without the bits. */
	error = phy_ocp_write(device, reg, (uint16_t)(value & ~bits));
	if (error != 0)
		return error;

	/* Succeeded: the bits are clear. */
	return 0;
}

/* Takes a free controller slot. */
static struct rtl_device *
take_slot(
	void)
{
	unsigned index;

	/* Looks for a slot no controller holds. */
	for (index = 0; index < RTL_MAX_DEVICES; index++) {
		/* A slot without a PCI device is free. */
		if (rtl_devices[index].pci == NULL)
			return &rtl_devices[index];
	}

	/* Every slot is taken. */
	return NULL;
}

/* Gives back what a failed attach took, in the reverse order, and frees the slot. */
static void
release_unpublished(
	struct rtl_device *device)
{
	/* Unmaps the registers. */
	if (device->bar_mapped) {
		drv_pci_device_unmap_bar(device->pci, &device->mapping);
		device->bar_mapped = 0U;
		device->registers = NULL;
	}

	/* Puts decoding and bus mastering back as the firmware left them. */
	if (device->enable_saved) {
		(void)drv_pci_device_restore_enable_state(device->pci, &device->enable_state);
		device->enable_saved = 0U;
	}

	/* Gives the BAR back to the bus. */
	if (device->bar_claimed) {
		drv_pci_device_release_bar(device->pci, RTL_REGISTER_BAR);
		device->bar_claimed = 0U;
	}

	/* Frees the slot. */
	device->pci = NULL;
}

/* Maps the register BAR uncached and enables decoding and bus mastering. */
static int
map_registers(
	struct rtl_device *device)
{
	struct drv_pci_bar bar;
	int error;

	/* Brings the function to full power, where its registers answer. */
	error = drv_pci_device_set_power_state(device->pci, DRV_PCI_D0);
	if (error != 0)
		return error;

	/* Claims the register BAR. */
	error = drv_pci_device_claim_bar(device->pci, RTL_REGISTER_BAR);
	if (error != 0)
		return error;
	device->bar_claimed = 1U;

	/* Reads where the BAR is and what it is. */
	error = drv_pci_device_bar(device->pci, RTL_REGISTER_BAR, &bar);
	if (error != 0)
		return error;

	/* Refuses a BAR that is not memory or too small for the registers. */
	if (bar.type != DRV_PCI_BAR_MEMORY32 && bar.type != DRV_PCI_BAR_MEMORY64)
		return ENODEV;
	if (bar.size < RTL_REGISTER_BAR_MINIMUM)
		return ENODEV;

	/* Saves the decoding and bus mastering the firmware left, for a failed attach. */
	error = drv_pci_device_save_enable_state(device->pci, &device->enable_state);
	if (error != 0)
		return error;
	device->enable_saved = 1U;

	/* Maps it uncached. */
	error = drv_pci_device_map_bar(device->pci,
				       RTL_REGISTER_BAR,
				       DRV_PCI_MAP_READ | DRV_PCI_MAP_WRITE | DRV_PCI_MAP_NOCACHE,
				       &device->mapping);
	if (error != 0)
		return error;
	device->bar_mapped = 1U;

	/* Refuses a mapping that does not cover the registers. */
	if (device->mapping.address == NULL || device->mapping.size < RTL_REGISTER_BAR_MINIMUM)
		return EIO;
	device->registers = device->mapping.address;

	/* Turns on memory decoding. */
	error = drv_pci_device_enable_memory(device->pci);
	if (error != 0)
		return error;

	/* Lets the controller reach memory. */
	error = drv_pci_device_set_bus_master(device->pci, true);
	if (error != 0)
		return error;

	/* Succeeded: the registers are reachable. */
	return 0;
}

/*
 * Finds the controller's version in the table.
 *
 * Procedure referenced: OpenBSD sys/dev/pci/if_rge.c 1.48 (rge_attach, the
 * hardware revision switch and the RTL8127ATF check).
 */
static int
identify_chip(
	struct rtl_device *device)
{
	uint32_t version;
	uint16_t port;
	unsigned index;

	/* Reads the version from the transmit configuration. */
	version = read32(device, RTL_REG_TX_CONFIG) & RTL_TX_CONFIG_VERSION_MASK;

	/* Looks it up. */
	device->chip = NULL;
	for (index = 0; index < sizeof(rtl_chips) / sizeof(rtl_chips[0]); index++) {
		/* The first entry with the version is the chip. */
		if (rtl_chips[index].version == version) {
			device->chip = &rtl_chips[index];
			break;
		}
	}

	/* A version the table does not have has no known steps. */
	if (device->chip == NULL)
		return ENODEV;

	/* Tells the RTL8127's SFP+ part from the copper one. */
	device->fiber = 0U;
	if (device->chip->family == RTL_FAMILY_8127) {
		port = mac_ocp_read(device, RTL_MAC_OCP_PORT_TYPE);
		if (port == RTL_MAC_OCP_PORT_TYPE_FIBER)
			device->fiber = 1U;
	}

	/* Succeeded: the chip's steps are known. */
	return 0;
}

/*
 * Turns off ASPM and clock power management on the controller's link.
 *
 * Procedure referenced: OpenBSD sys/dev/pci/if_rge.c 1.48 (rge_attach,
 * the PCI Express check).
 */
static void
disable_link_power_management(
	struct rtl_device *device)
{
	unsigned capability;
	uint16_t control;
	int error;

	/* Finds the PCI Express capability; a device without one has no link to set. */
	error = drv_pci_device_find_capability(device->pci, RTL_PCI_CAPABILITY_EXPRESS, &capability);
	if (error != 0)
		return;

	/* Reads the link control register. */
	error = drv_pci_device_config_read16(device->pci,
					     capability + RTL_PCI_EXPRESS_LINK_CONTROL,
					     &control);
	if (error != 0)
		return;

	/* Writes it back without ASPM and clock power management. */
	control &= (uint16_t)~(RTL_PCI_LINK_ASPM_MASK | RTL_PCI_LINK_CLOCK_PM);
	(void)drv_pci_device_config_write16(device->pci,
					    capability + RTL_PCI_EXPRESS_LINK_CONTROL,
					    control);
}

/*
 * Masks every interrupt source and acknowledges what is pending.
 *
 * Procedure referenced: OpenBSD sys/dev/pci/if_rge.c 1.48 (rge_hw_reset,
 * the single-vector case).
 */
static void
mask_interrupts(
	struct rtl_device *device)
{
	uint32_t status;

	/* Masks the sources. */
	write32(device, RTL_REG_INTERRUPT_MASK, 0U);

	/* Acknowledges whatever is already latched. */
	status = read32(device, RTL_REG_INTERRUPT_STATUS);
	write32(device, RTL_REG_INTERRUPT_STATUS, status);
}

/*
 * Stops the receiver and transmitter, lets the FIFOs drain and resets the
 * MAC.
 *
 * Procedure referenced: OpenBSD sys/dev/pci/if_rge.c 1.48 (rge_reset).
 */
static int
reset_controller(
	struct rtl_device *device)
{
	uint32_t config;
	uint16_t idle;
	uint8_t command;
	uint8_t mcu;
	unsigned poll;

	/* Stops accepting frames of every kind. */
	config = read32(device, RTL_REG_RX_CONFIG);
	write32(device, RTL_REG_RX_CONFIG, config & ~RTL_RX_CONFIG_ACCEPT_MASK);

	/* Gates the receive data path. */
	write8(device,
	       RTL_REG_POWER_SWITCH,
	       (uint8_t)(read8(device, RTL_REG_POWER_SWITCH) | RTL_POWER_SWITCH_RX_GATE));

	/* Asks the MAC to stop and gives it time to see the request. */
	write8(device,
	       RTL_REG_COMMAND,
	       (uint8_t)(read8(device, RTL_REG_COMMAND) | RTL_COMMAND_STOP_REQUEST));
	kern_usleep_range(RTL_STOP_SETTLE_US, RTL_STOP_SETTLE_US);

	/* Waits for both FIFOs to empty; a reset follows either way. */
	for (poll = 0; poll < RTL_DRAIN_POLLS; poll++) {
		kern_usleep_range(RTL_DRAIN_POLL_US, RTL_DRAIN_POLL_US);

		/* The FIFOs are drained once both flags are set. */
		mcu = read8(device, RTL_REG_MCU_COMMAND);
		if ((mcu & (RTL_MCU_RX_FIFO_EMPTY | RTL_MCU_TX_FIFO_EMPTY)) ==
		    (RTL_MCU_RX_FIFO_EMPTY | RTL_MCU_TX_FIFO_EMPTY))
			break;
	}

	/* Waits for the later parts' DMA to go idle as well. */
	if (device->chip->family != RTL_FAMILY_8125) {
		for (poll = 0; poll < RTL_DRAIN_POLLS; poll++) {
			kern_usleep_range(RTL_DRAIN_POLL_US, RTL_DRAIN_POLL_US);

			/* The DMA is idle once every idle flag is set. */
			idle = read16(device, RTL_REG_IDLE_STATUS);
			if ((idle & RTL_IDLE_STATUS_READY) == RTL_IDLE_STATUS_READY)
				break;
		}
	}

	/* Drops every command bit but the two enables, then resets. */
	command = read8(device, RTL_REG_COMMAND);
	write8(device, RTL_REG_COMMAND, (uint8_t)(command & (RTL_COMMAND_TX_ENABLE | RTL_COMMAND_RX_ENABLE)));
	write8(device, RTL_REG_COMMAND, RTL_COMMAND_RESET);

	/* Waits for the reset to finish. */
	for (poll = 0; poll < RTL_RESET_POLLS; poll++) {
		kern_usleep_range(RTL_RESET_POLL_US, RTL_RESET_POLL_US);

		/* The MAC is out of reset once the bit clears. */
		command = read8(device, RTL_REG_COMMAND);
		if ((command & RTL_COMMAND_RESET) == 0)
			return 0;
	}

	/* A MAC that stays in reset is reported. */
	return ETIMEDOUT;
}

/*
 * Takes the controller from the firmware's out-of-band management.
 *
 * Procedure referenced: OpenBSD sys/dev/pci/if_rge.c 1.48 (rge_exit_oob).
 */
static int
exit_out_of_band(
	struct rtl_device *device)
{
	uint16_t ups;
	uint8_t mcu;
	int error;

	/* Turns the firmware's wake-on-LAN agent off. */
	mac_ocp_write(device, RTL_MAC_OCP_REALWOW, RTL_MAC_OCP_REALWOW_OFF);

	/* Resets the MAC. */
	error = reset_controller(device);
	if (error != 0)
		return error;

	/* Leaves out-of-band mode. */
	mcu = read8(device, RTL_REG_MCU_COMMAND);
	write8(device, RTL_REG_MCU_COMMAND, (uint8_t)(mcu & ~RTL_MCU_OUT_OF_BAND));
	mac_ocp_clear(device, RTL_MAC_OCP_OOB_CONTROL, RTL_MAC_OCP_OOB_CONTROL_BIT);
	wait_twi_ready(device);

	/* Hands the MAC's management back to the driver. */
	mac_ocp_write(device, RTL_MAC_OCP_HANDOFF_A, RTL_MAC_OCP_HANDOFF_A_VALUE);
	mac_ocp_write(device, RTL_MAC_OCP_HANDOFF_B, RTL_MAC_OCP_HANDOFF_B_VALUE);
	mac_ocp_write(device, RTL_MAC_OCP_HANDOFF_C, RTL_MAC_OCP_HANDOFF_C_VALUE);
	wait_twi_ready(device);

	/* Brings the PHY back from the firmware's power saving when it is there. */
	ups = mac_ocp_read(device, RTL_MAC_OCP_UPS);
	if ((ups & RTL_MAC_OCP_UPS_RESUME) != 0) {
		error = hand_back_phy(device);
		if (error != 0)
			return error;
	}

	/* Succeeded: the driver owns the controller. */
	return 0;
}

/* Gives the MAC's two-wire interface a bounded time to report ready. */
static void
wait_twi_ready(
	struct rtl_device *device)
{
	uint16_t status;
	unsigned poll;

	/* Polls the ready flag; the steps after it run either way. */
	for (poll = 0; poll < RTL_TWI_POLLS; poll++) {
		kern_usleep_range(RTL_TWI_POLL_US, RTL_TWI_POLL_US);

		/* The interface is ready once the flag is set. */
		status = read16(device, RTL_REG_TWI_COMMAND);
		if ((status & RTL_TWI_READY) != 0)
			break;
	}
}

/*
 * Waits for the PHY to leave the firmware's power saving and clears the
 * marks it left.
 *
 * Procedure referenced: OpenBSD sys/dev/pci/if_rge.c 1.48 (rge_exit_oob).
 */
static int
hand_back_phy(
	struct rtl_device *device)
{
	uint16_t state;
	unsigned poll;
	int error;

	/* Waits for the PHY to report it is handed back; the clears follow either way. */
	for (poll = 0; poll < RTL_PHY_STATE_POLLS; poll++) {
		/* A PHY that does not answer ends the wait. */
		error = phy_ocp_read(device, RTL_PHY_OCP_STATE, &state);
		if (error != 0)
			return error;

		/* The PHY is back once its state says so. */
		if ((state & RTL_PHY_OCP_STATE_MASK) == RTL_PHY_OCP_STATE_HANDED_BACK)
			break;

		kern_usleep_range(RTL_PHY_STATE_POLL_US, RTL_PHY_STATE_POLL_US);
	}

	/* Clears the MAC's resume mark. */
	mac_ocp_clear(device, RTL_MAC_OCP_UPS, RTL_MAC_OCP_UPS_RESUME);

	/* Clears the PHY's marks; the first RTL8125 has only the second. */
	if (device->chip->family != RTL_FAMILY_8125) {
		error = phy_ocp_clear(device, RTL_PHY_OCP_UPS_A, RTL_PHY_OCP_UPS_A_BIT);
		if (error != 0)
			return error;
	}

	/* Clears the mark every member has. */
	error = phy_ocp_clear(device, RTL_PHY_OCP_UPS_B, RTL_PHY_OCP_UPS_B_BITS);
	if (error != 0)
		return error;

	/* Succeeded: the PHY is the driver's. */
	return 0;
}

/*
 * Reads the station address the chip was given at manufacture and makes it
 * the one the MAC filters on.
 *
 * Procedure referenced: OpenBSD sys/dev/pci/if_rge.c 1.48
 * (rge_get_macaddr, rge_set_macaddr).
 */
static void
read_station_address(
	struct rtl_device *device)
{
	uint32_t low;
	uint32_t high;
	uint8_t lock;

	/* Reads the permanent address, low byte first. */
	low = read32(device, RTL_REG_PERMANENT0);
	high = read32(device, RTL_REG_PERMANENT4);
	device->mac[0] = (uint8_t)low;
	device->mac[1] = (uint8_t)(low >> 8);
	device->mac[2] = (uint8_t)(low >> 16);
	device->mac[3] = (uint8_t)(low >> 24);
	device->mac[4] = (uint8_t)high;
	device->mac[5] = (uint8_t)(high >> 8);

	/* Opens the configuration registers for writing. */
	lock = read8(device, RTL_REG_CONFIG_LOCK);
	write8(device, RTL_REG_CONFIG_LOCK, (uint8_t)(lock | RTL_CONFIG_LOCK_WRITABLE));

	/* Writes the address into the station registers. */
	write32(device, RTL_REG_STATION0, low);
	write32(device, RTL_REG_STATION4, high & 0xffffU);

	/* Locks the configuration registers again. */
	lock = read8(device, RTL_REG_CONFIG_LOCK);
	write8(device, RTL_REG_CONFIG_LOCK, (uint8_t)(lock & ~RTL_CONFIG_LOCK_WRITABLE));
}

/* Allocates one interrupt vector and connects the handler to it. */
static int
setup_interrupt(
	struct rtl_device *device)
{
	unsigned count;
	int error;

	/* Takes one vector, preferring MSI-X, then MSI, then the pin. */
	error = drv_pci_device_allocate_irqs(device->pci,
					     DRV_PCI_IRQ_ALLOW_MSIX | DRV_PCI_IRQ_ALLOW_MSI | DRV_PCI_IRQ_ALLOW_INTX,
					     1U,
					     1U,
					     &device->irq,
					     &count);
	if (error != 0)
		return error;

	/* Refuses an allocation that gave no vector. */
	if (count != 1U) {
		if (count != 0U)
			drv_pci_device_free_irqs(device->pci, &device->irq, count);
		return EIO;
	}

	/* Connects the handler. */
	error = drv_pci_device_establish_irq(device->pci,
					     &device->irq,
					     rtl_interrupt,
					     device,
					     "rtl8125",
					     &device->irq_cookie);
	if (error != 0) {
		drv_pci_device_free_irqs(device->pci, &device->irq, 1U);
		return error;
	}

	/* Succeeded: the vector reaches the handler. */
	return 0;
}

/*
 * Acknowledges the controller's interrupt.
 *
 * Runs in interrupt context.  A shared pin may call it for another
 * device's interrupt, so it reports whether the controller had one.
 */
static int
rtl_interrupt(
	void *argument)
{
	struct rtl_device *device;
	unsigned long irq;
	uint32_t status;

	/* Samples and acknowledges what the controller latched. */
	device = argument;
	irq = spin_lock_irqsave(&device->lock);

	status = read32(device, RTL_REG_INTERRUPT_STATUS);

	/* Writes the latched bits back to clear them; all ones is a device gone from the bus. */
	if (status != 0U && status != 0xffffffffU)
		write32(device, RTL_REG_INTERRUPT_STATUS, status);

	spin_unlock_irqrestore(&device->lock, irq);

	/* Nothing latched means the interrupt was another device's. */
	if (status == 0U || status == 0xffffffffU)
		return 0;

	/* The interrupt was the controller's. */
	return 1;
}

/* Publishes the controller as a network interface without a carrier. */
static int
publish_interface(
	struct rtl_device *device)
{
	unsigned index;
	int error;

	/* Names it after its slot. */
	index = (unsigned)(device - rtl_devices);
	device->name[0] = 'r';
	device->name[1] = 'g';
	device->name[2] = 'e';
	device->name[3] = (char)('0' + index);
	device->name[4] = '\0';

	/* Takes an interface slot. */
	device->net = net_device_alloc();
	if (device->net == NULL)
		return ENOMEM;

	/* Describes an Ethernet port with the controller's address. */
	kern_memcpy(device->net->name, device->name, 5U);
	device->net->mtu = RTL_DEFAULT_MTU;
	device->net->mtu_max = RTL_DEFAULT_MTU;
	device->net->hwaddr_len = 6U;
	kern_memcpy(device->net->hwaddr, device->mac, 6U);
	device->net->flags = NET_DEVICE_BROADCAST | NET_DEVICE_MULTICAST;
	device->net->ops = &rtl_operations;
	device->net->driver_data = device;

	/* Publishes it. */
	error = net_device_create(device->net);
	if (error != 0)
		return error;

	/* Succeeded: the interface exists. */
	return 0;
}

/* Opens the interface, which keeps no carrier while the link is not brought up. */
static int
rtl_open(
	struct net_device *net)
{
	UNUSED_PARAMETER(net);

	/* Succeeded: the interface is open without a carrier. */
	return 0;
}

/* Closes the interface, which has no I/O in flight. */
static void
rtl_close(
	struct net_device *net)
{
	UNUSED_PARAMETER(net);
}

/* Drops a frame, which has no link to go out on. */
static int
rtl_transmit(
	struct net_device *net,
	struct packet_buf *packet)
{
	UNUSED_PARAMETER(net);

	/* The packet is consumed either way. */
	packet_buf_free(packet);

	/* Reports that the link is down. */
	return ENETDOWN;
}

/* Reports that no frame has arrived, as the receiver is not running. */
static unsigned
rtl_poll(
	struct net_device *net,
	unsigned budget)
{
	UNUSED_PARAMETER(net);
	UNUSED_PARAMETER(budget);

	/* Nothing was received. */
	return 0;
}
