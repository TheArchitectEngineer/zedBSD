/*
 * WS031 Linux-parity — P7: i915_driver_probe() after i915_gem_init().
 * See driver_probe.h for the reference sequence and the recorded adaptations.
 */
#include "../internal.h"
#include <kern/klog.h>
#include <errno.h>
#include <string.h>
#include "driver_probe.h"
#include "display_nogem.h"
#include "bios.h"    /* PARITY_N1_TEST */
#include "power_domains.h"
#include "display_core.h"
#include "pch.h"
#include "osdep/mmio.h"
#include "osdep/runtime_pm.h"

/* enum port (intel_display_limits.h) */
#define PORT_A        0
#define PORT_D        3
#define PORT_TC1      3
#define PORT_D_XELPD  7   /* PORT_TC5 */

/* intel_uncore_rmw(): write only when the value changes; returns the new value. */
static uint32_t
rmw(struct osdep_mmio *m, uint32_t reg, uint32_t clear, uint32_t set)
{
	uint32_t old = osdep_mmio_read32(m, reg);
	uint32_t val = (old & ~clear) | set;

	if (val != old)
		osdep_mmio_write32(m, reg, val);
	return val;
}

int
parity_intel_ddi_hpd_pin(int display_ver, int port)
{
	/* intel_ddi_init(): xelpd_hpd_pin / tgl_hpd_pin / intel_hpd_pin_default. */
	if (display_ver >= 13 && port >= PORT_D_XELPD)
		return PARITY_HPD_PORT_D + port - PORT_D_XELPD;
	if (display_ver >= 12 && port >= PORT_TC1)
		return PARITY_HPD_PORT_TC1 + port - PORT_TC1;
	return PARITY_HPD_PORT_A + port - PORT_A;
}

/* i915_reg.h bit helpers, on the pin enumeration. */
static uint32_t gen11_tc_hotplug(int pin)      { return 1u << (16 + (pin - PARITY_HPD_PORT_TC1)); }
static uint32_t gen11_tbt_hotplug(int pin)     { return 1u << (pin - PARITY_HPD_PORT_TC1); }
static uint32_t gen11_hotplug_ctl_enable(int pin) { return 8u << ((pin - PARITY_HPD_PORT_TC1) * 4); }
static uint32_t sde_ddi_hotplug_icp(int pin)   { return 1u << (16 + (pin - PARITY_HPD_PORT_A)); }
static uint32_t sde_tc_hotplug_icp(int pin)    { return 1u << (24 + (pin - PARITY_HPD_PORT_TC1)); }
static uint32_t shotplug_ctl_ddi_hpd_enable(int pin) { return 0x8u << ((pin - PARITY_HPD_PORT_A) * 4); }
static uint32_t icp_tc_hpd_enable(int pin)     { return 8u << ((pin - PARITY_HPD_PORT_TC1) * 4); }

static int is_tc_pin(int pin)  { return pin >= PARITY_HPD_PORT_TC1 && pin <= PARITY_HPD_PORT_TC6; }
static int is_ddi_pin(int pin) { return pin >= PARITY_HPD_PORT_A && pin <= PARITY_HPD_PORT_D; }

void
parity_intel_hpd_init_pins(struct parity_hotplug *hp, int display_ver, int pch_type)
{
	int pin;

	memset(hp->hpd, 0, sizeof(hp->hpd));
	memset(hp->pch_hpd, 0, sizeof(hp->pch_hpd));
	/* hpd->hpd = hpd_gen11 (DISPLAY_VER >= 11, < 14) */
	if (display_ver >= 11 && display_ver < 14)
		for (pin = PARITY_HPD_PORT_TC1; pin <= PARITY_HPD_PORT_TC6; pin++)
			hp->hpd[pin] = gen11_tc_hotplug(pin) | gen11_tbt_hotplug(pin);
	/* hpd->pch_hpd = hpd_icp (PCH_ICP <= type < PCH_DG1) */
	if (pch_type >= PARITY_PCH_ICP) {
		for (pin = PARITY_HPD_PORT_A; pin <= PARITY_HPD_PORT_C; pin++)
			hp->pch_hpd[pin] = sde_ddi_hotplug_icp(pin);
		for (pin = PARITY_HPD_PORT_TC1; pin <= PARITY_HPD_PORT_TC6; pin++)
			hp->pch_hpd[pin] = sde_tc_hotplug_icp(pin);
	}
	hp->pins_inited = 1;
}

