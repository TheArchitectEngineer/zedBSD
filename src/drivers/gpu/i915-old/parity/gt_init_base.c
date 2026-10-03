/*
 * WS031 Linux-parity — P6-a / P6-b.  See gt_init.h.
 *
 * This file holds the MECHANISM (workaround-list construction and application,
 * MOCS table programming, PAT, RC6 and RPS).  The ADL-P workaround TABLES live
 * next to it in gt_wa_adlp.c, derived from the 6.8.12 reference.
 */
#include "gt_init.h"
#include "gt_mmio.h"
#include "pcode.h"
#include "osdep/mmio.h"
#include <kern/klog.h>
#include <errno.h>

volatile int parity_gt_test_skip_rc6;

/* ---------------- registers ---------------- */

#define GEN12_PAT_INDEX(i)          (0x4800u + (unsigned)(i) * 4u)
#define GEN8_PPAT_WB                (3u << 0)
#define GEN8_PPAT_WT                (2u << 0)
#define GEN8_PPAT_WC                (1u << 0)
#define GEN8_PPAT_UC                (0u << 0)

#define GEN12_GLOBAL_MOCS(i)        (0x4000u + (unsigned)(i) * 4u)
#define GEN9_LNCFCMOCS(i)           (0xb020u + (unsigned)(i) * 4u)

#define RING_FORCE_TO_NONPRIV(base, i) ((base) + 0x4d0u + (unsigned)(i) * 4u)
#define RING_NOPID(base)            ((base) + 0x94u)
#define RING_MAX_NONPRIV_SLOTS      12u

/* RC6 */
#define GEN6_RC_CONTROL             0xa090u
#define GEN6_RC_STATE               0xa094u
#define GEN6_PMINTRMSK              0xa168u
#define GEN6_RC6_WAKE_RATE_LIMIT    0xa09cu
#define GEN10_MEDIA_WAKE_RATE_LIMIT 0xa0a0u
#define GEN6_RC_EVALUATION_INTERVAL 0xa0a8u
#define GEN6_RC_IDLE_HYSTERSIS      0xa0acu
#define GEN6_RC_SLEEP               0xa0b0u
#define GEN6_RC6_THRESHOLD          0xa0b8u
#define GEN9_MEDIA_PG_IDLE_HYSTERESIS  0xa0c4u
#define GEN9_RENDER_PG_IDLE_HYSTERESIS 0xa0c8u
#define GEN9_PG_ENABLE              0xa210u
#define GEN9_RENDER_PG_ENABLE       (1u << 0)
#define GEN9_MEDIA_PG_ENABLE        (1u << 1)
#define GEN11_MEDIA_SAMPLER_PG_ENABLE (1u << 2)
#define VDN_HCP_POWERGATE_ENABLE(n) (1u << (3u + 2u * (unsigned)(n)))
#define VDN_MFX_POWERGATE_ENABLE(n) (1u << (4u + 2u * (unsigned)(n)))
#define GEN6_RC_CTL_RC6_ENABLE      (1u << 18)
#define GUC_MAX_IDLE_COUNT          0xc3e4u
#define RING_MAX_IDLE(base)         ((base) + 0x54u)

/* RPS */
#define MCHBAR_MIRROR_BASE_SNB      0x140000u
#define GEN6_RP_STATE_CAP           (MCHBAR_MIRROR_BASE_SNB + 0x5998u)
#define GEN10_FREQ_INFO_REC         (MCHBAR_MIRROR_BASE_SNB + 0x5ef0u)
#define RPE_MASK                    0x0000ff00u   /* GENMASK(15, 8) */
#define GEN9_FREQ_SCALER            3u
#define GEN6_RPNSWREQ               0xa008u
#define GEN9_FREQUENCY(x)           ((uint32_t)(x) << 23)
#define GEN6_RP_IDLE_HYSTERSIS      0xa070u
#define HSW_PCODE_DYNAMIC_DUTY_CYCLE_CONTROL 0x1au

