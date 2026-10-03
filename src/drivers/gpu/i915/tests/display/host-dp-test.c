/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the eDP first stage (PPS and VDD ownership, AUX, DPCD,
 * EDID): the production display files the kernel builds (dp-sink.c,
 * panel.c, aux.c, edid-read.c) against the register model dp-fake-hw.c,
 * with the target laptop's captured DPCD and EDID as the sink's content.
 *
 *   sh plan/ws031/tests/run-dp-host-test.sh
 */

#include "host-test.h"
#include "dp-fake-hw.h"

#include "../../display/internal.h"
#include "../../display/dp-sink.h"

#include <stdio.h>
#include <string.h>

/*
 * The captured sink: DPCD pages 0x000, 0x100 and 0x700, and the EDID.
 *
 * Read once by main() from plan/ws031/display-ref and never changed.
 */
static uint8_t i915_dp_d000[256];
static uint8_t i915_dp_d100[256];
static uint8_t i915_dp_d700[256];
static uint8_t i915_dp_edid[128];

/*
 * The display that owns the DP world, the world, the register model, the
 * environment it serves, and the result of the stage under test.
 *
 * The display and the world live for the whole run; the model, the
 * environment and the result are started afresh by i915_dp_fresh() for
 * every case.
 */
static struct i915_display i915_dp_display;
static struct i915_dp_world *i915_dp_world;
static struct i915_dp_fake_hw i915_dp_hw;
static struct i915_dp_env i915_dp_env;
static struct i915_edp_result i915_dp_res;

static struct i915_edp_config i915_dp_vbt_config(void);
static void i915_dp_fresh(void);
static void i915_dp_script_all(unsigned count, unsigned fault, unsigned param);
static int i915_dp_released(const char *what);
static unsigned i915_dp_edid_checksum(const uint8_t *block);
static void i915_dp_test_normal(void);
static void i915_dp_test_end_after_begin(void);
static void i915_dp_test_delays(void);
static void i915_dp_test_adopt(void);
static void i915_dp_test_no_sink(void);
static void i915_dp_test_replies(void);
static void i915_dp_test_channel(void);
static void i915_dp_test_edid(void);
static void i915_dp_test_edid_extensions(void);
static void i915_dp_test_refusals(void);

/*
 * Runs the eDP first stage on the register model, case by case.
 *
 * Usage: host-dp-test <display-ref directory> [-v]
 */
int
main(
	int argc,
	char **argv)
{
	int error;
	int status;

	/* Needs the reference directory; a second argument makes the run verbose. */
	if (argc < 2) {
		fprintf(stderr, "usage: %s <display-ref dir> [-v]\n", argv[0]);
		return 2;
	}

	if (argc > 2)
		i915_host_verbose = 1;

	/* Reads the captured sink. */
	i915_host_read_reference(argv[1], "dpcd-drm_dp_aux0-000.bin", i915_dp_d000, sizeof(i915_dp_d000));
	i915_host_read_reference(argv[1], "dpcd-drm_dp_aux0-100.bin", i915_dp_d100, sizeof(i915_dp_d100));
	i915_host_read_reference(argv[1], "dpcd-drm_dp_aux0-700.bin", i915_dp_d700, sizeof(i915_dp_d700));
	i915_host_read_reference(argv[1], "edid-eDP-1.bin", i915_dp_edid, sizeof(i915_dp_edid));

	/* Creates the DP world the stage runs in. */
	error = drv_i915_dp_world_create(&i915_dp_display);
	if (error != 0) {
		printf("dp world: error %d\n", error);
		return 2;
	}

	i915_dp_world = i915_dp_display.dp_world;

	/* Runs the cases. */
	i915_dp_test_normal();
	i915_dp_test_end_after_begin();
	i915_dp_test_delays();
	i915_dp_test_adopt();
	i915_dp_test_no_sink();
	i915_dp_test_replies();
	i915_dp_test_channel();
	i915_dp_test_edid();
	i915_dp_test_edid_extensions();
	i915_dp_test_refusals();

	/* Releases the world. */
	drv_i915_dp_world_destroy(&i915_dp_display);

	/* Reports the tally. */
	status = i915_host_report("dp_host_test");
	if (status != 0)
		return status;

	/* Succeeded: every check passed. */
	return 0;
}

