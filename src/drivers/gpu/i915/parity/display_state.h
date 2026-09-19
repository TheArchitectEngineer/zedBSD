/*
 * WS031 Linux-parity — P3 tail: intel_mode_config_init() .. intel_fbc_init().
 *
 * Ports the remainder of intel_display_driver_probe_noirq() that follows the
 * (asynchronous) DMC load and the modeset/flip workqueues:
 *
 *   intel_mode_config_init(i915);          -- void
 *   intel_cdclk_init(i915);                -- global state object
 *   intel_color_init(i915);                -- DISPLAY_VER == 10 only
 *   intel_dbuf_init(i915);                 -- global state object
 *   intel_bw_init(i915);                   -- global state object + forced SAGV disable
 *   intel_pmdemand_init(i915);             -- global state object
 *   intel_init_quirks(i915);               -- void
 *   intel_fbc_init(i915);                  -- void
 *
 * Four of them register an intel_global_obj on display.global.obj_list, so the
 * shared mechanism (intel_atomic_global_obj_init) is ported once here and the
 * insertion ORDER is observable: cdclk, dbuf, bw, pmdemand.
 *
 * Note the two distinct "bw" states the reference keeps, which the port also
 * keeps apart:
 *   - display.bw.max[] / sagv.status  -- the PCODE-probed HW table from P2,
 *     already owned by struct parity_bw_state (dram_bw.h).
 *   - struct intel_bw_state           -- the ATOMIC global state object created
 *     here, holding qgv_points_mask (parity_bw_obj_state below).
 *
 * The reference kzalloc()s each state object and propagates -ENOMEM; the port
 * uses device-owned storage behind an explicit allocator so those failure paths
 * stay reachable and testable.
 */
#ifndef PARITY_DISPLAY_STATE_H
#define PARITY_DISPLAY_STATE_H

#include <stdint.h>
#include <kern/lock.h>
#include "backend_sync.h"

struct osdep_mmio;
struct mutex;
struct parity_bw_state;

/* ---------------------------------------------------------------- *
 * intel_global_state.h: intel_global_obj / intel_global_state
 * ---------------------------------------------------------------- */

struct parity_global_obj;

struct parity_global_state {
	struct parity_global_obj *obj;
	unsigned ref;                 /* kref_init() -> 1 */
	int changed;
};

struct parity_global_state_funcs {
	struct parity_global_state *(*duplicate_state)(struct parity_global_obj *obj);
	void (*destroy_state)(struct parity_global_obj *obj,
		struct parity_global_state *state);
};

struct parity_global_obj {
	struct parity_global_obj *next;      /* list_head, appended at the TAIL */
	struct parity_global_state *state;
	const struct parity_global_state_funcs *funcs;
	/*
	 * Diagnostic only; the reference's intel_global_obj has no name.  Set by
	 * the caller AFTER intel_atomic_global_obj_init (which memsets the obj).
	 */
	const char *name;
};

/* Per-user global state objects (the reference's *_state->base members). */
struct parity_cdclk_obj_state {
	struct parity_global_state base;
};
struct parity_dbuf_obj_state {
	struct parity_global_state base;
};
struct parity_bw_obj_state {
	struct parity_global_state base;
	uint16_t qgv_points_mask;
};
struct parity_pmdemand_obj_state {
	struct parity_global_state base;
};

/* ---------------------------------------------------------------- *
 * intel_mode_config_init()
 * ---------------------------------------------------------------- */

/*
 * The cursor/max-size ladders in intel_mode_config_init() branch on platforms
 * that predate everything this port targets.  They are kept as real branches
 * (so the ladder itself is exercised) and selected by this enum; ADL-P is
 * PARITY_PLAT_NONE and takes the modern "else" arms.
 */
enum parity_legacy_platform {
	PARITY_PLAT_NONE = 0,   /* ADL-P and every other modern part */
	PARITY_PLAT_I845G,
	PARITY_PLAT_I865G,
	PARITY_PLAT_I830,
	PARITY_PLAT_I85X,
	PARITY_PLAT_I915G,
	PARITY_PLAT_I915GM,
	PARITY_PLAT_BROADWELL,
	PARITY_PLAT_SKYLAKE,
	PARITY_PLAT_BROXTON
};

