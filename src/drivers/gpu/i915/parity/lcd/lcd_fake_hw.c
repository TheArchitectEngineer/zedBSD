/*
 * WS031 Linux-parity — register / sink model for the LCD modeset (see lcd_fake_hw.h).  zedBSD project
 * code; test support only.  Register offsets and bits are the public ones of the display engine
 * (ICL+ combo PLL enable, DDI_BUF_CTL, TGL+ DP_TP_CTL / STATUS, TRANSCONF, PIPEDSL, frame counter,
 * PLANE_CTL / SURF, DPCLKA_CFGCR0) and of the DP standard's DPCD (0x100.., 0x202..).
 */
#include "lcd_fake_hw.h"
#include "../dp/parity_edp.h"
#include "lcd_power_domain_enum.h"      /* reference, extracted: the domain numbers the ops carry */
#include <string.h>

#define REG_DPLL_ENABLE(id)   (0x46010u + 4u * (unsigned)(id))
#define  PLL_ENABLE           (1u << 31)
#define  PLL_LOCK             (1u << 30)
#define  PLL_POWER_ENABLE     (1u << 27)
#define  PLL_POWER_STATE      (1u << 26)
#define REG_DPCLKA_CFGCR0     0x164280u
#define REG_DDI_BUF_CTL(port) (0x64000u + 0x100u * (unsigned)(port))
#define  DDI_BUF_CTL_ENABLE   (1u << 31)
#define  DDI_BUF_IS_IDLE      (1u << 7)
#define REG_DP_TP_CTL(t)      (0x60540u + 0x1000u * (unsigned)(t))
#define  DP_TP_CTL_ENABLE     (1u << 31)
#define  DP_TP_CTL_TRAIN_MASK (7u << 8)
#define REG_DP_TP_STATUS(t)   (0x60544u + 0x1000u * (unsigned)(t))
#define  DP_TP_STATUS_IDLE_DONE (1u << 25)
#define REG_TRANSCONF(p)      (0x70008u + 0x1000u * (unsigned)(p))
#define  TRANSCONF_ENABLE     (1u << 31)
#define  TRANSCONF_STATE      (1u << 30)
#define REG_PIPEDSL(p)        (0x70000u + 0x1000u * (unsigned)(p))
#define REG_PIPE_FRMCNT(p)    (0x70040u + 0x1000u * (unsigned)(p))
#define REG_PLANE_CTL(p)      (0x70180u + 0x1000u * (unsigned)(p))
#define REG_PLANE_SURF(p)     (0x7019cu + 0x1000u * (unsigned)(p))
#define  PLANE_CTL_ENABLE     (1u << 31)
#define REG_PLANE_SURFLIVE(p) (0x701acu + 0x1000u * (unsigned)(p))
#define REG_PLANE_WM0(p)      (0x70240u + 0x1000u * (unsigned)(p))
#define REG_PLANE_BUF_CFG(p)  (0x7027cu + 0x1000u * (unsigned)(p))
#define REG_PIPESTATUS(p)     (0x70058u + 0x1000u * (unsigned)(p))     /* ICL_PIPESTATUS: write-one-to-clear */
#define  PIPESTATUS_UNDERRUN  (1u << 31)
#define REG_DE_PIPE_IMR(p)    (0x44404u + 0x10u * (unsigned)(p))
#define REG_MBUS_CTL          0x4438cu
#define  MBUS_JOIN_BIT        (1u << 31)
#define DBUF_POWER_REQUEST    (1u << 31)
#define DBUF_POWER_STATE      (1u << 30)
#define PPC_POWER_ON          1u

static const uint32_t dbuf_ctl_reg[4] = { 0x45008u, 0x44fe8u, 0x44300u, 0x44304u };

/* the slices a DDB range [start, last] lies in: with MBUS joined the pipe sees all four as one buffer, else two */
static uint8_t slices_of_range(const struct lcd_fake_hw *hw, uint32_t start, uint32_t last)
{
	uint32_t per = hw->dbuf_size / 4u;
	uint8_t m = 0u;
	unsigned s;

	if (per == 0u)
		return 0u;
	for (s = 0; s < 4u; s++)
		if (start < (s + 1u) * per && last >= s * per)
			m |= (uint8_t)(1u << s);
	return m;
}