/* Returns the target's VBT panel data (igt intel_vbt_decode: T3 2000, T7 800, T9 2000, T10 1100, T12 5000; controller 0). */
static struct i915_edp_config
i915_dp_vbt_config(void)
{
	struct i915_edp_config config;

	/* Port A, AUX A, the 19.2 MHz raw clock. */
	memset(&config, 0, sizeof(config));
	config.port = 0;
	config.aux_ch = 0;
	config.rawclk_khz = 19200U;

	/* The panel power sequence in 100 us units, and the PPS / backlight controller. */
	config.t1_t3 = 2000U;
	config.t8 = 800U;
	config.t9 = 2000U;
	config.t10 = 1100U;
	config.t11_t12 = 5000U;
	config.bl_controller = 0;

	/* Debug messages only in a verbose run. */
	config.log_level = -1;
	if (i915_host_verbose)
		config.log_level = 2;

	/* Succeeded: the configuration. */
	return config;
}

/* Starts the model and the environment afresh on the captured sink. */
static void
i915_dp_fresh(void)
{
	/* A fresh model with the captured DPCD and EDID, handed to the driver. */
	drv_i915_dp_fake_init(&i915_dp_hw, i915_dp_d000, i915_dp_d100, i915_dp_d700, i915_dp_edid, sizeof(i915_dp_edid));
	drv_i915_dp_fake_bind_env(&i915_dp_hw, &i915_dp_env, i915_dp_world);
}

/* Scripts the same fault for the next count AUX transactions. */
static void
i915_dp_script_all(
	unsigned count,
	unsigned fault,
	unsigned param)
{
	uint8_t faults[I915_DP_FAKE_SCRIPT_MAX];
	uint8_t params[I915_DP_FAKE_SCRIPT_MAX];
	unsigned i;

	/* Fills the script. */
	for (i = 0U; i < count && i < I915_DP_FAKE_SCRIPT_MAX; i++) {
		faults[i] = (uint8_t)fault;
		params[i] = (uint8_t)param;
	}

	drv_i915_dp_fake_script(&i915_dp_hw, count, faults, params);
}

/* Tells whether everything the stage may own is back: the pass criterion of every failure case. */
static int
i915_dp_released(
	const char *what)
{
	int released;

	/* The DP layer owns nothing; what it put asynchronously is the power layer's until flushed. */
	released = 0;
	if (i915_dp_env.power_refs[0] == 0 &&
	    i915_dp_env.power_refs[1] == 0 &&
	    (i915_dp_hw.pp_control & 8U) == 0U &&
	    i915_dp_res.vdd_wakeref_held == 0 &&
	    i915_dp_res.vdd_work_pending == 0 &&
	    i915_dp_res.power_put_underflows == 0U &&
	    i915_dp_hw.lock_held[0] == 0 &&
	    i915_dp_hw.lock_held[1] == 0 &&
	    i915_dp_hw.lock_errors == 0U &&
	    i915_dp_env.lock_errors == 0U)
		released = 1;

	/* After the power layer's flush no reference is left on the hardware side. */
	drv_i915_dp_fake_flush_async(&i915_dp_hw);
	if (i915_dp_hw.refs_core != 0 || i915_dp_hw.refs_aux != 0)
		released = 0;

	/* Shows what is still held. */
	if (!released) {
		printf("  [%s] refs core=%d aux=%d pp_control=0x%x wakeref=%d work=%d underflow=%u\n",
		       what,
		       i915_dp_hw.refs_core,
		       i915_dp_hw.refs_aux,
		       i915_dp_hw.pp_control,
		       i915_dp_res.vdd_wakeref_held,
		       i915_dp_res.vdd_work_pending,
		       i915_dp_res.power_put_underflows);
	}

	/* Succeeded: reports whether everything is back. */
	return released;
}

/* Returns the checksum byte that makes an EDID block sum to zero. */
static unsigned
i915_dp_edid_checksum(
	const uint8_t *block)
{
	unsigned sum;
	unsigned i;

	/* Sums the first 127 bytes. */
	sum = 0U;
	for (i = 0U; i < 127U; i++)
		sum += block[i];

	/* Succeeded: the byte that completes the sum. */
	return (0x100U - (sum & 0xffU)) & 0xffU;
}

