/*
 * WS031 Linux-parity — P7: the rest of i915_driver_probe() after i915_gem_init().
 *
 * The reference (i915_driver.c, 6.8.12):
 *   intel_pxp_init(i915)                 see pxp.h (P7-0)
 *   intel_display_driver_probe(i915)     P7-a, display/intel_display_driver.c:
 *     intel_initial_commit()             an atomic commit that recomputes the
 *                                        plane state of every ACTIVE crtc; with
 *                                        no active crtc it commits nothing
 *     intel_overlay_setup()              HAS_OVERLAY: gen2-4 only
 *     intel_fbdev_init()                 CONFIG_DRM_FBDEV_EMULATION
 *     intel_hpd_init()                   every pin HPD_ENABLED, then
 *                                        hpd_irq_setup = gen11_hpd_irq_setup:
 *                                        GEN11_DE_HPD_IMR, TC/TBT_HOTPLUG_CTL,
 *                                        and (PCH >= ICP) SHPD_FILTER_CNT,
 *                                        SDEIMR, SHOTPLUG_CTL_DDI/TC
 *     intel_hpd_poll_disable()           poll_enabled = false + poll_init_work
 *     skl_watermark_ipc_init()           HAS_IPC: DISP_ARB_CTL2.DISP_IPC_ENABLE
 *   i915_driver_register(i915)           P7-b:
 *     gem/pmu/vgpu/drm_dev/debugfs/sysfs/perf/gt sysfs/hwmon registrations
 *     intel_display_driver_register()    opregion register, acpi video, audio
 *                                        components, debugfs, fbdev config,
 *                                        drm_kms_helper_poll_init
 *     intel_power_domains_enable()       PUT the POWER_DOMAIN_INIT reference
 *                                        held since intel_power_domains_init_hw:
 *                                        every well with no other reference
 *                                        turns off, DC_off last (DC6 allowed)
 *     intel_runtime_pm_enable()          autosuspend 10 s, drop the probe ref
 *     dsm handler, vga switcheroo
 *   and the remove path mirrors it: intel_runtime_pm_disable (take the ref
 *   back), intel_power_domains_disable (get INIT again), display unregister.
 *
 * ADAPTATIONS (recorded):
 *   - No DRM object model here: intel_initial_commit is executed only in its
 *     no-active-crtc form (an active crtc is reported as unimplemented);
 *     connectors do not exist, so the poll_init work touches no connector
 *     (the DISPLAY_CORE power get/put around it is kept); the userspace-facing
 *     registrations (drm_dev, debugfs, sysfs, pmu, perf, hwmon, audio
 *     components, fbdev, acpi video, dsm, switcheroo) are recorded as N/A.
 *   - The poll_init work runs inline where the reference queues it.
 *   - intel_runtime_pm_enable drops the probe reference but does not arm
 *     autosuspend: intel_runtime_suspend() (what the PM core would run 10 s
 *     later) is not ported, so the device stays in D0.
 *   - intel_power_domains_verify_state() is compiled only under
 *     CONFIG_DRM_I915_DEBUG_RUNTIME_PM in the reference; here its per-well
 *     refcount/HW comparison is kept as a diagnostic.
 */
#ifndef PARITY_DRIVER_PROBE_H
#define PARITY_DRIVER_PROBE_H

#include <stdint.h>

struct osdep_mmio;
struct osdep_rpm;
struct parity_power_domains;
struct parity_pw_ctx;
struct parity_display_core;
struct parity_display_nogem;

/* enum hpd_pin (intel_display_limits.h) */
#define PARITY_HPD_NONE      0
#define PARITY_HPD_PORT_A    4
#define PARITY_HPD_PORT_B    5
#define PARITY_HPD_PORT_C    6
#define PARITY_HPD_PORT_D    7
#define PARITY_HPD_PORT_E    8
#define PARITY_HPD_PORT_TC1  9
#define PARITY_HPD_PORT_TC6  14
#define PARITY_HPD_NUM_PINS  15

/* hotplug.stats[].state */
#define PARITY_HPD_ENABLED        0
#define PARITY_HPD_DISABLED       1
#define PARITY_HPD_MARK_DISABLED  2

/* Registers (i915_reg.h). */
#define PARITY_GEN11_DE_HPD_IMR       0x44474u
#define PARITY_GEN11_TBT_HOTPLUG_CTL  0x44030u
#define PARITY_GEN11_TC_HOTPLUG_CTL   0x44038u
#define PARITY_SDEIMR                 0xc4004u
#define PARITY_SHOTPLUG_CTL_DDI       0xc4030u
#define PARITY_SHOTPLUG_CTL_TC        0xc4034u
#define PARITY_SHPD_FILTER_CNT        0xc4038u
#define PARITY_SHPD_FILTER_CNT_500_ADJ 0x001d9u
#define PARITY_SHPD_FILTER_CNT_250     0x000f8u
#define PARITY_DISP_ARB_CTL2          0x45004u
#define PARITY_DISP_IPC_ENABLE        (1u << 3)

