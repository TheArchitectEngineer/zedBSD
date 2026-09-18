/*
 * WS031 Linux-parity — the eDP first stage on the real GPU: binds struct
 * parity_dp_env to the parity MMIO / wait / power-domain layers, and the
 * one-shot AUX acquisition test the probe runs when built with
 * PARITY_AUX_TEST.  zedBSD project code.
 */
#include "../../internal.h"
#include <kern/klog.h>
#include <kern/clock.h>
#include <string.h>
#include <errno.h>
#include "../osdep/mmio.h"
#include "../wait.h"
#include "../power_domains.h"
#include "../bios.h"
#include "parity_edp.h"
#include "parity_dp_kernel.h"
#include "dp_fixture_latitude5330.h"

/* dp_compat.h names these two domains by number */
_Static_assert(PARITY_PW_DOMAIN_DISPLAY_CORE == 0, "POWER_DOMAIN_DISPLAY_CORE");
_Static_assert(PARITY_PW_DOMAIN_AUX_A == 53, "POWER_DOMAIN_AUX_A");

static uint32_t k_read32(void *ctx, uint32_t reg)
{
	return osdep_mmio_read32(((struct parity_dp_kernel *)ctx)->mmio, reg);
}

static void k_write32(void *ctx, uint32_t reg, uint32_t value)
{
	osdep_mmio_write32(((struct parity_dp_kernel *)ctx)->mmio, reg, value);
}

static int k_wait_reg(void *ctx, uint32_t reg, uint32_t mask, uint32_t value,
	unsigned fast_us, unsigned slow_ms, uint32_t *out)
{
	struct parity_dp_kernel *k = ctx;
	int rc = parity_wait_reg(k->mmio, reg, mask, value, fast_us, slow_ms, out);

	if (rc == 0)
		return 0;
	if (rc != -ETIMEDOUT) {          /* zedBSD numbering here: a time-base fault, not a timeout */
		k->time_faults++;
		return -PARITY_EDP_EIO;
	}
	k->wait_timeouts++;
	return -PARITY_EDP_ETIMEDOUT;
}

static void k_sleep_us(void *ctx, unsigned us)
{
	struct parity_dp_kernel *k = ctx;

	/* the attaching thread busy-waits: there is no finer sleep than one 10 ms tick to build on */
	if (parity_udelay(us) != 0)
		k->time_faults++;
}

static uint64_t k_now_ms(void *ctx)
{
	struct parity_dp_kernel *k = ctx;
	uint64_t counter = 0, freq = 0;

	if (!kern_rtc_read_counter(&counter, &freq) || freq == 0u) {
		k->time_faults++;
		return k->last_ms;
	}
	k->last_ms = (counter / freq) * 1000u + ((counter % freq) * 1000u) / freq;
	return k->last_ms;
}

static int k_power_get(void *ctx, int domain)
{
	struct parity_dp_kernel *k = ctx;

	return parity_display_power_get(k->pd, (enum parity_power_domain)domain, k->pwc);
}

static void k_power_put(void *ctx, int domain)
{
	struct parity_dp_kernel *k = ctx;

	parity_display_power_put(k->pd, (enum parity_power_domain)domain, k->pwc);
}

void parity_dp_kernel_bind(struct parity_dp_kernel *k, struct parity_dp_env *env)
{
	memset(env, 0, sizeof(*env));
	env->ctx = k;
	env->read32 = k_read32;
	env->write32 = k_write32;
	env->wait_reg = k_wait_reg;
	env->sleep_us = k_sleep_us;
	env->now_ms = k_now_ms;
	env->power_get = k_power_get;
	env->power_put = k_power_put;
}

/* ---- the one-shot real-hardware AUX acquisition ---- */
static void log_bytes(const char *tag, const uint8_t *b, unsigned n)
{
	unsigned i;

	for (i = 0; i + 16u <= n; i += 16u)
		kern_logf("i915: parity AUX-TEST %s +%03x: %02x %02x %02x %02x %02x %02x %02x %02x "
			"%02x %02x %02x %02x %02x %02x %02x %02x\n", tag, i,
			b[i], b[i + 1], b[i + 2], b[i + 3], b[i + 4], b[i + 5], b[i + 6], b[i + 7],
			b[i + 8], b[i + 9], b[i + 10], b[i + 11], b[i + 12], b[i + 13], b[i + 14], b[i + 15]);
}

static void log_pps(const char *when, const struct parity_edp_pps_regs *r)
{
	kern_logf("i915: parity AUX-TEST pps %s: PP_STATUS=0x%08x PP_CONTROL=0x%08x PP_ON_DELAYS=0x%08x "
		"PP_OFF_DELAYS=0x%08x\n", when, r->pp_status, r->pp_control, r->pp_on_delays, r->pp_off_delays);
}

