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
#include <kern/sched.h>
#include <kern/thread.h>
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
 * RGE_PPSW, RGE_ADDR0, RGE_ADDR1, RGE_INT_CFG0, RGE_TIMERINT0..3, RGE_CFG2,
 * RGE_CFG5, RGE_CSIDR, RGE_CSIAR, RGE_PMCH, RGE_EPHYAR; 0x00f1 from
 * rge_hw_init).
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
#define RTL_REG_INTERRUPT_CONFIG0	0x0034U
#define RTL_REG_TIMER_INTERRUPT0	0x0058U
#define RTL_REG_TIMER_INTERRUPT1	0x005cU
#define RTL_REG_TIMER_INTERRUPT2	0x008cU
#define RTL_REG_TIMER_INTERRUPT3	0x00f4U
#define RTL_REG_CONFIG2			0x0053U
#define RTL_REG_CONFIG5			0x0056U
#define RTL_REG_CSI_DATA		0x0064U
#define RTL_REG_CSI_ADDRESS		0x0068U
#define RTL_REG_POWER_CONTROL		0x006fU
#define RTL_REG_EPHY			0x0080U
#define RTL_REG_MISC_F1			0x00f1U
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
 * The fields of the configuration, CSI and EPHY windows and of the PHY
 * status.
 *
 * Values referenced: OpenBSD sys/dev/pci/if_rgereg.h 1.20
 * (RGE_CFG2_CLKREQ_EN, RGE_CFG5_PME_STS, RGE_CSIAR_BYTE_EN,
 * RGE_CSIAR_BYTE_EN_SHIFT, RGE_CSIAR_ADDR_MASK, RGE_CSIAR_BUSY,
 * RGE_EPHYAR_DATA_MASK, RGE_EPHYAR_BUSY, RGE_EPHYAR_ADDR_MASK,
 * RGE_EPHYAR_ADDR_SHIFT, RGE_EPHYAR_EXT_ADDR, RGE_PHYSTAT_*); sys/dev/pci/if_rge.c
 * 1.48 (rge_disable_aspm_clkreq: INT_CFG0 bit 0x08; rge_hw_init: 0x00f1
 * bit 0x80; rge_set_phy_power: PMCH 0xc0).
 */
#define RTL_CONFIG2_CLKREQ		0x80U
#define RTL_CONFIG5_PME_STATUS		0x01U
#define RTL_INTERRUPT_CONFIG0_CLKREQ	0x08U
#define RTL_MISC_F1_BIT			0x80U
#define RTL_POWER_CONTROL_PHY_ON	0xc0U
#define RTL_CSI_BYTE_ENABLE		0x0000f000U
#define RTL_CSI_ADDRESS_MASK		0x00000fffU
#define RTL_CSI_BUSY			0x80000000U
#define RTL_EPHY_DATA_MASK		0x0000ffffU
#define RTL_EPHY_BUSY			0x80000000U
#define RTL_EPHY_ADDRESS_MASK		0x0000007fU
#define RTL_EPHY_ADDRESS_SHIFT		16U
#define RTL_EPHY_EXTENDED_ADDRESS	0x0ffeU
#define RTL_PHY_STATUS_FULL_DUPLEX	0x0001U
#define RTL_PHY_STATUS_LINK		0x0002U
#define RTL_PHY_STATUS_10		0x0004U
#define RTL_PHY_STATUS_100		0x0008U
#define RTL_PHY_STATUS_1000		0x0010U
#define RTL_PHY_STATUS_2500		0x0400U
#define RTL_PHY_STATUS_5000		0x1000U
#define RTL_PHY_STATUS_10000		0x4000U

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
 * The clause-22 registers of the PHY and their bits (IEEE 802.3 clause 22:
 * the control, auto-negotiation advertisement and 1000BASE-T control
 * registers), and where the PHY's OCP space holds them: register r at
 * 0xa400 + 2r.
 *
 * Values referenced: OpenBSD sys/dev/pci/if_rgereg.h 1.20 (RGE_PHYBASE) and
 * sys/dev/pci/if_rge.c 1.48 (rge_write_phy).
 */
#define RTL_PHY_OCP_MII_BASE		0xa400U
#define RTL_MII_CONTROL			0U
#define RTL_MII_ADVERTISE		4U
#define RTL_MII_1000_CONTROL		9U
#define RTL_MII_CONTROL_RESET		0x8000U
#define RTL_MII_CONTROL_AUTONEG		0x1000U
#define RTL_MII_CONTROL_POWER_DOWN	0x0800U
#define RTL_MII_CONTROL_RESTART		0x0200U
#define RTL_MII_ADVERTISE_10		0x0020U
#define RTL_MII_ADVERTISE_10_FULL	0x0040U
#define RTL_MII_ADVERTISE_100		0x0080U
#define RTL_MII_ADVERTISE_100_FULL	0x0100U
#define RTL_MII_ADVERTISE_PAUSE		0x0400U
#define RTL_MII_ADVERTISE_PAUSE_ASYM	0x0800U
#define RTL_MII_1000_FULL		0x0200U
#define RTL_MII_1000_HALF		0x0100U

/*
 * The PHY OCP registers the PHY's power, reset, EEE and multi-gigabit
 * advertisement use.
 *
 * Values referenced: OpenBSD sys/dev/pci/if_rge.c 1.48 (rge_set_phy_power,
 * rge_phy_config, rge_ifmedia_upd) and sys/dev/pci/if_rgereg.h 1.20
 * (RGE_ADV_2500TFDX, RGE_ADV_5000TFDX, RGE_ADV_10000TFDX).
 */
#define RTL_PHY_OCP_STATE_READY		0x0003U
#define RTL_PHY_OCP_MULTIGIG		0xa5d4U
#define RTL_PHY_OCP_MULTIGIG_2500	0x0080U
#define RTL_PHY_OCP_MULTIGIG_5000	0x0100U
#define RTL_PHY_OCP_MULTIGIG_10000	0x1000U
#define RTL_PHY_OCP_INDEX		0xa436U
#define RTL_PHY_OCP_DATA		0xa438U
#define RTL_PHY_OCP_RAM_CODE_VERSION	0x801eU

