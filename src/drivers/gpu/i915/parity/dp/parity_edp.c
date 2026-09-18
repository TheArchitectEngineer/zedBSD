/*
 * WS031 Linux-parity — eDP bring-up, first stage (see parity_edp.h).
 *
 * zedBSD project code.  It owns the objects the reference functions work on
 * (one digital port / intel_dp / connector: a single live eDP), supplies the
 * environment hooks declared in dp_compat.h, and calls the reference functions
 * in the order of intel_edp_init_connector() / intel_edp_init_dpcd():
 *
 *   intel_pps_init()                      PPS pick, delays (BIOS / VBT / spec), registers, VDD adopt
 *   drm_dp_read_dpcd_caps()               every AUX transfer takes the PPS lock, forces VDD on
 *   drm_dp_dpcd_read(DP_EDP_DPCD_REV)       and keeps it on while pps.initializing
 *   EDID over the AUX DDC adapter
 *   intel_pps_init_late()                 final delays; schedules the delayed VDD-off
 *   intel_pps_vdd_off_sync()              the stop path (and the reference's out_vdd_off)
 *
 * Not done here, and said so rather than implied: intel_hpd_enable_detection(),
 * the shared-AUX HPD check, drm_dp_read_desc() / DPCD quirks, PSR / DSC / MSO
 * caps, sink-rate tables, mode construction, backlight setup.
 */
#include "dp_compat.h"
#include "intel_pps_regs.h"

/* the glue entry points of the generated files */
void parity_intel_dp_aux_init(struct intel_dp *intel_dp);
int parity_drm_edid_read(struct i2c_adapter *ddc, u8 *buf, unsigned max_blocks, unsigned *extensions);
ssize_t drm_dp_dpcd_read(struct drm_dp_aux *aux, unsigned int offset, void *buffer, size_t size);
int drm_dp_read_dpcd_caps(struct drm_dp_aux *aux, u8 dpcd[DP_RECEIVER_CAP_SIZE]);

static struct {
	int live;
	struct parity_dp_env *env;
	struct drm_i915_private i915;
	struct intel_digital_port dig_port;
	struct intel_connector connector;
	u64 t0_ms;
} edp;

static int dp_log_level;
static unsigned dp_log_errors;

/* ---- environment hooks (dp_compat.h) ---- */
int parity_dp_log_enabled(int level)
{
	if (level == PARITY_VBT_LOG_ERR)
		dp_log_errors++;
	return level <= dp_log_level;
}

void parity_dp_note(int level, const char *fmt)
{
	if (!parity_dp_log_enabled(level))
		return;
	parity_vbt_emit(level == PARITY_VBT_LOG_ERR ? "i915: parity edp [err] " : "i915: parity edp ");
	parity_vbt_emit(fmt);           /* the format text; see vbt_compat.h on why not the arguments */
}

struct parity_dp_env *parity_dp_env_current(void)
{
	return edp.live ? edp.env : 0;
}

void parity_dp_sleep_us(unsigned us)
{
	struct parity_dp_env *env = parity_dp_env_current();

	if (env == 0)
		return;
	env->sleeps++;
	env->slept_us += us;
	env->sleep_us(env->ctx, us);
}

u64 parity_dp_now_ms(void)
{
	struct parity_dp_env *env = parity_dp_env_current();

	return env != 0 ? env->now_ms(env->ctx) : 0;
}

static int power_slot(int domain)
{
	return domain == POWER_DOMAIN_DISPLAY_CORE ? 0 : 1;
}

intel_wakeref_t parity_dp_power_get(struct drm_i915_private *i915, int domain)
{
	struct parity_dp_env *env = i915->dp_env;

	if (env->power_get(env->ctx, domain) != 0) {
		/* the reference cannot fail here; the transfer that follows will time out and say so */
		env->power_get_failures++;
		parity_vbt_log(PARITY_VBT_LOG_ERR, "display power get failed\n");
		return -1;
	}
	env->power_refs[power_slot(domain)]++;
	return 1;
}

void parity_dp_power_put(struct drm_i915_private *i915, int domain, intel_wakeref_t wakeref)
{
	struct parity_dp_env *env = i915->dp_env;

	if (wakeref <= 0)               /* 0: nothing held; -1: the get had failed */
		return;
	if (env->power_refs[power_slot(domain)] <= 0) {
		env->power_put_underflows++;
		parity_vbt_log(PARITY_VBT_LOG_ERR, "display power put without a reference\n");
		return;
	}
	env->power_refs[power_slot(domain)]--;
	env->power_put(env->ctx, domain);
}

/* ---- helpers ---- */
static void read_pps_regs(struct parity_edp_pps_regs *r)
{
	struct drm_i915_private *dev_priv = &edp.i915;   /* the PP_* macros name `dev_priv` */
	int idx = edp.dig_port.dp.pps.pps_idx;

	if (idx < 0 || idx > 1)
		idx = 0;
	r->pp_status = intel_de_read(dev_priv, PP_STATUS(idx));
	r->pp_control = intel_de_read(dev_priv, PP_CONTROL(idx));
	r->pp_on_delays = intel_de_read(dev_priv, PP_ON_DELAYS(idx));
	r->pp_off_delays = intel_de_read(dev_priv, PP_OFF_DELAYS(idx));
}

