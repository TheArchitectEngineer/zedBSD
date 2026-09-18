/*
 * WS031 Linux-parity — register / sink model for the LCD modeset (see lcd_fake_hw.h).  zedBSD project
 * code; test support only.  Register offsets and bits are the public ones of the display engine
 * (ICL+ combo PLL enable, DDI_BUF_CTL, TGL+ DP_TP_CTL / STATUS, TRANSCONF, PIPEDSL, frame counter,
 * PLANE_CTL / SURF, DPCLKA_CFGCR0) and of the DP standard's DPCD (0x100.., 0x202..).
 */
#include "lcd_fake_hw.h"
#include "../dp/parity_edp.h"
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
#define PPC_POWER_ON          1u

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

static uint32_t f_read32(void *ctx, uint32_t reg)
{
	struct lcd_fake_hw *hw = ctx;

	if (reg == REG_PIPEDSL(hw->pipe)) {
		if (hw->pipe_on_since_us != 0u)
			hw->scanline = (hw->scanline + 97u) % (hw->vtotal != 0u ? hw->vtotal : 1u);
		return hw->scanline;
	}
	if (reg == REG_PIPE_FRMCNT(hw->pipe))
		return hw->pipe_on_since_us != 0u ? (uint32_t)((hw->dpf->now_us - hw->pipe_on_since_us) * 60u / 1000000u) : 0u;
	return lcd_fake_reg(hw, reg);
}

static void f_write32(void *ctx, uint32_t reg, uint32_t value)
{
	struct lcd_fake_hw *hw = ctx;
	uint32_t *v = slot(hw, reg, 1), old = *v;

	if (reg == REG_DPLL_ENABLE(hw->dpll_id)) {
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
			}
		} else if (hw->fault_pipe_stuck_on && (old & TRANSCONF_STATE)) {
			value |= TRANSCONF_STATE;               /* the pipe does not stop */
		} else {
			hw->pipe_on_since_us = 0u;
		}
	} else if (reg == REG_PLANE_SURF(hw->pipe)) {
		uint32_t ctl = lcd_fake_reg(hw, REG_PLANE_CTL(hw->pipe));

		if (ctl & PLANE_CTL_ENABLE) {
			hw->plane_arms++;
			hw->plane_ctl_at_arm = ctl;
			hw->plane_surf_at_arm = value;
			if (hw->pipe_on_since_us == 0u)
				hw->plane_armed_without_pipe++;
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
	if (domain < 0 || domain >= LCD_FAKE_MAX_DOMAINS || hw->power_refs[domain] <= 0) {
		hw->power_underflows++;
		return;
	}
	hw->power_refs[domain]--;
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
	memset(dpf->dpcd + 0x202, 0, 6u);
	dpf->dpcd[0x600] = 1u;                          /* DP_SET_POWER: D0 */
	dpf->on_dpcd_write = sink_on_dpcd_write;
	dpf->on_dpcd_write_ctx = hw;

	hw->ops.ctx = hw;
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
		hw->pll_disabled_with_pipe_on + hw->training_without_panel_power + hw->pattern_mismatch +
		hw->power_underflows + hw->lock_errors + hw->regs_overflow;
}
