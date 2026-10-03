/*
 * WS031 Linux-parity — the environment intel_bios_port.c (generated from the
 * Linux 6.8.12 reference) is compiled in.  zedBSD project code: it supplies the
 * types, logging, allocation and list primitives the reference text expects,
 * fixed to the one platform this port drives (ADL-P, display version 13, PCH ADP).
 *
 * The reference enums and data structures themselves are NOT retyped here: they
 * are extracted textually into vbt_ref_types.h / intel_bios.h by
 * plan/ws031/handover/tools/port_intel_bios.py.
 */
#ifndef PARITY_VBT_COMPAT_H
#define PARITY_VBT_COMPAT_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/*
 * intel_bios_port.c is the reference text and is not edited to please this
 * tree's -Wall -Wextra -Werror; these are the reference's own warnings.
 */
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
#ifndef __cplusplus
#ifndef bool
#define bool _Bool
#define true 1
#define false 0
#endif
#endif

#define __packed __attribute__((packed))
#define BIT(n) (1u << (n))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define min(a, b) ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a < _b ? _a : _b; })
#define max(a, b) ({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); _a > _b ? _a : _b; })
#define min_t(t, a, b) ({ t _a = (t)(a); t _b = (t)(b); _a < _b ? _a : _b; })
#define max_t(t, a, b) ({ t _a = (t)(a); t _b = (t)(b); _a > _b ? _a : _b; })
#define clamp(v, lo, hi) min(max(v, lo), hi)
#define struct_size(p, member, n) (sizeof(*(p)) + sizeof((p)->member[0]) * (n))
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#define BUILD_BUG_ON(c) _Static_assert(!(c), "BUILD_BUG_ON")
#define GFP_KERNEL 0
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define fallthrough __attribute__((fallthrough))
#define __force
typedef u16 __be16;
#define be16_to_cpu(x) ((u16)((((u16)(x)) >> 8) | (((u16)(x)) << 8)))   /* the host is little endian */
/* overflow.h contract: true when [start, start+size) does not fit in max (or wraps) */
#define range_overflows_t(type, start, size, max) 	({ type _s = (type)(start), _z = (type)(size), _m = (type)(max); _s >= _m || _z > _m - _s; })

/* ---- a minimal intrusive list (zedBSD implementation of the list_head contract) ---- */
struct list_head { struct list_head *next, *prev; };
static inline void INIT_LIST_HEAD(struct list_head *h) { h->next = h; h->prev = h; }
static inline int list_empty(const struct list_head *h) { return h->next == h; }
static inline void list_add_tail(struct list_head *n, struct list_head *h)
{
	n->prev = h->prev; n->next = h; h->prev->next = n; h->prev = n;
}
static inline void list_del(struct list_head *n)
{
	n->prev->next = n->next; n->next->prev = n->prev; n->next = n; n->prev = n;
}
#define list_entry(ptr, type, member) container_of(ptr, type, member)
#define list_for_each_entry(pos, head, member) \
	for (pos = list_entry((head)->next, __typeof__(*pos), member); \
	     &pos->member != (head); \
	     pos = list_entry(pos->member.next, __typeof__(*pos), member))
#define list_for_each_entry_safe(pos, n, head, member) \
	for (pos = list_entry((head)->next, __typeof__(*pos), member), \
	     n = list_entry(pos->member.next, __typeof__(*pos), member); \
	     &pos->member != (head); \
	     pos = n, n = list_entry(n->member.next, __typeof__(*n), member))

/* ---- allocation: a device-owned bump arena, released as a whole by intel_bios_driver_remove ---- */
void *parity_vbt_zalloc(size_t bytes);
void parity_vbt_free(void *p);
#define kzalloc(sz, gfp) parity_vbt_zalloc(sz)
#define kmalloc(sz, gfp) parity_vbt_zalloc(sz)
#define kfree(p) parity_vbt_free((void *)(p))
static inline void *kmemdup(const void *src, size_t len, int gfp)
{
	void *p = parity_vbt_zalloc(len);

	(void)gfp;
	if (p != 0)
		memcpy(p, src, len);
	return p;
}

