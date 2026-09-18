/*
 * WS031 Linux-parity — P5: intel_display_driver_probe_nogem().
 *
 * P5-a covers the front section, up to (but not including) intel_setup_outputs():
 *
 *   intel_wm_init            -> skl_wm_init: intel_sagv_init + skl_setup_wm_latency
 *   intel_panel_sanitize_ssc -> LVDS/SSC; nothing to do on ADL-P
 *   intel_pps_setup          -> pps.mmio_base
 *   intel_gmbus_setup        -> gmbus.mmio_base, the ICP pin table, GMBUS reset
 *   intel_crtc_init x4       -> per-pipe CRTC + plane records
 *   intel_plane_possible_crtcs_init / intel_shared_dpll_init /
 *   intel_fdi_pll_freq_update / intel_update_czclk
 *   intel_display_driver_init_hw -> intel_update_cdclk + adlp_display_wa_apply
 *   intel_dpll_update_ref_clks / intel_hdcp_component_init /
 *   intel_update_max_cdclk / intel_hti_init / intel_vga_disable
 *
 * The DRM object model (drm_crtc / drm_plane / drm_encoder / drm_connector) is
 * NOT brought in; CRTCs, planes and shared DPLLs are the minimal parity records
 * the readout and sanitize phases (P5-c / P5-d) actually consume.
 *
 * Confirmed from the reference device tables rather than assumed, for ADL-P
 * (xe_lpd, DISPLAY_VER 13, PCH_ADP):
 *   - num_scalers[pipe] = 2, num_sprites[pipe] = 4  -> 6 planes/pipe
 *     (1 primary + 4 sprites + 1 cursor)
 *   - has_hti is NOT set on xe_lpd (it is RKL/ADL-S only) -> intel_hti_init is
 *     a no-op here; HDPORT_STATE is not read.
 *   - HAS_HW_SAGV_WM (ver >= 13 && !DGFX) is true -> wm.num_levels = 6, not 8.
 *   - intel_ddi_crt_present() returns false for DISPLAY_VER >= 9.
 */
#ifndef PARITY_DISPLAY_NOGEM_H
#define PARITY_DISPLAY_NOGEM_H

#include <stdint.h>
#include <kern/lock.h>
#include <kern/waitq.h>

struct osdep_mmio;
struct mutex;
struct parity_cdclk_dev;
struct parity_bw_state;
struct parity_vga_client;
struct parity_power_domains;
struct parity_pw_ctx;
struct parity_vbt_state;

#define PARITY_NOGEM_MAX_PIPES    4
#define PARITY_NOGEM_MAX_PLANES   6    /* primary + 4 sprites + cursor */
#define PARITY_NOGEM_MAX_DPLLS    8    /* adlp_plls has 7 + terminator */
#define PARITY_NOGEM_MAX_WM_LVL   8    /* I915_MAX_WM / skl_latency[] */
#define PARITY_NOGEM_MAX_GMBUS    15   /* pin indices 1..14 */
#define PARITY_NOGEM_MAX_ENCODERS 8

/* enum port (values preserved; PORT_TC1 aliases PORT_D). */
#define PARITY_PORT_NONE   (-1)
#define PARITY_PORT_A      0
#define PARITY_PORT_B      1
#define PARITY_PORT_C      2
#define PARITY_PORT_D      3
#define PARITY_PORT_TC1    3
#define PARITY_PORT_TC2    4
#define PARITY_PORT_TC3    5
#define PARITY_PORT_TC4    6

/* enum phy (values preserved). */
#define PARITY_PHY_A 0
#define PARITY_PHY_B 1
#define PARITY_PHY_C 2
#define PARITY_PHY_D 3
#define PARITY_PHY_E 4
#define PARITY_PHY_F 5
#define PARITY_PHY_I 8

/* Why intel_ddi_init() declined to create an encoder (diagnostic). */
enum parity_ddi_skip {
	PARITY_DDI_OK = 0,
	PARITY_DDI_SKIP_PORT_NONE,
	PARITY_DDI_SKIP_STRAP,
	PARITY_DDI_SKIP_PORT_INVALID,
	PARITY_DDI_SKIP_PORT_IN_USE,
	PARITY_DDI_SKIP_DSI,
	PARITY_DDI_SKIP_HTI,
	PARITY_DDI_SKIP_NOT_DVI_HDMI_DP,
	PARITY_DDI_SKIP_EDP_INIT_FAILED     /* intel_ddi_init_dp_connector() failed: the encoder is dropped */
};

