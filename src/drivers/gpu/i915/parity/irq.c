/*
 * WS031 Linux-parity — P4: intel_irq_install() / intel_irq_uninstall().  See irq.h.
 */
#include "irq.h"
#include "pch.h"
#include "gt_mmio.h"
#include "power_domains.h"
#include "osdep/mmio.h"
#include <kern/klog.h>
#include <hal/hal.h>
#include <errno.h>
#include <kern/lock.h>
#include <kern/sched.h>
#include "wait.h"

/* ---------------- i915_reg.h / gt/intel_gt_regs.h ---------------- */

#define GEN11_GFX_MSTR_IRQ              0x190010u
#define GEN11_MASTER_IRQ                (1u << 31)
#define GEN11_PCU_IRQ                   (1u << 30)
#define GEN11_GU_MISC_IRQ               (1u << 29)
#define GEN11_DISPLAY_IRQ               (1u << 16)

#define GEN11_RENDER_COPY_INTR_ENABLE   0x190030u
#define GEN11_VCS_VECS_INTR_ENABLE      0x190034u
#define GEN11_GUC_SG_INTR_ENABLE        0x190038u
#define GEN11_GPM_WGBOXPERF_INTR_ENABLE 0x19003cu
#define GEN11_CRYPTO_RSVD_INTR_ENABLE   0x190040u

#define GEN11_RCS0_RSVD_INTR_MASK       0x190090u
#define GEN11_BCS_RSVD_INTR_MASK        0x1900a0u
#define GEN11_VCS0_VCS1_INTR_MASK       0x1900a8u
#define GEN11_VCS2_VCS3_INTR_MASK       0x1900acu
#define GEN11_VECS0_VECS1_INTR_MASK     0x1900d0u
#define GEN11_GUC_SG_INTR_MASK          0x1900e8u
#define GEN11_GPM_WGBOXPERF_INTR_MASK   0x1900ecu
#define GEN11_CRYPTO_RSVD_INTR_MASK     0x1900f0u

#define GT_RENDER_USER_INTERRUPT        (1u << 0)
#define GT_CS_MASTER_ERROR_INTERRUPT    (1u << 3)
#define GT_CONTEXT_SWITCH_INTERRUPT     (1u << 8)
#define GT_WAIT_SEMAPHORE_INTERRUPT     (1u << 11)

#define GEN11_GU_MISC_IMR               0x444f4u
#define GEN11_GU_MISC_IIR               0x444f8u
#define GEN11_GU_MISC_IER               0x444fcu
#define GEN11_GU_MISC_GSE               (1u << 27)

#define GEN8_PCU_IMR                    0x444e4u
#define GEN8_PCU_IIR                    0x444e8u
#define GEN8_PCU_IER                    0x444ecu

/* gt/intel_gt_regs.h: the gen11 GT interrupt identity handshake. */
#define GEN11_GT_INTR_DW(x)             (0x190018u + (unsigned)(x) * 4u)
#define GEN11_INTR_IDENTITY_REG(x)      (0x190060u + (unsigned)(x) * 4u)
#define GEN11_IIR_REG_SELECTOR(x)       (0x190070u + (unsigned)(x) * 4u)
#define GEN11_INTR_DATA_VALID           (1u << 31)
#define GEN11_INTR_ENGINE_CLASS(x)      ((((x) & 0x00070000u)) >> 16)
#define GEN11_INTR_ENGINE_INSTANCE(x)   ((((x) & 0x03f00000u)) >> 20)
#define GEN11_INTR_ENGINE_INTR(x)       ((x) & 0xffffu)
#define GEN11_GT_DW_IRQ(x)              (1u << (unsigned)(x))

#define GEN11_DISPLAY_INT_CTL           0x44200u
/* GEN11_DISPLAY_INT_CTL carries the GEN8_MASTER_IRQ display bit layout. */
#define GEN8_DE_PCH_IRQ                 (1u << 23)
#define GEN8_DE_MISC_IRQ                (1u << 22)
#define GEN11_DE_HPD_IRQ                (1u << 21)
#define GEN8_DE_PORT_IRQ                (1u << 20)
#define GEN8_DE_PIPE_IRQ(pipe)          (1u << (16u + (unsigned)(pipe)))
#define GEN11_DISPLAY_IRQ_ENABLE        (1u << 31)

/* _MMIO_TRANS2(tran, _PSR_IMR_A): trans_offsets are A 0x60000 .. D 0x63000 and
 * DISPLAY_MMIO_BASE is 0 on gen11+, so the stride is a flat 0x1000. */
#define _PSR_IMR_A                      0x60814u
#define _PSR_IIR_A                      0x60818u
#define TRANS_PSR_IMR(t)                (_PSR_IMR_A + 0x1000u * (unsigned)(t))
#define TRANS_PSR_IIR(t)                (_PSR_IIR_A + 0x1000u * (unsigned)(t))

#define GEN8_DE_PIPE_IMR(p)             (0x44404u + 0x10u * (unsigned)(p))
#define GEN8_DE_PIPE_IIR(p)             (0x44408u + 0x10u * (unsigned)(p))
#define GEN8_DE_PIPE_IER(p)             (0x4440cu + 0x10u * (unsigned)(p))

#define GEN8_DE_PORT_IMR                0x44444u
#define GEN8_DE_PORT_IIR                0x44448u
#define GEN8_DE_PORT_IER                0x4444cu

#define GEN8_DE_MISC_IMR                0x44464u
#define GEN8_DE_MISC_IIR                0x44468u
#define GEN8_DE_MISC_IER                0x4446cu

#define GEN11_DE_HPD_IMR                0x44474u
#define GEN11_DE_HPD_IIR                0x44478u
#define GEN11_DE_HPD_IER                0x4447cu

#define SDEIMR                          0xc4004u
#define SDEIIR                          0xc4008u
#define SDEIER                          0xc400cu
#define SDE_GMBUS_ICP                   (1u << 23)

/* Pipe IIR bits. */
#define GEN8_PIPE_FIFO_UNDERRUN         (1u << 31)
#define GEN8_PIPE_CDCLK_CRC_DONE        (1u << 28)
#define XELPD_PIPE_SOFT_UNDERRUN        (1u << 22)
#define XELPD_PIPE_HARD_UNDERRUN        (1u << 21)
#define GEN8_PIPE_CURSOR_FAULT          (1u << 10)
#define GEN8_PIPE_SPRITE_FAULT          (1u << 9)
#define GEN8_PIPE_PRIMARY_FAULT         (1u << 8)
#define GEN8_PIPE_PRIMARY_FLIP_DONE     (1u << 4)
#define GEN8_PIPE_VBLANK                (1u << 0)
#define GEN9_PIPE_CURSOR_FAULT          (1u << 11)
#define GEN11_PIPE_PLANE7_FAULT         (1u << 22)
#define GEN11_PIPE_PLANE6_FAULT         (1u << 21)
#define GEN11_PIPE_PLANE5_FAULT         (1u << 20)
#define GEN9_PIPE_PLANE4_FAULT          (1u << 10)
#define GEN9_PIPE_PLANE3_FAULT          (1u << 9)
#define GEN9_PIPE_PLANE2_FAULT          (1u << 8)
#define GEN9_PIPE_PLANE1_FAULT          (1u << 7)
#define GEN9_PIPE_PLANE1_FLIP_DONE      (1u << 3)

