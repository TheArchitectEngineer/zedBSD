/*
 * WS031 Linux-parity — the extra environment skl_plane_port.c is compiled in (on top of lcd_compat.h).
 * zedBSD project code.  Scope of this slice: ONE plane showing a LINEAR framebuffer, no rotation, no
 * scaler, no colour key, no CCS / DPT / planar formats.  The glue refuses anything else before the
 * reference code runs, so the helpers below that only matter for those cases are fixed to the linear
 * answer -- each is named here, none is a silent default inside the reference text.
 */
#ifndef PARITY_LCD_PLANE_COMPAT_H
#define PARITY_LCD_PLANE_COMPAT_H

typedef uint32_t __u32;
typedef uint64_t __u64;
#include "lcd_drm_fourcc.h"       /* reference, whole file: DRM_FORMAT_*, modifiers */
#include "lcd_drm_plane_defs.h"   /* reference, extracted: blend modes, rotation bits, colour enums */
#include "lcd_plane_types.h"      /* reference, extracted: enum plane_id */
#include "lcd_i915_colorkey.h"    /* reference, extracted: struct drm_intel_sprite_colorkey, I915_SET_COLORKEY_* */
#include "lcd_plane_regs.h"       /* reference, extracted: the Skylake+ plane register block */
#include "lcd_psr_selfetch_regs.h"

/* register address helpers the plane block uses (display/intel_display_reg_defs.h, same arithmetic) */
#define _PIPE(pipe, a, b) _PICK_EVEN(pipe, a, b)
#define _PLANE(plane, a, b) _PICK_EVEN(plane, a, b)
#define _MMIO_PIPE(pipe, a, b) _MMIO(_PIPE(pipe, a, b))
#define _MMIO_PLANE(plane, a, b) _MMIO(_PLANE(plane, a, b))
#ifndef intel_de_write_fw
#define intel_de_write_fw(i915, r, v) intel_de_write(i915, r, v)
#endif
#define lower_32_bits(n) ((u32)((n) & 0xffffffffu))
#define upper_32_bits(n) ((u32)(((u64)(n)) >> 32))
#define HAS_FLAT_CCS(i915) 0                          /* discrete (DG2+) only */

/* ---- DRM / i915 objects: the members the kept functions use ---- */
struct drm_format_info {
	u32 format;
	u8 num_planes;
	u8 cpp[4];
	bool has_alpha, is_yuv;
};
struct drm_framebuffer {
	struct drm_device *dev;
	const struct drm_format_info *format;
	u64 modifier;
};
struct drm_plane { struct drm_device *dev; };
struct intel_plane { struct drm_plane base; enum plane_id id; enum pipe pipe; bool async_flip; };
#define to_intel_plane(p) container_of(p, struct intel_plane, base)
struct intel_plane_state {
	struct { struct drm_plane *plane; struct drm_rect src, dst; bool visible; } uapi;   /* src is 16.16 fixed point */
	struct {
		const struct drm_framebuffer *fb;
		unsigned int rotation;
		u16 alpha;
		u16 pixel_blend_mode;
		enum drm_color_encoding color_encoding;
		enum drm_color_range color_range;
	} hw;
	struct drm_intel_sprite_colorkey ckey;
	struct { struct { u32 offset; unsigned int x, y, scanout_stride, mapping_stride; } color_plane[4]; } view;
	u32 ctl, color_ctl, cus_ctl;
	int scaler_id;
	bool force_black, decrypt, planar_slave;
	struct intel_plane *planar_linked_plane;
	u64 ccval;
	struct drm_rect psr2_sel_fetch_area;
	const struct { struct { u64 start; } node; } *dpt_vma;   /* always NULL here: linear framebuffers do not use a DPT */
	u32 ggtt_offset;            /* zedBSD: where the pinned buffer sits in the GGTT (Linux: i915_ggtt_offset(ggtt_vma)) */
};
#define intel_plane_ggtt_offset(plane_state) ((plane_state)->ggtt_offset)

/* ---- fixed to the linear, single-plane case (the glue refuses everything else first) ---- */
#define is_surface_linear(fb, color_plane) ((fb)->modifier == DRM_FORMAT_MOD_LINEAR)
#define intel_tile_height(fb, color_plane) (1u)               /* not reached: linear */
#define intel_tile_width_bytes(fb, color_plane) (1u)          /* not reached: linear */
#define intel_fb_uses_dpt(fb) (0)                             /* linear framebuffers do not use a DPT */
#define intel_fb_is_rc_ccs_cc_modifier(modifier) (0)
#define intel_format_info_is_yuv_semiplanar(info, modifier) (0)
#define skl_main_to_aux_plane(fb, color_plane) (0)            /* no aux (CCS / UV) plane */

/*
 * Callees that are NOT ported.  They are reported through the emit hook as a named step at the
 * position the reference calls them, so the recorded sequence shows the gap instead of hiding it.
 */
#define skl_program_plane_scaler(plane, crtc_state, plane_state) \
	to_i915((plane)->base.dev)->emit->step(to_i915((plane)->base.dev)->emit->ctx, "skl_program_plane_scaler")
#define icl_program_input_csc(plane, crtc_state, plane_state) \
	to_i915((plane)->base.dev)->emit->step(to_i915((plane)->base.dev)->emit->ctx, "icl_program_input_csc")
#define icl_plane_csc_load_black(plane) \
	to_i915((plane)->base.dev)->emit->step(to_i915((plane)->base.dev)->emit->ctx, "icl_plane_csc_load_black")

struct intel_crtc_state;
void skl_write_plane_wm(struct intel_plane *plane, const struct intel_crtc_state *crtc_state);   /* skl_watermark_port.c */

/* kept non-static reference function (intel_atomic_plane_port.c), used by the plane min-cdclk hook */
unsigned int intel_plane_pixel_rate(const struct intel_crtc_state *crtc_state, const struct intel_plane_state *plane_state);

#endif /* PARITY_LCD_PLANE_COMPAT_H */
