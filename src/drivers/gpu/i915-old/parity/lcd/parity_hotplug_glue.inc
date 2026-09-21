/*
 * WS031 E-123 -- zedBSD glue of intel_hotplug_port.c (included at its end).  zedBSD project code.
 * The instance of the hotplug state, the objects intel_setup_outputs() would have made, the IRQ entry from
 * gen8_de_irq_handler(), the register access, the work trampoline, and the records the tests read.
 *
 * Adaptations (recorded, not hidden):
 *  - the connectors are created here, when the hotplug path starts (the reference creates them in intel_ddi_init);
 *    connector ids are their index + 1, names follow drm's "<type>-<n>";
 *  - dig_port->hpd_pulse (intel_dp_hpd_pulse), the DP connector detect (intel_dp_detect) and the TC connected check
 *    (intel_tc_port_connected) are not ported: each call is counted and logged as a step (hpd_pulse answers
 *    IRQ_HANDLED, which is what the reference answers for an eDP long pulse);
 *  - polling (drm_kms_helper_poll_*), uevents (drm_kms_helper_*hotplug_event) and the GMBUS wait queue are counted;
 *  - intel_hpd_irq_setup() copies stats[].state into the register-programming code of driver_probe.c.
 */
#include "../osdep/mmio.h"
#include "../driver_probe.h"
#include "../display_nogem.h"
#include "../power_domains.h"
#include "parity_hotplug.h"

unsigned long parity_hpd_irq_saved;
/* which kind of instance has run in this boot (the two must never share the device's interrupt path) */
static int parity_hpd_real_seen, parity_hpd_model_seen;

int parity_hpd_model_allowed(void)
{
	return !parity_hpd_real_seen;
}

static struct drm_i915_private hpd_i915;
static struct workqueue_struct hpd_unordered_wq, hpd_dp_wq;
static struct intel_digital_port hpd_ports[PARITY_HPD_MAX_CONNECTORS];
static struct intel_connector hpd_conns[PARITY_HPD_MAX_CONNECTORS];
static struct drm_connector_state hpd_conn_states[PARITY_HPD_MAX_CONNECTORS];
static char hpd_enc_names[PARITY_HPD_MAX_CONNECTORS][16];
static char hpd_conn_names[PARITY_HPD_MAX_CONNECTORS][16];
static int hpd_last_status[PARITY_HPD_MAX_CONNECTORS];

static struct {
	struct parity_hotplug *hp;
	struct osdep_mmio *m;
	struct parity_power_domains *pd;
	struct parity_pw_ctx *c;
	struct parity_hpd_fake_regs *fake;
	int pch_type, irqs_enabled;
	int started, wq_created, mutex_inited;
	int model;                              /* this instance drives fake registers (a model test) */
	volatile int live;
	volatile unsigned inflight;
	unsigned num;
	int hdmi;
	u32 cur_dig;
	unsigned irq_entries, irq_dropped, ddi_triggers, gmbus_irqs;
	unsigned hotplug_works, digport_works, reenable_works, retries_armed;
	unsigned hpd_pulse_steps, hotplug_events, irq_setups, storms, warnings;
	unsigned to_connected, to_disconnected;
	unsigned n_irq, n_hot;
	struct parity_hpd_irq_record irq[PARITY_HPD_MAX_IRQ_RECORDS];
	struct parity_hpd_hotplug_record hot[PARITY_HPD_MAX_HOTPLUG_RECORDS];
} hpd;

static const char *hpd_status_name(int s)
{
	return drm_get_connector_status_name((enum drm_connector_status)s);
}

int parity_hpd_warn(int cond, const char *what, const char *where)
{
	if (cond) {
		hpd.warnings++;
		kern_logf("i915: parity hpd WARN (%s): %s\n", where, what);
	}
	return cond;
}

uint64_t parity_hpd_sync_deadline(void)
{
	uint64_t d = 0;

	(void)kern_deadline_after(sched_ticks(), 5u * KERN_CLOCK_HZ, &d);
	return d;
}