/* intel_hpd_enabled_irqs / intel_hpd_hotplug_irqs over the encoders. */
static void
hpd_irqs(const struct parity_hotplug *hp, const uint32_t *table,
	uint32_t *enabled, uint32_t *hotplug)
{
	unsigned i;

	*enabled = 0u;
	*hotplug = 0u;
	for (i = 0u; i < hp->n_encoders; i++) {
		int pin = hp->encoder_pin[i];

		if (pin <= PARITY_HPD_NONE || pin >= PARITY_HPD_NUM_PINS)
			continue;
		if (hp->state[pin] == PARITY_HPD_ENABLED)
			*enabled |= table[pin];
		*hotplug |= table[pin];
	}
}

/* intel_hpd_hotplug_mask(): the mask over EVERY pin; _enables(): over the encoders. */
static uint32_t
hotplug_mask(uint32_t (*fn)(int), int (*applies)(int))
{
	uint32_t v = 0u;
	int pin;

	for (pin = PARITY_HPD_NONE; pin < PARITY_HPD_NUM_PINS; pin++)
		if (applies(pin))
			v |= fn(pin);
	return v;
}

static uint32_t
hotplug_enables(const struct parity_hotplug *hp, uint32_t (*fn)(int), int (*applies)(int))
{
	uint32_t v = 0u;
	unsigned i;

	for (i = 0u; i < hp->n_encoders; i++)
		if (applies(hp->encoder_pin[i]))
			v |= fn(hp->encoder_pin[i]);
	return v;
}

/* gen11_hpd_irq_setup() (+ icp_hpd_irq_setup on PCH >= ICP). */
static void
gen11_hpd_irq_setup(struct parity_hotplug *hp, struct osdep_mmio *m,
	int pch_type, int intel_irqs_enabled)
{
	uint32_t enabled_irqs, hotplug_irqs;

	hpd_irqs(hp, hp->hpd, &enabled_irqs, &hotplug_irqs);
	hp->de_enabled_irqs = enabled_irqs;
	hp->de_hotplug_irqs = hotplug_irqs;
	hp->de_hpd_imr = rmw(m, PARITY_GEN11_DE_HPD_IMR, hotplug_irqs,
		~enabled_irqs & hotplug_irqs);
	(void)osdep_mmio_read32(m, PARITY_GEN11_DE_HPD_IMR);   /* posting read */

	/* gen11_tc_hpd_detection_setup / gen11_tbt_hpd_detection_setup */
	hp->tc_ctl = rmw(m, PARITY_GEN11_TC_HOTPLUG_CTL,
		hotplug_mask(gen11_hotplug_ctl_enable, is_tc_pin),
		hotplug_enables(hp, gen11_hotplug_ctl_enable, is_tc_pin));
	hp->tbt_ctl = rmw(m, PARITY_GEN11_TBT_HOTPLUG_CTL,
		hotplug_mask(gen11_hotplug_ctl_enable, is_tc_pin),
		hotplug_enables(hp, gen11_hotplug_ctl_enable, is_tc_pin));

	if (pch_type < PARITY_PCH_ICP)
		return;

	/* icp_hpd_irq_setup() */
	hpd_irqs(hp, hp->pch_hpd, &enabled_irqs, &hotplug_irqs);
	hp->pch_enabled_irqs = enabled_irqs;
	hp->pch_hotplug_irqs = hotplug_irqs;
	hp->shpd_filter = pch_type <= PARITY_PCH_TGP ?
		PARITY_SHPD_FILTER_CNT_500_ADJ : PARITY_SHPD_FILTER_CNT_250;
	osdep_mmio_write32(m, PARITY_SHPD_FILTER_CNT, hp->shpd_filter);

	/* ibx_display_interrupt_update(): only while intel_irqs_enabled(). */
	{
		uint32_t sdeimr = osdep_mmio_read32(m, PARITY_SDEIMR);

		sdeimr &= ~hotplug_irqs;
		sdeimr |= (~enabled_irqs & hotplug_irqs);
		if (intel_irqs_enabled) {
			osdep_mmio_write32(m, PARITY_SDEIMR, sdeimr);
			(void)osdep_mmio_read32(m, PARITY_SDEIMR);
			hp->sdeimr = sdeimr;
		} else {
			hp->sdeimr_skipped = 1;
			hp->sdeimr = osdep_mmio_read32(m, PARITY_SDEIMR);
		}
	}

	/* icp_ddi_hpd_detection_setup / icp_tc_hpd_detection_setup */
	hp->shotplug_ddi = rmw(m, PARITY_SHOTPLUG_CTL_DDI,
		hotplug_mask(shotplug_ctl_ddi_hpd_enable, is_ddi_pin),
		hotplug_enables(hp, shotplug_ctl_ddi_hpd_enable, is_ddi_pin));
	hp->shotplug_tc = rmw(m, PARITY_SHOTPLUG_CTL_TC,
		hotplug_mask(icp_tc_hpd_enable, is_tc_pin),
		hotplug_enables(hp, icp_tc_hpd_enable, is_tc_pin));
}

