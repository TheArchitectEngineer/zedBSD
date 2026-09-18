/*
 * WS031 Linux-parity — GPU-free register model for the eDP first stage
 * (see dp_fake_hw.h).  zedBSD project code; test support only.
 *
 * Register offsets and bit positions are written out here independently of the
 * driver's headers on purpose: a wrong definition on the driver side must show
 * up as a test failure, not cancel out.  Sources: the PCH PPS block at 0xC7200
 * and DDI AUX channel A at 0x64010 as documented for Gen12 display, and the
 * DisplayPort AUX message format (4-bit command, 20-bit address, length - 1).
 */
#include "dp_fake_hw.h"

#define R_PP_STATUS    0xC7200u
#define R_PP_CONTROL   0xC7204u
#define R_PP_ON        0xC7208u
#define R_PP_OFF       0xC720Cu
#define R_PPS1_FIRST   0xC7300u
#define R_PPS1_LAST    0xC730Cu
#define R_SCHICKEN1    0xC2000u
#define R_SDSPCLK_GATE 0xC2020u
#define R_AUX_CTL      0x64010u
#define R_AUX_DATA0    0x64014u

#define PPC_POWER_ON   (1u << 0)
#define PPC_FORCE_VDD  (1u << 3)
#define PPS_ON         (1u << 31)
#define PPS_SEQ_UP     (1u << 28)
#define PPS_SEQ_DOWN   (2u << 28)
#define PPS_CYCLE      (1u << 27)

#define AUX_BUSY       (1u << 31)
#define AUX_DONE       (1u << 30)
#define AUX_TIMEOUT    (1u << 28)
#define AUX_RXERR      (1u << 25)
#define AUX_SIZE_SHIFT 20
#define AUX_SIZE_MASK  (0x1fu << AUX_SIZE_SHIFT)
#define AUX_STATUS     (AUX_BUSY | AUX_DONE | AUX_TIMEOUT | AUX_RXERR | AUX_SIZE_MASK)

static void bytes_clear(void *p, size_t n)
{
	unsigned char *b = p;

	while (n-- != 0)
		*b++ = 0;
}

void dp_fake_init(struct dp_fake_hw *hw, const uint8_t *dpcd_000, const uint8_t *dpcd_100,
	const uint8_t *dpcd_700, const uint8_t *edid, unsigned edid_size)
{
	unsigned i;

	bytes_clear(hw, sizeof(*hw));
	hw->now_us = 5000000u;               /* an arbitrary non-zero boot time */
	for (i = 0; i < 256u; i++) {
		if (dpcd_000 != 0)
			hw->dpcd[0x000u + i] = dpcd_000[i];
		if (dpcd_100 != 0)
			hw->dpcd[0x100u + i] = dpcd_100[i];
		if (dpcd_700 != 0)
			hw->dpcd[0x700u + i] = dpcd_700[i];
	}
	if (edid_size > sizeof(hw->edid))
		edid_size = sizeof(hw->edid);
	for (i = 0; i < edid_size; i++)
		hw->edid[i] = edid[i];
	hw->edid_size = edid_size;
}

void dp_fake_script(struct dp_fake_hw *hw, unsigned n, const uint8_t *faults, const uint8_t *params)
{
	unsigned i;

	if (n > DP_FAKE_SCRIPT_MAX)
		n = DP_FAKE_SCRIPT_MAX;
	for (i = 0; i < n; i++) {
		hw->script[i].fault = faults[i];
		hw->script[i].param = params != 0 ? params[i] : 0;
	}
	hw->script_len = n;
	hw->script_pos = 0;
}

/* stuck-busy release: becomes a hardware timeout once its time has passed */
static uint64_t stuck_until_us;

static void aux_finish(struct dp_fake_hw *hw, uint32_t cfg, uint32_t status)
{
	hw->aux_ctl = (cfg & ~AUX_STATUS) | status;
}

static void aux_reply(struct dp_fake_hw *hw, uint32_t cfg, const uint8_t *reply, unsigned n)
{
	unsigned i;

	for (i = 0; i < 5u; i++)
		hw->aux_data[i] = 0;
	for (i = 0; i < n && i < 20u; i++)
		hw->aux_data[i >> 2] |= (uint32_t)reply[i] << (24u - 8u * (i & 3u));
	aux_finish(hw, cfg, AUX_DONE | ((uint32_t)n << AUX_SIZE_SHIFT));
}

