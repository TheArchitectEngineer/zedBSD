/*
 * WS031 Linux-parity — the eDP first stage bound to the real kernel and GPU
 * (parity_dp_kernel.c).  zedBSD project code.
 *
 * struct parity_edp_device is the device-side owner of the panel: the locks,
 * the delayed-work threads, the env, the configuration and the RESULT of the
 * normal connector initialisation (DPCD, EDID, PPS delays).  It outlives the
 * call that created it: later display stages and the diagnostics read it, and
 * only parity_edp_device_fini() releases it.
 */
#ifndef PARITY_DP_KERNEL_H
#define PARITY_DP_KERNEL_H

#include <stdint.h>
#include <kern/lock.h>
#include "../backend_delayed.h"
#include "parity_edp.h"
#include "../lcd/parity_lcd_calc.h"

struct osdep_mmio;
struct parity_power_domains;
struct parity_pw_ctx;
struct parity_vbt_state;

/* the env's kernel backend: real mutexes, tick-driven sleeps, timer + worker threads */
struct parity_dp_kernel {
	struct osdep_mmio *mmio;
	struct parity_power_domains *pd;
	struct parity_pw_ctx *pwc;
	struct mutex locks[2];                  /* PARITY_DP_LOCK_* */
	struct parity_kworkqueue wq;            /* the display worker: VDD-off and async power put */
	struct parity_ktimerq tq;
	struct parity_kdelayed vdd_off_work;
	struct parity_kdelayed async_put_work;
	int sync_started;
	unsigned wait_timeouts, time_faults;
	unsigned tick_sleeps;                   /* ordinary sleeps: tick + wait queue (kern_usleep_range) */
	unsigned busy_sleeps;                   /* always 0 since E-110: no busy remainder is left */
	uint64_t tick_slept_us, busy_slept_us;
	uint64_t last_ms;
};

/* locks + threads only (what the GPU-free concurrency checks combine with the register model) */
int  parity_dp_kernel_sync_start(struct parity_dp_kernel *k);
void parity_dp_kernel_sync_stop(struct parity_dp_kernel *k);
void parity_dp_kernel_bind_sync(struct parity_dp_kernel *k, struct parity_dp_env *env);
/* everything: MMIO, waits, sleeps, clock, power domains (async put bound to `pd`), locks, works */
void parity_dp_kernel_bind(struct parity_dp_kernel *k, struct parity_dp_env *env);
void parity_dp_kernel_sleep_us(struct parity_dp_kernel *k, unsigned us);

struct parity_edp_device {
	int started;                            /* locks and threads exist */
	int connector_live;                     /* the normal initialisation succeeded; state retained */
	int init_rc, late_rc;
	struct parity_dp_kernel k;
	struct parity_dp_env env;
	struct parity_edp_config cfg;
	struct parity_edp_result res;           /* kept for the later display stages */
	struct parity_lcd_state lcd;            /* LCD-A: mode, link, M/N, PLL words computed from `res` */
	int lcd_rc;
	struct parity_lcd_words lcd_words;      /* transcoder A M/N, timing and pipe-source words (computed, not written) */
	int lcd_words_rc;
	struct parity_lcd_words lcd_cpu_words;  /* hsw_configure_cpu_transcoder's operations (computed, not written) */
	struct parity_lcd_words lcd_ddi_words;  /* TRANS_MSA_MISC, TRANS_DDI_FUNC_CTL2, TRANS_DDI_FUNC_CTL (computed, not written) */
	uint32_t lcd_ddi_buf_ctl;               /* intel_dp->DP as intel_ddi_init_dp_buf_reg leaves it */
	uint32_t ddi_buf_ctl_readout;           /* DDI_BUF_CTL of the eDP port when the connector was initialised */
	int lcd_cpu_words_rc, lcd_ddi_words_rc;
	int vbt_bpp;
	struct parity_vbt_state *vbt;
};

void parity_edp_device_prepare(struct parity_edp_device *dev, struct osdep_mmio *mmio,
	struct parity_power_domains *pd, struct parity_pw_ctx *pwc, struct parity_vbt_state *vbt);

/*
 * The intel_ddi_init() -> intel_dp_init_connector() -> intel_edp_init_connector() step,
 * called from parity_intel_setup_outputs() for a DP-capable encoder.
 *   1  the port is not the VBT's eDP: nothing to do
 *   0  eDP initialised; the panel state stays in `dev`
 *  <0  eDP initialisation failed (the reference then drops the encoder)
 */
int parity_edp_device_init_connector(void *dev, int port);

/* Stop: VDD-off cancelled synchronously and forced, parked power references flushed, threads gone. */
void parity_edp_device_fini(struct parity_edp_device *dev);

/*
 * Diagnostics on the RESIDENT panel (build PARITY_AUX_TEST): compares what the normal
 * initialisation kept with the capture made through Linux, then exercises the delayed VDD-off
 * (runs by itself from the tick), a re-acquisition before its deadline, and leaves a fresh
 * reservation pending for the stop path to cancel.  0 = PASS.
 */
int parity_edp_aux_test_run(struct parity_edp_device *dev);

#endif /* PARITY_DP_KERNEL_H */
