/*
 * WS031 Linux-parity — display power domains (structure + map), see power_domains.c.
 *
 * Ports intel_power_domains_init(): option sanitize, allowed_dc_mask,
 * target_dc_state, lock, async-put work, and intel_display_power_map_init() --
 * the platform power-well descriptor set (ADL-P = display ver 13 -> xelpd, 30
 * wells) built into a power_wells[] array plus a per-domain -> wells map.  The
 * descriptor order, ids, hsw control indices and attributes are the reference
 * values (NOT inferred from array position).  Per-well refcount / hardware-
 * enabled / always_on are tracked SEPARATELY.  The power-well enable/disable/sync
 * MMIO is the init_hw (5.2) step; here the ops are connected by kind.
 *
 * DC state values are the reference register-bit encodings (not abstract flags).
 */
#ifndef PARITY_POWER_DOMAINS_H
#define PARITY_POWER_DOMAINS_H

#include <stdint.h>
#include <kern/lock.h>

struct osdep_trace;

/* enum intel_display_power_domain (v6.8 order preserved). */
enum parity_power_domain {
	PARITY_PW_DOMAIN_DISPLAY_CORE = 0,
	PARITY_PW_DOMAIN_PIPE_A, PARITY_PW_DOMAIN_PIPE_B,
	PARITY_PW_DOMAIN_PIPE_C, PARITY_PW_DOMAIN_PIPE_D,
	PARITY_PW_DOMAIN_PIPE_PANEL_FITTER_A, PARITY_PW_DOMAIN_PIPE_PANEL_FITTER_B,
	PARITY_PW_DOMAIN_PIPE_PANEL_FITTER_C, PARITY_PW_DOMAIN_PIPE_PANEL_FITTER_D,
	PARITY_PW_DOMAIN_TRANSCODER_A, PARITY_PW_DOMAIN_TRANSCODER_B,
	PARITY_PW_DOMAIN_TRANSCODER_C, PARITY_PW_DOMAIN_TRANSCODER_D,
	PARITY_PW_DOMAIN_TRANSCODER_EDP, PARITY_PW_DOMAIN_TRANSCODER_DSI_A,
	PARITY_PW_DOMAIN_TRANSCODER_DSI_C, PARITY_PW_DOMAIN_TRANSCODER_VDSC_PW2,
	PARITY_PW_DOMAIN_PORT_DDI_LANES_A, PARITY_PW_DOMAIN_PORT_DDI_LANES_B,
	PARITY_PW_DOMAIN_PORT_DDI_LANES_C, PARITY_PW_DOMAIN_PORT_DDI_LANES_D,
	PARITY_PW_DOMAIN_PORT_DDI_LANES_E, PARITY_PW_DOMAIN_PORT_DDI_LANES_F,
	PARITY_PW_DOMAIN_PORT_DDI_LANES_TC1, PARITY_PW_DOMAIN_PORT_DDI_LANES_TC2,
	PARITY_PW_DOMAIN_PORT_DDI_LANES_TC3, PARITY_PW_DOMAIN_PORT_DDI_LANES_TC4,
	PARITY_PW_DOMAIN_PORT_DDI_LANES_TC5, PARITY_PW_DOMAIN_PORT_DDI_LANES_TC6,
	PARITY_PW_DOMAIN_PORT_DDI_IO_A, PARITY_PW_DOMAIN_PORT_DDI_IO_B,
	PARITY_PW_DOMAIN_PORT_DDI_IO_C, PARITY_PW_DOMAIN_PORT_DDI_IO_D,
	PARITY_PW_DOMAIN_PORT_DDI_IO_E, PARITY_PW_DOMAIN_PORT_DDI_IO_F,
	PARITY_PW_DOMAIN_PORT_DDI_IO_TC1, PARITY_PW_DOMAIN_PORT_DDI_IO_TC2,
	PARITY_PW_DOMAIN_PORT_DDI_IO_TC3, PARITY_PW_DOMAIN_PORT_DDI_IO_TC4,
	PARITY_PW_DOMAIN_PORT_DDI_IO_TC5, PARITY_PW_DOMAIN_PORT_DDI_IO_TC6,
	PARITY_PW_DOMAIN_PORT_DSI, PARITY_PW_DOMAIN_PORT_CRT,
	PARITY_PW_DOMAIN_PORT_OTHER, PARITY_PW_DOMAIN_VGA,
	PARITY_PW_DOMAIN_AUDIO_MMIO, PARITY_PW_DOMAIN_AUDIO_PLAYBACK,
	PARITY_PW_DOMAIN_AUX_IO_A, PARITY_PW_DOMAIN_AUX_IO_B, PARITY_PW_DOMAIN_AUX_IO_C,
	PARITY_PW_DOMAIN_AUX_IO_D, PARITY_PW_DOMAIN_AUX_IO_E, PARITY_PW_DOMAIN_AUX_IO_F,
	PARITY_PW_DOMAIN_AUX_A, PARITY_PW_DOMAIN_AUX_B, PARITY_PW_DOMAIN_AUX_C,
	PARITY_PW_DOMAIN_AUX_D, PARITY_PW_DOMAIN_AUX_E, PARITY_PW_DOMAIN_AUX_F,
	PARITY_PW_DOMAIN_AUX_USBC1, PARITY_PW_DOMAIN_AUX_USBC2, PARITY_PW_DOMAIN_AUX_USBC3,
	PARITY_PW_DOMAIN_AUX_USBC4, PARITY_PW_DOMAIN_AUX_USBC5, PARITY_PW_DOMAIN_AUX_USBC6,
	PARITY_PW_DOMAIN_AUX_TBT1, PARITY_PW_DOMAIN_AUX_TBT2, PARITY_PW_DOMAIN_AUX_TBT3,
	PARITY_PW_DOMAIN_AUX_TBT4, PARITY_PW_DOMAIN_AUX_TBT5, PARITY_PW_DOMAIN_AUX_TBT6,
	PARITY_PW_DOMAIN_GMBUS, PARITY_PW_DOMAIN_GT_IRQ,
	PARITY_PW_DOMAIN_DC_OFF, PARITY_PW_DOMAIN_TC_COLD_OFF,
	PARITY_PW_DOMAIN_INIT,
	PARITY_PW_DOMAIN_NUM
};

