/*
 * WS031 Linux-parity — DRAM info + display bandwidth (see dram_bw.h).
 *
 * Faithful port of intel_dram_detect()/gen12_get_dram_info()/
 * icl_pcode_read_mem_global_info() and intel_bw_init_hw()/tgl_get_bw_info()/
 * icl_get_qgv_points() for ADL-P (GRAPHICS_VER 12, DISPLAY_VER 13).  The computed
 * bandwidth table and SAGV status are stored into the device-owned parity_bw_state
 * (mirroring i915->display.bw.max[] / sagv.status) so later stages can read them.
 */
#include "../internal.h"
#include <kern/klog.h>
#include <errno.h>
#include "osdep/mmio.h"
#include "pcode.h"
#include "dram_bw.h"

/* PCODE requests (i915_reg.h): ICL_PCODE_MEM_SUBSYSYSTEM_INFO | subcommand. */
#define REQ_GLOBAL_INFO     0x0000000du
#define REQ_QGV_POINT(pt)   (((uint32_t)(pt) << 16) | 0x0000010du)
#define REQ_PSF_GV_INFO     0x0000020du

#define P_DIV_ROUND_UP(a, b)      (((a) + (b) - 1) / (b))
#define P_DIV_ROUND_CLOSEST(a, b) (((a) + (b) / 2) / (b))
#define P_MIN(a, b)               ((a) < (b) ? (a) : (b))
#define P_MAX(a, b)               ((a) > (b) ? (a) : (b))

struct qgv_point {
	unsigned dclk, t_rp, t_rdpre, t_rc, t_ras, t_rcd;
};

struct psf_gv_point {
	unsigned clk;
};

struct qgv_info {
	struct qgv_point points[PARITY_NUM_QGV_POINTS];
	struct psf_gv_point psf_points[PARITY_NUM_PSF_POINTS];
	unsigned num_points;
	unsigned num_psf_points;
	unsigned t_bl;
	unsigned max_numchannels;
	unsigned channel_width;
	unsigned deinterleave;
};

static void
zero_bytes(void *p, unsigned n)
{
	unsigned i;

	for (i = 0u; i < n; i++)
		((char *)p)[i] = 0;
}

/* icl_pcode_read_mem_global_info decode (gen12); pure, GPU-free testable. */
int
parity_dram_decode(uint32_t val, struct parity_dram_info *di)
{
	switch (val & 0xfu) {
	case 0: di->type = PARITY_DRAM_DDR4; break;
	case 1: di->type = PARITY_DRAM_DDR5; break;
	case 2: di->type = PARITY_DRAM_LPDDR5; break;
	case 3: di->type = PARITY_DRAM_LPDDR4; break;
	case 4: di->type = PARITY_DRAM_DDR3; break;
	case 5: di->type = PARITY_DRAM_LPDDR3; break;
	default:
		di->type = PARITY_DRAM_UNKNOWN;
		kern_logf("i915: parity P2 dram: unknown type field 0x%x (tolerated)\n", val & 0xfu);
		return -EINVAL;
	}

	di->num_channels = (val & 0xf0u) >> 4;
	di->num_qgv_points = (val & 0xf00u) >> 8;
	di->num_psf_gv_points = (val & 0x3000u) >> 12;
	di->valid = 1;

	kern_logf("i915: parity P2 dram: raw=0x%08x type=%d channels=%u qgv_points=%u psf_gv_points=%u wm_lv0_adjust=%d\n",
		val, di->type, di->num_channels, di->num_qgv_points,
		di->num_psf_gv_points, di->wm_lv_0_adjust_needed);
	return 0;
}

/* gen12_get_dram_info -> icl_pcode_read_mem_global_info. */
int
parity_dram_detect(struct mutex *sb_lock, struct osdep_mmio *m, struct parity_dram_info *di)
{
	uint32_t val = 0;
	int ret;

	/*
	 * detect_mem_freq(): no ADL-P branch.  intel_dram_detect(): GRAPHICS_VER 12,
	 * not DG2, HAS_DISPLAY assumed true.  wm_lv_0_adjust_needed is set true
	 * (!IS_GEN9_LP), then overridden false by gen12_get_dram_info().
	 */
	di->wm_lv_0_adjust_needed = 1;
	di->wm_lv_0_adjust_needed = 0;

	ret = parity_pcode_read(sb_lock, m, REQ_GLOBAL_INFO, &val, 0);
	if (ret != 0) {
		kern_logf("i915: parity P2 dram: global-info pcode failed err=%d (tolerated)\n", ret);
		return ret;
	}
	return parity_dram_decode(val, di);
}