/* ---- registers ---- */
u32 parity_hpd_read(u32 reg)
{
	if (hpd.fake != NULL) {
		switch (reg) {
		case 0xc4000u: return hpd.fake->sdeisr;
		case 0xc4030u: return hpd.fake->shotplug_ddi;
		case 0xc4034u: return hpd.fake->shotplug_tc;
		case 0xc5108u:          /* GMBUS2 */
			if (hpd.fake->gm_nak)
				return GMBUS_SATOER;
			return GMBUS_HW_RDY | GMBUS_HW_WAIT_PHASE |
				(hpd.fake->gm_active && hpd.fake->gm_left != 0u ? GMBUS_ACTIVE : 0u);
		case 0xc510cu: {        /* GMBUS3: the next 4 bytes of the sink's EDID */
			u32 v = 0u, k;

			for (k = 0u; k < 4u && hpd.fake->gm_left != 0u; k++, hpd.fake->gm_left--, hpd.fake->gm_ptr++)
				if (hpd.fake->ddc_edid != NULL && hpd.fake->gm_ptr < hpd.fake->ddc_edid_len)
					v |= (u32)hpd.fake->ddc_edid[hpd.fake->gm_ptr] << (8u * k);
			hpd.fake->gm_reads++;
			return v;
		}
		default:
			if (reg >= 0xc5100u && reg <= 0xc5120u)
				return hpd.fake->gmbus[(reg - 0xc5100u) / 4u % 6u];
			return 0u;
		}
	}
	return osdep_mmio_read32(hpd.m, reg);
}

u32 parity_hpd_rmw(u32 reg, u32 clear, u32 set)
{
	u32 old = parity_hpd_read(reg), val = (old & ~clear) | set;

	if (reg == 0xc4030u)
		hpd.cur_dig = old;
	if (hpd.fake != NULL) {
		/* the model of the hardware: the status fields (0x3 of each nibble) are write-1-to-clear */
		hpd.fake->rmw_writes++;
		hpd.fake->last_rmw_write = val;
		if (reg == 0xc4030u)
			hpd.fake->shotplug_ddi = val & ~(val & 0x33333333u);
		else if (reg == 0xc4034u)
			hpd.fake->shotplug_tc = val & ~(val & 0x33333333u);
		return old;
	}
	osdep_mmio_write32(hpd.m, reg, val);
	return old;
}

/* the model of the GMBUS controller (fake registers): a sink at 0x50 answers index reads of its EDID */
static void hpd_fake_gmbus_write(u32 reg, u32 val)
{
	struct parity_hpd_fake_regs *f = hpd.fake;

	if (reg == 0xc5104u) {                                  /* GMBUS1 */
		u32 cycle = (val >> 25) & 7u, addr = (val >> GMBUS_SLAVE_ADDR_SHIFT) & 0x7fu;

		if (val & GMBUS_SW_CLR_INT) {
			f->gm_nak = 0;
			f->gm_active = 0;
			return;
		}
		if (!(val & GMBUS_SW_RDY))
			return;
		if (cycle & 4u) {                               /* STOP */
			f->gm_active = 0;
			return;
		}
		if (!f->ddc_present || addr != 0x50u) {
			f->gm_nak = 1;
			f->gm_naks++;
			return;
		}
		if (cycle & 2u)                                 /* INDEX (| WAIT): the offset byte */
			f->gm_ptr = (val >> GMBUS_SLAVE_INDEX_SHIFT) & 0xffu;
		f->gm_left = (val & GMBUS_SLAVE_READ) ? (val >> GMBUS_BYTE_COUNT_SHIFT) & 0x1ffu : 0u;
		f->gm_active = 1;
	} else if (reg == 0xc510cu && !(f->gmbus[1] & GMBUS_SLAVE_READ)) {
		f->gm_ptr = val & 0xffu;                        /* a written offset */
	}
	if (reg >= 0xc5100u && reg <= 0xc5120u)
		f->gmbus[(reg - 0xc5100u) / 4u % 6u] = val;
}

