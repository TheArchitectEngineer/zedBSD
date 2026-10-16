/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Derived from the Linux kernel v6.8.12 (drivers/gpu/drm/i915/display/intel_display_power.c),
 * which carries the following notice.
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

/*
 * The display power wells, domains and core (see power.h).
 *
 * Each power domain stands for a set of functions of the xd power wells
 * (xd-power.c), whose counts decide which wells come up and go down, in
 * the platform table's order; the wells the older code reads are copies
 * of that table with the users and the last known state.  A last
 * reference may be put asynchronously: its wells then stay up, owed to
 * the delayed work, until the work gives them back.
 *
 * The well operations switch a well by its request bit through the xd
 * power wells, with the VGA, interrupt and Type-C PHY steps of this
 * context; DC_off leaves the DC states, and its release enters the one
 * the DMC is allowed.
 *
 * The display-core bring-up (icl_display_core_init) and its caller
 * intel_power_domains_init_hw() run the DC-state disable, the PCH reset
 * handshake, the combo PHYs, power well 1, the CDCLK, the DBUF slices, the
 * MBUS credits, BW_BUDDY and the workarounds, then keep POWER_DOMAIN_INIT
 * until the driver probe gives it back.
 */

#include "modeset-internal.h"
#include "power.h"
#include "clock.h"
#include "takeover.h"
#include "modeset.h"
#include "dkl-phy.h"
#include "xd-ddi.h"
#include "xd-power.h"
#include "xd-dbuf.h"
#include "xd-core.h"
#include "xd.h"
#include <kern/kcrt.h>

#include "../intel/display-regs-xd.h"
#include "../mmio.h"
#include "../sync.h"
#include "../trace.h"

#include <kern/klog.h>
#include <kern/lock.h>
#include <kern/waitq.h>

#include <uapi/errno.h>

/* The trace stage of the display noirq bring-up. */
#define I915_POWER_TRACE_STAGE_NOIRQ	4U

/* How long an AUX_USBC well's enable waits for the PHY microcontroller's health (1 ms, as Linux). */
#define I915_TC_UC_HEALTH_TIMEOUT_US	1000u

/* The driver request registers of the well families (i915_reg.h). */
#define ICL_PWR_WELL_CTL_AUX2	0x45444u
#define ICL_PWR_WELL_CTL_DDI2	0x45454u
#define HSW_PWR_WELL_CTL2	0x45404u

/* The DBUF slice power state bit (skl_watermark_regs.h). */
#define DBUF_POWER_STATE		(1u << 30)

/* The reference's delay of an asynchronous put when the caller names none. */
#define I915_POWER_ASYNC_PUT_DELAY_MS	100

/*
 * What the xd power well counts switch wells with: the domains state
 * whose copies they turn on and off, and the well context of the
 * operations.  One lives on the stack of each get, put and work run.
 */
struct i915_pw_switch_ctx {
	struct i915_power_domains *pd;
	struct i915_pw_ctx *pwc;
};

/*
 * What the xd power wells' hooks act for: the power-well context and the
 * well being switched (for the messages).  One lives on the stack of each
 * enable or disable.
 */
struct i915_pw_hook_ctx {
	struct i915_pw_ctx *pwc;
	struct i915_power_well *well;
};

/*
 * The DBUF slice control registers, slice S1 first (skl_watermark_regs.h:
 * _DBUF_CTL_S0 0x45008, _DBUF_CTL_S1 0x44FE8, _DBUF_CTL_S2 0x44300,
 * _DBUF_CTL_S3 0x44304).
 *
 * The table once began at 0x44FE8 and ended at 0x44308: slice 1 then
 * powered the second slice and the fourth request went to a register that
 * is not a DBUF control.  It never changes.
 */
static const uint32_t i915_dbuf_ctl_s[4] = { 0x45008u, 0x44FE8u, 0x44300u, 0x44304u };

/* The trace stage of the combo PHY bring-up (the display's noirq stage). */
#define I915_COMBO_TRACE_STAGE		4U

static uint32_t i915_get_allowed_dc_mask(unsigned display_ver, int enable_dc);
static uint32_t i915_sanitize_target_dc_state(const struct i915_power_domains *pd, uint32_t target);
static int i915_sanitize_disable_power_well(int param);
static unsigned i915_pw_driver_reg(enum i915_pw_ops_kind ops);
static uint32_t i915_pw_state(unsigned idx);
static int i915_pw_xd_well(const struct i915_power_well *well, struct i915_pw_ctx *pwc);
static void i915_pw_xd_hooks(struct i915_pw_ctx *pwc, struct i915_power_well *well, struct i915_pw_hook_ctx *hook, struct i915_xd_power_hooks *hooks);
static void i915_pw_hook_pipes_up(void *context, unsigned pipe_mask);
static int i915_pw_hook_pipes_down(void *context, unsigned pipe_mask);
static void i915_pw_hook_vga_reset(void *context);
static int i915_pw_hook_tc_health(void *context, unsigned tc_port);
static uint64_t i915_pw_functions(unsigned display_ver, enum i915_power_domain domain);
static uint64_t i915_pw_port_function(unsigned display_ver, unsigned offset, uint64_t ddi0);
static void i915_pw_copy_well(struct i915_power_well *well, const struct i915_xd_well_desc *desc, unsigned xd_well, unsigned display_ver);
static struct i915_power_well *i915_pw_copy_of(struct i915_power_domains *pd, unsigned xd_well);
static void i915_pw_switch(struct i915_power_domains *pd, struct i915_pw_ctx *pwc, struct i915_pw_switch_ctx *ctx, struct i915_xd_power_switch *sw);
static int i915_pw_switch_on(void *context, unsigned xd_well);
static int i915_pw_switch_off(void *context, unsigned xd_well);
static void i915_pw_count_put(struct i915_power_domains *pd, enum i915_power_domain domain);
static void i915_pw_copy_counts(struct i915_power_domains *pd);
static int i915_pw_work_queue(void *context, int delay_ms);
static int i915_pw_work_cancel(void *context, int sync);
static void i915_pw_set_state(struct i915_pw_ctx *pwc, struct i915_power_well *well, int state);
static void i915_pw_dc_off_on(struct i915_pw_ctx *pwc);
static void i915_pw_dc_off_off(struct i915_pw_ctx *pwc);
static struct i915_xd_power *i915_pw_dc_policy(struct i915_pw_ctx *pwc);
static void i915_pw_dc_log(struct i915_pw_ctx *pwc, const struct i915_xd_dc_report *report);
static struct i915_trace *i915_core_trace(struct i915_display_core *dc);
static struct i915_trace *i915_dc_off_trace(struct i915_pw_ctx *pwc);
static uint8_t i915_dbuf_slice_mask(const struct i915_display_core *dc);
static void i915_gen9_dbuf_slices_update(struct i915_display_core *dc, uint8_t req_slices);
static void i915_core_fault(struct i915_display_core *dc, const char *where);
static int i915_icl_display_core_init(struct i915_display_core *dc, int resume);
static void i915_icl_display_core_uninit(struct i915_display_core *dc);
static unsigned i915_core_dram_type(int dram_type);
static void i915_core_hooks(struct i915_display_core *dc, struct i915_xd_core_hooks *hooks);
static int i915_core_well1_on(void *context);
static void i915_core_well1_off(void *context);
static void i915_core_cdclk_on(void *context);
static void i915_core_cdclk_off(void *context);
static void i915_core_dbuf(void *context, uint8_t slices);
static int i915_core_faulted(void *context);
static void i915_combo_phys_up(struct i915_pw_ctx *pwc, struct i915_trace *trace);
static unsigned i915_wells_on(struct i915_power_domains *pd, struct i915_pw_ctx *pwc);

/*
 * Builds the power-domain state of a display (intel_power_domains_init()).
 *
 * Sanitizes the power-well and DC options, computes the allowed DC states
 * and the target DC state, prepares the lock, the xd power well counts of
 * the platform's description, the copy of its wells the older code reads,
 * and each domain's wells.  Returns 0, or EINVAL when there is no state,
 * no description of the device, or more wells than the copy holds.
 */
int
drv_i915_power_domains_init(
	struct i915_power_domains *pd,
	unsigned display_ver,
	struct i915_xd *xd,
	int enable_dc_param,
	int disable_pw_param,
	struct i915_trace *trace)
{
	const struct i915_xd_platform *platform;
	unsigned domain;
	unsigned well;
	unsigned index;
	uint64_t functions;
	uint64_t xd_wells;

	/* Refuses a missing state. */
	if (pd == NULL)
		return EINVAL;

	/* A display without a description has no wells to count. */
	if (xd == NULL || !xd->chosen) {
		kern_logf("i915: intel_power_domains_init: no platform description for display version %u\n",
			display_ver);
		return EINVAL;
	}

	/* Refuses a table larger than the copy and the counts hold. */
	platform = xd->choice.platform;
	if (platform->nwells > I915_PW_MAX || platform->nwells > I915_XD_POWER_MAX_WELLS) {
		kern_logf("i915: intel_power_domains_init: %u wells in the %s description\n",
			platform->nwells,
			platform->name);
		return EINVAL;
	}

	/* Sanitizes the options and derives the DC states they allow. */
	pd->disable_power_well = i915_sanitize_disable_power_well(disable_pw_param);
	pd->allowed_dc_mask = i915_get_allowed_dc_mask(display_ver, enable_dc_param);
	pd->target_dc_state = i915_sanitize_target_dc_state(pd, I915_DC_STATE_EN_UPTO_DC6);

	/* Prepares the lock and marks the asynchronous put work as initialized. */
	(void)mutex_init(&pd->lock, LOCK_RANK_DEVICE, "i915-power-domains");
	pd->async_put_work_inited = 1;

	/* Starts the use counts empty, whatever the caller's memory held. */
	for (domain = 0u; domain < I915_PW_DOMAIN_NUM; domain++)
		pd->domain_use_count[domain] = 0u;

	/* Starts with no delayed work bound. */
	pd->async_ops = NULL;
	pd->async_ctx = NULL;
	pd->async_pwc = NULL;

	/* Starts every diagnostic counter at zero. */
	pd->async_puts = 0u;
	pd->async_parked = 0u;
	pd->async_grabs = 0u;
	pd->async_work_runs = 0u;
	pd->async_work_empty = 0u;
	pd->async_released = 0u;
	pd->async_requeues = 0u;
	pd->async_flushes = 0u;
	pd->async_state_errors = 0u;
	pd->use_count_errors = 0u;

	/* Prepares the xd counts of the platform's wells: no user, none known to be on. */
	pd->xd = xd;
	pd->display_ver = display_ver;
	drv_i915_xd_power_init(&xd->power, platform);

	/*
	 * Copies the wells the older code reads, in the table's order.  The
	 * Type-C cold well is a GT mailbox request, which no domain takes
	 * here, so it has no copy.
	 */
	pd->num_power_wells = 0u;
	for (well = 0u; well < platform->nwells; well++) {
		/* Passes the mailbox well over. */
		if (platform->wells[well].control == I915_XD_WELL_CONTROL_MAILBOX)
			continue;

		/* Fills the next copy. */
		i915_pw_copy_well(&pd->power_wells[pd->num_power_wells], &platform->wells[well], well, display_ver);
		pd->num_power_wells++;
	}

	/* Gives each domain the copies of the wells that power its functions. */
	for (domain = 0u; domain < I915_PW_DOMAIN_NUM; domain++) {
		functions = i915_pw_functions(display_ver, (enum i915_power_domain)domain);
		xd_wells = drv_i915_xd_power_wells_of(platform, functions);

		/* Marks each copy whose well is among them. */
		pd->domain_wells[domain] = 0u;
		for (index = 0u; index < pd->num_power_wells; index++) {
			if ((xd_wells & (1ULL << pd->power_wells[index].xd_well)) != 0u)
				pd->domain_wells[domain] |= (uint64_t)1u << index;
		}
	}

