/*
 * WS031 host test: the eDP first stage (PPS / VDD ownership, AUX, DPCD, EDID) -- the SAME
 * production files the kernel builds (parity_edp.c + the generated reference ports) -- against
 * the register model dp_fake_hw.c, with the captured target DPCD / EDID as the sink's content.
 *
 *   sh plan/ws031/tests/run-dp-host-test.sh
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parity_edp.h"
#include "dp_fake_hw.h"

void parity_vbt_emit(const char *text) { fputs(text, stdout); }
int parity_vbt_fmtcheck(const char *fmt, ...) { (void)fmt; return 0; }

static unsigned checks, failures;
#define CHECK(cond, msg) do { checks++; if (!(cond)) { failures++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static unsigned char d000[256], d100[256], d700[256], edid[128];
static struct dp_fake_hw hw;
static struct parity_dp_env env;
static struct parity_edp_result res;
static int verbose;

static void slurp(const char *dir, const char *name, unsigned char *buf, size_t n)
{
	char path[512];
	FILE *f;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	f = fopen(path, "rb");
	if (!f || fread(buf, 1, n, f) != n) { perror(path); exit(2); }
	fclose(f);
}

/* the target's VBT panel data (igt intel_vbt_decode: T3 2000, T7 800, T9 2000, T10 1100, T12 5000; controller 0) */
static struct parity_edp_config vbt_cfg(void)
{
	struct parity_edp_config c;

	memset(&c, 0, sizeof(c));
	c.port = 0; c.aux_ch = 0; c.rawclk_khz = 19200;
	c.t1_t3 = 2000; c.t8 = 800; c.t9 = 2000; c.t10 = 1100; c.t11_t12 = 5000;
	c.bl_controller = 0;
	c.log_level = verbose ? 2 : -1;
	return c;
}

static void fresh(void)
{
	dp_fake_init(&hw, d000, d100, d700, edid, sizeof(edid));
	dp_fake_bind_env(&hw, &env);
}

static void script1(unsigned n, unsigned fault, unsigned param)
{
	static uint8_t f[DP_FAKE_SCRIPT_MAX], p[DP_FAKE_SCRIPT_MAX];
	unsigned i;

	for (i = 0; i < n; i++) { f[i] = (uint8_t)fault; p[i] = (uint8_t)param; }
	dp_fake_script(&hw, n, f, p);
}

/* everything the stage may own is back: the pass criterion of every failure test */
static int released(const char *what)
{
	int ok = hw.refs_core == 0 && hw.refs_aux == 0 && (hw.pp_control & 8u) == 0 &&
		res.vdd_wakeref_held == 0 && res.vdd_work_pending == 0 && res.power_put_underflows == 0;

	if (!ok)
		printf("  [%s] refs core=%d aux=%d pp_control=0x%x wakeref=%d work=%d underflow=%u\n", what,
		       hw.refs_core, hw.refs_aux, hw.pp_control, res.vdd_wakeref_held, res.vdd_work_pending,
		       res.power_put_underflows);
	return ok;
}

