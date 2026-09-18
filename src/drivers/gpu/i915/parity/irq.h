/*
 * WS031 Linux-parity — P4: intel_irq_install() / intel_irq_uninstall().
 *
 * Ports i915_irq.c's gen11 path:
 *
 *   intel_irq_install:
 *     runtime_pm.irqs_enabled = true; irq_enabled = true   (BEFORE postinstall)
 *     intel_irq_reset      -> gen11_irq_reset
 *     request_irq          -> hal_irq_attach_msi (the vector was allocated in P2)
 *     intel_irq_postinstall-> gen11_irq_postinstall
 *
 * The MSI vector itself is NOT allocated here: P2 already called
 * hal_irq_alloc_msi() and deliberately left the handler unattached, so the HAL's
 * split MSI contract (allocate / attach / detach-sync / free) maps 1:1 onto the
 * reference's request_irq / free_irq without any HAL change.
 *
 * Two adaptation differences from the reference, both forced by the HAL contract:
 *   - the handler is `void (*)(int, hal_irq_ack_t, void *)` and must itself call
 *     hal_irq_send_eoi(ack); Linux returns IRQ_HANDLED/IRQ_NONE.  With MSI
 *     (never shared) that return value only feeds spurious-IRQ detection, so the
 *     outcome is recorded in the device state instead of being returned.
 *   - IRQF_SHARED is meaningless for MSI and has no HAL equivalent.
 */
#ifndef PARITY_IRQ_H
#define PARITY_IRQ_H

#include <stdint.h>

struct osdep_mmio;
struct parity_power_domains;
struct parity_pw_ctx;
struct parity_pch_state;
struct parity_gt_mmio;

#define PARITY_IRQ_MAX_PIPES 4

/* Which arm of gen11_gt_irq_postinstall() the submission backend selects. */
enum parity_gt_submission {
	PARITY_SUBMISSION_EXECLISTS = 0,  /* enable_guc=0: CS_MASTER_ERROR|CTX_SWITCH|SEMAPHORE */
	PARITY_SUBMISSION_GUC             /* GuC submission: those three are left out */
};

struct parity_irq_dev {
	struct osdep_mmio *m;
	struct parity_power_domains *pd;
	struct parity_pw_ctx *pwc;
	const struct parity_pch_state *pch;
	/*
	 * The GT engine objects (P6-0a).  The GT half of the handler needs them
	 * to map an interrupt identity (class, instance) back to an engine.
	 */
	struct parity_gt_mmio *gt;

	int display_ver;
	unsigned pipe_mask;              /* runtime pipe_mask (ADL-P: 0xf) */
	unsigned cpu_transcoder_mask;    /* runtime cpu_transcoder_mask */
	int has_display;
	int submission;                  /* enum parity_gt_submission */
	int dsi_present;                 /* intel_bios_is_dsi_present() */

	/* Reference state. */
	int irqs_enabled;                /* runtime_pm.irqs_enabled */
	int irq_enabled;                 /* i915->irq_enabled */
	uint32_t de_irq_mask[PARITY_IRQ_MAX_PIPES];

	/* Computed masks, kept for the diagnostics and the tests. */
	uint32_t gt_irqs, gt_dmask, gt_smask;
	uint32_t de_pipe_masked, de_pipe_enables;
	uint32_t de_port_masked, de_port_enables;
	uint32_t de_misc_masked;

	/* HAL attachment. */
	int msi_irq;                     /* logical IRQ from hal_irq_alloc_msi (P2) */
	int handler_attached;

