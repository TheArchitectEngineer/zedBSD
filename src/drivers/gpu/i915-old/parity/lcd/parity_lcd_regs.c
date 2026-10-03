/*
 * WS031 Linux-parity -- the registers the LCD test reads back for its log, by the reference's own names and
 * addresses (extracted macro headers; nothing here is a retyped number), next to the value Linux left in the same
 * register on the same machine with the same panel and mode (plan/ws031/display-ref/regs-selected.txt).  Those
 * dump values are COMPARISON material for the log only: no production code computes with them.  zedBSD project code.
 */
#include "lcd_compat.h"
#include "lcd_seq_compat.h"
#include "lcd_modeset_compat.h"
#include "lcd_plane_compat.h"
#include "lcd_wm_compat.h"
#include "lcd_mreg_backlight.h"
#include "lcd_ddi_regs.h"
#include "parity_lcd_observe.h"
#include "../osdep/mmio.h"
#include <kern/klog.h>
#include <kern/clock.h>

static struct parity_lcd_named_reg table[32];

#define ROW(n, r, have, val, msk) do { \
	if (i < sizeof(table) / sizeof(table[0])) { \
		table[i].name = (n); table[i].reg = i915_mmio_reg_offset(r); table[i].has_linux = (have); \
		table[i].linux_value = (val); table[i].compare_mask = (msk); i++; } } while (0)

const struct parity_lcd_named_reg *parity_lcd_reg_table(int pipe, int port, int dpll_id, unsigned *n)
{
	struct drm_i915_private *i915 = 0;      /* some macros name a device they do not evaluate */
	enum transcoder tr = (enum transcoder)pipe;
	unsigned i = 0;

	(void)i915;
	ROW("ICL_DPLL_ENABLE", ICL_DPLL_ENABLE(dpll_id), 1, 0xcc000000u, 0xcc000000u);
	ROW("TGL_DPLL_CFGCR0", TGL_DPLL_CFGCR0(dpll_id), 1, 0x00e001a5u, 0xffffffffu);
	ROW("TGL_DPLL_CFGCR1", TGL_DPLL_CFGCR1(dpll_id), 1, 0x00000088u, 0xffffffffu);
	ROW("ICL_DPCLKA_CFGCR0", ICL_DPCLKA_CFGCR0, 0, 0u, 0u);
	ROW("DDI_BUF_CTL", DDI_BUF_CTL(port), 1, 0x80000002u, 0xffffff7fu);
	ROW("TRANS_CLK_SEL", TRANS_CLK_SEL(tr), 1, 0x10000000u, 0xffffffffu);
	ROW("TRANS_DDI_FUNC_CTL", TRANS_DDI_FUNC_CTL(tr), 1, 0x8a210002u, 0xffffffffu);
	ROW("TRANSCONF", TRANSCONF(tr), 1, 0xc0000000u, 0xc0000000u);
	ROW("PIPE_MISC", PIPE_MISC(pipe), 0, 0u, 0u);        /* not in the register dump; Linux's display_info says dither=yes, bpp=18 */
	ROW("PLANE_CTL_1", PLANE_CTL(pipe, PLANE_PRIMARY), 1, 0x94000000u, 0xffffffffu);
	ROW("PLANE_COLOR_CTL_1", PLANE_COLOR_CTL(pipe, PLANE_PRIMARY), 1, 0x00002000u, 0xffffffffu);
	ROW("PLANE_STRIDE_1", PLANE_STRIDE(pipe, PLANE_PRIMARY), 1, 0x00000078u, 0xffffffffu);
	ROW("PLANE_SIZE_1", PLANE_SIZE(pipe, PLANE_PRIMARY), 1, 0x0437077fu, 0xffffffffu);
	ROW("PLANE_SURF_1", PLANE_SURF(pipe, PLANE_PRIMARY), 0, 0u, 0u);
	ROW("PLANE_WM_1_0", PLANE_WM(pipe, PLANE_PRIMARY, 0), 1, 0x80004010u, 0xffffffffu);
	ROW("PLANE_BUF_CFG_1", PLANE_BUF_CFG(pipe, PLANE_PRIMARY), 1, 0x0fdb0000u, 0xffffffffu);
	ROW("MBUS_CTL", MBUS_CTL, 1, 0xdc000700u, 0xffffffffu);
	ROW("PIPE_MBUS_DBOX_CTL", PIPE_MBUS_DBOX_CTL(pipe), 0, 0u, 0u);
	ROW("DBUF_CTL_S1", DBUF_CTL_S(DBUF_S1), 1, 0xc043c000u, 0xffffffffu);
	ROW("DBUF_CTL_S2", DBUF_CTL_S(DBUF_S2), 1, 0xc043c000u, 0xffffffffu);
	ROW("DBUF_CTL_S3", DBUF_CTL_S(DBUF_S3), 0, 0u, 0u);
	ROW("DBUF_CTL_S4", DBUF_CTL_S(DBUF_S4), 0, 0u, 0u);
	ROW("BXT_BLC_PWM_CTL", BXT_BLC_PWM_CTL(0), 0, 0u, 0u);
	/* 0xc8254: the dump tool labels it BLC_PWM_PCH_CTL2; the CNP backlight code of the reference names it BXT_BLC_PWM_FREQ(0) */
	ROW("BXT_BLC_PWM_FREQ", BXT_BLC_PWM_FREQ(0), 1, 0x00017700u, 0xffffffffu);
	ROW("BXT_BLC_PWM_DUTY", BXT_BLC_PWM_DUTY(0), 0, 0u, 0u);
	ROW("ICL_PIPESTATUS", ICL_PIPESTATUS(pipe), 0, 0u, 0u);
	ROW("GEN8_DE_PIPE_IMR", GEN8_DE_PIPE_IMR(pipe), 0, 0u, 0u);
	ROW("GEN8_DE_PIPE_IER", GEN8_DE_PIPE_IER(pipe), 0, 0u, 0u);
	ROW("GEN8_DE_PIPE_IIR", GEN8_DE_PIPE_IIR(pipe), 0, 0u, 0u);
	ROW("PIPE_FRMCOUNT_G4X", PIPE_FRMCOUNT_G4X(pipe), 0, 0u, 0u);
	*n = i;
	return table;
}

