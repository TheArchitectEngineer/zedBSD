/*
 * WS031 Linux-parity — display power domains (see power_domains.h).
 *
 * intel_power_domains_init(): sanitize the disable_power_well option, compute
 * allowed_dc_mask + target_dc_state (reference register-bit encodings), init the
 * lock and async-put work, then build the power-well map
 * (intel_display_power_map_init).  ADL-P is display version 13 -> the xelpd
 * descriptor set (30 wells): always_on, PW_1, DC_off, PW_2, PW_A..D, DDI_IO_*,
 * AUX_A..E, AUX_USBC1..4, AUX_TBT1..4 -- in the reference order and with the
 * reference ids, hsw control indices and attributes (always_on / has_vga /
 * has_fuses / irq_pipe_mask / is_tc_tbt / fixed_enable_delay / enable_timeout /
 * ops kind).  A NULL domain list means "no domains"; a zero-length list means
 * "all domains" (PW_1 is always_on yet has a NULL list).  Live state (refcount,
 * hw_enabled) is separate from the descriptor.  The enable/disable/sync MMIO is
 * the init_hw (5.2) step; the hsw.idx / ids carried here are used there.
 */
#include "../internal.h"
#include <kern/klog.h>
#include <kern/lock.h>
#include <kern/waitq.h>
#include "parity.h"
#include "osdep/trace.h"
#include "power_domains.h"
#include "osdep/mmio.h"
#include "wait.h"
#include "vga.h"
#include <errno.h>

#define BIT_PIPE_A  (1u << 0)
#define BIT_PIPE_B  (1u << 1)
#define BIT_PIPE_C  (1u << 2)
#define BIT_PIPE_D  (1u << 3)

/* Reference hsw power-well control indices (i915_reg.h). */
#define IDX_PW_1      0u
#define IDX_PW_2      1u
#define IDX_PW_A      5u
#define IDX_PW_B      6u
#define IDX_PW_C      7u
#define IDX_PW_D      8u
#define IDX_DDI_A     0u
#define IDX_DDI_B     1u
#define IDX_DDI_C     2u
#define IDX_DDI_D     7u   /* XELPD */
#define IDX_DDI_E     8u   /* XELPD */
#define IDX_DDI_TC1   3u
#define IDX_DDI_TC2   4u
#define IDX_DDI_TC3   5u
#define IDX_DDI_TC4   6u
#define IDX_AUX_A     0u
#define IDX_AUX_B     1u
#define IDX_AUX_C     2u
#define IDX_AUX_D     7u   /* XELPD */
#define IDX_AUX_E     8u   /* XELPD */
#define IDX_AUX_TC1   3u
#define IDX_AUX_TC2   4u
#define IDX_AUX_TC3   5u
#define IDX_AUX_TC4   6u
#define IDX_AUX_TBT1  9u
#define IDX_AUX_TBT2  10u
#define IDX_AUX_TBT3  11u
#define IDX_AUX_TBT4  12u

static void
pw_dom(struct parity_pw_domain_mask *m, enum parity_power_domain d)
{
	m->bits[(unsigned)d >> 6] |= (uint64_t)1u << ((unsigned)d & 63u);
}

/* get_allowed_dc_mask(): ADL-P is display ver >= 12 (DC9 + DC3CO + up-to-DC6). */
static uint32_t
get_allowed_dc_mask(unsigned display_ver, int enable_dc)
{
	uint32_t mask = (display_ver >= 11u) ? PARITY_DC_STATE_EN_DC9 : 0u;
	uint32_t max = PARITY_DC_STATE_EN_DC3CO | PARITY_DC_STATE_EN_UPTO_DC6;

	/* enable_dc: -1 = auto (take the platform max); 0 = disabled; else max. */
	if (enable_dc != 0)
		mask |= max;
	return mask;
}

/* sanitize_target_dc_state(): fall to the next state if the request is not allowed. */
static uint32_t
sanitize_target_dc_state(const struct parity_power_domains *pd, uint32_t target)
{
	static const uint32_t states[] = {
		PARITY_DC_STATE_EN_UPTO_DC6, PARITY_DC_STATE_EN_UPTO_DC5, 0u /* DISABLE */ };
	unsigned i;

	for (i = 0u; i + 1u < sizeof(states) / sizeof(states[0]); i++) {
		if (target != states[i])
			continue;
		if (pd->allowed_dc_mask & target)
			break;
		target = states[i + 1u];
	}
	return target;
}

static int
sanitize_disable_power_well(int param)
{
	if (param < 0)
		return 1;   /* platform default: ADL-P allows dynamic disabling */
	return param ? 1 : 0;
}

static struct parity_power_well *
pw_add(struct parity_power_domains *pd, const char *name,
	enum parity_pw_ops_kind ops, int always_on, int has_vga, int has_fuses,
	int is_tc_tbt, int fixed_enable_delay, unsigned enable_timeout,
	unsigned irq_pipe_mask, unsigned hsw_idx, int id, int domains_all,
	const enum parity_power_domain *doms, unsigned ndoms)
{
	struct parity_power_well *w;
	unsigned i;

	if (pd->num_power_wells >= PARITY_PW_MAX)
		return 0;
	w = &pd->power_wells[pd->num_power_wells++];
	w->name = name;
	w->ops = ops;
	w->always_on = always_on;
	w->has_vga = has_vga;
	w->has_fuses = has_fuses;
	w->is_tc_tbt = is_tc_tbt;
	w->fixed_enable_delay = fixed_enable_delay;
	w->enable_timeout = enable_timeout;
	w->irq_pipe_mask = irq_pipe_mask;
	w->hsw_idx = hsw_idx;
	w->id = id;
	w->domains_all = domains_all;
	w->domains.bits[0] = 0u;
	w->domains.bits[1] = 0u;
	for (i = 0u; i < ndoms; i++)
		pw_dom(&w->domains, doms[i]);
	w->refcount = 0u;         /* SW reference count (separate from HW state) */
	w->hw_enabled = -1;       /* unknown until sync_hw at init_hw */
	return w;
}