u32 parity_hpd_write(u32 reg, u32 val)
{
	if (hpd.fake != NULL) {
		hpd_fake_gmbus_write(reg, val);
		return val;
	}
	osdep_mmio_write32(hpd.m, reg, val);
	return val;
}

/* ---- objects ---- */
struct intel_encoder *parity_hpd_encoder_at(unsigned idx)
{
	return idx < hpd.num ? &hpd_ports[idx].base : NULL;
}

struct intel_connector *parity_hpd_connector_next(struct drm_connector_list_iter *it)
{
	return it->idx < hpd.num ? &hpd_conns[it->idx++] : NULL;
}

/* ---- power ---- */
intel_wakeref_t intel_display_power_get(struct drm_i915_private *i915, enum intel_display_power_domain domain)
{
	if (hpd.pd == NULL)
		return 1;       /* the model tests: no power wells */
	return parity_display_power_get(hpd.pd, domain == POWER_DOMAIN_GMBUS ? PARITY_PW_DOMAIN_GMBUS :
		PARITY_PW_DOMAIN_DISPLAY_CORE, hpd.c) == 0 ? 1 : 0;
}

void intel_display_power_put(struct drm_i915_private *i915, enum intel_display_power_domain domain, intel_wakeref_t wf)
{
	if (hpd.pd == NULL || wf == 0)
		return;
	parity_display_power_put(hpd.pd, domain == POWER_DOMAIN_GMBUS ? PARITY_PW_DOMAIN_GMBUS :
		PARITY_PW_DOMAIN_DISPLAY_CORE, hpd.c);
}

/* ---- the surroundings ---- */
void intel_hpd_irq_setup(struct drm_i915_private *i915)
{
	int pin;

	hpd.irq_setups++;
	if (!i915->display_irqs_enabled || hpd.hp == NULL)
		return;
	for_each_hpd_pin(pin)
		hpd.hp->state[pin] = (int)i915->display.hotplug.stats[pin].state;   /* same encoding (PARITY_HPD_*) */
	if (hpd.fake == NULL)
		parity_intel_hpd_irq_setup(hpd.hp, hpd.m, hpd.pch_type, hpd.irqs_enabled);
}

void parity_hpd_gmbus_woken(void)
{
	hpd.gmbus_irqs++;   /* intel_gmbus_irq_handler(): wake_up_all(gmbus.wait_queue) */
}

bool intel_irqs_enabled(struct drm_i915_private *i915)
{
	return hpd.irqs_enabled != 0;
}

void drm_kms_helper_poll_reschedule(struct drm_device *dev)
{
	hpd.storms++;
	kern_logf("i915: parity hpd step drm_kms_helper_poll_reschedule (connector polling not ported)\n");
}

void drm_kms_helper_connector_hotplug_event(struct drm_connector *connector)
{
	kern_logf("i915: parity hpd uevent (connector hotplug event, no userspace listener): %s\n", connector->name);
}

void drm_kms_helper_hotplug_event(struct drm_device *dev)
{
	kern_logf("i915: parity hpd uevent (hotplug event, no userspace listener)\n");
}

static void i915_hpd_poll_init_work(struct work_struct *work)
{
	/* run inline by parity_intel_hpd_poll_disable() at probe (driver_probe.c); never queued here */
}

static enum irqreturn hpd_dp_pulse_step(struct intel_digital_port *dig_port, bool long_hpd)
{
	hpd.hpd_pulse_steps++;
	kern_logf("i915: parity hpd step intel_dp_hpd_pulse (unported): %s %s pulse -> IRQ_HANDLED\n",
		dig_port->base.base.name, long_hpd ? "long" : "short");
	return IRQ_HANDLED;
}