static void snapshot_ownership(struct parity_edp_result *res)
{
	struct intel_dp *intel_dp = &edp.dig_port.dp;
	struct drm_i915_private *dev_priv = &edp.i915;
	int idx = intel_dp->pps.pps_idx;

	if (idx < 0 || idx > 1)
		idx = 0;
	res->vdd_wanted = intel_dp->pps.want_panel_vdd;
	res->vdd_on_hw = (intel_de_read(dev_priv, PP_CONTROL(idx)) & EDP_FORCE_VDD) != 0;
	res->vdd_wakeref_held = intel_dp->pps.vdd_wakeref != 0;
	res->vdd_work_pending = intel_dp->pps.panel_vdd_work.pending;
	res->power_refs_core = edp.env->power_refs[0];
	res->power_refs_aux = edp.env->power_refs[1];
	res->power_get_failures = edp.env->power_get_failures;
	res->power_put_underflows = edp.env->power_put_underflows;
	res->i2c_defers = intel_dp->aux.i2c_defer_count;
	res->i2c_nacks = intel_dp->aux.i2c_nack_count;
	res->log_errors = dp_log_errors;
	res->elapsed_ms = parity_dp_now_ms() - edp.t0_ms;
}

static void apply_panel_vbt(const struct parity_edp_config *cfg)
{
	struct intel_vbt_panel_data *vbt = &edp.connector.panel.vbt;

	vbt->edp.pps.t1_t3 = cfg->t1_t3;
	vbt->edp.pps.t8 = cfg->t8;
	vbt->edp.pps.t9 = cfg->t9;
	vbt->edp.pps.t10 = cfg->t10;
	vbt->edp.pps.t11_t12 = cfg->t11_t12;
	vbt->backlight.controller = cfg->bl_controller;
}

static int fail(struct parity_edp_result *res, int stage, int rc)
{
	/* intel_edp_init_connector(): out_vdd_off */
	intel_pps_vdd_off_sync(&edp.dig_port.dp);
	res->failed_stage = stage;
	res->rc = rc;
	snapshot_ownership(res);
	return rc;
}

int parity_edp_begin(struct parity_dp_env *env, const struct parity_edp_config *cfg,
	struct parity_edp_result *res)
{
	struct intel_dp *intel_dp;
	long n;
	int rc;

	if (env == 0 || cfg == 0 || res == 0)
		return -EINVAL;
	memset(res, 0, sizeof(*res));
	if (edp.live)
		return -EBUSY;
	if (cfg->port != PORT_A || cfg->aux_ch < AUX_CH_A || cfg->aux_ch > AUX_CH_C)
		return -EINVAL;             /* combo-PHY eDP only: no Type-C AUX here */

	memset(&edp, 0, sizeof(edp));
	edp.live = 1;
	edp.env = env;
	dp_log_level = cfg->log_level;
	dp_log_errors = 0;
	env->power_refs[0] = env->power_refs[1] = 0;
	env->power_get_failures = env->power_put_underflows = 0;
	env->sleeps = 0;
	env->slept_us = 0;
	edp.t0_ms = env->now_ms(env->ctx);

	edp.i915.dp_env = env;
	edp.i915.display.pps.mmio_base = PCH_PPS_BASE;      /* intel_pps_setup(): HAS_PCH_SPLIT */
	mutex_init(&edp.i915.display.pps.mutex);
	edp.i915.display_runtime.rawclk_freq = cfg->rawclk_khz;
	edp.dig_port.i915 = &edp.i915;
	edp.dig_port.base.base.dev = &edp.i915.drm;
	edp.dig_port.base.base.name = "DDI A/PHY A";
	edp.dig_port.base.port = (enum port)cfg->port;
	edp.dig_port.aux_ch = (enum aux_ch)cfg->aux_ch;
	intel_dp = &edp.dig_port.dp;
	intel_dp->is_edp = true;
	intel_dp->attached_connector = &edp.connector;
	intel_dp->aux.name = "AUX A/DDI A/PHY A";
	intel_dp->aux.drm_dev = &edp.i915.drm;
	edp.connector.panel.vbt.backlight.controller = -1;  /* intel_panel_init_alloc() */
	apply_panel_vbt(cfg);
	parity_intel_dp_aux_init(intel_dp);

	read_pps_regs(&res->before);

	/* ---- intel_pps_init() ---- */
	res->pps_valid = intel_pps_init(intel_dp);
	res->pps_idx = intel_dp->pps.pps_idx;
	read_pps_regs(&res->after_init);
	res->delay_power_up_ms = intel_dp->pps.panel_power_up_delay;
	res->delay_power_down_ms = intel_dp->pps.panel_power_down_delay;
	res->delay_power_cycle_ms = intel_dp->pps.panel_power_cycle_delay;
	res->delay_bl_on_ms = intel_dp->pps.backlight_on_delay;
	res->delay_bl_off_ms = intel_dp->pps.backlight_off_delay;
	if (!res->pps_valid)
		return fail(res, PARITY_EDP_STAGE_PPS_INIT, -ENXIO);    /* "unusable PPS, disabling eDP" */
	res->stage = PARITY_EDP_STAGE_PPS_INIT;

