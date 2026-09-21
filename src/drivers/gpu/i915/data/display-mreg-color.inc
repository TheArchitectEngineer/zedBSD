/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

/*
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_color_regs.h
 * (sha256 f4b148d849385b5bf42b5f99d69b29be7fc55e248d3f42e40c8a7e4934a56c53) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.  Do not edit by hand.
 */
/*
 * zedBSD display rebuild: moved from src/drivers/gpu/i915/parity/lcd/lcd_mreg_color.h of the
 * pre-rebuild tree (commit 7e7ff337) without change to any definition;
 * the include guard PARITY_LCD_MREG_COLOR_H was renamed.  The provenance above still applies and names the generator.
 */

#ifndef DRIVERS_GPU_I915_DATA_DISPLAY_MREG_COLOR_INC
#define DRIVERS_GPU_I915_DATA_DISPLAY_MREG_COLOR_INC

#define _GAMMA_MODE_A		0x4a480
#define _GAMMA_MODE_B		0x4ac80
#define GAMMA_MODE(pipe) _MMIO_PIPE(pipe, _GAMMA_MODE_A, _GAMMA_MODE_B)
#define  PRE_CSC_GAMMA_ENABLE			REG_BIT(31) /* icl+ */
#define  POST_CSC_GAMMA_ENABLE			REG_BIT(30) /* icl+ */
#define  GAMMA_MODE_MODE_MASK			REG_GENMASK(1, 0)
#define  GAMMA_MODE_MODE_8BIT			REG_FIELD_PREP(GAMMA_MODE_MODE_MASK, 0)
#define  GAMMA_MODE_MODE_10BIT			REG_FIELD_PREP(GAMMA_MODE_MODE_MASK, 1)
#define  GAMMA_MODE_MODE_12BIT_MULTI_SEG	REG_FIELD_PREP(GAMMA_MODE_MODE_MASK, 3) /* icl-tgl */
#define _PIPE_A_CSC_MODE	0x49028
#define  ICL_CSC_ENABLE			(1 << 31) /* icl+ */
#define  ICL_OUTPUT_CSC_ENABLE		(1 << 30) /* icl+ */
#define _PIPE_B_CSC_MODE	0x49128
#define PIPE_CSC_MODE(pipe)		_MMIO_PIPE(pipe, _PIPE_A_CSC_MODE, _PIPE_B_CSC_MODE)
#define _SKL_BOTTOM_COLOR_A		0x70034
#define _SKL_BOTTOM_COLOR_B		0x71034
#define SKL_BOTTOM_COLOR(pipe)		_MMIO_PIPE(pipe, _SKL_BOTTOM_COLOR_A, _SKL_BOTTOM_COLOR_B)

#endif /* DRIVERS_GPU_I915_DATA_DISPLAY_MREG_COLOR_INC */