/* The normal run: acquisition, late init, the delayed VDD-off worker, the end. */
static void
i915_dp_test_normal(void)
{
	struct i915_edp_config config;
	uint8_t bytes[2];
	long count;
	int error;
	int end;
	int same;
	int ran;
	int released;

	/* Acquires the eDP up to the EDID. */
	config = i915_dp_vbt_config();
	i915_dp_fresh();
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	i915_host_check(error == 0 && i915_dp_res.stage == I915_EDP_STAGE_ACQUIRED, "begin succeeds up to the EDID");
	i915_host_check(i915_dp_res.pps_valid == 1 && i915_dp_res.pps_idx == 0, "PPS 0 (the VBT's controller) is valid");

	/* The DPCD and the EDID read back as captured. */
	same = memcmp(i915_dp_res.dpcd, i915_dp_d000, 15U);
	i915_host_check(i915_dp_res.dpcd_ok != 0 && same == 0, "DPCD receiver caps == the captured 0x000..0x00e");
	i915_host_check(i915_dp_res.dpcd[0] == 0x11U &&
			i915_dp_res.dpcd[1] == 0x0aU &&
			(i915_dp_res.dpcd[2] & 0x1fU) == 2U,
			"DPCD 1.1, HBR, 2 lanes (captured panel)");
	same = memcmp(i915_dp_res.edp_dpcd, i915_dp_d700, 3U);
	i915_host_check(i915_dp_res.edp_dpcd_ok != 0 && same == 0, "eDP display-control caps == the captured 0x700..0x702");
	i915_host_check(i915_dp_res.link_cfg_ok != 0 &&
			i915_dp_res.link_cfg[0] == i915_dp_d100[0] &&
			i915_dp_res.link_cfg[1] == i915_dp_d100[1],
			"link config bytes read back as found");
	same = memcmp(i915_dp_res.edid, i915_dp_edid, 128U);
	i915_host_check(i915_dp_res.edid_ok != 0 &&
			i915_dp_res.edid_blocks == 1U &&
			i915_dp_res.edid_extensions == 0U &&
			same == 0,
			"EDID over I2C-over-AUX == the captured 128 bytes");
	i915_host_check(i915_dp_res.edid[8] == 0x06U &&
			i915_dp_res.edid[9] == 0xafU &&
			i915_dp_res.edid[10] == 0x99U &&
			i915_dp_res.edid[11] == 0x2bU,
			"panel identity AUO 0x2b99");

	/* The reference's rule: max(register, VBT); T8/T9 written as 1; T11_T12 = VBT + 100 ms rounded up to 100 ms. */
	i915_host_check(i915_dp_res.after_init.pp_on_delays == 0x07d00001U && i915_dp_res.after_init.pp_off_delays == 0x044c0001U,
			"PP_ON/OFF_DELAYS = 0x07d00001 / 0x044c0001 (the values Linux programmed on the target)");
	i915_host_check(((i915_dp_res.after_init.pp_control >> 4) & 0x1fU) == 6U, "power-cycle field = 6 (600 ms)");
	i915_host_check(i915_dp_res.delay_power_up_ms == 200 &&
			i915_dp_res.delay_power_down_ms == 110 &&
			i915_dp_res.delay_power_cycle_ms == 600 &&
			i915_dp_res.delay_bl_on_ms == 80 &&
			i915_dp_res.delay_bl_off_ms == 200,
			"software delays 200/110/600/80/200 ms (i915 debugfs on the target)");

	/* Who holds what after the acquisition. */
	i915_host_check(i915_dp_res.vdd_on_hw == 1 &&
			i915_dp_res.vdd_wakeref_held == 1 &&
			i915_dp_res.vdd_wanted == 0 &&
			i915_dp_res.vdd_work_pending == 0,
			"after acquisition: VDD forced on, its AUX reference held, no worker yet (initializing)");
	i915_host_check(i915_dp_res.power_refs_aux == 1 &&
			i915_dp_res.power_refs_core == 0 &&
			i915_dp_hw.refs_aux == 1 &&
			i915_dp_hw.refs_core == 0,
			"exactly the VDD's AUX reference is held between transfers");
	i915_host_check(i915_dp_hw.vdd_on_events == 1U, "VDD was switched on once (kept on across the transfers)");
	i915_host_check(i915_dp_env.async_puts != 0U && i915_dp_hw.async_parked == 0U,
			"each transfer put its own AUX reference asynchronously, never as the last one (VDD holds another)");
	i915_host_check(i915_dp_hw.lock_acquisitions[I915_DP_LOCK_PPS] != 0U &&
			i915_dp_hw.lock_acquisitions[I915_DP_LOCK_AUX] != 0U &&
			i915_dp_hw.lock_errors == 0U &&
			i915_dp_env.lock_errors == 0U,
			"PPS and AUX locks go through the env: no recursion, none left held");
	i915_host_check(i915_dp_hw.aux_without_sink_power == 0U &&
			i915_dp_hw.aux_without_aux_power == 0U &&
			i915_dp_hw.aux_without_core_power == 0U &&
			i915_dp_hw.pp_writes_without_core_power == 0U,
			"no AUX transfer or PP write without its power");
	i915_host_check(i915_dp_hw.unknown_reg_reads == 0U && i915_dp_hw.unknown_reg_writes == 0U,
			"no register outside PPS0 / AUX A / SOUTH_* touched");
	i915_host_check(i915_dp_env.slept_us >= 200000U, "the 200 ms panel power-up delay was waited before the first AUX transfer");
	i915_host_check(i915_dp_res.log_errors == 0U, "no error-level message");
	if (i915_host_verbose) {
		printf("normal: aux_transactions=%u native r/w=%u/%u i2c r/w=%u/%u slept=%llu us elapsed=%llu ms\n",
		       i915_dp_hw.aux_transactions,
		       i915_dp_hw.aux_native_reads,
		       i915_dp_hw.aux_native_writes,
		       i915_dp_hw.aux_i2c_reads,
		       i915_dp_hw.aux_i2c_writes,
		       (unsigned long long)i915_dp_env.slept_us,
		       (unsigned long long)i915_dp_res.elapsed_ms);
	}

	/* A second live eDP is refused; the refused call cleared the result, the live object is untouched. */
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	i915_host_check(error == -I915_EDP_EBUSY, "a second live eDP is refused (-EBUSY)");
	error = drv_i915_edp_init_late(i915_dp_world, &config, &i915_dp_res);
	i915_host_check(error == -I915_EDP_EINVAL, "init_late needs the result of the acquisition it follows");

	/* The late init schedules the delayed VDD-off. */
	i915_dp_res.stage = I915_EDP_STAGE_ACQUIRED;
	error = drv_i915_edp_init_late(i915_dp_world, &config, &i915_dp_res);
	i915_host_check(error == 0 &&
			i915_dp_res.stage == I915_EDP_STAGE_LATE &&
			i915_dp_res.vdd_work_pending == 1 &&
			i915_dp_res.vdd_on_hw == 1,
			"init_late: delayed VDD-off scheduled, VDD still on");

	/* The worker runs only when due: 5 x the 600 ms power-cycle delay. */
	ran = (int)drv_i915_dp_fake_run_due(&i915_dp_hw);
	i915_host_check(ran == 0 && (i915_dp_hw.pp_control & 8U) != 0U, "the worker does not run before it is due");
	i915_dp_hw.now_us += 2999U * 1000U;
	ran = (int)drv_i915_dp_fake_run_due(&i915_dp_hw);
	i915_host_check(ran == 0, "... not at 2999 ms (5 x the 600 ms power-cycle delay, NOT the 600 ms itself)");
	i915_dp_hw.now_us += 2U * 1000U;
	ran = (int)drv_i915_dp_fake_run_due(&i915_dp_hw);
	i915_host_check(ran == 1, "due at 3000 ms: the worker body runs");
	drv_i915_edp_snapshot(i915_dp_world, &i915_dp_res);
	i915_host_check(i915_dp_res.vdd_on_hw == 0 &&
			i915_dp_res.vdd_wakeref_held == 0 &&
			i915_dp_res.power_refs_aux == 0 &&
			i915_dp_hw.refs_aux == 0,
			"the worker forced VDD off and returned the AUX reference of the VDD (an ordinary put)");

	/* An AUX read after that takes VDD again on its own and hands it to the delayed worker. */
	count = drv_i915_edp_dpcd_read(i915_dp_world, 0x000U, bytes, 2U);
	i915_host_check(count == 2 && bytes[0] == 0x11U && i915_dp_hw.vdd_on_events == 2U, "a later AUX read turns VDD on again by itself");
	i915_host_check(i915_dp_env.slept_us >= 800000U, "... after the power-cycle wait (T12) and the power-up delay");
	drv_i915_edp_snapshot(i915_dp_world, &i915_dp_res);
	i915_host_check(i915_dp_res.vdd_work_pending == 1 && i915_dp_res.vdd_on_hw == 1 && i915_dp_res.vdd_wanted == 0,
			"... and hands VDD to a fresh delayed off (not initializing any more)");
	i915_host_check(i915_dp_hw.async_parked == 0U, "the AUX reference of the transfer was not the last one (VDD still holds one)");

	/* A re-acquisition before the deadline: the old reservation must not drop the VDD in use. */
	i915_dp_hw.now_us += 2000U * 1000U;
	count = drv_i915_edp_dpcd_read(i915_dp_world, 0x000U, bytes, 2U);
	i915_host_check(count == 2 && i915_dp_hw.work_cancelled >= 1U && i915_dp_hw.vdd_on_events == 2U,
			"a read 2 s later cancels the reservation and re-uses the VDD that is still on");

	/* 3.5 s after the first reservation: the old deadline passing does nothing. */
	i915_dp_hw.now_us += 1500U * 1000U;
	ran = (int)drv_i915_dp_fake_run_due(&i915_dp_hw);
	i915_host_check(ran == 0 && (i915_dp_hw.pp_control & 8U) != 0U,
			"the old deadline passing does nothing: the new reservation counts from the last use");
	i915_dp_hw.now_us += 1600U * 1000U;
	ran = (int)drv_i915_dp_fake_run_due(&i915_dp_hw);
	i915_host_check(ran >= 1 && (i915_dp_hw.pp_control & 8U) == 0U, "... and the new one fires 3 s after the last use");
	count = drv_i915_edp_dpcd_read(i915_dp_world, 0x000U, bytes, 2U);
	drv_i915_edp_snapshot(i915_dp_world, &i915_dp_res);
	i915_host_check(count == 2 && i915_dp_res.vdd_on_hw == 1 && i915_dp_res.vdd_work_pending == 1, "VDD on again, off reserved");

	/* The stop happens while the off is still reserved (the timer-wait side of the stop race). */
	end = drv_i915_edp_end(i915_dp_world, &i915_dp_res);
	released = i915_dp_released("normal");
	i915_host_check(end == 0 && released, "end: worker cancelled, VDD off, every reference returned");
	i915_host_check(i915_dp_hw.vdd_off_events == 3U && i915_dp_hw.work_cancel_syncs >= 1U,
			"VDD off three times (worker, worker, end); end cancelled synchronously and outside the PPS lock");
	end = drv_i915_edp_end(i915_dp_world, &i915_dp_res);
	i915_host_check(end == -I915_EDP_EINVAL, "end without a live eDP is refused");
}

