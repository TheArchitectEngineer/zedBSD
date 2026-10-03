/*
 * WS031 Linux-parity — combo PHY init (see combo_phy.h).
 *
 * Faithful port of icl_combo_phys_init() / icl_combo_phy_verify_state() and the
 * procmon selection for ADL-P (display version 13).  On ADL-P every combo PHY has
 * a PHY_MISC register (has_phy_misc == true) and only PHY_A is a master
 * (phy_is_master); the JSL/EHL/RKL/DG1/ADL-S branches do not apply.
 */
#include "../internal.h"
#include <kern/klog.h>
#include "parity.h"
#include "osdep/mmio.h"
#include "osdep/trace.h"
#include "combo_phy.h"

/* intel_combo_phy_regs.h: per-PHY register bases and field bits. */
#define COMBOPHY_A   0x162000u
#define COMBOPHY_B   0x6C000u
#define PORT_COMP    0x100u   /* COMP_DW base within a PHY */
#define PORT_PCS_GRP 0x600u
#define PORT_PCS_LN0 0x800u
#define PORT_TX_GRP  0x680u
#define PORT_TX_LN0  0x880u

#define COMP_INIT               (1u << 31)
#define IREFGEN                 (1u << 24)
#define CL_POWER_DOWN_ENABLE    (1u << 4)
#define PROCESS_INFO_MASK       (7u << 26)
#define VOLTAGE_INFO_MASK       (3u << 24)
#define PHY_MISC_DE_IO_COMP_PWR_DOWN  (1u << 23)
#define TX_DW8_ODCC_CLK_SEL           (1u << 31)
#define TX_DW8_ODCC_CLK_DIV_SEL_MASK  (3u << 29)
#define TX_DW8_ODCC_CLK_DIV_SEL_DIV2  (1u << 29)
#define DCC_MODE_SELECT_MASK    (3u << 20)
#define RUN_DCC_ONCE            (0u << 20)

/* Procmon reference values (icl_procmon_values[], dw1/dw9/dw10). */
struct icl_procmon { uint32_t dw1, dw9, dw10; };
static const struct icl_procmon procmon_values[5] = {
	{ 0x00000000u, 0x62AB67BBu, 0x51914F96u },   /* 0.85V dot0 */
	{ 0x00000000u, 0x86E172C7u, 0x77CA5EABu },   /* 0.95V dot0 */
	{ 0x00000000u, 0x93F87FE1u, 0x8AE871C5u },   /* 0.95V dot1 */
	{ 0x00000000u, 0x98FA82DDu, 0x89E46DC1u },   /* 1.05V dot0 */
	{ 0x00440000u, 0x9A00AB25u, 0x8AE38FF1u },   /* 1.05V dot1 */
};

static unsigned combophy_base(unsigned phy) { return phy == 0u ? COMBOPHY_A : COMBOPHY_B; }
static unsigned comp_dw(unsigned phy, unsigned dw) { return combophy_base(phy) + PORT_COMP + 4u * dw; }
static unsigned cl_dw(unsigned phy, unsigned dw)   { return combophy_base(phy) + 4u * dw; }
static unsigned phy_misc(unsigned phy)             { return phy == 0u ? 0x64C00u : 0x64C04u; }
static unsigned tx_dw8_ln0(unsigned phy)   { return combophy_base(phy) + PORT_TX_LN0 + 4u * 8u; }
static unsigned tx_dw8_grp(unsigned phy)   { return combophy_base(phy) + PORT_TX_GRP + 4u * 8u; }
static unsigned pcs_dw1_ln0(unsigned phy)  { return combophy_base(phy) + PORT_PCS_LN0 + 4u * 1u; }
static unsigned pcs_dw1_grp(unsigned phy)  { return combophy_base(phy) + PORT_PCS_GRP + 4u * 1u; }

/* On ADL-P every combo PHY has PHY_MISC; only PHY_A is a master. */
static int has_phy_misc(unsigned phy) { (void)phy; return 1; }
static int phy_is_master(unsigned phy) { return phy == 0u; }

static void
rmw(struct osdep_mmio *m, unsigned reg, uint32_t clear, uint32_t set)
{
	uint32_t v = osdep_mmio_raw_read32(m, reg);

	osdep_mmio_raw_write32(m, reg, (v & ~clear) | set);
}

/* icl_get_procmon_ref_values(): select from COMP_DW3 process/voltage info. */
static const struct icl_procmon *
get_procmon(struct osdep_mmio *m, unsigned phy)
{
	uint32_t val = osdep_mmio_raw_read32(m, comp_dw(phy, 3u));

	switch (val & (PROCESS_INFO_MASK | VOLTAGE_INFO_MASK)) {
	case (0u << 24) | (0u << 26): return &procmon_values[0];   /* 0.85V dot0 */
	case (1u << 24) | (0u << 26): return &procmon_values[1];   /* 0.95V dot0 */
	case (1u << 24) | (1u << 26): return &procmon_values[2];   /* 0.95V dot1 */
	case (2u << 24) | (0u << 26): return &procmon_values[3];   /* 1.05V dot0 */
	case (2u << 24) | (1u << 26): return &procmon_values[4];   /* 1.05V dot1 */
	default:
		/* MISSING_CASE + fallthrough to the 0.85V dot0 defaults (reference). */
		return &procmon_values[0];
	}
}

static int
check_phy_reg(struct osdep_mmio *m, unsigned reg, uint32_t mask, uint32_t expected)
{
	return ((osdep_mmio_raw_read32(m, reg) & mask) == expected) ? 1 : 0;
}

