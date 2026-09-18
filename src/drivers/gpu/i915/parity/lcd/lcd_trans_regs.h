/*
 * zedBSD WS031: `enum transcoder` and the transcoder timing / pipe source / M-N / context-latency register
 * definitions extracted textually from the Linux v6.8.12 i915 reference (display/intel_display_limits.h:
 * SPDX MIT; i915_reg.h: MIT permission notice; Copyright Intel Corporation -- the full notice is kept in
 * intel_display_port.c) by tools/port_lcd_calc.py.  Do not edit by hand.
 */
#ifndef PARITY_LCD_TRANS_REGS_H
#define PARITY_LCD_TRANS_REGS_H

enum transcoder {
	INVALID_TRANSCODER = -1,
	/*
	 * The following transcoders have a 1:1 transcoder -> pipe mapping,
	 * keep their values fixed: the code assumes that TRANSCODER_A=0, the
	 * rest have consecutive values and match the enum values of the pipes
	 * they map to.
	 */
	TRANSCODER_A = PIPE_A,
	TRANSCODER_B = PIPE_B,
	TRANSCODER_C = PIPE_C,
	TRANSCODER_D = PIPE_D,

	/*
	 * The following transcoders can map to any pipe, their enum value
	 * doesn't need to stay fixed.
	 */
	TRANSCODER_EDP,
	TRANSCODER_DSI_0,
	TRANSCODER_DSI_1,
	TRANSCODER_DSI_A = TRANSCODER_DSI_0,	/* legacy DSI */
	TRANSCODER_DSI_C = TRANSCODER_DSI_1,	/* legacy DSI */

	I915_MAX_TRANSCODERS
};

/* Pipe/transcoder A timing regs */
#define _TRANS_HTOTAL_A		0x60000
#define   HTOTAL_MASK			REG_GENMASK(31, 16)
#define   HTOTAL(htotal)		REG_FIELD_PREP(HTOTAL_MASK, (htotal))
#define   HACTIVE_MASK			REG_GENMASK(15, 0)
#define   HACTIVE(hdisplay)		REG_FIELD_PREP(HACTIVE_MASK, (hdisplay))
#define _TRANS_HBLANK_A		0x60004
#define   HBLANK_END_MASK		REG_GENMASK(31, 16)
#define   HBLANK_END(hblank_end)	REG_FIELD_PREP(HBLANK_END_MASK, (hblank_end))
#define   HBLANK_START_MASK		REG_GENMASK(15, 0)
#define   HBLANK_START(hblank_start)	REG_FIELD_PREP(HBLANK_START_MASK, (hblank_start))
#define _TRANS_HSYNC_A		0x60008
#define   HSYNC_END_MASK		REG_GENMASK(31, 16)
#define   HSYNC_END(hsync_end)		REG_FIELD_PREP(HSYNC_END_MASK, (hsync_end))
#define   HSYNC_START_MASK		REG_GENMASK(15, 0)
#define   HSYNC_START(hsync_start)	REG_FIELD_PREP(HSYNC_START_MASK, (hsync_start))
#define _TRANS_VTOTAL_A		0x6000c
#define   VTOTAL_MASK			REG_GENMASK(31, 16)
#define   VTOTAL(vtotal)		REG_FIELD_PREP(VTOTAL_MASK, (vtotal))
#define   VACTIVE_MASK			REG_GENMASK(15, 0)
#define   VACTIVE(vdisplay)		REG_FIELD_PREP(VACTIVE_MASK, (vdisplay))
#define _TRANS_VBLANK_A		0x60010
#define   VBLANK_END_MASK		REG_GENMASK(31, 16)
#define   VBLANK_END(vblank_end)	REG_FIELD_PREP(VBLANK_END_MASK, (vblank_end))
#define   VBLANK_START_MASK		REG_GENMASK(15, 0)
#define   VBLANK_START(vblank_start)	REG_FIELD_PREP(VBLANK_START_MASK, (vblank_start))
#define _TRANS_VSYNC_A		0x60014
#define   VSYNC_END_MASK		REG_GENMASK(31, 16)
#define   VSYNC_END(vsync_end)		REG_FIELD_PREP(VSYNC_END_MASK, (vsync_end))
#define   VSYNC_START_MASK		REG_GENMASK(15, 0)
#define   VSYNC_START(vsync_start)	REG_FIELD_PREP(VSYNC_START_MASK, (vsync_start))
#define _TRANS_EXITLINE_A	0x60018
#define _PIPEASRC		0x6001c
#define   PIPESRC_WIDTH_MASK	REG_GENMASK(31, 16)
#define   PIPESRC_WIDTH(w)	REG_FIELD_PREP(PIPESRC_WIDTH_MASK, (w))
#define   PIPESRC_HEIGHT_MASK	REG_GENMASK(15, 0)
#define   PIPESRC_HEIGHT(h)	REG_FIELD_PREP(PIPESRC_HEIGHT_MASK, (h))
#define _BCLRPAT_A		0x60020
#define _TRANS_VSYNCSHIFT_A	0x60028
#define _TRANS_MULT_A		0x6002c