/* DC state flags -- reference register-bit encodings (intel_dmc_regs / power). */
#define PARITY_DC_STATE_EN_UPTO_DC5   0x00000001u
#define PARITY_DC_STATE_EN_UPTO_DC6   0x00000002u
#define PARITY_DC_STATE_EN_DC9        0x00000008u
#define PARITY_DC_STATE_EN_DC3CO      0x40000000u

/* i915_power_well_id values (DISP_PW_ID_NONE must be 0). */
#define PARITY_DISP_PW_ID_NONE   0
#define PARITY_SKL_DISP_PW_1     8
#define PARITY_SKL_DISP_PW_2     9
#define PARITY_SKL_DISP_DC_OFF   11

/* Which ops family a well uses (bodies executed at init_hw). */
enum parity_pw_ops_kind {
	PARITY_PW_OPS_ALWAYS_ON = 0,
	PARITY_PW_OPS_HSW,        /* hsw_power_well_ops */
	PARITY_PW_OPS_ICL_DDI,    /* icl_ddi_power_well_ops */
	PARITY_PW_OPS_ICL_AUX,    /* icl_aux_power_well_ops */
	PARITY_PW_OPS_DC_OFF,     /* gen9_dc_off_power_well_ops */
};

/* 128-bit power-domain membership mask (PARITY_PW_DOMAIN_NUM <= 128). */
struct parity_pw_domain_mask {
	uint64_t bits[2];
};

/* A runtime power well (i915_power_well: desc data + separate live state). */
struct parity_power_well {
	const char *name;
	enum parity_pw_ops_kind ops;
	int always_on;
	int has_vga;
	int has_fuses;
	int is_tc_tbt;
	int fixed_enable_delay;
	unsigned enable_timeout;     /* ms; 0 = ops default */
	unsigned irq_pipe_mask;
	unsigned hsw_idx;            /* HSW/ICL power-well control index (reference) */
	int id;                     /* i915_power_well_id (0 = DISP_PW_ID_NONE) */
	struct parity_pw_domain_mask domains;
	int domains_all;            /* zero-length domain list = ALL domains */
	/* Live state, tracked separately from the descriptor: */
	unsigned refcount;          /* SW get/put reference count */
	int hw_enabled;             /* last known HW state (-1 = unknown until sync) */
};

#define PARITY_PW_MAX  40

/* i915_power_domains: the whole power-domain state for the device. */
struct parity_power_domains {
	uint32_t allowed_dc_mask;
	uint32_t target_dc_state;
	int disable_power_well;      /* sanitized option */
	struct mutex lock;
	int async_put_work_inited;   /* INIT_DELAYED_WORK(async_put_work) */