static void aux_transaction(struct dp_fake_hw *hw, uint32_t cfg)
{
	uint8_t msg[20], reply[21];
	unsigned size = (cfg & AUX_SIZE_MASK) >> AUX_SIZE_SHIFT, i, len, n = 1;
	unsigned fault = DP_FAKE_OK, param = 0, cmd, is_i2c, is_read;
	uint32_t addr;
	int sink_on;

	hw->aux_transactions++;
	for (i = 0; i < 20u; i++)
		msg[i] = (uint8_t)(hw->aux_data[i >> 2] >> (24u - 8u * (i & 3u)));
	if (size < 3u || size > 20u) {
		aux_finish(hw, cfg, AUX_DONE | AUX_RXERR);
		return;
	}
	cmd = msg[0] >> 4;
	addr = ((uint32_t)(msg[0] & 0xfu) << 16) | ((uint32_t)msg[1] << 8) | msg[2];
	len = size >= 4u ? (unsigned)msg[3] + 1u : 0u;
	is_i2c = (cmd & 0x8u) == 0;
	is_read = (cmd & 0x3u) == 1u;

	sink_on = (hw->pp_control & PPC_POWER_ON) != 0 ||
		((hw->pp_control & PPC_FORCE_VDD) != 0 &&
		 hw->now_us - hw->vdd_on_since_us >= hw->sink_power_up_us);
	if (hw->refs_core <= 0)
		hw->aux_without_core_power++;
	if (hw->refs_aux <= 0)
		hw->aux_without_aux_power++;
	if (!sink_on)
		hw->aux_without_sink_power++;
	if (hw->refs_core <= 0 || hw->refs_aux <= 0 || !sink_on) {
		aux_finish(hw, cfg, AUX_DONE | AUX_TIMEOUT);
		return;
	}

	if (hw->script_pos < hw->script_len) {
		fault = hw->script[hw->script_pos].fault;
		param = hw->script[hw->script_pos].param;
		hw->script_pos++;
	} else if (hw->fault_every_i2c_read != 0 && is_i2c && is_read && len != 0u) {
		fault = (unsigned)hw->fault_every_i2c_read;
	}
	if ((fault == DP_FAKE_I2C_DEFER || fault == DP_FAKE_I2C_NACK) && !is_i2c)
		fault = DP_FAKE_OK;

	switch (fault) {
	case DP_FAKE_HW_TIMEOUT:
		aux_finish(hw, cfg, AUX_DONE | AUX_TIMEOUT);
		return;
	case DP_FAKE_RECEIVE_ERROR:
		aux_finish(hw, cfg, AUX_DONE | AUX_RXERR);
		return;
	case DP_FAKE_STUCK_BUSY:
		hw->aux_ctl = (cfg & ~AUX_STATUS) | AUX_BUSY;
		stuck_until_us = hw->now_us + (uint64_t)(param != 0 ? param : 25u) * 1000u;
		return;
	case DP_FAKE_BAD_SIZE_ZERO:
		aux_finish(hw, cfg, AUX_DONE);
		return;
	case DP_FAKE_BAD_SIZE_BIG:
		aux_finish(hw, cfg, AUX_DONE | (21u << AUX_SIZE_SHIFT));
		return;
	case DP_FAKE_NATIVE_DEFER: reply[0] = 0x20; aux_reply(hw, cfg, reply, 1); return;
	case DP_FAKE_NATIVE_NACK:  reply[0] = 0x10; aux_reply(hw, cfg, reply, 1); return;
	case DP_FAKE_I2C_DEFER:    reply[0] = 0x80; aux_reply(hw, cfg, reply, 1); return;
	case DP_FAKE_I2C_NACK:     reply[0] = 0x40; aux_reply(hw, cfg, reply, 1); return;
	case DP_FAKE_INVALID_REPLY: reply[0] = 0x30; aux_reply(hw, cfg, reply, 1); return;
	default:
		break;
	}

	reply[0] = 0x00;
	if (!is_i2c) {
		if (is_read) {
			hw->aux_native_reads++;
			if (len > 16u)
				len = 16u;
			for (i = 0; i < len; i++)
				reply[1u + i] = addr + i < sizeof(hw->dpcd) ? hw->dpcd[addr + i] : 0;
			n = 1u + len;
		} else {
			hw->aux_native_writes++;
			for (i = 0; i < len && 4u + i < size; i++)
				if (addr + i < sizeof(hw->dpcd))
					hw->dpcd[addr + i] = msg[4u + i];
		}
	} else if (is_read) {
		hw->aux_i2c_reads++;
		if (addr != 0x50u) {
			reply[0] = 0x40;
		} else {
			if (len > 16u)
				len = 16u;
			for (i = 0; i < len; i++) {
				unsigned at = (unsigned)hw->i2c_segment * 256u + hw->i2c_offset;

				reply[1u + i] = at < hw->edid_size ? hw->edid[at] : 0xffu;
				hw->i2c_offset++;
			}
			n = 1u + len;
		}
	} else {
		hw->aux_i2c_writes++;
		if (addr == 0x50u) {
			if (len != 0u && size > 4u)
				hw->i2c_offset = msg[4u + (size - 5u)];
		} else if (addr == 0x30u) {
			if (len != 0u && size > 4u)
				hw->i2c_segment = msg[4u];
		} else {
			reply[0] = 0x40;
		}
	}
	if (fault == DP_FAKE_SHORT_REPLY && is_read && n > 1u + param)
		n = 1u + param;
	if (fault == DP_FAKE_CORRUPT_DATA && is_read && n > 1u)
		reply[1] = (uint8_t)~reply[1];
	aux_reply(hw, cfg, reply, n);
}

