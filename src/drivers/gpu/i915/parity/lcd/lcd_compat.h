/*
 * WS031 Linux-parity — the environment the generated LCD state-calculation files
 * (drm_edid_mode_port.c, intel_link_port.c, intel_dpll_port.c, intel_display_port.c, intel_ddi_port.c,
 * intel_vrr_port.c) are compiled in.
 * zedBSD project code.  These are pure calculations: no hardware, no time, no
 * allocation beyond one mode object.  Fixed to ADL-P (display version 13).
 */
#ifndef PARITY_LCD_COMPAT_H
#define PARITY_LCD_COMPAT_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef u16 __le16;
#define le16_to_cpu(x) ((u16)(x))                 /* the host is little endian */
#ifndef bool
#define bool _Bool
#define true 1
#define false 0
#endif
#undef EINVAL
#define EINVAL 22                                 /* Linux numbering, returned negative */

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define min_t(t, a, b) ({ t _a = (t)(a); t _b = (t)(b); _a < _b ? _a : _b; })
#define min(a, b) ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a < _b ? _a : _b; })
#define max(a, b) ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a > _b ? _a : _b; })
#define EXPORT_SYMBOL(x)
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define DIV_ROUND_UP_ULL(n, d) ((u64)(((u64)(n) + (u64)(d) - 1u) / (u64)(d)))
#define DIV_ROUND_DOWN_ULL(n, d) ((u64)((u64)(n) / (u64)(d)))
#define DIV_ROUND_CLOSEST(x, d) (((x) + ((d) / 2)) / (d))       /* positive operands only here */
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#define fallthrough __attribute__((fallthrough))
#define BIT(n) (1u << (n))
#define REG_BIT(n) ((u32)1u << (n))
#define REG_GENMASK(h, l) ((u32)((0xffffffffu >> (31 - (h))) & (0xffffffffu << (l))))
#define REG_FIELD_PREP(mask, val) ((u32)((((u32)(val)) << __builtin_ctz(mask)) & (mask)))
static inline u64 mul_u32_u32(u32 a, u32 b) { return (u64)a * b; }
static inline u64 div_u64(u64 dividend, u32 divisor) { return dividend / divisor; }
static inline unsigned long roundup_pow_of_two(unsigned long n)
{
	unsigned long p = 1;

	while (p < n)
		p <<= 1;
	return p;
}