#define I915_MAX_VCS 8u

/* ---------------- wa list construction (intel_workarounds.c) ---------------- */

static void
wa_add_entry(struct parity_wa_list *wal, uint32_t reg, uint32_t clr, uint32_t set,
	uint32_t read_mask, int kind, int is_mcr, const char *name)
{
	unsigned i;

	/* _wa_add() merges an entry for a register already in the list. */
	for (i = 0u; i < wal->count; i++) {
		if (wal->list[i].reg != reg || wal->list[i].is_mcr != is_mcr)
			continue;
		wal->list[i].clr |= clr;
		wal->list[i].set = (wal->list[i].set & ~clr) | set;
		wal->list[i].read_mask |= read_mask;
		if (kind == PARITY_WA_NO_VERIFY)
			wal->list[i].kind = PARITY_WA_NO_VERIFY;
		return;
	}

	if (wal->count >= (unsigned)PARITY_WA_MAX) {
		wal->overflow++;
		return;
	}
	wal->list[wal->count].reg = reg;
	wal->list[wal->count].clr = clr;
	wal->list[wal->count].set = set;
	wal->list[wal->count].read_mask = read_mask;
	wal->list[wal->count].kind = kind;
	wal->list[wal->count].is_mcr = is_mcr;
	wal->list[wal->count].name = name;
	wal->count++;
}

/* wa_write_or(): set bits, verify them. */
void
parity_wa_write_or(struct parity_wa_list *wal, uint32_t reg, uint32_t set,
	int is_mcr, const char *name)
{
	wa_add_entry(wal, reg, set, set, set, PARITY_WA_PLAIN, is_mcr, name);
}

/* wa_write_clr_set(): clear a field and set a value inside it. */
void
parity_wa_write_clr_set(struct parity_wa_list *wal, uint32_t reg, uint32_t clr,
	uint32_t set, int is_mcr, const char *name)
{
	wa_add_entry(wal, reg, clr, set, clr | set, PARITY_WA_PLAIN, is_mcr, name);
}

/* wa_write(): write a whole register value. */
void
parity_wa_write(struct parity_wa_list *wal, uint32_t reg, uint32_t set,
	int is_mcr, const char *name)
{
	wa_add_entry(wal, reg, 0xffffffffu, set, 0xffffffffu, PARITY_WA_PLAIN,
		is_mcr, name);
}

/*
 * wa_masked_en() / _dis() / _field_set(): a masked register takes the bits to
 * change in its upper half.  The reference stores clr=0, set=_MASKED_*(bits)
 * and marks the entry masked_reg so the apply pass does not read first and the
 * verify pass compares only the low half.
 */
void
parity_wa_masked_en(struct parity_wa_list *wal, uint32_t reg, uint32_t bits,
	int is_mcr, const char *name)
{
	wa_add_entry(wal, reg, 0u, (bits << 16) | bits, bits, PARITY_WA_MASKED,
		is_mcr, name);
}

void
parity_wa_masked_dis(struct parity_wa_list *wal, uint32_t reg, uint32_t bits,
	int is_mcr, const char *name)
{
	wa_add_entry(wal, reg, 0u, (bits << 16), bits, PARITY_WA_MASKED,
		is_mcr, name);
}

void
parity_wa_masked_field_set(struct parity_wa_list *wal, uint32_t reg,
	uint32_t mask, uint32_t val, int is_mcr, const char *name)
{
	wa_add_entry(wal, reg, 0u, (mask << 16) | val, mask, PARITY_WA_MASKED,
		is_mcr, name);
}

/* wa_add() with read_mask 0 = "write it, do not trust the readback". */
void
parity_wa_add_no_verify(struct parity_wa_list *wal, uint32_t reg, uint32_t clr,
	uint32_t set, int is_mcr, const char *name)
{
	wa_add_entry(wal, reg, clr, set, 0u, PARITY_WA_NO_VERIFY, is_mcr, name);
}

