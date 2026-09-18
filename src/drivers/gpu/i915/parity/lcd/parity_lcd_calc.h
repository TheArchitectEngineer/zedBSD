/*
 * WS031 Linux-parity — LCD-A, first slice: from what the panel and the VBT report
 * (EDID, DPCD, VBT colour depth) to the values the display hardware will be
 * programmed with: the panel mode, the link configuration and its bandwidth check,
 * the link / data M/N pair and the combo PLL configuration words.
 *
 * Calculation only: nothing is written to hardware.  The arithmetic is the
 * reference's own (generated files in this directory); the comparison target is
 * what Linux programmed on the same machine (plan/ws031/display-ref/).
 *
 * NOT computed yet (LCD-A, next slices): DDI buffer translation / voltage swing,
 * transcoder timing register words, plane + scanout layout, CDCLK / bandwidth /
 * DBUF / watermark requirements, the enable and disable state sequences.
 */
#ifndef PARITY_LCD_CALC_H
#define PARITY_LCD_CALC_H

#include <stdint.h>

struct parity_lcd_mode {
	int clock_khz;
	uint16_t hdisplay, hsync_start, hsync_end, htotal;
	uint16_t vdisplay, vsync_start, vsync_end, vtotal;
	int hsync_positive, vsync_positive;
	uint16_t width_mm, height_mm;
	unsigned descriptor_index;        /* which of the base block's four descriptors */
	int edid_bpc;                     /* EDID 1.4 digital input colour depth; 0 = undefined */
};

struct parity_lcd_link {
	int sink_max_rate_khz, sink_max_lanes;    /* DPCD 0x001 / 0x002: the capability */
	int rate_khz, lanes;                      /* the selected configuration */
	int use_max_params;                       /* eDP < 1.4: the reference trains at the maximum */
	int bpp;                                  /* pipe bits per pixel */
	int required_kbps, available_kbps;        /* intel_dp_link_required / intel_dp_max_data_rate */
	uint32_t tu, data_m, data_n, link_m, link_n;
};

struct parity_lcd_pll {
	int ref_khz;                              /* display.dpll.ref_clks.nssc */
	uint32_t cfgcr0, cfgcr1, div0;
};

struct parity_lcd_state {
	struct parity_lcd_mode mode;
	struct parity_lcd_link link;
	struct parity_lcd_pll pll;
	unsigned notes;                           /* messages the reference text emitted */
};

/* one register write, in the order the reference's writer functions issue them */
struct parity_lcd_regwrite {
	uint32_t reg, value;          /* a write: the value.  A read-modify-write: the bits SET */
	uint32_t clear;               /* read-modify-write only: the bits cleared first */
	uint8_t rmw;                  /* 1 = read-modify-write (the resulting word depends on the hardware) */
	const char *step;             /* not a register operation: a reference callee that is not ported yet, at its position */
};
#define PARITY_LCD_MAX_REGWRITES 96u
struct parity_lcd_words {
	unsigned n;
	unsigned overflow;
	struct parity_lcd_regwrite w[PARITY_LCD_MAX_REGWRITES];
};

/*
 * The transcoder M/N, timing and pipe-source words for `s`, produced by running the reference's
 * intel_cpu_transcoder_set_m1_n1(), intel_set_transcoder_timings() and intel_set_pipe_src_size()
 * against a recorder.  The list is what those functions write and in their order (LINK_N last arms
 * the M/N update); where the three calls sit in the whole enable sequence is NOT expressed here.
 */
int parity_lcd_emit_transcoder(const struct parity_lcd_state *s, int pipe, int cpu_transcoder,
	uint32_t src_width, uint32_t src_height, struct parity_lcd_words *out);

/*
 * What the reference's hsw_configure_cpu_transcoder() issues for `s`: M/N, timings, the VRR words
 * (CHICKEN_TRANS bit, TRANS_VRR_CTL), TRANS_MULT, the frame start delay and TRANSCONF -- in ITS order,
 * because the caller itself is reference text.  TRANSCONF comes out without the enable bit (a modeset:
 * the transcoder is enabled later by intel_enable_transcoder()).
 */
int parity_lcd_emit_cpu_transcoder(const struct parity_lcd_state *s, int pipe, int cpu_transcoder,
	struct parity_lcd_words *out);

/*
 * The DDI-side words for an eDP/DP SST output on `port` (0 = A): TRANS_MSA_MISC (intel_ddi_set_dp_msa),
 * TRANS_DDI_FUNC_CTL2 and TRANS_DDI_FUNC_CTL (intel_ddi_enable_transcoder_func), and -- a VALUE, not a
 * write -- what intel_ddi_init_dp_buf_reg() leaves in intel_dp->DP for DDI_BUF_CTL (the enable bit is
 * added later by the link-training preparation).  `saved_port_bits` = the port's DDI_BUF_CTL readout masked
 * with DDI_BUF_PORT_REVERSAL, plus the VBT lane-reversal flag (intel_ddi_init).  The three steps are separate calls in the enable
 * sequence; their relative position there is NOT expressed by this list.
 */
int parity_lcd_emit_ddi(const struct parity_lcd_state *s, int port, int pipe, int cpu_transcoder,
	uint32_t saved_port_bits, struct parity_lcd_words *out, uint32_t *ddi_buf_ctl_value);

/*
 * The universal-plane words for ONE full-screen primary plane on a linear XRGB8888 buffer, from the
 * reference's skl_plane_ctl() / glk_plane_color_ctl() and its writers icl_plane_update_noarm() /
 * icl_plane_update_arm(), in their order (PLANE_SURF last: it arms the update).  Watermarks are NOT
 * computed here: the list carries the named step "skl_write_plane_wm" where the reference writes them.
 * `fourcc` / `modifier` other than XRGB8888 / linear, a plane other than the primary, a pitch that is
 * not a multiple of 64 or a surface address that is not 4 KiB aligned: -22.
 */
int parity_lcd_emit_plane(int pipe, int plane_id, uint32_t fourcc, uint64_t modifier, uint32_t width,
	uint32_t height, uint32_t pitch, uint32_t surf_ggtt_offset, struct parity_lcd_words *out);

/* index of the first entry that is the named step `name` at or after `from`, or -1 */
int parity_lcd_words_step(const struct parity_lcd_words *w, const char *name, unsigned from);

/* drm_err / WARN lines of the reference text since the last bind; the hook sees each one as it happens */
unsigned parity_lcd_errors(void);
void parity_lcd_error_bind(void (*hook)(void *ctx, const char *what), void *ctx);

/* how many plain writes to `reg` the list holds; the last one's value in *value */
unsigned parity_lcd_words_find(const struct parity_lcd_words *w, uint32_t reg, uint32_t *value);

/*
 * 0, or a negative Linux errno: -22 malformed input (EDID without a usable detailed timing,
 * DPCD without a known rate), -28 (-ENOSPC) the mode does not fit the link.
 * `dpcd` = the 15 receiver-capability bytes, `edp_dpcd` = the 3 bytes from 0x700.
 */
int parity_lcd_compute(const uint8_t *edid128, const uint8_t *dpcd, const uint8_t *edp_dpcd,
	int vbt_bpp, int ref_nssc_khz, struct parity_lcd_state *out);

#endif /* PARITY_LCD_CALC_H */