	/* Publishes the map as built and the state as initialized, and records it. */
	pd->map_initialized = 1;
	pd->initialized = 1;
	drv_i915_trace_record(trace, I915_POWER_TRACE_STAGE_NOIRQ, I915_TRACE_ACQUIRE,
		"intel_power_domains_init", (uint64_t)pd->num_power_wells,
		(uint64_t)pd->allowed_dc_mask);

	/* Logs what was built. */
	kern_logf("i915: intel_power_domains_init: wells=%u allowed_dc=0x%x "
		"target_dc=0x%x disable_pw=%d (%s wells)\n",
		pd->num_power_wells, pd->allowed_dc_mask, pd->target_dc_state,
		pd->disable_power_well, platform->name);

	/* Succeeded: the wells and the options are ready for the hardware init. */
	return 0;
}

/*
 * Drops the power-well map (intel_power_domains_cleanup()).
 *
 * No register is touched: this path is only valid before any power get.
 */
void
drv_i915_power_domains_cleanup(
	struct i915_power_domains *pd)
{
	/* Nothing to drop without an initialized state. */
	if (pd == NULL)
		return;
	if (!pd->initialized)
		return;

	/* Forgets the wells and the map (intel_display_power_map_cleanup()). */
	pd->num_power_wells = 0u;
	pd->map_initialized = 0;
	pd->initialized = 0;
}

/*
 * Reports the bitmask of the wells that provide a domain.
 */
uint64_t
drv_i915_power_domain_wells(
	const struct i915_power_domains *pd,
	enum i915_power_domain domain)
{
	/* A missing state or an unknown domain has no wells. */
	if (pd == NULL)
		return 0u;
	if ((unsigned)domain >= I915_PW_DOMAIN_NUM)
		return 0u;

	/* Succeeded: reports the domain's wells. */
	return pd->domain_wells[domain];
}

/*
 * Finds the index of the well with a power-well id, or -1.
 */
int
drv_i915_power_well_by_id(
	const struct i915_power_domains *pd,
	int id)
{
	unsigned index;

	/* A missing state and DISP_PW_ID_NONE name no well. */
	if (pd == NULL)
		return -1;
	if (id == I915_DISP_PW_ID_NONE)
		return -1;

	/* Looks the id up among the wells. */
	for (index = 0u; index < pd->num_power_wells; index++) {
		if (pd->power_wells[index].id == id)
			return (int)index;
	}

	/* No well has the id. */
	return -1;
}

/*
 * Turns a power well on (the well's enable operation).
 *
 * Always-on and DC_off wells are the domain layer's; a well switched by a
 * request is brought up by the xd power wells (xd-power.c) from the
 * platform's description, with the VGA, interrupt and Type-C PHY hooks
 * of this context.  As in the reference, an acknowledge or fuse that
 * comes late is warned about and the enable continues.  Returns 0, or
 * EIO when the time base failed (or the well has no description) and the
 * caller must stop.
 */
int
drv_i915_power_well_enable(
	struct i915_power_well *well,
	struct i915_pw_ctx *pwc)
{
	struct i915_pw_hook_ctx hook;
	struct i915_xd_power_hooks hooks;
	struct i915_xd_well_report report;
	unsigned gate;
	int xd_well;
	int error;

	/* An always-on well is simply on. */
	if (well->ops == I915_PW_OPS_ALWAYS_ON) {
		i915_pw_set_state(pwc, well, 1);
		return 0;
	}

	/* DC_off is enabled by leaving the DC states (gen9_dc_off_power_well_enable()). */
	if (well->ops == I915_PW_OPS_DC_OFF) {
		i915_pw_dc_off_on(pwc);
		i915_pw_set_state(pwc, well, 1);
		return 0;
	}

	/* Finds the well in the platform's description. */
	xd_well = i915_pw_xd_well(well, pwc);
	if (xd_well < 0)
		return EIO;

	/* Brings the well up. */
	i915_pw_xd_hooks(pwc, well, &hook, &hooks);
	error = drv_i915_xd_power_well_enable(&pwc->xd->io, pwc->xd->choice.platform, (unsigned)xd_well, &hooks, &report);
	if (error != 0)
		return EIO;

	/* Warns about the fuses that came late; the enable goes on. */
	for (gate = 0u; gate < 32u; gate++) {
		if ((report.fuses_late & (1u << gate)) != 0u)
			kern_logf("i915: power well fuse PG%u wait timeout (continuing)\n", gate);
	}

	/*
	 * A real acknowledge timeout is warned and the enable continues (AUX in
	 * particular expects one); it is recorded, never unwound.  A Type-C AUX
	 * well may time out by design: a Thunderbolt tunnel that is down, or the
	 * TC cold exit the AUX_USBC well itself starts.
	 */
	if (report.state_late) {
		pwc->ack_timeouts++;
		kern_logf("i915: power well %s enable ACK timeout (continuing)\n",
			well->name);
	}

	/* Succeeded: the well is on. */
	i915_pw_set_state(pwc, well, 1);
	return 0;
}

/*
 * Turns a power well off (the well's disable operation).
 *
 * Returns 0, or EBUSY when a pipe's interrupt drain failed: the well is
 * then left on, and so is every well after it.
 */
int
drv_i915_power_well_disable(
	struct i915_power_well *well,
	struct i915_pw_ctx *pwc)
{
	struct i915_pw_hook_ctx hook;
	struct i915_xd_power_hooks hooks;
	struct i915_xd_well_report report;
	int enabled;
	int xd_well;
	int error;

	/* A failed interrupt drain refuses every later disable of a real well. */
	if (pwc->irq_sync_failed && well->ops != I915_PW_OPS_ALWAYS_ON) {
		pwc->disable_refusals++;
		return EBUSY;
	}

	/* An always-on well is never actually disabled. */
	if (well->ops == I915_PW_OPS_ALWAYS_ON) {
		i915_pw_set_state(pwc, well, 1);
		return 0;
	}

	/* DC_off goes off by letting the DMC take the target DC state once its firmware is loaded. */
	if (well->ops == I915_PW_OPS_DC_OFF) {
		i915_pw_dc_off_off(pwc);

		/* Records whether DC_off still reads as enabled. */
		enabled = drv_i915_power_well_is_enabled(well, pwc);
		i915_pw_set_state(pwc, well, enabled);
		return 0;
	}

	/* Finds the well in the platform's description. */
	xd_well = i915_pw_xd_well(well, pwc);
	if (xd_well < 0)
		return EIO;

	/*
	 * Takes the well down: the interrupt layer drains the pipe's
	 * interrupts first (hsw_power_well_pre_disable() ->
	 * gen8_irq_power_well_pre_disable()); a failed drain leaves the request
	 * bit untouched.
	 */
	i915_pw_xd_hooks(pwc, well, &hook, &hooks);
	error = drv_i915_xd_power_well_disable(&pwc->xd->io, pwc->xd->choice.platform, (unsigned)xd_well, &hooks, &report);
	if (error == EBUSY) {
		pwc->disable_refusals++;
		return EBUSY;
	}
	if (error != 0)
		return EIO;

	/* Records the driver's ownership (request and state both set). */
	enabled = drv_i915_power_well_is_enabled(well, pwc);
	i915_pw_set_state(pwc, well, enabled);

	/* Succeeded: the driver's request is gone. */
	return 0;
}

/*
 * Tells whether a power well is on as the driver owns it (1 or 0).
 */
int
drv_i915_power_well_is_enabled(
	struct i915_power_well *well,
	struct i915_pw_ctx *pwc)
{
	struct i915_xd_power *power;
	int held;
	int xd_well;
	int owned;

	/* An always-on well is always on. */
	if (well->ops == I915_PW_OPS_ALWAYS_ON)
		return 1;

	/* DC_off is on while the display is held out of the DC states; without the xd state it reads as off. */
	if (well->ops == I915_PW_OPS_DC_OFF) {
		power = i915_pw_dc_policy(pwc);
		if (power == NULL)
			return 0;

		/* Asks whether a DC state is set. */
		held = drv_i915_xd_power_dc_off_held(power, &pwc->xd->io);
		if (!held)
			return 0;

		/* Succeeded: no DC state is set. */
		return 1;
	}

	/* A well without a description reads as off. */
	xd_well = i915_pw_xd_well(well, pwc);
	if (xd_well < 0)
		return 0;

	/* hsw_power_well_enabled(): enabled only when both the driver request and the state are set. */
	owned = drv_i915_xd_power_well_owned(&pwc->xd->io, pwc->xd->choice.platform, (unsigned)xd_well);
	if (owned == 0)
		return 0;

	/* Succeeded: the driver owns an enabled well. */
	return 1;
}

/*
 * Takes over a well's request from the firmware and records its state
 * (intel_power_well_sync_hw()).
 *
 * The software reference count is not touched.
 */
void
drv_i915_power_well_sync_hw(
	struct i915_power_well *well,
	struct i915_pw_ctx *pwc)
{
	int enabled;
	int xd_well;

	/* An always-on well is simply on. */
	if (well->ops == I915_PW_OPS_ALWAYS_ON) {
		i915_pw_set_state(pwc, well, 1);
		return;
	}

	/*
	 * hsw_power_well_sync_hw(): when the firmware holds the request, the
	 * driver request is set first and the firmware request cleared after
	 * it -- never the reverse, and never skipped because the software count
	 * is zero.
	 */
	if (well->ops != I915_PW_OPS_DC_OFF) {
		xd_well = i915_pw_xd_well(well, pwc);
		if (xd_well >= 0)
			drv_i915_xd_power_well_adopt(&pwc->xd->io, pwc->xd->choice.platform, (unsigned)xd_well);
	}

	/* Records what the hardware now says. */
	enabled = drv_i915_power_well_is_enabled(well, pwc);
	i915_pw_set_state(pwc, well, enabled);
}

/*
 * Tells whether a domain counts as enabled (__intel_display_power_is_enabled()).
 *
 * Answers from the recorded state of the wells of the domain's functions,
 * not a fresh hardware read, and without the domains lock, so an
 * interrupt-reset path never touches a register or waits to answer.
 * Returns 1 or 0.
 */
int
drv_i915_display_power_is_enabled(
	struct i915_power_domains *pd,
	enum i915_power_domain domain,
	struct i915_pw_ctx *pwc)
{
	uint64_t functions;
	int powered;

	UNUSED_PARAMETER(pwc);

	/* A state without the xd counts has no well on. */
	if (pd->xd == NULL)
		return 0;

	/* Asks whether every well of the domain's functions is recorded as on. */
	functions = i915_pw_functions(pd->display_ver, domain);
	powered = drv_i915_xd_power_has(&pd->xd->power, functions);
	if (!powered)
		return 0;

	/* Succeeded: every well of the domain is on. */
	return 1;
}

/*
 * Tells whether every well of a domain is powered now, whoever requested
 * it (1 or 0).
 *
 * Read only: the state bit of the driver request register mirrors the
 * well's real state, and no request bit is read or written.  Always-on
 * wells count as on; DC_off gates no register access and is skipped.
 * Usable before intel_power_domains_init_hw().
 */
int
drv_i915_power_domain_hw_state_on(
	struct i915_power_domains *pd,
	enum i915_power_domain domain,
	struct i915_mmio *mmio)
{
	uint64_t wells;
	unsigned index;
	struct i915_power_well *well;
	uint32_t value;
	uint32_t state_bit;

	/* Walks the domain's wells, last one first. */
	wells = drv_i915_power_domain_wells(pd, domain);
	index = pd->num_power_wells;
	while (index-- > 0u) {
		if ((wells & ((uint64_t)1u << index)) == 0u)
			continue;

		/* Skips the wells that gate no register. */
		well = &pd->power_wells[index];
		if (well->always_on || well->ops == I915_PW_OPS_ALWAYS_ON || well->ops == I915_PW_OPS_DC_OFF)
			continue;

		/* Reads the well's state bit. */
		value = drv_i915_raw_read32(mmio, i915_pw_driver_reg(well->ops));
		state_bit = i915_pw_state(well->hsw_idx);
		if ((value & state_bit) == 0u)
			return 0;
	}

	/* Succeeded: every gating well of the domain is powered. */
	return 1;
}