/*
 * The MAC OCP registers the hardware set-up uses.
 *
 * Values referenced: OpenBSD sys/dev/pci/if_rge.c 1.48 (rge_hw_init,
 * rge_phy_config: the EEE registers).
 */
#define RTL_MAC_OCP_UPS_CONTROL		0xd40aU
#define RTL_MAC_OCP_UPS_CONTROL_BIT	0x0010U
#define RTL_MAC_OCP_MCU_ENABLE		0xfc26U
#define RTL_MAC_OCP_MCU_BREAK_FIRST	0xfc28U
#define RTL_MAC_OCP_MCU_BREAK_ENABLE	0xfc48U
#define RTL_MAC_OCP_EEE			0xe040U
#define RTL_MAC_OCP_EEE_BITS		0x0003U
#define RTL_MAC_OCP_EEE_ADVANCED	0xe052U
#define RTL_MAC_OCP_EEE_ADVANCED_BIT	0x0001U
#define RTL_CSI_UNCORRECTABLE		0x0108U
#define RTL_CSI_UNCORRECTABLE_BIT	0x00100000U

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
#define RTL_PHY_RESET_POLLS		2500U
#define RTL_PHY_RESET_POLL_MS		1U
#define RTL_CSI_SETTLE_US		20U
#define RTL_MCU_STOP_US			3000U
#define RTL_LINK_MS			500U

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

/* What one step of a PHY register sequence does. */
enum rtl_phy_step_kind {
	RTL_PHY_STEP_WRITE,
	RTL_PHY_STEP_MODIFY,
	RTL_PHY_STEP_READ
};

/*
 * One step of a PHY register sequence.
 *
 * A write stores value; a modify clears the mask bits and sets the value
 * bits of what the register holds; a read only reads, for a register whose
 * read has an effect.
 */
struct rtl_phy_step {
	enum rtl_phy_step_kind kind;
	uint16_t reg;
	uint16_t mask;
	uint16_t value;
};