/* The end straight after the acquisition, without the late init. */
static void
i915_dp_test_end_after_begin(void)
{
	struct i915_edp_config config;
	int error;
	int end;
	int released;

	/* Acquires and ends at once. */
	config = i915_dp_vbt_config();
	i915_dp_fresh();
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	end = drv_i915_edp_end(i915_dp_world, &i915_dp_res);
	released = i915_dp_released("end-after-begin");
	i915_host_check(error == 0 && end == 0 && released,
			"end right after begin releases VDD and its reference");
}

/* The delay selection: the larger of the register and the VBT, and the eDP-spec limits. */
static void
i915_dp_test_delays(void)
{
	struct i915_edp_config config;
	int error;
	int released;

	/* The firmware left T1+T3 = 3000 (300 ms), larger than the VBT's. */
	config = i915_dp_vbt_config();
	i915_dp_fresh();
	i915_dp_hw.pp_on_delays = 0x0bb80001U;
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	i915_host_check(error == 0 &&
			i915_dp_res.delay_power_up_ms == 300 &&
			(i915_dp_res.after_init.pp_on_delays >> 16) == 3000U,
			"a larger firmware value wins over the VBT (max rule)");
	(void)drv_i915_edp_end(i915_dp_world, &i915_dp_res);

	/* No firmware value and no VBT value: the eDP-spec limits. */
	i915_dp_fresh();
	config.t1_t3 = 0U;
	config.t8 = 0U;
	config.t9 = 0U;
	config.t10 = 0U;
	config.t11_t12 = 0U;
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	i915_host_check(error == 0 &&
			i915_dp_res.delay_power_up_ms == 210 &&
			i915_dp_res.delay_power_down_ms == 500 &&
			i915_dp_res.delay_power_cycle_ms == 610 &&
			((i915_dp_res.after_init.pp_control >> 4) & 0x1fU) == 7U,
			"no firmware value and no VBT value: eDP-spec limits (210/500/610 ms, cycle field 7)");

	/* The late init re-reads the delays: the registers now hold the spec values, which win by the max rule. */
	config = i915_dp_vbt_config();
	i915_dp_res.stage = I915_EDP_STAGE_ACQUIRED;
	error = drv_i915_edp_init_late(i915_dp_world, &config, &i915_dp_res);
	i915_host_check(error == 0 && i915_dp_res.delay_power_up_ms == 210 && i915_dp_res.delay_power_cycle_ms == 700,
			"late init re-reads the delays: the registers now hold the spec values, which win by the max rule");
	(void)drv_i915_edp_end(i915_dp_world, &i915_dp_res);
	released = i915_dp_released("delays");
	i915_host_check(released, "released");
}