/*
 * Turns off every power well the firmware left on that nothing references
 * (intel_power_domains_sanitize_state()), the last well first; each well
 * is read afresh, not from the recorded state.  Returns how many were
 * turned off.
 */
unsigned
drv_i915_power_wells_sanitize(
	struct i915_power_domains *pd,
	struct i915_pw_ctx *pwc)
{
	struct i915_power_well *well;
	unsigned index;
	unsigned disabled;
	int enabled;

	/* Walks the wells from the last. */
	disabled = 0U;
	index = pd->num_power_wells;
	while (index > 0U) {
		/* An always-on well and a referenced well stay on. */
		index--;
		well = &pd->power_wells[index];
		if (well->always_on != 0)
			continue;
		if (well->refcount != 0U)
			continue;

		/* A well that is off needs nothing. */
		enabled = drv_i915_power_well_is_enabled(well, pwc);
		if (enabled == 0)
			continue;

		/* Disables the unused well. */
		kern_logf("i915: BIOS left unused %s power well enabled, disabling it\n", well->name);
		(void)drv_i915_power_well_disable(well, pwc);
		disabled++;
	}

	/* Succeeded: the wells turned off. */
	return disabled;
}

/*
 * Takes one reference on a display power domain (intel_display_power_get()).
 *
 * The wells of the domain's functions count the reference (a reference
 * owed to the delayed work is taken over first).  Returns 0, or the error
 * of the well that could not be turned on (the wells already taken are
 * given back).
 */
int
drv_i915_display_power_get(
	struct i915_power_domains *pd,
	enum i915_power_domain domain,
	struct i915_pw_ctx *pwc)
{
	struct i915_pw_switch_ctx ctx;
	struct i915_xd_power_switch sw;
	uint64_t functions;
	int get_result;

	/* Takes the domain's wells under the domains lock. */
	i915_pw_switch(pd, pwc, &ctx, &sw);
	functions = i915_pw_functions(pd->display_ver, domain);
	mutex_lock(&pd->lock);

	get_result = drv_i915_xd_power_get(&pd->xd->power, functions, &sw);

	/* The domain's count is the number of references its users hold; a failed get holds none. */
	if (get_result == 0)
		pd->domain_use_count[domain]++;
	i915_pw_copy_counts(pd);

	mutex_unlock(&pd->lock);

	/* Reports a well that did not turn on. */
	if (get_result != 0)
		return get_result;

	/* Succeeded: the caller holds the domain. */
	return 0;
}

/*
 * Gives one reference on a display power domain back (intel_display_power_put()).
 */
void
drv_i915_display_power_put(
	struct i915_power_domains *pd,
	enum i915_power_domain domain,
	struct i915_pw_ctx *pwc)
{
	struct i915_pw_switch_ctx ctx;
	struct i915_xd_power_switch sw;
	uint64_t functions;

	/* Gives the domain's wells back under the domains lock. */
	i915_pw_switch(pd, pwc, &ctx, &sw);
	functions = i915_pw_functions(pd->display_ver, domain);
	mutex_lock(&pd->lock);

	i915_pw_count_put(pd, domain);
	drv_i915_xd_power_put(&pd->xd->power, functions, &sw);
	i915_pw_copy_counts(pd);

	mutex_unlock(&pd->lock);
}

/*
 * Binds the delayed work behind the asynchronous put and the well context
 * it releases with; a NULL ops unbinds it.
 */
void
drv_i915_display_power_async_bind(
	struct i915_power_domains *pd,
	const struct i915_pw_async_ops *ops,
	void *ctx,
	struct i915_pw_ctx *pwc)
{
	struct i915_xd_power_work work;

	/* The xd counts arm and disarm the work through the bound operations. */
	work.queue = NULL;
	work.cancel = NULL;
	work.context = pd;
	if (ops != NULL) {
		work.queue = i915_pw_work_queue;
		work.cancel = i915_pw_work_cancel;
	}

	/* Publishes the work under the domains lock. */
	mutex_lock(&pd->lock);

	pd->async_ops = ops;
	pd->async_ctx = ctx;
	pd->async_pwc = pwc;
	drv_i915_xd_power_bind_work(&pd->xd->power, &work);

	mutex_unlock(&pd->lock);
}

/*
 * Gives a reference back asynchronously (__intel_display_power_put_async()).
 *
 * The last reference of a domain leaves its wells up, owed to the delayed
 * work; any other reference, or any reference with no work bound, is put
 * at once.  A negative delay is the default of 100 ms.
 */
void
drv_i915_display_power_put_async(
	struct i915_power_domains *pd,
	enum i915_power_domain domain,
	struct i915_pw_ctx *pwc,
	int delay_ms)
{
	struct i915_pw_switch_ctx ctx;
	struct i915_xd_power_switch sw;
	uint64_t functions;
	unsigned use_count;

	/* Takes the default delay when none is named. */
	if (delay_ms < 0)
		delay_ms = I915_POWER_ASYNC_PUT_DELAY_MS;

	/* Defers or puts the reference under the domains lock. */
	i915_pw_switch(pd, pwc, &ctx, &sw);
	functions = i915_pw_functions(pd->display_ver, domain);
	mutex_lock(&pd->lock);

	pd->async_puts++;
	use_count = pd->domain_use_count[domain];
	i915_pw_count_put(pd, domain);
	if (pd->async_ops == NULL || use_count > 1u) {
		/* Not the last reference, or no delayed work to hand it to: an ordinary put. */
		drv_i915_xd_power_put(&pd->xd->power, functions, &sw);
	} else {
		/* The last reference: the wells stay up until the work gives them back. */
		drv_i915_xd_power_put_later(&pd->xd->power, functions, delay_ms, &sw);
	}
	i915_pw_copy_counts(pd);

	mutex_unlock(&pd->lock);
}

/*
 * Runs the delayed work of the asynchronous put
 * (intel_display_power_put_async_work()).
 */
void
drv_i915_display_power_async_work(
	struct i915_power_domains *pd)
{
	struct i915_pw_switch_ctx ctx;
	struct i915_xd_power_switch sw;

	/* Gives back what is owed to the work under the domains lock, with the bound context. */
	i915_pw_switch(pd, pd->async_pwc, &ctx, &sw);
	mutex_lock(&pd->lock);

	drv_i915_xd_power_work(&pd->xd->power, &sw);
	i915_pw_copy_counts(pd);

	mutex_unlock(&pd->lock);
}

/*
 * Releases every reference owed to the work now (intel_display_power_flush_work()).
 */
void
drv_i915_display_power_flush_work(
	struct i915_power_domains *pd)
{
	struct i915_pw_switch_ctx ctx;
	struct i915_xd_power_switch sw;

	/* Nothing can be owed without a bound work and its context. */
	if (pd->async_pwc == NULL)
		return;

	/* Gives back what is owed under the domains lock. */
	i915_pw_switch(pd, pd->async_pwc, &ctx, &sw);
	mutex_lock(&pd->lock);

	drv_i915_xd_power_flush(&pd->xd->power, &sw);
	i915_pw_copy_counts(pd);

	mutex_unlock(&pd->lock);
}

/*
 * Releases every owed reference and waits out a running work
 * (intel_display_power_flush_work_sync()).
 */
void
drv_i915_display_power_flush_work_sync(
	struct i915_power_domains *pd)
{
	int owes;

	/* Releases what is owed. */
	drv_i915_display_power_flush_work(pd);

	/* Cancels the work synchronously, outside the lock its body takes. */
	if (pd->async_ops != NULL)
		(void)pd->async_ops->cancel(pd->async_ctx, 1);

	/* Checks that nothing is owed any more. */
	mutex_lock(&pd->lock);

	owes = drv_i915_xd_power_owes(&pd->xd->power);
	if (owes || pd->xd->power.work_armed)
		pd->xd->power.stats.errors++;
	i915_pw_copy_counts(pd);

	mutex_unlock(&pd->lock);
}

/*
 * Prepares the PM demand lock and wait queue (intel_pmdemand_init_early()).
 */
void
drv_i915_pmdemand_init_early(
	struct i915_pmdemand *pm)
{
	/* Prepares the lock and the queue. */
	(void)mutex_init(&pm->lock, LOCK_RANK_DEVICE, "i915-pmdemand");
	waitq_init(&pm->waitqueue, "i915-pmdemand");

	/* Marks the early initialization as done. */
	pm->early_initialized = 1;
}

/*
 * Reports the DBUF slices that are powered now.
 */
uint8_t
drv_i915_enabled_dbuf_slices_mask(
	struct i915_display_core *dc)
{
	uint8_t mask;
	unsigned slice;
	uint32_t value;

	/* Reads each slice's power state. */
	mask = 0u;
	for (slice = 0u; slice < 4u; slice++) {
		value = drv_i915_raw_read32(dc->m, i915_dbuf_ctl_s[slice]);
		if ((value & DBUF_POWER_STATE) != 0u)
			mask |= (uint8_t)(1u << slice);
	}

	/* Succeeded: reports the powered slices. */
	return mask;
}

/*
 * Reports the control register of a DBUF slice, or 0 past the fourth.
 *
 * The table itself, for an independent check against the reference's
 * macro.
 */
uint32_t
drv_i915_dbuf_ctl_reg(
	unsigned slice)
{
	/* A slice past the table has no register. */
	if (slice >= 4u)
		return 0u;

	/* Succeeded: reports the slice's register. */
	return i915_dbuf_ctl_s[slice];
}

/*
 * Powers the requested DBUF slices on and the others off, within the
 * slices the display has (the body of intel_dbuf_pre/post_plane_update()).
 */
void
drv_i915_gen9_dbuf_slices_update(
	struct i915_display_core *dc,
	uint8_t req_slices)
{
	uint8_t slices;

	/* Keeps the request within the display's slices and applies it. */
	slices = (uint8_t)(req_slices & i915_dbuf_slice_mask(dc));
	i915_gen9_dbuf_slices_update(dc, slices);
}

/*
 * Brings the display core up (intel_power_domains_init_hw()).
 *
 * Runs icl_display_core_init(), then takes the POWER_DOMAIN_INIT reference
 * that keeps every well on until intel_power_domains_enable(), and syncs
 * every well with the hardware.  An adaptation-layer fault stops at the
 * child where it happened, with no reference taken and no sync run.
 */
void
drv_i915_power_domains_init_hw(
	struct i915_display_core *dc,
	int resume)
{
	struct i915_power_domains *pd;
	unsigned index;
	int core_result;

	/* Marks the bring-up as running. */
	pd = dc->pd;
	dc->initializing = 1;

	/* Shares the DC_off enable's dependencies through the well context (the same objects). */
	dc->pwc->cd = dc->cd;
	dc->pwc->dbuf_slices = &dc->dbuf_enabled_slices;
	dc->pwc->target_dc_state = pd->target_dc_state;
	dc->pwc->allowed_dc_mask = pd->allowed_dc_mask;

	/* Runs icl_display_core_init() (display version 11+). */
	core_result = i915_icl_display_core_init(dc, resume);
	if (core_result != 0) {
		/* A fault mid-child: no INIT reference and no sync; the fault position is kept. */
		dc->initializing = 0;
		return;
	}

	/*
	 * Keeps every well on for the dependent hardware access of the init and
	 * to keep what the firmware enabled powered until the readout.  The
	 * reference is dropped by intel_power_domains_enable().
	 */
	(void)drv_i915_display_power_get(pd, I915_PW_DOMAIN_INIT, dc->pwc);
	dc->init_wakeref_held = 1;
	dc->pm_wakeref = 1;
	dc->reached_init_ref = 1;

	/*
	 * disable_power_well is set on these platforms, so the reference takes
	 * no extra disable_wakeref.
	 */

	/* intel_power_domains_sync_hw(): syncs every well under the domains lock. */
	mutex_lock(&pd->lock);

	for (index = 0u; index < pd->num_power_wells; index++)
		drv_i915_power_well_sync_hw(&pd->power_wells[index], dc->pwc);

	mutex_unlock(&pd->lock);

	/* Records the end of the bring-up. */
	dc->reached_sync_hw = 1;
	dc->initializing = 0;
}

