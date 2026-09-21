/*
 * WS031 Linux-parity — P6-0a: the rest of intel_gt_init_mmio().  See gt_mmio.h.
 */
#include "gt_mmio.h"
#include "osdep/mmio.h"
#include <kern/klog.h>
#include <errno.h>

/* ---------------- registers (i915_reg.h / gt/intel_gt_regs.h) ---------------- */

#define RENDER_RING_BASE        0x02000u
#define BLT_RING_BASE           0x22000u
#define GEN11_BSD_RING_BASE     0x1c0000u
#define GEN11_BSD2_RING_BASE    0x1c4000u   /* VCS1 -- NOT VCS2 */
#define GEN11_BSD3_RING_BASE    0x1d0000u   /* VCS2 */
#define GEN11_VEBOX_RING_BASE   0x1c8000u

#define RPM_CONFIG0             0x00d00u
#define GEN11_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_SHIFT  3u
#define GEN11_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_MASK   (0x7u << 3)
#define GEN10_RPM_CONFIG0_CTC_SHIFT_PARAMETER_SHIFT 1u
#define GEN10_RPM_CONFIG0_CTC_SHIFT_PARAMETER_MASK  (0x3u << 1)

#define CTC_MODE                0x0a26cu
#define CTC_SOURCE_PARAMETER_MASK 1u
#define CTC_SOURCE_DIVIDE_LOGIC 1u

#define GEN9_TIMESTAMP_OVERRIDE 0x044074u   /* only on the DIVIDE_LOGIC path */

#define GEN10_MIRROR_FUSE3      0x09118u
#define GEN10_L3BANK_MASK       0x0fu

#define GEN11_EU_DISABLE        0x09134u
#define GEN11_EU_DIS_MASK       0xffu
#define GEN11_GT_SLICE_ENABLE   0x09138u
#define GEN11_GT_S_ENA_MASK     0xffu
#define GEN12_GT_GEOMETRY_DSS_ENABLE 0x0913cu
#define GEN11_GT_VEBOX_VDBOX_DISABLE 0x09140u
#define GEN11_GT_VDBOX_DISABLE_MASK  0xffu
#define GEN11_GT_VEBOX_DISABLE_SHIFT 16u
#define GEN11_GT_VEBOX_DISABLE_MASK  (0x0fu << 16)

#define GEN12_FAULT_TLB_DATA0   0x0ceb8u
#define GEN12_FAULT_TLB_DATA1   0x0cebcu
#define GEN12_RING_FAULT_REG    0x0cec4u
/* intel_gt_clear_error_registers() also clears these (FORCEWAKE_RENDER range). */
#define PGTBL_ER                0x02024u
#define IPEIR_I965              0x02064u
#define GEN2_IIR                0x020a4u
#define EIR                     0x020b0u
#define EMR                     0x020b4u
#define I915_MASTER_ERROR_INTERRUPT (1u << 15)
#define RING_FAULT_VALID        (1u << 0)
#define GEN8_RING_FAULT_ENGINE_ID(x) (((x) >> 12) & 0x7u)
#define FAULT_VA_HIGH_BITS      0xfu
#define FAULT_GTT_SEL           (1u << 4)

/* intel_engine_cs.c context sizes. */
#define PAGE_SIZE_4K                    4096u
#define GEN11_LR_CONTEXT_RENDER_SIZE    (14u * PAGE_SIZE_4K)
#define GEN8_LR_CONTEXT_OTHER_SIZE      (2u * PAGE_SIZE_4K)

#define I915_MAX_VCS 8u
#define I915_MAX_VECS 4u

/* ---------------- helpers ---------------- */

static unsigned
hweight16v(uint16_t v)
{
	unsigned n = 0u;

	while (v != 0u) { n += (unsigned)(v & 1u); v = (uint16_t)(v >> 1); }
	return n;
}

static unsigned
hweight32v(uint32_t v)
{
	unsigned n = 0u;

	while (v != 0u) { n += (unsigned)(v & 1u); v >>= 1; }
	return n;
}

/* ---------------- intel_gt_clock_utils.c ---------------- */

