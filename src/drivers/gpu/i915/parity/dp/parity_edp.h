/*
 * WS031 Linux-parity — eDP panel bring-up, first stage: panel power sequencer
 * (PPS / VDD) ownership and the AUX channel, up to the sink's DPCD and EDID.
 *
 * The work is done by the reference's own functions (generated files in this
 * directory: intel_pps_port.c, intel_dp_aux_port.c, drm_dp_helper_port.c,
 * drm_edid_port.c) in the order of intel_edp_init_connector().  This header is
 * the zedBSD-facing surface: plain C types only, so the probe and the GPU-free
 * tests can use it without the reference's environment.
 *
 * Every hardware and time dependency goes through struct parity_dp_env, so the
 * same production code runs on the real GPU (parity_dp_kernel.c binds it to the
 * MMIO / wait / power-domain layers) and on the register model (dp_fake_hw.c).
 */
#ifndef PARITY_EDP_H
#define PARITY_EDP_H

#include <stdint.h>
#include <stddef.h>

struct parity_dp_env {
	void *ctx;
	uint32_t (*read32)(void *ctx, uint32_t reg);
	void (*write32)(void *ctx, uint32_t reg, uint32_t value);
	/* __intel_wait_for_register(): 0, or a negative errno (-ETIMEDOUT); *out (may be NULL) = last value */
	int (*wait_reg)(void *ctx, uint32_t reg, uint32_t mask, uint32_t value,
		unsigned fast_us, unsigned slow_ms, uint32_t *out);
	void (*sleep_us)(void *ctx, unsigned us);
	uint64_t (*now_ms)(void *ctx);                  /* monotonic */
	int (*power_get)(void *ctx, int domain);        /* enum parity_power_domain value; 0 = ok */
	void (*power_put)(void *ctx, int domain);
	/* intel_display_power_put_async(): the reference leaves the DP layer now; the power layer
	 * parks it, hands it back to the next get of that domain, or releases it ~100 ms later */
	void (*power_put_async)(void *ctx, int domain);
	/* the PPS mutex and the AUX hardware mutex (PARITY_DP_LOCK_*): real mutual exclusion */
	void (*lock)(void *ctx, int which);
	void (*unlock)(void *ctx, int which);
	/*
	 * delayed work (PARITY_DP_WORK_*): queue = 1 when newly queued (0: already pending);
	 * cancel = 1 when it was pending; with sync != 0 it also waits until the work body is
	 * not running -- the caller then must not hold a lock the body takes.  The backend runs
	 * the body by calling parity_edp_work_run(which) from a context that may sleep.
	 */
	int (*delayed_queue)(void *ctx, int which, unsigned delay_ms);
	int (*delayed_cancel)(void *ctx, int which, int sync);
	int (*delayed_pending)(void *ctx, int which);
	/* bookkeeping kept by the DP layer (read by the tests and the run log) */
	int power_refs[2];                              /* [0] DISPLAY_CORE, [1] the AUX domain */
	unsigned power_get_failures;
	unsigned power_put_underflows;
	unsigned sleeps;
	uint64_t slept_us;
	unsigned lock_errors;                           /* recursion / unlock of a free lock */
	unsigned async_puts;
};

#define PARITY_DP_LOCK_PPS 0
#define PARITY_DP_LOCK_AUX 1
#define PARITY_DP_WORK_VDD_OFF 0        /* edp_panel_vdd_work */

#define PARITY_EDP_MAX_EDID_BLOCKS 4u

/*
 * Result codes are the reference's negative errnos in LINUX numbering (zedBSD's
 * <errno.h> numbers differ): rc == -PARITY_EDP_ETIMEDOUT and so on.
 */
#define PARITY_EDP_EIO 5
#define PARITY_EDP_ENXIO 6
#define PARITY_EDP_E2BIG 7
#define PARITY_EDP_EBUSY 16
#define PARITY_EDP_EINVAL 22
#define PARITY_EDP_EPROTO 71
#define PARITY_EDP_ETIMEDOUT 110
#define PARITY_EDP_EREMOTEIO 121

struct parity_edp_config {
	int port;                 /* enum port: 0 = A */
	int aux_ch;               /* enum aux_ch from the VBT child: 0 = A */
	uint32_t rawclk_khz;
	/* the VBT panel's eDP power sequence (100 us units) and PPS / backlight controller index;
	 * all zero when the panel type is not known before the EDID (the reference's "early" state) */
	uint16_t t1_t3, t8, t9, t10, t11_t12;
	int bl_controller;
	int log_level;            /* 0 errors, 1 +info, 2 +debug */
};