/*
 * Gives the runtime-PM side of the init reference back
 * (intel_power_domains_driver_remove()).
 *
 * The reference takes the init wakeref, puts no domain on these platforms
 * (disable_power_well is set), flushes the asynchronous put, verifies and
 * puts only the runtime-PM wakeref: the wells keep their count and stay
 * on for a reload.
 */
void
drv_i915_power_domains_driver_remove(
	struct i915_display_core *dc)
{
	int had_wakeref;

	/* wakeref = fetch_and_zero(&init_wakeref). */
	had_wakeref = dc->init_wakeref_held;
	dc->init_wakeref_held = 0;

	/*
	 * No disable_wakeref domain put, and the flush and verify have nothing
	 * to do here.  intel_runtime_pm_put(wakeref) gives the runtime-PM side
	 * back; there is intentionally no POWER_DOMAIN_INIT put.
	 */
	if (had_wakeref)
		dc->pm_wakeref = 0;
}

/*
 * Compares each well's software count with its hardware state
 * (intel_power_domains_verify_state()) and reports the mismatches.
 *
 * The reference compiles it under CONFIG_DRM_I915_DEBUG_RUNTIME_PM only;
 * here the per-well half is kept as a diagnostic.  The per-domain half
 * needs counts this port does not keep.
 */
unsigned
drv_i915_power_domains_verify_state(
	struct i915_power_domains *pd,
	struct i915_pw_ctx *pwc)
{
	unsigned index;
	unsigned mismatches;
	struct i915_power_well *well;
	int enabled;
	int expect;

	/* Compares every well. */
	mismatches = 0u;
	for (index = 0u; index < pd->num_power_wells; index++) {
		well = &pd->power_wells[index];
		enabled = drv_i915_power_well_is_enabled(well, pwc);

		/* A held or always-on well is expected on, any other off. */
		expect = 0;
		if (well->refcount != 0u || well->always_on)
			expect = 1;

		/* Reports a well whose state does not follow its count. */
		if (expect != enabled) {
			kern_logf("i915: power well %s state mismatch (refcount %u/enabled %d)\n",
				well->name, well->refcount, enabled);
			mismatches++;
		}
	}

	/* Succeeded: reports how many wells disagreed. */
	return mismatches;
}

/*
 * Gives the POWER_DOMAIN_INIT reference back (intel_power_domains_enable()).
 *
 * Every well no other reference holds turns off, DC_off last, so DC6 is
 * allowed from here on.  The wells on before and after, the DC state and
 * the verification are recorded in the probe state.
 */
void
drv_i915_power_domains_enable(
	struct i915_driver_probe *probe,
	struct i915_display_core *dc)
{
	/* Counts the wells on before the put. */
	probe->wells_on_before = i915_wells_on(dc->pd, dc->pwc);

	/* wakeref = fetch_and_zero(&init_wakeref); intel_display_power_put(INIT, wakeref). */
	if (dc->init_wakeref_held) {
		dc->init_wakeref_held = 0;
		dc->pm_wakeref = 0;
		drv_i915_display_power_put(dc->pd, I915_PW_DOMAIN_INIT, dc->pwc);
	}

	/* Records what the put left. */
	probe->wells_on_after = i915_wells_on(dc->pd, dc->pwc);
	probe->dc_state_after = (int)dc->pwc->dc_state;
	probe->verify_mismatches = drv_i915_power_domains_verify_state(dc->pd, dc->pwc);
}

/*
 * Takes the POWER_DOMAIN_INIT reference again (intel_power_domains_disable()).
 */
void
drv_i915_power_domains_disable(
	struct i915_display_core *dc)
{
	/* init_wakeref = intel_display_power_get(INIT). */
	if (!dc->init_wakeref_held) {
		(void)drv_i915_display_power_get(dc->pd, I915_PW_DOMAIN_INIT, dc->pwc);
		dc->init_wakeref_held = 1;
		dc->pm_wakeref = 1;
	}

	/* Verifies the wells against their counts. */
	(void)drv_i915_power_domains_verify_state(dc->pd, dc->pwc);
}

/*
 * Takes the display core down for a suspend (intel_power_domains_suspend()
 * -> icl_display_core_uninit()).
 *
 * The caller took the INIT reference with drv_i915_power_domains_disable()
 * when the suspend began and has stopped every output.  The INIT reference
 * is given back, so that every well no other reference holds turns off;
 * the parked puts are finished and the wells checked against their counts;
 * then the DC states are left, the DBUF, the CDCLK, power well 1 and the
 * combo PHYs go down.  The resume's drv_i915_power_domains_init_hw(dc, 1)
 * brings the core up again.
 */
void
drv_i915_power_domains_suspend(
	struct i915_display_core *dc)
{
	/* Gives the INIT reference back. */
	if (dc->init_wakeref_held) {
		dc->init_wakeref_held = 0;
		dc->pm_wakeref = 0;
		drv_i915_display_power_put(dc->pd, I915_PW_DOMAIN_INIT, dc->pwc);
	}

	/* Finishes the parked puts and checks every well against its count. */
	drv_i915_display_power_flush_work_sync(dc->pd);
	(void)drv_i915_power_domains_verify_state(dc->pd, dc->pwc);

	/* Takes the core down; core_suspended tells the resume to bring it up again. */
	i915_icl_display_core_uninit(dc);
	dc->core_suspended = 1;
}

/*
 * Puts the display in DC9 for S0 idle, with the PCH let stop the
 * display's reference clock.
 *
 * The display core must be down and the interrupts off (irqs_enabled
 * zero).  It reports the refusal of the xd power wells' check without
 * writing anything.
 */
int
drv_i915_display_dc9_enter(
	struct i915_display_core *dc,
	int irqs_enabled)
{
	struct i915_xd_power *power;
	struct i915_xd_dc_report report;
	int error;

	/* A display without the xd state cannot enter DC9. */
	power = i915_pw_dc_policy(dc->pwc);
	if (power == NULL)
		return EINVAL;

	/* Enters DC9 when the checks allow it. */
	error = drv_i915_xd_power_dc9_enter(power, &dc->pwc->xd->io, irqs_enabled, &report);
	i915_pw_dc_log(dc->pwc, &report);
	if (error != 0) {
		kern_logf("i915: DC9: not entered (%d; DC_STATE_EN 0x%x, PW_2 %d, interrupts %d)\n", error, report.checked, report.pw2_on, irqs_enabled);
		return error;
	}

	/* Succeeded: the display is in DC9. */
	kern_logf("i915: DC9: entered\n");
	return 0;
}

/*
 * Leaves DC9 after S0 idle: the PCH's reference clock first, then the DC
 * state.  It reports the refusal of the xd power wells' check without
 * writing the DC state.
 */
int
drv_i915_display_dc9_leave(
	struct i915_display_core *dc)
{
	struct i915_xd_power *power;
	struct i915_xd_dc_report report;
	int error;

	/* A display without the xd state cannot leave DC9. */
	power = i915_pw_dc_policy(dc->pwc);
	if (power == NULL)
		return EINVAL;

	/* Leaves DC9 when the checks allow it. */
	error = drv_i915_xd_power_dc9_leave(power, &dc->pwc->xd->io, &report);
	i915_pw_dc_log(dc->pwc, &report);
	if (error != 0) {
		kern_logf("i915: DC9: not left (%d; DC_STATE_EN 0x%x, PW_2 %d)\n", error, report.checked, report.pw2_on);
		return error;
	}

	/* Succeeded: the display left DC9. */
	kern_logf("i915: DC9: left\n");
	return 0;
}

/*
 * Takes a domain into a crtc's power-domain set (intel_display_power_get_in_set()).
 */
void
drv_i915_display_power_get_in_set(
	struct drm_i915_private *i915,
	struct intel_display_power_domain_set *power_domain_set,
	enum intel_display_power_domain domain)
{
	intel_wakeref_t wf;
	bool already_in_set;

	/* A domain already in the set is a warning of the Linux text. */
	already_in_set = test_bit(domain, power_domain_set->mask.bits);
	if (already_in_set)
		drv_i915_lcd_error("WARN_ON(test_bit(domain, power_domain_set->mask.bits))\n");

	/*
	 * Takes the reference.  The cookie is kept only under
	 * CONFIG_DRM_I915_DEBUG_RUNTIME_PM, which is not enabled.
	 */
	wf = i915_lcd_intel_display_power_get(i915, domain);
	UNUSED_PARAMETER(wf);

	/* Records the domain as held by the set. */
	i915_set_bit(domain, power_domain_set->mask.bits);
}

/*
 * Gives the domains of a mask back from a crtc's power-domain set
 * (intel_display_power_put_mask_in_set()).
 */
void
drv_i915_display_power_put_mask_in_set(
	struct drm_i915_private *i915,
	struct intel_display_power_domain_set *power_domain_set,
	struct intel_power_domain_mask *mask)
{
	enum intel_display_power_domain domain;
	bool subset;
	bool in_mask;

	/* A domain the set does not hold is a warning of the Linux text. */
	subset = i915_bitmap_subset(mask->bits, power_domain_set->mask.bits, POWER_DOMAIN_NUM);
	if (!subset)
		drv_i915_lcd_error("WARN_ON(!bitmap_subset(mask->bits, power_domain_set->mask.bits, POWER_DOMAIN_NUM))\n");

	/*
	 * Puts every domain of the mask (for_each_power_domain) with the cookie
	 * -1: per-domain cookies are kept only under
	 * CONFIG_DRM_I915_DEBUG_RUNTIME_PM.
	 */
	for (domain = 0; domain < POWER_DOMAIN_NUM; domain++) {
		in_mask = test_bit(domain, mask->bits);
		if (!in_mask)
			continue;

		/* Gives the reference back and forgets it. */
		i915_lcd_intel_display_power_put(i915, domain, -1);
		i915_clear_bit(domain, power_domain_set->mask.bits);
	}
}

/*
 * Takes a power reference for a panel run (the run's power_get hook).
 *
 * Returns the cookie domain + 1, or 0 when the domain is unknown or could
 * not be taken.  The run counts its references per domain.
 */
int
drv_i915_lcd_power_get(
	void *ctx,
	int domain)
{
	struct i915_lcd_kernel *kernel;
	int get_result;

	/* Resolves the run the hook belongs to. */
	kernel = ctx;

	/* Refuses an unknown domain. */
	if (domain < 0 || domain >= (int)POWER_DOMAIN_NUM) {
		kernel->power_get_failures++;
		return 0;
	}

	/* Takes the domain. */
	get_result = drv_i915_display_power_get(kernel->d->pd, (enum i915_power_domain)domain, kernel->d->pwc);
	if (get_result != 0) {
		kernel->power_get_failures++;
		kern_logf("i915: LCD-B power get domain=%d FAILED rc=%d\n", domain, get_result);
		return 0;
	}

	/* The run's count of the references it holds on the domain. */
	kernel->power_refs[domain]++;

	/* Succeeded: the cookie names the domain. */
	return domain + 1;
}

/*
 * Takes a power reference for a panel run only when the domain is already
 * enabled (intel_display_power_get_if_enabled()).
 *
 * A readout never turns a well on.  Returns the cookie, or 0 when the
 * domain is off or unknown.
 */
int
drv_i915_lcd_power_get_if_enabled(
	void *ctx,
	int domain)
{
	struct i915_lcd_kernel *kernel;
	int enabled;
	int cookie;

	/* Resolves the run the hook belongs to. */
	kernel = ctx;

	/* An unknown domain is not enabled. */
	if (domain < 0 || domain >= (int)POWER_DOMAIN_NUM)
		return 0;

	/* The domain counts as enabled when every one of its wells is. */
	enabled = drv_i915_display_power_is_enabled(kernel->d->pd, (enum i915_power_domain)domain, kernel->d->pwc);
	if (!enabled)
		return 0;

	/* Takes the reference. */
	cookie = drv_i915_lcd_power_get(ctx, domain);

	/* Succeeded: reports the cookie (0 when the get failed). */
	return cookie;
}