/* Which clock vtable intel_ddi_init() selected. */
enum parity_ddi_clk {
	PARITY_DDI_CLK_NONE = 0,
	PARITY_DDI_CLK_ICL_COMBO,
	PARITY_DDI_CLK_ICL_TC
};

/* intel_encoder: the subset the readout and sanitize phases consume. */
struct parity_encoder {
	int port;               /* enum port */
	int phy;                /* enum phy */
	int is_tc;
	int clk_funcs;          /* enum parity_ddi_clk */
	int power_domain;       /* POWER_DOMAIN_PORT_DDI_LANES_* */
	int init_hdmi, init_dp;
	uint8_t dvo_port;
	uint32_t device_type;
	/* readout (P5-c) */
	int crtc_linked;        /* encoder->base.crtc != NULL */
	unsigned pipe_mask;
	int is_mst;
	int in_use;
};

/* intel_plane: the subset the readout/sanitize phases use. */
enum parity_plane_type {
	PARITY_PLANE_PRIMARY = 0,
	PARITY_PLANE_SPRITE,
	PARITY_PLANE_CURSOR
};

struct parity_plane {
	int id;                 /* enum plane_id: PLANE_PRIMARY=0, SPRITE0.., CURSOR */
	int type;               /* enum parity_plane_type */
	unsigned pipe;
	int visible;            /* plane_state->uapi.visible (readout, P5-c) */
	int in_use;
};

/* intel_crtc + the parts of intel_crtc_state P5 fills in. */
struct parity_crtc_state {
	int active;             /* hw.active */
	int enable;             /* hw.enable */
	int cpu_transcoder;     /* -1 = INVALID_TRANSCODER */
	int inherited;
	unsigned active_planes;
	/* readout results (P5-c) */
	uint32_t transconf;
	uint32_t trans_ddi_func_ctl;
	unsigned enabled_transcoders;   /* hsw_enabled_transcoders() bitmask */
	/* intel_get_transcoder_timings() */
	unsigned hdisplay, htotal, vdisplay, vtotal;
	unsigned pipe_src_w, pipe_src_h;
	int power_gated;                /* the pipe/transcoder power was off */
};

struct parity_crtc {
	unsigned pipe;
	unsigned num_scalers;
	unsigned plane_ids_mask;
	int active;             /* crtc->active */
	int enabled;            /* crtc->base.enabled */
	struct parity_plane planes[PARITY_NOGEM_MAX_PLANES];
	unsigned num_planes;
	struct parity_crtc_state state;
	int fifo_underrun_reporting;
	int in_use;
};

/* intel_shared_dpll (adlp_plls). */
enum parity_dpll_funcs {
	PARITY_DPLL_FUNCS_NONE = 0,
	PARITY_DPLL_FUNCS_COMBO,
	PARITY_DPLL_FUNCS_TBT,
	PARITY_DPLL_FUNCS_DKL
};

struct parity_dpll {
	const char *name;
	int id;                 /* enum intel_dpll_id */
	int funcs;              /* enum parity_dpll_funcs */
	unsigned index;
	uint32_t enable_reg;
	int on;                 /* readout (P5-c) */
	unsigned pipe_mask;
	unsigned active_mask;
};

/* One ICP gmbus pin (the adapter itself needs an i2c core we do not have). */
struct parity_gmbus_pin {
	const char *name;
	unsigned gpio;
	unsigned reg0;          /* pin | GMBUS_RATE_100KHZ */
	int present;
};

struct parity_display_nogem {
	/* intel_wm_init -> skl_wm_init */
	unsigned wm_num_levels;
	uint16_t wm_skl_latency[PARITY_NOGEM_MAX_WM_LVL];
	int wm_latency_valid;
	int sagv_status;                /* mirrors display.sagv.status */
	uint32_t sagv_block_time_us;

	/* intel_pps_setup / intel_gmbus_setup */
	uint32_t pps_mmio_base;
	uint32_t gmbus_mmio_base;
	struct mutex gmbus_lock;
	struct wait_queue gmbus_waitq;
	struct parity_gmbus_pin gmbus_pins[PARITY_NOGEM_MAX_GMBUS];
	unsigned gmbus_pins_present;
	int gmbus_adapters_unimplemented;   /* i2c core absent: recorded, not faked */