/* intel_hpd_irq_setup() for the hotplug path (storm masking / re-enable): the pins' state is in hp->state */
void
parity_intel_hpd_irq_setup(struct parity_hotplug *hp, struct osdep_mmio *m, int pch_type,
	int intel_irqs_enabled)
{
	hp->irq_setups++;
	gen11_hpd_irq_setup(hp, m, pch_type, intel_irqs_enabled);
}

void
parity_intel_hpd_init(struct parity_hotplug *hp, struct osdep_mmio *m,
	int display_ver, int pch_type, int display_irqs_enabled, int intel_irqs_enabled)
{
	int pin;

	if (!hp->pins_inited)
		parity_intel_hpd_init_pins(hp, display_ver, pch_type);
	for (pin = PARITY_HPD_NONE; pin < PARITY_HPD_NUM_PINS; pin++) {
		hp->count[pin] = 0u;
		hp->state[pin] = PARITY_HPD_ENABLED;
	}
	/* intel_hpd_irq_setup(): only with display_irqs_enabled and a hotplug func. */
	if (!display_irqs_enabled || display_ver < 11) {
		hp->irq_setup_skipped = 1;
		return;
	}
	hp->irq_setups++;
	gen11_hpd_irq_setup(hp, m, pch_type, intel_irqs_enabled);
}

void
parity_intel_hpd_poll_disable(struct parity_hotplug *hp,
	struct parity_power_domains *pd, struct parity_pw_ctx *c)
{
	hp->poll_enabled = 0;
	/*
	 * i915_hpd_poll_init_work(), run inline (adaptation): !enabled takes a
	 * DISPLAY_CORE reference for the connector detection it would run.  No
	 * DRM connectors exist here, and mode_config.poll_enabled is still false
	 * (drm_kms_helper_poll_init comes with the registration), so
	 * i915_hpd_poll_detect_connectors() does nothing.
	 */
	hp->poll_init_works++;
	(void)parity_display_power_get(pd, PARITY_PW_DOMAIN_DISPLAY_CORE, c);
	hp->poll_core_gets++;
	parity_display_power_put(pd, PARITY_PW_DOMAIN_DISPLAY_CORE, c);
}

