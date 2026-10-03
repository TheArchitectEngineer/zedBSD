/*
 * WS031 Linux-parity — DRAM info + display bandwidth (hw_probe tail).
 *
 * Ports intel_dram_detect() and intel_bw_init_hw() -> tgl_get_bw_info(&adlp_sa_info)
 * for ADL-P, driven by the common PCODE mailbox.  The results are written into a
 * device-owned bandwidth state (parity_bw_state) that later stages read, mirroring
 * the reference i915->display.bw.max[] / sagv.status.  Both are void in the
 * reference: a tolerated PCODE/data failure is logged and the caller continues.
 */
#ifndef PARITY_DRAM_BW_H
#define PARITY_DRAM_BW_H

#include <stdint.h>

struct osdep_mmio;
struct mutex;

#define PARITY_NUM_QGV_POINTS 8   /* I915_NUM_QGV_POINTS */
#define PARITY_NUM_PSF_POINTS 3   /* I915_NUM_PSF_GV_POINTS */
#define PARITY_BW_GROUPS      6   /* ARRAY_SIZE(display.bw.max) */

/* INTEL_DRAM_* subset reachable on ADL-P's gen12 decode. */
enum parity_dram_type {
	PARITY_DRAM_DDR4 = 0,
	PARITY_DRAM_DDR5,
	PARITY_DRAM_LPDDR5,
	PARITY_DRAM_LPDDR4,
	PARITY_DRAM_DDR3,
	PARITY_DRAM_LPDDR3,
	PARITY_DRAM_UNKNOWN
};

/* SAGV status (display.sagv.status subset). */
enum parity_sagv_status {
	PARITY_SAGV_UNKNOWN = 0,
	PARITY_SAGV_DISABLED,
	PARITY_SAGV_ENABLED,
	PARITY_SAGV_NOT_CONTROLLED
};

/* Subset of dram_info the bandwidth branch consumes. */
struct parity_dram_info {
	int type;                   /* enum parity_dram_type */
	unsigned num_channels;
	unsigned num_qgv_points;
	unsigned num_psf_gv_points;
	int wm_lv_0_adjust_needed;
	int valid;                  /* global info decoded successfully */
};

/* Mirrors struct intel_bw_info (display.bw.max[i]). */
struct parity_bw_group {
	unsigned deratedbw[PARITY_NUM_QGV_POINTS];
	unsigned peakbw[PARITY_NUM_QGV_POINTS];
	unsigned psf_bw[PARITY_NUM_PSF_POINTS];
	unsigned num_qgv_points;
	unsigned num_psf_gv_points;
	unsigned num_planes;
};

/* Device-owned display bandwidth state (display.bw / sagv.status). */
struct parity_bw_state {
	struct parity_bw_group max[PARITY_BW_GROUPS];
	int sagv_status;
	int valid;                  /* bandwidth table computed */
};

/*
 * Pure decode of the PCODE global-info word into *di (gen12 mapping).  Returns 0,
 * or -EINVAL for an unknown DRAM type field.  GPU-free testable.
 */
int parity_dram_decode(uint32_t val, struct parity_dram_info *di);

/*
 * intel_dram_detect() for ADL-P (gen12).  Fills *di from PCODE global info under
 * the device sb_lock.  Returns 0, or a negative errno for a tolerated failure.
 */
int parity_dram_detect(struct mutex *sb_lock, struct osdep_mmio *m,
	struct parity_dram_info *di);

/*
 * intel_bw_init_hw() -> tgl_get_bw_info(&adlp_sa_info).  Reads the QGV/PSF points
 * via PCODE and reproduces the bandwidth computation, storing the result into
 * *bw (device-owned).  Returns 0, or a negative errno for a tolerated failure.
 */
int parity_bw_init_hw(struct mutex *sb_lock, struct osdep_mmio *m,
	const struct parity_dram_info *di, struct parity_bw_state *bw);

#endif /* PARITY_DRAM_BW_H */