struct parity_edp_pps_regs { uint32_t pp_status, pp_control, pp_on_delays, pp_off_delays; };

enum parity_edp_stage {
	PARITY_EDP_STAGE_NONE = 0,
	PARITY_EDP_STAGE_PPS_INIT,        /* intel_pps_init() */
	PARITY_EDP_STAGE_DPCD,            /* intel_edp_init_dpcd(): receiver caps + eDP display control */
	PARITY_EDP_STAGE_EDID,            /* drm_edid_read_ddc(): I2C-over-AUX */
	PARITY_EDP_STAGE_ACQUIRED,        /* waiting for parity_edp_init_late() */
	PARITY_EDP_STAGE_LATE,            /* intel_pps_init_late() done: VDD-off is scheduled */
	PARITY_EDP_STAGE_ENDED,
};

struct parity_edp_result {
	int stage;                        /* the last stage COMPLETED */
	int rc;                           /* 0, or the negative errno of the stage that failed */
	int failed_stage;

	int pps_idx, pps_valid;
	struct parity_edp_pps_regs before;       /* as found */
	struct parity_edp_pps_regs after_init;   /* after intel_pps_init() programmed the delays */
	struct parity_edp_pps_regs after_acquire;/* VDD is expected ON here */
	struct parity_edp_pps_regs after_end;    /* after intel_pps_vdd_off_sync() */
	int delay_power_up_ms, delay_power_down_ms, delay_power_cycle_ms, delay_bl_on_ms, delay_bl_off_ms;

	uint8_t dpcd[15];                 /* DP_RECEIVER_CAP_SIZE */
	int dpcd_ok;
	uint8_t edp_dpcd[3];              /* EDP_DISPLAY_CTL_CAP_SIZE, from 0x700 */
	int edp_dpcd_ok;
	uint8_t link_cfg[2];              /* DP_LINK_BW_SET / DP_LANE_COUNT_SET as found (not written) */
	int link_cfg_ok;
	uint8_t edid[128u * PARITY_EDP_MAX_EDID_BLOCKS];
	unsigned edid_blocks;             /* valid blocks read */
	unsigned edid_extensions;         /* the base block's extension count */
	int edid_ok;

	/* ownership at the end of each phase */
	int vdd_wanted, vdd_on_hw, vdd_wakeref_held, vdd_work_pending;
	int power_refs_core, power_refs_aux;
	unsigned power_get_failures, power_put_underflows;
	unsigned i2c_defers, i2c_nacks;
	unsigned log_errors;              /* drm_err / WARN lines the reference emitted */
	uint64_t elapsed_ms;
};

/*
 * intel_edp_init_connector() up to the EDID: intel_pps_init(), the DPCD caps,
 * the eDP display-control caps and the EDID.  On success the sink's VDD is left
 * FORCED ON (the reference keeps it on while "initializing"); the caller must
 * continue with parity_edp_init_late() or stop with parity_edp_end().  On
 * failure VDD has already been forced off (the reference's out_vdd_off path),
 * and parity_edp_end() must still be called to release the object.
 * Only one eDP may be live: -EBUSY otherwise.
 */
int parity_edp_begin(struct parity_dp_env *env, const struct parity_edp_config *cfg,
	struct parity_edp_result *res);

/*
 * intel_pps_init_late(): the panel's final VBT data (after the EDID-based panel
 * type lookup) replaces the delays, the registers are re-programmed and the
 * delayed VDD-off is scheduled.
 */
int parity_edp_init_late(const struct parity_edp_config *final_cfg, struct parity_edp_result *res);

/*
 * The body of a delayed work, called by the env's backend (worker thread, or the register
 * model's clock) when the work is due.  PARITY_DP_WORK_VDD_OFF = edp_panel_vdd_work():
 * takes the PPS lock, and forces VDD off unless someone wants it again by then.
 */
void parity_edp_work_run(int which);

/* Refreshes the ownership fields of `res` (VDD, worker, references) from the live eDP. */
void parity_edp_snapshot(struct parity_edp_result *res);

/*
 * Stop: intel_pps_vdd_off_sync() (cancels the delayed worker, forces VDD off,
 * returns the AUX power reference), then checks that nothing is still owned.
 * Returns 0 when every reference / lock / worker is released.
 */
int parity_edp_end(struct parity_edp_result *res);

/* A raw AUX read for tests and diagnostics (live eDP only): drm_dp_dpcd_read(). */
long parity_edp_dpcd_read(unsigned offset, uint8_t *buf, size_t size);

#endif /* PARITY_EDP_H */