/* VDD left on by the firmware is adopted, then released. */
static void
i915_dp_test_adopt(void)
{
	struct i915_edp_config config;
	int error;
	int end;
	int released;

	/* The firmware left VDD forced on. */
	config = i915_dp_vbt_config();
	i915_dp_fresh();
	i915_dp_hw.pp_control = 0x8U;
	i915_dp_hw.vdd_on_since_us = 1U;
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	i915_host_check(error == 0 && i915_dp_hw.vdd_on_events == 0U && i915_dp_res.power_refs_aux == 1,
			"firmware VDD adopted: one reference, no second switch-on");

	/* The end releases it. */
	end = drv_i915_edp_end(i915_dp_world, &i915_dp_res);
	released = i915_dp_released("adopt");
	i915_host_check(end == 0 && released, "... and released at end");
}

/* The sink never answers: VDD is up but the panel needs longer than the delay. */
static void
i915_dp_test_no_sink(void)
{
	struct i915_edp_config config;
	int error;
	int end;
	int released;

	/* The sink needs an hour after VDD. */
	config = i915_dp_vbt_config();
	i915_dp_fresh();
	i915_dp_hw.sink_power_up_us = 3600U * 1000000U;
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	i915_host_check(error == -I915_EDP_ETIMEDOUT &&
			i915_dp_res.failed_stage == I915_EDP_STAGE_DPCD &&
			i915_dp_res.dpcd_ok == 0,
			"no sink answer: -ETIMEDOUT at the DPCD stage");
	i915_host_check(i915_dp_res.vdd_on_hw == 0 && i915_dp_res.vdd_wakeref_held == 0,
			"the failure path already forced VDD off (out_vdd_off)");

	/* The end releases everything; the retries were finite. */
	end = drv_i915_edp_end(i915_dp_world, &i915_dp_res);
	released = i915_dp_released("no-sink");
	i915_host_check(end == 0 && released, "released");
	i915_host_check(i915_dp_hw.aux_transactions == 32U * 5U, "32 retries x 5 hardware tries, then it gives up (finite)");
}