static int dc_off_held(const struct lcd_fake_hw *hw)
{
	return hw->power_refs[POWER_DOMAIN_DC_OFF] > 0;
}

static uint32_t *slot(struct lcd_fake_hw *hw, uint32_t reg, int create)
{
	unsigned i;

	for (i = 0; i < hw->nregs; i++)
		if (hw->regs[i].reg == reg)
			return &hw->regs[i].val;
	if (!create)
		return 0;
	if (hw->nregs >= LCD_FAKE_MAX_REGS) {
		hw->regs_overflow++;
		return &hw->regs[LCD_FAKE_MAX_REGS - 1u].val;
	}
	hw->regs[hw->nregs].reg = reg;
	hw->regs[hw->nregs].val = 0u;
	return &hw->regs[hw->nregs++].val;
}

uint32_t lcd_fake_reg(const struct lcd_fake_hw *hw, uint32_t reg)
{
	unsigned i;

	for (i = 0; i < hw->nregs; i++)
		if (hw->regs[i].reg == reg)
			return hw->regs[i].val;
	return 0u;
}

static int pll_locked(const struct lcd_fake_hw *hw)
{
	return (lcd_fake_reg(hw, REG_DPLL_ENABLE(hw->dpll_id)) & PLL_LOCK) != 0u;
}

static int ddi_clock_on(const struct lcd_fake_hw *hw)
{
	/* ICL_DPCLKA_CFGCR0_DDI_CLK_OFF(phy): bit 10 for PHY A, 11 for PHY B */
	return (lcd_fake_reg(hw, REG_DPCLKA_CFGCR0) & (1u << (10 + hw->port))) == 0u;
}

static uint32_t frame_of(const struct lcd_fake_hw *hw)
{
	return hw->pipe_on_since_us != 0u ? (uint32_t)((hw->dpf->now_us - hw->pipe_on_since_us) * 60u / 1000000u) : 0u;
}

/* the next frame boundary after now */
static void to_next_frame(struct lcd_fake_hw *hw)
{
	uint32_t f = frame_of(hw) + 1u;

	if (hw->pipe_on_since_us != 0u)
		hw->dpf->now_us = hw->pipe_on_since_us + ((uint64_t)f * 1000000u + 59u) / 60u;
}

static uint32_t f_read32(void *ctx, uint32_t reg)
{
	struct lcd_fake_hw *hw = ctx;

	if (reg == REG_PLANE_SURFLIVE(hw->pipe)) {
		if (hw->surf_pending_valid && !hw->fault_flip_never_latch && frame_of(hw) > hw->surf_pending_frame) {
			hw->surf_live = hw->surf_pending;
			hw->surf_pending_valid = 0;
		}
		return hw->surf_live;
	}

	if (reg == REG_PIPEDSL(hw->pipe)) {
		if (hw->scanline_hold_reads != 0u) {
			hw->scanline_hold_reads--;
			hw->scanline = hw->scanline_hold;
			return hw->scanline;
		}
		if (hw->pipe_on_since_us != 0u)
			hw->scanline = (hw->scanline + 97u) % (hw->vtotal != 0u ? hw->vtotal : 1u);
		return hw->scanline;
	}
	if (reg == REG_PIPE_FRMCNT(hw->pipe)) {
		if (hw->plane_arms != 0u && ++hw->frame_reads_after_arm == (unsigned)hw->fault_underrun_steady_after)
			*slot(hw, REG_PIPESTATUS(hw->pipe), 1) |= PIPESTATUS_UNDERRUN;
		if (hw->fault_frame_counter_frozen)
			return 7u;
		return hw->pipe_on_since_us != 0u ? (uint32_t)((hw->dpf->now_us - hw->pipe_on_since_us) * 60u / 1000000u) : 0u;
	}
	/* pipe A..D interrupt registers live in the pipe's power well: off = reads 0 (the well's enable programs the mask) */
	if (reg == REG_DE_PIPE_IMR(hw->pipe) && hw->power_refs[POWER_DOMAIN_PIPE_A + hw->pipe] <= 0)
		return 0u;
	return lcd_fake_reg(hw, reg);
}