#define GEN8_DE_PIPE_IRQ_FAULT_ERRORS \
	(GEN8_PIPE_CURSOR_FAULT | GEN8_PIPE_SPRITE_FAULT | GEN8_PIPE_PRIMARY_FAULT)
#define GEN9_DE_PIPE_IRQ_FAULT_ERRORS \
	(GEN9_PIPE_CURSOR_FAULT | GEN9_PIPE_PLANE4_FAULT | GEN9_PIPE_PLANE3_FAULT | \
	 GEN9_PIPE_PLANE2_FAULT | GEN9_PIPE_PLANE1_FAULT)
#define GEN11_DE_PIPE_IRQ_FAULT_ERRORS \
	(GEN9_DE_PIPE_IRQ_FAULT_ERRORS | GEN11_PIPE_PLANE7_FAULT | \
	 GEN11_PIPE_PLANE6_FAULT | GEN11_PIPE_PLANE5_FAULT)
#define RKL_DE_PIPE_IRQ_FAULT_ERRORS \
	(GEN9_DE_PIPE_IRQ_FAULT_ERRORS | GEN11_PIPE_PLANE5_FAULT)

/* DE_PORT AUX bits. */
#define GEN8_AUX_CHANNEL_A              (1u << 0)
#define GEN9_AUX_CHANNEL_B              (1u << 25)
#define GEN9_AUX_CHANNEL_C              (1u << 26)
#define GEN9_AUX_CHANNEL_D              (1u << 27)
#define ICL_AUX_CHANNEL_E               (1u << 29)
#define ICL_AUX_CHANNEL_F               (1u << 28)
#define TGL_DE_PORT_AUX_DDIA            (1u << 0)
#define TGL_DE_PORT_AUX_DDIB            (1u << 1)
#define TGL_DE_PORT_AUX_DDIC            (1u << 2)
#define TGL_DE_PORT_AUX_USBC1           (1u << 8)
#define TGL_DE_PORT_AUX_USBC2           (1u << 9)
#define TGL_DE_PORT_AUX_USBC3           (1u << 10)
#define TGL_DE_PORT_AUX_USBC4           (1u << 11)
#define TGL_DE_PORT_AUX_USBC5           (1u << 12)
#define TGL_DE_PORT_AUX_USBC6           (1u << 13)
#define XELPD_DE_PORT_AUX_DDID          (1u << 12)
#define XELPD_DE_PORT_AUX_DDIE          (1u << 13)
#define DSI0_TE                         (1u << 23)
#define DSI1_TE                         (1u << 24)

#define GEN8_DE_MISC_GSE                (1u << 27)
#define GEN8_DE_EDP_PSR                 (1u << 19)

#define GEN11_DE_TC_HOTPLUG_MASK        0x003f0000u   /* TC1..TC6, bits 16..21 */
#define GEN11_DE_TBT_HOTPLUG_MASK       0x0000003fu   /* TC1..TC6, bits 0..5 */

/* ---------------- small helpers ---------------- */

static void
wr(struct parity_irq_dev *d, uint32_t reg, uint32_t val, unsigned *counter)
{
	osdep_mmio_write32(d->m, reg, val);
	if (counter != 0)
		(*counter)++;
}

static uint32_t
rd(struct parity_irq_dev *d, uint32_t reg)
{
	return osdep_mmio_read32(d->m, reg);
}

/* gen3_irq_reset(): mask everything, disable, then clear IIR TWICE. */
static void
gen3_irq_reset(struct parity_irq_dev *d, uint32_t imr, uint32_t iir, uint32_t ier)
{
	wr(d, imr, 0xffffffffu, &d->reset_writes);
	osdep_mmio_posting_read32(d->m, imr);

	wr(d, ier, 0u, &d->reset_writes);

	/* IIR can theoretically queue up two events. Be paranoid. */
	wr(d, iir, 0xffffffffu, &d->reset_writes);
	osdep_mmio_posting_read32(d->m, iir);
	wr(d, iir, 0xffffffffu, &d->reset_writes);
	osdep_mmio_posting_read32(d->m, iir);
}

/* gen3_assert_iir_is_zero(): a stale IIR is a warning, not a failure. */
static void
gen3_assert_iir_is_zero(struct parity_irq_dev *d, uint32_t iir)
{
	uint32_t val = rd(d, iir);

	if (val == 0u)
		return;

	kern_logf("i915: parity WARN interrupt register 0x%x is not zero: 0x%08x\n",
		iir, val);
	wr(d, iir, 0xffffffffu, 0);
	osdep_mmio_posting_read32(d->m, iir);
	wr(d, iir, 0xffffffffu, 0);
	osdep_mmio_posting_read32(d->m, iir);
}

/* gen3_irq_init(): assert IIR clean, then IER, then IMR (+ posting read). */
static void
gen3_irq_init(struct parity_irq_dev *d, uint32_t imr, uint32_t imr_val,
	uint32_t ier, uint32_t ier_val, uint32_t iir)
{
	gen3_assert_iir_is_zero(d, iir);

	wr(d, ier, ier_val, &d->postinstall_writes);
	wr(d, imr, imr_val, &d->postinstall_writes);
	osdep_mmio_posting_read32(d->m, imr);
}

static uint32_t
gen11_master_intr_disable(struct parity_irq_dev *d)
{
	osdep_mmio_raw_write32(d->m, GEN11_GFX_MSTR_IRQ, 0u);
	/*
	 * Now with master disabled, get a sample of level indications for this
	 * interrupt.  Indications will be cleared on related acks.
	 */
	return osdep_mmio_raw_read32(d->m, GEN11_GFX_MSTR_IRQ);
}

static void
gen11_master_intr_enable(struct parity_irq_dev *d)
{
	osdep_mmio_raw_write32(d->m, GEN11_GFX_MSTR_IRQ, GEN11_MASTER_IRQ);
}

static int
pipe_power_on(struct parity_irq_dev *d, unsigned pipe)
{
	return parity_display_power_is_enabled(d->pd,
		(enum parity_power_domain)(PARITY_PW_DOMAIN_PIPE_A + pipe), d->pwc);
}

static int
transcoder_power_on(struct parity_irq_dev *d, unsigned trans)
{
	return parity_display_power_is_enabled(d->pd,
		(enum parity_power_domain)(PARITY_PW_DOMAIN_TRANSCODER_A + trans), d->pwc);
}

/* ---------------- gen8_de_* mask helpers ---------------- */

uint32_t
parity_gen8_de_pipe_fault_mask(int display_ver)
{
	/* HAS_D12_PLANE_MINIMIZATION is RKL/ADL-S only; ADL-P takes ver >= 13. */
	if (display_ver >= 13)
		return RKL_DE_PIPE_IRQ_FAULT_ERRORS;
	else if (display_ver >= 11)
		return GEN11_DE_PIPE_IRQ_FAULT_ERRORS;
	else if (display_ver >= 9)
		return GEN9_DE_PIPE_IRQ_FAULT_ERRORS;
	else
		return GEN8_DE_PIPE_IRQ_FAULT_ERRORS;
}