void
parity_wa_list_dump(const struct parity_wa_list *wal, const char *what)
{
	unsigned i;

	kern_logf("i915: parity WA-MANIFEST %s '%s': %u entries%s\n",
		what, wal->name != 0 ? wal->name : "?", wal->count,
		wal->overflow != 0u ? " (OVERFLOW)" : "");
	for (i = 0u; i < wal->count; i++) {
		const struct parity_wa *w = &wal->list[i];

		kern_logf("i915: parity WA %s reg=0x%05x clr=0x%08x set=0x%08x "
			"rmask=0x%08x kind=%d mcr=%d %s\n", what, w->reg, w->clr,
			w->set, w->read_mask, w->kind, w->is_mcr,
			w->name != 0 ? w->name : "");
	}
}

/*
 * wa_list_apply().  Open-coded read-modify-write: a masked register is written
 * unconditionally (its value carries its own mask), a plain one is only written
 * when the value actually changes or when there is nothing to clear.
 */
void
parity_wa_list_apply(const struct parity_wa_list *wal, struct osdep_mmio *m,
	int verify, struct parity_wa_apply_result *res)
{
	unsigned i;

	if (res != 0) {
		res->written = 0u; res->skipped_unchanged = 0u;
		res->verified = 0u; res->mismatched = 0u; res->not_verifiable = 0u;
	}

	for (i = 0u; i < wal->count; i++) {
		const struct parity_wa *w = &wal->list[i];
		uint32_t old = 0u, val;

		if (w->kind == PARITY_WA_MASKED) {
			val = w->set;
		} else {
			if (w->clr != 0u)
				old = osdep_mmio_read32(m, w->reg);
			val = (old & ~w->clr) | w->set;
		}

		if (w->kind == PARITY_WA_MASKED || val != old || w->clr == 0u) {
			osdep_mmio_write32(m, w->reg, val);
			if (res != 0) res->written++;
		} else if (res != 0) {
			res->skipped_unchanged++;
		}

		if (!verify)
			continue;

		if (w->read_mask == 0u) {
			/* wa_add(..., read_mask 0): the readback is not trusted. */
			if (res != 0) res->not_verifiable++;
			continue;
		}
		{
			uint32_t back = osdep_mmio_read32(m, w->reg);
			uint32_t want = (w->kind == PARITY_WA_MASKED)
				? (w->set & 0xffffu) : w->set;

			if ((back & w->read_mask) != (want & w->read_mask)) {
				kern_logf("i915: parity WA MISMATCH %s reg=0x%05x "
					"expected 0x%08x got 0x%08x (mask 0x%08x)\n",
					w->name != 0 ? w->name : "?", w->reg,
					want, back, w->read_mask);
				if (res != 0) res->mismatched++;
			} else if (res != 0) {
				res->verified++;
			}
		}
	}
}

void
parity_engine_apply_whitelist(const struct parity_wa_list *wal,
	const struct parity_engine *e, struct osdep_mmio *m)
{
	unsigned i;

	if (wal->count == 0u)
		return;

	for (i = 0u; i < wal->count && i < RING_MAX_NONPRIV_SLOTS; i++)
		osdep_mmio_write32(m, RING_FORCE_TO_NONPRIV(e->mmio_base, i),
			wal->list[i].reg | wal->list[i].set /* access flags */);

	/* And clear the rest just in case of garbage. */
	for (; i < RING_MAX_NONPRIV_SLOTS; i++)
		osdep_mmio_write32(m, RING_FORCE_TO_NONPRIV(e->mmio_base, i),
			RING_NOPID(e->mmio_base));
}

/* ---------------- intel_mocs.c ---------------- */