/* Pipe/transcoder B timing regs */
#define _TRANS_HTOTAL_B		0x61000
#define _TRANS_HBLANK_B		0x61004
#define _TRANS_HSYNC_B		0x61008
#define _TRANS_VTOTAL_B		0x6100c
#define _TRANS_VBLANK_B		0x61010
#define _TRANS_VSYNC_B		0x61014
#define _PIPEBSRC		0x6101c
#define _BCLRPAT_B		0x61020
#define _TRANS_VSYNCSHIFT_B	0x61028
#define _TRANS_MULT_B		0x6102c

/* DSI 0 timing regs */
#define _TRANS_HTOTAL_DSI0	0x6b000
#define _TRANS_HSYNC_DSI0	0x6b008
#define _TRANS_VTOTAL_DSI0	0x6b00c
#define _TRANS_VSYNC_DSI0	0x6b014
#define _TRANS_VSYNCSHIFT_DSI0	0x6b028

/* DSI 1 timing regs */
#define _TRANS_HTOTAL_DSI1	0x6b800
#define _TRANS_HSYNC_DSI1	0x6b808
#define _TRANS_VTOTAL_DSI1	0x6b80c
#define _TRANS_VSYNC_DSI1	0x6b814
#define _TRANS_VSYNCSHIFT_DSI1	0x6b828

#define TRANS_HTOTAL(trans)	_MMIO_TRANS2((trans), _TRANS_HTOTAL_A)
#define TRANS_HBLANK(trans)	_MMIO_TRANS2((trans), _TRANS_HBLANK_A)
#define TRANS_HSYNC(trans)	_MMIO_TRANS2((trans), _TRANS_HSYNC_A)
#define TRANS_VTOTAL(trans)	_MMIO_TRANS2((trans), _TRANS_VTOTAL_A)
#define TRANS_VBLANK(trans)	_MMIO_TRANS2((trans), _TRANS_VBLANK_A)
#define TRANS_VSYNC(trans)	_MMIO_TRANS2((trans), _TRANS_VSYNC_A)
#define BCLRPAT(trans)		_MMIO_TRANS2((trans), _BCLRPAT_A)
#define TRANS_VSYNCSHIFT(trans)	_MMIO_TRANS2((trans), _TRANS_VSYNCSHIFT_A)
#define PIPESRC(pipe)		_MMIO_TRANS2((pipe), _PIPEASRC)
#define TRANS_MULT(trans)	_MMIO_TRANS2((trans), _TRANS_MULT_A)

#define _PIPEA_DATA_M1		0x60030
#define _PIPEA_DATA_N1		0x60034
#define _PIPEA_DATA_M2		0x60038
#define _PIPEA_DATA_N2		0x6003c
#define _PIPEA_LINK_M1		0x60040
#define _PIPEA_LINK_N1		0x60044
#define _PIPEA_LINK_M2		0x60048
#define _PIPEA_LINK_N2		0x6004c

/* PIPEB timing regs are same start from 0x61000 */

#define _PIPEB_DATA_M1		0x61030
#define _PIPEB_DATA_N1		0x61034
#define _PIPEB_DATA_M2		0x61038
#define _PIPEB_DATA_N2		0x6103c
#define _PIPEB_LINK_M1		0x61040
#define _PIPEB_LINK_N1		0x61044
#define _PIPEB_LINK_M2		0x61048
#define _PIPEB_LINK_N2		0x6104c

#define PIPE_DATA_M1(tran) _MMIO_TRANS2(tran, _PIPEA_DATA_M1)
#define PIPE_DATA_N1(tran) _MMIO_TRANS2(tran, _PIPEA_DATA_N1)
#define PIPE_DATA_M2(tran) _MMIO_TRANS2(tran, _PIPEA_DATA_M2)
#define PIPE_DATA_N2(tran) _MMIO_TRANS2(tran, _PIPEA_DATA_N2)
#define PIPE_LINK_M1(tran) _MMIO_TRANS2(tran, _PIPEA_LINK_M1)
#define PIPE_LINK_N1(tran) _MMIO_TRANS2(tran, _PIPEA_LINK_N1)
#define PIPE_LINK_M2(tran) _MMIO_TRANS2(tran, _PIPEA_LINK_M2)
#define PIPE_LINK_N2(tran) _MMIO_TRANS2(tran, _PIPEA_LINK_N2)

#define _TRANS_A_SET_CONTEXT_LATENCY		0x6007C
#define _TRANS_B_SET_CONTEXT_LATENCY		0x6107C
#define _TRANS_C_SET_CONTEXT_LATENCY		0x6207C
#define _TRANS_D_SET_CONTEXT_LATENCY		0x6307C
#define TRANS_SET_CONTEXT_LATENCY(tran)		_MMIO_TRANS2(tran, _TRANS_A_SET_CONTEXT_LATENCY)
#define  TRANS_SET_CONTEXT_LATENCY_MASK		REG_GENMASK(15, 0)
#define  TRANS_SET_CONTEXT_LATENCY_VALUE(x)	REG_FIELD_PREP(TRANS_SET_CONTEXT_LATENCY_MASK, (x))

#endif /* PARITY_LCD_TRANS_REGS_H */