static void cfg_from_panel(struct parity_edp_config *cfg, const struct parity_vbt_encoder *enc,
	const struct parity_vbt_panel *pn, int have_panel)
{
	memset(cfg, 0, sizeof(*cfg));
	cfg->port = enc->port;
	cfg->aux_ch = enc->aux_ch;
	/*
	 * rawclk: only the pre-CNP PP_DIVISOR path consumes it, which this PCH does not
	 * have.  19200 kHz is what the reference reported on the target; the reference's
	 * own readout (cnp_rawclk) is ported with the backlight.
	 */
	cfg->rawclk_khz = 19200u;
	cfg->bl_controller = -1;
	if (have_panel) {
		cfg->t1_t3 = pn->t1_t3; cfg->t8 = pn->t8; cfg->t9 = pn->t9;
		cfg->t10 = pn->t10; cfg->t11_t12 = pn->t11_t12;
		cfg->bl_controller = pn->bl_controller;
	}
	cfg->log_level = 1;
}

int parity_edp_aux_test_run(struct osdep_mmio *mmio, struct parity_power_domains *pd,
	struct parity_pw_ctx *pwc, struct parity_vbt_state *vbt)
{
	static struct parity_dp_kernel k;
	static struct parity_dp_env env;
	static struct parity_edp_result res;
	struct parity_edp_config cfg;
	struct parity_vbt_panel pn;
	const struct parity_vbt_encoder *enc;
	uint8_t digest[32];
	int rc, late_rc = 0, end_rc, have_panel, dpcd_match, edp_match, edid_match, pass;
	unsigned wi, wells_before = 0u, wells_after = 0u;

	memset(&k, 0, sizeof(k));
	k.mmio = mmio; k.pd = pd; k.pwc = pwc;
	parity_dp_kernel_bind(&k, &env);

	if (!vbt->parsed_live || !vbt->vbt_found) {
		kern_logf("i915: parity AUX-TEST not run: no real VBT (source=%d); the eDP child must come "
			"from the VBT, not from the defaults\n", vbt->source);
		return -1;
	}
	enc = parity_vbt_encoder_for_port(&vbt->parsed, 0);
	if (enc == 0 || !enc->supports_edp) {
		kern_logf("i915: parity AUX-TEST not run: the VBT has no eDP child on port A\n");
		return -1;
	}
	have_panel = parity_vbt_init_panel(&vbt->parsed, 0, 0, &pn) == 0;
	cfg_from_panel(&cfg, enc, &pn, have_panel);
	for (wi = 0u; wi < pd->num_power_wells; wi++)
		wells_before += pd->power_wells[wi].refcount;
	kern_logf("i915: parity AUX-TEST begin: vbt_source=%d port=%c aux_ch=%d panel_early=%d type=%d "
		"pps(100us) t1_t3=%u t8=%u t9=%u t10=%u t11_t12=%u controller=%d well_refs=%u\n",
		vbt->source, 'A' + cfg.port, cfg.aux_ch, have_panel, have_panel ? pn.panel_type : -1,
		cfg.t1_t3, cfg.t8, cfg.t9, cfg.t10, cfg.t11_t12, cfg.bl_controller, wells_before);