/*
 * Gives a power reference of a panel run back (the run's power_put hook).
 *
 * A put the run holds no reference for is counted and not forwarded.  A
 * well that could not be turned off (a failed pipe interrupt drain) is a
 * backend fault: the stop is not confirmed.
 */
void
drv_i915_lcd_power_put(
	void *ctx,
	int domain,
	int wakeref)
{
	struct i915_lcd_kernel *kernel;
	unsigned refusals_before;

	UNUSED_PARAMETER(wakeref);

	/* Resolves the run the hook belongs to. */
	kernel = ctx;

	/* Refuses a put without a reference of this run. */
	if (domain < 0 || domain >= (int)POWER_DOMAIN_NUM || kernel->power_refs[domain] <= 0) {
		kernel->errors++;
		kern_logf("i915: LCD-B power put domain=%d without a reference of this run: NOT forwarded\n", domain);
		return;
	}

	/* The run's count of the references it holds on the domain. */
	kernel->power_refs[domain]--;

	/* Puts the domain, watching for a refused well disable. */
	refusals_before = kernel->d->pwc->disable_refusals;
	drv_i915_display_power_put(kernel->d->pd, (enum i915_power_domain)domain, kernel->d->pwc);
	if (kernel->d->pwc->disable_refusals != refusals_before) {
		drv_i915_lcd_backend_fault("a power well was NOT turned off: the pipe interrupt drain failed (the stop is not "
			"confirmed; display power is kept)\n");
	}
}

/*
 * Gives a power reference of a panel run back after a delay (the run's
 * power_put_async hook).
 *
 * A put the run holds no reference for is counted and not forwarded.
 */
void
drv_i915_lcd_power_put_async(
	void *ctx,
	int domain,
	int wakeref,
	int delay_ms)
{
	struct i915_lcd_kernel *kernel;

	UNUSED_PARAMETER(wakeref);

	/* Resolves the run the hook belongs to. */
	kernel = ctx;

	/* Refuses a put without a reference of this run. */
	if (domain < 0 || domain >= (int)POWER_DOMAIN_NUM || kernel->power_refs[domain] <= 0) {
		kernel->errors++;
		kern_logf("i915: LCD-B async power put domain=%d without a reference of this run: NOT forwarded\n", domain);
		return;
	}

	/* The run's count of the references it holds on the domain. */
	kernel->power_refs[domain]--;

	/* Hands the reference to the asynchronous put. */
	drv_i915_display_power_put_async(kernel->d->pd, (enum i915_power_domain)domain, kernel->d->pwc, delay_ms);
}

/*
 * Names the power domain that powers an AUX channel outside Thunderbolt.
 *
 * A combo channel (A to C, and display version 13's D and E) takes its AUX
 * well.  A Type-C channel takes its AUX_USBC well, which is also what keeps
 * the Type-C subsystem out of TC cold while the port is used in DP-alt or
 * legacy mode (Linux v6.8.12 intel_display_power.c, the display version 12
 * and 13 port-domain tables).  An unknown channel takes AUX_A, the answer
 * Linux warns about and gives.
 */
enum i915_power_domain
drv_i915_aux_legacy_power_domain(
	unsigned display_ver,
	int aux_ch)
{
	/* Channels A to C are the combo PHY ports of every version. */
	if (aux_ch >= I915_AUX_CH_A && aux_ch <= I915_AUX_CH_C)
		return (enum i915_power_domain)(I915_PW_DOMAIN_AUX_A + (aux_ch - I915_AUX_CH_A));

	/* Display version 13 has four Type-C channels and then the combo ports D and E. */
	if (display_ver >= 13u) {
		/* A Type-C channel takes the AUX_USBC well of its number. */
		if (aux_ch >= I915_AUX_CH_USBC1 && aux_ch <= I915_AUX_CH_USBC4)
			return (enum i915_power_domain)(I915_PW_DOMAIN_AUX_USBC1 + (aux_ch - I915_AUX_CH_USBC1));

		/* The combo ports D and E take the AUX wells D and E. */
		if (aux_ch >= I915_AUX_CH_D_XELPD && aux_ch <= I915_AUX_CH_E_XELPD)
			return (enum i915_power_domain)(I915_PW_DOMAIN_AUX_D + (aux_ch - I915_AUX_CH_D_XELPD));

		/* Succeeded: any other channel falls back to AUX_A. */
		return I915_PW_DOMAIN_AUX_A;
	}

	/* Display version 12 has six Type-C channels. */
	if (aux_ch >= I915_AUX_CH_USBC1 && aux_ch <= I915_AUX_CH_USBC6)
		return (enum i915_power_domain)(I915_PW_DOMAIN_AUX_USBC1 + (aux_ch - I915_AUX_CH_USBC1));

	/* Succeeded: any other channel falls back to AUX_A. */
	return I915_PW_DOMAIN_AUX_A;
}

/*
 * Names the power domain of an AUX channel used through Thunderbolt.
 *
 * Only a Type-C channel has one (AUX_TBT1 on); any other channel answers
 * I915_PW_DOMAIN_NUM, which no well provides.
 */
enum i915_power_domain
drv_i915_aux_tbt_power_domain(
	unsigned display_ver,
	int aux_ch)
{
	int last_tc;

	/* Display version 13 has four Type-C channels, version 12 six. */
	last_tc = I915_AUX_CH_USBC6;
	if (display_ver >= 13u)
		last_tc = I915_AUX_CH_USBC4;

	/* A Type-C channel takes the Thunderbolt AUX well of the same number. */
	if (aux_ch >= I915_AUX_CH_USBC1 && aux_ch <= last_tc)
		return (enum i915_power_domain)(I915_PW_DOMAIN_AUX_TBT1 + (aux_ch - I915_AUX_CH_USBC1));

	/* Succeeded: a combo channel has no Thunderbolt domain. */
	return I915_PW_DOMAIN_NUM;
}

/*
 * Names the power domain of an AUX channel's I/O.
 *
 * Only a combo channel has one: A to C, and on display version 13 the
 * combo ports D and E.  A Type-C channel has none and takes AUX_IO_A, the
 * answer Linux warns about and gives.
 */
enum i915_power_domain
drv_i915_aux_io_power_domain(
	unsigned display_ver,
	int aux_ch)
{
	/* Channels A to C are the combo PHY ports of every version. */
	if (aux_ch >= I915_AUX_CH_A && aux_ch <= I915_AUX_CH_C)
		return (enum i915_power_domain)(I915_PW_DOMAIN_AUX_IO_A + (aux_ch - I915_AUX_CH_A));

	/* Display version 13's combo ports D and E. */
	if (display_ver >= 13u &&
	    aux_ch >= I915_AUX_CH_D_XELPD &&
	    aux_ch <= I915_AUX_CH_E_XELPD)
		return (enum i915_power_domain)(I915_PW_DOMAIN_AUX_IO_D + (aux_ch - I915_AUX_CH_D_XELPD));

	/* Succeeded: a Type-C channel falls back to AUX_IO_A. */
	return I915_PW_DOMAIN_AUX_IO_A;
}

/* Computes the allowed DC states (get_allowed_dc_mask(): display version 12+ allows DC9, DC3CO and up to DC6). */
static uint32_t
i915_get_allowed_dc_mask(
	unsigned display_ver,
	int enable_dc)
{
	uint32_t mask;
	uint32_t max_states;

	/* DC9 is allowed from display version 11. */
	mask = 0u;
	if (display_ver >= 11u)
		mask = I915_DC_STATE_EN_DC9;

	/* enable_dc: -1 is auto (the platform maximum), 0 disabled, anything else the maximum. */
	max_states = I915_DC_STATE_EN_DC3CO | I915_DC_STATE_EN_UPTO_DC6;
	if (enable_dc != 0)
		mask |= max_states;

	/* Succeeded: reports the allowed states. */
	return mask;
}

/* Falls to the next DC state while the requested one is not allowed (sanitize_target_dc_state()). */
static uint32_t
i915_sanitize_target_dc_state(
	const struct i915_power_domains *pd,
	uint32_t target)
{
	static const uint32_t states[] = {
		I915_DC_STATE_EN_UPTO_DC6,
		I915_DC_STATE_EN_UPTO_DC5,
		0u	/* DC_STATE_DISABLE */
	};
	unsigned index;

	/* Walks the states from the deepest, stepping down past each one that is not allowed. */
	for (index = 0u; index + 1u < ARRAY_SIZE(states); index++) {
		if (target != states[index])
			continue;
		if ((pd->allowed_dc_mask & target) != 0u)
			break;
		target = states[index + 1u];
	}

	/* Succeeded: reports the deepest allowed state at or below the request. */
	return target;
}

/* Sanitizes the disable_power_well option (the platform default allows dynamic disabling). */
static int
i915_sanitize_disable_power_well(
	int param)
{
	/* A negative value takes the platform default. */
	if (param < 0)
		return 1;

	/* Any other value is a plain yes or no. */
	if (param != 0)
		return 1;

	/* Succeeded: dynamic disabling is off. */
	return 0;
}

/* Names the driver request register of a well family (i915_reg.h). */
static unsigned
i915_pw_driver_reg(
	enum i915_pw_ops_kind ops)
{
	/* Picks the family's register. */
	switch (ops) {
	case I915_PW_OPS_ICL_AUX:
		return ICL_PWR_WELL_CTL_AUX2;
	case I915_PW_OPS_ICL_DDI:
		return ICL_PWR_WELL_CTL_DDI2;
	default:
		break;
	}

	/* Every other family uses HSW_PWR_WELL_CTL2 (the driver's). */
	return HSW_PWR_WELL_CTL2;
}

/* The state bit of a control index (HSW_PWR_WELL_CTL_STATE). */
static uint32_t
i915_pw_state(
	unsigned idx)
{
	/* Each index owns two bits, the state the lower one. */
	return 0x1u << (idx * 2u);
}

/*
 * Finds a well switched by a request in the platform's description: the
 * row of the same control and index.  Returns the row, or -1 (logged)
 * when the display has no description or the description no such well.
 */
static int
i915_pw_xd_well(
	const struct i915_power_well *well,
	struct i915_pw_ctx *pwc)
{
	unsigned control;
	int xd_well;

	/* A display without a description cannot switch the well. */
	if (pwc->xd == NULL || !pwc->xd->chosen) {
		kern_logf("i915: power well %s: no platform description\n", well->name);
		return -1;
	}

	/* The control the well's operations family switches. */
	control = I915_XD_WELL_CONTROL_MAIN;
	if (well->ops == I915_PW_OPS_ICL_AUX)
		control = I915_XD_WELL_CONTROL_AUX;
	if (well->ops == I915_PW_OPS_ICL_DDI)
		control = I915_XD_WELL_CONTROL_DDI;

	/* Looks the row up. */
	xd_well = drv_i915_xd_power_find(pwc->xd->choice.platform, control, well->hsw_idx);
	if (xd_well < 0) {
		kern_logf("i915: power well %s: not in the %s description\n", well->name, pwc->xd->choice.platform->name);
		return -1;
	}

	/* Succeeded: the well's row. */
	return xd_well;
}

/* Fills the xd power wells' hooks with this context's VGA, interrupt and Type-C PHY steps. */
static void
i915_pw_xd_hooks(
	struct i915_pw_ctx *pwc,
	struct i915_power_well *well,
	struct i915_pw_hook_ctx *hook,
	struct i915_xd_power_hooks *hooks)
{
	/* The context and the well the hooks act for. */
	hook->pwc = pwc;
	hook->well = well;

	/* The steps. */
	hooks->pipes_up = i915_pw_hook_pipes_up;
	hooks->pipes_down = i915_pw_hook_pipes_down;
	hooks->vga_reset = i915_pw_hook_vga_reset;
	hooks->tc_health = i915_pw_hook_tc_health;
	hooks->context = hook;
}

/*
 * Restores the interrupts of a well's pipes after it came up
 * (gen8_irq_power_well_post_enable()).  It acts only once interrupts are
 * enabled; before they are installed it does nothing.
 */