	/* ---- intel_edp_init_dpcd() ---- */
	rc = drm_dp_read_dpcd_caps(&intel_dp->aux, intel_dp->dpcd);
	if (rc != 0)
		return fail(res, PARITY_EDP_STAGE_DPCD, rc);            /* "failed to retrieve link info" */
	memcpy(res->dpcd, intel_dp->dpcd, sizeof(res->dpcd));
	res->dpcd_ok = 1;
	n = drm_dp_dpcd_read(&intel_dp->aux, DP_EDP_DPCD_REV, intel_dp->edp_dpcd, sizeof(intel_dp->edp_dpcd));
	if (n == (long)sizeof(intel_dp->edp_dpcd)) {
		memcpy(res->edp_dpcd, intel_dp->edp_dpcd, sizeof(res->edp_dpcd));
		res->edp_dpcd_ok = 1;
	}
	/* diagnostic only (the reference does not read these here): the link configuration as found */
	n = drm_dp_dpcd_read(&intel_dp->aux, DP_LINK_BW_SET, res->link_cfg, sizeof(res->link_cfg));
	res->link_cfg_ok = n == (long)sizeof(res->link_cfg);
	res->stage = PARITY_EDP_STAGE_DPCD;

	/* ---- drm_edid_read_ddc() ---- */
	rc = parity_drm_edid_read(&intel_dp->aux.ddc, res->edid, PARITY_EDP_MAX_EDID_BLOCKS,
		&res->edid_extensions);
	if (rc < 1)
		return fail(res, PARITY_EDP_STAGE_EDID, rc == 0 ? -EIO : rc);
	res->edid_blocks = (unsigned)rc;
	res->edid_ok = res->edid_blocks == 1u + res->edid_extensions ||
		res->edid_blocks == PARITY_EDP_MAX_EDID_BLOCKS;
	res->stage = PARITY_EDP_STAGE_EDID;

	read_pps_regs(&res->after_acquire);
	res->stage = PARITY_EDP_STAGE_ACQUIRED;
	snapshot_ownership(res);
	return 0;
}

int parity_edp_init_late(const struct parity_edp_config *final_cfg, struct parity_edp_result *res)
{
	struct intel_dp *intel_dp = &edp.dig_port.dp;

	if (!edp.live || final_cfg == 0 || res == 0 || res->stage != PARITY_EDP_STAGE_ACQUIRED)
		return -EINVAL;
	apply_panel_vbt(final_cfg);
	intel_pps_init_late(intel_dp);
	res->pps_idx = intel_dp->pps.pps_idx;
	res->delay_power_up_ms = intel_dp->pps.panel_power_up_delay;
	res->delay_power_down_ms = intel_dp->pps.panel_power_down_delay;
	res->delay_power_cycle_ms = intel_dp->pps.panel_power_cycle_delay;
	res->delay_bl_on_ms = intel_dp->pps.backlight_on_delay;
	res->delay_bl_off_ms = intel_dp->pps.backlight_off_delay;
	read_pps_regs(&res->after_acquire);
	res->stage = PARITY_EDP_STAGE_LATE;
	snapshot_ownership(res);
	return 0;
}

int parity_edp_run_due_work(struct parity_edp_result *res)
{
	struct delayed_work *dw = &edp.dig_port.dp.pps.panel_vdd_work;

	if (!edp.live || !dw->pending || parity_dp_now_ms() < dw->due_ms)
		return 0;
	dw->pending = 0;
	dw->ran++;
	dw->fn(&dw->work);
	if (res != 0)
		snapshot_ownership(res);
	return 1;
}

int parity_edp_end(struct parity_edp_result *res)
{
	struct intel_dp *intel_dp = &edp.dig_port.dp;
	int clean;

	if (!edp.live)
		return -EINVAL;
	/* intel_dp_encoder_flush_work() / shutdown: cancel the worker, force VDD off, return the reference */
	intel_pps_vdd_off_sync(intel_dp);

	clean = !intel_dp->pps.panel_vdd_work.pending && intel_dp->pps.vdd_wakeref == 0 &&
		edp.env->power_refs[0] == 0 && edp.env->power_refs[1] == 0 &&
		!edp.i915.display.pps.mutex.held && !intel_dp->aux.hw_mutex.held &&
		edp.env->power_put_underflows == 0;
	if (res != 0) {
		read_pps_regs(&res->after_end);
		snapshot_ownership(res);
		if (res->vdd_on_hw)
			clean = 0;
		res->stage = PARITY_EDP_STAGE_ENDED;
	}
	edp.live = 0;
	edp.env = 0;
	return clean ? 0 : -EBUSY;
}

long parity_edp_dpcd_read(unsigned offset, uint8_t *buf, size_t size)
{
	if (!edp.live)
		return -EINVAL;
	return drm_dp_dpcd_read(&edp.dig_port.dp.aux, offset, buf, size);
}