int main(int argc, char **argv)
{
	struct parity_edp_config cfg;
	int rc, end;
	unsigned i;

	if (argc < 2) { fprintf(stderr, "usage: %s <display-ref dir> [-v]\n", argv[0]); return 2; }
	verbose = argc > 2;
	slurp(argv[1], "dpcd-drm_dp_aux0-000.bin", d000, 256);
	slurp(argv[1], "dpcd-drm_dp_aux0-100.bin", d100, 256);
	slurp(argv[1], "dpcd-drm_dp_aux0-700.bin", d700, 256);
	slurp(argv[1], "edid-eDP-1.bin", edid, 128);
	cfg = vbt_cfg();

	/* ---- 1. normal: acquisition, late init, delayed VDD-off worker, end ---- */
	fresh();
	rc = parity_edp_begin(&env, &cfg, &res);
	CHECK(rc == 0 && res.stage == PARITY_EDP_STAGE_ACQUIRED, "begin succeeds up to the EDID");
	CHECK(res.pps_valid == 1 && res.pps_idx == 0, "PPS 0 (the VBT's controller) is valid");
	CHECK(res.dpcd_ok && memcmp(res.dpcd, d000, 15) == 0, "DPCD receiver caps == the captured 0x000..0x00e");
	CHECK(res.dpcd[0] == 0x11 && res.dpcd[1] == 0x0a && (res.dpcd[2] & 0x1f) == 2, "DPCD 1.1, HBR, 2 lanes (captured panel)");
	CHECK(res.edp_dpcd_ok && memcmp(res.edp_dpcd, d700, 3) == 0, "eDP display-control caps == the captured 0x700..0x702");
	CHECK(res.link_cfg_ok && res.link_cfg[0] == d100[0] && res.link_cfg[1] == d100[1], "link config bytes read back as found");
	CHECK(res.edid_ok && res.edid_blocks == 1 && res.edid_extensions == 0 && memcmp(res.edid, edid, 128) == 0,
	      "EDID over I2C-over-AUX == the captured 128 bytes");
	CHECK(res.edid[8] == 0x06 && res.edid[9] == 0xaf && res.edid[10] == 0x99 && res.edid[11] == 0x2b,
	      "panel identity AUO 0x2b99");
	/* the reference's rule: max(register, VBT); T8/T9 written as 1; T11_T12 = VBT + 100 ms rounded up to 100 ms */
	CHECK(res.after_init.pp_on_delays == 0x07d00001u && res.after_init.pp_off_delays == 0x044c0001u,
	      "PP_ON/OFF_DELAYS = 0x07d00001 / 0x044c0001 (the values Linux programmed on the target)");
	CHECK(((res.after_init.pp_control >> 4) & 0x1f) == 6, "power-cycle field = 6 (600 ms)");
	CHECK(res.delay_power_up_ms == 200 && res.delay_power_down_ms == 110 && res.delay_power_cycle_ms == 600 &&
	      res.delay_bl_on_ms == 80 && res.delay_bl_off_ms == 200, "software delays 200/110/600/80/200 ms (i915 debugfs on the target)");
	CHECK(res.vdd_on_hw == 1 && res.vdd_wakeref_held == 1 && res.vdd_wanted == 0 && res.vdd_work_pending == 0,
	      "after acquisition: VDD forced on, its AUX reference held, no worker yet (initializing)");
	CHECK(res.power_refs_aux == 1 && res.power_refs_core == 0 && hw.refs_aux == 1 && hw.refs_core == 0,
	      "exactly the VDD's AUX reference is held between transfers");
	CHECK(hw.vdd_on_events == 1, "VDD was switched on once (kept on across the transfers)");
	CHECK(hw.aux_without_sink_power == 0 && hw.aux_without_aux_power == 0 && hw.aux_without_core_power == 0 &&
	      hw.pp_writes_without_core_power == 0, "no AUX transfer or PP write without its power");
	CHECK(hw.unknown_reg_reads == 0 && hw.unknown_reg_writes == 0, "no register outside PPS0 / AUX A / SOUTH_* touched");
	CHECK(env.slept_us >= 200000u, "the 200 ms panel power-up delay was waited before the first AUX transfer");
	CHECK(res.log_errors == 0, "no error-level message");
	if (verbose)
		printf("normal: aux_transactions=%u native r/w=%u/%u i2c r/w=%u/%u slept=%llu us elapsed=%llu ms\n",
		       hw.aux_transactions, hw.aux_native_reads, hw.aux_native_writes, hw.aux_i2c_reads,
		       hw.aux_i2c_writes, (unsigned long long)env.slept_us, (unsigned long long)res.elapsed_ms);

	CHECK(parity_edp_begin(&env, &cfg, &res) == -16, "a second live eDP is refused (-EBUSY)");
	/* the refused call cleared `res`; the live object is untouched */
	rc = parity_edp_init_late(&cfg, &res);
	CHECK(rc == -22, "init_late needs the result of the acquisition it follows");
	res.stage = PARITY_EDP_STAGE_ACQUIRED;
	rc = parity_edp_init_late(&cfg, &res);
	CHECK(rc == 0 && res.stage == PARITY_EDP_STAGE_LATE && res.vdd_work_pending == 1 && res.vdd_on_hw == 1,
	      "init_late: delayed VDD-off scheduled, VDD still on");
	CHECK(parity_edp_run_due_work(&res) == 0 && res.vdd_on_hw == 1, "the worker does not run before it is due");
	hw.now_us += 2999u * 1000u;
	CHECK(parity_edp_run_due_work(&res) == 0, "... not at 2999 ms (5 x the 600 ms power-cycle delay)");
	hw.now_us += 2u * 1000u;
	CHECK(parity_edp_run_due_work(&res) == 1 && res.vdd_on_hw == 0 && res.vdd_wakeref_held == 0 &&
	      res.power_refs_aux == 0, "due: the worker forces VDD off and returns the AUX reference");
	/* an AUX read after that takes VDD again on its own and hands it to the delayed worker */
	{
		uint8_t b[2];
		long n = parity_edp_dpcd_read(0x000, b, 2);

		CHECK(n == 2 && b[0] == 0x11 && hw.vdd_on_events == 2, "a later AUX read turns VDD on again by itself");
		CHECK(env.slept_us >= 800000u, "... after the power-cycle wait (T12) and the power-up delay");
	}
	end = parity_edp_end(&res);
	CHECK(end == 0 && released("normal"), "end: worker cancelled, VDD off, every reference returned");
	CHECK(hw.vdd_off_events == 2, "VDD off twice (worker, end)");
	CHECK(parity_edp_end(&res) == -22, "end without a live eDP is refused");

	/* ---- 2. end straight after the acquisition (no late init) ---- */
	fresh();
	rc = parity_edp_begin(&env, &cfg, &res);
	end = parity_edp_end(&res);
	CHECK(rc == 0 && end == 0 && released("end-after-begin"), "end right after begin releases VDD and its reference");

	/* ---- 3. delay selection ---- */
	fresh();
	hw.pp_on_delays = 0x0bb80001u;            /* firmware left T1+T3 = 3000 (300 ms) */
	rc = parity_edp_begin(&env, &cfg, &res);
	CHECK(rc == 0 && res.delay_power_up_ms == 300 && (res.after_init.pp_on_delays >> 16) == 3000,
	      "a larger firmware value wins over the VBT (max rule)");
	(void)parity_edp_end(&res);
	fresh();
	memset(&cfg.t1_t3, 0, 5 * sizeof(uint16_t));
	rc = parity_edp_begin(&env, &cfg, &res);
	CHECK(rc == 0 && res.delay_power_up_ms == 210 && res.delay_power_down_ms == 500 && res.delay_power_cycle_ms == 610 &&
	      ((res.after_init.pp_control >> 4) & 0x1f) == 7,
	      "no firmware value and no VBT value: eDP-spec limits (210/500/610 ms, cycle field 7)");
	cfg = vbt_cfg();
	res.stage = PARITY_EDP_STAGE_ACQUIRED;
	rc = parity_edp_init_late(&cfg, &res);
	CHECK(rc == 0 && res.delay_power_up_ms == 210 && res.delay_power_cycle_ms == 700,
	      "late init re-reads the delays: the registers now hold the spec values, which win by the max rule");
	(void)parity_edp_end(&res);
	CHECK(released("delays"), "released");

	/* ---- 4. VDD left on by firmware is adopted, then released ---- */
	fresh();
	hw.pp_control = 0x8u;
	hw.vdd_on_since_us = 1;
	rc = parity_edp_begin(&env, &cfg, &res);
	CHECK(rc == 0 && hw.vdd_on_events == 0 && res.power_refs_aux == 1, "firmware VDD adopted: one reference, no second switch-on");
	end = parity_edp_end(&res);
	CHECK(end == 0 && released("adopt"), "... and released at end");

	/* ---- 5. the sink never answers (VDD up but the panel needs longer than the delay) ---- */
	fresh();
	hw.sink_power_up_us = 3600u * 1000000u;
	rc = parity_edp_begin(&env, &cfg, &res);
	CHECK(rc == -110 && res.failed_stage == PARITY_EDP_STAGE_DPCD && !res.dpcd_ok, "no sink answer: -ETIMEDOUT at the DPCD stage");
	CHECK(res.vdd_on_hw == 0 && res.vdd_wakeref_held == 0, "the failure path already forced VDD off (out_vdd_off)");
	end = parity_edp_end(&res);
	CHECK(end == 0 && released("no-sink"), "released");
	CHECK(hw.aux_transactions == 32u * 5u, "32 retries x 5 hardware tries, then it gives up (finite)");

	/* ---- 6. native DEFER / NACK / invalid reply / short reply ---- */
	fresh(); script1(3, DP_FAKE_NATIVE_DEFER, 0);
	rc = parity_edp_begin(&env, &cfg, &res);
	CHECK(rc == 0 && memcmp(res.dpcd, d000, 15) == 0, "3 native DEFERs are retried");
	(void)parity_edp_end(&res);
	fresh(); script1(1, DP_FAKE_NATIVE_NACK, 0);
	rc = parity_edp_begin(&env, &cfg, &res);
	CHECK(rc == 0, "one native NACK is retried");
	(void)parity_edp_end(&res);
	fresh(); script1(40, DP_FAKE_NATIVE_NACK, 0);
	rc = parity_edp_begin(&env, &cfg, &res);
	end = parity_edp_end(&res);
	CHECK(rc == -5 && end == 0 && released("nack"), "persistent native NACK: -EIO after 32 tries, released");
	fresh(); script1(2, DP_FAKE_INVALID_REPLY, 0);
	rc = parity_edp_begin(&env, &cfg, &res);
	CHECK(rc == 0, "a reserved reply code is not taken as data");
	(void)parity_edp_end(&res);
	fresh();
	{
		static const uint8_t f[2] = { DP_FAKE_OK, DP_FAKE_SHORT_REPLY }, p[2] = { 0, 5 };

		dp_fake_script(&hw, 2, f, p);          /* the 1-byte probe is fine, the 15-byte read comes back with 5 */
	}
	rc = parity_edp_begin(&env, &cfg, &res);
	CHECK(rc == 0 && memcmp(res.dpcd, d000, 15) == 0, "a short DPCD reply is not accepted as the full read (-EPROTO, retried)");
	(void)parity_edp_end(&res);
	fresh(); script1(DP_FAKE_SCRIPT_MAX, DP_FAKE_SHORT_REPLY, 0);
	rc = parity_edp_begin(&env, &cfg, &res);
	end = parity_edp_end(&res);
	CHECK(rc == -71 && end == 0 && released("short"), "persistent short replies: -EPROTO, released");

	/* ---- 7. channel-level faults ---- */
	fresh(); script1(2, DP_FAKE_RECEIVE_ERROR, 0);
	rc = parity_edp_begin(&env, &cfg, &res);
	CHECK(rc == 0, "receive errors are retried by the hardware-try loop");
	(void)parity_edp_end(&res);
	fresh(); script1(1, DP_FAKE_BAD_SIZE_ZERO, 0);
	rc = parity_edp_begin(&env, &cfg, &res);
	CHECK(rc == 0 && memcmp(res.dpcd, d000, 15) == 0, "a forbidden message size 0 is -EBUSY and retried, never unpacked");
	(void)parity_edp_end(&res);
	fresh(); script1(1, DP_FAKE_BAD_SIZE_BIG, 0);
	rc = parity_edp_begin(&env, &cfg, &res);
	CHECK(rc == 0 && memcmp(res.dpcd, d000, 15) == 0, "a forbidden message size 21 is -EBUSY and retried, never unpacked");
	(void)parity_edp_end(&res);
	fresh(); script1(1, DP_FAKE_STUCK_BUSY, 25);
	rc = parity_edp_begin(&env, &cfg, &res);
	end = parity_edp_end(&res);
	CHECK(rc == 0 && end == 0 && hw.wait_timeouts >= 1 && released("stuck"), "a channel stuck busy for 25 ms: bounded waits, then recovery");
	fresh(); script1(DP_FAKE_SCRIPT_MAX, DP_FAKE_STUCK_BUSY, 250);
	rc = parity_edp_begin(&env, &cfg, &res);
	end = parity_edp_end(&res);
	CHECK(rc < 0 && end == 0 && released("stuck-for-ever"), "a channel that stays busy: finite failure, released");

	/* ---- 8. EDID faults ---- */
	fresh();
	{
		/* probe, caps, probe, edp caps, probe, link cfg = 6 native transactions, then the EDID's I2C ones */
		uint8_t f[12], p[12];

		memset(f, DP_FAKE_OK, sizeof(f)); memset(p, 0, sizeof(p));
		for (i = 6; i < 11; i++) f[i] = DP_FAKE_I2C_DEFER;
		dp_fake_script(&hw, 11, f, p);
	}
	rc = parity_edp_begin(&env, &cfg, &res);
	CHECK(rc == 0 && res.i2c_defers == 5 && memcmp(res.edid, edid, 128) == 0, "5 I2C DEFERs during the EDID are retried");
	(void)parity_edp_end(&res);
	fresh();
	{
		uint8_t f[10], p[10];

		memset(f, DP_FAKE_OK, sizeof(f)); memset(p, 0, sizeof(p));
		f[9] = DP_FAKE_SHORT_REPLY; p[9] = 4;  /* a 16-byte I2C read answers with 4 bytes */
		dp_fake_script(&hw, 10, f, p);
	}
	rc = parity_edp_begin(&env, &cfg, &res);
	CHECK(rc == 0 && memcmp(res.edid, edid, 128) == 0, "a partial I2C read continues from where it stopped (no gap, no repeat)");
	(void)parity_edp_end(&res);
	fresh();
	{
		uint8_t f[7], p[7];

		memset(f, DP_FAKE_OK, sizeof(f)); memset(p, 0, sizeof(p));
		f[6] = DP_FAKE_I2C_NACK;
		dp_fake_script(&hw, 7, f, p);
	}
	rc = parity_edp_begin(&env, &cfg, &res);
	CHECK(rc == 0 && res.i2c_nacks == 1 && memcmp(res.edid, edid, 128) == 0, "one I2C NACK: the block read is retried");
	(void)parity_edp_end(&res);
	fresh();
	hw.fault_every_i2c_read = DP_FAKE_CORRUPT_DATA;
	rc = parity_edp_begin(&env, &cfg, &res);
	end = parity_edp_end(&res);
	CHECK(rc == -71 && res.failed_stage == PARITY_EDP_STAGE_EDID && end == 0 && released("edid-corrupt"),
	      "an EDID that never checksums: -EPROTO after 4 tries, released");
	fresh();
	hw.edid_size = 64;                        /* a truncated EEPROM: the rest reads 0xff */
	rc = parity_edp_begin(&env, &cfg, &res);
	end = parity_edp_end(&res);
	CHECK(rc == -71 && end == 0 && released("edid-truncated"), "a truncated EDID is rejected, released");
	fresh();
	memset(hw.edid, 0, sizeof(hw.edid));
	rc = parity_edp_begin(&env, &cfg, &res);
	end = parity_edp_end(&res);
	CHECK(rc == -6 && end == 0 && released("edid-zero"), "an all-zero EDID stops at once (-ENXIO), released");
	fresh();
	{
		unsigned sum = 0;

		hw.edid[126] = 1;                     /* one extension */
		for (i = 0, sum = 0; i < 127; i++) sum += hw.edid[i];
		hw.edid[127] = (uint8_t)(0x100u - (sum & 0xffu));
		hw.edid[128] = 0x02; hw.edid[129] = 0x03;
		for (i = 128, sum = 0; i < 255; i++) sum += hw.edid[i];
		hw.edid[255] = (uint8_t)(0x100u - (sum & 0xffu));
		hw.edid_size = 256;
	}
	rc = parity_edp_begin(&env, &cfg, &res);
	CHECK(rc == 0 && res.edid_blocks == 2 && res.edid_extensions == 1 && memcmp(res.edid, hw.edid, 256) == 0,
	      "an extension block is read and checked");
	(void)parity_edp_end(&res);
	fresh();
	{
		unsigned sum = 0;

		hw.edid[126] = 200;                   /* claims 200 extensions: more than the buffer holds */
		for (i = 0; i < 127; i++) sum += hw.edid[i];
		hw.edid[127] = (uint8_t)(0x100u - (sum & 0xffu));
	}
	rc = parity_edp_begin(&env, &cfg, &res);
	end = parity_edp_end(&res);
	CHECK(res.edid_blocks <= PARITY_EDP_MAX_EDID_BLOCKS && res.edid_extensions == 200 && end == 0 && released("edid-ext"),
	      "an extension count beyond the buffer never writes past it");

	/* ---- 9. power reference failure, bad configuration ---- */
	fresh();
	hw.fail_power_get = 1;
	rc = parity_edp_begin(&env, &cfg, &res);
	end = parity_edp_end(&res);
	CHECK(rc < 0 && res.power_get_failures != 0 && res.power_put_underflows == 0 && end == 0,
	      "a failed power-domain get: reported, the matching put is skipped (no underflow)");
	fresh();
	cfg.port = 1;
	CHECK(parity_edp_begin(&env, &cfg, &res) == -22, "a port other than A is refused");
	cfg = vbt_cfg(); cfg.aux_ch = 3;
	CHECK(parity_edp_begin(&env, &cfg, &res) == -22, "a Type-C AUX channel is refused");
	cfg = vbt_cfg();
	rc = parity_edp_begin(&env, &cfg, &res);
	CHECK(rc == 0, "... and a refused configuration left nothing live");
	(void)parity_edp_end(&res);

	printf("dp_host_test: %u checks, %u failures\n", checks, failures);
	return failures != 0;
}
