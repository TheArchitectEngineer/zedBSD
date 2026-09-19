/*
 * WS031 Linux-parity attach — public entry.
 *
 * A diagnostic, single-path attach that walks the reference i915 probe order
 * (P0..P2 so far) through the OS adaptation layer, stopping at a chosen stage
 * and reporting one of STOPPED_AT / BLOCKED / FAILED.  It never publishes the
 * device.  Selected once at the top of attach, instead of the legacy path — the
 * two never mix, and there is no fallback from parity to legacy.
 */
#ifndef PARITY_PARITY_H
#define PARITY_PARITY_H

struct i915_device;

/* Stage the reference probe reached (execution order, not implementation order). */
enum parity_stage {
	PARITY_STAGE_NONE = 0,
	PARITY_STAGE_P0,     /* PCI enable / device create / early probe / vGPU / GT probe */
	PARITY_STAGE_P1,     /* bridge / BAR / uncore / device info / GT MMIO / sanitize */
	PARITY_STAGE_P2,     /* DMA / GGTT / memory region / bus master / MSI / opregion / DRAM / bw */
	PARITY_STAGE_P3      /* intel_display_driver_probe_noirq: display noirq bring-up */
};

/* Diagnostic outcome — kept distinct from a Linux API success return. */
enum parity_outcome {
	PARITY_STOPPED = 0,  /* reached the requested stop stage cleanly */
	PARITY_BLOCKED,      /* hit an unimplemented adaptation-layer / dependency */
	PARITY_FAILED        /* a step that actually ran returned failure */
};

struct parity_result {
	enum parity_stage reached;   /* last stage entered */
	enum parity_outcome outcome;
	int error;                   /* the -errno of a FAILED step, else 0 */
	const char *where;           /* static label of the stop point (blocked/failed) */
	const char *last_completed;  /* last op that actually completed (distinct from where) */
	/* a display test run inside the probe (0 = none ran); kept apart from the probe outcome */
	int lcd_test_ran, lcd_test_pass, lcd_cleanup_rc, lcd_retained;
	const char *lcd_first_anomaly_stage;
};

/*
 * Run the parity attach up to `stop_after` (inclusive), fill *out, and always
 * tear down whatever it acquired.  Returns 0 (it is a diagnostic; it never leaves
 * the device published).  Safe to call in place of the legacy attach body.
 */
int drv_i915_parity_attach(struct i915_device *device, enum parity_stage stop_after,
			   struct parity_result *out);

#endif /* PARITY_PARITY_H */