static void f_write32(void *ctx, uint32_t reg, uint32_t value)
{
	struct lcd_fake_hw *hw = ctx;
	uint32_t *v, old;

	if (hw->fault_drop_write_reg != 0u && reg == hw->fault_drop_write_reg)
		return;
	v = slot(hw, reg, 1);
	old = *v;

	if (reg == REG_PIPESTATUS(hw->pipe)) {
		*v = old & ~value;                      /* write-one-to-clear */
		return;
	}
	if (reg == REG_DPLL_ENABLE(hw->dpll_id)) {
		if ((value & PLL_ENABLE) && !(old & PLL_ENABLE) && !dc_off_held(hw))
			hw->modeset_without_dc_off++;
		value &= ~(PLL_LOCK | PLL_POWER_STATE);
		if (value & PLL_POWER_ENABLE)
			value |= PLL_POWER_STATE;
		if ((value & PLL_ENABLE) && (value & PLL_POWER_STATE) && !hw->fault_pll_no_lock)
			value |= PLL_LOCK;
		if ((old & PLL_ENABLE) && !(value & PLL_ENABLE) && hw->pipe_on_since_us != 0u)
			hw->pll_disabled_with_pipe_on++;
	} else if (reg == REG_DDI_BUF_CTL(hw->port)) {
		if (value & DDI_BUF_CTL_ENABLE) {
			value &= ~DDI_BUF_IS_IDLE;
			if (!(old & DDI_BUF_CTL_ENABLE) && (!pll_locked(hw) || !ddi_clock_on(hw)))
				hw->ddi_enabled_without_pll++;
		} else {
			value |= DDI_BUF_IS_IDLE;
		}
	} else if (reg == REG_DP_TP_CTL(hw->pipe)) {
		uint32_t *st = slot(hw, REG_DP_TP_STATUS(hw->pipe), 1);

		/* idle pattern requested: the hardware reports "idle done" */
		if ((value & DP_TP_CTL_ENABLE) && (value & DP_TP_CTL_TRAIN_MASK) == (2u << 8))
			*st |= DP_TP_STATUS_IDLE_DONE;
		else
			*st &= ~DP_TP_STATUS_IDLE_DONE;
	} else if (reg == REG_DP_TP_STATUS(hw->pipe)) {
		value = old & ~value;                   /* write-one-to-clear */
	} else if (reg == REG_TRANSCONF(hw->pipe)) {
		value &= ~TRANSCONF_STATE;
		if (value & TRANSCONF_ENABLE) {
			value |= TRANSCONF_STATE;
			if (!(old & TRANSCONF_ENABLE)) {
				hw->pipe_on_since_us = hw->dpf->now_us != 0u ? hw->dpf->now_us : 1u;
				if (!(lcd_fake_reg(hw, REG_DDI_BUF_CTL(hw->port)) & DDI_BUF_CTL_ENABLE))
					hw->pipe_enabled_without_link++;
				if (!dc_off_held(hw))
					hw->modeset_without_dc_off++;
				if (hw->power_refs[POWER_DOMAIN_PIPE_A + hw->pipe] <= 0 || hw->power_refs[POWER_DOMAIN_TRANSCODER_A + hw->pipe] <= 0 ||
				    hw->power_refs[POWER_DOMAIN_PORT_DDI_LANES_A + hw->port] <= 0 || hw->power_refs[POWER_DOMAIN_DISPLAY_CORE] <= 0)
					hw->pipe_enabled_without_power++;
			}
		} else if (hw->fault_pipe_stuck_on && (old & TRANSCONF_STATE)) {
			value |= TRANSCONF_STATE;               /* the pipe does not stop */
		} else {
			hw->pipe_on_since_us = 0u;
		}
	} else if (reg == REG_PLANE_SURF(hw->pipe)) {
		uint32_t ctl = lcd_fake_reg(hw, REG_PLANE_CTL(hw->pipe));

		/* double buffered: live at the next frame boundary (the first arm of a starting pipe latches at once) */
		if (hw->surf_live == 0u || hw->pipe_on_since_us == 0u) {
			hw->surf_live = value;
		} else {
			hw->surf_pending = value;
			hw->surf_pending_valid = 1;
			hw->surf_pending_frame = frame_of(hw);
		}
		if (!hw->irq_off)
			hw->arm_outside_section++;
		if (ctl & PLANE_CTL_ENABLE) {
			hw->plane_arms++;
			hw->plane_ctl_at_arm = ctl;
			hw->plane_surf_at_arm = value;
			if (hw->pipe_on_since_us == 0u)
				hw->plane_armed_without_pipe++;
			{
				/* PLANE_BUF_CFG: start bits 11:0, end (last block) bits 27:16 -- 12-bit fields on display version 13 */
				uint32_t cfg = lcd_fake_reg(hw, REG_PLANE_BUF_CFG(hw->pipe));
				uint32_t start = cfg & 0xfffu, last = (cfg >> 16) & 0xfffu;

				if (cfg == 0u || last < start || (hw->dbuf_size != 0u && last >= hw->dbuf_size) ||
				    !(lcd_fake_reg(hw, REG_PLANE_WM0(hw->pipe)) & (1u << 31)))
					hw->plane_armed_without_ddb++;
				else if ((slices_of_range(hw, start, last) & ~hw->dbuf_enabled) != 0u ||
					 ((slices_of_range(hw, start, last) & 0x0cu) != 0u && hw->pipe == 0 &&
					  !(lcd_fake_reg(hw, REG_MBUS_CTL) & MBUS_JOIN_BIT)))
					hw->plane_armed_outside_slices++;
				if (!dc_off_held(hw))
					hw->modeset_without_dc_off++;
				if (hw->fault_underrun_at_arm)
					*slot(hw, REG_PIPESTATUS(hw->pipe), 1) |= PIPESTATUS_UNDERRUN;
			}
		} else {
			hw->plane_disarms++;
		}
	}
	*v = value;
}