/* One write of a PCI Express PHY (EPHY) table. */
struct rtl_ephy_write {
	uint16_t reg;
	uint16_t value;
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
	/*
	 * Whether the interface is open, and the link as last announced.
	 * Guarded by the lock; the link thread reads the PHY status only
	 * while the interface is open.
	 */
	unsigned opened;
	unsigned carrier;
	uint32_t link_mbps;
	struct thread *link_thread;
	uint16_t ram_code_version;
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

/*
 * The RTL8127's PCI Express PHY settings.
 *
 * Values referenced: OpenBSD sys/dev/pci/if_rgereg.h 1.20 (mac_r27_ephy).
 */
static const struct rtl_ephy_write rtl_ephy_8127[] = {
	{ 0x8088, 0x0064 }, { 0x8488, 0x0064 }, { 0x8888, 0x0064 },
	{ 0x8c88, 0x0064 }, { 0x8188, 0x0064 }, { 0x8588, 0x0064 },
	{ 0x8988, 0x0064 }, { 0x8d88, 0x0064 }, { 0x808c, 0x09b0 },
	{ 0x848c, 0x09b0 }, { 0x888c, 0x0f90 }, { 0x8c8c, 0x0f90 },
	{ 0x818c, 0x09b0 }, { 0x858c, 0x09b0 }, { 0x898c, 0x0f90 },
	{ 0x8d8c, 0x0f90 }, { 0x808a, 0x09b8 }, { 0x848a, 0x09b8 },
	{ 0x888a, 0x0f98 }, { 0x8c8a, 0x0f98 }, { 0x818a, 0x09b8 },
	{ 0x858a, 0x09b8 }, { 0x898a, 0x0f98 }, { 0x8d8a, 0x0f98 },
	{ 0x9020, 0x0080 }, { 0x9420, 0x0080 }, { 0x9820, 0x0080 },
	{ 0x9c20, 0x0080 }, { 0x901e, 0x0190 }, { 0x941e, 0x0190 },
	{ 0x981e, 0x0140 }, { 0x9c1e, 0x0140 }, { 0x901c, 0x0190 },
	{ 0x941c, 0x0190 }, { 0x981c, 0x0140 }, { 0x9c1c, 0x0140 }
};

/*
 * The RTL8127's PHY settings after its reset, in order, without the PHY
 * microcode patch that the reference loads first.
 *
 * Procedure and values referenced: OpenBSD sys/dev/pci/if_rge.c 1.48
 * (rge_phy_config_mac_r27, with its mac_cfg_value, mac_cfg2_value and
 * mac_cfg_a438_value tables written out in place).
 */
static const struct rtl_phy_step rtl_phy_8127[] = {
	{ RTL_PHY_STEP_WRITE, 0xa4d2, 0x0000, 0x0000 },
	{ RTL_PHY_STEP_READ, 0xa4d4, 0x0000, 0x0000 },
	{ RTL_PHY_STEP_MODIFY, 0xa442, 0x0800, 0x0000 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x8415 },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0xff00, 0x9300 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x81a3 },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0xff00, 0x0f00 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x81ae },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0xff00, 0x0f00 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x81b9 },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0xff00, 0xb900 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x83b0 },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0x0e00, 0x0000 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x83c5 },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0x0e00, 0x0000 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x83da },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0x0e00, 0x0000 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x83ef },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0x0e00, 0x0000 },
	{ RTL_PHY_STEP_MODIFY, 0xbf38, 0x01f0, 0x0160 },
	{ RTL_PHY_STEP_MODIFY, 0xbf3a, 0x001f, 0x0014 },
	{ RTL_PHY_STEP_MODIFY, 0xbf28, 0x6000, 0x0000 },
	{ RTL_PHY_STEP_MODIFY, 0xbf2c, 0xc000, 0x0000 },
	{ RTL_PHY_STEP_MODIFY, 0xbf28, 0x1fff, 0x0187 },
	{ RTL_PHY_STEP_MODIFY, 0xbf2a, 0x003f, 0x0003 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x8173 },
	{ RTL_PHY_STEP_WRITE, 0xa438, 0x0000, 0x8620 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x8175 },
	{ RTL_PHY_STEP_WRITE, 0xa438, 0x0000, 0x8671 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x817c },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0x0000, 0x2000 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x8187 },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0x0000, 0x2000 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x8192 },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0x0000, 0x2000 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x819d },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0x0000, 0x2000 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x81a8 },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0x2000, 0x0000 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x81b3 },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0x2000, 0x0000 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x81be },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0x0000, 0x2000 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x817d },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0xff00, 0xa600 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x8188 },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0xff00, 0xa600 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x8193 },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0xff00, 0xa600 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x819e },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0xff00, 0xa600 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x81a9 },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0xff00, 0x1400 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x81b4 },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0xff00, 0x1400 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x81bf },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0xff00, 0xa600 },
	{ RTL_PHY_STEP_MODIFY, 0xaeaa, 0x0028, 0x0000 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x84f0 },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0x201c },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x84f2 },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0x3117 },
	{ RTL_PHY_STEP_WRITE, 0xaec6, 0x0000, 0x0000 },
	{ RTL_PHY_STEP_WRITE, 0xae20, 0x0000, 0xffff },
	{ RTL_PHY_STEP_WRITE, 0xaece, 0x0000, 0xffff },
	{ RTL_PHY_STEP_WRITE, 0xaed2, 0x0000, 0xffff },
	{ RTL_PHY_STEP_WRITE, 0xaec8, 0x0000, 0x0000 },
	{ RTL_PHY_STEP_MODIFY, 0xaed0, 0x0001, 0x0000 },
	{ RTL_PHY_STEP_WRITE, 0xadb8, 0x0000, 0x0150 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x8197 },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0xff00, 0x5000 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x8231 },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0xff00, 0x5000 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x82cb },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0xff00, 0x5000 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x82cd },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0xff00, 0x5700 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x8233 },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0xff00, 0x5700 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x8199 },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0xff00, 0x5700 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x815a },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0x0150 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x81f4 },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0x0150 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x828e },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0x0150 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x81b1 },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0x0000 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x824b },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0x0000 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x82e5 },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0x0000 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x84f7 },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0xff00, 0x2800 },
	{ RTL_PHY_STEP_MODIFY, 0xaec2, 0x0000, 0x1000 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x81b3 },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0xff00, 0xad00 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x824d },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0xff00, 0xad00 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x82e7 },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0xff00, 0xad00 },
	{ RTL_PHY_STEP_MODIFY, 0xae4e, 0x000f, 0x0001 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x82ce },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0xf000, 0x4000 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x83a5 },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0xff00, 0x0400 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x83a6 },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0xff00, 0x0400 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x83a7 },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0xff00, 0x0400 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x83a8 },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0xff00, 0x0400 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x84ac },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0x0000 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x84ae },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0x0000 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x84b0 },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0xf818 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x84b2 },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0xff00, 0x6000 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x8ffc },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0x6008 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x8ffe },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0xf4ff },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x8015 },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0x0000, 0x0200 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x8016 },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0x0800, 0x0000 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x8fe6 },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0xff00, 0x0800 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x8fe4 },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0x2114 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x8647 },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0xa7b1 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x8649 },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0xbbca },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x864b },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0xff00, 0xdc00 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x8154 },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0xc000, 0x4000 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x8158 },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0xc000, 0x0000 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x826c },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0xffff },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x826e },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0xffff },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x8872 },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0xff00, 0x0e00 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x8012 },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0x0000, 0x0800 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x8012 },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0x0000, 0x4000 },
	{ RTL_PHY_STEP_MODIFY, 0xb576, 0x0000, 0x0001 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x834a },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0xff00, 0x0700 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x8217 },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0x3f00, 0x2a00 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x81b1 },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0xff00, 0x0b00 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x8fed },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0xff00, 0x4e00 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x88ac },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0xff00, 0x2300 },
	{ RTL_PHY_STEP_MODIFY, 0xbf0c, 0x0000, 0x3800 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x88de },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0xff00, 0x0000 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x80b4 },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0x5195 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x8370 },
	{ RTL_PHY_STEP_WRITE, 0xa438, 0x0000, 0x8671 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x8372 },
	{ RTL_PHY_STEP_WRITE, 0xa438, 0x0000, 0x86c8 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x8401 },
	{ RTL_PHY_STEP_WRITE, 0xa438, 0x0000, 0x86c8 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x8403 },
	{ RTL_PHY_STEP_WRITE, 0xa438, 0x0000, 0x86da },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x8406 },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0x1800, 0x1000 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x8408 },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0x1800, 0x1000 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x840a },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0x1800, 0x1000 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x840c },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0x1800, 0x1000 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x840e },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0x1800, 0x1000 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x8410 },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0x1800, 0x1000 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x8412 },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0x1800, 0x1000 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x8414 },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0x1800, 0x1000 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x8416 },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0x1800, 0x1000 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x82bd },
	{ RTL_PHY_STEP_WRITE, 0xa438, 0x0000, 0x1f40 },
	{ RTL_PHY_STEP_MODIFY, 0xbfb4, 0x07ff, 0x0328 },
	{ RTL_PHY_STEP_WRITE, 0xbfb6, 0x0000, 0x3e14 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x81c4 },
	{ RTL_PHY_STEP_WRITE, 0xa438, 0x0000, 0x003b },
	{ RTL_PHY_STEP_WRITE, 0xa438, 0x0000, 0x0086 },
	{ RTL_PHY_STEP_WRITE, 0xa438, 0x0000, 0x00b7 },
	{ RTL_PHY_STEP_WRITE, 0xa438, 0x0000, 0x00db },
	{ RTL_PHY_STEP_WRITE, 0xa438, 0x0000, 0x00fe },
	{ RTL_PHY_STEP_WRITE, 0xa438, 0x0000, 0x00fe },
	{ RTL_PHY_STEP_WRITE, 0xa438, 0x0000, 0x00fe },
	{ RTL_PHY_STEP_WRITE, 0xa438, 0x0000, 0x00fe },
	{ RTL_PHY_STEP_WRITE, 0xa438, 0x0000, 0x00c3 },
	{ RTL_PHY_STEP_WRITE, 0xa438, 0x0000, 0x0078 },
	{ RTL_PHY_STEP_WRITE, 0xa438, 0x0000, 0x0047 },
	{ RTL_PHY_STEP_WRITE, 0xa438, 0x0000, 0x0023 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x88d7 },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0x01a0 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x88d9 },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0x01a0 },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x8ffa },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0x002a },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x8fee },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0xffdf },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x8ff0 },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0xffff },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x8ff2 },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0x0a4a },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x8ff4 },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0xaa5a },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x8ff6 },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0x0a4a },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x8ff8 },
	{ RTL_PHY_STEP_WRITE, 0xb87e, 0x0000, 0xaa5a },
	{ RTL_PHY_STEP_WRITE, 0xb87c, 0x0000, 0x88d5 },
	{ RTL_PHY_STEP_MODIFY, 0xb87e, 0xff00, 0x0200 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x84bb },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0xff00, 0x0a00 },
	{ RTL_PHY_STEP_WRITE, 0xa436, 0x0000, 0x84c0 },
	{ RTL_PHY_STEP_MODIFY, 0xa438, 0xff00, 0x1600 },
	{ RTL_PHY_STEP_MODIFY, 0xa430, 0x0000, 0x0003 },
	{ RTL_PHY_STEP_MODIFY, 0xa442, 0x0000, 0x0080 },
	{ RTL_PHY_STEP_MODIFY, 0xa430, 0x1000, 0x0000 },
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
static void set8(struct rtl_device *device, uint32_t offset, uint8_t bits);
static void clear8(struct rtl_device *device, uint32_t offset, uint8_t bits);
static int phy_ocp_modify(struct rtl_device *device, uint16_t reg, uint16_t mask, uint16_t value);
static int mii_read(struct rtl_device *device, unsigned reg, uint16_t *value);
static int mii_write(struct rtl_device *device, unsigned reg, uint16_t value);
static uint32_t csi_read(struct rtl_device *device, uint32_t reg);
static void csi_write(struct rtl_device *device, uint32_t reg, uint32_t value);
static void ephy_write(struct rtl_device *device, uint16_t reg, uint16_t value);
static int run_phy_steps(struct rtl_device *device, const struct rtl_phy_step *steps, unsigned count);
static int bring_up_link(struct rtl_device *device);
static int power_up_phy(struct rtl_device *device);
static void disable_clock_request(struct rtl_device *device);
static void stop_mac_mcu(struct rtl_device *device);
static void configure_hardware(struct rtl_device *device);
static void configure_ephy(struct rtl_device *device);
static int reset_phy(struct rtl_device *device);
static int configure_phy(struct rtl_device *device);
static int disable_eee(struct rtl_device *device);
static int advertise_all(struct rtl_device *device);
static void start_link_thread(struct rtl_device *device);
static void link_worker(void *argument);
static void check_link(struct rtl_device *device);
static uint32_t speed_of(uint16_t status);
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

	/* Follows the link from here on. */
	start_link_thread(device);

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