/* ---- logging ---- */
/*
 * The kernel has no vsnprintf, and the reference uses format specifiers the
 * kernel logger does not promise (%zu, %.*s).  So in the kernel a message is
 * reported as its format text only (arguments are type-checked, never
 * evaluated); the host test build (PARITY_VBT_HOST) prints them in full.
 * Every error-level message is counted either way.
 */
#define PARITY_VBT_LOG_ERR   0
#define PARITY_VBT_LOG_INFO  1
#define PARITY_VBT_LOG_DEBUG 2
int parity_vbt_log_enabled(int level);           /* counts errors; 1 when the level is shown */
void parity_vbt_note(int level, const char *fmt);
void parity_vbt_emit(const char *text);           /* supplied by the embedder */
int parity_vbt_fmtcheck(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#ifdef PARITY_VBT_HOST
#include <stdio.h>
#define parity_vbt_log(level, fmt, ...) \
	do { if (parity_vbt_log_enabled(level)) printf(fmt, ##__VA_ARGS__); } while (0)
#else
#define parity_vbt_log(level, fmt, ...) \
	do { if (0) (void)parity_vbt_fmtcheck(fmt, ##__VA_ARGS__); parity_vbt_note(level, fmt); } while (0)
#endif
#define drm_dbg_kms(drm, fmt, ...)  parity_vbt_log(PARITY_VBT_LOG_DEBUG, fmt, ##__VA_ARGS__)
#define drm_dbg(drm, fmt, ...)      parity_vbt_log(PARITY_VBT_LOG_DEBUG, fmt, ##__VA_ARGS__)
#define drm_info(drm, fmt, ...)     parity_vbt_log(PARITY_VBT_LOG_INFO, fmt, ##__VA_ARGS__)
#define drm_notice(drm, fmt, ...)   parity_vbt_log(PARITY_VBT_LOG_INFO, fmt, ##__VA_ARGS__)
#define drm_err(drm, fmt, ...)      parity_vbt_log(PARITY_VBT_LOG_ERR, fmt, ##__VA_ARGS__)
#define drm_WARN(drm, cond, fmt, ...) \
	({ int _w = !!(cond); if (_w) parity_vbt_log(PARITY_VBT_LOG_ERR, "WARN: " fmt, ##__VA_ARGS__); _w; })
#define drm_WARN_ON(drm, cond) \
	({ int _w = !!(cond); if (_w) parity_vbt_log(PARITY_VBT_LOG_ERR, "WARN_ON(%s)\n", #cond); _w; })
#define WARN_ON(cond) drm_WARN_ON(0, cond)
#define drm_warn(drm, fmt, ...)     parity_vbt_log(PARITY_VBT_LOG_ERR, fmt, ##__VA_ARGS__)
#define DRM_DEBUG_KMS(fmt, ...)     parity_vbt_log(PARITY_VBT_LOG_DEBUG, fmt, ##__VA_ARGS__)
#define DRM_DEBUG_DRIVER(fmt, ...)  parity_vbt_log(PARITY_VBT_LOG_DEBUG, fmt, ##__VA_ARGS__)
#define MISSING_CASE(x) parity_vbt_log(PARITY_VBT_LOG_ERR, "Missing case (%s == %ld)\n", #x, (long)(x))

/* ---- the platform: ADL-P only ---- */
#define DISPLAY_VER(i915)       13
#define IS_ALDERLAKE_P(i915)    1
#define IS_ALDERLAKE_S(i915)    0
#define IS_ROCKETLAKE(i915)     0
#define IS_DG1(i915)            0
#define IS_DGFX(i915)           0
#define IS_DG2(i915)            0
#define IS_TIGERLAKE(i915)      0
#define IS_JASPERLAKE(i915)     0
#define IS_ELKHARTLAKE(i915)    0
#define IS_ICELAKE(i915)        0
#define IS_ICL_WITH_PORT_F(i915) 0
#define IS_GEMINILAKE(i915)     0
#define IS_BROXTON(i915)        0
#define IS_BROADWELL(i915)      0
#define IS_HASWELL(i915)        0
#define IS_VALLEYVIEW(i915)     0
#define IS_CHERRYVIEW(i915)     0
#define IS_G4X(i915)            0
#define IS_PINEVIEW(i915)       0
#define IS_MOBILE(i915)         1
#define HAS_DDI(i915)           1
#define HAS_LSPCON(i915)        1
#define HAS_DISPLAY(i915)       1
#define HAS_PCH_SPLIT(i915)     1
#define HAS_PCH_TGP(i915)       0
#define HAS_PCH_MTP(i915)       0
#define INTEL_PCH_TYPE(i915)    PCH_ADP
#define HAS_PCH_CNP(i915)       0

/* drm_dp.h: the DPCD encodings parse_edp() stores */
#define DP_LINK_BW_1_62   0x06
#define DP_LINK_BW_2_7    0x0a
#define DP_LINK_BW_5_4    0x14
#define DP_LINK_BW_8_1    0x1e
#define DP_TRAIN_VOLTAGE_SWING_LEVEL_0 (0 << 0)
#define DP_TRAIN_VOLTAGE_SWING_LEVEL_1 (1 << 0)
#define DP_TRAIN_VOLTAGE_SWING_LEVEL_2 (2 << 0)
#define DP_TRAIN_VOLTAGE_SWING_LEVEL_3 (3 << 0)
#define DP_TRAIN_PRE_EMPH_LEVEL_0 (0 << 3)
#define DP_TRAIN_PRE_EMPH_LEVEL_1 (1 << 3)
#define DP_TRAIN_PRE_EMPH_LEVEL_2 (2 << 3)
#define DP_TRAIN_PRE_EMPH_LEVEL_3 (3 << 3)

/* intel_gmbus.h pin numbers (the values, as in the reference header) */
#define GMBUS_PIN_DISABLED 0
#define GMBUS_PIN_1_BXT 1
#define GMBUS_PIN_2_BXT 2
#define GMBUS_PIN_3_BXT 3
#define GMBUS_PIN_4_CNP 4
#define GMBUS_PIN_9_TC1_ICP 9
#define GMBUS_PIN_10_TC2_ICP 10
#define GMBUS_PIN_11_TC3_ICP 11
#define GMBUS_PIN_12_TC4_ICP 12
#define GMBUS_PIN_13_TC5_TGP 13
#define GMBUS_PIN_14_TC6_TGP 14
/* legacy pins referenced by pre-ICP tables in the reference text */
#define GMBUS_PIN_SSC 1
#define GMBUS_PIN_VGADDC 2
#define GMBUS_PIN_PANEL 3
#define GMBUS_PIN_DPC 4
#define GMBUS_PIN_DPB 5
#define GMBUS_PIN_DPD 6
#define GMBUS_PIN_DPD_CHV 3
#define GMBUS_PIN_5_MTP 5

/* ---- drm mode (the fields fill_detail_timing_data() and parse_generic_dtd() write) ---- */
#define DRM_DISPLAY_MODE_LEN 32
#define DRM_MODE_TYPE_PREFERRED (1 << 3)
#define DRM_MODE_FLAG_PHSYNC (1 << 0)
#define DRM_MODE_FLAG_NHSYNC (1 << 1)
#define DRM_MODE_FLAG_PVSYNC (1 << 2)
#define DRM_MODE_FLAG_NVSYNC (1 << 3)
struct drm_display_mode {
	int clock;		/* kHz */
	u16 hdisplay, hsync_start, hsync_end, htotal;
	u16 vdisplay, vsync_start, vsync_end, vtotal;
	u32 flags;
	u8 type;
	u16 width_mm, height_mm;
	char name[DRM_DISPLAY_MODE_LEN];
};
#define DRM_MODE_FMT "\"%s\": %d %d %d %d %d %d %d %d %d 0x%x 0x%x"
#define DRM_MODE_ARG(m) (m)->name, 0, (m)->clock, (m)->hdisplay, (m)->hsync_start, \
	(m)->hsync_end, (m)->htotal, (m)->vdisplay, (m)->vsync_start, (m)->vsync_end, \
	(m)->vtotal - 0 + 0 * (int)(m)->type, (m)->flags
void drm_mode_set_name(struct drm_display_mode *mode);

enum drm_panel_orientation {
	DRM_MODE_PANEL_ORIENTATION_UNKNOWN = -1,
	DRM_MODE_PANEL_ORIENTATION_NORMAL = 0,
	DRM_MODE_PANEL_ORIENTATION_BOTTOM_UP,
	DRM_MODE_PANEL_ORIENTATION_LEFT_UP,
	DRM_MODE_PANEL_ORIENTATION_RIGHT_UP,
};

/* ---- EDID base block: the PnP id at offset 8 (pnpid_get_panel_type), the extension count and the checksum ---- */
struct edid { u8 header[8]; u8 mfg_id[2]; u8 prod_code[2]; u32 serial; u8 mfg_week; u8 mfg_year; u8 rest[108]; u8 extensions; u8 checksum; } __packed;
struct drm_edid { const struct edid *edid; };
static inline const struct edid *drm_edid_raw(const struct drm_edid *e) { return e != 0 ? e->edid : 0; }
/* drm_edid.h: three 5-bit letters, 'A' == 1 */
static inline const char *drm_edid_decode_mfg_id(u16 mfg_id, char vend[4])
{
	vend[0] = (char)('@' + ((mfg_id >> 10) & 0x1f));
	vend[1] = (char)('@' + ((mfg_id >> 5) & 0x1f));
	vend[2] = (char)('@' + ((mfg_id >> 0) & 0x1f));
	vend[3] = '\0';
	return vend;
}

#define MIPI_SEQ_MAX_PLACEHOLDER 0
#include "intel_bios.h"          /* reference, extracted: backlight type, edp_power_seq, mipi structs, prototypes */
#include "vbt_ref_types.h"       /* reference, extracted: enum port/aux_ch/phy/intel_pch/drrs_type, vbt data structs */

struct intel_panel {
	const struct drm_edid *fixed_edid;
	struct intel_vbt_panel_data vbt;
};
struct drm_device { int unused; };
/* the DP AUX / PPS port (../dp/dp_compat.h) adds its members through these hooks */
#ifndef PARITY_I915_EXTRA_DISPLAY_MEMBERS
#define PARITY_I915_EXTRA_DISPLAY_MEMBERS
#endif
#ifndef PARITY_I915_EXTRA_MEMBERS
#define PARITY_I915_EXTRA_MEMBERS
#endif
struct drm_i915_private {
	struct drm_device drm;
	struct {
		struct intel_vbt_data vbt;
		struct { int edp_vswing; } params;   /* i915.edp_vswing module parameter: 0 = use the VBT */
		PARITY_I915_EXTRA_DISPLAY_MEMBERS
	} display;
	PARITY_I915_EXTRA_MEMBERS
};

#define for_each_port_masked(__port, __ports_mask) 	for ((__port) = PORT_A; (__port) < I915_MAX_PORTS; (__port)++) 		if (!((__ports_mask) & BIT(__port))) {} else

#define port_name(p) ((p) + 'A')

/* zedBSD: the validated VBT bytes chosen by ../bios.c (NULL = no VBT -> missing defaults). */
const void *parity_vbt_provider_get(struct drm_i915_private *i915);

/* intel_display.c: ADL-P maps ports A..C to combo PHY A..C and TC1..TC4 to PHY F..I */
static inline enum phy intel_port_to_phy(struct drm_i915_private *i915, enum port port)
{
	(void)i915;
	if (port >= PORT_TC1)
		return (enum phy)(PHY_F + (port - PORT_TC1));
	return (enum phy)(PHY_A + (port - PORT_A));
}
static inline bool intel_phy_is_tc(struct drm_i915_private *i915, enum phy phy)
{
	(void)i915;
	return phy >= PHY_F && phy <= PHY_I;
}
/* intel_gmbus.c: PCH >= ICP uses gmbus_pins_icp = { 1, 2, 3, 9..14 } */
static inline bool intel_gmbus_is_valid_pin(struct drm_i915_private *i915, unsigned int pin)
{
	(void)i915;
	return (pin >= 1u && pin <= 3u) || (pin >= 9u && pin <= 14u);
}
/* intel_opregion.c: no OpRegion in this configuration (ASLS == 0): the reference returns -ENODEV */
static inline int intel_opregion_get_panel_type(struct drm_i915_private *i915)
{
	(void)i915;
	return -19;
}

#endif /* PARITY_VBT_COMPAT_H */