#define _LE_CACHEABILITY(v) ((uint32_t)(v) << 0)
#define _LE_TGT_CACHE(v)    ((uint32_t)(v) << 2)
#define LE_LRUM(v)          ((uint32_t)(v) << 4)
#define LE_AOM(v)           ((uint32_t)(v) << 6)
#define LE_RSC(v)           ((uint32_t)(v) << 7)
#define LE_SCC(v)           ((uint32_t)(v) << 8)
#define LE_SCF(v)           ((uint32_t)(v) << 14)
#define LE_SSE(v)           ((uint32_t)(v) << 17)
#define LE_0_PAGETABLE      _LE_CACHEABILITY(0)
#define LE_1_UC             _LE_CACHEABILITY(1)
#define LE_3_WB             _LE_CACHEABILITY(3)
#define LE_TC_1_LLC         _LE_TGT_CACHE(1)
#define _L3_CACHEABILITY(v) ((uint16_t)((v) << 4))
#define L3_1_UC             _L3_CACHEABILITY(1)
#define L3_3_WB             _L3_CACHEABILITY(3)

struct mocs_entry { unsigned idx; uint32_t control; uint16_t l3cc; };

/*
 * gen12_mocs_table = GEN11_MOCS_ENTRIES (2..23) plus 48..51, 60, 61, 62.
 * Entries 0 and 1 are "defined per-platform" and gen12 does NOT define them, so
 * they take unused_entries_index (2), as does every other undefined index.
 */
static const struct mocs_entry gen12_mocs_table[] = {
	{  2, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3), L3_3_WB },
	{  3, LE_1_UC | LE_TC_1_LLC,              L3_1_UC },
	{  4, LE_1_UC | LE_TC_1_LLC,              L3_3_WB },
	{  5, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3), L3_1_UC },
	{  6, LE_3_WB | LE_TC_1_LLC | LE_LRUM(1), L3_1_UC },
	{  7, LE_3_WB | LE_TC_1_LLC | LE_LRUM(1), L3_3_WB },
	{  8, LE_3_WB | LE_TC_1_LLC | LE_LRUM(2), L3_1_UC },
	{  9, LE_3_WB | LE_TC_1_LLC | LE_LRUM(2), L3_3_WB },
	{ 10, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_AOM(1), L3_1_UC },
	{ 11, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_AOM(1), L3_3_WB },
	{ 12, LE_3_WB | LE_TC_1_LLC | LE_LRUM(1) | LE_AOM(1), L3_1_UC },
	{ 13, LE_3_WB | LE_TC_1_LLC | LE_LRUM(1) | LE_AOM(1), L3_3_WB },
	{ 14, LE_3_WB | LE_TC_1_LLC | LE_LRUM(2) | LE_AOM(1), L3_1_UC },
	{ 15, LE_3_WB | LE_TC_1_LLC | LE_LRUM(2) | LE_AOM(1), L3_3_WB },
	{ 16, LE_1_UC | LE_TC_1_LLC | LE_SCF(1),  L3_1_UC },
	{ 17, LE_1_UC | LE_TC_1_LLC | LE_SCF(1),  L3_3_WB },
	{ 18, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_SSE(3), L3_3_WB },
	{ 19, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_SCC(7), L3_3_WB },
	{ 20, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_SCC(3), L3_3_WB },
	{ 21, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_SCC(1), L3_3_WB },
	{ 22, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_RSC(1) | LE_SCC(3), L3_3_WB },
	{ 23, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_RSC(1) | LE_SCC(7), L3_3_WB },
	{ 62, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3), L3_1_UC },
	{ 48, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3), L3_3_WB },
	{ 49, LE_1_UC | LE_TC_1_LLC,              L3_3_WB },
	{ 50, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3), L3_1_UC },
	{ 51, LE_1_UC | LE_TC_1_LLC,              L3_1_UC },
	{ 60, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3), L3_1_UC },
	{ 61, LE_1_UC | LE_TC_1_LLC,              L3_3_WB }
};