/* Native DEFER, NACK, an invalid reply and a short reply. */
static void
i915_dp_test_replies(void)
{
	static const uint8_t short_faults[2] = { I915_DP_FAKE_OK, I915_DP_FAKE_SHORT_REPLY };
	static const uint8_t short_params[2] = { 0U, 5U };
	struct i915_edp_config config;
	int error;
	int end;
	int same;
	int released;

	config = i915_dp_vbt_config();

	/* Three native DEFERs are retried. */
	i915_dp_fresh();
	i915_dp_script_all(3U, I915_DP_FAKE_NATIVE_DEFER, 0U);
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	same = memcmp(i915_dp_res.dpcd, i915_dp_d000, 15U);
	i915_host_check(error == 0 && same == 0, "3 native DEFERs are retried");
	(void)drv_i915_edp_end(i915_dp_world, &i915_dp_res);

	/* One native NACK is retried. */
	i915_dp_fresh();
	i915_dp_script_all(1U, I915_DP_FAKE_NATIVE_NACK, 0U);
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	i915_host_check(error == 0, "one native NACK is retried");
	(void)drv_i915_edp_end(i915_dp_world, &i915_dp_res);

	/* A persistent native NACK fails after 32 tries. */
	i915_dp_fresh();
	i915_dp_script_all(40U, I915_DP_FAKE_NATIVE_NACK, 0U);
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	end = drv_i915_edp_end(i915_dp_world, &i915_dp_res);
	released = i915_dp_released("nack");
	i915_host_check(error == -I915_EDP_EIO && end == 0 && released, "persistent native NACK: -EIO after 32 tries, released");

	/* A reserved reply code is not taken as data. */
	i915_dp_fresh();
	i915_dp_script_all(2U, I915_DP_FAKE_INVALID_REPLY, 0U);
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	i915_host_check(error == 0, "a reserved reply code is not taken as data");
	(void)drv_i915_edp_end(i915_dp_world, &i915_dp_res);

	/* The 1-byte probe is fine, the 15-byte read comes back with 5. */
	i915_dp_fresh();
	drv_i915_dp_fake_script(&i915_dp_hw, 2U, short_faults, short_params);
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	same = memcmp(i915_dp_res.dpcd, i915_dp_d000, 15U);
	i915_host_check(error == 0 && same == 0, "a short DPCD reply is not accepted as the full read (-EPROTO, retried)");
	(void)drv_i915_edp_end(i915_dp_world, &i915_dp_res);

	/* Persistent short replies fail. */
	i915_dp_fresh();
	i915_dp_script_all(I915_DP_FAKE_SCRIPT_MAX, I915_DP_FAKE_SHORT_REPLY, 0U);
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	end = drv_i915_edp_end(i915_dp_world, &i915_dp_res);
	released = i915_dp_released("short");
	i915_host_check(error == -I915_EDP_EPROTO && end == 0 && released, "persistent short replies: -EPROTO, released");
}

