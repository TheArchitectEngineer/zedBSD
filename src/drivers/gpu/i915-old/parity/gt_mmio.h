/*
 * WS031 Linux-parity — P6-0a: the rest of intel_gt_init_mmio().
 *
 * P1 did part of this (slice/DSS fuse read, MCR multicast selector) but the
 * pieces P6 depends on were never ported:
 *
 *   intel_gt_init_clock_frequency  -- CTC_MODE / RPM_CONFIG0 -> timestamp clock
 *   intel_sseu_info_init           -- gen12_sseu_info_init, the FULL decode
 *                                     (GT_SLICE_ENABLE, GEOMETRY_DSS_ENABLE,
 *                                      EU_DISABLE -> gen11_compute_sseu_info)
 *   intel_gt_mcr_init              -- the ICL L3BANK steering table + fuse
 *   intel_engines_init_mmio        -- the engine objects themselves
 *   intel_gt_check_and_clear_faults
 *
 * Reference notes that are easy to get wrong and are asserted by the tests:
 *   - GEN11_EU_DISABLE is a DISABLE mask with ONE BIT PER PAIR of EUs; the
 *     decode expands each set bit of ~disable into two EU bits.
 *   - gen12 uses "Dual-Subslices": max 1 slice, 6 DSS, 16 EUs per DSS.
 *   - GEN11_GT_VEBOX_VDBOX_DISABLE is a DISABLE register on media ver < 12.50,
 *     so the reference INVERTS it before use.
 */
#ifndef PARITY_GT_MMIO_H
#define PARITY_GT_MMIO_H

#include <stdint.h>

struct osdep_mmio;

#define PARITY_MAX_ENGINES        6    /* RCS0, BCS0, VCS0, VCS2, VECS0 (+slack) */
#define PARITY_SSEU_MAX_SUBSLICES 6

/* enum intel_engine_id, values preserved for the irq class/instance lookup. */
#define PARITY_RCS0   0
#define PARITY_BCS0   1
#define PARITY_VCS0   8
#define PARITY_VCS2   10
#define PARITY_VECS0  16

/* enum intel_engine_class (GuC/IRQ class ordinals). */
#define PARITY_RENDER_CLASS             0
#define PARITY_VIDEO_DECODE_CLASS       1
#define PARITY_VIDEO_ENHANCEMENT_CLASS  2
#define PARITY_COPY_ENGINE_CLASS        3
#define PARITY_OTHER_CLASS              4
#define PARITY_MAX_ENGINE_CLASS         4
#define PARITY_MAX_ENGINE_INSTANCE      7

/* sseu_dev_info subset. */
struct parity_sseu {
	uint8_t max_slices;
	uint8_t max_subslices;
	uint8_t max_eus_per_subslice;
	uint8_t slice_mask;
	uint16_t subslice_mask;                       /* hsw[0] */
	uint16_t eu_mask[PARITY_SSEU_MAX_SUBSLICES];  /* hsw[0][ss] */
	uint16_t eu_per_subslice;
	uint16_t eu_total;
	int has_slice_pg;
	int valid;
};

/* intel_engine_cs, the subset P6 needs before any submission exists. */
struct parity_engine {
	int id;                 /* PARITY_RCS0 .. */
	int class;
	int instance;
	uint32_t mmio_base;
	uint32_t mask;          /* BIT(id) */
	uint32_t reset_domain;
	uint32_t context_size;
	unsigned logical_instance;
	const char *name;
	uint32_t uabi_capabilities;
	int in_use;
};

struct parity_gt_mmio {
	/* intel_gt_init_clock_frequency */
	uint32_t clock_frequency;       /* Hz */
	uint32_t clock_period_ns;
	int clock_valid;

	struct parity_sseu sseu;

	/* intel_gt_mcr_init */
	uint32_t l3bank_mask;
	int l3bank_steering;            /* the ICL L3BANK table is installed */

	/* intel_engines_init_mmio */
	struct parity_engine engines[PARITY_MAX_ENGINES];
	unsigned num_engines;
	uint32_t engine_mask;           /* gt->info.engine_mask after fusing */
	uint32_t vdbox_sfc_access;
	uint32_t sfc_mask;

	/* intel_gt_check_and_clear_faults */
	int fault_valid_seen;
	uint32_t fault_reg;

	int inited;
};

/*
 * intel_gt_init_mmio() for ADL-P.  `platform_engine_mask` is the device-table
 * value (RCS0|BCS0|VECS0|VCS0|VCS2 for ADL-P); the media fuses are applied to
 * it here exactly as the reference does.  Returns 0 or -errno.
 */
int parity_intel_gt_init_mmio(struct parity_gt_mmio *g, int graphics_ver,
	uint32_t platform_engine_mask, struct osdep_mmio *m);

/* Pieces exposed for the GPU-free tests. */
void parity_gen12_sseu_info_init(struct parity_sseu *s, struct osdep_mmio *m);
void parity_gen11_compute_sseu_info(struct parity_sseu *s, uint32_t ss_en,
	uint16_t eu_en);
uint32_t parity_gen11_read_clock_frequency(struct osdep_mmio *m,
	uint32_t *period_ns_out);
uint32_t parity_engine_mask_apply_media_fuses(struct parity_gt_mmio *g,
	uint32_t engine_mask, struct osdep_mmio *m);
uint32_t parity_intel_engine_context_size(int graphics_ver, int class);
void parity_intel_gt_check_and_clear_faults(struct parity_gt_mmio *g,
	struct osdep_mmio *m);

#endif /* PARITY_GT_MMIO_H */