/* intel_display_power_map_init(): build the xelpd wells + per-domain map. */
static int
power_map_init(struct parity_power_domains *pd)
{
	static const enum parity_power_domain pw_a[] = {
		PARITY_PW_DOMAIN_PIPE_A, PARITY_PW_DOMAIN_PIPE_PANEL_FITTER_A,
		PARITY_PW_DOMAIN_INIT };
	static const enum parity_power_domain pw_b[] = {
		PARITY_PW_DOMAIN_PIPE_B, PARITY_PW_DOMAIN_PIPE_PANEL_FITTER_B,
		PARITY_PW_DOMAIN_TRANSCODER_B, PARITY_PW_DOMAIN_INIT };
	static const enum parity_power_domain pw_c[] = {
		PARITY_PW_DOMAIN_PIPE_C, PARITY_PW_DOMAIN_PIPE_PANEL_FITTER_C,
		PARITY_PW_DOMAIN_TRANSCODER_C, PARITY_PW_DOMAIN_INIT };
	static const enum parity_power_domain pw_d[] = {
		PARITY_PW_DOMAIN_PIPE_D, PARITY_PW_DOMAIN_PIPE_PANEL_FITTER_D,
		PARITY_PW_DOMAIN_TRANSCODER_D, PARITY_PW_DOMAIN_INIT };
	/* xelpd_pwdoms_pw_2 = PW_B + PW_C + PW_D + DC_OFF_PORT(DDI lanes) + INIT. */
	static const enum parity_power_domain pw_2[] = {
		PARITY_PW_DOMAIN_PIPE_B, PARITY_PW_DOMAIN_PIPE_PANEL_FITTER_B,
		PARITY_PW_DOMAIN_TRANSCODER_B,
		PARITY_PW_DOMAIN_PIPE_C, PARITY_PW_DOMAIN_PIPE_PANEL_FITTER_C,
		PARITY_PW_DOMAIN_TRANSCODER_C,
		PARITY_PW_DOMAIN_PIPE_D, PARITY_PW_DOMAIN_PIPE_PANEL_FITTER_D,
		PARITY_PW_DOMAIN_TRANSCODER_D,
		PARITY_PW_DOMAIN_PORT_DDI_LANES_C, PARITY_PW_DOMAIN_PORT_DDI_LANES_D,
		PARITY_PW_DOMAIN_PORT_DDI_LANES_E, PARITY_PW_DOMAIN_PORT_DDI_LANES_TC1,
		PARITY_PW_DOMAIN_PORT_DDI_LANES_TC2, PARITY_PW_DOMAIN_PORT_DDI_LANES_TC3,
		PARITY_PW_DOMAIN_PORT_DDI_LANES_TC4, PARITY_PW_DOMAIN_INIT };
	/* xelpd_pwdoms_dc_off = DC_OFF_PORT + PW_C + PW_D + DSI + AUDIO + AUX_A/B + DC_OFF + INIT. */
	static const enum parity_power_domain dc_off[] = {
		PARITY_PW_DOMAIN_PORT_DDI_LANES_C, PARITY_PW_DOMAIN_PORT_DDI_LANES_D,
		PARITY_PW_DOMAIN_PORT_DDI_LANES_E, PARITY_PW_DOMAIN_PORT_DDI_LANES_TC1,
		PARITY_PW_DOMAIN_PORT_DDI_LANES_TC2, PARITY_PW_DOMAIN_PORT_DDI_LANES_TC3,
		PARITY_PW_DOMAIN_PORT_DDI_LANES_TC4,
		PARITY_PW_DOMAIN_PIPE_C, PARITY_PW_DOMAIN_PIPE_PANEL_FITTER_C,
		PARITY_PW_DOMAIN_TRANSCODER_C,
		PARITY_PW_DOMAIN_PIPE_D, PARITY_PW_DOMAIN_PIPE_PANEL_FITTER_D,
		PARITY_PW_DOMAIN_TRANSCODER_D,
		PARITY_PW_DOMAIN_PORT_DSI, PARITY_PW_DOMAIN_AUDIO_MMIO,
		PARITY_PW_DOMAIN_AUX_A, PARITY_PW_DOMAIN_AUX_B,
		PARITY_PW_DOMAIN_DC_OFF, PARITY_PW_DOMAIN_INIT };
	static const enum parity_power_domain ddi_io_a[] = { PARITY_PW_DOMAIN_PORT_DDI_IO_A };
	static const enum parity_power_domain ddi_io_b[] = { PARITY_PW_DOMAIN_PORT_DDI_IO_B };
	static const enum parity_power_domain ddi_io_c[] = { PARITY_PW_DOMAIN_PORT_DDI_IO_C };
	static const enum parity_power_domain ddi_io_d[] = { PARITY_PW_DOMAIN_PORT_DDI_IO_D };
	static const enum parity_power_domain ddi_io_e[] = { PARITY_PW_DOMAIN_PORT_DDI_IO_E };
	static const enum parity_power_domain ddi_io_tc1[] = { PARITY_PW_DOMAIN_PORT_DDI_IO_TC1 };
	static const enum parity_power_domain ddi_io_tc2[] = { PARITY_PW_DOMAIN_PORT_DDI_IO_TC2 };
	static const enum parity_power_domain ddi_io_tc3[] = { PARITY_PW_DOMAIN_PORT_DDI_IO_TC3 };
	static const enum parity_power_domain ddi_io_tc4[] = { PARITY_PW_DOMAIN_PORT_DDI_IO_TC4 };
	static const enum parity_power_domain aux_a[] = { PARITY_PW_DOMAIN_AUX_IO_A, PARITY_PW_DOMAIN_AUX_A };
	static const enum parity_power_domain aux_b[] = { PARITY_PW_DOMAIN_AUX_IO_B, PARITY_PW_DOMAIN_AUX_B };
	static const enum parity_power_domain aux_c[] = { PARITY_PW_DOMAIN_AUX_IO_C, PARITY_PW_DOMAIN_AUX_C };
	static const enum parity_power_domain aux_d[] = { PARITY_PW_DOMAIN_AUX_IO_D, PARITY_PW_DOMAIN_AUX_D };
	static const enum parity_power_domain aux_e[] = { PARITY_PW_DOMAIN_AUX_IO_E, PARITY_PW_DOMAIN_AUX_E };
	static const enum parity_power_domain aux_usbc1[] = { PARITY_PW_DOMAIN_AUX_USBC1 };
	static const enum parity_power_domain aux_usbc2[] = { PARITY_PW_DOMAIN_AUX_USBC2 };
	static const enum parity_power_domain aux_usbc3[] = { PARITY_PW_DOMAIN_AUX_USBC3 };
	static const enum parity_power_domain aux_usbc4[] = { PARITY_PW_DOMAIN_AUX_USBC4 };
	static const enum parity_power_domain aux_tbt1[] = { PARITY_PW_DOMAIN_AUX_TBT1 };
	static const enum parity_power_domain aux_tbt2[] = { PARITY_PW_DOMAIN_AUX_TBT2 };
	static const enum parity_power_domain aux_tbt3[] = { PARITY_PW_DOMAIN_AUX_TBT3 };
	static const enum parity_power_domain aux_tbt4[] = { PARITY_PW_DOMAIN_AUX_TBT4 };

	unsigned i;

/* n=name k=ops ao vga fu tbt fed to(timeout ms) irq idx id arr */
#define ADD(n, k, ao, vga, fu, tbt, fed, to, irq, idx, id, arr) \
	pw_add(pd, (n), (k), (ao), (vga), (fu), (tbt), (fed), (to), (irq), (idx), (id), 0, \
		(arr), (unsigned)(sizeof(arr) / sizeof((arr)[0])))

	pd->num_power_wells = 0u;

	/* i9xx_power_wells_always_on -> zero-length domain list = ALL domains. */
	(void)pw_add(pd, "always_on", PARITY_PW_OPS_ALWAYS_ON, 1, 0, 0, 0, 0, 0u, 0u, 0u,
		PARITY_DISP_PW_ID_NONE, 1 /* domains_all */, 0, 0u);
	/* icl_power_wells_pw_1: always_on, has_fuses, NULL domain list = none. */
	(void)pw_add(pd, "PW_1", PARITY_PW_OPS_HSW, 1, 0, 1, 0, 0, 0u, 0u, IDX_PW_1,
		PARITY_SKL_DISP_PW_1, 0 /* not all */, 0, 0u);
	/* xelpd_power_wells_dc_off. */
	(void)ADD("DC_off", PARITY_PW_OPS_DC_OFF, 0, 0, 0, 0, 0, 0u, 0u, 0u, PARITY_SKL_DISP_DC_OFF, dc_off);
	/* xelpd_power_wells_main. */
	(void)ADD("PW_2", PARITY_PW_OPS_HSW, 0, 1, 1, 0, 0, 0u, 0u, IDX_PW_2, PARITY_SKL_DISP_PW_2, pw_2);
	(void)ADD("PW_A", PARITY_PW_OPS_HSW, 0, 0, 1, 0, 0, 0u, BIT_PIPE_A, IDX_PW_A, PARITY_DISP_PW_ID_NONE, pw_a);
	(void)ADD("PW_B", PARITY_PW_OPS_HSW, 0, 0, 1, 0, 0, 0u, BIT_PIPE_B, IDX_PW_B, PARITY_DISP_PW_ID_NONE, pw_b);
	(void)ADD("PW_C", PARITY_PW_OPS_HSW, 0, 0, 1, 0, 0, 0u, BIT_PIPE_C, IDX_PW_C, PARITY_DISP_PW_ID_NONE, pw_c);
	(void)ADD("PW_D", PARITY_PW_OPS_HSW, 0, 0, 1, 0, 0, 0u, BIT_PIPE_D, IDX_PW_D, PARITY_DISP_PW_ID_NONE, pw_d);
	(void)ADD("DDI_IO_A", PARITY_PW_OPS_ICL_DDI, 0, 0, 0, 0, 0, 0u, 0u, IDX_DDI_A, PARITY_DISP_PW_ID_NONE, ddi_io_a);
	(void)ADD("DDI_IO_B", PARITY_PW_OPS_ICL_DDI, 0, 0, 0, 0, 0, 0u, 0u, IDX_DDI_B, PARITY_DISP_PW_ID_NONE, ddi_io_b);
	(void)ADD("DDI_IO_C", PARITY_PW_OPS_ICL_DDI, 0, 0, 0, 0, 0, 0u, 0u, IDX_DDI_C, PARITY_DISP_PW_ID_NONE, ddi_io_c);
	(void)ADD("DDI_IO_D", PARITY_PW_OPS_ICL_DDI, 0, 0, 0, 0, 0, 0u, 0u, IDX_DDI_D, PARITY_DISP_PW_ID_NONE, ddi_io_d);
	(void)ADD("DDI_IO_E", PARITY_PW_OPS_ICL_DDI, 0, 0, 0, 0, 0, 0u, 0u, IDX_DDI_E, PARITY_DISP_PW_ID_NONE, ddi_io_e);
	(void)ADD("DDI_IO_TC1", PARITY_PW_OPS_ICL_DDI, 0, 0, 0, 0, 0, 0u, 0u, IDX_DDI_TC1, PARITY_DISP_PW_ID_NONE, ddi_io_tc1);
	(void)ADD("DDI_IO_TC2", PARITY_PW_OPS_ICL_DDI, 0, 0, 0, 0, 0, 0u, 0u, IDX_DDI_TC2, PARITY_DISP_PW_ID_NONE, ddi_io_tc2);
	(void)ADD("DDI_IO_TC3", PARITY_PW_OPS_ICL_DDI, 0, 0, 0, 0, 0, 0u, 0u, IDX_DDI_TC3, PARITY_DISP_PW_ID_NONE, ddi_io_tc3);
	(void)ADD("DDI_IO_TC4", PARITY_PW_OPS_ICL_DDI, 0, 0, 0, 0, 0, 0u, 0u, IDX_DDI_TC4, PARITY_DISP_PW_ID_NONE, ddi_io_tc4);
	/* AUX_A..E: fixed_enable_delay. */
	(void)ADD("AUX_A", PARITY_PW_OPS_ICL_AUX, 0, 0, 0, 0, 1, 0u, 0u, IDX_AUX_A, PARITY_DISP_PW_ID_NONE, aux_a);
	(void)ADD("AUX_B", PARITY_PW_OPS_ICL_AUX, 0, 0, 0, 0, 1, 0u, 0u, IDX_AUX_B, PARITY_DISP_PW_ID_NONE, aux_b);
	(void)ADD("AUX_C", PARITY_PW_OPS_ICL_AUX, 0, 0, 0, 0, 1, 0u, 0u, IDX_AUX_C, PARITY_DISP_PW_ID_NONE, aux_c);
	(void)ADD("AUX_D", PARITY_PW_OPS_ICL_AUX, 0, 0, 0, 0, 1, 0u, 0u, IDX_AUX_D, PARITY_DISP_PW_ID_NONE, aux_d);
	(void)ADD("AUX_E", PARITY_PW_OPS_ICL_AUX, 0, 0, 0, 0, 1, 0u, 0u, IDX_AUX_E, PARITY_DISP_PW_ID_NONE, aux_e);
	/* AUX_USBC1..4: fixed_enable_delay + WA_14017248603 enable_timeout=500ms. */
	(void)ADD("AUX_USBC1", PARITY_PW_OPS_ICL_AUX, 0, 0, 0, 0, 1, 500u, 0u, IDX_AUX_TC1, PARITY_DISP_PW_ID_NONE, aux_usbc1);
	(void)ADD("AUX_USBC2", PARITY_PW_OPS_ICL_AUX, 0, 0, 0, 0, 1, 500u, 0u, IDX_AUX_TC2, PARITY_DISP_PW_ID_NONE, aux_usbc2);
	(void)ADD("AUX_USBC3", PARITY_PW_OPS_ICL_AUX, 0, 0, 0, 0, 1, 500u, 0u, IDX_AUX_TC3, PARITY_DISP_PW_ID_NONE, aux_usbc3);
	(void)ADD("AUX_USBC4", PARITY_PW_OPS_ICL_AUX, 0, 0, 0, 0, 1, 500u, 0u, IDX_AUX_TC4, PARITY_DISP_PW_ID_NONE, aux_usbc4);
	/* AUX_TBT1..4: is_tc_tbt. */
	(void)ADD("AUX_TBT1", PARITY_PW_OPS_ICL_AUX, 0, 0, 0, 1, 0, 0u, 0u, IDX_AUX_TBT1, PARITY_DISP_PW_ID_NONE, aux_tbt1);
	(void)ADD("AUX_TBT2", PARITY_PW_OPS_ICL_AUX, 0, 0, 0, 1, 0, 0u, 0u, IDX_AUX_TBT2, PARITY_DISP_PW_ID_NONE, aux_tbt2);
	(void)ADD("AUX_TBT3", PARITY_PW_OPS_ICL_AUX, 0, 0, 0, 1, 0, 0u, 0u, IDX_AUX_TBT3, PARITY_DISP_PW_ID_NONE, aux_tbt3);
	(void)ADD("AUX_TBT4", PARITY_PW_OPS_ICL_AUX, 0, 0, 0, 1, 0, 0u, 0u, IDX_AUX_TBT4, PARITY_DISP_PW_ID_NONE, aux_tbt4);
#undef ADD

	/* Build the per-domain -> wells map from each well's membership. */
	for (i = 0u; i < PARITY_PW_DOMAIN_NUM; i++)
		pd->domain_wells[i] = 0u;
	for (i = 0u; i < pd->num_power_wells; i++) {
		const struct parity_power_well *w = &pd->power_wells[i];
		unsigned d;

		if (w->domains_all) {
			for (d = 0u; d < PARITY_PW_DOMAIN_NUM; d++)
				pd->domain_wells[d] |= (uint64_t)1u << i;
			continue;
		}
		for (d = 0u; d < PARITY_PW_DOMAIN_NUM; d++)
			if (w->domains.bits[d >> 6] & ((uint64_t)1u << (d & 63u)))
				pd->domain_wells[d] |= (uint64_t)1u << i;
	}

	pd->map_initialized = 1;
	return 0;
}