	/* Handler-observed state (an MSI handler cannot return IRQ_NONE upwards). */
	volatile unsigned irq_count;          /* handler invocations */
	volatile unsigned irq_none_count;     /* master_ctl == 0 (not ours / spurious) */
	volatile unsigned irq_handled_count;
	volatile uint32_t last_master_ctl;
	volatile uint32_t last_gu_misc_iir;
	volatile unsigned gt_irq_count, display_irq_count;
	/*
	 * Display source accounting.  The bottom halves (vblank / flip-done /
	 * underrun reporting / HPD / AUX) belong to P5+, so the handler ACKS each
	 * source -- which it must, or an enabled source would re-assert forever --
	 * and counts what it saw.  "lied" counts a master bit set with a zero IIR,
	 * which the reference also treats as an error worth reporting.
	 */
	volatile unsigned de_misc_acks, de_hpd_acks, de_port_acks, de_pch_acks;
	volatile unsigned de_pipe_iir_acks[PARITY_IRQ_MAX_PIPES];
	volatile unsigned de_vblank_count[PARITY_IRQ_MAX_PIPES];
	volatile unsigned de_flip_done_count, de_underrun_count, de_fault_count;
	volatile unsigned de_lied_count;
	/*
	 * GT source accounting.  As on the display side every asserted bank must
	 * be ACKED, and each identity register handshake must complete, or the
	 * master line stays asserted.  The engine bottom halves (breadcrumbs,
	 * CSB tasklet) arrive with the submission backend in P6-c; until then
	 * what was acked is counted.
	 */
	volatile unsigned gt_bank_acks[2];
	volatile unsigned gt_identity_reads;
	volatile unsigned gt_identity_invalid;   /* DATA_VALID never appeared */
	volatile unsigned gt_engine_intrs;
	volatile unsigned gt_other_intrs;
	volatile unsigned gt_unknown_class;
	volatile unsigned gt_user_intr, gt_ctx_switch_intr;
	volatile unsigned gt_error_intr, gt_semaphore_intr;
	volatile uint32_t last_gt_intr_dw[2];
	volatile uint32_t last_gt_identity;
	volatile uint32_t last_disp_ctl;
	volatile uint32_t last_de_pipe_iir[PARITY_IRQ_MAX_PIPES];

	/* Diagnostics. */
	unsigned reset_writes;
	unsigned postinstall_writes;
	int reached_postinstall;
	int reached_master_enable;
};

/*
 * intel_irq_install().  Returns 0, or a negative errno from the handler attach
 * (in which case irq_enabled is cleared again, as the reference does).
 */
int parity_intel_irq_install(struct parity_irq_dev *d);

/*
 * intel_irq_uninstall(): reset the sources, detach the handler (synchronously --
 * the HAL guarantees no handler is running on any CPU when this returns), and
 * clear the enabled flags.  The MSI vector itself stays allocated; probe.c frees
 * it with the rest of the P2 resources.
 */
void parity_intel_irq_uninstall(struct parity_irq_dev *d);

/* The pieces, exposed so the GPU-free tests can drive them individually. */
void parity_intel_irq_reset(struct parity_irq_dev *d);
void parity_intel_irq_postinstall(struct parity_irq_dev *d);
void parity_gen11_gt_irq_reset(struct parity_irq_dev *d);
void parity_gen11_gt_irq_postinstall(struct parity_irq_dev *d);
void parity_gen11_display_irq_reset(struct parity_irq_dev *d);
void parity_gen11_de_irq_postinstall(struct parity_irq_dev *d);
/*
 * The display half of the interrupt handler.  Exposed so the GPU-free tests can
 * inject a DISPLAY_INT_CTL / IIR state and verify that every enabled source is
 * ACKED (an un-acked source would re-assert forever).
 */
void parity_gen11_display_irq_handler(struct parity_irq_dev *d);

/*
 * The GT half of the interrupt handler (gen11_gt_irq_handler).  Exposed so the
 * GPU-free tests can inject a bank/identity state and verify the identity
 * handshake and the per-engine decode.
 */
void parity_gen11_gt_irq_handler(struct parity_irq_dev *d, uint32_t master_ctl);

/* The ADL-P mask helpers (gen8_de_*_mask), exposed for the tests. */
uint32_t parity_gen8_de_pipe_fault_mask(int display_ver);
uint32_t parity_gen8_de_port_aux_mask(int display_ver);
uint32_t parity_gen8_de_pipe_underrun_mask(int display_ver);
uint32_t parity_gen8_de_pipe_flip_done_mask(int display_ver);

#endif /* PARITY_IRQ_H */