static void
i915_pw_hook_pipes_up(
	void *context,
	unsigned pipe_mask)
{
	struct i915_pw_hook_ctx *hook;
	struct i915_pw_ctx *pwc;

	/* Finds the context. */
	hook = context;
	pwc = hook->pwc;

	/* Restores the pipe's interrupt registers, which live in the well. */
	if (pwc->irqs_enabled) {
		pwc->irq_post_enable_calls++;
		if (pwc->irq_ops != NULL)
			pwc->irq_ops->post_enable(pwc->irq_ctx, pipe_mask);
	}
}

/*
 * Drains the interrupts of a well's pipes before it goes off
 * (gen8_irq_power_well_pre_disable()): stops the pipe's interrupt
 * sources and waits for a handler already running; afterwards the
 * handler would read a powered-off register.  Reports nonzero when the
 * drain failed now or earlier: the well then stays on.
 */
static int
i915_pw_hook_pipes_down(
	void *context,
	unsigned pipe_mask)
{
	struct i915_pw_hook_ctx *hook;
	struct i915_pw_ctx *pwc;
	int drain_result;

	/* Finds the context. */
	hook = context;
	pwc = hook->pwc;

	/* Drains through the interrupt layer once interrupts are enabled. */
	if (pwc->irqs_enabled && !pwc->irq_sync_failed) {
		pwc->irq_pre_disable_calls++;
		if (pwc->irq_ops != NULL) {
			/* Drains the pipe's interrupts through the interrupt layer. */
			drain_result = pwc->irq_ops->pre_disable(pwc->irq_ctx, pipe_mask);

			/* The handler was not shown to have left the pipe: this well and every later one stay on. */
			if (drain_result != 0) {
				pwc->irq_sync_failed = 1;
				kern_logf("i915: power well %s: pipe interrupt drain FAILED; the well is kept on and every later "
					"well disable is refused\n", hook->well->name);
			}
		}
	}

	/* A failed drain keeps the well on. */
	if (pwc->irq_sync_failed)
		return 1;

	/* Succeeded: the pipes are drained. */
	return 0;
}

/* Keeps vgacon sane after the well that holds the VGA plane is on (intel_vga_reset_io_mem()). */
static void
i915_pw_hook_vga_reset(
	void *context)
{
	struct i915_pw_hook_ctx *hook;

	/* Rewrites the VGA registers through the VGA client. */
	hook = context;
	drv_i915_vga_reset_io_mem(hook->pwc->vga);
	hook->pwc->vga_reset_calls++;
}

/*
 * Waits up to a millisecond for a Type-C port's PHY microcontroller to
 * report itself healthy after its AUX_USBC well came up.
 *
 * A PHY that does not report it is warned about and counted, and the
 * enable continues, as Linux does.  Returns 0, or EIO when the time base
 * failed.
 */
static int
i915_pw_hook_tc_health(
	void *context,
	unsigned tc_port)
{
	struct i915_pw_hook_ctx *hook;
	struct i915_pw_ctx *pwc;
	int wait_result;

	/* Finds the context. */
	hook = context;
	pwc = hook->pwc;

	/* Without the PHY access (the GPU-free tests) there is nothing to read. */
	if (pwc->dkl == NULL)
		return 0;

	/* Polls the microcontroller's health bit through the Dekel window. */
	wait_result = drv_i915_dkl_phy_wait_set(pwc->dkl, tc_port, I915_DKL_CMN_UC_DW27, I915_DKL_CMN_UC_DW27_UC_HEALTH, I915_TC_UC_HEALTH_TIMEOUT_US);
	if (wait_result == EIO)
		return EIO;

	/* A PHY that stayed unhealthy is warned about; the enable goes on. */
	if (wait_result != 0) {
		pwc->tc_uc_health_timeouts++;
		kern_logf("i915: power well %s: timeout waiting for the Type-C PHY's microcontroller health (TC%u)\n",
			hook->well->name,
			tc_port + 1u);
	}

	/* Succeeded: the wait is over. */
	return 0;
}

/*
 * Names the functions of the xd power wells a domain stands for.
 *
 * The pipes, transcoders, DDI lanes and IO, and AUX channels map to their
 * own functions; the ports D and E of display 13 sit on the DDI and AUX
 * numbers that Tiger Lake gives its Type-C ports 5 and 6.  The audio
 * registers need only the exit from DC states on display 13, the audio
 * well on display 12, and DSI (display 13 only) the exit from DC states.  The init
 * reference holds every pipe and the exit from DC states.  A domain no
 * well powers (the display core, GMBUS, the GT interrupts, the eDP
 * transcoder, the Type-C cold block) has none.
 *
 * Values referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_display_power_map.c
 * (xelpd_pwdoms_pw_a to xelpd_pwdoms_pw_d, xelpd_pwdoms_pw_2,
 * xelpd_pwdoms_dc_off, tgl_pwdoms_pw_3 to tgl_pwdoms_pw_5,
 * tgl_pwdoms_dc_off, icl_pwdoms_aux_a to icl_pwdoms_aux_e).
 */
static uint64_t
i915_pw_functions(
	unsigned display_ver,
	enum i915_power_domain domain)
{
	unsigned offset;

	/* A pipe and its panel fitter need the pipe. */
	if (domain >= I915_PW_DOMAIN_PIPE_A && domain <= I915_PW_DOMAIN_PIPE_D)
		return I915_XD_POWERS_PIPE((unsigned)(domain - I915_PW_DOMAIN_PIPE_A));
	if (domain >= I915_PW_DOMAIN_PIPE_PANEL_FITTER_A && domain <= I915_PW_DOMAIN_PIPE_PANEL_FITTER_D)
		return I915_XD_POWERS_PIPE((unsigned)(domain - I915_PW_DOMAIN_PIPE_PANEL_FITTER_A));

	/* A transcoder A to D needs the transcoder. */
	if (domain >= I915_PW_DOMAIN_TRANSCODER_A && domain <= I915_PW_DOMAIN_TRANSCODER_D)
		return I915_XD_POWERS_TRANS((unsigned)(domain - I915_PW_DOMAIN_TRANSCODER_A));

	/* The lanes, the DDI IO, the AUX IO and the AUX channel of a port need that function of its DDI. */
	if (domain >= I915_PW_DOMAIN_PORT_DDI_LANES_A && domain <= I915_PW_DOMAIN_PORT_DDI_LANES_TC6) {
		offset = (unsigned)(domain - I915_PW_DOMAIN_PORT_DDI_LANES_A);
		return i915_pw_port_function(display_ver, offset, I915_XD_POWERS_LANES(0U));
	}
	if (domain >= I915_PW_DOMAIN_PORT_DDI_IO_A && domain <= I915_PW_DOMAIN_PORT_DDI_IO_TC6) {
		offset = (unsigned)(domain - I915_PW_DOMAIN_PORT_DDI_IO_A);
		return i915_pw_port_function(display_ver, offset, I915_XD_POWERS_DDI_IO(0U));
	}
	if (domain >= I915_PW_DOMAIN_AUX_IO_A && domain <= I915_PW_DOMAIN_AUX_IO_F) {
		offset = (unsigned)(domain - I915_PW_DOMAIN_AUX_IO_A);
		return i915_pw_port_function(display_ver, offset, I915_XD_POWERS_AUX_IO(0U));
	}
	if (domain >= I915_PW_DOMAIN_AUX_A && domain <= I915_PW_DOMAIN_AUX_USBC6) {
		offset = (unsigned)(domain - I915_PW_DOMAIN_AUX_A);
		return i915_pw_port_function(display_ver, offset, I915_XD_POWERS_AUX(0U));
	}

	/* The Thunderbolt AUX of a Type-C port. */
	if (domain >= I915_PW_DOMAIN_AUX_TBT1 && domain <= I915_PW_DOMAIN_AUX_TBT6)
		return I915_XD_POWERS_TBT_AUX((unsigned)(domain - I915_PW_DOMAIN_AUX_TBT1));

	/* The remaining domains, one by one. */
	switch (domain) {
	case I915_PW_DOMAIN_PORT_DSI:
		/* DSI keeps the display out of DC states on display 13; Tiger Lake's wells have no DSI. */
		if (display_ver >= 13u)
			return I915_XD_POWERS_DC_OFF;
		return 0u;
	case I915_PW_DOMAIN_VGA:
		return I915_XD_POWERS_VGA;
	case I915_PW_DOMAIN_AUDIO_PLAYBACK:
		return I915_XD_POWERS_AUDIO;
	case I915_PW_DOMAIN_AUDIO_MMIO:
		/* The audio registers sit outside power well 2 from display 13 on. */
		if (display_ver >= 13u)
			return I915_XD_POWERS_DC_OFF;
		return I915_XD_POWERS_AUDIO;
	case I915_PW_DOMAIN_DC_OFF:
		return I915_XD_POWERS_DC_OFF;
	case I915_PW_DOMAIN_INIT:
		/* The init reference keeps every pipe's wells and the exit from DC states. */
		return I915_XD_POWERS_PIPE(0U) |
		       I915_XD_POWERS_PIPE(1U) |
		       I915_XD_POWERS_PIPE(2U) |
		       I915_XD_POWERS_PIPE(3U) |
		       I915_XD_POWERS_DC_OFF;
	default:
		break;
	}

	/* No well powers the domain. */
	return 0u;
}

/*
 * Names the function of the DDI (or AUX channel) of a port, given the
 * port's place in the domain list (A to F, then Type-C 1 to 6) and the
 * function of DDI 0.  Display 13 numbers its ports D and E as 7 and 8;
 * display 12 gives those numbers to Type-C ports 5 and 6.  A port the
 * display does not have has no function.
 */
static uint64_t
i915_pw_port_function(
	unsigned display_ver,
	unsigned offset,
	uint64_t ddi0)
{
	/* Ports A to C are DDIs 0 to 2 on both. */
	if (offset <= 2u)
		return ddi0 << offset;

	/* Ports D and E are DDIs 7 and 8 on display 13; display 12 has no such port, nor any port F. */
	if (offset <= 5u) {
		if (display_ver >= 13u && offset <= 4u)
			return ddi0 << (offset + 4u);
		return 0u;
	}

	/* Type-C ports 1 to 4 are DDIs 3 to 6 on both. */
	if (offset <= 9u)
		return ddi0 << (offset - 3u);

	/* Type-C ports 5 and 6 are DDIs 7 and 8 on display 12 only. */
	if (display_ver == 12u)
		return ddi0 << (offset - 3u);

	/* Display 13 has no Type-C port 5 or 6. */
	return 0u;
}

/* Fills the copy of one well of the platform's table that the older code reads; its state is unknown. */
static void
i915_pw_copy_well(
	struct i915_power_well *well,
	const struct i915_xd_well_desc *desc,
	unsigned xd_well,
	unsigned display_ver)
{
	/* The name and the operations family of the well's control. */
	well->name = desc->name;
	well->ops = I915_PW_OPS_HSW;
	if (desc->control == I915_XD_WELL_CONTROL_AUX)
		well->ops = I915_PW_OPS_ICL_AUX;
	if (desc->control == I915_XD_WELL_CONTROL_DDI)
		well->ops = I915_PW_OPS_ICL_DDI;
	if (desc->control == I915_XD_WELL_CONTROL_DC_OFF)
		well->ops = I915_PW_OPS_DC_OFF;

	/* Power well 1 belongs to the display core and the DMC: no domain turns it off. */
	well->always_on = 0;
	if (desc->control == I915_XD_WELL_CONTROL_MAIN && desc->index == I915_XD_WELL_PW_1)
		well->always_on = 1;

	/* The facts the table carries. */
	well->has_vga = 0;
	if ((desc->flags & I915_XD_WELL_VGA) != 0u)
		well->has_vga = 1;
	well->has_fuses = 0;
	if (desc->fuse_gate != I915_XD_WELL_NO_FUSE)
		well->has_fuses = 1;
	well->is_tc_tbt = 0;
	if ((desc->flags & I915_XD_WELL_TBT_AUX) != 0u)
		well->is_tc_tbt = 1;
	well->fixed_enable_delay = 0;
	if ((desc->flags & I915_XD_WELL_FIXED_DELAY) != 0u)
		well->fixed_enable_delay = 1;
	well->enable_timeout = desc->enable_timeout_ms;
	well->irq_pipe_mask = desc->irq_pipes;
	well->hsw_idx = desc->index;

	/* The ids other code finds a well by. */
	well->id = I915_DISP_PW_ID_NONE;
	if (desc->control == I915_XD_WELL_CONTROL_DC_OFF)
		well->id = I915_SKL_DISP_DC_OFF;
	if (desc->control == I915_XD_WELL_CONTROL_MAIN) {
		if (desc->index == I915_XD_WELL_PW_1)
			well->id = I915_SKL_DISP_PW_1;
		if (desc->index == I915_XD_WELL_PW_2)
			well->id = I915_SKL_DISP_PW_2;
		if (desc->index == I915_XD_WELL_PW_3 && display_ver == 12u)
			well->id = I915_ICL_DISP_PW_3;
	}

	/* The entry in the table, no user, and a state unknown until the sync. */
	well->xd_well = xd_well;
	well->refcount = 0u;
	well->hw_enabled = -1;
}