int
parity_intel_power_domains_init(struct parity_power_domains *pd,
	unsigned display_ver, int enable_dc_param, int disable_pw_param,
	struct osdep_trace *trace)
{
	if (pd == 0)
		return -1;

	pd->disable_power_well = sanitize_disable_power_well(disable_pw_param);
	pd->allowed_dc_mask = get_allowed_dc_mask(display_ver, enable_dc_param);
	pd->target_dc_state = sanitize_target_dc_state(pd, PARITY_DC_STATE_EN_UPTO_DC6);

	(void)mutex_init(&pd->lock, LOCK_RANK_DEVICE, "parity-power-domains");
	pd->async_put_work_inited = 1;   /* INIT_DELAYED_WORK(async_put_work) */
	{
		unsigned d;

		/* the use counts and the async-put state start empty whatever the caller's memory held */
		for (d = 0u; d < PARITY_PW_DOMAIN_NUM; d++)
			pd->domain_use_count[d] = 0u;
		pd->async_put_domains[0].bits[0] = pd->async_put_domains[0].bits[1] = 0u;
		pd->async_put_domains[1].bits[0] = pd->async_put_domains[1].bits[1] = 0u;
		pd->async_put_wakeref = 0;
		pd->async_put_next_delay = 0;
		pd->async_ops = 0;
		pd->async_ctx = 0;
		pd->async_pwc = 0;
		pd->async_puts = pd->async_parked = pd->async_grabs = pd->async_work_runs = pd->async_work_empty = 0u;
		pd->async_released = pd->async_requeues = pd->async_flushes = 0u;
		pd->async_state_errors = pd->use_count_errors = 0u;
	}
	pd->map_initialized = 0;
	pd->num_power_wells = 0u;