void
parity_get_mocs_settings(struct parity_mocs *t, int graphics_ver)
{
	unsigned i;
	uint32_t unused_control;
	uint16_t unused_l3cc;
	unsigned n = (unsigned)(sizeof(gen12_mocs_table) / sizeof(gen12_mocs_table[0]));

	for (i = 0u; i < (unsigned)PARITY_MOCS_ENTRIES; i++) {
		t->control[i] = 0u;
		t->l3cc[i] = 0u;
	}
	t->n_entries = 0u;
	t->uc_index = 0u;
	t->unused_entries_index = 0u;
	t->valid = 0;

	if (graphics_ver < 12) {
		kern_logf("i915: parity MOCS: only the gen12 table is ported\n");
		return;
	}

	/*
	 * ADL-P takes the "GRAPHICS_VER >= 12" arm -- gen12_mocs_table with
	 * uc_index 3 and unused_entries_index 2.  It is NOT tgl_mocs_table
	 * (that arm is IS_TIGERLAKE || IS_ROCKETLAKE only, kept for ABI).
	 */
	t->n_entries = (unsigned)PARITY_MOCS_ENTRIES;   /* GEN9_NUM_MOCS_ENTRIES */
	t->uc_index = 3u;
	t->unused_entries_index = 2u;

	/* Find the unused-entry values first: every gap takes them. */
	unused_control = 0u;
	unused_l3cc = 0u;
	for (i = 0u; i < n; i++)
		if (gen12_mocs_table[i].idx == t->unused_entries_index) {
			unused_control = gen12_mocs_table[i].control;
			unused_l3cc = gen12_mocs_table[i].l3cc;
		}

	for (i = 0u; i < (unsigned)PARITY_MOCS_ENTRIES; i++) {
		t->control[i] = unused_control;
		t->l3cc[i] = unused_l3cc;
	}
	for (i = 0u; i < n; i++) {
		unsigned idx = gen12_mocs_table[i].idx;

		if (idx >= (unsigned)PARITY_MOCS_ENTRIES)
			continue;
		t->control[idx] = gen12_mocs_table[i].control;
		t->l3cc[idx] = gen12_mocs_table[i].l3cc;
	}

	t->valid = 1;
}

void
parity_intel_mocs_init(const struct parity_mocs *t, struct osdep_mmio *m,
	unsigned *global_writes, unsigned *l3cc_writes)
{
	unsigned i;
	unsigned gw = 0u, lw = 0u;

	if (!t->valid)
		return;

	/* __init_mocs_table() at the GLOBAL MOCS offset (has_global_mocs). */
	for (i = 0u; i < t->n_entries; i++) {
		osdep_mmio_write32(m, GEN12_GLOBAL_MOCS(i), t->control[i]);
		gw++;
	}

	parity_init_l3cc_table(t, m, &lw);

	if (global_writes != 0) *global_writes = gw;
	if (l3cc_writes != 0) *l3cc_writes = lw;
}

void
parity_init_l3cc_table(const struct parity_mocs *t, struct osdep_mmio *m,
	unsigned *writes)
{
	unsigned i, lw = 0u;

	if (!t->valid)
		return;
	/* init_l3cc_table(): two 16-bit entries packed per register. */
	for (i = 0u; i < (t->n_entries + 1u) / 2u; i++) {
		uint32_t l3cc = (uint32_t)t->l3cc[2u * i] |
			((uint32_t)t->l3cc[2u * i + 1u] << 16);

		osdep_mmio_write32(m, GEN9_LNCFCMOCS(i), l3cc);
		lw++;
	}
	if (writes != 0)
		*writes = lw;
}

/* ---------------- setup_private_pat -> tgl_setup_private_ppat ---------------- */