/* intel_dp_connector_helper_funcs.detect_ctx = intel_dp_detect (unported step); intel_dp_connector_funcs has no .detect */
static int hpd_dp_detect_step(struct drm_connector *connector, struct drm_modeset_acquire_ctx *ctx, bool force)
{
	kern_logf("i915: parity hpd step intel_dp_detect (unported): %s -> unknown\n", connector->name);
	return connector_status_unknown;
}
static const struct drm_connector_funcs hpd_dp_connector_funcs = { .detect = NULL };
static const struct drm_connector_helper_funcs hpd_dp_helper_funcs = { .detect_ctx = hpd_dp_detect_step };
/* intel_hdmi_connector_helper_funcs: no .detect_ctx (the connector funcs' .detect = intel_hdmi_detect is used) */
static const struct drm_connector_helper_funcs hpd_hdmi_helper_funcs = { .detect_ctx = NULL };

static bool hpd_tc_connected_step(struct intel_encoder *encoder)
{
	kern_logf("i915: parity hpd step intel_tc_port_connected (unported): %s -> false\n", encoder->base.name);
	return false;
}

/* each encoder->hotplug() call is recorded around the reference's intel_ddi_hotplug() */
static enum intel_hotplug_state hpd_hotplug_recorded(struct intel_encoder *encoder, struct intel_connector *connector)
{
	unsigned idx = (unsigned)(connector - hpd_conns);
	int old = connector->base.status, live;
	struct parity_hpd_edid_info ei;
	enum intel_hotplug_state st = parity_hpd_ddi_hotplug_fn()(encoder, connector);

	live = (parity_hpd_read(0xc4000u) & hpd_i915.display.hotplug.pch_hpd[encoder->hpd_pin]) != 0u;
	hpd.hotplug_events++;
	if (st == INTEL_HOTPLUG_RETRY)
		hpd.retries_armed++;
	if (hpd.n_hot < PARITY_HPD_MAX_HOTPLUG_RECORDS) {
		struct parity_hpd_hotplug_record *r = &hpd.hot[hpd.n_hot++];

		r->tick = sched_ticks(); r->connector = idx; r->pin = encoder->hpd_pin; r->retries = connector->hotplug_retries;
		r->old_status = old; r->new_status = connector->base.status; r->state = st; r->live = live;
		parity_hpd_edid_info(&ei);
		r->edid_rc = ei.rc; r->edid_blocks = ei.blocks;
	}
	if ((int)idx == hpd.hdmi && old != (int)connector->base.status) {
		if (connector->base.status == connector_status_connected)
			hpd.to_connected++;
		else if (connector->base.status == connector_status_disconnected)
			hpd.to_disconnected++;
	}
	parity_hpd_edid_info(&ei);
	/* the status the connector adopted, and what it was decided from: the live status and the EDID read */
	kern_logf("i915: parity HPD-EVENT %s pin=%d retry=%d status %s -> %s (%s) | live=%d EDID rc=%d blocks=%u "
		"digital=%u | epoch=%llu t=%llu\n", connector->base.name, encoder->hpd_pin, connector->hotplug_retries,
		hpd_status_name(old), hpd_status_name(connector->base.status),
		st == INTEL_HOTPLUG_CHANGED ? "CHANGED" : st == INTEL_HOTPLUG_RETRY ? "RETRY" : "UNCHANGED", live,
		ei.rc, ei.blocks, ei.digital, (unsigned long long)connector->base.epoch_counter,
		(unsigned long long)sched_ticks());
	return st;
}

void parity_hpd_work_trampoline(void *ctx)
{
	struct work_struct *w = ctx;

	if (w == &hpd_i915.display.hotplug.hotplug_work.work)
		hpd.hotplug_works++;
	else if (w == &hpd_i915.display.hotplug.dig_port_work)
		hpd.digport_works++;
	else if (w == &hpd_i915.display.hotplug.reenable_work.work)
		hpd.reenable_works++;
	w->func(w);
}

