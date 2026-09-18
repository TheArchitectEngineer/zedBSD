/*
 * WS031 Linux-parity — P6-a / P6-b: intel_gt_init() up to the first submission.
 *
 * The purpose of P6 is that the GT ends up in the SAME register state Linux
 * 6.8.12 leaves it in, so the deferred EU/PS hang can be reasoned about with a
 * known-equal baseline.  Everything here is therefore expressed as an explicit
 * MANIFEST -- a list of (register, clear, set, kind) entries with the
 * workaround name attached -- that can be dumped, diffed against a Linux dump,
 * applied to hardware, and read back.
 *
 * Reference: gt/intel_workarounds.c (wa_init_start/wa_add/wa_list_apply),
 * gt/intel_mocs.c (get_mocs_settings/__init_mocs_table/init_l3cc_table),
 * gt/intel_gt.c (intel_gt_init_workarounds), gt/intel_rc6.c, gt/intel_rps.c.
 *
 * Decision (2026-09-18, expert): the big-bang tree's tables are transcribed
 * from Linux 7.1, a DIFFERENT kernel; the tables here are re-derived from the
 * 6.8.12 reference and any 7.1 difference is recorded rather than inherited.
 */
#ifndef PARITY_GT_INIT_H
#define PARITY_GT_INIT_H

#include <stdint.h>

struct osdep_mmio;
struct mutex;
struct parity_gt_mmio;
struct parity_engine;

#define PARITY_WA_MAX        48
#define PARITY_WL_MAX        16   /* RING_MAX_NONPRIV_SLOTS */
#define PARITY_MOCS_ENTRIES  64   /* GEN9_NUM_MOCS_ENTRIES */

/*
 * struct i915_wa.  The reference distinguishes four shapes and the apply and
 * verify passes behave differently for each, so the kind is explicit here
 * rather than inferred from the masks:
 *
 *  PLAIN      clr/set are real bits; apply does read-modify-write and the
 *             readback must match `set` within `read_mask`.
 *  MASKED     a "masked register": the upper 16 bits select which of the lower
 *             16 change.  No read is needed; the value IS (mask << 16) | bits.
 *  NO_VERIFY  written like PLAIN but the readback is not trustworthy
 *             (e.g. FF_MODE2 / Wa_1608008084, MISCCPCTL / Wa_14015795083).
 */
enum parity_wa_kind {
	PARITY_WA_PLAIN = 0,
	PARITY_WA_MASKED,
	PARITY_WA_NO_VERIFY
};

struct parity_wa {
	uint32_t reg;
	uint32_t clr;
	uint32_t set;
	uint32_t read_mask;
	int kind;            /* enum parity_wa_kind */
	int is_mcr;          /* MCR_REG(): a multicast write */
	const char *name;    /* "Wa_1606700617" etc, for the manifest */
};

struct parity_wa_list {
	struct parity_wa list[PARITY_WA_MAX];
	unsigned count;
	unsigned overflow;
	const char *name;
};

/* The result of applying a list to hardware. */
struct parity_wa_apply_result {
	unsigned written;
	unsigned skipped_unchanged;
	unsigned verified;
	unsigned mismatched;
	unsigned not_verifiable;
};

/* ---------------- workaround list builders (intel_workarounds.c) ---------------- */

/*
 * The wa_* helpers, with the reference's exact clr/set/read_mask semantics.
 * `is_mcr` marks an MCR_REG() (a multicast write); the reference keeps that in
 * a separate field because the apply pass uses a different accessor for it.
 */
void parity_wa_write_or(struct parity_wa_list *wal, uint32_t reg, uint32_t set,
	int is_mcr, const char *name);
void parity_wa_write_clr_set(struct parity_wa_list *wal, uint32_t reg,
	uint32_t clr, uint32_t set, int is_mcr, const char *name);
void parity_wa_write(struct parity_wa_list *wal, uint32_t reg, uint32_t set,
	int is_mcr, const char *name);