/*
 * Opens the interface: the controller and the PHY are set up again and the
 * link thread starts following the negotiation.
 */
static int
rtl_open(
	struct net_device *net)
{
	struct rtl_device *device;
	unsigned long irq;
	int error;

	/* Brings the link up from reset. */
	device = net->driver_data;
	error = bring_up_link(device);
	if (error != 0) {
		kern_logf("rtl8125: %s link set-up failed (%d)\n", device->name, error);
		return error;
	}

	/* Lets the link thread read the PHY status. */
	irq = spin_lock_irqsave(&device->lock);

	device->opened = 1U;

	spin_unlock_irqrestore(&device->lock, irq);

	/* Tells which PHY microcode the link runs on. */
	kern_logf("rtl8125: %s PHY microcode version 0x%04x\n", device->name, device->ram_code_version);

	/* Succeeded: the interface is open and the carrier follows the link. */
	return 0;
}

/* Closes the interface; the link thread announces the carrier lost. */
static void
rtl_close(
	struct net_device *net)
{
	struct rtl_device *device;
	unsigned long irq;

	/* Stops the link thread from reading the PHY status. */
	device = net->driver_data;
	irq = spin_lock_irqsave(&device->lock);

	device->opened = 0U;

	spin_unlock_irqrestore(&device->lock, irq);
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


/* Sets bits of one byte register. */
static void
set8(
	struct rtl_device *device,
	uint32_t offset,
	uint8_t bits)
{
	uint8_t value;

	/* Writes the register back with the bits. */
	value = read8(device, offset);
	write8(device, offset, (uint8_t)(value | bits));
}

/* Clears bits of one byte register. */
static void
clear8(
	struct rtl_device *device,
	uint32_t offset,
	uint8_t bits)
{
	uint8_t value;

	/* Writes the register back without the bits. */
	value = read8(device, offset);
	write8(device, offset, (uint8_t)(value & ~bits));
}


/* Clears the mask bits and sets the value bits of one PHY OCP register. */
static int
phy_ocp_modify(
	struct rtl_device *device,
	uint16_t reg,
	uint16_t mask,
	uint16_t value)
{
	uint16_t current;
	int error;

	/* Reads the register. */
	error = phy_ocp_read(device, reg, &current);
	if (error != 0)
		return error;

	/* Writes it back changed. */
	error = phy_ocp_write(device, reg, (uint16_t)((current & ~mask) | value));
	if (error != 0)
		return error;

	/* Succeeded: the register holds the new bits. */
	return 0;
}

/* Reads one clause-22 register of the PHY through its OCP space. */
static int
mii_read(
	struct rtl_device *device,
	unsigned reg,
	uint16_t *value)
{
	int error;

	/* Reads the register's OCP copy. */
	error = phy_ocp_read(device, (uint16_t)(RTL_PHY_OCP_MII_BASE + reg * 2U), value);
	if (error != 0)
		return error;

	/* Succeeded: the value is the register's. */
	return 0;
}

/* Writes one clause-22 register of the PHY through its OCP space. */
static int
mii_write(
	struct rtl_device *device,
	unsigned reg,
	uint16_t value)
{
	int error;

	/* Writes the register's OCP copy. */
	error = phy_ocp_write(device, (uint16_t)(RTL_PHY_OCP_MII_BASE + reg * 2U), value);
	if (error != 0)
		return error;

	/* Succeeded: the PHY took the value. */
	return 0;
}

/*
 * Reads one longword of the function's configuration space through the
 * controller's CSI window.
 *
 * The window sets its flag once the data has arrived; the read goes on
 * after a bounded wait either way.
 *
 * Procedure referenced: OpenBSD sys/dev/pci/if_rge.c 1.48 (rge_read_csi).
 */
static uint32_t
csi_read(
	struct rtl_device *device,
	uint32_t reg)
{
	uint32_t window;
	unsigned poll;

	/* Names the longword with every byte enabled. */
	write32(device, RTL_REG_CSI_ADDRESS, (reg & RTL_CSI_ADDRESS_MASK) | RTL_CSI_BYTE_ENABLE);

	/* Waits for the data. */
	for (poll = 0; poll < RTL_OCP_POLLS; poll++) {
		kern_usleep_range(RTL_OCP_POLL_US, RTL_OCP_POLL_US);

		/* The data is valid once the flag is set. */
		window = read32(device, RTL_REG_CSI_ADDRESS);
		if ((window & RTL_CSI_BUSY) != 0)
			break;
	}

	/* Reports the longword after the window settles. */
	kern_usleep_range(RTL_CSI_SETTLE_US, RTL_CSI_SETTLE_US);
	return read32(device, RTL_REG_CSI_DATA);
}

/*
 * Writes one longword of the function's configuration space through the
 * controller's CSI window.
 *
 * Procedure referenced: OpenBSD sys/dev/pci/if_rge.c 1.48 (rge_write_csi).
 */
static void
csi_write(
	struct rtl_device *device,
	uint32_t reg,
	uint32_t value)
{
	uint32_t window;
	unsigned poll;

	/* Hands over the data and names the longword with every byte enabled. */
	write32(device, RTL_REG_CSI_DATA, value);
	write32(device,
		RTL_REG_CSI_ADDRESS,
		(reg & RTL_CSI_ADDRESS_MASK) | RTL_CSI_BYTE_ENABLE | RTL_CSI_BUSY);

	/* Waits for the write to finish; the window settles either way. */
	for (poll = 0; poll < RTL_OCP_POLLS; poll++) {
		kern_usleep_range(RTL_OCP_POLL_US, RTL_OCP_POLL_US);

		/* The write is done once the flag is clear. */
		window = read32(device, RTL_REG_CSI_ADDRESS);
		if ((window & RTL_CSI_BUSY) == 0)
			break;
	}
	kern_usleep_range(RTL_CSI_SETTLE_US, RTL_CSI_SETTLE_US);
}

/*
 * Writes one register of the PCI Express PHY.
 *
 * Procedure referenced: OpenBSD sys/dev/pci/if_rge.c 1.48 (rge_write_ephy).
 */
static void
ephy_write(
	struct rtl_device *device,
	uint16_t reg,
	uint16_t value)
{
	uint32_t window;
	unsigned poll;

	/* Names the register and the value with the write bit. */
	window = (uint32_t)(reg & RTL_EPHY_ADDRESS_MASK) << RTL_EPHY_ADDRESS_SHIFT;
	window |= RTL_EPHY_BUSY | (value & RTL_EPHY_DATA_MASK);
	write32(device, RTL_REG_EPHY, window);

	/* Waits for the write to finish; the window settles either way. */
	for (poll = 0; poll < RTL_OCP_POLLS; poll++) {
		kern_usleep_range(RTL_OCP_POLL_US, RTL_OCP_POLL_US);

		/* The write is done once the flag is clear. */
		window = read32(device, RTL_REG_EPHY);
		if ((window & RTL_EPHY_BUSY) == 0)
			break;
	}
	kern_usleep_range(RTL_CSI_SETTLE_US, RTL_CSI_SETTLE_US);
}

/* Runs a PHY register sequence in order and stops at the first PHY that does not answer. */
static int
run_phy_steps(
	struct rtl_device *device,
	const struct rtl_phy_step *steps,
	unsigned count)
{
	uint16_t ignored;
	unsigned index;
	int error;

	/* Applies each step. */
	for (index = 0; index < count; index++) {
		/* Does what the step says. */
		switch (steps[index].kind) {
		case RTL_PHY_STEP_WRITE:
			error = phy_ocp_write(device, steps[index].reg, steps[index].value);
			break;
		case RTL_PHY_STEP_MODIFY:
			error = phy_ocp_modify(device, steps[index].reg, steps[index].mask, steps[index].value);
			break;
		default:
			error = phy_ocp_read(device, steps[index].reg, &ignored);
			break;
		}

		/* A PHY that stops answering ends the sequence. */
		if (error != 0)
			return error;
	}

	/* Succeeded: every step was applied. */
	return 0;
}

/*
 * Brings the controller and its PHY from reset to an autonegotiating link.
 *
 * It runs in thread context because the PHY's reset takes up to seconds.
 * The MAC's and the PHY's microcode patches are not loaded: the MAC's
 * microcode stays stopped and the PHY runs what it came up with.
 *
 * Procedure referenced: OpenBSD sys/dev/pci/if_rge.c 1.48 (rge_init,
 * rge_chipinit, rge_phy_config, rge_ifmedia_upd).
 */
static int
bring_up_link(
	struct rtl_device *device)
{
	int error;