uint32_t
parity_gen8_de_port_aux_mask(int display_ver)
{
	uint32_t mask;

	if (display_ver >= 20)
		return 0u;
	else if (display_ver >= 14)
		return TGL_DE_PORT_AUX_DDIA | TGL_DE_PORT_AUX_DDIB;
	else if (display_ver >= 13)
		return TGL_DE_PORT_AUX_DDIA | TGL_DE_PORT_AUX_DDIB |
			TGL_DE_PORT_AUX_DDIC | XELPD_DE_PORT_AUX_DDID |
			XELPD_DE_PORT_AUX_DDIE | TGL_DE_PORT_AUX_USBC1 |
			TGL_DE_PORT_AUX_USBC2 | TGL_DE_PORT_AUX_USBC3 |
			TGL_DE_PORT_AUX_USBC4;
	else if (display_ver >= 12)
		return TGL_DE_PORT_AUX_DDIA | TGL_DE_PORT_AUX_DDIB |
			TGL_DE_PORT_AUX_DDIC | TGL_DE_PORT_AUX_USBC1 |
			TGL_DE_PORT_AUX_USBC2 | TGL_DE_PORT_AUX_USBC3 |
			TGL_DE_PORT_AUX_USBC4 | TGL_DE_PORT_AUX_USBC5 |
			TGL_DE_PORT_AUX_USBC6;

	mask = GEN8_AUX_CHANNEL_A;
	if (display_ver >= 9)
		mask |= GEN9_AUX_CHANNEL_B | GEN9_AUX_CHANNEL_C | GEN9_AUX_CHANNEL_D;
	if (display_ver == 11)
		mask |= ICL_AUX_CHANNEL_F | ICL_AUX_CHANNEL_E;

	return mask;
}

uint32_t
parity_gen8_de_pipe_underrun_mask(int display_ver)
{
	uint32_t mask = GEN8_PIPE_FIFO_UNDERRUN;

	if (display_ver >= 13)
		mask |= XELPD_PIPE_SOFT_UNDERRUN | XELPD_PIPE_HARD_UNDERRUN;

	return mask;
}

uint32_t
parity_gen8_de_pipe_flip_done_mask(int display_ver)
{
	if (display_ver >= 9)
		return GEN9_PIPE_PLANE1_FLIP_DONE;
	else
		return GEN8_PIPE_PRIMARY_FLIP_DONE;
}

/* ---------------- gt/intel_gt_irq.c ---------------- */

void
parity_gen11_gt_irq_reset(struct parity_irq_dev *d)
{
	/*
	 * Disable RCS, BCS, VCS and VECS class engines.  ADL-P has
	 * RCS0|BCS0|VECS0|VCS0|VCS2 and no CCS / GSC0 / HECI-GSC, so the
	 * Xe-HP-only registers in the reference are not written here.
	 */
	wr(d, GEN11_RENDER_COPY_INTR_ENABLE, 0u, &d->reset_writes);
	wr(d, GEN11_VCS_VECS_INTR_ENABLE, 0u, &d->reset_writes);

	/* Restore masks irqs on RCS, BCS, VCS and VECS engines. */
	wr(d, GEN11_RCS0_RSVD_INTR_MASK, ~0u, &d->reset_writes);
	wr(d, GEN11_BCS_RSVD_INTR_MASK, ~0u, &d->reset_writes);
	wr(d, GEN11_VCS0_VCS1_INTR_MASK, ~0u, &d->reset_writes);
	wr(d, GEN11_VCS2_VCS3_INTR_MASK, ~0u, &d->reset_writes);
	wr(d, GEN11_VECS0_VECS1_INTR_MASK, ~0u, &d->reset_writes);

	wr(d, GEN11_GPM_WGBOXPERF_INTR_ENABLE, 0u, &d->reset_writes);
	wr(d, GEN11_GPM_WGBOXPERF_INTR_MASK, ~0u, &d->reset_writes);
	wr(d, GEN11_GUC_SG_INTR_ENABLE, 0u, &d->reset_writes);
	wr(d, GEN11_GUC_SG_INTR_MASK, ~0u, &d->reset_writes);

	wr(d, GEN11_CRYPTO_RSVD_INTR_ENABLE, 0u, &d->reset_writes);
	wr(d, GEN11_CRYPTO_RSVD_INTR_MASK, ~0u, &d->reset_writes);
}

void
parity_gen11_gt_irq_postinstall(struct parity_irq_dev *d)
{
	uint32_t irqs = GT_RENDER_USER_INTERRUPT;
	uint32_t guc_mask = 0u;   /* intel_uc_wants_guc(): 0 with enable_guc=0 */
	uint32_t dmask, smask;

	/*
	 * With execlists submission the driver owns the context switch, so it
	 * needs the CS interrupts.  The GuC-submission arm leaves them out; that
	 * is the other side of the backend decision, kept explicit here.
	 */
	if (d->submission != PARITY_SUBMISSION_GUC)
		irqs |= GT_CS_MASTER_ERROR_INTERRUPT |
			GT_CONTEXT_SWITCH_INTERRUPT |
			GT_WAIT_SEMAPHORE_INTERRUPT;

	dmask = irqs << 16 | irqs;
	smask = irqs << 16;

	d->gt_irqs = irqs;
	d->gt_dmask = dmask;
	d->gt_smask = smask;

	/* Enable RCS, BCS, VCS and VECS class interrupts. */
	wr(d, GEN11_RENDER_COPY_INTR_ENABLE, dmask, &d->postinstall_writes);
	wr(d, GEN11_VCS_VECS_INTR_ENABLE, dmask, &d->postinstall_writes);

	/* Unmask irqs on RCS, BCS, VCS and VECS engines. */
	wr(d, GEN11_RCS0_RSVD_INTR_MASK, ~smask, &d->postinstall_writes);
	wr(d, GEN11_BCS_RSVD_INTR_MASK, ~smask, &d->postinstall_writes);
	wr(d, GEN11_VCS0_VCS1_INTR_MASK, ~dmask, &d->postinstall_writes);
	wr(d, GEN11_VCS2_VCS3_INTR_MASK, ~dmask, &d->postinstall_writes);
	wr(d, GEN11_VECS0_VECS1_INTR_MASK, ~dmask, &d->postinstall_writes);

	if (guc_mask != 0u) {
		wr(d, GEN11_GUC_SG_INTR_ENABLE, guc_mask << 16, &d->postinstall_writes);
	}

	/*
	 * RPS interrupts will get enabled/disabled on demand when RPS itself is
	 * enabled/disabled (pm_ier = 0, pm_imr = ~0).
	 */
	wr(d, GEN11_GPM_WGBOXPERF_INTR_ENABLE, 0u, &d->postinstall_writes);
	wr(d, GEN11_GPM_WGBOXPERF_INTR_MASK, ~0u, &d->postinstall_writes);
}

/* ---------------- display/intel_display_irq.c ---------------- */