static uint32_t f_rmw32(void *ctx, uint32_t reg, uint32_t clear, uint32_t set)
{
	uint32_t old = f_read32(ctx, reg);

	f_write32(ctx, reg, (old & ~clear) | set);
	return old;
}

static int f_wait_reg(void *ctx, uint32_t reg, uint32_t mask, uint32_t value, unsigned timeout_ms)
{
	struct lcd_fake_hw *hw = ctx;

	if (hw->fault_time_base_reg != 0u && reg == hw->fault_time_base_reg) {
		parity_lcd_backend_fault("model: time base fault during a register wait (not a timeout)" "\n");
		return PARITY_LCD_EIO;
	}
	/* status follows control at once in this model: either it is there, or the whole timeout passes */
	if ((f_read32(ctx, reg) & mask) == value)
		return 0;
	hw->dpf->now_us += (uint64_t)timeout_ms * 1000u;
	return (f_read32(ctx, reg) & mask) == value ? 0 : -110;
}

static void f_sleep(void *ctx, unsigned us)
{
	struct lcd_fake_hw *hw = ctx;

	hw->dpf->now_us += us;
}

static long f_dpcd_read(void *ctx, unsigned offset, uint8_t *buf, size_t size)
{
	(void)ctx;
	return parity_edp_dpcd_read(offset, buf, size);
}

static long f_dpcd_write(void *ctx, unsigned offset, const uint8_t *buf, size_t size)
{
	(void)ctx;
	return parity_edp_dpcd_write(offset, buf, size);
}

static int f_read_dpcd_caps(void *ctx, uint8_t dpcd[15])
{
	(void)ctx;
	return parity_edp_read_dpcd_caps(dpcd);
}

static int f_panel(void *ctx, int op)
{
	(void)ctx;
	return parity_edp_panel_op(op);
}

static int f_power_get(void *ctx, int domain)
{
	struct lcd_fake_hw *hw = ctx;

	if (domain < 0 || domain >= LCD_FAKE_MAX_DOMAINS || hw->fault_power_get == domain + 1)
		return 0;
	hw->power_refs[domain]++;
	hw->power_gets++;
	return domain + 1;
}