	/* CRTCs / planes / DPLLs */
	struct parity_crtc crtcs[PARITY_NOGEM_MAX_PIPES];
	unsigned num_crtcs;
	struct parity_dpll dplls[PARITY_NOGEM_MAX_DPLLS];
	unsigned num_dplls;
	int dpll_mgr_present;
	struct mutex dpll_lock;
	uint32_t dpll_ref_nssc;

	/* cdclk-derived */
	uint32_t max_cdclk_freq;
	int cdclk_logical_set;          /* cdclk_state->logical = actual = hw */

	/* WAs / misc */
	int adlp_wa_applied;
	int hti_state_read;             /* has_hti is 0 on xe_lpd -> stays 0 */
	uint32_t hti_state;
	int fdi_pll_freq_updated;       /* ADL-P: the function returns early */
	int czclk_updated;              /* ADL-P: the function returns early */
	int hdcp_component_unimplemented;

	/* intel_vga_disable */
	int vga_already_disabled;
	int vga_disable_done;

	/* intel_setup_outputs (P5-b) */
	struct parity_encoder encoders[PARITY_NOGEM_MAX_ENCODERS];
	unsigned num_encoders;
	unsigned ddi_init_calls;
	unsigned ddi_skipped;
	int ddi_skip_reason[PARITY_NOGEM_MAX_ENCODERS];
	int ddi_skip_port[PARITY_NOGEM_MAX_ENCODERS];
	unsigned num_ddi_skips;
	int crt_present;
	int outputs_done;
	/*
	 * intel_ddi_init() -> intel_ddi_init_dp_connector() -> intel_dp_init_connector() ->
	 * intel_edp_init_connector(), for a DP-capable encoder: 1 = not an eDP port (nothing
	 * done), 0 = eDP initialised and KEPT by the hook's owner, < 0 = failed (the reference
	 * then frees the encoder).  NULL = no connector construction (the pre-E-109 behaviour).
	 */
	int (*dp_connector_init)(void *ctx, int port);
	void *dp_connector_ctx;
	int edp_port;                       /* port whose eDP connector is live, or -1 */
	int edp_init_rc;

	/* intel_modeset_readout_hw_state (P5-c) */
	unsigned active_pipes;          /* cdclk_state/dbuf_state->active_pipes */
	unsigned readout_crtcs;
	unsigned readout_planes_visible;
	unsigned readout_encoders_linked;
	unsigned readout_dplls_on;
	int readout_done;
	/*
	 * Fields the reference reads that this port deliberately does NOT: color
	 * config, DSC, VRR, bigjoiner, scaler detail, output_format, linetime,
	 * pixel_multiplier, framestart_delay.  None of them feeds a sanitize
	 * decision, and inventing them would be worse than recording the gap.
	 */
	int readout_detail_unimplemented;

	/* intel_modeset_setup_hw_state, sanitize half (P5-d) */
	int early_display_was_applied;   /* IS_DISPLAY_VER(10,12) only -> 0 on ADL-P */
	int pch_sanitize_applied;        /* HAS_PCH_IBX only -> 0 */
	int plane_mapping_sanitized;     /* DISPLAY_VER >= 4 returns -> 0 */
	unsigned vblank_resets;
	unsigned dmc_pipes_enabled;
	unsigned vblank_on_count;
	unsigned fbc_deactivated;
	unsigned crtcs_disabled_noatomic;
	unsigned encoder_clocks_gated;
	unsigned dplls_disabled;
	int cmtg_wa_applied;             /* ADL-P A0..B0 + DPLL0 only -> 0 on D0 */
	unsigned wells_disabled;
	int wm_hw_state_read;
	/*
	 * intel_crtc_disable_noatomic() would need the full modeset disable path
	 * (hsw_crtc_disable: encoder post_disable, DDI, DPLL, pipe, DBUF).  It is
	 * only reachable from an ACTIVE pipe with no encoders; when that happens
	 * this flag is raised instead of pretending the pipe was disabled.
	 */
	int crtc_disable_noatomic_unimplemented;
	int sanitize_done;

	/* diagnostics */
	unsigned mmio_writes;
	const char *fail_where;
	int inited;
};

/*
 * P5-a: everything from intel_wm_init() up to (not including)
 * intel_setup_outputs().  Returns 0, or a negative errno from the first step
 * the reference propagates one from (intel_gmbus_setup).
 */