	if (power_map_init(pd) != 0)
		return -1;

	pd->initialized = 1;
	osdep_trace_emit(trace, PARITY_STAGE_P3, OSDEP_TR_ACQUIRE,
		"intel_power_domains_init", (uint64_t)pd->num_power_wells,
		(uint64_t)pd->allowed_dc_mask);
	kern_logf("i915: parity P3 intel_power_domains_init: wells=%u allowed_dc=0x%x "
		"target_dc=0x%x disable_pw=%d (xelpd map)\n",
		pd->num_power_wells, pd->allowed_dc_mask, pd->target_dc_state,
		pd->disable_power_well);
	return 0;
}

void
parity_intel_power_domains_cleanup(struct parity_power_domains *pd)
{
	if (pd == 0 || !pd->initialized)
		return;
	/* intel_display_power_map_cleanup(): drop the map (no MMIO here; this path
	 * is only valid BEFORE any HW power get -- see the init_hw cleanup). */
	pd->num_power_wells = 0u;
	pd->map_initialized = 0;
	pd->initialized = 0;
}

uint64_t
parity_power_domain_wells(const struct parity_power_domains *pd,
	enum parity_power_domain domain)
{
	if (pd == 0 || (unsigned)domain >= PARITY_PW_DOMAIN_NUM)
		return 0u;
	return pd->domain_wells[domain];
}

int
parity_power_well_by_id(const struct parity_power_domains *pd, int id)
{
	unsigned i;

	if (pd == 0 || id == PARITY_DISP_PW_ID_NONE)
		return -1;
	for (i = 0u; i < pd->num_power_wells; i++)
		if (pd->power_wells[i].id == id)
			return (int)i;
	return -1;
}


/* --- Power-well operation bodies (unit C) --- */

/* Driver control register per ops family (i915_reg.h). */
static unsigned
pw_driver_reg(enum parity_pw_ops_kind ops)
{
	switch (ops) {
	case PARITY_PW_OPS_ICL_AUX: return 0x45444u;   /* ICL_PWR_WELL_CTL_AUX2 */
	case PARITY_PW_OPS_ICL_DDI: return 0x45454u;   /* ICL_PWR_WELL_CTL_DDI2 */
	default:                    return 0x45404u;   /* HSW_PWR_WELL_CTL2 (driver) */
	}
}