void
parity_tgl_setup_private_ppat(struct osdep_mmio *m, unsigned *writes)
{
	/* TGL doesn't support LLC or AGE settings. */
	static const uint32_t pat[8] = {
		GEN8_PPAT_WB, GEN8_PPAT_WC, GEN8_PPAT_WT, GEN8_PPAT_UC,
		GEN8_PPAT_WB, GEN8_PPAT_WB, GEN8_PPAT_WB, GEN8_PPAT_WB
	};
	unsigned i;

	for (i = 0u; i < 8u; i++)
		osdep_mmio_write32(m, GEN12_PAT_INDEX(i), pat[i]);

	if (writes != 0) *writes = 8u;
}

/* ---------------- intel_rc6.c ---------------- */

void
parity_intel_rc6_init(struct parity_rc6 *rc6, struct osdep_mmio *m)
{
	/*
	 * rc6_supported(): HAS_RC6 and not vgpu/mock.  The GEN9_LP and MTL and
	 * media-A-step exclusions do not apply to ADL-P.
	 */
	rc6->supported = 1;
	rc6->enabled = 0;
	rc6->ctl_enable = 0u;
	rc6->pg_enable = 0u;
	rc6->wa_disabled = 0;

	/* Sanitize rc6: ensure it is disabled before we are ready. */
	osdep_mmio_write32(m, GEN6_RC_CONTROL, 0u);
}

void
parity_gen11_rc6_enable(struct parity_rc6 *rc6, struct osdep_mmio *m,
	const struct parity_gt_mmio *g)
{
	uint32_t pg_enable;
	unsigned i;

	if (!rc6->supported)
		return;

	if (parity_gt_test_skip_rc6) {
		/*
		 * Diagnostic only: the thresholds are still programmed, but the
		 * PG_ENABLE and RC_CONTROL writes that actually turn render power
		 * gating on are skipped, so an EU experiment can A/B it.
		 */
		rc6->wa_disabled = 1;
	}

	/* 2b: Program RC6 thresholds (no GuC RC). */
	osdep_mmio_write32(m, GEN6_RC6_WAKE_RATE_LIMIT, (54u << 16) | 85u);
	osdep_mmio_write32(m, GEN10_MEDIA_WAKE_RATE_LIMIT, 150u);
	osdep_mmio_write32(m, GEN6_RC_EVALUATION_INTERVAL, 125000u);
	osdep_mmio_write32(m, GEN6_RC_IDLE_HYSTERSIS, 25u);
	for (i = 0u; i < g->num_engines; i++)
		osdep_mmio_write32(m, RING_MAX_IDLE(g->engines[i].mmio_base), 10u);
	osdep_mmio_write32(m, GUC_MAX_IDLE_COUNT, 0xau);
	osdep_mmio_write32(m, GEN6_RC_SLEEP, 0u);
	osdep_mmio_write32(m, GEN6_RC6_THRESHOLD, 50000u);

	/* 2c: Coarse Power Gating policies. */
	osdep_mmio_write32(m, GEN9_MEDIA_PG_IDLE_HYSTERESIS, 60u);
	osdep_mmio_write32(m, GEN9_RENDER_PG_IDLE_HYSTERESIS, 60u);

	/* 3a: Enable RC6.  Without GuC RC the driver owns RC_CTL. */
	rc6->ctl_enable = GEN6_RC_CTL_RC6_ENABLE;

	pg_enable = GEN9_RENDER_PG_ENABLE | GEN9_MEDIA_PG_ENABLE |
		GEN11_MEDIA_SAMPLER_PG_ENABLE;

	/* GRAPHICS_VER >= 12 && !DG1: per-VCS HCP/MFX power gating. */
	for (i = 0u; i < I915_MAX_VCS; i++) {
		unsigned e;

		for (e = 0u; e < g->num_engines; e++)
			if (g->engines[e].class == PARITY_VIDEO_DECODE_CLASS &&
			    (unsigned)g->engines[e].instance == i) {
				pg_enable |= VDN_HCP_POWERGATE_ENABLE(i) |
					VDN_MFX_POWERGATE_ENABLE(i);
				break;
			}
	}
	rc6->pg_enable = pg_enable;

	if (rc6->wa_disabled) {
		kern_logf("i915: parity RC6/PG SKIPPED by the diagnostic switch "
			"(pg_enable would be 0x%x, ctl 0x%x)\n", pg_enable,
			rc6->ctl_enable);
		return;
	}

	osdep_mmio_write32(m, GEN9_PG_ENABLE, pg_enable);
	osdep_mmio_write32(m, GEN6_RC_CONTROL, rc6->ctl_enable);
	rc6->enabled = 1;
}