/* ---- lifecycle ---- */
static void hpd_make_objects(const struct parity_display_nogem *nogem)
{
	unsigned i, n_dp = 0u, n_hdmi = 0u, n_edp = 0u;

	hpd.num = 0u;
	hpd.hdmi = -1;
	for (i = 0u; i < nogem->num_encoders && hpd.num < PARITY_HPD_MAX_CONNECTORS; i++) {
		const struct parity_encoder *pe = &nogem->encoders[i];
		struct intel_digital_port *dp = &hpd_ports[hpd.num];
		struct intel_connector *ic = &hpd_conns[hpd.num];
		int is_edp = pe->init_dp && (pe->device_type & 0x1000u) != 0u && pe->port == PARITY_PORT_A;

		memset(dp, 0, sizeof(*dp));
		memset(ic, 0, sizeof(*ic));
		hpd_enc_names[hpd.num][0] = 'D'; hpd_enc_names[hpd.num][1] = 'D'; hpd_enc_names[hpd.num][2] = 'I';
		hpd_enc_names[hpd.num][3] = ' '; hpd_enc_names[hpd.num][4] = (char)('A' + pe->port);
		hpd_enc_names[hpd.num][5] = '\0';
		dp->base.base.dev = &hpd_i915.drm;
		dp->base.base.base.id = (int)hpd.num + 1;
		dp->base.base.name = hpd_enc_names[hpd.num];
		dp->base.type = is_edp ? INTEL_OUTPUT_EDP : INTEL_OUTPUT_DDI;
		dp->base.port = (enum port)pe->port;
		dp->base.hpd_pin = (enum hpd_pin)parity_intel_ddi_hpd_pin(13, pe->port);
		dp->base.hotplug = hpd_hotplug_recorded;             /* intel_ddi_hotplug, recorded */
		dp->connected = pe->is_tc ? hpd_tc_connected_step : parity_hpd_lpt_connected_fn();
		if (pe->init_dp)
			dp->hpd_pulse = hpd_dp_pulse_step;               /* intel_dp_hpd_pulse: unported step */

		ic->encoder = &dp->base;
		ic->base.dev = &hpd_i915.drm;
		ic->base.base.id = (int)hpd.num + 1;
		ic->base.status = connector_status_unknown;          /* drm_connector_init() */
		ic->base.state = &hpd_conn_states[hpd.num];
		hpd_conn_states[hpd.num].crtc = NULL;                /* no crtc drives it */
		if (pe->init_hdmi && !pe->init_dp) {
			ic->base.connector_type = DRM_MODE_CONNECTOR_HDMIA;
			ic->base.funcs = parity_hpd_hdmi_connector_funcs();
			ic->base.helper_private = &hpd_hdmi_helper_funcs;    /* drm_connector_helper_add() */
			/* intel_hdmi_init_connector(): ddc = intel_gmbus_get_adapter(ddc_pin); port B -> GMBUS_PIN_2_BXT (the
			 * VBT's ddc_pin 2 maps to the same pin) */
			ic->base.ddc = parity_hpd_gmbus_adapter(&hpd_i915, pe->port == PARITY_PORT_B ? GMBUS_PIN_2_BXT :
				(unsigned)(1 + pe->port));
			ic->polled = DRM_CONNECTOR_POLL_HPD;
			dp->hdmi.attached_connector = ic;
			memcpy(hpd_conn_names[hpd.num], "HDMI-A-", 7);
			hpd_conn_names[hpd.num][7] = (char)('1' + n_hdmi++);
			hpd_conn_names[hpd.num][8] = '\0';
			if (hpd.hdmi < 0)
				hpd.hdmi = (int)hpd.num;
		} else if (is_edp) {
			ic->base.connector_type = DRM_MODE_CONNECTOR_eDP;
			ic->base.funcs = &hpd_dp_connector_funcs;
			ic->base.helper_private = &hpd_dp_helper_funcs;      /* drm_connector_helper_add() */
			dp->dp.attached_connector = ic;
			memcpy(hpd_conn_names[hpd.num], "eDP-", 4);
			hpd_conn_names[hpd.num][4] = (char)('1' + n_edp++);
			hpd_conn_names[hpd.num][5] = '\0';
		} else {
			ic->base.connector_type = DRM_MODE_CONNECTOR_DisplayPort;
			ic->base.funcs = &hpd_dp_connector_funcs;
			ic->base.helper_private = &hpd_dp_helper_funcs;      /* drm_connector_helper_add() */
			ic->polled = DRM_CONNECTOR_POLL_HPD;
			dp->dp.attached_connector = ic;
			memcpy(hpd_conn_names[hpd.num], "DP-", 3);
			hpd_conn_names[hpd.num][3] = (char)('1' + n_dp++);
			hpd_conn_names[hpd.num][4] = '\0';
		}
		ic->base.polled = ic->polled;                        /* i915_hpd_poll_init_work(): HPD enabled */
		ic->base.name = hpd_conn_names[hpd.num];
		hpd_last_status[hpd.num] = connector_status_unknown;
		hpd.num++;
	}
}

