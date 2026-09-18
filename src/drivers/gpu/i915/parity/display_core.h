/*
 * WS031 Linux-parity — display-core HW bring-up (see display_core.c).
 *
 * Ports intel_power_domains_init_hw(false) -> icl_display_core_init(false) for
 * ADL-P: DC-state disable, PCH reset handshake, combo PHY init (with the void
 * contract's fault check), PW1 enable (fuses), CDCLK init, DBUF slice enable,
 * BW_BUDDY, the xe_lpd workarounds, then hold the POWER_DOMAIN_INIT reference
 * and sync every power well.  Children share ONE device state (same MMIO, the
 * same power-domains + cdclk + PCODE sb_lock); nothing is re-initialised per
 * child.  intel_power_domains_driver_remove() cancels the init wakeref's rpm
 * side while KEEPING the wells enabled (not a domain put).
 */
#ifndef PARITY_DISPLAY_CORE_H
#define PARITY_DISPLAY_CORE_H

#include <stdint.h>

struct osdep_mmio;
struct mutex;
struct parity_power_domains;
struct parity_cdclk_dev;
struct parity_pw_ctx;

/* Shared device state threaded through the display-core bring-up. */
struct parity_display_core {
	struct parity_power_domains *pd;   /* the one power-domains state */
	struct parity_cdclk_dev *cd;       /* the one cdclk state (cd->m == m) */
	struct parity_pw_ctx *pwc;         /* power-well op context (pwc->mmio == m) */
	struct osdep_mmio *m;              /* the one MMIO backend */
	struct mutex *sb_lock;             /* the one PCODE sideband lock */

	/* DRAM info carried from P2 (BW_BUDDY table lookup). */
	int dram_type;                     /* enum parity_dram_type */
	unsigned dram_channels;

	/* Live state (not re-initialised per child). */
	int initializing;
	uint8_t dbuf_enabled_slices;
	int init_wakeref_held;             /* POWER_DOMAIN_INIT domain reference held */
	int pm_wakeref;                    /* runtime-PM side of the init wakeref */

	/* Diagnostics. */
	int fault_stop;                    /* 1 once an adaptation-layer fault stopped us */
	const char *fault_where;           /* child at which we stopped */
	int reached_init_ref;
	int reached_sync_hw;
	int last_child;                    /* ordinal of the last child entered */
};

/* intel_power_domains_init_hw(resume): the whole sequence above. */
void parity_intel_power_domains_init_hw(struct parity_display_core *dc, int resume);

/* intel_power_domains_driver_remove(): cancel the init rpm wakeref, keep wells. */
void parity_intel_power_domains_driver_remove(struct parity_display_core *dc);

/* Exposed for tests. */
uint8_t parity_enabled_dbuf_slices_mask(struct parity_display_core *dc);

#endif /* PARITY_DISPLAY_CORE_H */