void
parity_gen11_display_irq_reset(struct parity_irq_dev *d)
{
	unsigned pipe, trans;

	if (!d->has_display)
		return;

	wr(d, GEN11_DISPLAY_INT_CTL, 0u, &d->reset_writes);

	if (d->display_ver >= 12) {
		for (trans = 0u; trans < 4u; trans++) {
			if ((d->cpu_transcoder_mask & (1u << trans)) == 0u)
				continue;
			if (!transcoder_power_on(d, trans))
				continue;
			wr(d, TRANS_PSR_IMR(trans), 0xffffffffu, &d->reset_writes);
			wr(d, TRANS_PSR_IIR(trans), 0xffffffffu, &d->reset_writes);
		}
	}

	for (pipe = 0u; pipe < PARITY_IRQ_MAX_PIPES; pipe++) {
		if ((d->pipe_mask & (1u << pipe)) == 0u)
			continue;
		if (!pipe_power_on(d, pipe))
			continue;
		gen3_irq_reset(d, GEN8_DE_PIPE_IMR(pipe), GEN8_DE_PIPE_IIR(pipe),
			GEN8_DE_PIPE_IER(pipe));
	}

	gen3_irq_reset(d, GEN8_DE_PORT_IMR, GEN8_DE_PORT_IIR, GEN8_DE_PORT_IER);
	gen3_irq_reset(d, GEN8_DE_MISC_IMR, GEN8_DE_MISC_IIR, GEN8_DE_MISC_IER);

	/* DISPLAY_VER < 14 -> the GEN11 HPD block, not PICA. */
	gen3_irq_reset(d, GEN11_DE_HPD_IMR, GEN11_DE_HPD_IIR, GEN11_DE_HPD_IER);

	if (d->pch != 0 && d->pch->type >= PARITY_PCH_ICP)
		gen3_irq_reset(d, SDEIMR, SDEIIR, SDEIER);
}

/* icp_irq_postinstall(): the south display block. */
static void
icp_irq_postinstall(struct parity_irq_dev *d)
{
	uint32_t mask = SDE_GMBUS_ICP;

	gen3_irq_init(d, SDEIMR, ~mask, SDEIER, 0xffffffffu, SDEIIR);
}

static void
gen8_de_irq_postinstall(struct parity_irq_dev *d)
{
	uint32_t de_pipe_masked;
	uint32_t de_pipe_enables;
	uint32_t de_port_masked;
	uint32_t de_port_enables;
	uint32_t de_misc_masked = GEN8_DE_EDP_PSR;
	unsigned pipe, trans;

	if (!d->has_display)
		return;

	de_pipe_masked = parity_gen8_de_pipe_fault_mask(d->display_ver) |
		GEN8_PIPE_CDCLK_CRC_DONE;
	de_port_masked = parity_gen8_de_port_aux_mask(d->display_ver);

	/* DISPLAY_VER >= 14 -> mtp; >= ICP -> icp; else ibx.  ADL-P: ICP path. */
	if (d->pch != 0 && d->pch->type >= PARITY_PCH_ICP)
		icp_irq_postinstall(d);

	if (d->display_ver < 11)
		de_misc_masked |= GEN8_DE_MISC_GSE;

	/* ver >= 11 (and < 14): DSI TE when the VBT reports a DSI port. */
	if (d->display_ver >= 11 && d->display_ver < 14) {
		if (d->dsi_present)
			de_port_masked |= DSI0_TE | DSI1_TE;
	}

	de_pipe_enables = de_pipe_masked |
		GEN8_PIPE_VBLANK |
		parity_gen8_de_pipe_underrun_mask(d->display_ver) |
		parity_gen8_de_pipe_flip_done_mask(d->display_ver);

	de_port_enables = de_port_masked;   /* not GLK/BXT/BDW: no extra hotplug bits */

	d->de_pipe_masked = de_pipe_masked;
	d->de_pipe_enables = de_pipe_enables;
	d->de_port_masked = de_port_masked;
	d->de_port_enables = de_port_enables;
	d->de_misc_masked = de_misc_masked;

	if (d->display_ver >= 12) {
		for (trans = 0u; trans < 4u; trans++) {
			if ((d->cpu_transcoder_mask & (1u << trans)) == 0u)
				continue;
			if (!transcoder_power_on(d, trans))
				continue;
			gen3_assert_iir_is_zero(d, TRANS_PSR_IIR(trans));
		}
	}

	for (pipe = 0u; pipe < PARITY_IRQ_MAX_PIPES; pipe++) {
		if ((d->pipe_mask & (1u << pipe)) == 0u)
			continue;

		d->de_irq_mask[pipe] = ~de_pipe_masked;

		if (pipe_power_on(d, pipe))
			gen3_irq_init(d, GEN8_DE_PIPE_IMR(pipe), d->de_irq_mask[pipe],
				GEN8_DE_PIPE_IER(pipe), de_pipe_enables,
				GEN8_DE_PIPE_IIR(pipe));
	}

	gen3_irq_init(d, GEN8_DE_PORT_IMR, ~de_port_masked,
		GEN8_DE_PORT_IER, de_port_enables, GEN8_DE_PORT_IIR);
	gen3_irq_init(d, GEN8_DE_MISC_IMR, ~de_misc_masked,
		GEN8_DE_MISC_IER, de_misc_masked, GEN8_DE_MISC_IIR);

	if (d->display_ver >= 11 && d->display_ver <= 13) {
		uint32_t de_hpd_masked = 0u;
		uint32_t de_hpd_enables = GEN11_DE_TC_HOTPLUG_MASK |
			GEN11_DE_TBT_HOTPLUG_MASK;

		gen3_irq_init(d, GEN11_DE_HPD_IMR, ~de_hpd_masked,
			GEN11_DE_HPD_IER, de_hpd_enables, GEN11_DE_HPD_IIR);
	}
}

void
parity_gen11_de_irq_postinstall(struct parity_irq_dev *d)
{
	if (!d->has_display)
		return;

	gen8_de_irq_postinstall(d);

	wr(d, GEN11_DISPLAY_INT_CTL, GEN11_DISPLAY_IRQ_ENABLE, &d->postinstall_writes);
}

/* ---------------- gt/intel_gt_irq.c: the GT half of the handler ---------------- */

/*
 * gen11_gt_engine_identity(): select the bit, then poll INTR_IDENTITY_REG until
 * DATA_VALID appears (the reference spins ~100us as an educated guess), then ack
 * the identity by writing DATA_VALID back.
 */
static uint32_t
gen11_gt_engine_identity(struct parity_irq_dev *d, unsigned bank, unsigned bit)
{
	uint32_t ident;
	unsigned spins;

	osdep_mmio_raw_write32(d->m, GEN11_IIR_REG_SELECTOR(bank), 1u << bit);

	/*
	 * A bounded spin, not a sleep: this runs in interrupt context.  The
	 * reference uses ~100us; each iteration is a real MMIO read, so the
	 * bound is expressed in reads rather than in wall time.
	 */
	for (spins = 0u; spins < 1000u; spins++) {
		ident = osdep_mmio_raw_read32(d->m, GEN11_INTR_IDENTITY_REG(bank));
		if (ident & GEN11_INTR_DATA_VALID)
			break;
	}
	d->gt_identity_reads++;

	if (!(ident & GEN11_INTR_DATA_VALID)) {
		kern_logf("i915: parity INTR_IDENTITY_REG%u:%u 0x%08x not valid!\n",
			bank, bit, ident);
		d->gt_identity_invalid++;
		return 0u;
	}

	osdep_mmio_raw_write32(d->m, GEN11_INTR_IDENTITY_REG(bank),
		GEN11_INTR_DATA_VALID);

	return ident;
}

/*
 * execlists_irq_handler() decode.  The bottom halves (RING_EIR handling, the
 * semaphore yield, the CSB tasklet and the breadcrumb signal) belong to the
 * submission backend in P6-c; here each source is DECODED and counted, and the
 * handler stays a pure acknowledge.
 */