static uint32_t
gen11_get_crystal_clock_freq(uint32_t rpm_config_reg)
{
	uint32_t crystal_clock =
		(rpm_config_reg & GEN11_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_MASK) >>
		GEN11_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_SHIFT;

	switch (crystal_clock) {
	case 0u: return 24000000u;    /* _24_MHZ */
	case 1u: return 19200000u;    /* _19_2_MHZ */
	case 2u: return 38400000u;    /* _38_4_MHZ */
	case 3u: return 25000000u;    /* _25_MHZ */
	default:
		kern_logf("i915: parity MISSING_CASE crystal_clock=%u\n", crystal_clock);
		return 0u;
	}
}

static uint32_t
read_reference_ts_freq(struct osdep_mmio *m)
{
	uint32_t ts_override = osdep_mmio_read32(m, GEN9_TIMESTAMP_OVERRIDE);
	uint32_t base_freq, frac_freq;

	base_freq = ((ts_override >> 0) & 0x3ffu) + 1u;
	base_freq *= 1000000u;

	frac_freq = ((ts_override >> 12) & 0xfu);
	frac_freq = 1000000u / (frac_freq + 1u);

	return base_freq + frac_freq;
}

uint32_t
parity_gen11_read_clock_frequency(struct osdep_mmio *m, uint32_t *period_ns_out)
{
	uint32_t ctc_reg = osdep_mmio_read32(m, CTC_MODE);
	uint32_t freq = 0u;

	/*
	 * CTC_MODE says whether the timestamp comes from TIMESTAMP_OVERRIDE or
	 * from the crystal clock via RPM_CONFIG0.
	 */
	if ((ctc_reg & CTC_SOURCE_PARAMETER_MASK) == CTC_SOURCE_DIVIDE_LOGIC) {
		freq = read_reference_ts_freq(m);
	} else {
		uint32_t c0 = osdep_mmio_read32(m, RPM_CONFIG0);

		freq = gen11_get_crystal_clock_freq(c0);

		/*
		 * The command streamer's timestamp may only increment every few
		 * clock cycles; the shift parameter says how many.
		 */
		freq >>= 3u - ((c0 & GEN10_RPM_CONFIG0_CTC_SHIFT_PARAMETER_MASK) >>
			GEN10_RPM_CONFIG0_CTC_SHIFT_PARAMETER_SHIFT);
	}

	if (period_ns_out != 0)
		*period_ns_out = (freq != 0u) ? (1000000000u / freq) : 0u;

	return freq;
}

/* ---------------- intel_sseu.c: gen12_sseu_info_init ---------------- */

void
parity_gen11_compute_sseu_info(struct parity_sseu *s, uint32_t ss_en,
	uint16_t eu_en)
{
	uint32_t valid_ss_mask;
	unsigned ss;

	valid_ss_mask = (s->max_subslices >= 32u) ? 0xffffffffu
		: ((1u << s->max_subslices) - 1u);

	s->slice_mask |= 1u;
	s->subslice_mask = (uint16_t)(ss_en & valid_ss_mask);

	for (ss = 0u; ss < s->max_subslices; ss++) {
		if ((s->subslice_mask & (1u << ss)) == 0u)
			continue;                 /* intel_sseu_has_subslice() */
		if (ss < PARITY_SSEU_MAX_SUBSLICES)
			s->eu_mask[ss] = eu_en;   /* sseu_set_eus() */
	}

	s->eu_per_subslice = (uint16_t)hweight16v(eu_en);

	/* compute_eu_total(): sum over the populated subslices. */
	s->eu_total = 0u;
	for (ss = 0u; ss < s->max_subslices && ss < PARITY_SSEU_MAX_SUBSLICES; ss++)
		s->eu_total = (uint16_t)(s->eu_total + hweight16v(s->eu_mask[ss]));
}