	/* Takes the controller back from the firmware and resets it. */
	error = exit_out_of_band(device);
	if (error != 0)
		return error;

	/* Powers the PHY up. */
	error = power_up_phy(device);
	if (error != 0)
		return error;

	/* Sets the MAC up without its microcode, then resets it with the interrupts masked. */
	configure_hardware(device);
	mask_interrupts(device);
	write32(device, RTL_REG_TIMER_INTERRUPT0, 0U);
	write32(device, RTL_REG_TIMER_INTERRUPT1, 0U);
	write32(device, RTL_REG_TIMER_INTERRUPT2, 0U);
	write32(device, RTL_REG_TIMER_INTERRUPT3, 0U);
	error = reset_controller(device);
	if (error != 0)
		return error;

	/* Sets the PCI Express PHY up. */
	configure_ephy(device);

	/* Resets the PHY with nothing advertised. */
	error = reset_phy(device);
	if (error != 0)
		return error;

	/* Applies the PHY's settings. */
	error = configure_phy(device);
	if (error != 0)
		return error;

	/* Turns energy-efficient Ethernet off. */
	error = disable_eee(device);
	if (error != 0)
		return error;

	/* Advertises every speed the part has and restarts the negotiation. */
	error = advertise_all(device);
	if (error != 0)
		return error;