int parity_hpd_start(struct parity_hotplug *hp, struct osdep_mmio *m, const struct parity_display_nogem *nogem,
	struct parity_power_domains *pd, struct parity_pw_ctx *c, int pch_type, int intel_irqs_enabled,
	struct parity_hpd_fake_regs *fake)
{
	int pin;

	if (hpd.started || hp == NULL || nogem == NULL || (m == NULL && fake == NULL))
		return -EINVAL;
	/*
	 * The model tests must not take over the state the device's own interrupt handler reaches: while the hardware's
	 * interrupts are installed, an SDE interrupt would enter the model instance (and its locks) on any CPU.  Once a
	 * real instance has run in this boot, a model instance is refused; the model tests then say so and do not run.
	 */
	if (fake != NULL && parity_hpd_real_seen)
		return -EBUSY;
	if (fake == NULL && parity_hpd_model_seen)
		return -EBUSY;
	memset(&hpd, 0, sizeof(hpd));
	memset(&hpd_i915, 0, sizeof(hpd_i915));
	hpd.hp = hp; hpd.m = m; hpd.pd = pd; hpd.c = c; hpd.fake = fake;
	hpd.pch_type = pch_type; hpd.irqs_enabled = intel_irqs_enabled;
	if (parity_kworkqueue_create(&hpd_unordered_wq.wq, "i915-unordered") != 0)
		return -ENOMEM;
	if (parity_ktimerq_create(&hpd_unordered_wq.tq, &hpd_unordered_wq.wq, "i915-unordered-timer") != 0) {
		parity_kworkqueue_destroy(&hpd_unordered_wq.wq);
		return -ENOMEM;
	}
	if (parity_kworkqueue_create(&hpd_dp_wq.wq, "i915-dp") != 0) {
		parity_ktimerq_destroy(&hpd_unordered_wq.tq);
		parity_kworkqueue_destroy(&hpd_unordered_wq.wq);
		return -ENOMEM;
	}
	hpd.wq_created = 1;
	spin_init(&hpd_i915.irq_lock, LOCK_RANK_DEVICE, "i915-irq");
	(void)mutex_init(&hpd_i915.drm.mode_config.mutex, LOCK_RANK_DEVICE, "drm-mode-config");
	hpd_i915.unordered_wq = &hpd_unordered_wq;
	hpd_i915.display.hotplug.dp_wq = &hpd_dp_wq;
	hpd_i915.display_irqs_enabled = true;
	hpd_i915.display.funcs.hotplug = &hpd_i915;             /* gen11_hpd_funcs: non-NULL */
	hpd_i915.display.hotplug.hpd = hp->hpd;                 /* intel_hpd_init_pins() */
	hpd_i915.display.hotplug.pch_hpd = hp->pch_hpd;
	intel_hpd_init_early(&hpd_i915);
	parity_hpd_gmbus_forget();
	parity_hpd_edid_forget();
	hpd_make_objects(nogem);
	/* intel_hpd_init(): stats reset, every pin enabled (the registers were programmed at P7) */
	for_each_hpd_pin(pin) {
		hpd_i915.display.hotplug.stats[pin].count = 0;
		hpd_i915.display.hotplug.stats[pin].state = HPD_ENABLED;
	}
	hpd.started = 1;
	hpd.model = fake != NULL;
	if (fake != NULL)
		parity_hpd_model_seen = 1;
	else
		parity_hpd_real_seen = 1;
	__atomic_store_n(&hpd.live, 1, __ATOMIC_SEQ_CST);
	kern_logf("i915: parity hpd start: connectors=%u hdmi=%s storm_threshold=%u short_storm=%u backend=%s\n", hpd.num,
		hpd.hdmi >= 0 ? hpd_conn_names[hpd.hdmi] : "-", hpd_i915.display.hotplug.hpd_storm_threshold,
		hpd_i915.display.hotplug.hpd_short_storm_enabled, fake != NULL ? "model" : "mmio");
	return 0;
}