void
parity_gen12_sseu_info_init(struct parity_sseu *s, struct osdep_mmio *m)
{
	uint32_t g_dss_en;
	uint16_t eu_en = 0u;
	uint8_t eu_en_fuse;
	uint8_t s_en;
	unsigned eu;

	/*
	 * Gen12 has Dual-Subslices, which behave similarly to 2 gen11 SS.
	 * intel_sseu_set_info(sseu, 1 slice, 6 DSS, 16 EUs per DSS).
	 */
	s->max_slices = 1u;
	s->max_subslices = 6u;
	s->max_eus_per_subslice = 16u;
	s->slice_mask = 0u;
	s->subslice_mask = 0u;
	for (eu = 0u; eu < PARITY_SSEU_MAX_SUBSLICES; eu++)
		s->eu_mask[eu] = 0u;

	/* TGL/RKL/DG1/ADL only ever had a single slice. */
	s_en = (uint8_t)(osdep_mmio_read32(m, GEN11_GT_SLICE_ENABLE) &
		GEN11_GT_S_ENA_MASK);
	if (s_en != 0x1u)
		kern_logf("i915: parity WARN GT_SLICE_ENABLE=0x%x (expected 0x1)\n",
			(unsigned)s_en);

	g_dss_en = osdep_mmio_read32(m, GEN12_GT_GEOMETRY_DSS_ENABLE);

	/*
	 * GEN11_EU_DISABLE is a DISABLE mask with ONE BIT PER PAIR of EUs, so
	 * each set bit of ~disable expands to TWO EU bits.
	 */
	eu_en_fuse = (uint8_t)(~(osdep_mmio_read32(m, GEN11_EU_DISABLE) &
		GEN11_EU_DIS_MASK));

	for (eu = 0u; eu < (unsigned)(s->max_eus_per_subslice / 2u); eu++)
		if (eu_en_fuse & (1u << eu))
			eu_en = (uint16_t)(eu_en | (1u << (eu * 2u)) |
				(1u << (eu * 2u + 1u)));

	parity_gen11_compute_sseu_info(s, g_dss_en, eu_en);

	/* TGL only supports slice-level power gating. */
	s->has_slice_pg = 1;
	s->valid = 1;
}

/* ---------------- intel_engine_cs.c ---------------- */

uint32_t
parity_intel_engine_context_size(int graphics_ver, int class)
{
	if (class == PARITY_RENDER_CLASS) {
		switch (graphics_ver) {
		case 12:
		case 11:
			return GEN11_LR_CONTEXT_RENDER_SIZE;
		default:
			kern_logf("i915: parity MISSING_CASE context size ver=%d\n",
				graphics_ver);
			return GEN11_LR_CONTEXT_RENDER_SIZE;
		}
	}
	/* Every non-render class on gen8+ uses the "other" size. */
	return GEN8_LR_CONTEXT_OTHER_SIZE;
}

/*
 * engine_mask_apply_media_fuses().  On media ver < 12.50 the register has
 * DISABLE semantics, so the reference INVERTS it before extracting the masks.
 */
uint32_t
parity_engine_mask_apply_media_fuses(struct parity_gt_mmio *g,
	uint32_t engine_mask, struct osdep_mmio *m)
{
	uint32_t media_fuse;
	uint16_t vdbox_mask, vebox_mask;
	unsigned logical_vdbox = 0u;
	unsigned i;

	media_fuse = osdep_mmio_read32(m, GEN11_GT_VEBOX_VDBOX_DISABLE);
	media_fuse = ~media_fuse;   /* MEDIA_VER_FULL < 12.50 */

	vdbox_mask = (uint16_t)(media_fuse & GEN11_GT_VDBOX_DISABLE_MASK);
	vebox_mask = (uint16_t)((media_fuse & GEN11_GT_VEBOX_DISABLE_MASK) >>
		GEN11_GT_VEBOX_DISABLE_SHIFT);

	g->sfc_mask = ~0u;   /* MEDIA_VER_FULL < 12.50 */

	for (i = 0u; i < I915_MAX_VCS; i++) {
		uint32_t bit = (1u << (PARITY_VCS0 + i));

		if ((engine_mask & bit) == 0u) {
			vdbox_mask = (uint16_t)(vdbox_mask & ~(1u << i));
			continue;
		}
		if ((vdbox_mask & (1u << i)) == 0u) {
			engine_mask &= ~bit;
			kern_logf("i915: parity vcs%u fused off\n", i);
			continue;
		}
		/*
		 * gen11_vdbox_has_sfc(): on gen11 the SFC is shared by an even
		 * VDBOX and the next odd one; only even logical instances get it.
		 */
		if ((logical_vdbox % 2u) == 0u)
			g->vdbox_sfc_access |= (1u << i);
		logical_vdbox++;
	}

	for (i = 0u; i < I915_MAX_VECS; i++) {
		uint32_t bit = (1u << (PARITY_VECS0 + i));

		if ((engine_mask & bit) == 0u) {
			vebox_mask = (uint16_t)(vebox_mask & ~(1u << i));
			continue;
		}
		if ((vebox_mask & (1u << i)) == 0u) {
			engine_mask &= ~bit;
			kern_logf("i915: parity vecs%u fused off\n", i);
		}
	}

	return engine_mask;
}