static uint32_t pp_status(const struct dp_fake_hw *hw)
{
	/* panel power is never requested in this stage: VDD alone leaves PP_STATUS idle-off */
	return (hw->pp_control & PPC_POWER_ON) != 0 ? (PPS_ON | 0x8u) : 0u;
}

static uint32_t fake_read(void *ctx, uint32_t reg)
{
	struct dp_fake_hw *hw = ctx;

	if (reg == R_AUX_CTL) {
		if ((hw->aux_ctl & AUX_BUSY) != 0 && hw->now_us >= stuck_until_us)
			aux_finish(hw, hw->aux_ctl, AUX_DONE | AUX_TIMEOUT);
		return hw->aux_ctl;
	}
	if (reg >= R_AUX_DATA0 && reg < R_AUX_DATA0 + 20u && ((reg - R_AUX_DATA0) & 3u) == 0)
		return hw->aux_data[(reg - R_AUX_DATA0) >> 2];
	switch (reg) {
	case R_PP_STATUS:    return pp_status(hw);
	case R_PP_CONTROL:   return hw->pp_control;
	case R_PP_ON:        return hw->pp_on_delays;
	case R_PP_OFF:       return hw->pp_off_delays;
	case R_SCHICKEN1:    return hw->south_chicken1;
	case R_SDSPCLK_GATE: return hw->south_dspclk_gate_d;
	default:
		break;
	}
	if (reg >= R_PPS1_FIRST && reg <= R_PPS1_LAST)
		return 0;
	hw->unknown_reg_reads++;
	hw->last_unknown_reg = reg;
	return 0;
}

static void fake_write(void *ctx, uint32_t reg, uint32_t value)
{
	struct dp_fake_hw *hw = ctx;

	if (reg == R_AUX_CTL) {
		if ((fake_read(hw, R_AUX_CTL) & AUX_BUSY) != 0)
			return;                  /* a busy channel ignores writes */
		hw->aux_ctl &= ~(value & (AUX_DONE | AUX_TIMEOUT | AUX_RXERR));   /* write one to clear */
		if ((value & AUX_BUSY) != 0)
			aux_transaction(hw, value);
		else
			hw->aux_ctl = (hw->aux_ctl & AUX_STATUS) | (value & ~AUX_STATUS);
		return;
	}
	if (reg >= R_AUX_DATA0 && reg < R_AUX_DATA0 + 20u && ((reg - R_AUX_DATA0) & 3u) == 0) {
		hw->aux_data[(reg - R_AUX_DATA0) >> 2] = value;
		return;
	}
	switch (reg) {
	case R_PP_CONTROL:
		if (hw->refs_core <= 0)
			hw->pp_writes_without_core_power++;
		if ((value & PPC_FORCE_VDD) != 0 && (hw->pp_control & PPC_FORCE_VDD) == 0) {
			hw->vdd_on_events++;
			hw->vdd_on_since_us = hw->now_us;
		}
		if ((value & PPC_FORCE_VDD) == 0 && (hw->pp_control & PPC_FORCE_VDD) != 0)
			hw->vdd_off_events++;
		hw->pp_control = value;
		return;
	case R_PP_ON:        hw->pp_on_delays = value; return;
	case R_PP_OFF:       hw->pp_off_delays = value; return;
	case R_SCHICKEN1:    hw->south_chicken1 = value; return;
	case R_SDSPCLK_GATE: hw->south_dspclk_gate_d = value; return;
	default:
		break;
	}
	hw->unknown_reg_writes++;
	hw->last_unknown_reg = reg;
}

static int fake_wait_reg(void *ctx, uint32_t reg, uint32_t mask, uint32_t value,
	unsigned fast_us, unsigned slow_ms, uint32_t *out)
{
	struct dp_fake_hw *hw = ctx;
	uint64_t deadline = hw->now_us + fast_us + (uint64_t)slow_ms * 1000u;
	uint32_t v;

	for (;;) {
		v = fake_read(hw, reg);
		if (out != 0)
			*out = v;
		if ((v & mask) == value)
			return 0;
		if (hw->now_us >= deadline)
			break;
		hw->now_us += 500u;
	}
	hw->wait_timeouts++;
	return -110;
}