	/* Succeeded: the PHY is negotiating. */
	return 0;
}

/*
 * Powers the PHY up and waits for it to report it is ready.
 *
 * Procedure referenced: OpenBSD sys/dev/pci/if_rge.c 1.48
 * (rge_set_phy_power).
 */
static int
power_up_phy(
	struct rtl_device *device)
{
	uint16_t state;
	unsigned poll;
	int error;

	/* Turns the PHY's power on and lets it autonegotiate. */
	set8(device, RTL_REG_POWER_CONTROL, RTL_POWER_CONTROL_PHY_ON);
	error = mii_write(device, RTL_MII_CONTROL, RTL_MII_CONTROL_AUTONEG);
	if (error != 0)
		return error;

	/* Waits for the PHY to be ready; a slow one is set up anyway. */
	for (poll = 0; poll < RTL_PHY_STATE_POLLS; poll++) {
		/* A PHY that does not answer ends the wait. */
		error = phy_ocp_read(device, RTL_PHY_OCP_STATE, &state);
		if (error != 0)
			return error;

		/* The PHY is ready once its state says so. */
		if ((state & RTL_PHY_OCP_STATE_MASK) == RTL_PHY_OCP_STATE_READY)
			break;

		kern_usleep_range(RTL_PHY_STATE_POLL_US, RTL_PHY_STATE_POLL_US);
	}

	/* Succeeded: the PHY is powered. */
	return 0;
}

/*
 * Keeps the link out of CLKREQ power saving and clears a pending PME.
 *
 * Procedure referenced: OpenBSD sys/dev/pci/if_rge.c 1.48
 * (rge_disable_aspm_clkreq).
 */
static void
disable_clock_request(
	struct rtl_device *device)
{
	uint8_t lock;
	unsigned unlocked;

	/* Opens the configuration registers unless they are open already. */
	lock = read8(device, RTL_REG_CONFIG_LOCK);
	unlocked = 0U;
	if ((lock & RTL_CONFIG_LOCK_WRITABLE) != RTL_CONFIG_LOCK_WRITABLE) {
		set8(device, RTL_REG_CONFIG_LOCK, RTL_CONFIG_LOCK_WRITABLE);
		unlocked = 1U;
	}

	/* Clears CLKREQ where the part keeps it. */
	if (device->chip->family == RTL_FAMILY_8126 || device->chip->family == RTL_FAMILY_8127) {
		clear8(device, RTL_REG_INTERRUPT_CONFIG0, RTL_INTERRUPT_CONFIG0_CLKREQ);
	} else {
		clear8(device, RTL_REG_CONFIG2, RTL_CONFIG2_CLKREQ);
	}

	/* Clears a pending PME. */
	clear8(device, RTL_REG_CONFIG5, RTL_CONFIG5_PME_STATUS);

	/* Locks the configuration registers again if this opened them. */
	if (unlocked)
		clear8(device, RTL_REG_CONFIG_LOCK, RTL_CONFIG_LOCK_WRITABLE);
}

/*
 * Stops the MAC's microcode and clears its break points.
 *
 * Procedure referenced: OpenBSD sys/dev/pci/if_rge.c 1.48 (rge_hw_init).
 */
static void
stop_mac_mcu(
	struct rtl_device *device)
{
	uint16_t reg;

	/* Disables the break points, then clears each one. */
	mac_ocp_write(device, RTL_MAC_OCP_MCU_BREAK_ENABLE, 0U);
	for (reg = RTL_MAC_OCP_MCU_BREAK_FIRST; reg < RTL_MAC_OCP_MCU_BREAK_ENABLE; reg = (uint16_t)(reg + 2U))
		mac_ocp_write(device, reg, 0U);

	/* Gives the microcode time to stop, then turns it off. */
	kern_usleep_range(RTL_MCU_STOP_US, RTL_MCU_STOP_US);
	mac_ocp_write(device, RTL_MAC_OCP_MCU_ENABLE, 0U);
}

/*
 * Sets the MAC up for the driver: no CLKREQ, no UPS, the microcode
 * stopped, and uncorrectable PCI Express errors reported.
 *
 * Procedure referenced: OpenBSD sys/dev/pci/if_rge.c 1.48 (rge_hw_init),
 * without the microcode patch.
 */
static void
configure_hardware(
	struct rtl_device *device)
{
	uint32_t status;

	/* Keeps the link out of CLKREQ power saving. */
	disable_clock_request(device);
	clear8(device, RTL_REG_MISC_F1, RTL_MISC_F1_BIT);

	/* Turns the firmware's power saving (UPS) off. */
	mac_ocp_clear(device, RTL_MAC_OCP_UPS_CONTROL, RTL_MAC_OCP_UPS_CONTROL_BIT);

	/* Stops the MAC's microcode, which runs without a patch from here on. */
	disable_clock_request(device);
	stop_mac_mcu(device);

	/* Reports uncorrectable errors in the PCI Express status. */
	status = csi_read(device, RTL_CSI_UNCORRECTABLE);
	csi_write(device, RTL_CSI_UNCORRECTABLE, status | RTL_CSI_UNCORRECTABLE_BIT);
}

/*
 * Writes the part's PCI Express PHY table.
 *
 * The RTL8127's registers are named by a page in an extended address
 * register and an offset; the page goes back to 0 at the end.
 *
 * Procedure referenced: OpenBSD sys/dev/pci/if_rge.c 1.48
 * (rge_ephy_config_mac_r27, rge_r27_write_ephy, rge_check_ephy_ext_add).
 */
static void
configure_ephy(
	struct rtl_device *device)
{
	unsigned index;
	uint16_t reg;

	/* Only the RTL8127's table is known here; the others keep their reset values. */
	if (device->chip->family != RTL_FAMILY_8127)
		return;

	/* Writes each entry through its page. */
	for (index = 0; index < sizeof(rtl_ephy_8127) / sizeof(rtl_ephy_8127[0]); index++) {
		reg = rtl_ephy_8127[index].reg;
		ephy_write(device, RTL_EPHY_EXTENDED_ADDRESS, (uint16_t)(reg >> 12));
		ephy_write(device, (uint16_t)(reg & 0x0fffU), rtl_ephy_8127[index].value);
	}

