/*
 * zedBSD — transcribed hardware definitions (values only)
 *
 * Source: Mesa (core Mesa, MIT per docs/license.rst; values are Intel PRM
 * hardware facts).
 *   - src/intel/genxml/gen120.xml  RENDER_SURFACE_STATE field bit positions,
 *     Surface Type and Tile Mode enum values (Gen12 / TGL, applies to ADL-P).
 *   - src/intel/isl/isl.h          ISL_FORMAT_* hardware surface format values.
 * SHA-256 recorded in plan/ws031/i915-vk-license-audit.md.
 *
 * Only values and bit positions are transcribed; the fill logic is new.
 */

#ifndef I915_VK_SURFACE_STATE_GEN12_INC
#define I915_VK_SURFACE_STATE_GEN12_INC

/* RENDER_SURFACE_STATE is sixteen dwords. */
#define GEN12_SURFACE_STATE_DWORDS	16U

/* Dword 0 field positions (gen120.xml RENDER_SURFACE_STATE). */
#define GEN12_SURFACE_TILE_MODE_SHIFT	12U	/* bits 13:12 */
#define GEN12_SURFACE_HALIGN_SHIFT	14U	/* bits 15:14 */
#define GEN12_SURFACE_VALIGN_SHIFT	16U	/* bits 17:16 */
#define GEN12_SURFACE_FORMAT_SHIFT	18U	/* bits 27:18 */
#define GEN12_SURFACE_FORMAT_MASK	0x3FFU
#define GEN12_SURFACE_ARRAY_BIT		(1U << 28)
#define GEN12_SURFACE_TYPE_SHIFT	29U	/* bits 31:29 */

/* Dword 2: width-1 in bits 13:0, height-1 in bits 29:16. */
#define GEN12_SURFACE_WIDTH_MASK	0x3FFFU
#define GEN12_SURFACE_HEIGHT_SHIFT	16U
#define GEN12_SURFACE_HEIGHT_MASK	0x3FFFU

/* Dword 3: surface pitch-1 in bits 17:0. */
#define GEN12_SURFACE_PITCH_MASK	0x3FFFFU

/* Dwords 8 and 9 hold the 64-bit surface base address. */
#define GEN12_SURFACE_BASE_LO_DWORD	8U
#define GEN12_SURFACE_BASE_HI_DWORD	9U

/* Surface Type enum values (gen120.xml). */
#define GEN12_SURFTYPE_1D		0U
#define GEN12_SURFTYPE_2D		1U
#define GEN12_SURFTYPE_3D		2U

/* Tile Mode enum values (gen120.xml). */
#define GEN12_TILE_LINEAR		0U
#define GEN12_TILE_XMAJOR		2U
#define GEN12_TILE_YMAJOR		3U

/* Hardware surface format values (isl.h ISL_FORMAT_*). */
#define GEN12_SURFACE_FORMAT_B8G8R8A8_UNORM	192U
#define GEN12_SURFACE_FORMAT_R8G8B8A8_UNORM	199U
#define GEN12_SURFACE_FORMAT_R8G8B8A8_UNORM_SRGB 200U

#endif /* I915_VK_SURFACE_STATE_GEN12_INC */