static uint32_t pw_req(unsigned idx)   { return 0x2u << (idx * 2u); }   /* HSW_PWR_WELL_CTL_REQ */
static uint32_t pw_state(unsigned idx) { return 0x1u << (idx * 2u); }   /* HSW_PWR_WELL_CTL_STATE */

/* Fuse-distribution status (gen9_wait_for_power_well_fuses). */
#define SKL_FUSE_STATUS            0x42000u
#define SKL_FUSE_PG_DIST(pg)       (1u << (27u - (pg)))   /* PG0=1<<27, PG1=1<<26 */
#define SKL_PG0                    0u
#define SKL_PG1                    1u
#define GEN8_CHICKEN_DCPR_1        0x46430u
#define DISABLE_FLR_SRC            (1u << 15)

/* gen9_wait_for_power_well_fuses(): intel_de_wait_for_set(...,timeout_ms=1); WARN+continue. */
static int
pw_wait_fuse(struct parity_pw_ctx *c, unsigned pg)
{
	uint32_t bit = SKL_FUSE_PG_DIST(pg);
	/* intel_de_wait_for_set(...,1): fast_timeout_us=2 + slow_timeout_ms=1 for ALL PGs
	 * (the 5us/1us is only a reference comment). */
	int rc = parity_wait_reg(c->mmio, SKL_FUSE_STATUS, bit, bit, 2u, 1u, 0);

	if (rc == -EIO)
		return -EIO;   /* time-base anomaly: caller must stop */
	if (rc != 0)
		kern_logf("i915: parity power well fuse PG%u wait timeout (continuing)\n", pg);
	return 0;
}

/* hsw_power_well_post_enable(): VGA and pipe-IRQ post-enable under their conditions. */
static void
pw_post_enable(struct parity_power_well *w, struct parity_pw_ctx *c)
{
	if (w->has_vga) {
		/* intel_vga_reset_io_mem(): keep vgacon sane after enabling the well. */
		parity_intel_vga_reset_io_mem(c->vga);
		c->vga_reset_calls++;
	}
	if (w->irq_pipe_mask != 0u) {
		/*
		 * gen8_irq_power_well_post_enable(): the reference takes the IRQ lock and
		 * only acts when intel_irqs_enabled().  Before P4 the handler is not
		 * installed (irqs_enabled == 0), so this is a guarded no-op -- but the
		 * guarded entry is present; we do NOT front-load the P4 handler install.
		 */
		if (c->irqs_enabled)
			c->irq_post_enable_calls++;   /* would program the pipe IRQ registers */
	}
}

int
parity_power_well_enable(struct parity_power_well *w, struct parity_pw_ctx *c)
{
	unsigned reg;
	uint32_t req, st, v;
	int rc;

	if (w->ops == PARITY_PW_OPS_ALWAYS_ON) {
		w->hw_enabled = 1;
		return 0;
	}
	if (w->ops == PARITY_PW_OPS_DC_OFF) {
		/* gen9_dc_off_power_well_enable() -> gen9_disable_dc_states(). */
		parity_dc_off_enable(c);
		w->hw_enabled = 1;
		return 0;
	}

	reg = pw_driver_reg(w->ops);
	req = pw_req(w->hsw_idx);
	st = pw_state(w->hsw_idx);

	/*
	 * has_fuses (hsw_power_well_enable): for PW1 (pg == PG1) wait for the PG0
	 * fuse state BEFORE enabling; every fused PW waits for its own PG state
	 * AFTER.  pg = ICL_PW_CTL_IDX_TO_PG(idx) = idx + SKL_PG1 on DISPLAY_VER >= 11.
	 */
	if (w->has_fuses) {
		unsigned pg = w->hsw_idx + SKL_PG1;
		if (pg == SKL_PG1) {
			/* Wa_16013190616:adlp */
			uint32_t d = osdep_mmio_raw_read32(c->mmio, GEN8_CHICKEN_DCPR_1);
			osdep_mmio_raw_write32(c->mmio, GEN8_CHICKEN_DCPR_1, d | DISABLE_FLR_SRC);
			if (pw_wait_fuse(c, SKL_PG0) == -EIO)
				return -EIO;
		}
	}

	/* Set the REQUEST bit: intel_de_rmw(driver, 0, REQ). */
	v = osdep_mmio_raw_read32(c->mmio, reg);
	osdep_mmio_raw_write32(c->mmio, reg, v | req);

	/*
	 * hsw_wait_for_power_well_enable(): wait for the STATE (ACK) bit.  A fixed
	 * enable delay is used only under IS_DG2(); ADL-P is not DG2, so we wait for
	 * the ACK (with the WA enable_timeout when the descriptor sets one).
	 */
	rc = parity_wait_reg(c->mmio, reg, st, st, 0u,
		(w->enable_timeout != 0u) ? w->enable_timeout : 2u, 0);
	if (rc == -EIO)
		return -EIO;   /* time-base anomaly / adaptation-layer error: stop */
	if (rc != 0) {
		/*
		 * hsw_wait_for_power_well_enable() is void: a real HW ACK timeout is
		 * warned and the enable CONTINUES (AUX in particular expects a timeout).
		 * Record it for diagnostics; do not fail the caller (no domain-get
		 * unwind on a plain HW timeout).
		 */
		c->ack_timeouts++;
		kern_logf("i915: parity power well %s enable ACK timeout (continuing)\n",
			w->name);
	}

	if (w->has_fuses) {
		unsigned pg = w->hsw_idx + SKL_PG1;   /* ICL_PW_CTL_IDX_TO_PG */
		if (pw_wait_fuse(c, pg) == -EIO)
			return -EIO;
	}

	w->hw_enabled = 1;
	pw_post_enable(w, c);
	return 0;
}