static void
gt_engine_irq(struct parity_irq_dev *d, uint32_t iir)
{
	if (iir & GT_CS_MASTER_ERROR_INTERRUPT)
		d->gt_error_intr++;
	if (iir & GT_WAIT_SEMAPHORE_INTERRUPT)
		d->gt_semaphore_intr++;
	if (iir & GT_CONTEXT_SWITCH_INTERRUPT)
		d->gt_ctx_switch_intr++;
	if (iir & GT_RENDER_USER_INTERRUPT)
		d->gt_user_intr++;
}

static void
gen11_gt_identity_handler(struct parity_irq_dev *d, uint32_t identity)
{
	unsigned class = GEN11_INTR_ENGINE_CLASS(identity);
	unsigned instance = GEN11_INTR_ENGINE_INSTANCE(identity);
	uint32_t intr = GEN11_INTR_ENGINE_INTR(identity);

	d->last_gt_identity = identity;

	if (intr == 0u)
		return;

	if (class <= (unsigned)PARITY_MAX_ENGINE_CLASS &&
	    instance <= (unsigned)PARITY_MAX_ENGINE_INSTANCE) {
		struct parity_engine *e = 0;
		unsigned i;

		if (d->gt != 0) {
			for (i = 0u; i < d->gt->num_engines; i++)
				if ((unsigned)d->gt->engines[i].class == class &&
				    (unsigned)d->gt->engines[i].instance == instance) {
					e = &d->gt->engines[i];
					break;
				}
		}
		if (e != 0) {
			d->gt_engine_intrs++;
			gt_engine_irq(d, intr);
			return;
		}
	}

	if (class == (unsigned)PARITY_OTHER_CLASS) {
		/* GuC / GTPM (RPS) / KCR / GSC: no bottom half in this port. */
		d->gt_other_intrs++;
		return;
	}

	kern_logf("i915: parity unknown interrupt class=0x%x instance=0x%x "
		"intr=0x%x\n", class, instance, intr);
	d->gt_unknown_class++;
}

static void
gen11_gt_bank_handler(struct parity_irq_dev *d, unsigned bank)
{
	uint32_t intr_dw;
	unsigned bit;

	intr_dw = osdep_mmio_raw_read32(d->m, GEN11_GT_INTR_DW(bank));
	d->last_gt_intr_dw[bank] = intr_dw;

	for (bit = 0u; bit < 32u; bit++) {
		if ((intr_dw & (1u << bit)) == 0u)
			continue;
		gen11_gt_identity_handler(d, gen11_gt_engine_identity(d, bank, bit));
	}

	/* Clear must be AFTER the shared identity has been served per engine. */
	osdep_mmio_raw_write32(d->m, GEN11_GT_INTR_DW(bank), intr_dw);
	if (intr_dw != 0u)
		d->gt_bank_acks[bank]++;
}

void
parity_gen11_gt_irq_handler(struct parity_irq_dev *d, uint32_t master_ctl)
{
	unsigned bank;

	for (bank = 0u; bank < 2u; bank++)
		if (master_ctl & GEN11_GT_DW_IRQ(bank))
			gen11_gt_bank_handler(d, bank);
}

/* ---------------- display/intel_display_irq.c: the IRQ handler ---------------- */

/*
 * gen8_de_irq_handler().  Every enabled display source must be ACKED here: the
 * IIR bits latch, and an un-acked source keeps the master line asserted, which
 * would turn a single event into an interrupt storm.  The bottom halves
 * (vblank, flip done, underrun reporting, HPD, AUX) are P5+ work, so this
 * counts what it acked instead of servicing it -- and says so.
 */
static void
gen8_de_irq_handler(struct parity_irq_dev *d, uint32_t master_ctl)
{
	uint32_t iir;
	unsigned pipe;

	if (master_ctl & GEN8_DE_MISC_IRQ) {
		iir = rd(d, GEN8_DE_MISC_IIR);
		if (iir != 0u) {
			osdep_mmio_write32(d->m, GEN8_DE_MISC_IIR, iir);
			d->de_misc_acks++;
		} else {
			d->de_lied_count++;   /* master control interrupt lied (DE MISC) */
		}
	}

	if (d->display_ver >= 11 && (master_ctl & GEN11_DE_HPD_IRQ)) {
		iir = rd(d, GEN11_DE_HPD_IIR);
		if (iir != 0u) {
			osdep_mmio_write32(d->m, GEN11_DE_HPD_IIR, iir);
			d->de_hpd_acks++;
		} else {
			d->de_lied_count++;   /* (DE HPD) */
		}
	}

	if (master_ctl & GEN8_DE_PORT_IRQ) {
		iir = rd(d, GEN8_DE_PORT_IIR);
		if (iir != 0u) {
			osdep_mmio_write32(d->m, GEN8_DE_PORT_IIR, iir);
			d->de_port_acks++;
		} else {
			d->de_lied_count++;   /* (DE PORT) */
		}
	}

	for (pipe = 0u; pipe < PARITY_IRQ_MAX_PIPES; pipe++) {
		uint32_t fault_errors;

		if ((d->pipe_mask & (1u << pipe)) == 0u)
			continue;
		if (!(master_ctl & GEN8_DE_PIPE_IRQ(pipe)))
			continue;
		/* admission: counted in first, then the gate is checked (the closing side sets the gate, then reads the count) */
		(void)__atomic_add_fetch(&d->pipe_inflight[pipe], 1u, __ATOMIC_SEQ_CST);
		if (__atomic_load_n(&d->pipe_closed[pipe], __ATOMIC_SEQ_CST)) {
			(void)__atomic_sub_fetch(&d->pipe_inflight[pipe], 1u, __ATOMIC_SEQ_CST);
			d->pipe_refused[pipe]++;
			continue;
		}

		iir = rd(d, GEN8_DE_PIPE_IIR(pipe));
		if (iir == 0u) {
			d->de_lied_count++;   /* (DE PIPE) */
			(void)__atomic_sub_fetch(&d->pipe_inflight[pipe], 1u, __ATOMIC_SEQ_CST);
			continue;
		}

		osdep_mmio_write32(d->m, GEN8_DE_PIPE_IIR(pipe), iir);
		d->de_pipe_iir_acks[pipe]++;
		d->last_de_pipe_iir[pipe] = iir;

		if (iir & GEN8_PIPE_VBLANK) {
			d->de_vblank_count[pipe]++;
			/* intel_handle_vblank() -> drm_handle_vblank(): counted and waiters woken only while enabled */
			if (d->vbl != 0 && d->vbl->inited) {
				unsigned long vf = spin_lock_irqsave(&d->vbl->lock);

				if (d->vbl->enabled[pipe]) {
					d->vbl->count[pipe]++;
					parity_kcomplete(&d->vbl->wake[pipe]);
				}
				spin_unlock_irqrestore(&d->vbl->lock, vf);
			}
		}

		if (iir & parity_gen8_de_pipe_flip_done_mask(d->display_ver))
			d->de_flip_done_count++;

		if (iir & parity_gen8_de_pipe_underrun_mask(d->display_ver))
			d->de_underrun_count++;

		fault_errors = iir & parity_gen8_de_pipe_fault_mask(d->display_ver);
		if (fault_errors != 0u) {
			d->de_fault_count++;
			kern_logf("i915: parity Fault errors on pipe %c: 0x%08x\n",
				(char)('A' + pipe), fault_errors);
		}
		(void)__atomic_sub_fetch(&d->pipe_inflight[pipe], 1u, __ATOMIC_SEQ_CST);
	}

	/*
	 * PCH: ADL-P is PCH_ADP (>= PCH_ICP) and DISPLAY_VER < 14, so there is no
	 * PICA block -- read and ack SDEIIR directly.
	 */
	if (d->pch != 0 && d->pch->type >= PARITY_PCH_ICP &&
	    (master_ctl & GEN8_DE_PCH_IRQ)) {
		iir = rd(d, SDEIIR);
		if (iir != 0u) {
			osdep_mmio_write32(d->m, SDEIIR, iir);
			d->de_pch_acks++;
		} else {
			d->de_lied_count++;   /* (SDE) */
		}
	}
}