static int
verify_procmon(struct osdep_mmio *m, unsigned phy)
{
	const struct icl_procmon *p = get_procmon(m, phy);
	int ret;

	ret = check_phy_reg(m, comp_dw(phy, 1u), (0xffu << 16) | 0xffu, p->dw1);
	ret &= check_phy_reg(m, comp_dw(phy, 9u), 0xffffffffu, p->dw9);
	ret &= check_phy_reg(m, comp_dw(phy, 10u), 0xffffffffu, p->dw10);
	return ret;
}

static void
set_procmon(struct osdep_mmio *m, unsigned phy)
{
	const struct icl_procmon *p = get_procmon(m, phy);

	rmw(m, comp_dw(phy, 1u), (0xffu << 16) | 0xffu, p->dw1);
	osdep_mmio_raw_write32(m, comp_dw(phy, 9u), p->dw9);
	osdep_mmio_raw_write32(m, comp_dw(phy, 10u), p->dw10);
}

/* icl_combo_phy_enabled(): PHY_MISC power-down clear AND COMP_DW0 COMP_INIT. */
static int
combo_phy_enabled(struct osdep_mmio *m, unsigned phy)
{
	if (!has_phy_misc(phy))
		return (osdep_mmio_raw_read32(m, comp_dw(phy, 0u)) & COMP_INIT) ? 1 : 0;
	return (!(osdep_mmio_raw_read32(m, phy_misc(phy)) & PHY_MISC_DE_IO_COMP_PWR_DOWN) &&
		(osdep_mmio_raw_read32(m, comp_dw(phy, 0u)) & COMP_INIT)) ? 1 : 0;
}

int
parity_combo_phy_verify_state(struct osdep_mmio *m, unsigned phy)
{
	int ret = 1;

	if (!combo_phy_enabled(m, phy))
		return 0;

	/* DISPLAY_VER >= 12 (ADL-P): ODCC + DCC bits. */
	ret &= check_phy_reg(m, tx_dw8_ln0(phy),
		TX_DW8_ODCC_CLK_SEL | TX_DW8_ODCC_CLK_DIV_SEL_MASK,
		TX_DW8_ODCC_CLK_SEL | TX_DW8_ODCC_CLK_DIV_SEL_DIV2);
	ret &= check_phy_reg(m, pcs_dw1_ln0(phy), DCC_MODE_SELECT_MASK, RUN_DCC_ONCE);

	ret &= verify_procmon(m, phy);

	if (phy_is_master(phy))
		ret &= check_phy_reg(m, comp_dw(phy, 8u), IREFGEN, IREFGEN);

	ret &= check_phy_reg(m, cl_dw(phy, 5u), CL_POWER_DOWN_ENABLE, CL_POWER_DOWN_ENABLE);
	return ret;
}

void
parity_combo_phy_init_one(struct osdep_mmio *m, unsigned phy)
{
	uint32_t val;

	if (has_phy_misc(phy)) {
		/* Clear the DE IO comp power-down (ADL-P has no JSL/EHL mux quirk). */
		val = osdep_mmio_raw_read32(m, phy_misc(phy));
		val &= ~PHY_MISC_DE_IO_COMP_PWR_DOWN;
		osdep_mmio_raw_write32(m, phy_misc(phy), val);
	}

	/* DISPLAY_VER >= 12: program ODCC (TX_DW8 group) and DCC (PCS_DW1 group). */
	val = osdep_mmio_raw_read32(m, tx_dw8_ln0(phy));
	val &= ~TX_DW8_ODCC_CLK_DIV_SEL_MASK;
	val |= TX_DW8_ODCC_CLK_SEL | TX_DW8_ODCC_CLK_DIV_SEL_DIV2;
	osdep_mmio_raw_write32(m, tx_dw8_grp(phy), val);

	val = osdep_mmio_raw_read32(m, pcs_dw1_ln0(phy));
	val &= ~DCC_MODE_SELECT_MASK;
	val |= RUN_DCC_ONCE;
	osdep_mmio_raw_write32(m, pcs_dw1_grp(phy), val);

	set_procmon(m, phy);

	if (phy_is_master(phy))
		rmw(m, comp_dw(phy, 8u), 0u, IREFGEN);

	rmw(m, comp_dw(phy, 0u), 0u, COMP_INIT);
	rmw(m, cl_dw(phy, 5u), 0u, CL_POWER_DOWN_ENABLE);
}

int
parity_intel_combo_phy_init(struct osdep_mmio *m, struct osdep_trace *trace,
	unsigned *initialised_out)
{
	unsigned phy;
	unsigned initialised = 0u;

	/* for_each_combo_phy (ADL-P: PHY_A, PHY_B). */
	for (phy = 0u; phy < PARITY_COMBO_PHY_NUM; phy++) {
		if (parity_combo_phy_verify_state(m, phy))
			continue;   /* already correct: do not re-write the PHY */
		parity_combo_phy_init_one(m, phy);
		initialised++;
	}

	osdep_trace_emit(trace, PARITY_STAGE_P3, OSDEP_TR_ACQUIRE,
		"intel_combo_phy_init", (uint64_t)initialised, 0u);
	kern_logf("i915: parity P3 intel_combo_phy_init: combo PHYs A/B (%u initialised)\n",
		initialised);
	if (initialised_out != 0)
		*initialised_out = initialised;
	return 0;   /* reference is void: success regardless of how many were programmed */
}