/* icl_get_qgv_points for DISPLAY_VER >= 12 (ADL-P), is_y_tile assumed true. */
static int
parity_icl_get_qgv_points(struct mutex *sb_lock, struct osdep_mmio *m,
	const struct parity_dram_info *di, struct qgv_info *qi, int is_y_tile)
{
	unsigned i;
	int ret;

	qi->num_points = di->num_qgv_points;
	qi->num_psf_points = di->num_psf_gv_points;

	switch (di->type) {
	case PARITY_DRAM_DDR4:
		qi->t_bl = is_y_tile ? 8u : 4u; qi->max_numchannels = 2u;
		qi->channel_width = 64u; qi->deinterleave = is_y_tile ? 1u : 2u;
		break;
	case PARITY_DRAM_DDR5:
		qi->t_bl = is_y_tile ? 16u : 8u; qi->max_numchannels = 4u;
		qi->channel_width = 32u; qi->deinterleave = is_y_tile ? 1u : 2u;
		break;
	case PARITY_DRAM_LPDDR4:
		/* ADL-P is not ROCKETLAKE: fall through to the LPDDR5 parameters. */
	case PARITY_DRAM_LPDDR5:
		qi->t_bl = 16u; qi->max_numchannels = 8u;
		qi->channel_width = 16u; qi->deinterleave = is_y_tile ? 2u : 4u;
		break;
	default:
		qi->t_bl = 16u; qi->max_numchannels = 1u;
		break;
	}

	if (qi->num_points > PARITY_NUM_QGV_POINTS)
		qi->num_points = PARITY_NUM_QGV_POINTS;

	for (i = 0u; i < qi->num_points; i++) {
		uint32_t val = 0, val2 = 0;
		unsigned dclk;

		ret = parity_pcode_read(sb_lock, m, REQ_QGV_POINT(i), &val, &val2);
		if (ret != 0) {
			kern_logf("i915: parity P2 bw: could not read QGV %u info err=%d\n", i, ret);
			return ret;
		}
		dclk = val & 0xffffu;
		qi->points[i].dclk = P_DIV_ROUND_UP(16667u * dclk + 500u, 1000u);
		qi->points[i].t_rp = (val & 0xff0000u) >> 16;
		qi->points[i].t_rcd = (val & 0xff000000u) >> 24;
		qi->points[i].t_rdpre = val2 & 0xffu;
		qi->points[i].t_ras = (val2 & 0xff00u) >> 8;
		qi->points[i].t_rc = qi->points[i].t_rp + qi->points[i].t_ras;
		kern_logf("i915: parity P2 bw: QGV %u DCLK=%u tRP=%u tRDPRE=%u tRAS=%u tRCD=%u tRC=%u\n",
			i, qi->points[i].dclk, qi->points[i].t_rp, qi->points[i].t_rdpre,
			qi->points[i].t_ras, qi->points[i].t_rcd, qi->points[i].t_rc);
	}

	if (qi->num_psf_points > 0u) {
		uint32_t val = 0;

		ret = parity_pcode_read(sb_lock, m, REQ_PSF_GV_INFO, &val, 0);
		if (ret != 0) {
			/* Reference fallback: drop PSF points from the calculation. */
			kern_logf("i915: parity P2 bw: failed to read PSF point data; PSF points dropped\n");
			qi->num_psf_points = 0u;
		} else {
			for (i = 0u; i < PARITY_NUM_PSF_POINTS; i++) {
				qi->psf_points[i].clk = val & 0xffu;
				val >>= 8;
			}
			for (i = 0u; i < qi->num_psf_points; i++)
				kern_logf("i915: parity P2 bw: PSF GV %u CLK=%u\n", i, qi->psf_points[i].clk);
		}
	}

	return 0;
}

/* icl_sagv_max_dclk. */
static unsigned
parity_sagv_max_dclk(const struct qgv_info *qi)
{
	unsigned dclk = 0, i;

	for (i = 0u; i < qi->num_points; i++)
		dclk = P_MAX(dclk, qi->points[i].dclk);
	return dclk;
}