int
parity_power_well_disable(struct parity_power_well *w, struct parity_pw_ctx *c)
{
	unsigned reg;
	uint32_t req, st, v;

	if (w->ops == PARITY_PW_OPS_ALWAYS_ON) {
		w->hw_enabled = 1;   /* always-on: never actually disabled */
		return 0;
	}
	if (w->ops == PARITY_PW_OPS_DC_OFF) {
		/* gen9_dc_off_power_well_disable() */
		c->dc_off_disable_calls++;
		if (c->dmc_has_payload) {
			switch (c->target_dc_state) {
			case PARITY_DC_STATE_EN_DC3CO:
				/* tgl_enable_dc3co() */
				parity_gen9_set_dc_state(c, PARITY_DC_STATE_EN_DC3CO);
				break;
			case PARITY_DC_STATE_EN_UPTO_DC6:
				/* skl_enable_dc6(): assert_can_enable_dc6 (warnings only) */
				if ((osdep_mmio_raw_read32(c->mmio, 0x48400u) & (0x80000000u | (0x1fu << 24))) ==
				    (0x80000000u | (1u << 24)))
					kern_logf("i915: parity DC6: utility pin enabled in PWM mode\n");
				if (osdep_mmio_raw_read32(c->mmio, 0x45504u) & PARITY_DC_STATE_EN_UPTO_DC6)
					kern_logf("i915: parity DC6 already programmed to be enabled\n");
				parity_gen9_set_dc_state(c, PARITY_DC_STATE_EN_UPTO_DC6);
				break;
			case PARITY_DC_STATE_EN_UPTO_DC5:
				/* gen9_enable_dc5(): Wa Display #1183 is DISPLAY_VER 9 only */
				parity_gen9_set_dc_state(c, PARITY_DC_STATE_EN_UPTO_DC5);
				break;
			default:
				break;
			}
		}
		w->hw_enabled = parity_power_well_is_enabled(w, c);
		return 0;
	}

	reg = pw_driver_reg(w->ops);
	req = pw_req(w->hsw_idx);
	st = pw_state(w->hsw_idx);

	/* hsw_power_well_pre_disable(): pipe-IRQ pre-disable (guarded) would run here. */
	v = osdep_mmio_raw_read32(c->mmio, reg);
	osdep_mmio_raw_write32(c->mmio, reg, v & ~req);
	/*
	 * Wait for the well to turn off only when WE are the last requester.  If
	 * BIOS/KVMR still holds the REQ the STATE stays set -- that is expected,
	 * not a timeout (do NOT unconditionally wait for STATE == 0).
	 */
	if ((osdep_mmio_raw_read32(c->mmio, reg - 4u) & req) == 0u)
		(void)parity_wait_reg(c->mmio, reg, st, 0u, 0u, 2u, 0);

	/* hw_enabled reflects driver ownership (is_enabled: REQ+STATE). */
	w->hw_enabled = parity_power_well_is_enabled(w, c);
	return 0;
}

int
parity_power_well_is_enabled(struct parity_power_well *w, struct parity_pw_ctx *c)
{
	unsigned reg;
	uint32_t mask;

	if (w->ops == PARITY_PW_OPS_ALWAYS_ON)
		return 1;
	if (w->ops == PARITY_PW_OPS_DC_OFF) {
		/* gen9_dc_off_power_well_enabled(): no DC5/DC6 (and DC3CO) state set. */
		uint32_t dmask = PARITY_DC_STATE_EN_UPTO_DC5 | PARITY_DC_STATE_EN_UPTO_DC6;

		if (c->allowed_dc_mask & PARITY_DC_STATE_EN_DC3CO)
			dmask |= PARITY_DC_STATE_EN_DC3CO;
		return ((osdep_mmio_raw_read32(c->mmio, 0x45504u) & dmask) == 0u) ? 1 : 0;
	}
	reg = pw_driver_reg(w->ops);
	/* ADL-P hsw_power_well_enabled(): enabled iff BOTH driver REQ and STATE. */
	mask = pw_req(w->hsw_idx) | pw_state(w->hsw_idx);
	return ((osdep_mmio_raw_read32(c->mmio, reg) & mask) == mask) ? 1 : 0;
}

void
parity_power_well_sync_hw(struct parity_power_well *w, struct parity_pw_ctx *c)
{
	if (w->ops == PARITY_PW_OPS_ALWAYS_ON) {
		w->hw_enabled = 1;
		return;
	}
	if (w->ops != PARITY_PW_OPS_DC_OFF) {
		/*
		 * hsw_power_well_sync_hw(): take over the request bit from BIOS.  If
		 * BIOS holds the driver REQ, set OUR (driver) REQ first when it is not
		 * already set, THEN clear the BIOS REQ -- never the reverse, and never
		 * skipped because the software refcount is zero.  The refcount is not
		 * touched here.
		 */
		unsigned dreg = pw_driver_reg(w->ops);
		unsigned breg = dreg - 4u;   /* CTL1 (BIOS) = CTL2 (driver) - 4 */
		uint32_t mask = pw_req(w->hsw_idx);
		uint32_t bios_req = osdep_mmio_raw_read32(c->mmio, breg);

		if (bios_req & mask) {
			uint32_t drv_req = osdep_mmio_raw_read32(c->mmio, dreg);

			if ((drv_req & mask) == 0u)
				osdep_mmio_raw_write32(c->mmio, dreg, drv_req | mask);
			osdep_mmio_raw_write32(c->mmio, breg, bios_req & ~mask);
		}
	}
	/* intel_power_well_sync_hw(): then read is_enabled() and store it. */
	w->hw_enabled = parity_power_well_is_enabled(w, c);
}

int
parity_power_well_get(struct parity_power_well *w, struct parity_pw_ctx *c)
{
	int rc = 0;

	/* get() enables on the 0->1 transition; a plain enable() (from display-core
	 * init) is a DIFFERENT entry that does not touch the reference count. */
	if (w->refcount == 0u)
		rc = parity_power_well_enable(w, c);
	if (rc == 0)
		w->refcount++;
	return rc;
}

void
parity_power_well_put(struct parity_power_well *w, struct parity_pw_ctx *c)
{
	if (w->refcount == 0u)
		return;   /* underflow guard */
	w->refcount--;
	if (w->refcount == 0u)
		(void)parity_power_well_disable(w, c);
}

/*
 * __intel_display_power_is_enabled().  The reference walks the domain's wells in
 * REVERSE and uses intel_power_well_is_enabled_cached() -- the recorded state,
 * not a fresh HW read -- so a caller in an IRQ-reset path does not touch MMIO
 * just to answer the question.  always-on wells are skipped.
 */
int
parity_display_power_is_enabled(struct parity_power_domains *pd,
	enum parity_power_domain d, struct parity_pw_ctx *c)
{
	uint64_t wells = parity_power_domain_wells(pd, d);
	unsigned i = pd->num_power_wells;

	(void)c;

	while (i-- > 0u) {
		struct parity_power_well *w;

		if ((wells & ((uint64_t)1u << i)) == 0u)
			continue;

		w = &pd->power_wells[i];
		if (w->always_on)
			continue;

		/* hw_enabled is -1 until the first sync_hw; that is not "enabled". */
		if (w->hw_enabled != 1)
			return 0;
	}

	return 1;
}

/* ---- async put bookkeeping (intel_display_power.c) ---- */
static int
mask_test(const struct parity_pw_domain_mask *m, unsigned d)
{
	return (m->bits[d >> 6] >> (d & 63u)) & 1u;
}

static void
mask_set(struct parity_pw_domain_mask *m, unsigned d)
{
	m->bits[d >> 6] |= (uint64_t)1u << (d & 63u);
}