static void hpd_icp_entry(uint32_t sde_iir);
unsigned parity_hpd_hw_irqs_during_model;

/* gen8_de_irq_handler()'s PCH branch (IRQ context).  A model instance (the ktest) is never fed by the hardware. */
void parity_hpd_pch_irq(uint32_t sde_iir)
{
	if (hpd.fake != NULL) {
		parity_hpd_hw_irqs_during_model++;     /* not the model's own counters */
		return;
	}
	hpd_icp_entry(sde_iir);
}

/* the model tests' interrupt: with interrupts disabled, as the reference's handler runs (a thread that takes the
 * irq_lock with plain spin_lock and is then interrupted on the same CPU would deadlock against the lock) */
static struct spinlock hpd_model_irq_lock;
static int hpd_model_irq_lock_inited;

void parity_hpd_model_irq(uint32_t sde_iir)
{
	unsigned long f;

	if (hpd.fake == NULL)
		return;
	if (!hpd_model_irq_lock_inited) {
		spin_init(&hpd_model_irq_lock, LOCK_RANK_DEVICE, "hpd-model-irq");
		hpd_model_irq_lock_inited = 1;
	}
	f = spin_lock_irqsave(&hpd_model_irq_lock);
	hpd_icp_entry(sde_iir);
	spin_unlock_irqrestore(&hpd_model_irq_lock, f);
}

static void hpd_icp_entry(uint32_t sde_iir)
{
	unsigned n;

	__atomic_add_fetch(&hpd.inflight, 1u, __ATOMIC_SEQ_CST);
	if (!__atomic_load_n(&hpd.live, __ATOMIC_SEQ_CST)) {
		hpd.irq_dropped++;
		__atomic_sub_fetch(&hpd.inflight, 1u, __ATOMIC_SEQ_CST);
		return;
	}
	hpd.irq_entries++;
	hpd.cur_dig = 0u;
	if (sde_iir & SDE_DDI_HOTPLUG_MASK_ICP)
		hpd.ddi_triggers++;
	icp_irq_handler(&hpd_i915, sde_iir);
	n = hpd.n_irq;
	if (n < PARITY_HPD_MAX_IRQ_RECORDS && (sde_iir & (SDE_DDI_HOTPLUG_MASK_ICP | SDE_TC_HOTPLUG_MASK_ICP))) {
		hpd.irq[n].tick = sched_ticks();
		hpd.irq[n].sde_iir = sde_iir;
		hpd.irq[n].shotplug_ddi = hpd.cur_dig;
		hpd.irq[n].event_bits_after = hpd_i915.display.hotplug.event_bits;
		hpd.n_irq = n + 1u;
	}
	__atomic_sub_fetch(&hpd.inflight, 1u, __ATOMIC_SEQ_CST);
}

void parity_hpd_stop(void)
{
	unsigned spins = 0u;

	if (!hpd.started)
		return;
	__atomic_store_n(&hpd.live, 0, __ATOMIC_SEQ_CST);
	while (__atomic_load_n(&hpd.inflight, __ATOMIC_SEQ_CST) != 0u && spins++ < 1000000u)
		__asm__ volatile("pause");
	intel_hpd_cancel_work(&hpd_i915);
	if (hpd.wq_created) {
		parity_kworkqueue_destroy(&hpd_dp_wq.wq);
		parity_ktimerq_destroy(&hpd_unordered_wq.tq);
		parity_kworkqueue_destroy(&hpd_unordered_wq.wq);
		hpd.wq_created = 0;
	}
	hpd.started = 0;
	kern_logf("i915: parity hpd stop: irq entries=%u dropped=%u works hotplug=%u digport=%u reenable=%u events=%u "
		"warnings=%u\n", hpd.irq_entries, hpd.irq_dropped, hpd.hotplug_works, hpd.digport_works, hpd.reenable_works,
		hpd.hotplug_events, hpd.warnings);
}