static void f_power_put(void *ctx, int domain, int wakeref)
{
	struct lcd_fake_hw *hw = ctx;

	(void)wakeref;
	hw->power_puts++;
	if (hw->fault_put_refused_domain == domain + 1) {
		parity_lcd_backend_fault("model: power well kept on (pipe interrupt drain failed)" "\n");
		return;                         /* the reference stays held */
	}
	if (domain < 0 || domain >= LCD_FAKE_MAX_DOMAINS || hw->power_refs[domain] <= 0) {
		hw->power_underflows++;
		return;
	}
	hw->power_refs[domain]--;
	if (hw->power_refs[domain] == 0 && hw->pipe_on_since_us != 0u &&
	    (domain == POWER_DOMAIN_PIPE_A + hw->pipe || domain == POWER_DOMAIN_TRANSCODER_A + hw->pipe))
		hw->power_dropped_with_pipe_on++;
}

static void f_power_put_async(void *ctx, int domain, int wakeref, int delay_ms)
{
	struct lcd_fake_hw *hw = ctx;

	hw->async_puts++;
	hw->last_async_delay_ms = delay_ms;
	f_power_put(ctx, domain, wakeref);
}

/* gen9_dbuf_slices_update(): request bit -> state bit, slice by slice */
static void f_dbuf_slices_update(void *ctx, unsigned req_slices)
{
	struct lcd_fake_hw *hw = ctx;
	unsigned s;

	hw->dbuf_updates++;
	if (hw->fault_drop_dbuf_update)
		return;
	if (hw->plane_arms > hw->plane_disarms && hw->pipe_on_since_us != 0u) {
		uint32_t cfg = lcd_fake_reg(hw, REG_PLANE_BUF_CFG(hw->pipe));

		if ((slices_of_range(hw, cfg & 0xfffu, (cfg >> 16) & 0xfffu) & ~req_slices) != 0u)
			hw->dbuf_shrunk_under_plane++;
	}
	for (s = 0; s < 4u; s++)
		*slot(hw, dbuf_ctl_reg[s], 1) = (lcd_fake_reg(hw, dbuf_ctl_reg[s]) & ~(DBUF_POWER_REQUEST | DBUF_POWER_STATE)) |
			((req_slices & (1u << s)) ? (DBUF_POWER_REQUEST | DBUF_POWER_STATE) : 0u);
	hw->dbuf_enabled = (uint8_t)(req_slices & 0x0fu);
}

static int f_vblank_get(void *ctx, int pipe)
{
	struct lcd_fake_hw *hw = ctx;

	(void)pipe;
	hw->vblank_refs++;
	return 0;
}

static void f_vblank_put(void *ctx, int pipe)
{
	struct lcd_fake_hw *hw = ctx;

	(void)pipe;
	if (hw->vblank_refs > 0)
		hw->vblank_refs--;
	else
		hw->power_underflows++;
}

static long f_vblank_sleep(void *ctx, int pipe, long ticks)
{
	struct lcd_fake_hw *hw = ctx;

	(void)pipe;
	hw->vblank_sleeps++;
	if (hw->irq_off)
		hw->sleep_irq_off++;            /* the reference enables IRQs before schedule_timeout() */
	if (hw->fault_no_vblank) {
		hw->dpf->now_us += (uint64_t)ticks * 10000u;
		return 0;
	}
	to_next_frame(hw);
	hw->scanline = 0u;                      /* just after the vblank: far from the evasion window */
	hw->scanline_hold_reads = 0u;
	return ticks > 1 ? ticks - 1 : 0;
}

static void f_irq_off(void *ctx)
{
	struct lcd_fake_hw *hw = ctx;

	if (hw->irq_off)
		hw->lock_errors++;
	hw->irq_off = 1;
	hw->irq_off_calls++;
}

static void f_irq_on(void *ctx)
{
	struct lcd_fake_hw *hw = ctx;

	if (!hw->irq_off)
		hw->lock_errors++;
	hw->irq_off = 0;
}

static void f_arm_event(void *ctx, int pipe)
{
	struct lcd_fake_hw *hw = ctx;

	(void)pipe;
	hw->event_armed = 1;
	hw->events_armed++;
}

