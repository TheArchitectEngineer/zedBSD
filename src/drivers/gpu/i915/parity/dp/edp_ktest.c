/*
 * WS031 Linux-parity — GPU-free kernel checks of the eDP first stage: the
 * production code (parity_edp.c + the generated reference ports) against the
 * register model, with the target's captured DPCD / EDID as the sink.  The
 * fuller matrix (every fault, ASan/UBSan) is the host test
 * plan/ws031/tests/dp-host-test.c; this proves the kernel build of the same
 * files behaves the same.  zedBSD project code.
 */
#include <stdint.h>
#include <string.h>
#include "parity_edp.h"
#include "dp_fake_hw.h"
#include "dp_fixture_latitude5330.h"
#include "edp_ktest.h"

static struct dp_fake_hw hw;
static struct parity_dp_env env;
static struct parity_edp_result res;

static struct parity_edp_config target_cfg(void)
{
	struct parity_edp_config c;

	memset(&c, 0, sizeof(c));
	c.rawclk_khz = 19200u;
	c.t1_t3 = 2000u; c.t8 = 800u; c.t9 = 2000u; c.t10 = 1100u; c.t11_t12 = 5000u;
	c.log_level = -1;
	return c;
}

static void fresh(void)
{
	dp_fake_init(&hw, dp_fixture_dpcd_000, dp_fixture_dpcd_100, dp_fixture_dpcd_700,
		dp_fixture_edid, sizeof(dp_fixture_edid));
	dp_fake_bind_env(&hw, &env);
}

static int released(void)
{
	return hw.refs_core == 0 && hw.refs_aux == 0 && (hw.pp_control & 8u) == 0u &&
		res.vdd_wakeref_held == 0 && res.vdd_work_pending == 0 && res.power_put_underflows == 0u;
}