int
parity_skl_watermark_ipc_init(struct osdep_mmio *m, int has_ipc, int platform_can)
{
	int enabled;

	if (!has_ipc)
		return 0;
	/* skl_watermark_ipc_can_enable(): SKL never, KBL/CFL/CML by DRAM symmetry. */
	enabled = platform_can ? 1 : 0;
	/* skl_watermark_ipc_update() */
	(void)rmw(m, PARITY_DISP_ARB_CTL2, PARITY_DISP_IPC_ENABLE,
		enabled ? PARITY_DISP_IPC_ENABLE : 0u);
	return enabled;
}

int
parity_intel_display_driver_probe(struct parity_driver_probe *p,
	struct osdep_mmio *m, const struct parity_display_nogem *nogem,
	struct parity_power_domains *pd, struct parity_pw_ctx *c,
	int display_ver, int pch_type, int display_irqs_enabled, int intel_irqs_enabled)
{
	unsigned i, pipe;

	if (p == 0 || m == 0 || nogem == 0 || pd == 0 || c == 0)
		return -EINVAL;

	/*
	 * intel_initial_commit(): for_each_intel_crtc, an ACTIVE crtc gets its
	 * planes added and its state recomputed by a DRM atomic commit.  With no
	 * active crtc the commit carries nothing and returns 0.
	 */
	p->active_crtcs = 0u;
	for (pipe = 0u; pipe < 32u; pipe++)
		if (nogem->active_pipes & (1u << pipe))
			p->active_crtcs++;
	if (p->active_crtcs != 0u) {
		p->initial_commit_unimplemented = 1;
		p->initial_commit_rc = -EOPNOTSUPP;
		/* the reference only logs a failed initial modeset and continues */
	} else {
		p->initial_commit_rc = 0;
	}

	/* intel_overlay_setup(): HAS_OVERLAY is gen2-4. */
	p->overlay = 0;
	/* intel_fbdev_init(): CONFIG_DRM_FBDEV_EMULATION=n form (returns 0). */
	p->fbdev = 0;

	/* intel_hpd_init(): the encoders' pins are the ones intel_ddi_init() set. */
	p->hp.n_encoders = 0u;
	for (i = 0u; i < nogem->num_encoders && i < 8u; i++)
		p->hp.encoder_pin[p->hp.n_encoders++] =
			parity_intel_ddi_hpd_pin(display_ver, nogem->encoders[i].port);
	parity_intel_hpd_init_pins(&p->hp, display_ver, pch_type);
	parity_intel_hpd_init(&p->hp, m, display_ver, pch_type,
		display_irqs_enabled, intel_irqs_enabled);

	/* intel_hpd_poll_disable() */
	parity_intel_hpd_poll_disable(&p->hp, pd, c);

	/* skl_watermark_ipc_init(): xe_lpd has_ipc; ADL-P is not SKL/KBL/CFL/CML. */
	p->ipc_enabled = parity_skl_watermark_ipc_init(m, 1, 1);
	return 0;
}

unsigned
parity_intel_power_domains_verify_state(struct parity_power_domains *pd,
	struct parity_pw_ctx *c)
{
	unsigned i, mismatches = 0u;

	for (i = 0u; i < pd->num_power_wells; i++) {
		struct parity_power_well *w = &pd->power_wells[i];
		int enabled = parity_power_well_is_enabled(w, c);
		int expect = (w->refcount != 0u || w->always_on) ? 1 : 0;

		if (expect != enabled) {
			kern_logf("i915: parity power well %s state mismatch (refcount %u/enabled %d)\n",
				w->name, w->refcount, enabled);
			mismatches++;
		}
	}
	/* The domain use-count half needs per-domain counts this port does not keep. */
	return mismatches;
}

static unsigned
wells_on(struct parity_power_domains *pd, struct parity_pw_ctx *c)
{
	unsigned i, n = 0u;

	for (i = 0u; i < pd->num_power_wells; i++)
		if (parity_power_well_is_enabled(&pd->power_wells[i], c))
			n++;
	return n;
}