	/* Returns to the first page. */
	ephy_write(device, RTL_EPHY_EXTENDED_ADDRESS, 0U);
}

/*
 * Clears what the PHY advertises and resets it.
 *
 * Procedure referenced: OpenBSD sys/dev/pci/if_rge.c 1.48 (rge_phy_config,
 * the PHY reset).
 */
static int
reset_phy(
	struct rtl_device *device)
{
	uint16_t advertise;
	uint16_t multigig;
	uint16_t control;
	unsigned poll;
	int error;

	/* Advertises no 10 or 100 Mbit/s mode. */
	error = mii_read(device, RTL_MII_ADVERTISE, &advertise);
	if (error != 0)
		return error;
	advertise &= (uint16_t)~(RTL_MII_ADVERTISE_100_FULL | RTL_MII_ADVERTISE_100 |
				  RTL_MII_ADVERTISE_10_FULL | RTL_MII_ADVERTISE_10);
	error = mii_write(device, RTL_MII_ADVERTISE, advertise);
	if (error != 0)
		return error;

	/* Advertises no gigabit mode. */
	error = mii_read(device, RTL_MII_1000_CONTROL, &control);
	if (error != 0)
		return error;
	control &= (uint16_t)~(RTL_MII_1000_FULL | RTL_MII_1000_HALF);
	error = mii_write(device, RTL_MII_1000_CONTROL, control);
	if (error != 0)
		return error;

	/* Advertises none of the multi-gigabit modes the part has. */
	multigig = RTL_PHY_OCP_MULTIGIG_2500;
	if (device->chip->family == RTL_FAMILY_8126)
		multigig |= RTL_PHY_OCP_MULTIGIG_5000;
	if (device->chip->family == RTL_FAMILY_8127)
		multigig |= RTL_PHY_OCP_MULTIGIG_5000 | RTL_PHY_OCP_MULTIGIG_10000;
	error = phy_ocp_modify(device, RTL_PHY_OCP_MULTIGIG, multigig, 0U);
	if (error != 0)
		return error;

	/* Resets the PHY. */
	error = mii_write(device,
			  RTL_MII_CONTROL,
			  RTL_MII_CONTROL_RESET | RTL_MII_CONTROL_AUTONEG | RTL_MII_CONTROL_RESTART);
	if (error != 0)
		return error;

	/* Waits for the reset to finish. */
	for (poll = 0; poll < RTL_PHY_RESET_POLLS; poll++) {
		/* A PHY that does not answer ends the wait. */
		error = mii_read(device, RTL_MII_CONTROL, &control);
		if (error != 0)
			return error;

		/* The PHY is out of reset once the bit clears. */
		if ((control & RTL_MII_CONTROL_RESET) == 0)
			return 0;

		sched_sleep(sched_ticks() + KERN_MS_TO_TICKS(RTL_PHY_RESET_POLL_MS));
	}

	/* A PHY that stays in reset is reported. */
	return ETIMEDOUT;
}

/*
 * Records the PHY's microcode version and applies the part's PHY settings.
 *
 * Procedure referenced: OpenBSD sys/dev/pci/if_rge.c 1.48 (rge_phy_config).
 */
static int
configure_phy(
	struct rtl_device *device)
{
	int error;

	/* Reads the version of the microcode the PHY runs. */
	error = phy_ocp_write(device, RTL_PHY_OCP_INDEX, RTL_PHY_OCP_RAM_CODE_VERSION);
	if (error != 0)
		return error;
	error = phy_ocp_read(device, RTL_PHY_OCP_DATA, &device->ram_code_version);
	if (error != 0)
		return error;

	/* Applies the RTL8127's settings; the other parts keep the PHY's own. */
	if (device->chip->family == RTL_FAMILY_8127) {
		error = run_phy_steps(device, rtl_phy_8127, sizeof(rtl_phy_8127) / sizeof(rtl_phy_8127[0]));
		if (error != 0)
			return error;
	}

	/* Clears the bit every part's settings end with. */
	error = phy_ocp_modify(device, 0xa5b4U, 0x8000U, 0U);
	if (error != 0)
		return error;

	/* Succeeded: the PHY is set up. */
	return 0;
}

/*
 * Turns energy-efficient Ethernet and its advanced mode off in the MAC and
 * the PHY.
 *
 * Procedure and values referenced: OpenBSD sys/dev/pci/if_rge.c 1.48
 * (rge_phy_config, "Disable EEE" and "Disable advanced EEE").
 */
static int
disable_eee(
	struct rtl_device *device)
{
	static const struct rtl_phy_step common[] = {
		{ RTL_PHY_STEP_MODIFY, 0xa6d8, 0x0010, 0x0000 },
		{ RTL_PHY_STEP_MODIFY, 0xa428, 0x0080, 0x0000 },
		{ RTL_PHY_STEP_MODIFY, 0xa4a2, 0x0200, 0x0000 },
	};
	static const struct rtl_phy_step advanced[] = {
		{ RTL_PHY_STEP_MODIFY, 0xa442, 0x3000, 0x0000 },
		{ RTL_PHY_STEP_MODIFY, 0xa430, 0x8000, 0x0000 },
	};
	uint16_t bits;
	int error;

	/* Turns EEE off in the MAC. */
	mac_ocp_clear(device, RTL_MAC_OCP_EEE, RTL_MAC_OCP_EEE_BITS);

	/* Turns the PHY's EEE advertisement off; the RTL8127 has one more speed of it. */
	bits = 0x0006U;
	if (device->chip->family == RTL_FAMILY_8127)
		bits = 0x000eU;
	error = phy_ocp_modify(device, 0xa5d0U, bits, 0U);
	if (error != 0)
		return error;

	/* Turns the PHY's multi-gigabit EEE off; the RTL8126 and RTL8127 have two bits of it. */
	bits = 0x0001U;
	if (device->chip->family == RTL_FAMILY_8126 || device->chip->family == RTL_FAMILY_8127)
		bits = 0x0003U;
	error = phy_ocp_modify(device, 0xa6d4U, bits, 0U);
	if (error != 0)
		return error;

	/* Turns the rest of the PHY's EEE off. */
	error = run_phy_steps(device, common, sizeof(common) / sizeof(common[0]));
	if (error != 0)
		return error;

	/* Turns advanced EEE off in the MAC and the PHY. */
	mac_ocp_clear(device, RTL_MAC_OCP_EEE_ADVANCED, RTL_MAC_OCP_EEE_ADVANCED_BIT);
	error = run_phy_steps(device, advanced, sizeof(advanced) / sizeof(advanced[0]));
	if (error != 0)
		return error;