/* logging: these calculations only log refusals; the caller reports the result */
int parity_lcd_fmtcheck(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void parity_lcd_note(const char *fmt);
#define drm_dbg_kms(dev, fmt, ...) do { if (0) (void)parity_lcd_fmtcheck(fmt, ##__VA_ARGS__); parity_lcd_note(fmt); } while (0)
#define MISSING_CASE(x) parity_lcd_note("Missing case (" #x ")\n")
#define drm_WARN_ON(dev, cond) ({ int _w = !!(cond); if (_w) parity_lcd_note("WARN_ON(" #cond ")\n"); _w; })

/* ---- DRM objects: the members the kept functions use ---- */
#define DRM_DISPLAY_MODE_LEN 32
#define DRM_MODE_TYPE_PREFERRED (1 << 3)
#define DRM_MODE_TYPE_DRIVER (1 << 6)
#define DRM_MODE_FLAG_PHSYNC (1 << 0)
#define DRM_MODE_FLAG_NHSYNC (1 << 1)
#define DRM_MODE_FLAG_PVSYNC (1 << 2)
#define DRM_MODE_FLAG_NVSYNC (1 << 3)
#define DRM_MODE_FLAG_INTERLACE (1 << 4)
#define DRM_MODE_FLAG_DBLSCAN (1 << 5)
#define DRM_MODE_FLAG_3D_MASK (0x1f << 14)
#define DRM_MODE_FLAG_3D_FRAME_PACKING (1 << 14)
/* drm_modes.h: drm_mode_set_crtcinfo() adjust flags */
#define CRTC_INTERLACE_HALVE_V (1 << 0)
#define CRTC_STEREO_DOUBLE (1 << 1)
#define CRTC_NO_DBLSCAN (1 << 2)
#define CRTC_NO_VSCAN (1 << 3)
struct drm_display_mode {
	int clock;		/* kHz */
	u16 hdisplay, hsync_start, hsync_end, htotal;
	u16 vdisplay, vsync_start, vsync_end, vtotal;
	u32 flags;
	u8 type;
	u16 width_mm, height_mm;
	char name[DRM_DISPLAY_MODE_LEN];
	u16 hskew, vscan;
	/* the timings the hardware is programmed with (drm_mode_set_crtcinfo) */
	int crtc_clock;
	u16 crtc_hdisplay, crtc_hblank_start, crtc_hblank_end, crtc_hsync_start, crtc_hsync_end, crtc_htotal, crtc_hskew;
	u16 crtc_vdisplay, crtc_vblank_start, crtc_vblank_end, crtc_vsync_start, crtc_vsync_end, crtc_vtotal;
};
void drm_mode_set_crtcinfo(struct drm_display_mode *p, int adjust_flags);
struct drm_device { int unused; };
#include "lcd_drm_colorspace.h"  /* reference, extracted: enum drm_colorspace */
struct drm_connector_state { enum drm_colorspace colorspace; void *connector; };
struct drm_display_info { u32 quirks; };
struct drm_connector {
	struct { int id; } base;
	const char *name;
	struct drm_device *dev;
	struct drm_display_info display_info;
};
#include "edid_ref_types.h"       /* reference, extracted: struct edid, detailed_timing, DRM_EDID_* */
struct drm_edid { const struct edid *edid; };
/* the VBT port has helpers of the same names over ITS mode type: keep the two link-time separate */
#define drm_mode_create parity_lcd_drm_mode_create
#define drm_mode_set_name parity_lcd_drm_mode_set_name
struct drm_display_mode *drm_mode_create(struct drm_device *dev);   /* one object, owned by the glue */
void drm_mode_set_name(struct drm_display_mode *mode);
/* drm_cvt_mode() is only reached through EDID_QUIRK_FORCE_REDUCED_BLANKING, which no quirk table sets here */
#define drm_cvt_mode(dev, h, v, r, reduced, interlaced, margins) ((struct drm_display_mode *)0)

/* ---- i915: the members the kept functions use ---- */
#include "lcd_ref_types.h"        /* reference, extracted: intel_link_m_n, intel_dpll_hw_state, field macros */
#define DISPLAY_VER(i915) 13
#define IS_ELKHARTLAKE(i915) 0
#define IS_TIGERLAKE(i915) 0
#define IS_ALDERLAKE_S(i915) 0
#define IS_ALDERLAKE_P(i915) 1
#define IS_DG2(i915) 0
#define IS_DG1(i915) 0
#define IS_ROCKETLAKE(i915) 0
#define IS_JASPERLAKE(i915) 0
#define IS_ICELAKE(i915) 0
#define IS_CHERRYVIEW(i915) 0
#define IS_DISPLAY_VER(i915, from, until) (13 >= (from) && 13 <= (until))
#define HAS_VRR(i915) (DISPLAY_VER(i915) >= 11)
#define IS_DISPLAY_STEP(i915, since, until) 0
#define STEP_B0 0
#define STEP_FOREVER 0
/* register writes: a word list in the tests / the state calculation, the hardware later */
struct parity_lcd_emit {
	void *ctx;
	void (*write32)(void *ctx, u32 reg, u32 value);
	void (*rmw32)(void *ctx, u32 reg, u32 clear, u32 set);  /* read-modify-write: only the hardware knows the result */
	void (*posting_read)(void *ctx, u32 reg);               /* may be NULL */
	void (*step)(void *ctx, const char *name);              /* a callee of the reference that is not ported yet */
};
typedef struct { u32 reg; } i915_reg_t;
#define _MMIO(r) ((const i915_reg_t){ .reg = (r) })
struct drm_i915_private {
	struct drm_device drm;
	struct parity_lcd_emit *emit;
	struct {
		struct { struct { int nssc; } ref_clks; } dpll;
		struct { bool override_afc_startup; u8 override_afc_startup_val; } vbt;
	} display;
};
struct drm_crtc { struct drm_device *dev; };
/* drm_atomic.h: the three flags drm_atomic_crtc_needs_modeset() looks at */
struct drm_crtc_state { struct drm_crtc *crtc; bool mode_changed, active_changed, connectors_changed; };
static inline bool drm_atomic_crtc_needs_modeset(const struct drm_crtc_state *state)
{
	return state->mode_changed || state->active_changed || state->connectors_changed;
}
#include "lcd_ddi_types.h"       /* reference, extracted: enum port, phy, intel_output_type, intel_output_format */
enum pipe { INVALID_PIPE = -1, PIPE_A = 0, PIPE_B, PIPE_C, PIPE_D };
struct intel_crtc { struct drm_crtc base; enum pipe pipe; bool active; };
#define to_intel_crtc(c) container_of(c, struct intel_crtc, base)
struct drm_rect { int x1, y1, x2, y2; };
static inline int drm_rect_width(const struct drm_rect *r) { return r->x2 - r->x1; }
static inline int drm_rect_height(const struct drm_rect *r) { return r->y2 - r->y1; }
struct intel_crtc_state {
	struct drm_crtc_state uapi;
	struct { struct drm_display_mode adjusted_mode; } hw;
	int port_clock;
	int cpu_transcoder;         /* enum transcoder */
	int master_transcoder, mst_master_transcoder;
	struct drm_rect pipe_src;
	unsigned int output_types;  /* bitmask of enum intel_output_type */
	enum intel_output_format output_format;
	int pipe_bpp, lane_count, fdi_lanes;
	u8 pixel_multiplier, framestart_delay;
	bool has_pch_encoder, dither, limited_color_range;
	bool gamma_enable, csc_enable, enable_psr2_sel_fetch;
	bool has_infoframe, has_panel_replay;
	u8 bigjoiner_pipes, lane_lat_optim_mask;
	void *shared_dpll;
	enum pipe hsw_workaround_pipe;
	bool has_hdmi_sink, hdmi_scrambling, hdmi_high_tmds_clock_ratio;
	struct { bool force_thru, enabled; } pch_pfit;
	struct intel_link_m_n dp_m_n, dp_m2_n2, fdi_m_n;
	struct { u16 flipline, vmin, vmax, guardband, pipeline_full; } vrr;
};
#define IS_HASWELL(i915) 0
/* encoder / digital port: the members the kept intel_ddi.c functions use */
struct intel_atomic_state;
struct intel_crtc_state;
struct intel_encoder {
	struct { struct drm_device *dev; } base;
	enum port port;
	/* the hooks hsw_crtc_enable() reaches through intel_encoders_*(); bound as intel_ddi_init() binds them */
	void (*pre_pll_enable)(struct intel_atomic_state *, struct intel_encoder *, const struct intel_crtc_state *, const struct drm_connector_state *);
	void (*pre_enable)(struct intel_atomic_state *, struct intel_encoder *, const struct intel_crtc_state *, const struct drm_connector_state *);
	void (*enable)(struct intel_atomic_state *, struct intel_encoder *, const struct intel_crtc_state *, const struct drm_connector_state *);
	void (*set_signal_levels)(struct intel_encoder *, const struct intel_crtc_state *);
};
struct intel_dp { u32 DP; };
struct intel_digital_port {
	struct intel_encoder base;
	struct intel_dp dp;
	u32 saved_port_bits;
	int ddi_io_wakeref, ddi_io_power_domain;
	struct { bool active; } lspcon;
	void (*set_infoframes)(struct intel_encoder *, bool, const struct intel_crtc_state *, const struct drm_connector_state *);
};
#define enc_to_dig_port(encoder) container_of(encoder, struct intel_digital_port, base)
#define enc_to_intel_dp(encoder) (&enc_to_dig_port(encoder)->dp)
#define intel_tc_port_in_tbt_alt_mode(dig_port) (0)   /* only reached for a Type-C PHY */
/* ADL-P transcoder register blocks: A..D at 0x60000 + n * 0x1000 (the reference's trans_offsets[]) */
static inline u32 parity_lcd_trans_offset(int tran) { return 0x60000u + 0x1000u * (u32)(tran < 0 || tran > 3 ? 0 : tran); }
#define _MMIO_TRANS2(tran, r) _MMIO(parity_lcd_trans_offset(tran) - 0x60000u + (r))
/* pipe register blocks sit at the same 0x1000 spacing (the reference's pipe_offsets[]) */
#define _MMIO_PIPE2(pipe, r) _MMIO(0x1000u * (u32)((pipe) < 0 || (pipe) > 3 ? 0 : (pipe)) + (r))
#define _PICK_EVEN(index, a, b) ((a) + (index) * ((b) - (a)))
#define _PICK(index, ...) (((const u32 []){ __VA_ARGS__ })[index])
#define _MMIO_PORT(port, a, b) _MMIO(_PICK_EVEN(port, a, b))
#define _MMIO_TRANS(tran, a, b) _MMIO(_PICK_EVEN(tran, a, b))
#define intel_de_write(i915, r, v) (i915)->emit->write32((i915)->emit->ctx, (r).reg, (v))
#define intel_de_rmw(i915, r, clear, set) (i915)->emit->rmw32((i915)->emit->ctx, (r).reg, (clear), (set))
#define intel_de_posting_read(i915, r) do { if ((i915)->emit->posting_read != 0) (i915)->emit->posting_read((i915)->emit->ctx, (r).reg); } while (0)
/* the DISPLAY_VER < 5 branch of intel_cpu_transcoder_set_m1_n1() is never taken (version 13); its
 * register names only have to exist for the text to compile */
#define PIPE_DATA_M_G4X(pipe) INVALID_MMIO_REG
#define PIPE_DATA_N_G4X(pipe) INVALID_MMIO_REG
#define PIPE_LINK_M_G4X(pipe) INVALID_MMIO_REG
#define PIPE_LINK_N_G4X(pipe) INVALID_MMIO_REG
#define INVALID_MMIO_REG _MMIO(0)
static inline struct drm_i915_private *to_i915(struct drm_device *dev)
{
	return container_of(dev, struct drm_i915_private, drm);
}

#include "lcd_trans_regs.h"      /* reference, extracted: enum transcoder (the inlines below use it) */
#include "lcd_ref_inlines.h"     /* reference, extracted: intel_crtc_has_type, _has_dp_encoder, _needs_modeset, transcoder_is_dsi */
/* prototypes of the kept non-static reference functions */
bool intel_phy_is_tc(struct drm_i915_private *dev_priv, enum phy phy);
enum phy intel_port_to_phy(struct drm_i915_private *i915, enum port port);
bool intel_dp_is_uhbr(const struct intel_crtc_state *crtc_state);
bool intel_dp_needs_vsc_sdp(const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);
bool intel_cpu_transcoder_has_m2_n2(struct drm_i915_private *dev_priv, enum transcoder transcoder);
void intel_cpu_transcoder_set_m1_n1(struct intel_crtc *crtc, enum transcoder transcoder, const struct intel_link_m_n *m_n);
void intel_cpu_transcoder_set_m2_n2(struct intel_crtc *crtc, enum transcoder transcoder, const struct intel_link_m_n *m_n);
void intel_vrr_set_transcoder_timings(const struct intel_crtc_state *crtc_state);
void intel_ddi_set_dp_msa(const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);
void intel_ddi_enable_transcoder_func(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state);
i915_reg_t hsw_chicken_trans_reg(struct drm_i915_private *i915, enum transcoder cpu_transcoder);
int intel_dp_link_symbol_size(int rate);
int intel_dp_link_symbol_clock(int rate);
int intel_dp_link_required(int pixel_clock, int bpp);
int intel_dp_effective_data_rate(int pixel_clock, int bpp_x16, int bw_overhead);
int intel_dp_max_data_rate(int max_link_rate, int max_lanes);
int drm_dp_bw_channel_coding_efficiency(bool is_uhbr);
bool drm_dp_is_uhbr_rate(int link_rate);
void intel_link_compute_m_n(u16 bits_per_pixel_x16, int nlanes, int pixel_clock, int link_clock,
	int bw_overhead, struct intel_link_m_n *m_n);

#endif /* PARITY_LCD_COMPAT_H */