/* ---------------- intel_rps.c ---------------- */

void
parity_intel_rps_init(struct parity_rps *rps, struct mutex *sb_lock,
	struct osdep_mmio *m)
{
	uint32_t cap;
	uint32_t ddcc = 0u, ddcc1 = 0u;

	/* __gen6_rps_get_freq_caps(): not GEN9_LP, GRAPHICS_VER >= 10. */
	cap = osdep_mmio_read32(m, GEN6_RP_STATE_CAP);
	rps->rp0_freq = (cap >> 0) & 0xffu;
	rps->rp1_freq = (osdep_mmio_read32(m, GEN10_FREQ_INFO_REC) & RPE_MASK) >> 8;
	rps->min_freq = (cap >> 16) & 0xffu;

	/* GRAPHICS_VER >= 11: the caps are in 50MHz units, scale to 16.67MHz. */
	rps->rp0_freq *= GEN9_FREQ_SCALER;
	rps->rp1_freq *= GEN9_FREQ_SCALER;
	rps->min_freq *= GEN9_FREQ_SCALER;

	rps->max_freq = rps->rp0_freq;
	rps->efficient_freq = rps->rp1_freq;

	/* GRAPHICS_VER >= 11: refine the efficient frequency from the PCODE. */
	rps->pcode_ok = (parity_pcode_read(sb_lock, m,
		HSW_PCODE_DYNAMIC_DUTY_CYCLE_CONTROL, &ddcc, &ddcc1) == 0);
	if (rps->pcode_ok) {
		uint32_t eff = ((ddcc >> 8) & 0xffu) * GEN9_FREQ_SCALER;

		if (eff < rps->min_freq) eff = rps->min_freq;
		if (eff > rps->max_freq) eff = rps->max_freq;
		rps->efficient_freq = eff;
	}

	rps->enabled = 0;
}

void
parity_intel_rps_enable(struct parity_rps *rps, struct osdep_mmio *m)
{
	if (rps->max_freq <= rps->min_freq) {
		/* leave disabled, no room for dynamic reclocking */
		return;
	}

	/* gen9_rps_enable(): GRAPHICS_VER != 9 so no GEN6_RC_VIDEO_FREQ write. */
	osdep_mmio_write32(m, GEN6_RP_IDLE_HYSTERSIS, 0xau);

	/* rps_reset() -> rps_set(min_freq): gen6_rps_set -> GEN9_FREQUENCY. */
	osdep_mmio_write32(m, GEN6_RPNSWREQ, GEN9_FREQUENCY(rps->min_freq));

	rps->enabled = 1;
}

/* ---------------- P6-a: build every table ---------------- */

void
parity_gt_init_tables(struct parity_gt_init *gi, const struct parity_gt_mmio *g,
	int graphics_ver, struct mutex *sb_lock, struct osdep_mmio *m)
{
	unsigned i;

	/* intel_set_mocs_index() happens before the engines are set up, because
	 * engine_fake_wa_init() needs mocs.uc_index. */
	parity_get_mocs_settings(&gi->mocs, graphics_ver);

	parity_gt_init_workarounds_adlp(&gi->gt_wa, g);

	for (i = 0u; i < g->num_engines && i < 6u; i++) {
		parity_engine_init_workarounds(&gi->engine_wa[i], &g->engines[i],
			graphics_ver, gi->mocs.uc_index);
		parity_engine_init_whitelist(&gi->whitelist[i], &g->engines[i],
			graphics_ver);
		parity_engine_init_ctx_wa(&gi->ctx_wa[i], &g->engines[i],
			graphics_ver, gi->mocs.uc_index);
	}

	/* intel_gt_pm_init(): rc6 then rps. */
	parity_intel_rc6_init(&gi->rc6, m);
	parity_intel_rps_init(&gi->rps, sb_lock, m);

	gi->tables_built = 1;
}

