/*
 * WS031 Linux-parity — CDCLK init (see cdclk.c).
 *
 * Faithful port of the ADL-P CDCLK bring-up: intel_init_cdclk_hooks() selects
 * the platform table + callbacks (ADL-P D0 -> adlp_cdclk_table + tgl funcs);
 * intel_cdclk_init_hw() -> bxt_cdclk_init_hw() reads the pre-OS state
 * (bxt_get_cdclk via the DE PLL / CDCLK_CTL), sanitises it (bxt_sanitize_cdclk),
 * and only if the state is not a legal freq/VCO for this platform reprograms it
 * (bxt_set_cdclk, using the common PCODE mailbox).  This is intel_cdclk_init_hw,
 * NOT intel_cdclk_init (which reprograms to the min cdclk later in P3).
 */
#ifndef PARITY_CDCLK_H
#define PARITY_CDCLK_H

#include <stdint.h>

struct osdep_mmio;
struct mutex;

/* Display stepping ordinals (subset of enum intel_step; ordering is faithful). */
enum parity_display_step {
	PARITY_STEP_NONE = 0,
	PARITY_STEP_A0, PARITY_STEP_A1, PARITY_STEP_A2, PARITY_STEP_A3,
	PARITY_STEP_B0, PARITY_STEP_B1, PARITY_STEP_B2, PARITY_STEP_B3,
	PARITY_STEP_C0, PARITY_STEP_C1, PARITY_STEP_C2, PARITY_STEP_C3,
	PARITY_STEP_D0
};

/* intel_cdclk_vals: one row of a platform CDCLK table. */
struct parity_cdclk_vals {
	uint32_t refclk;   /* kHz */
	uint32_t cdclk;    /* kHz */
	uint8_t  divider;  /* CD2X divider (2/3/... informational) */
	uint8_t  ratio;    /* PLL ratio */
	uint16_t waveform; /* squash waveform (0 on ADL-P: no squash) */
};

/*
 * intel_cdclk_config: dev_priv->display.cdclk.hw plus computed targets.
 * vco special values: 0 = PLL off (bypass), ~0u = unknown (sanitize wants a
 * full PLL disable+enable).  Never treat ~0u as a real frequency.
 */
struct parity_cdclk_config {
	uint32_t ref;      /* refclk kHz */
	uint32_t vco;      /* PLL VCO kHz; 0 = off; ~0u = unknown/force */
	uint32_t cdclk;    /* cdclk kHz; 0 = force reprogram */
	uint32_t bypass;   /* bypass kHz */
	uint8_t  voltage_level;
};

enum parity_cdclk_funcs { PARITY_CDCLK_FUNCS_NONE = 0, PARITY_CDCLK_FUNCS_TGL };

struct parity_cdclk_dev {
	struct parity_cdclk_config hw;          /* current HW state (display.cdclk.hw) */
	const struct parity_cdclk_vals *table;  /* selected platform table */
	int funcs;                               /* enum parity_cdclk_funcs */
	int display_ver;                         /* 13 for ADL-P */
	int has_cdclk_crawl;                     /* ADL-P: 1 */
	int has_cdclk_squash;                    /* ADL-P: 0 */
	struct osdep_mmio *m;                    /* MMIO backend */
	struct mutex *sb_lock;                   /* PCODE sideband lock */

	/* Diagnostics captured across init_hw (observed_before ... state_after). */
	struct parity_cdclk_config diag_observed_before;
	struct parity_cdclk_config diag_sanitized;
	struct parity_cdclk_config diag_requested;
	int diag_no_change;            /* 1 = sanitize accepted HW; nothing reprogrammed */
	int diag_prepare_status;       /* PCODE prepare-for-change result (0 / -errno) */
	int diag_hw_sequence_reached;  /* 1 once the PLL/CDCLK_CTL writes ran */
	int diag_notify_status;        /* PCODE voltage-notify result */
};

/* Map an ADL-P PCI revision id to its display stepping (adlp_revids[]). */
int parity_adlp_display_step(uint8_t revid);

/* intel_init_cdclk_hooks: select the table + callbacks (ADL-P branch). */
void parity_intel_init_cdclk_hooks(struct parity_cdclk_dev *cd,
	int display_ver, int display_step, int is_alderlake_p);

/* Readout (bxt_get_cdclk) + intel_update_cdclk. */
void parity_bxt_get_cdclk(struct parity_cdclk_dev *cd, struct parity_cdclk_config *cfg);
void parity_intel_update_cdclk(struct parity_cdclk_dev *cd);

/* bxt_sanitize_cdclk: accept a legal pre-OS state, else force reprogram (0/~0). */
void parity_bxt_sanitize_cdclk(struct parity_cdclk_dev *cd);

/* bxt_set_cdclk: PCODE prepare -> PLL/CDCLK_CTL -> PCODE notify -> readout. */
void parity_bxt_set_cdclk(struct parity_cdclk_dev *cd,
	const struct parity_cdclk_config *cfg);

/* intel_cdclk_init_hw -> bxt_cdclk_init_hw: sanitize + reprogram-if-needed. */
void parity_intel_cdclk_init_hw(struct parity_cdclk_dev *cd);

/* Table accessor + calc helpers exposed for tests. */
const struct parity_cdclk_vals *parity_adlp_cdclk_table(void);
int parity_bxt_calc_cdclk(struct parity_cdclk_dev *cd, int min_cdclk);
int parity_bxt_calc_cdclk_pll_vco(struct parity_cdclk_dev *cd, int cdclk);
uint8_t parity_tgl_calc_voltage_level(int cdclk);
/* intel_update_max_cdclk(), the display version 11+ branch: 648000 kHz on a 24 MHz reference, else 652800 kHz */
uint32_t parity_intel_max_cdclk_freq(const struct parity_cdclk_dev *cd);

#endif /* PARITY_CDCLK_H */