/* Finds the copy of a well of the platform's table, or NULL for a well with none. */
static struct i915_power_well *
i915_pw_copy_of(
	struct i915_power_domains *pd,
	unsigned xd_well)
{
	unsigned index;

	/* Looks the entry up among the copies. */
	for (index = 0u; index < pd->num_power_wells; index++) {
		if (pd->power_wells[index].xd_well == xd_well)
			return &pd->power_wells[index];
	}

	/* The well has no copy. */
	return NULL;
}

/* Fills the switch the xd counts turn wells on and off through, with this state and context. */
static void
i915_pw_switch(
	struct i915_power_domains *pd,
	struct i915_pw_ctx *pwc,
	struct i915_pw_switch_ctx *ctx,
	struct i915_xd_power_switch *sw)
{
	/* The state and the well context the switching acts with. */
	ctx->pd = pd;
	ctx->pwc = pwc;

	/* The steps. */
	sw->on = i915_pw_switch_on;
	sw->off = i915_pw_switch_off;
	sw->context = ctx;
}

/*
 * Turns a well on for the xd counts through its copy's enable operation.
 * Returns 0, or the error that stops the caller.
 */
static int
i915_pw_switch_on(
	void *context,
	unsigned xd_well)
{
	struct i915_pw_switch_ctx *ctx;
	struct i915_power_well *well;
	int error;

	/*
	 * A well with no copy (Tiger Lake's TC cold mailbox) is not switched
	 * here; it counts as on while it has users.
	 */
	ctx = context;
	well = i915_pw_copy_of(ctx->pd, xd_well);
	if (well == NULL) {
		drv_i915_xd_power_record(&ctx->pd->xd->power, xd_well, 1);
		return 0;
	}

	/* Turns the well on. */
	error = drv_i915_power_well_enable(well, ctx->pwc);
	if (error != 0)
		return error;

	/* Succeeded: the well is on. */
	return 0;
}

/*
 * Turns a well off for the xd counts through its copy's disable
 * operation.  Returns 0, or EBUSY when a refused disable leaves the well
 * on (it is then kept on for good by the failed stop).
 */
static int
i915_pw_switch_off(
	void *context,
	unsigned xd_well)
{
	struct i915_pw_switch_ctx *ctx;
	struct i915_power_well *well;
	int error;

	/* A well with no copy is not switched here; it counts as off without users. */
	ctx = context;
	well = i915_pw_copy_of(ctx->pd, xd_well);
	if (well == NULL) {
		drv_i915_xd_power_record(&ctx->pd->xd->power, xd_well, 0);
		return 0;
	}

	/* Turns the well off. */
	error = drv_i915_power_well_disable(well, ctx->pwc);

	/* A refused disable keeps the well on. */
	if (error == EBUSY) {
		ctx->pwc->kept_wells++;
		return EBUSY;
	}

	/* Succeeded: the well is off, or its disable failed in a way that leaves nothing to keep. */
	return 0;
}

/* Gives one reference on a domain back from its count; a put at zero is reported, not counted. */
static void
i915_pw_count_put(
	struct i915_power_domains *pd,
	enum i915_power_domain domain)
{
	/* The count is the number of references the domain's users hold. */
	if (pd->domain_use_count[domain] == 0u) {
		pd->use_count_errors++;
		kern_logf("i915: power: use count on domain %u is already zero\n", (unsigned)domain);
	} else {
		pd->domain_use_count[domain]--;
	}
}

/* Copies the xd counts' users and diagnostics into the copies the older code reads. */
static void
i915_pw_copy_counts(
	struct i915_power_domains *pd)
{
	const struct i915_xd_power *power;
	unsigned index;

	/* Copies each well's users. */
	power = &pd->xd->power;
	for (index = 0u; index < pd->num_power_wells; index++)
		pd->power_wells[index].refcount = power->refs[pd->power_wells[index].xd_well];

	/* Copies the diagnostics of the delayed give-back. */
	pd->async_parked = power->stats.deferred;
	pd->async_grabs = power->stats.taken_over;
	pd->async_work_runs = power->stats.work_runs;
	pd->async_work_empty = power->stats.work_empty;
	pd->async_released = power->stats.released;
	pd->async_requeues = power->stats.requeues;
	pd->async_flushes = power->stats.flushes;
	pd->async_state_errors = power->stats.errors;
}

/* Arms the bound delayed work for the xd counts: 1 when it was not armed already. */
static int
i915_pw_work_queue(
	void *context,
	int delay_ms)
{
	struct i915_power_domains *pd;
	int queued;

	/* Arms the work through the bound operations. */
	pd = context;
	queued = pd->async_ops->queue(pd->async_ctx, delay_ms);

	/* Succeeded: reports whether it was newly armed. */
	return queued;
}

/* Disarms the bound delayed work for the xd counts. */
static int
i915_pw_work_cancel(
	void *context,
	int sync)
{
	struct i915_power_domains *pd;
	int cancelled;

	/* Disarms the work through the bound operations. */
	pd = context;
	cancelled = pd->async_ops->cancel(pd->async_ctx, sync);

	/* Succeeded: reports what the cancel found. */
	return cancelled;
}

/*
 * Records a well's last known state in its copy, and in the xd record the
 * interrupt paths read without the lock (1 on, 0 off, -1 unknown).
 */
static void
i915_pw_set_state(
	struct i915_pw_ctx *pwc,
	struct i915_power_well *well,
	int state)
{
	int on;

	/* The copy the older code reads. */
	well->hw_enabled = state;

	/* A context without the xd state (a well built by hand) has no record. */
	if (pwc == NULL || pwc->xd == NULL)
		return;

	/* The record of the well's entry. */
	on = 0;
	if (state == 1)
		on = 1;
	drv_i915_xd_power_record(&pwc->xd->power, well->xd_well, on);
}

/*
 * Leaves the DC states when DC_off is enabled.
 *
 * The current CDCLK and DBUF are read and compared with the saved state --
 * never re-initialized -- and the combo PHYs, whose context the DMC does
 * not keep across DC states, are restored.  The CDCLK and DBUF objects
 * are the device's own, shared through the well context.
 */
static void
i915_pw_dc_off_on(

	struct i915_pw_ctx *pwc)
{
	struct i915_xd_power *power;
	struct i915_xd_dc_report report;
	struct i915_cdclk_config readout;
	struct i915_trace *trace;
	unsigned slice;
	uint8_t current_slices;
	uint32_t value;

	/* Counts the enable for the diagnostics. */
	pwc->dc_off_enable_calls++;

	/* Leaves the DC states through the verified write, which the copy of the state last written follows. */
	power = i915_pw_dc_policy(pwc);
	if (power != NULL) {
		drv_i915_xd_power_dc_leave(power, &pwc->xd->io, &report);
		i915_pw_dc_log(pwc, &report);
	}

	/* Reads the CDCLK into a temporary and compares it with the saved state. */
	if (pwc->cd != NULL) {
		kern_memset(&readout, 0, sizeof(readout));
		drv_i915_bxt_get_cdclk(pwc->cd, &readout);
		pwc->dc_off_cdclk_readouts++;
		if (readout.cdclk != pwc->cd->hw.cdclk || readout.vco != pwc->cd->hw.vco) {
			kern_logf("i915: DC_off: cdclk needs modeset (hw %u/%u vs saved %u/%u)\n",
				readout.cdclk, readout.vco, pwc->cd->hw.cdclk, pwc->cd->hw.vco);
		}
	}

	/* gen9_assert_dbuf_enabled(): compares the powered slices with the saved mask. */
	if (pwc->dbuf_slices != NULL) {
		current_slices = 0u;
		for (slice = 0u; slice < 4u; slice++) {
			value = drv_i915_raw_read32(pwc->mmio, i915_dbuf_ctl_s[slice]);
			if ((value & DBUF_POWER_STATE) != 0u)
				current_slices |= (uint8_t)(1u << slice);
		}

		/* Counts the check and reports a mismatch. */
		pwc->dc_off_dbuf_asserts++;
		if (current_slices != *pwc->dbuf_slices) {
			kern_logf("i915: DC_off: DBUF mismatch (hw 0x%x vs saved 0x%x)\n",
				current_slices, *pwc->dbuf_slices);
		}
	}

	/* The DMC loses combo PHY B's context across DC on display version 11+: restores it. */
	trace = i915_dc_off_trace(pwc);
	drv_i915_trace_init(trace);
	i915_combo_phys_up(pwc, trace);
	pwc->dc_off_combo_inits++;
}

/*
 * Lets the DMC take the display into the target DC state when DC_off is
 * disabled, once its firmware is loaded.
 */
static void
i915_pw_dc_off_off(
	struct i915_pw_ctx *pwc)
{
	struct i915_xd_power *power;
	struct i915_xd_dc_report report;

	/* Counts the disable for the diagnostics. */
	pwc->dc_off_disable_calls++;

	/* A context without the xd state writes no DC state. */
	power = i915_pw_dc_policy(pwc);
	if (power == NULL)
		return;

	/* Enters the target state; a write that does not read back is logged. */
	(void)drv_i915_xd_power_dc_enter(power, &pwc->xd->io, &report);
	i915_pw_dc_log(pwc, &report);
}

/*
 * Hands the context's DC policy (the allowed states, the target, whether
 * the DMC firmware is loaded) to the xd power wells and returns them, or
 * NULL for a context without the xd state or before the power domains
 * were built.
 */
static struct i915_xd_power *
i915_pw_dc_policy(
	struct i915_pw_ctx *pwc)
{
	struct i915_xd_power *power;

	/* A context without the xd state has no DC states to change. */
	if (pwc == NULL || pwc->xd == NULL)
		return NULL;
	power = &pwc->xd->power;
	if (power->platform == NULL)
		return NULL;

	/* The policy follows the context, which the display core and the DMC load set. */
	drv_i915_xd_power_dc_policy(power, pwc->allowed_dc_mask, pwc->target_dc_state, pwc->dmc_has_payload);

	/* Succeeded: the power wells whose DC state the caller changes. */
	return power;
}

/*
 * Logs what a DC state change met and copies the state last written and
 * the write counts into the context the older code reads.
 */
static void
i915_pw_dc_log(
	struct i915_pw_ctx *pwc,
	const struct i915_xd_dc_report *report)
{
	struct i915_xd_power *power;

	/* A utility pin driving a PWM keeps the display out of DC6. */
	if (report->pwm_pin)
		kern_logf("i915: DC6: utility pin enabled in PWM mode\n");

	/* DC6 was requested already. */
	if (report->dc6_already)
		kern_logf("i915: DC6 already programmed to be enabled\n");

	/* The DMC ignored the last request. */
	if (report->mismatch)
		kern_logf("i915: DC state mismatch (0x%x -> 0x%x)\n", report->expected, report->found);

	/* The state never read back as written. */
	if (report->not_taken)
		kern_logf("i915: Writing dc state to 0x%x failed, now 0x%x\n", report->wanted, report->now);

	/* Copies the state last written and the counts. */
	power = &pwc->xd->power;
	pwc->dc_state = power->dc_state;
	pwc->dc_state_writes = power->dc_writes;
	pwc->dc_state_rewrites = power->dc_rewrites;
}