	/* Succeeded: EEE is off. */
	return 0;
}

/*
 * Advertises every speed the part has, with pause, and restarts the
 * negotiation.
 *
 * Procedure and values referenced: OpenBSD sys/dev/pci/if_rge.c 1.48
 * (rge_ifmedia_upd, the automatic media).
 */
static int
advertise_all(
	struct rtl_device *device)
{
	uint16_t multigig;
	uint16_t lite;
	uint16_t value;
	int error;

	/* Turns Gigabit Lite off. */
	error = phy_ocp_modify(device, 0xa428U, 0x0200U, 0U);
	if (error != 0)
		return error;
	lite = 0x0001U;
	if (device->chip->family == RTL_FAMILY_8126 || device->chip->family == RTL_FAMILY_8127)
		lite = 0x0007U;
	error = phy_ocp_modify(device, 0xa5eaU, lite, 0U);
	if (error != 0)
		return error;

	/* Chooses the multi-gigabit speeds the part has. */
	multigig = RTL_PHY_OCP_MULTIGIG_2500;
	if (device->chip->family == RTL_FAMILY_8126)
		multigig |= RTL_PHY_OCP_MULTIGIG_5000;
	if (device->chip->family == RTL_FAMILY_8127)
		multigig |= RTL_PHY_OCP_MULTIGIG_5000 | RTL_PHY_OCP_MULTIGIG_10000;

	/* Advertises 10 and 100 Mbit/s at both duplexes, with pause. */
	error = mii_write(device,
			  RTL_MII_ADVERTISE,
			  RTL_MII_ADVERTISE_100_FULL | RTL_MII_ADVERTISE_100 |
				  RTL_MII_ADVERTISE_10_FULL | RTL_MII_ADVERTISE_10 |
				  RTL_MII_ADVERTISE_PAUSE_ASYM | RTL_MII_ADVERTISE_PAUSE);
	if (error != 0)
		return error;

	/* Advertises gigabit at both duplexes. */
	error = mii_write(device, RTL_MII_1000_CONTROL, RTL_MII_1000_FULL | RTL_MII_1000_HALF);
	if (error != 0)
		return error;

	/* Advertises the multi-gigabit speeds. */
	error = phy_ocp_modify(device, RTL_PHY_OCP_MULTIGIG, multigig, multigig);
	if (error != 0)
		return error;

	/* Restarts the negotiation. */
	value = RTL_MII_CONTROL_RESET | RTL_MII_CONTROL_AUTONEG | RTL_MII_CONTROL_RESTART;
	error = mii_write(device, RTL_MII_CONTROL, value);
	if (error != 0)
		return error;

	/* Succeeded: the PHY is negotiating. */
	return 0;
}

/* Starts the thread that follows the link while the interface is open. */
static void
start_link_thread(
	struct rtl_device *device)
{
	int error;

	/* Creates the thread; without it the carrier never comes up. */
	error = kthread_create(link_worker, device, SCHED_PRIORITY_DEFAULT, &device->link_thread);
	if (error != 0) {
		kern_logf("rtl8125: %s link thread unavailable (%d)\n", device->name, error);
		return;
	}

	/* Keeps the interface for the thread, which lives as long as the system. */
	net_device_ref(device->net);

	/* Lets it run. */
	thread_start(device->link_thread);
}

/* Follows the link every RTL_LINK_MS for the life of the system. */
static void
link_worker(
	void *argument)
{
	struct rtl_device *device;

	/* Checks the link at once, then on its interval. */
	device = argument;
	for (;;) {
		check_link(device);
		sched_sleep(sched_ticks() + KERN_MS_TO_TICKS(RTL_LINK_MS));
	}
}

/*
 * Reads the link from the PHY status and announces a change.
 *
 * Procedure referenced: OpenBSD sys/dev/pci/if_rge.c 1.48
 * (rge_get_link_status, rge_link_state).
 */
static void
check_link(
	struct rtl_device *device)
{
	unsigned long irq;
	uint16_t status;
	uint32_t mbps;
	unsigned opened;
	unsigned carrier;
	unsigned changed;

	/* Samples the PHY status while the interface is open. */
	irq = spin_lock_irqsave(&device->lock);

	opened = device->opened;
	status = 0U;
	if (opened)
		status = read16(device, RTL_REG_PHY_STATUS);

	spin_unlock_irqrestore(&device->lock, irq);

	/* A closed interface has no carrier. */
	carrier = 0U;
	mbps = 0U;
	if (opened && (status & RTL_PHY_STATUS_LINK) != 0) {
		carrier = 1U;
		mbps = speed_of(status);
	}

	/* Records the link and whether it moved. */
	irq = spin_lock_irqsave(&device->lock);

	changed = 0U;
	if (carrier != device->carrier || mbps != device->link_mbps) {
		device->carrier = carrier;
		device->link_mbps = mbps;
		changed = 1U;
	}

	spin_unlock_irqrestore(&device->lock, irq);

	/* Announces a change. */
	if (changed) {
		device->net->link_mbps = mbps;
		(void)net_device_set_carrier(device->net, (int)carrier);
		kern_logf("rtl8125: %s link %s %u Mbit/s%s\n",
			  device->name,
			  carrier ? "up" : "down",
			  (unsigned)mbps,
			  (status & RTL_PHY_STATUS_FULL_DUPLEX) != 0 ? " full duplex" : "");
	}
}

/* Reports the speed a PHY status names, in Mbit/s. */
static uint32_t
speed_of(
	uint16_t status)
{
	/* Takes the fastest speed bit that is set. */
	if ((status & RTL_PHY_STATUS_10000) != 0)
		return 10000U;
	if ((status & RTL_PHY_STATUS_5000) != 0)
		return 5000U;
	if ((status & RTL_PHY_STATUS_2500) != 0)
		return 2500U;
	if ((status & RTL_PHY_STATUS_1000) != 0)
		return 1000U;
	if ((status & RTL_PHY_STATUS_100) != 0)
		return 100U;
	if ((status & RTL_PHY_STATUS_10) != 0)
		return 10U;

	/* No speed bit is set. */
	return 0U;
}