/* ---------------- P6-b: program the hardware ---------------- */

void
parity_gt_init_hw_core(struct parity_gt_init *gi, const struct parity_gt_mmio *g,
	struct osdep_mmio *m)
{
	(void)g;
	/*
	 * setup_private_pat() happens in i915_gem_init(), before intel_gt_init;
	 * it is kept at the head of the P6 HW programming so it precedes
	 * everything that relies on the PAT index.
	 */
	{
		unsigned w = 0u;

		parity_tgl_setup_private_ppat(m, &w);
		gi->pat_programmed = (w == 8u);
	}

	/*
	 * intel_gt_init_hw(): apply the GT workarounds, then verify them.  The
	 * reference only verifies under CONFIG_DRM_I915_DEBUG_GEM; the expert
	 * approved running the readback permanently as a diagnostic (it changes
	 * no hardware state).
	 */
	parity_wa_list_apply(&gi->gt_wa, m, 1, &gi->gt_wa_applied);

	/*
	 * intel_gt_init_swizzling / init_unused_rings are gen<=3 only and
	 * i915_ppgtt_init_hw()'s gtt_write_workarounds has no gen12 arm, so on
	 * ADL-P nothing happens between the GT workarounds and MOCS.
	 * intel_uc_init_hw() is __uc_check_hw with enable_guc=0 -> 0.
	 */
	parity_intel_mocs_init(&gi->mocs, m, &gi->mocs_global_writes,
		&gi->mocs_l3cc_writes);
	gi->hw_programmed = 1;
}

void
parity_engine_apply_resume_wa(struct parity_gt_init *gi,
	const struct parity_gt_mmio *g, unsigned idx, struct osdep_mmio *m)
{
	if (idx >= g->num_engines || idx >= 6u)
		return;
	/* intel_engine_resume(): apply_workarounds, apply_whitelist, then ->resume. */
	parity_wa_list_apply(&gi->engine_wa[idx], m, 1, &gi->engine_wa_applied[idx]);
	parity_engine_apply_whitelist(&gi->whitelist[idx], &g->engines[idx], m);
	gi->whitelist_writes += 12u;   /* RING_MAX_NONPRIV_SLOTS */
	gi->engines_resumed++;
}

void
parity_intel_rc6_sanitize(struct parity_rc6 *rc6, struct osdep_mmio *m)
{
	/* An enabled rc6 here would be an unbalanced suspend/resume. */
	rc6->enabled = 0;
	if (!rc6->supported)
		return;
	/* __intel_rc6_disable(): GuC is off, so straight to the registers. */
	osdep_mmio_write32(m, GEN9_PG_ENABLE, 0u);
	osdep_mmio_write32(m, GEN6_RC_CONTROL, 0u);
	osdep_mmio_write32(m, GEN6_RC_STATE, 0u);
}

void
parity_intel_rps_sanitize(struct parity_rps *rps, struct osdep_mmio *m)
{
	(void)rps;
	/*
	 * rps_disable_interrupts(): PMINTRMSK takes rps_pm_sanitize_mask(~0),
	 * and pm_intrmsk_mbz is 0 on gen11+ (the REDIRECT_TO_GUC bit is gen8..10).
	 * The GPM IER/IMR updates that follow leave the values P4 programmed
	 * (IER 0, IMR ~0) unchanged, so they write nothing.
	 */
	osdep_mmio_write32(m, GEN6_PMINTRMSK, 0xffffffffu);
}