/*
 * gen11_display_irq_handler(): gate the display block off while its sources are
 * read and acked, then re-enable it.
 */
void
parity_gen11_display_irq_handler(struct parity_irq_dev *d)
{
	uint32_t disp_ctl = osdep_mmio_raw_read32(d->m, GEN11_DISPLAY_INT_CTL);

	d->last_disp_ctl = disp_ctl;

	osdep_mmio_raw_write32(d->m, GEN11_DISPLAY_INT_CTL, 0u);
	gen8_de_irq_handler(d, disp_ctl);
	osdep_mmio_raw_write32(d->m, GEN11_DISPLAY_INT_CTL, GEN11_DISPLAY_IRQ_ENABLE);
}

/* ---------------- i915_irq.c: reset / postinstall / handler ---------------- */

void
parity_intel_irq_reset(struct parity_irq_dev *d)
{
	(void)gen11_master_intr_disable(d);

	parity_gen11_gt_irq_reset(d);
	parity_gen11_display_irq_reset(d);

	gen3_irq_reset(d, GEN11_GU_MISC_IMR, GEN11_GU_MISC_IIR, GEN11_GU_MISC_IER);
	gen3_irq_reset(d, GEN8_PCU_IMR, GEN8_PCU_IIR, GEN8_PCU_IER);
}

void
parity_intel_irq_postinstall(struct parity_irq_dev *d)
{
	uint32_t gu_misc_masked = GEN11_GU_MISC_GSE;

	parity_gen11_gt_irq_postinstall(d);
	parity_gen11_de_irq_postinstall(d);

	gen3_irq_init(d, GEN11_GU_MISC_IMR, ~gu_misc_masked,
		GEN11_GU_MISC_IER, gu_misc_masked, GEN11_GU_MISC_IIR);

	d->reached_postinstall = 1;

	gen11_master_intr_enable(d);
	osdep_mmio_posting_read32(d->m, GEN11_GFX_MSTR_IRQ);
	d->reached_master_enable = 1;
}

/*
 * gen11_irq_handler().  The HAL hands us an ack token and expects no return
 * value, so the IRQ_NONE / IRQ_HANDLED outcome is recorded in the device state.
 * The EOI is ours to send, and it is sent on every path.
 */
static void
gen11_irq_handler_body(int irq, hal_irq_ack_t ack, void *arg)
{
	struct parity_irq_dev *d = (struct parity_irq_dev *)arg;
	uint32_t master_ctl;
	uint32_t gu_misc_iir;

	(void)irq;

	d->irq_count++;

	if (!d->irqs_enabled) {
		d->irq_none_count++;
		hal_irq_send_eoi(ack);
		return;
	}

	master_ctl = gen11_master_intr_disable(d);
	d->last_master_ctl = master_ctl;
	if (master_ctl == 0u) {
		gen11_master_intr_enable(d);
		d->irq_none_count++;
		hal_irq_send_eoi(ack);
		return;
	}

	/*
	 * Find, queue (onto bottom-halves), then clear each source.  The DISPLAY
	 * sources are read and ACKED (they must be, or an enabled source re-asserts
	 * forever); their bottom halves, and the whole GT side, belong to P5 / P6,
	 * so for now what was acked is counted rather than serviced.
	 */
	if (master_ctl & (GEN11_GT_DW_IRQ(0) | GEN11_GT_DW_IRQ(1))) {
		d->gt_irq_count++;
		parity_gen11_gt_irq_handler(d, master_ctl);
	}

	if (master_ctl & GEN11_DISPLAY_IRQ) {
		d->display_irq_count++;
		parity_gen11_display_irq_handler(d);
	}

	/* gen11_gu_misc_irq_ack(): read and clear GU_MISC IIR while masked. */
	gu_misc_iir = 0u;
	if (master_ctl & GEN11_GU_MISC_IRQ) {
		gu_misc_iir = rd(d, GEN11_GU_MISC_IIR);
		if (gu_misc_iir != 0u)
			osdep_mmio_write32(d->m, GEN11_GU_MISC_IIR, gu_misc_iir);
	}
	d->last_gu_misc_iir = gu_misc_iir;

	gen11_master_intr_enable(d);

	d->irq_handled_count++;
	hal_irq_send_eoi(ack);
}

/* the handler the HAL calls: the reference's body, bracketed by the counts intel_synchronize_irq() waits on */
static void
gen11_irq_handler(int irq, hal_irq_ack_t ack, void *arg)
{
	struct parity_irq_dev *d = (struct parity_irq_dev *)arg;

	(void)__atomic_add_fetch(&d->handler_entries, 1u, __ATOMIC_SEQ_CST);
	gen11_irq_handler_body(irq, ack, arg);
	(void)__atomic_add_fetch(&d->handler_exits, 1u, __ATOMIC_SEQ_CST);
}

/* ---------------- display/intel_display_irq.c: power-well hooks, pipe IRQ mask, vblank ---------------- */

void
parity_irq_vblank_init(struct parity_irq_dev *d, struct parity_irq_vblank *v)
{
	unsigned p;

	for (p = 0u; p < sizeof(*v); p++)
		((char *)v)[p] = 0;
	spin_init(&v->lock, LOCK_RANK_DEVICE, "parity-irq-lock");
	for (p = 0u; p < PARITY_IRQ_MAX_PIPES; p++)
		parity_kcompletion_init(&v->wake[p], "parity-vblank");
	v->inited = 1;
	d->vbl = v;
}

int
parity_intel_synchronize_irq(struct parity_irq_dev *d)
{
	unsigned seen = __atomic_load_n(&d->handler_entries, __ATOMIC_SEQ_CST);
	unsigned waited;

	if (d->vbl != 0)
		d->vbl->sync_calls++;
	for (waited = 0u; waited < 100000u; waited += 10u) {
		if ((int)(__atomic_load_n(&d->handler_exits, __ATOMIC_SEQ_CST) - seen) >= 0)
			return 0;
		if (parity_udelay(10u) != 0) {
			if (d->vbl != 0)
				d->vbl->sync_time_faults++;
			kern_logf("i915: parity intel_synchronize_irq: the time base failed while waiting (not a timeout)\n");
			return -EIO;
		}
	}
	if (d->vbl != 0)
		d->vbl->sync_timeouts++;
	kern_logf("i915: parity intel_synchronize_irq: a handler invocation did not finish (entries=%u exits=%u)\n",
		seen, d->handler_exits);
	return -ETIMEDOUT;
}