	struct parity_power_well power_wells[PARITY_PW_MAX];
	unsigned num_power_wells;
	/* per-domain -> bitmask of well indices that provide it. */
	uint64_t domain_wells[PARITY_PW_DOMAIN_NUM];

	int map_initialized;
	int initialized;
};

/* intel_power_domains_init(): sanitize options + build the power-well map. */
int parity_intel_power_domains_init(struct parity_power_domains *pd,
	unsigned display_ver, int enable_dc_param, int disable_pw_param,
	struct osdep_trace *trace);

/* intel_power_domains_cleanup(): release the map (wells left as the ref does). */
void parity_intel_power_domains_cleanup(struct parity_power_domains *pd);

/* Query: bitmask of wells that provide a domain (for tests/inspection). */
uint64_t parity_power_domain_wells(const struct parity_power_domains *pd,
	enum parity_power_domain domain);

/* Find a well index by its i915_power_well_id, or -1. */
int parity_power_well_by_id(const struct parity_power_domains *pd, int id);

struct osdep_mmio;
struct parity_vga_client;
struct parity_cdclk_dev;

/*
 * Context for the power-well operation bodies: the MMIO handle, the VGA client
 * (has_vga post-enable) and the IRQ-enabled gate (intel_irqs_enabled(); 0 before
 * P4).  The *_calls counters are diagnostics the GPU-free tests observe.
 */
struct parity_pw_ctx {
	struct osdep_mmio *mmio;
	struct parity_vga_client *vga;
	int irqs_enabled;
	unsigned vga_reset_calls;
	unsigned irq_post_enable_calls;
	unsigned ack_timeouts;   /* real HW ACK timeouts (warn+continue) */
	/* DC_off power-well enable (gen9_disable_dc_states) needs the shared CDCLK,
	 * the saved DBUF slice mask and the target DC state -- these are the SAME
	 * device objects the parent uses (not per-well copies). */
	struct parity_cdclk_dev *cd;
	uint8_t *dbuf_slices;          /* saved DBUF mask, for gen9_assert_dbuf_enabled */
	uint32_t target_dc_state;
	uint32_t allowed_dc_mask;
	unsigned dc_off_enable_calls;  /* diagnostics observed by the GPU-free tests */
	unsigned dc_off_cdclk_readouts;
	unsigned dc_off_dbuf_asserts;
	unsigned dc_off_combo_inits;
};

/* Low-level ops (drive HW); separate from the refcount get/put. */
int  parity_power_well_enable(struct parity_power_well *w, struct parity_pw_ctx *c);
int  parity_power_well_disable(struct parity_power_well *w, struct parity_pw_ctx *c);
int  parity_power_well_is_enabled(struct parity_power_well *w, struct parity_pw_ctx *c);
void parity_power_well_sync_hw(struct parity_power_well *w, struct parity_pw_ctx *c);
/* gen9_dc_off_power_well_enable -> gen9_disable_dc_states (implemented in display_core.c). */
void parity_dc_off_enable(struct parity_pw_ctx *c);

/* Reference-counted get/put: enable on 0->1, disable on 1->0. */
int  parity_power_well_get(struct parity_power_well *w, struct parity_pw_ctx *c);
void parity_power_well_put(struct parity_power_well *w, struct parity_pw_ctx *c);
/* Domain get/put: the domain's wells in ascending order (put reverse). */
int  parity_display_power_get(struct parity_power_domains *pd,
	enum parity_power_domain d, struct parity_pw_ctx *c);
void parity_display_power_put(struct parity_power_domains *pd,
	enum parity_power_domain d, struct parity_pw_ctx *c);

/*
 * __intel_display_power_is_enabled(): a domain counts as enabled when every one
 * of its wells is enabled, checked in REVERSE well order and against the CACHED
 * hw_enabled state (not a fresh HW read); always-on wells are skipped.  Returns
 * 1/0.  Used by the P4 IRQ reset/postinstall to skip pipes and transcoders whose
 * power is off.
 */
int parity_display_power_is_enabled(struct parity_power_domains *pd,
	enum parity_power_domain d, struct parity_pw_ctx *c);

/* pmdemand state: mutex + waitqueue, initialised early. */
struct parity_pmdemand {
	struct mutex lock;
	struct wait_queue waitqueue;
	int early_initialized;
};

/* intel_pmdemand_init_early(): initialise the pmdemand lock + wait queue. */
void parity_intel_pmdemand_init_early(struct parity_pmdemand *pm);

#endif /* PARITY_POWER_DOMAINS_H */
