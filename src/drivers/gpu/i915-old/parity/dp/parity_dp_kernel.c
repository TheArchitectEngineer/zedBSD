/*
 * WS031 Linux-parity — the eDP first stage on the real kernel and GPU: the env's
 * backend (MMIO, waits, tick-driven sleeps, real mutexes, timer + worker threads,
 * power domains with asynchronous put), the resident panel device, and the
 * diagnostics run by PARITY_AUX_TEST.  zedBSD project code.
 */
#include "../../internal.h"
#include <kern/klog.h>
#include <kern/clock.h>
#include <kern/lock.h>
#include <kern/waitq.h>
#include <kern/sched.h>
#include <string.h>
#include <errno.h>
#include "../osdep/mmio.h"
#include "../wait.h"
#include "../power_domains.h"
#include "../bios.h"
#include "../cdclk.h"
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

static int now_us(struct parity_dp_kernel *k, uint64_t *us)
{
	uint64_t counter = 0, freq = 0;

	if (!kern_rtc_read_counter(&counter, &freq) || freq == 0u) {
		k->time_faults++;
		return 0;
	}
	*us = (counter / freq) * 1000000u + ((counter % freq) * 1000000u) / freq;
	return 1;
}

/*
 * msleep / usleep_range: every sleep the reference text asks for is an ORDINARY
 * (sleepable-context) sleep, and all of them go to the kernel's accepted backend,
 * kern_usleep_range(): one absolute deadline computed at entry, the existing 10 ms
 * tick + wait queue, the same deadline re-checked on every wake, a late wake allowed.
 * There is no busy remainder: a 500 us retry interval simply waits for the next tick.
 * The short counter-based delay (parity_udelay) and the atomic register polls stay
 * where they were -- in parity_wait_reg()'s fast stage -- and are not reached from here.
 * The requested values and retry counts come unchanged from the reference text.
 */
void parity_dp_kernel_sleep_us(struct parity_dp_kernel *k, unsigned us)
{
	uint64_t before = 0, after = 0;

	if (us == 0u)
		return;
	(void)now_us(k, &before);
	kern_usleep_range(us, us);
	(void)__atomic_add_fetch(&k->tick_sleeps, 1u, __ATOMIC_SEQ_CST);
	if (now_us(k, &after) && after >= before) {
		(void)__atomic_add_fetch(&k->tick_slept_us, after - before, __ATOMIC_SEQ_CST);
		if (after - before < us)
			k->time_faults++;       /* a sleep that returned early is a time-base fault, not a success */
	}
}

static void k_sleep_us(void *ctx, unsigned us)
{
	parity_dp_kernel_sleep_us(ctx, us);
}