/* Faults of the channel itself. */
static void
i915_dp_test_channel(void)
{
	struct i915_edp_config config;
	int error;
	int end;
	int same;
	int released;

	config = i915_dp_vbt_config();

	/* Receive errors are retried by the hardware-try loop. */
	i915_dp_fresh();
	i915_dp_script_all(2U, I915_DP_FAKE_RECEIVE_ERROR, 0U);
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	i915_host_check(error == 0, "receive errors are retried by the hardware-try loop");
	(void)drv_i915_edp_end(i915_dp_world, &i915_dp_res);

	/* A forbidden message size 0 is retried, never unpacked. */
	i915_dp_fresh();
	i915_dp_script_all(1U, I915_DP_FAKE_BAD_SIZE_ZERO, 0U);
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	same = memcmp(i915_dp_res.dpcd, i915_dp_d000, 15U);
	i915_host_check(error == 0 && same == 0, "a forbidden message size 0 is -EBUSY and retried, never unpacked");
	(void)drv_i915_edp_end(i915_dp_world, &i915_dp_res);

	/* A forbidden message size 21 is retried, never unpacked. */
	i915_dp_fresh();
	i915_dp_script_all(1U, I915_DP_FAKE_BAD_SIZE_BIG, 0U);
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	same = memcmp(i915_dp_res.dpcd, i915_dp_d000, 15U);
	i915_host_check(error == 0 && same == 0, "a forbidden message size 21 is -EBUSY and retried, never unpacked");
	(void)drv_i915_edp_end(i915_dp_world, &i915_dp_res);

	/* A channel stuck busy for 25 ms: bounded waits, then recovery. */
	i915_dp_fresh();
	i915_dp_script_all(1U, I915_DP_FAKE_STUCK_BUSY, 25U);
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	end = drv_i915_edp_end(i915_dp_world, &i915_dp_res);
	released = i915_dp_released("stuck");
	i915_host_check(error == 0 && end == 0 && i915_dp_hw.wait_timeouts >= 1U && released,
			"a channel stuck busy for 25 ms: bounded waits, then recovery");

	/* A channel that stays busy: a finite failure. */
	i915_dp_fresh();
	i915_dp_script_all(I915_DP_FAKE_SCRIPT_MAX, I915_DP_FAKE_STUCK_BUSY, 250U);
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	end = drv_i915_edp_end(i915_dp_world, &i915_dp_res);
	released = i915_dp_released("stuck-for-ever");
	i915_host_check(error < 0 && end == 0 && released, "a channel that stays busy: finite failure, released");
}

/* Faults while the EDID is read. */
static void
i915_dp_test_edid(void)
{
	struct i915_edp_config config;
	uint8_t faults[12];
	uint8_t params[12];
	unsigned i;
	int error;
	int end;
	int same;
	int released;

	config = i915_dp_vbt_config();

	/* Probe, caps, probe, eDP caps, probe, link config = 6 native transactions, then five I2C DEFERs. */
	i915_dp_fresh();
	memset(faults, I915_DP_FAKE_OK, sizeof(faults));
	memset(params, 0, sizeof(params));
	for (i = 6U; i < 11U; i++)
		faults[i] = I915_DP_FAKE_I2C_DEFER;

	drv_i915_dp_fake_script(&i915_dp_hw, 11U, faults, params);
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	same = memcmp(i915_dp_res.edid, i915_dp_edid, 128U);
	i915_host_check(error == 0 && i915_dp_res.i2c_defers == 5U && same == 0, "5 I2C DEFERs during the EDID are retried");
	(void)drv_i915_edp_end(i915_dp_world, &i915_dp_res);

	/* A 16-byte I2C read answers with 4 bytes. */
	i915_dp_fresh();
	memset(faults, I915_DP_FAKE_OK, sizeof(faults));
	memset(params, 0, sizeof(params));
	faults[9] = I915_DP_FAKE_SHORT_REPLY;
	params[9] = 4U;
	drv_i915_dp_fake_script(&i915_dp_hw, 10U, faults, params);
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	same = memcmp(i915_dp_res.edid, i915_dp_edid, 128U);
	i915_host_check(error == 0 && same == 0, "a partial I2C read continues from where it stopped (no gap, no repeat)");
	(void)drv_i915_edp_end(i915_dp_world, &i915_dp_res);

	/* One I2C NACK: the block read is retried. */
	i915_dp_fresh();
	memset(faults, I915_DP_FAKE_OK, sizeof(faults));
	memset(params, 0, sizeof(params));
	faults[6] = I915_DP_FAKE_I2C_NACK;
	drv_i915_dp_fake_script(&i915_dp_hw, 7U, faults, params);
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	same = memcmp(i915_dp_res.edid, i915_dp_edid, 128U);
	i915_host_check(error == 0 && i915_dp_res.i2c_nacks == 1U && same == 0, "one I2C NACK: the block read is retried");
	(void)drv_i915_edp_end(i915_dp_world, &i915_dp_res);

	/* An EDID that never checksums. */
	i915_dp_fresh();
	i915_dp_hw.fault_every_i2c_read = I915_DP_FAKE_CORRUPT_DATA;
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	end = drv_i915_edp_end(i915_dp_world, &i915_dp_res);
	released = i915_dp_released("edid-corrupt");
	i915_host_check(error == -I915_EDP_EPROTO &&
			i915_dp_res.failed_stage == I915_EDP_STAGE_EDID &&
			end == 0 &&
			released,
			"an EDID that never checksums: -EPROTO after 4 tries, released");

	/* A truncated EEPROM: the rest reads 0xff. */
	i915_dp_fresh();
	i915_dp_hw.edid_size = 64U;
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	end = drv_i915_edp_end(i915_dp_world, &i915_dp_res);
	released = i915_dp_released("edid-truncated");
	i915_host_check(error == -I915_EDP_EPROTO && end == 0 && released, "a truncated EDID is rejected, released");

	/* An all-zero EDID stops at once. */
	i915_dp_fresh();
	memset(i915_dp_hw.edid, 0, sizeof(i915_dp_hw.edid));
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	end = drv_i915_edp_end(i915_dp_world, &i915_dp_res);
	released = i915_dp_released("edid-zero");
	i915_host_check(error == -I915_EDP_ENXIO && end == 0 && released, "an all-zero EDID stops at once (-ENXIO), released");
}