static int f_wait_event(void *ctx, int pipe, unsigned timeout_ms)
{
	struct lcd_fake_hw *hw = ctx;

	(void)pipe;
	if (!hw->event_armed) {
		hw->waits_refused++;
		return -22;
	}
	if (hw->fault_no_vblank) {
		hw->dpf->now_us += (uint64_t)timeout_ms * 1000u;
		return -110;
	}
	if (!hw->fault_early_event)
		to_next_frame(hw);              /* the event completes at the next vblank */
	hw->event_armed = 0;
	hw->events_done++;
	return 0;
}

static void f_cancel_event(void *ctx, int pipe)
{
	struct lcd_fake_hw *hw = ctx;

	(void)pipe;
	if (hw->event_armed)
		hw->events_cancelled++;
	hw->event_armed = 0;
}

static void f_observe(void *ctx, int point)
{
	struct lcd_fake_hw *hw = ctx;

	if (hw->nobs < sizeof(hw->obs))
		hw->obs[hw->nobs++] = (uint8_t)point;
	if (hw->on_observe != 0)
		hw->on_observe(hw->on_observe_ctx, point);
}

static void f_lock(void *ctx, int which, int take)
{
	struct lcd_fake_hw *hw = ctx;

	if (which < 0 || which > 1 || hw->lock_held[which] == (take != 0)) {
		hw->lock_errors++;
		return;
	}
	hw->lock_held[which] = take != 0;
}

static void f_step(void *ctx, const char *name) { (void)ctx; (void)name; }

/* ---- the sink: link training as the DP standard describes the receiver's side ---- */
static void sink_on_dpcd_write(void *ctx, unsigned addr, unsigned len)
{
	struct lcd_fake_hw *hw = ctx;
	uint8_t *d = hw->dpf->dpcd;
	unsigned lanes, lane, pattern, all_eq = 1;
	uint32_t tp;
	int source_ok;

	if (addr > 0x106u || addr + len <= 0x102u)
		return;                                 /* not TRAINING_PATTERN_SET / TRAINING_LANEx_SET */
	if (addr <= 0x102u)
		hw->training_pattern_writes++;
	pattern = d[0x102] & 0x0fu;
	lanes = d[0x101] & 0x1fu;
	if (lanes > 4u)
		lanes = 4u;
	if (pattern == 0u)
		return;                                 /* training over: the status stays as it is */
	if (!(hw->dpf->pp_control & PPC_POWER_ON))
		hw->training_without_panel_power++;
	tp = lcd_fake_reg(hw, REG_DP_TP_CTL(hw->pipe));
	/* DP_TP_CTL pattern field: PAT1 = 0, PAT2 = 1, PAT3 = 4, PAT4 = 5 */
	if (((tp & DP_TP_CTL_TRAIN_MASK) >> 8) != (pattern == 1u ? 0u : pattern == 2u ? 1u : pattern == 3u ? 4u : 5u))
		hw->pattern_mismatch++;
	source_ok = pll_locked(hw) && ddi_clock_on(hw) && (lcd_fake_reg(hw, REG_DDI_BUF_CTL(hw->port)) & DDI_BUF_CTL_ENABLE) != 0u &&
		(tp & DP_TP_CTL_ENABLE) != 0u && (hw->dpf->pp_control & PPC_POWER_ON) != 0u;

	d[0x202] = d[0x203] = 0u;
	for (lane = 0; lane < lanes; lane++) {
		uint8_t set = d[0x103u + lane], st = 0u;
		unsigned vs = set & 3u, pe = (set >> 3) & 3u;

		if (source_ok && !hw->fault_cr_never && vs >= hw->sink_want_vswing)
			st |= 1u;                               /* DP_LANE_CR_DONE */
		if ((st & 1u) && pattern != 1u && !hw->fault_eq_never && pe >= hw->sink_want_preemph)
			st |= 2u | 4u;                          /* DP_LANE_CHANNEL_EQ_DONE | DP_LANE_SYMBOL_LOCKED */
		if (!(st & 2u))
			all_eq = 0u;
		d[0x202u + lane / 2u] |= (uint8_t)(st << (4u * (lane & 1u)));
	}
	d[0x204] = (uint8_t)((pattern != 1u && all_eq && lanes != 0u) ? 1u : 0u);      /* DP_INTERLANE_ALIGN_DONE */
	/* ADJUST_REQUEST: what the sink wants, for every lane */
	d[0x206] = d[0x207] = (uint8_t)((hw->sink_want_vswing & 3u) | ((hw->sink_want_preemph & 3u) << 2) |
		((hw->sink_want_vswing & 3u) << 4) | ((hw->sink_want_preemph & 3u) << 6));
	if ((d[0x202] & 1u) != 0u)
		hw->link_status_cr_done++;
	if (d[0x204] & 1u)
		hw->link_status_eq_done++;
}