static uint64_t k_now_ms(void *ctx)
{
	struct parity_dp_kernel *k = ctx;
	uint64_t us = 0;

	if (now_us(k, &us))
		k->last_ms = us / 1000u;
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

static void k_power_put_async(void *ctx, int domain)
{
	struct parity_dp_kernel *k = ctx;

	parity_display_power_put_async(k->pd, (enum parity_power_domain)domain, k->pwc, -1);
}

/* ---- locks and delayed work ---- */
static void k_lock(void *ctx, int which)
{
	mutex_lock(&((struct parity_dp_kernel *)ctx)->locks[which]);
}

static void k_unlock(void *ctx, int which)
{
	mutex_unlock(&((struct parity_dp_kernel *)ctx)->locks[which]);
}

static uint64_t sync_deadline(void)
{
	uint64_t d = 0;

	/* a bound for waiting on a running body (its longest wait is the 5 s panel-status timeout) */
	(void)kern_deadline_after(sched_ticks(), 10u * KERN_CLOCK_HZ, &d);
	return d;
}

static int k_delayed_queue(void *ctx, int which, unsigned delay_ms)
{
	struct parity_dp_kernel *k = ctx;

	(void)which;
	return parity_kdelayed_queue(&k->tq, &k->vdd_off_work, delay_ms);
}

static int k_delayed_cancel(void *ctx, int which, int sync)
{
	struct parity_dp_kernel *k = ctx;

	(void)which;
	return sync ? parity_kdelayed_cancel_sync(&k->tq, &k->vdd_off_work, sync_deadline()) :
		parity_kdelayed_cancel(&k->tq, &k->vdd_off_work);
}

static int k_delayed_pending(void *ctx, int which)
{
	struct parity_dp_kernel *k = ctx;

	(void)which;
	return parity_kdelayed_pending(&k->tq, &k->vdd_off_work);
}

static void vdd_off_body(void *arg)
{
	(void)arg;
	parity_edp_work_run(PARITY_DP_WORK_VDD_OFF);
}

static void async_put_body(void *arg)
{
	parity_display_power_async_work(((struct parity_dp_kernel *)arg)->pd);
}

static int pd_async_queue(void *ctx, int delay_ms)
{
	struct parity_dp_kernel *k = ctx;

	return parity_kdelayed_queue(&k->tq, &k->async_put_work, (unsigned)delay_ms);
}

static int pd_async_cancel(void *ctx, int sync)
{
	struct parity_dp_kernel *k = ctx;

	return sync ? parity_kdelayed_cancel_sync(&k->tq, &k->async_put_work, sync_deadline()) :
		parity_kdelayed_cancel(&k->tq, &k->async_put_work);
}

static const struct parity_pw_async_ops pd_async_ops = { pd_async_queue, pd_async_cancel };

int parity_dp_kernel_sync_start(struct parity_dp_kernel *k)
{
	int rc;

	(void)mutex_init(&k->locks[PARITY_DP_LOCK_PPS], LOCK_RANK_DEVICE, "parity-pps");
	(void)mutex_init(&k->locks[PARITY_DP_LOCK_AUX], LOCK_RANK_DEVICE, "parity-dp-aux");
	rc = parity_kworkqueue_create(&k->wq, "parity-display-wq");
	if (rc != 0)
		return rc;
	rc = parity_ktimerq_create(&k->tq, &k->wq, "parity-display-timer");
	if (rc != 0) {
		parity_kworkqueue_destroy(&k->wq);
		return rc;
	}
	parity_kdelayed_init(&k->vdd_off_work, vdd_off_body, k);
	parity_kdelayed_init(&k->async_put_work, async_put_body, k);
	k->sync_started = 1;
	return 0;
}

void parity_dp_kernel_sync_stop(struct parity_dp_kernel *k)
{
	if (!k->sync_started)
		return;
	(void)parity_kdelayed_cancel_sync(&k->tq, &k->vdd_off_work, sync_deadline());
	(void)parity_kdelayed_cancel_sync(&k->tq, &k->async_put_work, sync_deadline());
	parity_ktimerq_destroy(&k->tq);
	parity_kworkqueue_destroy(&k->wq);
	k->sync_started = 0;
}

void parity_dp_kernel_bind_sync(struct parity_dp_kernel *k, struct parity_dp_env *env)
{
	env->ctx = k;
	env->lock = k_lock;
	env->unlock = k_unlock;
	env->delayed_queue = k_delayed_queue;
	env->delayed_cancel = k_delayed_cancel;
	env->delayed_pending = k_delayed_pending;
}

void parity_dp_kernel_bind(struct parity_dp_kernel *k, struct parity_dp_env *env)
{
	memset(env, 0, sizeof(*env));
	parity_dp_kernel_bind_sync(k, env);
	env->read32 = k_read32;
	env->write32 = k_write32;
	env->wait_reg = k_wait_reg;
	env->sleep_us = k_sleep_us;
	env->now_ms = k_now_ms;
	env->power_get = k_power_get;
	env->power_put = k_power_put;
	env->power_put_async = k_power_put_async;
}

/* ---- the resident panel device ---- */
static void log_bytes(const char *tag, const uint8_t *b, unsigned n)
{
	unsigned i;

	for (i = 0; i + 16u <= n; i += 16u)
		kern_logf("i915: parity edp %s +%03x: %02x %02x %02x %02x %02x %02x %02x %02x "
			"%02x %02x %02x %02x %02x %02x %02x %02x\n", tag, i,
			b[i], b[i + 1], b[i + 2], b[i + 3], b[i + 4], b[i + 5], b[i + 6], b[i + 7],
			b[i + 8], b[i + 9], b[i + 10], b[i + 11], b[i + 12], b[i + 13], b[i + 14], b[i + 15]);
}

static void log_pps(const char *when, const struct parity_edp_pps_regs *r)
{
	kern_logf("i915: parity edp pps %s: PP_STATUS=0x%08x PP_CONTROL=0x%08x PP_ON_DELAYS=0x%08x "
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

void parity_edp_device_prepare(struct parity_edp_device *dev, struct osdep_mmio *mmio,
	struct parity_power_domains *pd, struct parity_pw_ctx *pwc, struct parity_vbt_state *vbt)
{
	memset(dev, 0, sizeof(*dev));
	dev->k.mmio = mmio;
	dev->k.pd = pd;
	dev->k.pwc = pwc;
	dev->vbt = vbt;
}

static unsigned well_refs(struct parity_power_domains *pd)
{
	unsigned wi, n = 0u;

	for (wi = 0u; wi < pd->num_power_wells; wi++)
		n += pd->power_wells[wi].refcount;
	return n;
}

int parity_edp_device_init_connector(void *ctx, int port)
{
	struct parity_edp_device *dev = ctx;
	struct parity_edp_result *res = &dev->res;
	const struct parity_vbt_encoder *enc;
	struct parity_vbt_panel pn;
	uint8_t digest[32];
	int rc, have_panel;

	if (dev == 0 || dev->vbt == 0 || !dev->vbt->parsed_live || !dev->vbt->vbt_found)
		return 1;       /* no real VBT: the defaults carry no eDP child */
	enc = parity_vbt_encoder_for_port(&dev->vbt->parsed, port);
	if (enc == 0 || !enc->supports_edp)
		return 1;
	if (dev->connector_live)
		return -PARITY_EDP_EBUSY;

	/* locks and threads exist only on a machine that has the panel */
	if (!dev->started) {
		rc = parity_dp_kernel_sync_start(&dev->k);
		if (rc != 0) {
			kern_logf("i915: parity edp: worker/timer threads could not start rc=%d\n", rc);
			return -PARITY_EDP_EIO;
		}
		parity_display_power_async_bind(dev->k.pd, &pd_async_ops, &dev->k, dev->k.pwc);
		parity_dp_kernel_bind(&dev->k, &dev->env);
		dev->started = 1;
	}

	have_panel = parity_vbt_init_panel(&dev->vbt->parsed, port, 0, &pn) == 0;
	cfg_from_panel(&dev->cfg, enc, &pn, have_panel);
	kern_logf("i915: parity edp init_connector (in setup_outputs): vbt_source=%d port=%c aux_ch=%d "
		"panel_early=%d type=%d pps(100us) t1_t3=%u t8=%u t9=%u t10=%u t11_t12=%u controller=%d well_refs=%u\n",
		dev->vbt->source, 'A' + dev->cfg.port, dev->cfg.aux_ch, have_panel, have_panel ? pn.panel_type : -1,
		dev->cfg.t1_t3, dev->cfg.t8, dev->cfg.t9, dev->cfg.t10, dev->cfg.t11_t12, dev->cfg.bl_controller,
		well_refs(dev->k.pd));

	rc = parity_edp_begin(&dev->env, &dev->cfg, res);
	dev->init_rc = rc;
	log_pps("before", &res->before);
	log_pps("after intel_pps_init", &res->after_init);
	kern_logf("i915: parity edp pps: idx=%d valid=%d delays(ms) up=%d down=%d cycle=%d bl_on=%d bl_off=%d\n",
		res->pps_idx, res->pps_valid, res->delay_power_up_ms, res->delay_power_down_ms,
		res->delay_power_cycle_ms, res->delay_bl_on_ms, res->delay_bl_off_ms);
	kern_logf("i915: parity edp acquire: rc=%d stage=%d failed_stage=%d dpcd_ok=%d edp_dpcd_ok=%d "
		"link_cfg_ok=%d edid_ok=%d edid_blocks=%u ext=%u i2c_defers=%u i2c_nacks=%u log_errors=%u "
		"wait_timeouts=%u time_faults=%u elapsed_ms=%llu | sleeps: tick=%u (%llu us) short=%u (%llu us)\n",
		rc, res->stage, res->failed_stage, res->dpcd_ok, res->edp_dpcd_ok, res->link_cfg_ok, res->edid_ok,
		res->edid_blocks, res->edid_extensions, res->i2c_defers, res->i2c_nacks, res->log_errors,
		dev->k.wait_timeouts, dev->k.time_faults, (unsigned long long)res->elapsed_ms,
		dev->k.tick_sleeps, (unsigned long long)dev->k.tick_slept_us,
		dev->k.busy_sleeps, (unsigned long long)dev->k.busy_slept_us);
	if (res->dpcd_ok)
		kern_logf("i915: parity edp DPCD 000: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x "
			"%02x %02x %02x %02x %02x | eDP 700: %02x %02x %02x (ok=%d) | 100: %02x %02x (ok=%d)\n",
			res->dpcd[0], res->dpcd[1], res->dpcd[2], res->dpcd[3], res->dpcd[4], res->dpcd[5],
			res->dpcd[6], res->dpcd[7], res->dpcd[8], res->dpcd[9], res->dpcd[10], res->dpcd[11],
			res->dpcd[12], res->dpcd[13], res->dpcd[14], res->edp_dpcd[0], res->edp_dpcd[1],
			res->edp_dpcd[2], res->edp_dpcd_ok, res->link_cfg[0], res->link_cfg[1], res->link_cfg_ok);
	if (res->edid_blocks != 0u) {
		log_bytes("EDID", res->edid, 128u * res->edid_blocks);
		parity_sha256(res->edid, 128u * res->edid_blocks, digest);
		kern_logf("i915: parity edp EDID sha256=%02x%02x%02x%02x%02x%02x%02x%02x.. mfg=%02x%02x "
			"product=%02x%02x\n", digest[0], digest[1], digest[2], digest[3], digest[4], digest[5],
			digest[6], digest[7], res->edid[8], res->edid[9], res->edid[11], res->edid[10]);
	}
	if (rc != 0) {
		/* the reference's out_vdd_off already ran; release the object, keep the threads for fini */
		(void)parity_edp_end(res);
		kern_logf("i915: parity edp init_connector FAILED rc=%d: eDP disabled, VDD off, refs core=%d aux=%d\n",
			rc, res->power_refs_core, res->power_refs_aux);
		return rc;
	}

	/* the reference's late step: EDID-based panel lookup, final delays, delayed VDD-off reserved */
	{
		struct parity_vbt_panel pl;
		int late_panel = parity_vbt_init_panel(&dev->vbt->parsed, port, res->edid, &pl) == 0;

		cfg_from_panel(&dev->cfg, enc, &pl, late_panel);
		dev->vbt_bpp = late_panel ? pl.bpp : 0;
		dev->late_rc = parity_edp_init_late(&dev->cfg, res);
		log_pps("after intel_pps_init_late", &res->after_acquire);
		kern_logf("i915: parity edp late: rc=%d panel_late=%d type=%d delays(ms) up=%d down=%d cycle=%d | "
			"KEPT: vdd_hw=%d vdd_wakeref=%d off_reserved=%d (in %d ms) refs core=%d aux=%d well_refs=%u\n",
			dev->late_rc, late_panel, late_panel ? pl.panel_type : -1, res->delay_power_up_ms,
			res->delay_power_down_ms, res->delay_power_cycle_ms, res->vdd_on_hw, res->vdd_wakeref_held,
			res->vdd_work_pending, res->delay_power_cycle_ms * 5, res->power_refs_core,
			res->power_refs_aux, well_refs(dev->k.pd));
	}
	dev->connector_live = 1;

	/*
	 * LCD-A, first slice: the values the display hardware will be programmed with, from what
	 * was just read over AUX plus the VBT colour depth.  The PLL reference is the CDCLK
	 * readout, as in icl_update_dpll_ref_clks().  Calculation only -- nothing is written.
	 */
	{
		int ref = dev->k.pwc != 0 && dev->k.pwc->cd != 0 ? (int)dev->k.pwc->cd->hw.ref : 0;

		dev->lcd_rc = parity_lcd_compute(res->edid, res->dpcd, res->edp_dpcd, dev->vbt_bpp, ref, &dev->lcd);
		kern_logf("i915: parity LCD-A rc=%d mode %ux%u clock=%d kHz h %u/%u/%u/%u v %u/%u/%u/%u sync %c%c bpc=%d | "
			"link %d kHz x%d (sink max %d x%d, use_max_params=%d) bpp=%d need=%d have=%d kBps | TU=%u data M/N 0x%x/0x%x "
			"link M/N %u/%u | DPLL ref=%d kHz CFGCR0=0x%08x CFGCR1=0x%08x DIV0=0x%x | notes=%u\n",
			dev->lcd_rc, dev->lcd.mode.hdisplay, dev->lcd.mode.vdisplay, dev->lcd.mode.clock_khz,
			dev->lcd.mode.hdisplay, dev->lcd.mode.hsync_start, dev->lcd.mode.hsync_end, dev->lcd.mode.htotal,
			dev->lcd.mode.vdisplay, dev->lcd.mode.vsync_start, dev->lcd.mode.vsync_end, dev->lcd.mode.vtotal,
			dev->lcd.mode.hsync_positive ? '+' : '-', dev->lcd.mode.vsync_positive ? '+' : '-', dev->lcd.mode.edid_bpc,
			dev->lcd.link.rate_khz, dev->lcd.link.lanes, dev->lcd.link.sink_max_rate_khz, dev->lcd.link.sink_max_lanes,
			dev->lcd.link.use_max_params, dev->lcd.link.bpp, dev->lcd.link.required_kbps, dev->lcd.link.available_kbps,
			dev->lcd.link.tu, dev->lcd.link.data_m, dev->lcd.link.data_n, dev->lcd.link.link_m, dev->lcd.link.link_n,
			dev->lcd.pll.ref_khz, dev->lcd.pll.cfgcr0, dev->lcd.pll.cfgcr1, dev->lcd.pll.div0, dev->lcd.notes);
		/* eDP on this machine: pipe A through TRANSCODER_A (ADL-P has no TRANSCODER_EDP); full-screen source */
		dev->lcd_words_rc = dev->lcd_rc == 0 ? parity_lcd_emit_transcoder(&dev->lcd, 0, 0,
			dev->lcd.mode.hdisplay, dev->lcd.mode.vdisplay, &dev->lcd_words) : -1;
		/*
		 * saved_port_bits as intel_ddi_init() forms it: the port's DDI_BUF_CTL readout masked with DDI_BUF_PORT_REVERSAL
		 * (bit 16).  A READ of the port register only; nothing is written.  eDP here = port A (0x64000).
		 */
		dev->ddi_buf_ctl_readout = dev->env.read32(dev->env.ctx, 0x64000u);
		dev->lcd_cpu_words_rc = dev->lcd_rc == 0 ? parity_lcd_emit_cpu_transcoder(&dev->lcd, 0, 0, &dev->lcd_cpu_words) : -1;
		dev->lcd_ddi_words_rc = dev->lcd_rc == 0 ? parity_lcd_emit_ddi(&dev->lcd, 0, 0, 0, dev->ddi_buf_ctl_readout & 0x00010000u,
			&dev->lcd_ddi_words, &dev->lcd_ddi_buf_ctl) : -1;
		if (dev->lcd_cpu_words_rc == 0 && dev->lcd_ddi_words_rc == 0) {
			unsigned wi;

			for (wi = 12u; wi < dev->lcd_cpu_words.n; wi++)
				kern_logf("i915: parity LCD-A cpu-transcoder[%2u] %s 0x%05x = 0x%08x clear=0x%08x (computed; NOT written)\n", wi,
					dev->lcd_cpu_words.w[wi].rmw ? "rmw  " : "write", dev->lcd_cpu_words.w[wi].reg, dev->lcd_cpu_words.w[wi].value,
					dev->lcd_cpu_words.w[wi].clear);
			for (wi = 0u; wi < dev->lcd_ddi_words.n; wi++)
				kern_logf("i915: parity LCD-A ddi[%u] write 0x%05x = 0x%08x (computed; NOT written)\n", wi,
					dev->lcd_ddi_words.w[wi].reg, dev->lcd_ddi_words.w[wi].value);
			kern_logf("i915: parity LCD-A ddi: DDI_BUF_CTL value=0x%08x (enable bit comes with link training) | DDI_BUF_CTL_A readout now=0x%08x\n",
				dev->lcd_ddi_buf_ctl, dev->ddi_buf_ctl_readout);
		}
		if (dev->lcd_words_rc == 0) {
			unsigned wi;

			for (wi = 0u; wi < dev->lcd_words.n; wi++)
				kern_logf("i915: parity LCD-A word[%2u] 0x%05x = 0x%08x (computed; NOT written)\n", wi,
					dev->lcd_words.w[wi].reg, dev->lcd_words.w[wi].value);
		}
	}
	return 0;
}

void parity_edp_device_fini(struct parity_edp_device *dev)
{
	struct parity_power_domains *pd;
	int end_rc = 0;

	if (dev == 0 || !dev->started)
		return;
	pd = dev->k.pd;
	if (dev->connector_live) {
		/* cancel_delayed_work_sync() happens inside, BEFORE the PPS lock is taken */
		end_rc = parity_edp_end(&dev->res);
		dev->connector_live = 0;
	}
	/* intel_display_power_flush_work_sync(): nothing parked, no body running */
	parity_display_power_flush_work_sync(pd);
	parity_display_power_async_bind(pd, 0, 0, 0);
	kern_logf("i915: parity edp fini: end_rc=%d vdd_hw=%d vdd_wakeref=%d off_reserved=%d refs core=%d aux=%d "
		"lock_errors=%u | vdd-off work armed=%u fired=%u ran=%d cancelled(timer/queue)=%u/%u | "
		"async put: puts=%u parked=%u grabs=%u work_runs=%u released=%u flushes=%u state_errors=%u "
		"use_count_errors=%u | well_refs=%u\n",
		end_rc, dev->res.vdd_on_hw, dev->res.vdd_wakeref_held, dev->res.vdd_work_pending,
		dev->res.power_refs_core, dev->res.power_refs_aux, dev->env.lock_errors,
		dev->k.vdd_off_work.armed_count, dev->k.vdd_off_work.fired_count, dev->k.vdd_off_work.work.ran_count,
		dev->k.vdd_off_work.cancelled_armed, dev->k.vdd_off_work.cancelled_pending,
		pd->async_puts, pd->async_parked, pd->async_grabs, pd->async_work_runs, pd->async_released,
		pd->async_flushes, pd->async_state_errors, pd->use_count_errors, well_refs(pd));
	parity_dp_kernel_sync_stop(&dev->k);
	dev->started = 0;
}

/* ---- diagnostics on the resident panel ---- */
static int vdd_is_on(struct parity_edp_device *dev)
{
	parity_edp_snapshot(&dev->res);
	return dev->res.vdd_on_hw;
}

/* waits (yielding) until VDD reads off, at most `limit_ms`; returns the ms waited or -1 */
static int wait_vdd_off(struct parity_edp_device *dev, unsigned limit_ms)
{
	unsigned waited = 0u;

	while (vdd_is_on(dev)) {
		if (waited >= limit_ms)
			return -1;
		parity_dp_kernel_sleep_us(&dev->k, 50000u);
		waited += 50u;
	}
	return (int)waited;
}

int parity_edp_aux_test_run(struct parity_edp_device *dev)
{
	struct parity_edp_result *res = &dev->res;
	uint8_t b[2];
	unsigned ran0, ran1, off_delay_ms;
	int dpcd_match, edp_match, edid_match, auto_off_ms, race_kept, race_off_ms, lcd_match, pass;
	long n;

	if (!dev->connector_live) {
		kern_logf("i915: parity AUX-TEST verdict: FAIL (no resident eDP: init_rc=%d; a real VBT with an "
			"eDP child is required)\n", dev->init_rc);
		return -1;
	}
	/* 1. what the normal initialisation kept, against the capture made through Linux */
	dpcd_match = res->dpcd_ok && memcmp(res->dpcd, dp_fixture_dpcd_000, sizeof(res->dpcd)) == 0;
	edp_match = res->edp_dpcd_ok && memcmp(res->edp_dpcd, dp_fixture_dpcd_700, sizeof(res->edp_dpcd)) == 0;
	edid_match = res->edid_ok && res->edid_blocks == 1u && memcmp(res->edid, dp_fixture_edid, 128u) == 0;
	off_delay_ms = (unsigned)res->delay_power_cycle_ms * 5u;

	/*
	 * LCD-A against what Linux programmed on this machine (plan/ws031/display-ref/: transcoder A
	 * timings, PIPE_DATA/LINK_M1/N1 = 0x7e4b17e4 / 0x800000 / 273406 / 524288, DPLL0 CFGCR0/1 =
	 * 0x00e001a5 / 0x88, DDI A x2 HBR 6 bpc).
	 */
	lcd_match = dev->lcd_rc == 0 && dev->lcd.mode.clock_khz == 140800 &&
		dev->lcd.mode.hdisplay == 1920u && dev->lcd.mode.hsync_start == 1936u && dev->lcd.mode.hsync_end == 1952u &&
		dev->lcd.mode.htotal == 2080u && dev->lcd.mode.vdisplay == 1080u && dev->lcd.mode.vsync_start == 1083u &&
		dev->lcd.mode.vsync_end == 1097u && dev->lcd.mode.vtotal == 1128u && dev->lcd.link.rate_khz == 270000 &&
		dev->lcd.link.lanes == 2 && dev->lcd.link.bpp == 18 && dev->lcd.link.tu == 64u &&
		dev->lcd.link.data_m == 0x4b17e4u && dev->lcd.link.data_n == 0x800000u && dev->lcd.link.link_m == 273406u &&
		dev->lcd.link.link_n == 524288u && dev->lcd.pll.cfgcr0 == 0x00e001a5u && dev->lcd.pll.cfgcr1 == 0x88u;
	{
		/* Linux's register dump on this machine (display-ref/regs-selected.txt), in the reference's write order */
		static const struct { uint32_t reg, value; } want[13] = {
			{ 0x60030u, 0x7e4b17e4u }, { 0x60034u, 0x00800000u }, { 0x60040u, 0x00042bfeu }, { 0x60044u, 0x00080000u },
			{ 0x6007cu, 0x00000000u }, { 0x60028u, 0x00000000u }, { 0x60000u, 0x081f077fu }, { 0x60004u, 0x081f077fu },
			{ 0x60008u, 0x079f078fu }, { 0x6000cu, 0x04670437u }, { 0x60010u, 0x04670000u }, { 0x60014u, 0x0448043au },
			{ 0x6001cu, 0x077f0437u },
		};
		unsigned wi;

		if (dev->lcd_words_rc != 0 || dev->lcd_words.n != 13u)
			lcd_match = 0;
		{
			/* Linux's dump: PIPE_DDI_FUNC_CTL_A 0x8a210002, DDI_BUF_CTL_A 0x80000002 (= the value + the enable bit) */
			uint32_t func = 0u;

			if (dev->lcd_cpu_words_rc != 0 || dev->lcd_cpu_words.n != 17u || dev->lcd_ddi_words_rc != 0 ||
			    parity_lcd_words_find(&dev->lcd_ddi_words, 0x60400u, &func) != 1u || func != 0x8a210002u ||
			    (dev->lcd_ddi_buf_ctl | 0x80000000u) != 0x80000002u)
				lcd_match = 0;
		}
		for (wi = 0u; wi < 13u && lcd_match; wi++)
			if (dev->lcd_words.w[wi].reg != want[wi].reg || dev->lcd_words.w[wi].value != want[wi].value)
				lcd_match = 0;
	}

	/* 2. the delayed VDD-off runs by itself: nobody drives the worker here */
	ran0 = (unsigned)dev->k.vdd_off_work.work.ran_count;
	auto_off_ms = wait_vdd_off(dev, off_delay_ms + 2000u);
	parity_edp_snapshot(res);
	kern_logf("i915: parity AUX-TEST auto-off: reserved_ms=%u waited_here_ms=%d vdd_hw=%d vdd_wakeref=%d "
		"worker_ran=%u->%d timer_fired=%u refs core=%d aux=%d well_refs=%u\n", off_delay_ms, auto_off_ms,
		res->vdd_on_hw, res->vdd_wakeref_held, ran0, dev->k.vdd_off_work.work.ran_count,
		dev->k.vdd_off_work.fired_count, res->power_refs_core, res->power_refs_aux, well_refs(dev->k.pd));

	/* 3. re-acquisition before the deadline: the old reservation must not drop the VDD in use */
	n = parity_edp_dpcd_read(0x000u, b, 2u);                  /* VDD on again (T12 + power-up waits), off reserved */
	ran1 = (unsigned)dev->k.vdd_off_work.work.ran_count;
	parity_dp_kernel_sleep_us(&dev->k, (off_delay_ms / 2u) * 1000u);
	n = n == 2 ? parity_edp_dpcd_read(0x000u, b, 2u) : n;     /* cancels the reservation, reserves anew */
	parity_dp_kernel_sleep_us(&dev->k, (off_delay_ms / 2u + 300u) * 1000u);   /* past the OLD deadline */
	race_kept = vdd_is_on(dev) && (unsigned)dev->k.vdd_off_work.work.ran_count == ran1;
	race_off_ms = wait_vdd_off(dev, off_delay_ms + 2000u);    /* the new reservation then fires by itself */
	parity_edp_snapshot(res);
	kern_logf("i915: parity AUX-TEST re-acquire: reads_ok=%d dpcd=%02x %02x kept_past_old_deadline=%d "
		"new_off_after_ms=%d cancelled(timer/queue)=%u/%u worker_ran=%d vdd_hw=%d refs core=%d aux=%d\n",
		n == 2, b[0], b[1], race_kept, race_off_ms, dev->k.vdd_off_work.cancelled_armed,
		dev->k.vdd_off_work.cancelled_pending, dev->k.vdd_off_work.work.ran_count, res->vdd_on_hw,
		res->power_refs_core, res->power_refs_aux);

	/* 4. leave a reservation pending: the stop path must cancel it synchronously and force VDD off */
	n = n == 2 ? parity_edp_dpcd_read(0x700u, b, 2u) : n;
	parity_edp_snapshot(res);
	kern_logf("i915: parity AUX-TEST left for the stop path: vdd_hw=%d off_reserved=%d vdd_wakeref=%d "
		"refs aux=%d | sleeps: tick=%u (%llu us) short=%u (%llu us) | lock_errors=%u log_errors=%u\n",
		res->vdd_on_hw, res->vdd_work_pending, res->vdd_wakeref_held, res->power_refs_aux,
		dev->k.tick_sleeps, (unsigned long long)dev->k.tick_slept_us, dev->k.busy_sleeps,
		(unsigned long long)dev->k.busy_slept_us, dev->env.lock_errors, res->log_errors);

	pass = dev->late_rc == 0 && dpcd_match && edp_match && edid_match && lcd_match && auto_off_ms >= 0 &&
		n == 2 && b[0] == dp_fixture_dpcd_700[0] && race_kept && race_off_ms >= 0 &&
		res->vdd_on_hw == 1 && res->vdd_work_pending == 1 && dev->env.lock_errors == 0u &&
		res->log_errors == 0u && dev->k.time_faults == 0u && dev->k.pd->async_state_errors == 0u &&
		dev->k.pd->use_count_errors == 0u;
	kern_logf("i915: parity AUX-TEST verdict: %s (resident=1 dpcd_match=%d edp_dpcd_match=%d edid_match=%d "
		"lcd_a_match=%d auto_off=%d reacquire_kept=%d reacquire_off=%d; the stop-path result is the \"edp fini\" line)\n",
		pass ? "PASS" : "FAIL", dpcd_match, edp_match, edid_match, lcd_match, auto_off_ms >= 0, race_kept,
		race_off_ms >= 0);
	return pass ? 0 : -1;
}
