/*
 * WS031 Linux-parity — the eDP first stage bound to the real GPU
 * (parity_dp_kernel.c).  zedBSD project code.
 */
#ifndef PARITY_DP_KERNEL_H
#define PARITY_DP_KERNEL_H

#include <stdint.h>

struct osdep_mmio;
struct parity_power_domains;
struct parity_pw_ctx;
struct parity_vbt_state;
struct parity_dp_env;

struct parity_dp_kernel {
	struct osdep_mmio *mmio;
	struct parity_power_domains *pd;
	struct parity_pw_ctx *pwc;
	unsigned wait_timeouts;
	unsigned time_faults;
	uint64_t last_ms;
};

void parity_dp_kernel_bind(struct parity_dp_kernel *k, struct parity_dp_env *env);

/*
 * One real AUX acquisition, start to finish: PPS init, VDD, DPCD, EDID, late
 * init, then the stop path (VDD off, references returned).  Logs everything it
 * reads and the PPS registers at each step, compares the panel identity with
 * the capture made through Linux, and prints a verdict line.  0 = PASS.
 * Needs a real VBT (the eDP child is never taken from the defaults).
 */
int parity_edp_aux_test_run(struct osdep_mmio *mmio, struct parity_power_domains *pd,
	struct parity_pw_ctx *pwc, struct parity_vbt_state *vbt);

#endif /* PARITY_DP_KERNEL_H */