void
parity_intel_power_domains_enable(struct parity_driver_probe *p,
	struct parity_display_core *dc)
{
	p->wells_on_before = wells_on(dc->pd, dc->pwc);
	/* wakeref = fetch_and_zero(&init_wakeref); intel_display_power_put(INIT, wakeref) */
	if (dc->init_wakeref_held) {
		dc->init_wakeref_held = 0;
		dc->pm_wakeref = 0;
		parity_display_power_put(dc->pd, PARITY_PW_DOMAIN_INIT, dc->pwc);
	}
	p->wells_on_after = wells_on(dc->pd, dc->pwc);
	p->dc_state_after = (int)dc->pwc->dc_state;
	p->verify_mismatches = parity_intel_power_domains_verify_state(dc->pd, dc->pwc);
}

void
parity_intel_power_domains_disable(struct parity_display_core *dc)
{
	/* init_wakeref = intel_display_power_get(INIT); verify_state */
	if (!dc->init_wakeref_held) {
		(void)parity_display_power_get(dc->pd, PARITY_PW_DOMAIN_INIT, dc->pwc);
		dc->init_wakeref_held = 1;
		dc->pm_wakeref = 1;
	}
	(void)parity_intel_power_domains_verify_state(dc->pd, dc->pwc);
}

void
parity_i915_driver_register(struct parity_driver_probe *p,
	struct parity_display_core *dc, struct osdep_rpm *probe_pm, int opregion_present)
{
	/* i915_gem_driver_register, i915_pmu_register, intel_vgpu_register (not vGPU),
	 * drm_dev_register, i915_debugfs_register, i915_setup_sysfs,
	 * i915_perf_register, intel_gt_driver_register (sysfs/debugfs),
	 * intel_pxp_debugfs_register, i915_hwmon_register (DGFX): userspace. */
	p->na_registrations = 9u;

	/*
	 * intel_display_driver_register(): intel_opregion_register() acts only
	 * with an OpRegion header; then acpi video, audio components, debugfs
	 * and the fbdev config (all N/A), and drm_kms_helper_poll_init().
	 */
	p->opregion_registered = opregion_present ? 1 : 0;
	p->na_registrations += 4u;
	p->hp.kms_poll_inited = 1;

	/*
	 * intel_power_domains_enable(): the INIT reference taken during init_hw is released here, and every
	 * well nothing references powers down.  The reference implementation can do that because
	 * intel_initial_commit() has by then taken the firmware display over, which references the wells that
	 * feed it.  E-124 (N1): the takeover happens later in this run, so the reference is KEPT until then --
	 * otherwise the wells of the live display drop and the picture dies here (observed on bare metal).
	 */
	if (PARITY_N1_TEST && p->active_crtcs != 0u) {
		p->power_domains_enable_deferred = 1;
		p->wells_on_before = p->wells_on_after = 0u;
	} else {
		parity_intel_power_domains_enable(p, dc);
	}

	/*
	 * intel_runtime_pm_enable(): autosuspend 10 s, allow, and put the probe
	 * reference the PCI core took.  Adaptation: the reference is dropped but
	 * autosuspend is not armed (intel_runtime_suspend is not ported).
	 */
	osdep_rpm_put(probe_pm);
	p->rpm_usage_after = osdep_rpm_usage(probe_pm);

	/* intel_register_dsm_handler (ACPI _DSM), i915_switcheroo_register: N/A. */
	p->na_registrations += 2u;
	p->registered = 1;
}

void
parity_i915_driver_unregister(struct parity_driver_probe *p,
	struct parity_display_core *dc, struct osdep_rpm *probe_pm)
{
	if (p == 0 || !p->registered)
		return;
	/* intel_runtime_pm_disable(): pm_runtime_get_sync (ownership back to core) */
	(void)osdep_rpm_get_sync(probe_pm);
	/* intel_power_domains_disable() */
	parity_intel_power_domains_disable(dc);
	/* intel_display_driver_unregister(): fbdev/audio N/A, drm_kms_helper_poll_fini,
	 * drm_atomic_helper_shutdown (nothing active), opregion unregister. */
	p->hp.kms_poll_inited = 0;
	p->opregion_registered = 0;
	p->registered = 0;
}