/* EDID extension blocks: one that is read, and a count beyond the buffer. */
static void
i915_dp_test_edid_extensions(void)
{
	struct i915_edp_config config;
	int error;
	int end;
	int same;
	int released;

	config = i915_dp_vbt_config();

	/* One valid extension block (a CTA block header) behind the base block. */
	i915_dp_fresh();
	i915_dp_hw.edid[126] = 1U;
	i915_dp_hw.edid[127] = (uint8_t)i915_dp_edid_checksum(i915_dp_hw.edid);
	i915_dp_hw.edid[128] = 0x02U;
	i915_dp_hw.edid[129] = 0x03U;
	i915_dp_hw.edid[255] = (uint8_t)i915_dp_edid_checksum(i915_dp_hw.edid + 128);
	i915_dp_hw.edid_size = 256U;
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	same = memcmp(i915_dp_res.edid, i915_dp_hw.edid, 256U);
	i915_host_check(error == 0 &&
			i915_dp_res.edid_blocks == 2U &&
			i915_dp_res.edid_extensions == 1U &&
			same == 0,
			"an extension block is read and checked");
	(void)drv_i915_edp_end(i915_dp_world, &i915_dp_res);

	/* A base block that claims 200 extensions: more than the buffer holds. */
	i915_dp_fresh();
	i915_dp_hw.edid[126] = 200U;
	i915_dp_hw.edid[127] = (uint8_t)i915_dp_edid_checksum(i915_dp_hw.edid);
	(void)drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	end = drv_i915_edp_end(i915_dp_world, &i915_dp_res);
	released = i915_dp_released("edid-ext");
	i915_host_check(i915_dp_res.edid_blocks <= I915_EDP_MAX_EDID_BLOCKS &&
			i915_dp_res.edid_extensions == 200U &&
			end == 0 &&
			released,
			"an extension count beyond the buffer never writes past it");
}

/* A failed power-domain get, and configurations the stage does not drive. */
static void
i915_dp_test_refusals(void)
{
	struct i915_edp_config config;
	int error;
	int end;

	/* A failed power-domain get is reported; the matching put is skipped. */
	config = i915_dp_vbt_config();
	i915_dp_fresh();
	i915_dp_hw.fail_power_get = 1;
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	end = drv_i915_edp_end(i915_dp_world, &i915_dp_res);
	i915_host_check(error < 0 &&
			i915_dp_res.power_get_failures != 0U &&
			i915_dp_res.power_put_underflows == 0U &&
			end == 0,
			"a failed power-domain get: reported, the matching put is skipped (no underflow)");

	/* A port other than A is refused. */
	i915_dp_fresh();
	config.port = 1;
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	i915_host_check(error == -I915_EDP_EINVAL, "a port other than A is refused");

	/* A Type-C AUX channel is refused. */
	config = i915_dp_vbt_config();
	config.aux_ch = 3;
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	i915_host_check(error == -I915_EDP_EINVAL, "a Type-C AUX channel is refused");

	/* A refused configuration left nothing live. */
	config = i915_dp_vbt_config();
	error = drv_i915_edp_begin(i915_dp_world, &i915_dp_env, &config, &i915_dp_res);
	i915_host_check(error == 0, "... and a refused configuration left nothing live");
	(void)drv_i915_edp_end(i915_dp_world, &i915_dp_res);
}
