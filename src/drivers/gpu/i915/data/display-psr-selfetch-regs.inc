/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

/*
 * zedBSD WS031: the PLANE_SEL_FETCH_* register definitions extracted textually from the Linux v6.8.12 i915
 * reference display/intel_psr_regs.h (sha256 630cd23c657051e61808036fbf663fc161f916cb1f8cee1c92b725177dbf36a7)
 * by tools/port_lcd_calc.py.  The notice above is the source file's own.  Do not edit by hand.
 */
/*
 * zedBSD display rebuild: moved from src/drivers/gpu/i915/parity/lcd/lcd_psr_selfetch_regs.h of the
 * pre-rebuild tree (commit 7e7ff337) without change to any definition;
 * the include guard PARITY_LCD_PSR_SELFETCH_REGS_H was renamed.  The provenance above still applies and names the generator.
 */

#ifndef DRIVERS_GPU_I915_DATA_DISPLAY_PSR_SELFETCH_REGS_INC
#define DRIVERS_GPU_I915_DATA_DISPLAY_PSR_SELFETCH_REGS_INC

#define _SEL_FETCH_PLANE_BASE_1_A		0x70890
#define _SEL_FETCH_PLANE_BASE_2_A		0x708B0
#define _SEL_FETCH_PLANE_BASE_3_A		0x708D0
#define _SEL_FETCH_PLANE_BASE_4_A		0x708F0
#define _SEL_FETCH_PLANE_BASE_5_A		0x70920
#define _SEL_FETCH_PLANE_BASE_6_A		0x70940
#define _SEL_FETCH_PLANE_BASE_7_A		0x70960
#define _SEL_FETCH_PLANE_BASE_CUR_A		0x70880
#define _SEL_FETCH_PLANE_BASE_1_B		0x71890

#define _SEL_FETCH_PLANE_BASE_A(plane) _PICK(plane, \
					     _SEL_FETCH_PLANE_BASE_1_A, \
					     _SEL_FETCH_PLANE_BASE_2_A, \
					     _SEL_FETCH_PLANE_BASE_3_A, \
					     _SEL_FETCH_PLANE_BASE_4_A, \
					     _SEL_FETCH_PLANE_BASE_5_A, \
					     _SEL_FETCH_PLANE_BASE_6_A, \
					     _SEL_FETCH_PLANE_BASE_7_A, \
					     _SEL_FETCH_PLANE_BASE_CUR_A)
#define _SEL_FETCH_PLANE_BASE_1(pipe) _PIPE(pipe, _SEL_FETCH_PLANE_BASE_1_A, _SEL_FETCH_PLANE_BASE_1_B)
#define _SEL_FETCH_PLANE_BASE(pipe, plane) (_SEL_FETCH_PLANE_BASE_1(pipe) - \
					    _SEL_FETCH_PLANE_BASE_1_A + \
					    _SEL_FETCH_PLANE_BASE_A(plane))

#define _SEL_FETCH_PLANE_CTL_1_A		0x70890
#define PLANE_SEL_FETCH_CTL(pipe, plane) _MMIO(_SEL_FETCH_PLANE_BASE(pipe, plane) + \
					       _SEL_FETCH_PLANE_CTL_1_A - \
					       _SEL_FETCH_PLANE_BASE_1_A)
#define PLANE_SEL_FETCH_CTL_ENABLE		REG_BIT(31)

#define _SEL_FETCH_PLANE_POS_1_A		0x70894
#define PLANE_SEL_FETCH_POS(pipe, plane) _MMIO(_SEL_FETCH_PLANE_BASE(pipe, plane) + \
					       _SEL_FETCH_PLANE_POS_1_A - \
					       _SEL_FETCH_PLANE_BASE_1_A)

#define _SEL_FETCH_PLANE_SIZE_1_A		0x70898
#define PLANE_SEL_FETCH_SIZE(pipe, plane) _MMIO(_SEL_FETCH_PLANE_BASE(pipe, plane) + \
						_SEL_FETCH_PLANE_SIZE_1_A - \
						_SEL_FETCH_PLANE_BASE_1_A)

#define _SEL_FETCH_PLANE_OFFSET_1_A		0x7089C
#define PLANE_SEL_FETCH_OFFSET(pipe, plane) _MMIO(_SEL_FETCH_PLANE_BASE(pipe, plane) + \
						  _SEL_FETCH_PLANE_OFFSET_1_A - \
						  _SEL_FETCH_PLANE_BASE_1_A)


#endif /* DRIVERS_GPU_I915_DATA_DISPLAY_PSR_SELFETCH_REGS_INC */