/*
 * The trace ring of the display core's combo PHY init: the display's own
 * (the display core is a member of the display).
 *
 * The ring is far larger than a kernel thread stack, so it is never an
 * automatic variable; the display-core init and the DC_off enable are
 * serialized (one start at a time, the domains lock), so one ring serves
 * both.
 */
static struct i915_trace *
i915_core_trace(
	struct i915_display_core *dc)
{
	struct i915_display *display;

	/* The display core is the dcore member of its display. */
	display = container_of(dc, struct i915_display, dcore);

	/* Succeeded: reports the display's ring. */
	return &display->dc_trace;
}

/* The same trace ring, reached from the well context (the pwc member of the display). */
static struct i915_trace *
i915_dc_off_trace(
	struct i915_pw_ctx *pwc)
{
	struct i915_display *display;

	/* The well context is the pwc member of its display. */
	display = container_of(pwc, struct i915_display, pwc);

	/* Succeeded: reports the display's ring. */
	return &display->dc_trace;
}

/*
 * The DBUF slices the display has: the device's mask (XE_LPD S1..S4, Tiger
 * Lake S1..S2), or all four when none was set.
 */
static uint8_t
i915_dbuf_slice_mask(
	const struct i915_display_core *dc)
{
	/* A display without a mask is treated as having all four. */
	if (dc->dbuf_slice_mask == 0u)
		return 0xFu;

	/* Succeeded: reports the display's slices. */
	return dc->dbuf_slice_mask;
}

/*
 * Sets the display's DBUF slices to the request through the xd DBUF,
 * under the domains lock, and records the result.
 */
static void
i915_gen9_dbuf_slices_update(
	struct i915_display_core *dc,
	uint8_t req_slices)
{
	uint8_t late;
	unsigned slice;

	/* Powers the slices under the domains lock. */
	mutex_lock(&dc->pd->lock);

	late = drv_i915_xd_dbuf_power(&dc->pwc->xd->io, dc->pwc->xd->choice.platform, req_slices);
	dc->dbuf_enabled_slices = req_slices;

	mutex_unlock(&dc->pd->lock);

	/* Reports a slice that did not follow the request. */
	for (slice = 0u; slice < 4u; slice++) {
		if ((late & (1u << slice)) == 0u)
			continue;
		if ((req_slices & (1u << slice)) != 0u) {
			kern_logf("i915: DBUF slice %u power enable timeout\n", slice);
		} else {
			kern_logf("i915: DBUF slice %u power disable timeout\n", slice);
		}
	}
}

/* Records the first adaptation-layer fault of the display-core init and where it stopped. */
static void
i915_core_fault(
	struct i915_display_core *dc,
	const char *where)
{
	/* Keeps the first fault and its position. */
	if (!dc->fault_stop) {
		dc->fault_stop = 1;
		dc->fault_where = where;
	}

	/* Logs every stop. */
	kern_logf("i915: display_core: stop at %s (adaptation-layer fault)\n", where);
}

/*
 * Brings the display core up through the xd core (icl_display_core_init()):
 * 0, or EIO when an adaptation-layer fault stopped it.
 */
static int
i915_icl_display_core_init(
	struct i915_display_core *dc,
	int resume)
{
	struct i915_xd_core_hooks hooks;
	struct i915_xd_core_input input;
	struct i915_xd *xd;
	struct i915_trace *trace;
	unsigned stop;
	unsigned phys;
	int error;

	/* The DMC reload of a resume is done later on these platforms, not here. */
	UNUSED_PARAMETER(resume);

	/* Starts the trace ring of the combo PHY init. */
	trace = i915_core_trace(dc);
	drv_i915_trace_init(trace);

	/* A display without the xd description has no core sequence. */
	dc->last_child = 1;
	xd = dc->pwc->xd;
	if (xd == NULL || !xd->chosen) {
		i915_core_fault(dc, "xd platform");
		return EIO;
	}

	/* The south display, the stepping and the memory. */
	input.pch_kind = xd->choice.pch_kind;
	input.step = xd->choice.step;
	input.dram_type = i915_core_dram_type(dc->dram_type);
	input.dram_channels = dc->dram_channels;

	/* The core's sequence, with power well 1, the CD clock and the DBUF slices here. */
	i915_core_hooks(dc, &hooks);
	error = drv_i915_xd_core_start(&xd->io, xd->choice.platform, i915_pw_dc_policy(dc->pwc), &input, &hooks, &stop, &phys);

	/* Records and logs the combo PHYs brought up. */
	drv_i915_trace_record(trace, I915_COMBO_TRACE_STAGE, I915_TRACE_ACQUIRE, "intel_combo_phy_init", (uint64_t)__builtin_popcount(phys), 0U);
	kern_logf("i915: combo PHYs: %u initialised (mask 0x%x)\n", (unsigned)__builtin_popcount(phys), phys);

	/* Reports where a fault stopped the start. */
	if (error != 0) {
		dc->last_child = 3 + (int)stop;
		if (stop == I915_XD_CORE_STOP_PHYS) {
			i915_core_fault(dc, "intel_combo_phy_init");
		} else if (stop == I915_XD_CORE_STOP_WELL1) {
			i915_core_fault(dc, "power_well_1_enable");
		} else {
			i915_core_fault(dc, "intel_cdclk_init_hw");
		}
		return EIO;
	}

	/* Succeeded: the display core is up. */
	dc->last_child = 7;
	return 0;
}

/*
 * Takes the display core down through the xd core (icl_display_core_uninit()):
 * the DC states left, the DBUF off, the CDCLK at the bypass with its PLL
 * off, power well 1 off and the combo PHYs down.
 */
static void
i915_icl_display_core_uninit(
	struct i915_display_core *dc)
{
	struct i915_xd_core_hooks hooks;
	struct i915_xd_dc_report report;
	struct i915_xd_power *power;
	struct i915_xd *xd;
	unsigned a_down;

	/* A display without the xd description has nothing to take down. */
	xd = dc->pwc->xd;
	if (xd == NULL || !xd->chosen)
		return;

	/* The core's sequence. */
	i915_core_hooks(dc, &hooks);
	power = i915_pw_dc_policy(dc->pwc);
	drv_i915_xd_core_stop(&xd->io, xd->choice.platform, power, &hooks, &report, &a_down);

	/* Logs the DC states left and the PHYs down. */
	if (power != NULL)
		i915_pw_dc_log(dc->pwc, &report);
	if (a_down != 0U)
		kern_logf("i915: combo PHY A was not up before the uninit\n");
	kern_logf("i915: combo PHYs: uninit\n");
}

/* Converts the display core's memory type into the xd one (0: unknown). */
static unsigned
i915_core_dram_type(
	int dram_type)
{
	/* The four memories the buddy tables know. */
	switch (dram_type) {
	case I915_DRAM_DDR4:
		return I915_XD_DRAM_DDR4;
	case I915_DRAM_DDR5:
		return I915_XD_DRAM_DDR5;
	case I915_DRAM_LPDDR4:
		return I915_XD_DRAM_LPDDR4;
	case I915_DRAM_LPDDR5:
		return I915_XD_DRAM_LPDDR5;
	default:
		break;
	}

	/* Succeeded: a memory the tables do not know. */
	return 0U;
}

/* Fills the xd core's hooks with the display core's parts. */
static void
i915_core_hooks(
	struct i915_display_core *dc,
	struct i915_xd_core_hooks *hooks)
{
	/* The display core is the hooks' context. */
	hooks->well1_on = i915_core_well1_on;
	hooks->well1_off = i915_core_well1_off;
	hooks->cdclk_on = i915_core_cdclk_on;
	hooks->cdclk_off = i915_core_cdclk_off;
	hooks->dbuf = i915_core_dbuf;
	hooks->faulted = i915_core_faulted;
	hooks->context = dc;
}

/* Enables power well 1 under the domains lock: 0, or EIO for a time-base fault. */
static int
i915_core_well1_on(
	void *context)
{
	struct i915_display_core *dc;
	int index;
	int enable_result;

	/* The well. */
	dc = context;
	index = drv_i915_power_well_by_id(dc->pd, I915_SKL_DISP_PW_1);
	if (index < 0)
		return EIO;

	/* Enables it under the domains lock. */
	mutex_lock(&dc->pd->lock);

	enable_result = drv_i915_power_well_enable(&dc->pd->power_wells[index], dc->pwc);

	mutex_unlock(&dc->pd->lock);

	/* Reports a time-base fault of the enable. */
	if (enable_result == EIO)
		return EIO;

	/* Succeeded: the well is on. */
	return 0;
}

/* Disables power well 1 under the domains lock. */
static void
i915_core_well1_off(
	void *context)
{
	struct i915_display_core *dc;
	int index;

	/* The well. */
	dc = context;
	index = drv_i915_power_well_by_id(dc->pd, I915_SKL_DISP_PW_1);
	if (index < 0)
		return;

	/* Disables it under the domains lock. */
	mutex_lock(&dc->pd->lock);

	(void)drv_i915_power_well_disable(&dc->pd->power_wells[index], dc->pwc);

	mutex_unlock(&dc->pd->lock);
}

/* Brings the CD clock up. */
static void
i915_core_cdclk_on(
	void *context)
{
	struct i915_display_core *dc;

	/* The clock's own start. */
	dc = context;
	drv_i915_cdclk_init_hw(dc->cd);
}

/* Takes the CD clock down. */
static void
i915_core_cdclk_off(
	void *context)
{
	struct i915_display_core *dc;

	/* The clock's own stop. */
	dc = context;
	drv_i915_cdclk_uninit_hw(dc->cd);
}

/* Powers the DBUF slices asked for, within the display's. */
static void
i915_core_dbuf(
	void *context,
	uint8_t slices)
{
	struct i915_display_core *dc;

	/* The slices of the display. */
	dc = context;
	i915_gen9_dbuf_slices_update(dc, (uint8_t)(slices & i915_dbuf_slice_mask(dc)));
}

/* Tells whether the register access met a time-base fault. */
static int
i915_core_faulted(
	void *context)
{
	int faulted;

	/* The access layer's fault state. */
	UNUSED_PARAMETER(context);
	faulted = drv_i915_time_base_faulted();
	if (faulted != 0)
		return 1;

	/* Succeeded: no fault. */
	return 0;
}

/* Counts the wells that are on as the driver owns them. */
static unsigned
i915_wells_on(
	struct i915_power_domains *pd,
	struct i915_pw_ctx *pwc)
{
	unsigned index;
	unsigned count;
	int enabled;

	/* Asks every well. */
	count = 0u;
	for (index = 0u; index < pd->num_power_wells; index++) {
		enabled = drv_i915_power_well_is_enabled(&pd->power_wells[index], pwc);
		if (enabled)
			count++;
	}

	/* Succeeded: reports how many are on. */
	return count;
}

/*
 * Brings up the combo PHYs through the xd DDI (those that do not read as
 * up), and records and logs how many it wrote.  A display without the xd
 * description has none to bring up.
 */
static void
i915_combo_phys_up(
	struct i915_pw_ctx *pwc,
	struct i915_trace *trace)
{
	unsigned written;
	unsigned count;
	unsigned phy;

	/* Without the description there is nothing to write. */
	written = 0U;
	if (pwc->xd != NULL && pwc->xd->chosen)
		written = drv_i915_xd_combo_phy_init(&pwc->xd->io, pwc->xd->choice.platform);

	/* Counts the PHYs written. */
	count = 0U;
	for (phy = 0U; phy < 8U; phy++) {
		if ((written & (1U << phy)) != 0U)
			count++;
	}

	/* Records and logs what was programmed. */
	drv_i915_trace_record(trace, I915_COMBO_TRACE_STAGE, I915_TRACE_ACQUIRE, "intel_combo_phy_init", (uint64_t)count, 0U);
	kern_logf("i915: combo PHYs: %u initialised (mask 0x%x)\n", count, written);
}