void lcd_fake_init(struct lcd_fake_hw *hw, struct dp_fake_hw *dpf, int pipe, int port, int dpll_id, unsigned vtotal)
{
	memset(hw, 0, sizeof(*hw));
	hw->dpf = dpf;
	hw->pipe = pipe;
	hw->port = port;
	hw->dpll_id = dpll_id;
	hw->vtotal = vtotal;
	/* as found before a modeset: DDI idle and its clock gated, PLL off, link not trained */
	*slot(hw, REG_DDI_BUF_CTL(port), 1) = DDI_BUF_IS_IDLE;
	*slot(hw, REG_DPCLKA_CFGCR0, 1) = (1u << 10) | (1u << 11);
	/* as the normal initialisation leaves them: DBUF slice 1 on, MBUS not joined, every pipe interrupt masked */
	*slot(hw, dbuf_ctl_reg[0], 1) = DBUF_POWER_REQUEST | DBUF_POWER_STATE;
	hw->dbuf_enabled = 0x01u;
	*slot(hw, REG_DE_PIPE_IMR(pipe), 1) = 0xffffffffu;
	memset(dpf->dpcd + 0x202, 0, 6u);
	dpf->dpcd[0x600] = 1u;                          /* DP_SET_POWER: D0 */
	dpf->on_dpcd_write = sink_on_dpcd_write;
	dpf->on_dpcd_write_ctx = hw;

	hw->ops.ctx = hw;
	hw->ops.model = 1;
	hw->ops.write32 = f_write32;
	hw->ops.rmw32 = f_rmw32;
	hw->ops.read32 = f_read32;
	hw->ops.wait_reg = f_wait_reg;
	hw->ops.usleep = f_sleep;
	hw->ops.udelay = f_sleep;
	hw->ops.dpcd_read = f_dpcd_read;
	hw->ops.dpcd_write = f_dpcd_write;
	hw->ops.read_dpcd_caps = f_read_dpcd_caps;
	hw->ops.panel = f_panel;
	hw->ops.power_get = f_power_get;
	hw->ops.power_put = f_power_put;
	hw->ops.power_put_async = f_power_put_async;
	hw->ops.dbuf_slices_update = f_dbuf_slices_update;
	hw->ops.observe = f_observe;
	hw->ops.vblank_get = f_vblank_get;
	hw->ops.vblank_put = f_vblank_put;
	hw->ops.vblank_sleep = f_vblank_sleep;
	hw->ops.irq_off = f_irq_off;
	hw->ops.irq_on = f_irq_on;
	hw->ops.arm_event = f_arm_event;
	hw->ops.wait_event = f_wait_event;
	hw->ops.cancel_event = f_cancel_event;
	hw->ops.lock = f_lock;
	hw->ops.step = f_step;
}

int lcd_fake_power_refs_total(const struct lcd_fake_hw *hw)
{
	int i, n = 0;

	for (i = 0; i < LCD_FAKE_MAX_DOMAINS; i++)
		n += hw->power_refs[i];
	return n;
}

unsigned lcd_fake_violations(const struct lcd_fake_hw *hw)
{
	return hw->ddi_enabled_without_pll + hw->pipe_enabled_without_link + hw->plane_armed_without_pipe +
		hw->pll_disabled_with_pipe_on + hw->training_without_panel_power + hw->pattern_mismatch + hw->plane_armed_without_ddb + hw->modeset_without_dc_off +
		hw->pipe_enabled_without_power + hw->plane_armed_outside_slices + hw->dbuf_shrunk_under_plane + hw->power_dropped_with_pipe_on +
		hw->power_underflows + hw->lock_errors + hw->regs_overflow;
}