/* intel_engines[] for the ids ADL-P can have. */
struct engine_info {
	int id;
	int class;
	int instance;
	uint32_t base;
	const char *name;
};

static const struct engine_info parity_engine_table[] = {
	{ PARITY_RCS0,  PARITY_RENDER_CLASS,            0, RENDER_RING_BASE,      "rcs0" },
	{ PARITY_BCS0,  PARITY_COPY_ENGINE_CLASS,       0, BLT_RING_BASE,         "bcs0" },
	{ PARITY_VCS0,  PARITY_VIDEO_DECODE_CLASS,      0, GEN11_BSD_RING_BASE,   "vcs0" },
	{ PARITY_VCS2,  PARITY_VIDEO_DECODE_CLASS,      2, GEN11_BSD3_RING_BASE,  "vcs2" },
	{ PARITY_VECS0, PARITY_VIDEO_ENHANCEMENT_CLASS, 0, GEN11_VEBOX_RING_BASE, "vecs0" }
};

/* get_reset_domain(): gen11+ uses a per-engine GRDOM bit. */
static uint32_t
get_reset_domain(int id)
{
	switch (id) {
	case PARITY_RCS0:  return (1u << 1);   /* GEN11_GRDOM_RENDER */
	case PARITY_BCS0:  return (1u << 2);   /* GEN11_GRDOM_BLT */
	case PARITY_VCS0:  return (1u << 3);   /* GEN11_GRDOM_MEDIA */
	case PARITY_VCS2:  return (1u << 5);   /* GEN11_GRDOM_MEDIA2 */
	case PARITY_VECS0: return (1u << 7);   /* GEN11_GRDOM_VECS */
	default:           return 0u;
	}
}

static void
setup_engine_capabilities(struct parity_gt_mmio *g, struct parity_engine *e)
{
	if (e->class == PARITY_VIDEO_DECODE_CLASS) {
		/* HEVC is on every instance from gen11. */
		e->uabi_capabilities |= (1u << 0);   /* HEVC */
		if (g->vdbox_sfc_access & (1u << (unsigned)e->instance))
			e->uabi_capabilities |= (1u << 1);   /* SFC */
	} else if (e->class == PARITY_VIDEO_ENHANCEMENT_CLASS) {
		if (g->sfc_mask & (1u << (unsigned)e->instance))
			e->uabi_capabilities |= (1u << 1);   /* SFC */
	}
}

/* ---------------- intel_gt.c: check_and_clear_faults ---------------- */

void
parity_intel_gt_check_and_clear_faults(struct parity_gt_mmio *g,
	struct osdep_mmio *m)
{
	uint32_t fault;

	/* gen8_check_faults(), GRAPHICS_VER >= 12 register set. */
	fault = osdep_mmio_read32(m, GEN12_RING_FAULT_REG);
	g->fault_reg = fault;
	if (fault & RING_FAULT_VALID) {
		uint32_t d0 = osdep_mmio_read32(m, GEN12_FAULT_TLB_DATA0);
		uint32_t d1 = osdep_mmio_read32(m, GEN12_FAULT_TLB_DATA1);
		uint64_t addr = ((uint64_t)(d1 & FAULT_VA_HIGH_BITS) << 44) |
			((uint64_t)d0 << 12);

		g->fault_valid_seen = 1;
		kern_logf("i915: parity Unexpected fault addr=0x%llx space=%s "
			"engine_id=%u\n", (unsigned long long)addr,
			(d1 & FAULT_GTT_SEL) ? "GGTT" : "PPGTT",
			GEN8_RING_FAULT_ENGINE_ID(fault));
	}