/* gen8_irq_power_well_post_enable() */
void
parity_gen8_irq_power_well_post_enable(struct parity_irq_dev *d, unsigned pipe_mask)
{
	uint32_t extra_ier = GEN8_PIPE_VBLANK | parity_gen8_de_pipe_underrun_mask(d->display_ver) |
		parity_gen8_de_pipe_flip_done_mask(d->display_ver);
	unsigned long flags;
	unsigned pipe;

	if (d->vbl == 0 || !d->vbl->inited)
		return;
	flags = spin_lock_irqsave(&d->vbl->lock);
	if (!d->irqs_enabled) {
		d->vbl->skipped_irqs_disabled++;
		spin_unlock_irqrestore(&d->vbl->lock, flags);
		return;
	}
	d->vbl->post_enable_calls++;
	for (pipe = 0u; pipe < PARITY_IRQ_MAX_PIPES; pipe++) {
		if ((d->pipe_mask & pipe_mask & (1u << pipe)) == 0u)
			continue;
		/* GEN8_IRQ_INIT_NDX(uncore, DE_PIPE, pipe, de_irq_mask[pipe], ~de_irq_mask[pipe] | extra_ier) */
		gen3_irq_init(d, GEN8_DE_PIPE_IMR(pipe), d->de_irq_mask[pipe],
			GEN8_DE_PIPE_IER(pipe), ~d->de_irq_mask[pipe] | extra_ier, GEN8_DE_PIPE_IIR(pipe));
		d->vbl->post_imr[pipe] = d->de_irq_mask[pipe];
		d->vbl->post_ier[pipe] = ~d->de_irq_mask[pipe] | extra_ier;
		__atomic_store_n(&d->pipe_closed[pipe], 0, __ATOMIC_SEQ_CST);     /* the handler may enter it again */
	}
	spin_unlock_irqrestore(&d->vbl->lock, flags);
}

int
parity_irq_drain_pipes(struct parity_irq_dev *d, unsigned pipe_mask, unsigned timeout_us)
{
	unsigned waited, pipe, busy;

	for (waited = 0u;; waited += 10u) {
		busy = 0u;
		for (pipe = 0u; pipe < PARITY_IRQ_MAX_PIPES; pipe++)
			if ((pipe_mask & (1u << pipe)) != 0u && __atomic_load_n(&d->pipe_inflight[pipe], __ATOMIC_SEQ_CST) != 0u)
				busy |= 1u << pipe;
		if (busy == 0u)
			return 0;
		if (waited >= timeout_us) {
			if (d->vbl != 0)
				d->vbl->drain_timeouts++;
			kern_logf("i915: parity IRQ drain: the handler did not leave pipe(s) 0x%x within %u us\n", busy, timeout_us);
			return -ETIMEDOUT;
		}
		if (parity_udelay(10u) != 0) {
			if (d->vbl != 0)
				d->vbl->drain_time_faults++;
			kern_logf("i915: parity IRQ drain: the time base failed while waiting (not a timeout)\n");
			return -EIO;
		}
	}
}

/*
 * gen8_irq_power_well_pre_disable(): the reference resets the pipe's interrupt registers and then completes
 * intel_synchronize_irq() before the well goes off.  Here: close the pipe's admission (no new handler work enters its
 * registers), reset the source, then drain what had already entered.  The pipe stays closed until the next
 * post-enable.  A drain that does not complete is returned, so the caller keeps the well on.
 */
int
parity_gen8_irq_power_well_pre_disable(struct parity_irq_dev *d, unsigned pipe_mask)
{
	unsigned long flags;
	unsigned pipe, closing = 0u;

	if (d->vbl == 0 || !d->vbl->inited)
		return 0;
	flags = spin_lock_irqsave(&d->vbl->lock);
	if (!d->irqs_enabled) {
		d->vbl->skipped_irqs_disabled++;
		spin_unlock_irqrestore(&d->vbl->lock, flags);
		return 0;
	}
	d->vbl->pre_disable_calls++;
	for (pipe = 0u; pipe < PARITY_IRQ_MAX_PIPES; pipe++) {
		if ((d->pipe_mask & pipe_mask & (1u << pipe)) == 0u)
			continue;
		__atomic_store_n(&d->pipe_closed[pipe], 1, __ATOMIC_SEQ_CST);
		closing |= 1u << pipe;
		/* GEN8_IRQ_RESET_NDX(uncore, DE_PIPE, pipe) */
		gen3_irq_reset(d, GEN8_DE_PIPE_IMR(pipe), GEN8_DE_PIPE_IIR(pipe), GEN8_DE_PIPE_IER(pipe));
	}
	spin_unlock_irqrestore(&d->vbl->lock, flags);

	/* make sure we're done processing display irqs (of these pipes) */
	d->vbl->sync_calls++;
	return parity_irq_drain_pipes(d, closing, 100000u);
}

/* bdw_update_pipe_irq(): called with the IRQ lock held */
static void
bdw_update_pipe_irq(struct parity_irq_dev *d, unsigned pipe, uint32_t interrupt_mask, uint32_t enabled_irq_mask)
{
	uint32_t new_val;

	if (enabled_irq_mask & ~interrupt_mask)
		kern_logf("i915: parity WARN bdw_update_pipe_irq: enabled bits outside the mask\n");
	if (!d->irqs_enabled) {
		kern_logf("i915: parity WARN bdw_update_pipe_irq: interrupts are not enabled\n");
		return;
	}
	new_val = d->de_irq_mask[pipe];
	new_val &= ~interrupt_mask;
	new_val |= (~enabled_irq_mask & interrupt_mask);
	if (new_val != d->de_irq_mask[pipe]) {
		d->de_irq_mask[pipe] = new_val;
		osdep_mmio_write32(d->m, GEN8_DE_PIPE_IMR(pipe), d->de_irq_mask[pipe]);
		osdep_mmio_posting_read32(d->m, GEN8_DE_PIPE_IMR(pipe));
	}
}

/*
 * bdw_enable_vblank() / bdw_disable_vblank().  gen11_dsi_configure_te() returns false unless the crtc drives a DSI
 * command-mode panel (not on this path).  HAS_PSR(): the reference then calls drm_crtc_vblank_restore(), which
 * re-estimates the DRM vblank count from time across PSR self-refresh; there is no DRM vblank count here (the count
 * is of handled interrupts, and waits also require the hardware frame counter to move), and PSR is not enabled on
 * this path -- an explicit adaptation, not reached with PSR.
 */
/* bdw_enable_vblank() / bdw_disable_vblank() without their lock: the callers below hold it (one lock, one rule) */
static int
bdw_enable_vblank_locked(struct parity_irq_dev *d, unsigned pipe)
{
	bdw_update_pipe_irq(d, pipe, GEN8_PIPE_VBLANK, GEN8_PIPE_VBLANK);
	return 0;
}

static void
bdw_disable_vblank_locked(struct parity_irq_dev *d, unsigned pipe)
{
	bdw_update_pipe_irq(d, pipe, GEN8_PIPE_VBLANK, 0u);
}