	rc = parity_edp_begin(&env, &cfg, &res);
	log_pps("before", &res.before);
	log_pps("after intel_pps_init", &res.after_init);
	kern_logf("i915: parity AUX-TEST pps: idx=%d valid=%d delays(ms) up=%d down=%d cycle=%d bl_on=%d bl_off=%d\n",
		res.pps_idx, res.pps_valid, res.delay_power_up_ms, res.delay_power_down_ms,
		res.delay_power_cycle_ms, res.delay_bl_on_ms, res.delay_bl_off_ms);
	kern_logf("i915: parity AUX-TEST acquire: rc=%d stage=%d failed_stage=%d dpcd_ok=%d edp_dpcd_ok=%d "
		"link_cfg_ok=%d edid_ok=%d edid_blocks=%u ext=%u i2c_defers=%u i2c_nacks=%u log_errors=%u "
		"wait_timeouts=%u time_faults=%u elapsed_ms=%llu slept_us=%llu\n",
		rc, res.stage, res.failed_stage, res.dpcd_ok, res.edp_dpcd_ok, res.link_cfg_ok, res.edid_ok,
		res.edid_blocks, res.edid_extensions, res.i2c_defers, res.i2c_nacks, res.log_errors,
		k.wait_timeouts, k.time_faults, (unsigned long long)res.elapsed_ms,
		(unsigned long long)env.slept_us);
	if (rc == 0) {
		log_pps("after acquisition (VDD expected on)", &res.after_acquire);
		kern_logf("i915: parity AUX-TEST ownership after acquisition: vdd_hw=%d vdd_wakeref=%d "
			"worker_pending=%d refs core=%d aux=%d\n", res.vdd_on_hw, res.vdd_wakeref_held,
			res.vdd_work_pending, res.power_refs_core, res.power_refs_aux);
	}
	if (res.dpcd_ok)
		kern_logf("i915: parity AUX-TEST DPCD 000: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x "
			"%02x %02x %02x %02x %02x | eDP 700: %02x %02x %02x (ok=%d) | 100: %02x %02x (ok=%d)\n",
			res.dpcd[0], res.dpcd[1], res.dpcd[2], res.dpcd[3], res.dpcd[4], res.dpcd[5], res.dpcd[6],
			res.dpcd[7], res.dpcd[8], res.dpcd[9], res.dpcd[10], res.dpcd[11], res.dpcd[12],
			res.dpcd[13], res.dpcd[14], res.edp_dpcd[0], res.edp_dpcd[1], res.edp_dpcd[2],
			res.edp_dpcd_ok, res.link_cfg[0], res.link_cfg[1], res.link_cfg_ok);
	if (res.edid_blocks != 0u) {
		log_bytes("EDID", res.edid, 128u * res.edid_blocks);
		parity_sha256(res.edid, 128u * res.edid_blocks, digest);
		kern_logf("i915: parity AUX-TEST EDID sha256=%02x%02x%02x%02x%02x%02x%02x%02x.. mfg=%02x%02x "
			"product=%02x%02x\n", digest[0], digest[1], digest[2], digest[3], digest[4], digest[5],
			digest[6], digest[7], res.edid[8], res.edid[9], res.edid[11], res.edid[10]);
	}

	/* the reference's late step: the EDID-based panel lookup, the final delays, the delayed VDD-off */
	if (rc == 0) {
		struct parity_vbt_panel pl;
		int late_panel = parity_vbt_init_panel(&vbt->parsed, 0, res.edid, &pl) == 0;

		cfg_from_panel(&cfg, enc, &pl, late_panel);
		late_rc = parity_edp_init_late(&cfg, &res);
		kern_logf("i915: parity AUX-TEST late: rc=%d panel_late=%d type=%d delays(ms) up=%d down=%d "
			"cycle=%d | vdd_hw=%d worker_pending=%d\n", late_rc, late_panel,
			late_panel ? pl.panel_type : -1, res.delay_power_up_ms, res.delay_power_down_ms,
			res.delay_power_cycle_ms, res.vdd_on_hw, res.vdd_work_pending);
		log_pps("after intel_pps_init_late", &res.after_acquire);
	}

	/* compare with the identity captured through Linux; the capture is never used as the data */
	dpcd_match = res.dpcd_ok && memcmp(res.dpcd, dp_fixture_dpcd_000, sizeof(res.dpcd)) == 0;
	edp_match = res.edp_dpcd_ok && memcmp(res.edp_dpcd, dp_fixture_dpcd_700, sizeof(res.edp_dpcd)) == 0;
	edid_match = res.edid_ok && res.edid_blocks == 1u && memcmp(res.edid, dp_fixture_edid, 128u) == 0;

	end_rc = parity_edp_end(&res);
	log_pps("after end", &res.after_end);
	for (wi = 0u; wi < pd->num_power_wells; wi++)
		wells_after += pd->power_wells[wi].refcount;
	kern_logf("i915: parity AUX-TEST end: rc=%d vdd_hw=%d vdd_wakeref=%d worker_pending=%d refs core=%d "
		"aux=%d put_underflows=%u get_failures=%u well_refs %u -> %u log_errors=%u\n", end_rc,
		res.vdd_on_hw, res.vdd_wakeref_held, res.vdd_work_pending, res.power_refs_core,
		res.power_refs_aux, res.power_put_underflows, res.power_get_failures, wells_before,
		wells_after, res.log_errors);

	pass = rc == 0 && late_rc == 0 && end_rc == 0 && dpcd_match && edp_match && edid_match &&
		wells_after == wells_before && res.log_errors == 0u && k.time_faults == 0u;
	kern_logf("i915: parity AUX-TEST verdict: %s (acquire=%d late=%d end=%d dpcd_match=%d edp_dpcd_match=%d "
		"edid_match=%d wells_balanced=%d)\n", pass ? "PASS" : "FAIL", rc, late_rc, end_rc, dpcd_match,
		edp_match, edid_match, wells_after == wells_before);
	return pass ? 0 : -1;
}