uint32_t parity_lcd_reg_by_name(const char *name)
{
	const struct parity_lcd_named_reg *t;
	unsigned n = 0, i;

	t = parity_lcd_reg_table(0, 0, 0, &n);
	for (i = 0; i < n; i++)
		if (strcmp(t[i].name, name) == 0)
			return t[i].reg;
	return 0u;
}

/* DBUF_CTL_S(slice) as the saved reference defines it (extracted skl_watermark_regs.h), for the independent check of the
 * table the power-domain initialisation uses (display_core.c) -- E-116 found that table shifted by one slice */
uint32_t parity_lcd_ref_dbuf_ctl(unsigned slice)
{
	return slice < 4u ? i915_mmio_reg_offset(DBUF_CTL_S((enum dbuf_slice)slice)) : 0u;
}

/*
 * LAST RESORT (zedBSD, not a reference path): at the end of the run, any pipe that still scans out is
 * stopped here.  A pipe left running would keep reading memory after the driver let it go, which on this
 * test host (VFIO) hangs the IOMMU unmap when the guest ends.  Bounded: each wait is a fixed number of
 * polls; everything is logged.
 */
unsigned parity_lcd_last_resort_stop(struct osdep_mmio *m)
{
	unsigned stopped = 0u, pipe, i;

	for (pipe = 0u; pipe < 4u; pipe++) {
		u32 base = 0x1000u * pipe;
		u32 transconf = osdep_mmio_read32(m, 0x70008u + base);
		u32 plane_ctl = osdep_mmio_read32(m, 0x70180u + base);

		if ((transconf & 0x80000000u) == 0u && (plane_ctl & 0x80000000u) == 0u)
			continue;
		kern_logf("i915: parity LAST-RESORT pipe %u is still on (TRANSCONF=0x%08x PLANE_CTL=0x%08x): stopping it so that nothing is scanned out when the driver is gone\n", pipe, transconf, plane_ctl);
		osdep_mmio_write32(m, 0x70180u + base, plane_ctl & ~0x80000000u);   /* PLANE_CTL: plane off */
		osdep_mmio_write32(m, 0x7019cu + base, 0u);                         /* PLANE_SURF: arm it */
		osdep_mmio_write32(m, 0x70008u + base, transconf & ~0x80000000u);   /* TRANSCONF: transcoder off */
		for (i = 0u; i < 100u; i++) {
			if ((osdep_mmio_read32(m, 0x70008u + base) & 0x40000000u) == 0u)   /* TRANSCONF_STATE */
				break;
			kern_usleep_range(1000u, 2000u);
		}
		kern_logf("i915: parity LAST-RESORT pipe %u after %u ms: TRANSCONF=0x%08x PLANE_CTL=0x%08x\n", pipe, i, osdep_mmio_read32(m, 0x70008u + base), osdep_mmio_read32(m, 0x70180u + base));
		stopped++;
	}
	for (pipe = 0u; pipe < 2u; pipe++) {                 /* DDI A / B buffers */
		u32 reg = 0x64000u + 0x100u * pipe;
		u32 buf = osdep_mmio_read32(m, reg);

		if ((buf & 0x80000000u) == 0u)
			continue;
		osdep_mmio_write32(m, reg, buf & ~0x80000000u);
		kern_logf("i915: parity LAST-RESORT DDI %c buffer was enabled (0x%08x): disabled\n", (char)(65 + pipe), buf);
		stopped++;
	}
	return stopped;
}