static void fake_sleep_us(void *ctx, unsigned us)
{
	((struct dp_fake_hw *)ctx)->now_us += us;
}

static uint64_t fake_now_ms(void *ctx)
{
	return ((struct dp_fake_hw *)ctx)->now_us / 1000u;
}

static int fake_power_get(void *ctx, int domain)
{
	struct dp_fake_hw *hw = ctx;
	int slot = domain == 0 ? 0 : 1;

	if (hw->fail_power_get)
		return -5;
	if (hw->parked[slot]) {            /* grab the parked reference back: no hardware change */
		hw->parked[slot] = 0;
		hw->async_grabbed++;
		return 0;
	}
	if (slot == 0)
		hw->refs_core++;
	else
		hw->refs_aux++;
	return 0;
}

static void fake_power_put_async(void *ctx, int domain)
{
	struct dp_fake_hw *hw = ctx;
	int slot = domain == 0 ? 0 : 1;
	int *refs = slot == 0 ? &hw->refs_core : &hw->refs_aux;

	if (*refs > 1) {                   /* not the last one: an ordinary put */
		(*refs)--;
		return;
	}
	hw->parked[slot] = 1;
	hw->parked_due_us[slot] = hw->now_us + 100000u;
	hw->async_parked++;
}

static void fake_lock(void *ctx, int which)
{
	struct dp_fake_hw *hw = ctx;

	if (hw->lock_held[which])
		hw->lock_errors++;             /* a real mutex would deadlock here */
	hw->lock_held[which] = 1;
	hw->lock_acquisitions[which]++;
}

static void fake_unlock(void *ctx, int which)
{
	struct dp_fake_hw *hw = ctx;

	if (!hw->lock_held[which])
		hw->lock_errors++;
	hw->lock_held[which] = 0;
}

static int fake_delayed_queue(void *ctx, int which, unsigned delay_ms)
{
	struct dp_fake_hw *hw = ctx;

	(void)which;
	if (hw->work_pending)
		return 0;
	hw->work_pending = 1;
	hw->work_due_us = hw->now_us + (uint64_t)delay_ms * 1000u;
	hw->work_queued++;
	return 1;
}

static int fake_delayed_cancel(void *ctx, int which, int sync)
{
	struct dp_fake_hw *hw = ctx;
	int was = hw->work_pending;

	(void)which;
	if (sync) {
		hw->work_cancel_syncs++;
		if (hw->lock_held[PARITY_DP_LOCK_PPS])
			hw->lock_errors++;         /* cancel_sync under the lock the body takes = deadlock */
	}
	if (was)
		hw->work_cancelled++;
	hw->work_pending = 0;
	return was;
}

static int fake_delayed_pending(void *ctx, int which)
{
	(void)which;
	return ((struct dp_fake_hw *)ctx)->work_pending;
}

static void release_parked(struct dp_fake_hw *hw, unsigned slot)
{
	hw->parked[slot] = 0;
	if (slot == 0)
		hw->refs_core--;
	else
		hw->refs_aux--;
	hw->async_released++;
}

unsigned dp_fake_run_due(struct dp_fake_hw *hw)
{
	unsigned ran = 0, slot;

	if (hw->work_pending && hw->now_us >= hw->work_due_us) {
		hw->work_pending = 0;
		hw->work_ran++;
		parity_edp_work_run(PARITY_DP_WORK_VDD_OFF);
		ran++;
	}
	for (slot = 0; slot < 2u; slot++)
		if (hw->parked[slot] && hw->now_us >= hw->parked_due_us[slot])
			release_parked(hw, slot);
	return ran;
}

void dp_fake_flush_async(struct dp_fake_hw *hw)
{
	unsigned slot;

	for (slot = 0; slot < 2u; slot++)
		if (hw->parked[slot])
			release_parked(hw, slot);
}

static void fake_power_put(void *ctx, int domain)
{
	struct dp_fake_hw *hw = ctx;

	if (domain == 0)
		hw->refs_core--;
	else
		hw->refs_aux--;
}

void dp_fake_bind_env(struct dp_fake_hw *hw, struct parity_dp_env *env)
{
	bytes_clear(env, sizeof(*env));
	env->ctx = hw;
	env->read32 = fake_read;
	env->write32 = fake_write;
	env->wait_reg = fake_wait_reg;
	env->sleep_us = fake_sleep_us;
	env->now_ms = fake_now_ms;
	env->power_get = fake_power_get;
	env->power_put = fake_power_put;
	env->power_put_async = fake_power_put_async;
	env->lock = fake_lock;
	env->unlock = fake_unlock;
	env->delayed_queue = fake_delayed_queue;
	env->delayed_cancel = fake_delayed_cancel;
	env->delayed_pending = fake_delayed_pending;
	stuck_until_us = 0;
}