/*
 * The reference counts, the enabled state, the IMR bit and the handler's count all change under the IRQ lock.
 * ADAPTATION: the last put masks at once.  Linux's vblank_disable_immediate still disables through the vblank core
 * after the pending vblank / event processing; this limited path has no DRM events or workers, so it masks inside
 * put().  Not to be carried over to a present / flip path as it is.
 */
int
parity_drm_vblank_get(struct parity_irq_dev *d, unsigned pipe)
{
	unsigned long flags;

	if (d->vbl == 0 || !d->vbl->inited || pipe >= PARITY_IRQ_MAX_PIPES)
		return -EINVAL;
	flags = spin_lock_irqsave(&d->vbl->lock);
	if (!d->irqs_enabled) {
		spin_unlock_irqrestore(&d->vbl->lock, flags);
		return -EINVAL;
	}
	if (d->vbl->refs[pipe]++ == 0u) {
		d->vbl->enable_calls[pipe]++;
		d->vbl->enabled[pipe] = 1;
		(void)bdw_enable_vblank_locked(d, pipe);
	}
	spin_unlock_irqrestore(&d->vbl->lock, flags);
	return 0;
}

void
parity_drm_vblank_put(struct parity_irq_dev *d, unsigned pipe)
{
	unsigned long flags;

	if (d->vbl == 0 || !d->vbl->inited || pipe >= PARITY_IRQ_MAX_PIPES)
		return;
	flags = spin_lock_irqsave(&d->vbl->lock);
	if (d->vbl->refs[pipe] != 0u && --d->vbl->refs[pipe] == 0u) {
		d->vbl->disable_calls[pipe]++;
		bdw_disable_vblank_locked(d, pipe);
		d->vbl->enabled[pipe] = 0;
	}
	spin_unlock_irqrestore(&d->vbl->lock, flags);
}

/* one consistent view of a pipe's vblank state, read under the IRQ lock */
static void
vbl_snapshot(struct parity_irq_dev *d, unsigned pipe, uint32_t *count, int *enabled, unsigned *refs)
{
	unsigned long flags = spin_lock_irqsave(&d->vbl->lock);

	if (count != 0)
		*count = d->vbl->count[pipe];
	if (enabled != 0)
		*enabled = d->vbl->enabled[pipe];
	if (refs != 0)
		*refs = d->vbl->refs[pipe];
	spin_unlock_irqrestore(&d->vbl->lock, flags);
}

int
parity_wait_vblank(struct parity_irq_dev *d, unsigned pipe, unsigned n, unsigned timeout_ms,
	uint32_t (*read_frame)(void *ctx), void *frame_ctx, uint32_t *count_seen)
{
	uint64_t deadline;
	uint32_t start, frame0 = 0u;
	int ok = 0;

	unsigned long flags;

	if (d->vbl == 0 || !d->vbl->inited || pipe >= PARITY_IRQ_MAX_PIPES || n == 0u)
		return -EINVAL;
	flags = spin_lock_irqsave(&d->vbl->lock);
	if (d->vbl->refs[pipe] == 0u) {
		spin_unlock_irqrestore(&d->vbl->lock, flags);
		return -EINVAL;
	}
	if (d->vbl->waiting[pipe]) {            /* a second waiter would re-arm the first one's wake-up */
		d->vbl->second_waiter_refusals++;
		spin_unlock_irqrestore(&d->vbl->lock, flags);
		return -EBUSY;
	}
	d->vbl->waiting[pipe] = 1;
	parity_kreinit_completion(&d->vbl->wake[pipe]);
	start = d->vbl->count[pipe];
	spin_unlock_irqrestore(&d->vbl->lock, flags);
	if (read_frame != 0)
		frame0 = read_frame(frame_ctx);
	deadline = sched_ticks() + (timeout_ms + 9u) / 10u + 1u;
	for (;;) {
		uint32_t now;

		vbl_snapshot(d, pipe, &now, 0, 0);
		if ((uint32_t)(now - start) >= n &&
		    (read_frame == 0 || read_frame(frame_ctx) != frame0)) {
			ok = 1;
			break;
		}
		if (!parity_kwait(&d->vbl->wake[pipe], deadline))
			break;
	}
	if (count_seen != 0) {
		uint32_t now;

		vbl_snapshot(d, pipe, &now, 0, 0);
		*count_seen = now - start;
	}
	flags = spin_lock_irqsave(&d->vbl->lock);
	d->vbl->waiting[pipe] = 0;
	spin_unlock_irqrestore(&d->vbl->lock, flags);
	return ok ? 0 : -ETIMEDOUT;
}

static void
pw_irq_post_enable(void *ctx, unsigned pipe_mask)
{
	parity_gen8_irq_power_well_post_enable(ctx, pipe_mask);
}

static int
pw_irq_pre_disable(void *ctx, unsigned pipe_mask)
{
	return parity_gen8_irq_power_well_pre_disable(ctx, pipe_mask);
}

const struct parity_pw_irq_ops parity_pw_irq_ops = { pw_irq_post_enable, pw_irq_pre_disable };

int
parity_intel_irq_install(struct parity_irq_dev *d)
{
	int rc;

	/*
	 * We enable some interrupt sources in our postinstall hooks, so mark
	 * interrupts as enabled _before_ actually enabling them to avoid special
	 * cases in our ordering checks.
	 */
	d->irqs_enabled = 1;
	d->irq_enabled = 1;

	/* The power-well post-enable IRQ path is gated on this same flag. */
	if (d->pwc != 0)
		d->pwc->irqs_enabled = 1;

	parity_intel_irq_reset(d);

	/*
	 * request_irq(): the vector was allocated in P2 with no handler, so this
	 * is the HAL's attach half.  Until it succeeds an arriving MSI is masked
	 * and acked by the HAL, never dispatched.
	 */
	rc = hal_irq_attach_msi(d->msi_irq, gen11_irq_handler, d);
	if (rc != HAL_OK) {
		d->irq_enabled = 0;
		if (d->pwc != 0)
			d->pwc->irqs_enabled = 0;
		kern_logf("i915: parity intel_irq_install: hal_irq_attach_msi(irq=%d) "
			"failed rc=%d\n", d->msi_irq, rc);
		return -EIO;
	}
	d->handler_attached = 1;

	parity_intel_irq_postinstall(d);

	return 0;
}

void
parity_intel_irq_uninstall(struct parity_irq_dev *d)
{
	if (!d->irq_enabled)
		return;

	if (d->vbl != 0 && d->vbl->inited) {
		unsigned p;

		for (p = 0u; p < PARITY_IRQ_MAX_PIPES; p++)
			if (d->vbl->refs[p] != 0u)
				kern_logf("i915: parity WARN intel_irq_uninstall: pipe %u still holds %u vblank reference(s)\n", p, d->vbl->refs[p]);
	}
	/*
	 * Mask and disable every source FIRST -- detaching the handler does not
	 * stop the device from sending messages.
	 */
	parity_intel_irq_reset(d);

	d->irq_enabled = 0;
	d->irqs_enabled = 0;
	if (d->pwc != 0)
		d->pwc->irqs_enabled = 0;

	if (d->handler_attached) {
		int rc = hal_irq_detach_msi_sync(d->msi_irq, gen11_irq_handler, d);

		if (rc != HAL_OK)
			kern_logf("i915: parity intel_irq_uninstall: detach_msi_sync "
				"(irq=%d) rc=%d\n", d->msi_irq, rc);
		d->handler_attached = 0;
	}
}