void parity_wa_masked_en(struct parity_wa_list *wal, uint32_t reg, uint32_t bits,
	int is_mcr, const char *name);
void parity_wa_masked_dis(struct parity_wa_list *wal, uint32_t reg, uint32_t bits,
	int is_mcr, const char *name);
void parity_wa_masked_field_set(struct parity_wa_list *wal, uint32_t reg,
	uint32_t mask, uint32_t val, int is_mcr, const char *name);
void parity_wa_add_no_verify(struct parity_wa_list *wal, uint32_t reg,
	uint32_t clr, uint32_t set, int is_mcr, const char *name);

/* ---------------- workaround lists ---------------- */

/* intel_gt_init_workarounds(): gt_tuning_settings + gen12_gt_workarounds_init. */
void parity_gt_init_workarounds(struct parity_wa_list *wal, int graphics_ver);
/*
 * The ADL-P build needs the GT state: icl_wa_init_mcr steers at the lowest
 * non-fused-off subslice, and Wa_14011060649 walks the VDBOX engines.
 */
void parity_gt_init_workarounds_adlp(struct parity_wa_list *wal,
	const struct parity_gt_mmio *g);

/* intel_engine_init_workarounds(): engine_fake_wa_init + rcs/xcs_engine_wa_init. */
void parity_engine_init_workarounds(struct parity_wa_list *wal,
	const struct parity_engine *e, int graphics_ver, unsigned mocs_uc_index);

/* intel_engine_init_ctx_wa(): the LRI-applied context workarounds. */
void parity_engine_init_ctx_wa(struct parity_wa_list *wal,
	const struct parity_engine *e, int graphics_ver, unsigned mocs_uc_index);

/* intel_engine_init_whitelist(): tgl_whitelist_build for GRAPHICS_VER 12. */
void parity_engine_init_whitelist(struct parity_wa_list *wal,
	const struct parity_engine *e, int graphics_ver);

/*
 * wa_list_apply().  PLAIN entries are read-modify-written and only written when
 * the value actually changes (or when clr is empty); MASKED entries are always
 * written.  When `verify` is set every entry that can be verified is read back
 * and compared -- the reference only does this under CONFIG_DRM_I915_DEBUG_GEM,
 * but the expert approved running it as a permanent diagnostic (it changes no
 * hardware state).
 */
void parity_wa_list_apply(const struct parity_wa_list *wal, struct osdep_mmio *m,
	int verify, struct parity_wa_apply_result *res);

/* intel_engine_apply_whitelist(): RING_FORCE_TO_NONPRIV slots, rest = NOPID. */
void parity_engine_apply_whitelist(const struct parity_wa_list *wal,
	const struct parity_engine *e, struct osdep_mmio *m);

/* Dump one list in manifest form (for the Linux-side diff). */
void parity_wa_list_dump(const struct parity_wa_list *wal, const char *what);

/* ---------------- MOCS ---------------- */

struct parity_mocs {
	uint32_t control[PARITY_MOCS_ENTRIES];
	uint16_t l3cc[PARITY_MOCS_ENTRIES];
	unsigned n_entries;
	unsigned uc_index;
	unsigned unused_entries_index;
	int valid;
};

/* get_mocs_settings(): gen12_mocs_table for ADL-P (NOT tgl_mocs_table). */
void parity_get_mocs_settings(struct parity_mocs *t, int graphics_ver);
/* intel_mocs_init(): global MOCS table + the L3CC (LNCFCMOCS) pairs. */
void parity_intel_mocs_init(const struct parity_mocs *t, struct osdep_mmio *m,
	unsigned *global_writes, unsigned *l3cc_writes);

/* ---------------- RC6 / RPS ---------------- */

struct parity_rc6 {
	int supported;
	int enabled;
	uint32_t ctl_enable;
	uint32_t pg_enable;
    int wa_disabled;              /* the diagnostic switch below suppressed it */
};