static void
mask_clear(struct parity_pw_domain_mask *m, unsigned d)
{
	m->bits[d >> 6] &= ~((uint64_t)1u << (d & 63u));
}

static int
mask_empty(const struct parity_pw_domain_mask *m)
{
	return m->bits[0] == 0u && m->bits[1] == 0u;
}

/* verify_async_put_domains_state(): the two masks are disjoint, the wakeref flag matches
 * "something is parked", and every parked domain holds exactly one reference. */
static void
verify_async_put_domains_state(struct parity_power_domains *pd)
{
	unsigned d;
	int err = 0;

	if ((pd->async_put_domains[0].bits[0] & pd->async_put_domains[1].bits[0]) != 0u ||
	    (pd->async_put_domains[0].bits[1] & pd->async_put_domains[1].bits[1]) != 0u)
		err = 1;
	if ((pd->async_put_wakeref != 0) !=
	    !(mask_empty(&pd->async_put_domains[0]) && mask_empty(&pd->async_put_domains[1])))
		err = 1;
	for (d = 0u; d < PARITY_PW_DOMAIN_NUM; d++)
		if ((mask_test(&pd->async_put_domains[0], d) || mask_test(&pd->async_put_domains[1], d)) &&
		    pd->domain_use_count[d] != 1u)
			err = 1;
	if (err) {
		pd->async_state_errors++;
		kern_logf("i915: parity power: async put state inconsistent (wakeref=%d [0]=%016llx:%016llx "
			"[1]=%016llx:%016llx)\n", pd->async_put_wakeref,
			(unsigned long long)pd->async_put_domains[0].bits[1],
			(unsigned long long)pd->async_put_domains[0].bits[0],
			(unsigned long long)pd->async_put_domains[1].bits[1],
			(unsigned long long)pd->async_put_domains[1].bits[0]);
	}
}

/* cancel_async_put_work() */
static void
cancel_async_put_work(struct parity_power_domains *pd, int sync)
{
	if (pd->async_ops != 0)
		(void)pd->async_ops->cancel(pd->async_ctx, sync);
	pd->async_put_next_delay = 0;
}

/* intel_display_power_grab_async_put_ref(): 1 when the get took a parked reference back */
static int
grab_async_put_ref(struct parity_power_domains *pd, unsigned d)
{
	int ret = 0;

	if (!mask_test(&pd->async_put_domains[0], d) && !mask_test(&pd->async_put_domains[1], d))
		goto out_verify;
	mask_clear(&pd->async_put_domains[0], d);
	mask_clear(&pd->async_put_domains[1], d);
	ret = 1;
	pd->async_grabs++;
	if (!mask_empty(&pd->async_put_domains[0]) || !mask_empty(&pd->async_put_domains[1]))
		goto out_verify;
	cancel_async_put_work(pd, 0);
	pd->async_put_wakeref = 0;
out_verify:
	verify_async_put_domains_state(pd);
	return ret;
}

/* __intel_display_power_get_domain(), with pd->lock held */
static int
get_domain_locked(struct parity_power_domains *pd, enum parity_power_domain d, struct parity_pw_ctx *c)
{
	uint64_t wells = parity_power_domain_wells(pd, d);
	unsigned i;

	if (grab_async_put_ref(pd, (unsigned)d))
		return 0;

	/* Get the domain's wells in ascending (reference enabling) order. */
	for (i = 0u; i < pd->num_power_wells; i++) {
		if ((wells & ((uint64_t)1u << i)) == 0u)
			continue;
		if (parity_power_well_get(&pd->power_wells[i], c) != 0) {
			/* Unwind the wells already taken, in reverse. */
			unsigned j = i;

			while (j-- > 0u)
				if (wells & ((uint64_t)1u << j))
					parity_power_well_put(&pd->power_wells[j], c);
			return -1;
		}
	}
	pd->domain_use_count[d]++;
	return 0;
}

/* __intel_display_power_put_domain(), with pd->lock held */
static void
put_domain_locked(struct parity_power_domains *pd, enum parity_power_domain d, struct parity_pw_ctx *c)
{
	uint64_t wells = parity_power_domain_wells(pd, d);
	unsigned i = pd->num_power_wells;

	if (pd->domain_use_count[d] == 0u) {
		pd->use_count_errors++;
		kern_logf("i915: parity power: use count on domain %u is already zero\n", (unsigned)d);
	} else {
		pd->domain_use_count[d]--;
	}
	if (mask_test(&pd->async_put_domains[0], (unsigned)d) || mask_test(&pd->async_put_domains[1], (unsigned)d)) {
		pd->async_state_errors++;
		kern_logf("i915: parity power: async disabling of domain %u is pending\n", (unsigned)d);
	}

	/* Put the domain's wells in reverse order. */
	while (i-- > 0u)
		if (wells & ((uint64_t)1u << i))
			parity_power_well_put(&pd->power_wells[i], c);
}

int
parity_display_power_get(struct parity_power_domains *pd,
	enum parity_power_domain d, struct parity_pw_ctx *c)
{
	int rc;

	mutex_lock(&pd->lock);
	rc = get_domain_locked(pd, d, c);
	mutex_unlock(&pd->lock);
	return rc;
}

void
parity_display_power_put(struct parity_power_domains *pd,
	enum parity_power_domain d, struct parity_pw_ctx *c)
{
	mutex_lock(&pd->lock);
	put_domain_locked(pd, d, c);
	mutex_unlock(&pd->lock);
}

void
parity_display_power_async_bind(struct parity_power_domains *pd,
	const struct parity_pw_async_ops *ops, void *ctx, struct parity_pw_ctx *pwc)
{
	mutex_lock(&pd->lock);
	pd->async_ops = ops;
	pd->async_ctx = ctx;
	pd->async_pwc = pwc;
	mutex_unlock(&pd->lock);
}

/* queue_async_put_domains_work() */
static void
queue_async_put_domains_work(struct parity_power_domains *pd, int delay_ms)
{
	if (pd->async_put_wakeref != 0)
		pd->async_state_errors++;
	pd->async_put_wakeref = 1;
	if (!pd->async_ops->queue(pd->async_ctx, delay_ms)) {
		pd->async_state_errors++;
		kern_logf("i915: parity power: async put work was already queued\n");
	}
}

/* release_async_put_domains() */
static void
release_async_put_domains(struct parity_power_domains *pd, const struct parity_pw_domain_mask *mask)
{
	struct parity_pw_domain_mask m = *mask;   /* `mask` may be one of the two being cleared */
	unsigned d;

	for (d = 0u; d < PARITY_PW_DOMAIN_NUM; d++) {
		if (!mask_test(&m, d))
			continue;
		/* Clear before put, so the put's sanity check is happy. */
		mask_clear(&pd->async_put_domains[0], d);
		mask_clear(&pd->async_put_domains[1], d);
		put_domain_locked(pd, (enum parity_power_domain)d, pd->async_pwc);
		pd->async_released++;
	}
}