/* intel_bw_init_hw -> tgl_get_bw_info(&adlp_sa_info) for ADL-P. */
int
parity_bw_init_hw(struct mutex *sb_lock, struct osdep_mmio *m,
	const struct parity_dram_info *di, struct parity_bw_state *bw)
{
	/* adlp_sa_info. */
	const int sa_deburst = 16, sa_deprogbwlimit = 38, sa_displayrtids = 256, sa_derating = 20;
	struct qgv_info qi;
	const int is_y_tile = 1;
	int num_channels;
	int ipqdepth, ipqdepthpch = 16;
	int dclk_max, maxdebw, peakbw, clperchgroup;
	int i, ret;
	int incomputable = 0;

	zero_bytes(&qi, sizeof(qi));

	num_channels = (int)(di->num_channels < 1u ? 1u : di->num_channels);

	ret = parity_icl_get_qgv_points(sb_lock, m, di, &qi, is_y_tile);
	if (ret != 0) {
		kern_logf("i915: parity P2 bw: failed to get memory subsystem info, ignoring bandwidth limits (err=%d)\n", ret);
		return ret;
	}

	/* DISPLAY_VER < 14 && LPDDR4/5: double the channel count. */
	if (di->type == PARITY_DRAM_LPDDR4 || di->type == PARITY_DRAM_LPDDR5)
		num_channels *= 2;

	qi.deinterleave = qi.deinterleave ? qi.deinterleave :
		(unsigned)P_DIV_ROUND_UP(num_channels, is_y_tile ? 4 : 2);
	if (num_channels < (int)qi.max_numchannels)   /* DISPLAY_VER >= 12 */
		qi.deinterleave = (unsigned)P_MAX(P_DIV_ROUND_UP((int)qi.deinterleave, 2), 1);
	if (num_channels > (int)qi.max_numchannels)   /* DISPLAY_VER >= 12 */
		kern_logf("i915: parity P2 bw: number of channels exceeds max number of channels\n");
	if (qi.max_numchannels != 0u)
		num_channels = P_MIN(num_channels, (int)qi.max_numchannels);

	dclk_max = (int)parity_sagv_max_dclk(&qi);
	peakbw = num_channels * P_DIV_ROUND_UP((int)qi.channel_width, 8) * dclk_max;
	maxdebw = P_MIN(sa_deprogbwlimit * 1000, peakbw * 6 / 10);   /* 60% */
	ipqdepth = P_MIN(ipqdepthpch, sa_displayrtids / num_channels);
	clperchgroup = 4 * P_DIV_ROUND_UP(8, num_channels) * (int)qi.deinterleave;

	for (i = 0; i < PARITY_BW_GROUPS; i++) {
		struct parity_bw_group *bi = &bw->max[i];
		int clpchgroup = (sa_deburst * (int)qi.deinterleave / num_channels) << i;
		int j;

		/* num_planes belongs to the NEXT group (max[i + 1]), per the reference. */
		if (i < PARITY_BW_GROUPS - 1) {
			struct parity_bw_group *bi_next = &bw->max[i + 1];

			if (clpchgroup < clperchgroup)
				bi_next->num_planes = (ipqdepth - clpchgroup) / clpchgroup + 1;
			else
				bi_next->num_planes = 0;
		}

		bi->num_qgv_points = qi.num_points;
		bi->num_psf_gv_points = qi.num_psf_points;

		for (j = 0; j < (int)qi.num_points; j++) {
			const struct qgv_point *sp = &qi.points[j];
			int ct, bw_j;

			ct = P_MAX((int)sp->t_rc,
				(int)sp->t_rp + (int)sp->t_rcd +
				(clpchgroup - 1) * (int)qi.t_bl + (int)sp->t_rdpre);
			if (ct <= 0) {
				/*
				 * Input check (explicit deviation from the reference, which
				 * trusts non-zero HW timings): a non-positive row-cycle time
				 * cannot yield a valid table entry, so leave it zero rather
				 * than fabricate a value.
				 */
				bi->deratedbw[j] = 0u;
				bi->peakbw[j] = 0u;
				incomputable = 1;
				kern_logf("i915: parity P2 bw: BW%d/QGV%d invalid timing ct=%d (input anomaly)\n",
					i, j, ct);
				continue;
			}
			bw_j = P_DIV_ROUND_UP((int)sp->dclk * clpchgroup * 32 * num_channels, ct);
			bi->deratedbw[j] = (unsigned)P_MIN(maxdebw, bw_j * (100 - sa_derating) / 100);
			bi->peakbw[j] = (unsigned)P_DIV_ROUND_CLOSEST((int)sp->dclk * num_channels * (int)qi.channel_width, 8);
			kern_logf("i915: parity P2 bw: BW%d/QGV%d num_planes=%u deratedbw=%u peakbw=%u\n",
				i, j, bi->num_planes, bi->deratedbw[j], bi->peakbw[j]);
		}

		for (j = 0; j < (int)qi.num_psf_points; j++) {
			bi->psf_bw[j] = (unsigned)P_DIV_ROUND_CLOSEST(64 * (int)qi.psf_points[j].clk * 100, 6);
			kern_logf("i915: parity P2 bw: BW%d/PSF%d num_planes=%u bw=%u\n",
				i, j, bi->num_planes, bi->psf_bw[j]);
		}
	}

	/* SAGV: 1 point means SAGV disabled in BIOS -> not controllable. */
	bw->sagv_status = (qi.num_points == 1u) ? (int)PARITY_SAGV_NOT_CONTROLLED
						: (int)PARITY_SAGV_ENABLED;
	/* An incomputable QGV timing leaves the table not fully valid. */
	bw->valid = incomputable ? 0 : 1;
	if (incomputable)
		kern_logf("i915: parity P2 bw: table has incomputable entries; bw.valid=0\n");
	kern_logf("i915: parity P2 bw: sagv_status=%d num_qgv_points=%u num_psf_points=%u\n",
		bw->sagv_status, qi.num_points, qi.num_psf_points);
	return 0;
}
