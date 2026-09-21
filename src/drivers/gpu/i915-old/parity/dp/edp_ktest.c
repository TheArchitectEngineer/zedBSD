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
#include "../lcd/parity_lcd_calc.h"

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
	int ok = env.power_refs[0] == 0 && env.power_refs[1] == 0 && (hw.pp_control & 8u) == 0u &&
		res.vdd_wakeref_held == 0 && res.vdd_work_pending == 0 && res.power_put_underflows == 0u &&
		hw.lock_held[0] == 0 && hw.lock_held[1] == 0 && hw.lock_errors == 0u && env.lock_errors == 0u;

	dp_fake_flush_async(&hw);              /* what was put asynchronously is the power layer's until flushed */
	return ok && hw.refs_core == 0 && hw.refs_aux == 0;
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
	check(rc == 0 && res.vdd_work_pending == 1 && dp_fake_run_due(&hw) == 0u && (hw.pp_control & 8u) != 0u,
		"edp: EDP-WORKER late init schedules the VDD-off worker; it does not run before 5 x the power-cycle delay");
	hw.now_us += 3001u * 1000u;
	rc = (int)dp_fake_run_due(&hw);
	parity_edp_snapshot(&res);
	check(rc == 1 && res.vdd_on_hw == 0 && res.vdd_wakeref_held == 0 && res.power_refs_aux == 0,
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

	/* LCD-A first slice, from the same captured panel data: the values Linux programmed on the target */
	{
		static struct parity_lcd_state lcd;

		rc = parity_lcd_compute(dp_fixture_edid, dp_fixture_dpcd_000, dp_fixture_dpcd_700, 18, 38400, &lcd);
		check(rc == 0 && lcd.mode.clock_khz == 140800 && lcd.mode.hdisplay == 1920u && lcd.mode.htotal == 2080u &&
			lcd.mode.hsync_start == 1936u && lcd.mode.hsync_end == 1952u && lcd.mode.vdisplay == 1080u &&
			lcd.mode.vtotal == 1128u && lcd.mode.vsync_start == 1083u && lcd.mode.vsync_end == 1097u &&
			lcd.mode.edid_bpc == 6 && lcd.link.bpp == 18 && lcd.link.rate_khz == 270000 && lcd.link.lanes == 2,
			"lcd: LCD-A-MODE 1920x1080 140.8 MHz timings, 6 bpc / 18 bpp, HBR x2 (Linux transcoder A + DDI A values)");
		check(rc == 0 && lcd.link.required_kbps == 316800 && lcd.link.available_kbps == 540000 && lcd.link.tu == 64u &&
			lcd.link.data_m == 0x4b17e4u && lcd.link.data_n == 0x800000u && lcd.link.link_m == 273406u &&
			lcd.link.link_n == 524288u && lcd.pll.cfgcr0 == 0x00e001a5u && lcd.pll.cfgcr1 == 0x88u && lcd.notes == 0u,
			"lcd: LCD-A-LINK data/link M/N and DPLL CFGCR0/1 equal the PIPE_DATA/LINK_M1/N1 and DPLL0 words Linux wrote");
		{
			static struct parity_lcd_words words;

			rc = parity_lcd_emit_transcoder(&lcd, 0, 0, 1920u, 1080u, &words);
			check(rc == 0 && words.n == 13u && words.w[0].reg == 0x60030u && words.w[0].value == 0x7e4b17e4u &&
				words.w[3].reg == 0x60044u && words.w[3].value == 0x00080000u && words.w[6].reg == 0x60000u &&
				words.w[6].value == 0x081f077fu && words.w[8].value == 0x079f078fu && words.w[9].value == 0x04670437u &&
				words.w[10].value == 0x04670000u && words.w[11].value == 0x0448043au && words.w[12].reg == 0x6001cu &&
				words.w[12].value == 0x077f0437u,
				"lcd: LCD-A-WORDS the reference's transcoder / M-N / PIPESRC writers emit the words of Linux's register dump, LINK_N last");
		}
		{
			static struct parity_lcd_words cw, dw;
			uint32_t buf = 0u, func = 0u;

			rc = parity_lcd_emit_cpu_transcoder(&lcd, 0, 0, &cw);
			check(rc == 0 && cw.n == 17u && cw.w[3].reg == 0x60044u && cw.w[11].reg == 0x60014u && cw.w[12].rmw == 1u &&
				cw.w[12].reg == 0x420c0u && cw.w[12].value == 0x80000000u && cw.w[13].reg == 0x60420u && cw.w[14].reg == 0x6002cu &&
				cw.w[15].rmw == 1u && cw.w[15].clear == 0x18000000u && cw.w[16].reg == 0x70008u && cw.w[16].value == 0u &&
				parity_lcd_words_find(&cw, 0x6001cu, 0) == 0u,
				"lcd: LCD-A-CPU-TRANSCODER the reference's hsw_configure_cpu_transcoder orders M/N, timings, VRR, MULT, frame start delay, TRANSCONF (no enable bit)");
			{
				static struct parity_lcd_words pw;
				uint32_t ctl = 0u, stride = 0u, size = 0u, color = 0u, surf = 0u;

				rc = parity_lcd_emit_plane(0, 0, 0x34325258u, 0ull, 1920u, 1080u, 7680u, 0x00180000u, &pw);
				check(rc == 0 && parity_lcd_words_find(&pw, 0x70180u, &ctl) == 1u && ctl == 0x94000000u &&
					parity_lcd_words_find(&pw, 0x70188u, &stride) == 1u && stride == 0x78u &&
					parity_lcd_words_find(&pw, 0x70190u, &size) == 1u && size == 0x0437077fu &&
					parity_lcd_words_find(&pw, 0x701ccu, &color) == 1u && color == 0x2000u &&
					parity_lcd_words_find(&pw, 0x7019cu, &surf) == 1u && surf == 0x00180000u && pw.n >= 2u &&
					pw.w[pw.n - 1u].reg == 0x7019cu && pw.w[pw.n - 2u].reg == 0x70180u,
					"lcd: LCD-A-PLANE-WORDS PLANE_CTL / STRIDE / SIZE / COLOR_CTL / SURF equal Linux's dump, PLANE_SURF last");
			}
			rc = parity_lcd_emit_ddi(&lcd, 0, 0, 0, 0u, &dw, &buf);
			check(rc == 0 && dw.n == 3u && dw.w[0].reg == 0x60410u && dw.w[0].value == 1u && dw.w[1].reg == 0x60404u &&
				parity_lcd_words_find(&dw, 0x60400u, &func) == 1u && func == 0x8a210002u && buf == 0x00000002u,
				"lcd: LCD-A-DDI-WORDS TRANS_DDI_FUNC_CTL equals Linux's dump (0x8a210002), DDI_BUF_CTL value + enable = 0x80000002, MSA 6 bpc");
		}
		{
			uint8_t one_lane[16];

			memcpy(one_lane, dp_fixture_dpcd_000, sizeof(one_lane));
			one_lane[1] = 0x06u; one_lane[2] = 0x01u;
			check(parity_lcd_compute(dp_fixture_edid, one_lane, dp_fixture_dpcd_700, 18, 38400, &lcd) == -28,
				"lcd: LCD-A-FIT a link that cannot carry the mode is refused (RBR x1)");
		}
	}

	fresh();
	cfg.aux_ch = 3;
	check(parity_edp_begin(&env, &cfg, &res) == -22,
		"edp: EDP-CONFIG a Type-C AUX channel is refused");
}