/* __intel_display_power_put_async() */
void
parity_display_power_put_async(struct parity_power_domains *pd,
	enum parity_power_domain d, struct parity_pw_ctx *c, int delay_ms)
{
	delay_ms = delay_ms >= 0 ? delay_ms : 100;

	mutex_lock(&pd->lock);
	pd->async_puts++;
	if (pd->async_ops == 0 || pd->domain_use_count[d] > 1u) {
		/* not the last reference (or no delayed work to hand it to): an ordinary put */
		put_domain_locked(pd, d, c);
		goto out_verify;
	}
	if (pd->domain_use_count[d] != 1u)
		pd->use_count_errors++;

	/* Let a pending work requeue itself or queue a new one. */
	pd->async_parked++;
	if (pd->async_put_wakeref != 0) {
		mask_set(&pd->async_put_domains[1], (unsigned)d);
		if (delay_ms > pd->async_put_next_delay)
			pd->async_put_next_delay = delay_ms;
	} else {
		mask_set(&pd->async_put_domains[0], (unsigned)d);
		queue_async_put_domains_work(pd, delay_ms);
	}
out_verify:
	verify_async_put_domains_state(pd);
	mutex_unlock(&pd->lock);
}

/* intel_display_power_put_async_work() */
void
parity_display_power_async_work(struct parity_power_domains *pd)
{
	mutex_lock(&pd->lock);
	pd->async_work_runs++;

	/* Bail out if all the parked references were grabbed by later gets or a flush. */
	if (pd->async_put_wakeref == 0) {
		pd->async_work_empty++;
		goto out_verify;
	}
	pd->async_put_wakeref = 0;

	release_async_put_domains(pd, &pd->async_put_domains[0]);

	/* Requeue the work if more domains were async put meanwhile. */
	if (!mask_empty(&pd->async_put_domains[1])) {
		pd->async_put_domains[0] = pd->async_put_domains[1];
		pd->async_put_domains[1].bits[0] = pd->async_put_domains[1].bits[1] = 0u;
		pd->async_requeues++;
		queue_async_put_domains_work(pd, pd->async_put_next_delay);
		pd->async_put_next_delay = 0;
	} else {
		/* Cancel the work that got queued after this one got dequeued. */
		cancel_async_put_work(pd, 0);
	}
out_verify:
	verify_async_put_domains_state(pd);
	mutex_unlock(&pd->lock);
}

/* intel_display_power_flush_work() */
void
parity_display_power_flush_work(struct parity_power_domains *pd)
{
	struct parity_pw_domain_mask all;

	mutex_lock(&pd->lock);
	pd->async_flushes++;
	if (pd->async_put_wakeref == 0)
		goto out_verify;
	pd->async_put_wakeref = 0;
	all.bits[0] = pd->async_put_domains[0].bits[0] | pd->async_put_domains[1].bits[0];
	all.bits[1] = pd->async_put_domains[0].bits[1] | pd->async_put_domains[1].bits[1];
	release_async_put_domains(pd, &all);
	cancel_async_put_work(pd, 0);
out_verify:
	verify_async_put_domains_state(pd);
	mutex_unlock(&pd->lock);
}

/* intel_display_power_flush_work_sync(): also guarantees the work body is not running */
void
parity_display_power_flush_work_sync(struct parity_power_domains *pd)
{
	parity_display_power_flush_work(pd);
	/* outside pd->lock: the work body takes it */
	if (pd->async_ops != 0)
		(void)pd->async_ops->cancel(pd->async_ctx, 1);
	mutex_lock(&pd->lock);
	pd->async_put_next_delay = 0;
	verify_async_put_domains_state(pd);
	if (pd->async_put_wakeref != 0)
		pd->async_state_errors++;
	mutex_unlock(&pd->lock);
}

void
parity_intel_pmdemand_init_early(struct parity_pmdemand *pm)
{
	(void)mutex_init(&pm->lock, LOCK_RANK_DEVICE, "parity-pmdemand");
	waitq_init(&pm->waitqueue, "parity-pmdemand");
	pm->early_initialized = 1;
}

/* gen9_dc_mask() */
static uint32_t
gen9_dc_mask(int display_ver)
{
	uint32_t mask = PARITY_DC_STATE_EN_UPTO_DC5;

	if (display_ver >= 12)
		mask |= PARITY_DC_STATE_EN_DC3CO | PARITY_DC_STATE_EN_UPTO_DC6 |
			PARITY_DC_STATE_EN_DC9;
	else if (display_ver == 11)
		mask |= PARITY_DC_STATE_EN_UPTO_DC6 | PARITY_DC_STATE_EN_DC9;
	else
		mask |= PARITY_DC_STATE_EN_UPTO_DC6;
	return mask;
}

/* gen9_write_dc_state(): the DMC may keep returning the old value. */
static void
gen9_write_dc_state(struct parity_pw_ctx *c, uint32_t state)
{
	unsigned rewrites = 0u, rereads = 0u;
	uint32_t v;

	osdep_mmio_raw_write32(c->mmio, 0x45504u, state);
	c->dc_state_writes++;
	do {
		v = osdep_mmio_raw_read32(c->mmio, 0x45504u);
		if (v != state) {
			osdep_mmio_raw_write32(c->mmio, 0x45504u, state);
			rewrites++;
			rereads = 0u;
		} else if (rereads++ > 5u) {
			break;
		}
	} while (rewrites < 100u);
	c->dc_state_rewrites += rewrites;
	if (v != state)
		kern_logf("i915: parity Writing dc state to 0x%x failed, now 0x%x\n", state, v);
}

void
parity_gen9_set_dc_state(struct parity_pw_ctx *c, uint32_t state)
{
	uint32_t val, mask;

	if ((state & ~c->allowed_dc_mask) != 0u)
		state &= c->allowed_dc_mask;   /* drm_WARN_ON_ONCE in the reference */
	val = osdep_mmio_raw_read32(c->mmio, 0x45504u);
	mask = gen9_dc_mask(c->display_ver);
	/* "Check if DMC is ignoring our DC state requests" */
	if ((val & mask) != c->dc_state)
		kern_logf("i915: parity DC state mismatch (0x%x -> 0x%x)\n",
			c->dc_state, val & mask);
	val &= ~mask;
	val |= state;
	gen9_write_dc_state(c, val);
	c->dc_state = val & mask;
}