struct parity_hotplug {
	/* intel_hpd_init_pins(): hpd_gen11 / hpd_icp */
	uint32_t hpd[PARITY_HPD_NUM_PINS];
	uint32_t pch_hpd[PARITY_HPD_NUM_PINS];
	int pins_inited;

	/* encoder->hpd_pin of every encoder intel_setup_outputs() created */
	int encoder_pin[8];
	unsigned n_encoders;

	int state[PARITY_HPD_NUM_PINS];
	unsigned count[PARITY_HPD_NUM_PINS];
	int poll_enabled;          /* display.hotplug.poll_enabled */
	int kms_poll_inited;       /* drm_kms_helper_poll_init(): mode_config.poll_enabled */

	/* what the last hpd_irq_setup computed and left in the registers */
	unsigned irq_setups;
	int irq_setup_skipped;     /* !display_irqs_enabled */
	uint32_t de_enabled_irqs, de_hotplug_irqs, pch_enabled_irqs, pch_hotplug_irqs;
	uint32_t de_hpd_imr, tc_ctl, tbt_ctl, shpd_filter, sdeimr, shotplug_ddi, shotplug_tc;
	int sdeimr_skipped;        /* ibx_display_interrupt_update: !intel_irqs_enabled */
	unsigned poll_init_works;
	unsigned poll_core_gets;
};

/* intel_hpd_pin_default / tgl_hpd_pin / xelpd_hpd_pin (intel_ddi_init). */
int parity_intel_ddi_hpd_pin(int display_ver, int port);
/* intel_hpd_irq_setup(): gen11 + icp programming from hp->state (the hotplug path's storm masking / re-enable) */
void parity_intel_hpd_irq_setup(struct parity_hotplug *hp, struct osdep_mmio *m, int pch_type, int intel_irqs_enabled);

/* intel_hotplug_irq_init()'s pin tables (DISPLAY_VER >= 11, PCH >= ICP). */
void parity_intel_hpd_init_pins(struct parity_hotplug *hp, int display_ver, int pch_type);

/* intel_hpd_init(): stats reset + hpd_irq_setup (gen11). */
void parity_intel_hpd_init(struct parity_hotplug *hp, struct osdep_mmio *m,
	int display_ver, int pch_type, int display_irqs_enabled, int intel_irqs_enabled);

/* intel_hpd_poll_disable() with its poll_init_work run inline. */
void parity_intel_hpd_poll_disable(struct parity_hotplug *hp,
	struct parity_power_domains *pd, struct parity_pw_ctx *c);

/* skl_watermark_ipc_init(): returns ipc_enabled. */
int parity_skl_watermark_ipc_init(struct osdep_mmio *m, int has_ipc, int platform_can);

struct parity_driver_probe {
	struct parity_hotplug hp;

	/* intel_display_driver_probe */
	unsigned active_crtcs;
	int initial_commit_rc;
	int initial_commit_unimplemented;   /* an active crtc needs the atomic commit */
	int overlay;                        /* HAS_OVERLAY */
	int fbdev;                          /* CONFIG_DRM_FBDEV_EMULATION */
	int ipc_enabled;

	/* i915_driver_register */
	int opregion_registered;
	unsigned na_registrations;          /* userspace-facing steps recorded N/A */
	unsigned wells_on_before, wells_on_after;
	int dc_state_after;                 /* DC_STATE_EN & mask after the INIT put */
	unsigned verify_mismatches;
	int rpm_usage_after;
	int registered;

	int err;
	const char *err_where;
};

/* intel_display_driver_probe(): P7-a. 0, or -EOPNOTSUPP for the DRM commit. */
int parity_intel_display_driver_probe(struct parity_driver_probe *p,
	struct osdep_mmio *m, const struct parity_display_nogem *nogem,
	struct parity_power_domains *pd, struct parity_pw_ctx *c,
	int display_ver, int pch_type, int display_irqs_enabled, int intel_irqs_enabled);

/* intel_power_domains_verify_state(): mismatches found (diagnostic). */
unsigned parity_intel_power_domains_verify_state(struct parity_power_domains *pd,
	struct parity_pw_ctx *c);

/* intel_power_domains_enable(): put the INIT reference, verify. */
void parity_intel_power_domains_enable(struct parity_driver_probe *p,
	struct parity_display_core *dc);

/* intel_power_domains_disable(): take the INIT reference again (remove path). */
void parity_intel_power_domains_disable(struct parity_display_core *dc);

/* i915_driver_register(): P7-b. */
void parity_i915_driver_register(struct parity_driver_probe *p,
	struct parity_display_core *dc, struct osdep_rpm *probe_pm, int opregion_present);

/* i915_driver_unregister()'s HW-facing half: rpm back, INIT again, poll fini. */
void parity_i915_driver_unregister(struct parity_driver_probe *p,
	struct parity_display_core *dc, struct osdep_rpm *probe_pm);

#endif /* PARITY_DRIVER_PROBE_H */