int parity_intel_display_nogem_front(struct parity_display_nogem *d,
	int display_ver, unsigned pipe_mask, struct osdep_mmio *m,
	struct mutex *sb_lock, struct parity_cdclk_dev *cd,
	struct parity_bw_state *bw, struct parity_vga_client *vga);

void parity_intel_display_nogem_fini(struct parity_display_nogem *d);

/* Pieces exposed for the GPU-free tests. */
void parity_skl_setup_wm_latency(struct parity_display_nogem *d, int display_ver,
	struct mutex *sb_lock, struct osdep_mmio *m, int wm_lv_0_adjust_needed);
void parity_adjust_wm_latency(uint16_t wm[], int num_levels, int read_latency,
	int wm_lv_0_adjust_needed);
void parity_intel_shared_dpll_init(struct parity_display_nogem *d, int display_ver,
	int is_alderlake_p);
int  parity_intel_crtc_init(struct parity_display_nogem *d, int display_ver,
	unsigned pipe);
void parity_intel_update_max_cdclk(struct parity_display_nogem *d, int display_ver,
	uint32_t cdclk_ref);
void parity_adlp_display_wa_apply(struct parity_display_nogem *d,
	struct osdep_mmio *m, int display_ver, int is_alderlake_p);

/*
 * P5-b: intel_setup_outputs().  HAS_DDI -> intel_ddi_crt_present() (false for
 * DISPLAY_VER >= 9) -> intel_bios_for_each_encoder(intel_ddi_init).
 *
 * Per the approved scope this stops at the DECISION + encoder record: the DRM
 * encoder registration, the DP/HDMI sub-init, AUX, HPD and connector creation
 * are NOT done.  That is enough for the readout and for
 * intel_sanitize_encoder(), which keys off the encoder<->crtc link, but it does
 * mean the connector-driven sanitize arms cannot fire (recorded, not hidden).
 */
void parity_intel_setup_outputs(struct parity_display_nogem *d, int display_ver,
	unsigned port_mask, const struct parity_vbt_state *vbt,
	struct osdep_mmio *m);

/* Pieces exposed for the tests. */
int parity_dvo_port_to_port(int display_ver, uint8_t dvo_port);
int parity_intel_port_to_phy(int display_ver, int port);
int parity_intel_phy_is_tc(int display_ver, int phy);
int parity_intel_ddi_is_tc(int display_ver, int port);
int parity_intel_ddi_crt_present(int display_ver);
/* intel_ddi_get_hw_state() / icl_ddi_*_is_clock_enabled(), for P5-c / P5-d. */
int parity_intel_ddi_get_hw_state(struct parity_display_nogem *d,
	struct parity_encoder *e, struct osdep_mmio *m, unsigned pipe_mask_avail,
	unsigned *pipe_out);
int parity_intel_ddi_is_clock_enabled(struct parity_encoder *e,
	struct osdep_mmio *m);
void parity_intel_ddi_disable_clock(struct parity_display_nogem *d,
	struct parity_encoder *e, struct osdep_mmio *m);

/*
 * P5-c: intel_modeset_readout_hw_state().  Reads the CRTC/plane/encoder/DPLL
 * hardware state under the same power gating the reference uses.  Writes
 * nothing.  `pd`/`pwc` provide the power-domain query.
 */
void parity_intel_modeset_readout_hw_state(struct parity_display_nogem *d,
	int display_ver, struct osdep_mmio *m, struct parity_power_domains *pd,
	struct parity_pw_ctx *pwc);

unsigned parity_hsw_enabled_transcoders(struct parity_display_nogem *d,
	int display_ver, unsigned pipe, struct osdep_mmio *m,
	struct parity_power_domains *pd, struct parity_pw_ctx *pwc);

/*
 * P5-d: the sanitize half of intel_modeset_setup_hw_state().  Runs after the
 * readout and turns hardware the pre-OS left in an inconsistent or unused state
 * back off: unused shared DPLLs, unused power wells, an active FBC, an ungated
 * DDI clock on a disabled encoder.  `display_step` gates the ADL-P A0 CMTG WA.
 */
void parity_intel_modeset_sanitize_hw_state(struct parity_display_nogem *d,
	int display_ver, int display_step, unsigned fbc_mask, struct osdep_mmio *m,
	struct parity_power_domains *pd, struct parity_pw_ctx *pwc);

#endif /* PARITY_DISPLAY_NOGEM_H */