int parity_hpd_probe_connector(unsigned idx)
{
	struct intel_connector *ic;
	int status;

	if (!hpd.started || idx >= hpd.num)
		return -EINVAL;
	ic = &hpd_conns[idx];
	mutex_lock(&hpd_i915.drm.mode_config.mutex);
	status = drm_helper_probe_detect(&ic->base, NULL, false);
	if (!ic->base.force)
		ic->base.status = status;
	mutex_unlock(&hpd_i915.drm.mode_config.mutex);
	return status;
}

const char *parity_hpd_connector_name(unsigned idx)
{
	return idx < hpd.num ? hpd_conn_names[idx] : "-";
}

void parity_hpd_summary(struct parity_hpd_summary *s)
{
	memset(s, 0, sizeof(*s));
	s->started = hpd.started; s->live = hpd.live; s->num_connectors = hpd.num;
	s->irq_entries = hpd.irq_entries; s->irq_dropped = hpd.irq_dropped; s->ddi_triggers = hpd.ddi_triggers;
	s->gmbus_irqs = hpd.gmbus_irqs; s->hotplug_works = hpd.hotplug_works; s->digport_works = hpd.digport_works;
	s->reenable_works = hpd.reenable_works; s->retries_armed = hpd.retries_armed;
	s->hpd_pulse_steps = hpd.hpd_pulse_steps; s->hotplug_events = hpd.hotplug_events;
	s->irq_setups = hpd.irq_setups; s->storms = hpd.storms; s->warnings = hpd.warnings;
	s->irq_records = hpd.n_irq; s->hotplug_records = hpd.n_hot;
	s->to_connected = hpd.to_connected; s->to_disconnected = hpd.to_disconnected;
	s->hdmi_connector = hpd.hdmi;
	s->hdmi_status = hpd.hdmi >= 0 ? (int)hpd_conns[hpd.hdmi].base.status : 0;
	s->hdmi_epoch = hpd.hdmi >= 0 ? (unsigned long long)hpd_conns[hpd.hdmi].base.epoch_counter : 0ull;
}

const struct parity_hpd_irq_record *parity_hpd_irq_record(unsigned i)
{
	return i < hpd.n_irq ? &hpd.irq[i] : NULL;
}

const struct parity_hpd_hotplug_record *parity_hpd_hotplug_record(unsigned i)
{
	return i < hpd.n_hot ? &hpd.hot[i] : NULL;
}

/* the model tests: run the armed storm re-enable work now (it is armed for HPD_STORM_REENABLE_DELAY) */
int parity_hpd_flush_reenable(void)
{
	struct delayed_work *d = &hpd_i915.display.hotplug.reenable_work;

	return d->work.q != NULL ? parity_kdelayed_flush(&d->work.q->tq, &d->dw, parity_hpd_sync_deadline()) : -ENOENT;
}

uint32_t parity_hpd_event_bits(void) { return hpd_i915.display.hotplug.event_bits; }
uint32_t parity_hpd_retry_bits(void) { return hpd_i915.display.hotplug.retry_bits; }
int parity_hpd_pin_state(int pin) { return pin > HPD_NONE && pin < HPD_NUM_PINS ? (int)hpd_i915.display.hotplug.stats[pin].state : -1; }
int parity_hpd_pin_count(int pin) { return pin > HPD_NONE && pin < HPD_NUM_PINS ? hpd_i915.display.hotplug.stats[pin].count : -1; }
int parity_hpd_connector_polled(unsigned idx) { return idx < hpd.num ? hpd_conns[idx].base.polled : -1; }
int parity_hpd_connector_status(unsigned idx) { return idx < hpd.num ? (int)hpd_conns[idx].base.status : -1; }