void parity_edp_ktest(parity_edp_ktest_check check)
{
	struct parity_edp_config cfg = target_cfg();
	uint8_t f[DP_FAKE_SCRIPT_MAX], p[DP_FAKE_SCRIPT_MAX];
	int rc, end;

	fresh();
	rc = parity_edp_begin(&env, &cfg, &res);
	check(rc == 0 && res.stage == PARITY_EDP_STAGE_ACQUIRED && res.pps_valid == 1 && res.pps_idx == 0 &&
		res.dpcd_ok && memcmp(res.dpcd, dp_fixture_dpcd_000, 15) == 0 &&
		res.edp_dpcd_ok && memcmp(res.edp_dpcd, dp_fixture_dpcd_700, 3) == 0 &&
		res.edid_ok && res.edid_blocks == 1u && memcmp(res.edid, dp_fixture_edid, 128) == 0,
		"edp: EDP-ACQUIRE PPS 0, DPCD caps, eDP caps and the 128-byte EDID equal the captured panel data");
	check(res.after_init.pp_on_delays == 0x07d00001u && res.after_init.pp_off_delays == 0x044c0001u &&
		((res.after_init.pp_control >> 4) & 0x1fu) == 6u && res.delay_power_up_ms == 200 &&
		res.delay_power_down_ms == 110 && res.delay_power_cycle_ms == 600 && res.delay_bl_on_ms == 80 &&
		res.delay_bl_off_ms == 200,
		"edp: EDP-DELAYS PP_ON/OFF 0x07d00001/0x044c0001, cycle field 6, software delays 200/110/600/80/200 ms (the target's Linux values)");
	check(res.vdd_on_hw == 1 && res.vdd_wakeref_held == 1 && res.power_refs_aux == 1 && res.power_refs_core == 0 &&
		hw.vdd_on_events == 1u && hw.aux_without_sink_power == 0u && hw.aux_without_aux_power == 0u &&
		hw.aux_without_core_power == 0u && hw.pp_writes_without_core_power == 0u &&
		hw.unknown_reg_reads == 0u && hw.unknown_reg_writes == 0u && env.slept_us >= 200000u &&
		res.log_errors == 0u,
		"edp: EDP-OWNERSHIP VDD on once + its AUX reference, every transfer powered, 200 ms power-up wait, no stray register");
	rc = parity_edp_init_late(&cfg, &res);
	check(rc == 0 && res.vdd_work_pending == 1 && parity_edp_run_due_work(&res) == 0 && res.vdd_on_hw == 1,
		"edp: EDP-WORKER late init schedules the VDD-off worker; it does not run before 5 x the power-cycle delay");
	hw.now_us += 3001u * 1000u;
	check(parity_edp_run_due_work(&res) == 1 && res.vdd_on_hw == 0 && res.vdd_wakeref_held == 0 &&
		res.power_refs_aux == 0,
		"edp: EDP-WORKER-DUE the due worker forces VDD off and returns the AUX reference");
	end = parity_edp_end(&res);
	check(end == 0 && released() && parity_edp_end(&res) != 0,
		"edp: EDP-END everything released; a second end is refused");

	fresh();
	rc = parity_edp_begin(&env, &cfg, &res);
	end = parity_edp_end(&res);
	check(rc == 0 && end == 0 && released() && hw.vdd_off_events == 1u,
		"edp: EDP-END-EARLY end right after the acquisition cancels nothing pending and forces VDD off");

	fresh();
	hw.sink_power_up_us = 3600u * 1000000u;
	rc = parity_edp_begin(&env, &cfg, &res);
	end = parity_edp_end(&res);
	check(rc == -110 && res.failed_stage == PARITY_EDP_STAGE_DPCD && end == 0 && released() &&
		hw.aux_transactions == 160u,
		"edp: EDP-NOSINK no answer -> -ETIMEDOUT after 32 x 5 finite tries, VDD and references released");

	fresh();
	memset(p, 0, sizeof(p));
	memset(f, DP_FAKE_OK, sizeof(f));
	f[0] = DP_FAKE_NATIVE_DEFER; f[1] = DP_FAKE_NATIVE_NACK; f[2] = DP_FAKE_RECEIVE_ERROR;
	f[3] = DP_FAKE_BAD_SIZE_BIG; f[4] = DP_FAKE_INVALID_REPLY; f[5] = DP_FAKE_BAD_SIZE_ZERO;
	f[7] = DP_FAKE_SHORT_REPLY; p[7] = 5u;
	dp_fake_script(&hw, 8u, f, p);
	rc = parity_edp_begin(&env, &cfg, &res);
	end = parity_edp_end(&res);
	check(rc == 0 && memcmp(res.dpcd, dp_fixture_dpcd_000, 15) == 0 && memcmp(res.edid, dp_fixture_edid, 128) == 0 &&
		end == 0 && released(),
		"edp: EDP-RETRY DEFER, NACK, receive error, forbidden sizes 21/0, reserved reply and a short read are retried, never taken as data");

	fresh();
	memset(f, DP_FAKE_OK, sizeof(f));
	memset(p, 0, sizeof(p));
	f[6] = DP_FAKE_I2C_DEFER; f[7] = DP_FAKE_I2C_DEFER;   /* the first I2C transaction, twice */
	f[11] = DP_FAKE_SHORT_REPLY; p[11] = 4u;               /* the first 16-byte EDID read answers with 4 */
	dp_fake_script(&hw, 12u, f, p);
	rc = parity_edp_begin(&env, &cfg, &res);
	end = parity_edp_end(&res);
	check(rc == 0 && res.i2c_defers == 2u && memcmp(res.edid, dp_fixture_edid, 128) == 0 && end == 0 && released(),
		"edp: EDP-I2C I2C DEFERs are retried and a partial I2C read continues without gap or repeat");

	fresh();
	hw.fault_every_i2c_read = DP_FAKE_CORRUPT_DATA;
	rc = parity_edp_begin(&env, &cfg, &res);
	end = parity_edp_end(&res);
	check(rc == -71 && res.failed_stage == PARITY_EDP_STAGE_EDID && end == 0 && released(),
		"edp: EDP-EDID-BAD an EDID that never checksums is rejected (-EPROTO), VDD and references released");

	fresh();
	hw.edid[126] = 200u;
	hw.edid[127] = (uint8_t)(hw.edid[127] - 200u);
	rc = parity_edp_begin(&env, &cfg, &res);
	end = parity_edp_end(&res);
	check(res.edid_blocks <= PARITY_EDP_MAX_EDID_BLOCKS && res.edid_extensions == 200u && end == 0 && released(),
		"edp: EDP-EDID-EXT an extension count beyond the buffer never writes past it");

	fresh();
	hw.fail_power_get = 1;
	rc = parity_edp_begin(&env, &cfg, &res);
	end = parity_edp_end(&res);
	check(rc < 0 && res.power_get_failures != 0u && res.power_put_underflows == 0u && end == 0,
		"edp: EDP-POWER-FAIL a failed power-domain get is reported and its put skipped (no underflow)");

	fresh();
	cfg.aux_ch = 3;
	check(parity_edp_begin(&env, &cfg, &res) == -22,
		"edp: EDP-CONFIG a Type-C AUX channel is refused");
}