struct parity_mode_config {
	unsigned min_width, min_height;
	unsigned max_width, max_height;
	unsigned cursor_width, cursor_height;
	unsigned preferred_depth;
	int prefer_shadow;
	int async_page_flip;
	int funcs_set;             /* mode_config->funcs = &intel_mode_funcs */
	int helper_private_set;    /* ->helper_private = &intel_mode_config_funcs */
	int drm_mode_config_inited;
};

/* ---------------------------------------------------------------- *
 * intel_fbc_init()
 * ---------------------------------------------------------------- */

#define PARITY_MAX_FBCS 4      /* I915_MAX_FBCS */

enum parity_fbc_id {
	PARITY_FBC_A = 0,
	PARITY_FBC_B,
	PARITY_FBC_C,
	PARITY_FBC_D
};

/* Which intel_fbc_funcs vtable intel_fbc_create() selected. */
enum parity_fbc_funcs_kind {
	PARITY_FBC_FUNCS_NONE = 0,
	PARITY_FBC_FUNCS_I8XX,
	PARITY_FBC_FUNCS_I965,
	PARITY_FBC_FUNCS_G4X,
	PARITY_FBC_FUNCS_ILK,
	PARITY_FBC_FUNCS_SNB,
	PARITY_FBC_FUNCS_IVB
};

struct parity_fbc {
	int id;                            /* enum parity_fbc_id */
	int funcs_kind;                    /* enum parity_fbc_funcs_kind */
	struct parity_kwork underrun_work; /* INIT_WORK(&fbc->underrun_work, ...) */
	struct mutex lock;                 /* mutex_init(&fbc->lock) */
	int lock_inited;
	int in_use;
};

/* ---------------------------------------------------------------- *
 * intel_init_quirks()
 * ---------------------------------------------------------------- */

/* enum intel_quirk_id, values preserved (used as BIT(quirk)). */
enum parity_quirk_id {
	PARITY_QUIRK_BACKLIGHT_PRESENT = 0,
	PARITY_QUIRK_INCREASE_DDI_DISABLED_TIME,
	PARITY_QUIRK_INCREASE_T12_DELAY,
	PARITY_QUIRK_INVERT_BRIGHTNESS,
	PARITY_QUIRK_LVDS_SSC_DISABLE,
	PARITY_QUIRK_NO_PPS_BACKLIGHT_POWER_HOOK,
	PARITY_QUIRK_FW_SYNC_LEN
};

/* ---------------------------------------------------------------- *
 * Device-owned P3-tail display state
 * ---------------------------------------------------------------- */

struct parity_display_state {
	/* display.global.obj_list (INIT_LIST_HEAD in intel_mode_config_init) */
	struct parity_global_obj *obj_list_head;
	struct parity_global_obj *obj_list_tail;
	unsigned obj_count;
	int obj_list_inited;

	struct parity_mode_config mode_config;

	/* The four global state objects + their device-owned storage. */
	struct parity_global_obj cdclk_obj, dbuf_obj, bw_obj, pmdemand_obj;
	struct parity_cdclk_obj_state cdclk_state;
	struct parity_dbuf_obj_state dbuf_state;
	struct parity_bw_obj_state bw_obj_state;
	struct parity_pmdemand_obj_state pmdemand_state;

	/* intel_color_init */
	int color_done;                 /* ran and returned 0 */

	/* intel_bw_init / icl_force_disable_sagv */
	int sagv_force_disable_attempted;
	int sagv_pcode_ret;             /* icl_pcode_restrict_qgv_points() result */
	unsigned sagv_qgv_points;       /* icl_max_bw_qgv_point_mask() */
	unsigned sagv_psf_points;       /* icl_max_bw_psf_gv_point_mask() */

	/* intel_pmdemand_init */
	int pmdemand_wa_14016740474;    /* ver-14 A0..C0 WA applied (never on ADL-P) */

	/* intel_init_quirks */
	unsigned quirk_mask;            /* display.quirks.mask */
	unsigned quirk_hooks_fired;
	int dmi_scanned;                /* the DMI list was walked */
	int dmi_available;              /* a DMI backend answered */