	/*
	 * intel_gt_clear_error_registers().  The ring fault register must be
	 * cleared with a READ-MODIFY-WRITE of RING_FAULT_VALID ONLY -- writing
	 * the whole register to zero clobbers its other fields and leaves the
	 * GT in a state that breaks later display bring-up.
	 */
	osdep_mmio_write32(m, PGTBL_ER, 0u);
	osdep_mmio_write32(m, IPEIR_I965, 0u);
	osdep_mmio_write32(m, EIR, 0u);
	{
		uint32_t eir = osdep_mmio_read32(m, EIR);

		if (eir != 0u) {
			/* Some errors can become stuck; mask them. */
			uint32_t emr = osdep_mmio_read32(m, EMR);

			kern_logf("i915: parity EIR stuck: 0x%08x, masking\n", eir);
			osdep_mmio_write32(m, EMR, emr | eir);
			osdep_mmio_write32(m, GEN2_IIR, I915_MASTER_ERROR_INTERRUPT);
		}
	}

	{
		uint32_t f = osdep_mmio_read32(m, GEN12_RING_FAULT_REG);

		osdep_mmio_write32(m, GEN12_RING_FAULT_REG, f & ~RING_FAULT_VALID);
	}
	osdep_mmio_posting_read32(m, GEN12_RING_FAULT_REG);
}

/* ---------------- the entry point ---------------- */

int
parity_intel_gt_init_mmio(struct parity_gt_mmio *g, int graphics_ver,
	uint32_t platform_engine_mask, struct osdep_mmio *m)
{
	unsigned i;
	unsigned n = 0u;
	uint32_t mask;

	/* intel_gt_init_clock_frequency() */
	g->clock_frequency = parity_gen11_read_clock_frequency(m,
		&g->clock_period_ns);
	g->clock_valid = (g->clock_frequency != 0u);

	/* intel_sseu_info_init() -> gen12_sseu_info_init() */
	parity_gen12_sseu_info_init(&g->sseu, m);

	/*
	 * intel_gt_mcr_init(): gen11..<12.50 installs the ICL L3BANK steering
	 * table; the mask is the INVERSE of the mirror fuse.
	 */
	g->l3bank_mask = (~osdep_mmio_read32(m, GEN10_MIRROR_FUSE3)) &
		GEN10_L3BANK_MASK;
	if (g->l3bank_mask == 0u)
		kern_logf("i915: parity WARN L3 bank mask is all zero!\n");
	g->l3bank_steering = 1;

	/* intel_engines_init_mmio() */
	mask = parity_engine_mask_apply_media_fuses(g, platform_engine_mask, m);

	for (i = 0u; i < (unsigned)(sizeof(parity_engine_table) /
				    sizeof(parity_engine_table[0])); i++) {
		const struct engine_info *info = &parity_engine_table[i];
		struct parity_engine *e;

		if ((mask & (1u << (unsigned)info->id)) == 0u)
			continue;
		if (n >= (unsigned)PARITY_MAX_ENGINES)
			break;

		e = &g->engines[n++];
		e->id = info->id;
		e->class = info->class;
		e->instance = info->instance;
		e->mmio_base = info->base;
		e->mask = (1u << (unsigned)info->id);
		e->reset_domain = get_reset_domain(info->id);
		e->name = info->name;
		e->logical_instance = (unsigned)info->instance;
		e->context_size = parity_intel_engine_context_size(graphics_ver,
			info->class);
		e->uabi_capabilities = 0u;
		setup_engine_capabilities(g, e);
		e->in_use = 1;
	}

	g->num_engines = n;
	g->engine_mask = mask;

	if (hweight32v(mask) != n)
		kern_logf("i915: parity WARN engine_mask 0x%x has %u bits but %u "
			"engines were built\n", mask, hweight32v(mask), n);

	parity_intel_gt_check_and_clear_faults(g, m);

	g->inited = 1;
	return 0;
}
