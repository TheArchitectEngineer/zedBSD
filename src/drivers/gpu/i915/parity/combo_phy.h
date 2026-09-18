/*
 * WS031 Linux-parity — combo PHY init (see combo_phy.c).
 *
 * Ports intel_combo_phy_init() -> icl_combo_phys_init() for ADL-P (combo PHYs A
 * and B): for each PHY, verify the current state (icl_combo_phy_verify_state);
 * if it is already correct, skip re-initialisation; otherwise program PHY_MISC,
 * the DISPLAY_VER>=12 ODCC/DCC bits, the procmon reference values selected from
 * the read-back process/voltage info, IREFGEN on the master PHY, COMP_INIT and
 * CL_POWER_DOWN_ENABLE -- in the reference order.  Lane registers are read and
 * group registers are written (kept distinct).
 */
#ifndef PARITY_COMBO_PHY_H
#define PARITY_COMBO_PHY_H

#include <stdint.h>

struct osdep_mmio;
struct osdep_trace;

/* ADL-P combo PHYs. */
#define PARITY_COMBO_PHY_A  0u
#define PARITY_COMBO_PHY_B  1u
#define PARITY_COMBO_PHY_NUM 2u

/* intel_combo_phy_init(): initialise combo PHYs A and B (idempotent per PHY). */
/*
 * intel_combo_phy_init(): reference-void; returns 0 on success (never an error
 * count).  *initialised_out (optional) receives how many PHYs were (re)programmed
 * -- a diagnostic, NOT an error code -- so the parent must not treat >0 as failure.
 */
int parity_intel_combo_phy_init(struct osdep_mmio *m, struct osdep_trace *trace,
	unsigned *initialised_out);

/* Exposed for tests: verify a single combo PHY's state (1 = ok, 0 = needs init). */
int parity_combo_phy_verify_state(struct osdep_mmio *m, unsigned phy);
/* Program a single combo PHY (icl_combo_phys_init body for one PHY). */
void parity_combo_phy_init_one(struct osdep_mmio *m, unsigned phy);

#endif /* PARITY_COMBO_PHY_H */