	/* intel_fbc_init */
	int enable_fbc_param;           /* display.params.enable_fbc IN (-1 = auto) */
	int enable_fbc_sanitized;
	unsigned fbc_mask;              /* DISPLAY_RUNTIME_INFO()->fbc_mask, post-WA */
	int fbc_vtd_wa;                 /* need_fbc_vtd_wa() fired */
	struct parity_fbc *fbc[PARITY_MAX_FBCS];
	struct parity_fbc fbc_store[PARITY_MAX_FBCS];
	unsigned fbc_created;

	/* diagnostics */
	unsigned state_allocs;          /* successful global-state allocations */
	const char *fail_where;
	int inited;
};

/*
 * Test-only hook (production leaves it 0): when non-zero, the Nth global-state
 * allocation fails, so the reference's -ENOMEM propagation stays reachable.
 * N counts from 1 over the successful-allocation sequence.
 */
extern volatile unsigned parity_display_state_test_alloc_fail_at;

/*
 * Test-only hook (production leaves it 0): forces the result of the DMI system
 * match used by intel_init_quirks()'s second loop.  0 = no DMI backend.
 */
extern volatile int parity_display_state_test_dmi_match;

/* ---------------------------------------------------------------- *
 * The ported entry points, in reference call order
 * ---------------------------------------------------------------- */

void parity_intel_mode_config_init(struct parity_display_state *d,
	int display_ver, int legacy_platform);

int parity_intel_cdclk_init(struct parity_display_state *d);

/*
 * intel_color_init(): a no-op returning 0 unless DISPLAY_VER == 10, where the
 * reference builds a linear degamma LUT blob.  That branch needs DRM property
 * blobs, which this port does not have, so it returns -ENOSYS rather than a
 * silent success -- an unimplemented path must never look like a completed one.
 * ADL-P (ver 13) takes the early return and yields 0.
 */
int parity_intel_color_init(struct parity_display_state *d, int display_ver);

int parity_intel_dbuf_init(struct parity_display_state *d);

/*
 * intel_bw_init(): registers the bw global state object and, when SAGV is
 * present and DISPLAY_VER is 11..13, performs the forced SAGV disable -- a real
 * PCODE transaction (ICL_PCODE_SAGV_DE_MEM_SS_CONFIG), not a no-op.  `bw` is
 * the P2-probed HW table (display.bw.max[] / sagv.status), which the SAGV point
 * masks are computed from and whose sagv.status the PCODE reply updates.
 */
int parity_intel_bw_init(struct parity_display_state *d, int display_ver,
	struct parity_bw_state *bw, struct mutex *sb_lock, struct osdep_mmio *m);

int parity_intel_pmdemand_init(struct parity_display_state *d, int display_ver,
	struct osdep_mmio *m, int ip_step);

void parity_intel_init_quirks(struct parity_display_state *d, uint16_t device,
	uint16_t subsystem_vendor, uint16_t subsystem_device);

void parity_intel_fbc_init(struct parity_display_state *d, int display_ver,
	unsigned fbc_mask, int vtd_active, int legacy_platform);

/* intel_atomic_global_obj_cleanup() + the FBC teardown, for probe unwind. */
void parity_intel_display_state_fini(struct parity_display_state *d);

/* ---------------------------------------------------------------- *
 * Helpers exposed for the GPU-free tests
 * ---------------------------------------------------------------- */

int parity_intel_has_sagv(int display_ver, int legacy_platform, int sagv_status);
uint16_t parity_icl_qgv_points_mask(const struct parity_bw_state *bw);
unsigned parity_icl_qgv_bw(const struct parity_bw_state *bw, int display_ver, int num_active_planes, int qgv_point);
unsigned parity_icl_max_bw_qgv_point_mask(const struct parity_bw_state *bw,
	int display_ver, int num_active_planes);
unsigned parity_icl_max_bw_psf_gv_point_mask(const struct parity_bw_state *bw);
int parity_icl_pcode_restrict_qgv_points(struct mutex *sb_lock,
	struct osdep_mmio *m, int display_ver, struct parity_bw_state *bw,
	uint32_t points_mask);
int parity_intel_sanitize_fbc_option(int display_ver, int legacy_platform,
	unsigned fbc_mask, int enable_fbc_param);

#endif /* PARITY_DISPLAY_STATE_H */
