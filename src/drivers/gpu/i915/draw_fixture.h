/*
 * The big-bang 3D draw fixture (selftest.c), addressable by GPU VA only, so the
 * WS031 Linux-parity path can submit the SAME command list and state bytes
 * without the legacy initialisation or its objects.  Nothing here initialises
 * hardware; legacy and parity initialisation are not mixed.
 */
#ifndef I915_DRAW_FIXTURE_H
#define I915_DRAW_FIXTURE_H

#include <stdint.h>

/*
 * One page holds the surface heap, dynamic heap, instruction heap, vertex data
 * and markers.  The pixel shader stores its entry marker with an A64 write to
 * the absolute address 0x100400c10, so the state page must sit at this VA.
 */
#define I915_DRAW_FIXTURE_STATE_VA        0x100400000ull
#define I915_DRAW_FIXTURE_WIDTH           32u
#define I915_DRAW_FIXTURE_HEIGHT          32u
#define I915_DRAW_FIXTURE_MARKER_OFFSET   3072u     /* +0 before, +4 after, +8 mid-draw, +16 PS */
#define I915_DRAW_FIXTURE_MARKER_BEFORE   0xa5a50001u
#define I915_DRAW_FIXTURE_MARKER_AFTER    0xd7a3f00du
#define I915_DRAW_FIXTURE_MARKER_MIDDRAW  0xc5c50003u
#define I915_DRAW_FIXTURE_PS_MARKER       0xc0ffee01u
#define I915_DRAW_FIXTURE_EXPECTED_PIXEL  0xffff0000u
#define I915_DRAW_FIXTURE_PS_OFFSET       1024u
#define I915_DRAW_FIXTURE_PS_BYTES        496u

/* Zeroes the page, then writes surface/dynamic state, the PS kernel (+EOT carpet) and vertices. */
void drv_i915_draw_fixture_write_state(void *state_page, uint64_t rt_va, uint32_t mocs);

/* i915_draw_build_batch() with all three heaps and the vertex buffer at state_va. */
unsigned drv_i915_draw_fixture_build_batch(uint32_t *cmds, unsigned capacity,
	uint64_t state_va, uint32_t mocs);

/* GEN12_MOCS(I915_MOCS_UNCACHED_INDEX): what the big-bang draw used for every state. */
uint32_t drv_i915_draw_fixture_mocs(void);

/*
 * ---- T1: the first textured draw ----
 * The same RECTLIST draw, but the PS samples an 8x8 R8G8B8A8_UNORM texture
 * (nearest, clamp, explicit LOD 0) at uv = (pixel + 0.5) / 32 and writes the
 * result to the 32x32 B8G8R8A8_UNORM render target.  Origin: upper left;
 * y grows downwards in both the RT and the texture; row 0 is first in memory.
 */
#define I915_TEX_FIXTURE_TEX_VA      0x100404000ull   /* what the generated surface state names */
#define I915_TEX_FIXTURE_TEX_W       8u
#define I915_TEX_FIXTURE_TEX_H       8u
#define I915_TEX_FIXTURE_TEX_BYTES   256u             /* isl: linear, row pitch 32 */
#define I915_TEX_FIXTURE_TEX_RSS_OFFSET 128u          /* surface heap: BT[1] -> here */
#define I915_TEX_FIXTURE_SAMPLER_OFFSET 896u          /* dynamic heap */

/* draw fixture state + the sampling PS, BT[1], the texture surface state and the sampler. */
void drv_i915_tex_fixture_write_state(void *state_page, uint64_t rt_va, uint64_t tex_va, uint32_t mocs);
unsigned drv_i915_tex_fixture_build_batch(uint32_t *cmds, unsigned capacity,
	uint64_t state_va, uint32_t mocs);
/*
 * Test image, RGBA bytes in memory order, texel (u,v) at rgba[(v*8+u)*4]:
 *   variant 0: R=16+32u G=16+32v B=16+32((u+3v)&7) A=255   (position-identifying, asymmetric)
 *   variant 1: R=239-32v G=16+32u B=16+32((3u+v)&7) A=255  (for update / binding switches)
 *   variant 2: R=240-32u G=240-32v B=16+32((u^v)&7) A=255  (a third, again distinct, image)
 */
#define I915_TEX_FIXTURE_VARIANTS 3u
void drv_i915_tex_fixture_pattern(uint8_t *rgba, unsigned variant);
/* What the B8G8R8A8 render target reads back as (little-endian dword) at pixel (x,y). */
uint32_t drv_i915_tex_fixture_expected_pixel(const uint8_t *rgba, unsigned x, unsigned y);

/*
 * ---- T3: two texture objects and a binding switch ----
 * Texture A keeps its surface state at +128, texture B gets one at +192; binding
 * table entry 1 names the one that is bound.  Nothing else in the state page or
 * in the batch changes between the two bindings.
 */
#define I915_TEX_FIXTURE_TEX_B_VA         0x100405000ull
#define I915_TEX_FIXTURE_TEX_B_RSS_OFFSET 192u
void drv_i915_tex_fixture_write_state_ab(void *state_page, uint64_t rt_va, uint64_t tex_a_va,
	uint64_t tex_b_va, unsigned bind_b, uint32_t mocs);

#endif /* I915_DRAW_FIXTURE_H */