struct parity_rps {
	uint32_t rp0_freq, rp1_freq, min_freq, max_freq;
	uint32_t efficient_freq;
	int enabled;
	int pcode_ok;
};

/*
 * Diagnostic switch (production default 0 = apply RC6 / render power gating
 * exactly as the reference does).  Set to 1 to SKIP gen11_rc6_enable()'s
 * GEN9_PG_ENABLE and RC_CTL writes so an EU experiment can A/B render power
 * gating without changing the default behaviour.
 */
extern volatile int parity_gt_test_skip_rc6;

void parity_intel_rc6_init(struct parity_rc6 *rc6, struct osdep_mmio *m);
void parity_gen11_rc6_enable(struct parity_rc6 *rc6, struct osdep_mmio *m,
	const struct parity_gt_mmio *g);
void parity_intel_rps_init(struct parity_rps *rps, struct mutex *sb_lock,
	struct osdep_mmio *m);
void parity_intel_rps_enable(struct parity_rps *rps, struct osdep_mmio *m);

/* ---------------- the P6 entry points ---------------- */

struct parity_gt_init {
	struct parity_wa_list gt_wa;
	struct parity_wa_list engine_wa[6];
	struct parity_wa_list ctx_wa[6];
	struct parity_wa_list whitelist[6];
	struct parity_mocs mocs;
	struct parity_rc6 rc6;
	struct parity_rps rps;

	/* PAT (setup_private_pat -> tgl_setup_private_ppat) */
	int pat_programmed;

	/* apply results */
	struct parity_wa_apply_result gt_wa_applied;
	struct parity_wa_apply_result engine_wa_applied[6];
	unsigned mocs_global_writes, mocs_l3cc_writes;
	unsigned whitelist_writes;
	unsigned engines_resumed;

	int tables_built;
	int hw_programmed;
};

/*
 * P6-a: build every table.  No hardware is touched except the PCODE reads RPS
 * needs; everything else is pure construction.
 */
void parity_gt_init_tables(struct parity_gt_init *gi, const struct parity_gt_mmio *g,
	int graphics_ver, struct mutex *sb_lock, struct osdep_mmio *m);

/*
 * P6-b: program the hardware.  tgl_setup_private_ppat -> GT workarounds (+
 * verify) -> MOCS -> per-engine workarounds / whitelist / enable_execlists ->
 * RPS -> RC6.  The caller must hold forcewake (the reference wraps the whole of
 * intel_gt_init and intel_gt_init_hw in FORCEWAKE_ALL).
 */
/* intel_gt_init_hw(): PAT (see the .c), GT workarounds + verify, MOCS. */
void parity_gt_init_hw_core(struct parity_gt_init *gi,
	const struct parity_gt_mmio *g, struct osdep_mmio *m);

/* intel_engine_resume()'s first half: engine workarounds + whitelist. */
void parity_engine_apply_resume_wa(struct parity_gt_init *gi,
	const struct parity_gt_mmio *g, unsigned idx, struct osdep_mmio *m);

/* init_l3cc_table(): used globally and again by intel_mocs_init_engine(RCS). */
void parity_init_l3cc_table(const struct parity_mocs *t, struct osdep_mmio *m,
	unsigned *writes);

/* intel_rc6_sanitize(): PG_ENABLE, RC_CONTROL and RC_STATE to 0. */
void parity_intel_rc6_sanitize(struct parity_rc6 *rc6, struct osdep_mmio *m);

/* intel_rps_sanitize() -> rps_disable_interrupts(). */
void parity_intel_rps_sanitize(struct parity_rps *rps, struct osdep_mmio *m);

/* tgl_setup_private_ppat(): 8 PAT entries. */
void parity_tgl_setup_private_ppat(struct osdep_mmio *m, unsigned *writes);

#endif /* PARITY_GT_INIT_H */
