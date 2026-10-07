/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws051-p003: the host test of the Type-C PLL and PHY text of the display
 * (src/drivers/gpu/i915/display/clock.c and phy.c):
 *
 *   - the DKL PLL words of the 5330's USB-C DP link (TC PLL 2 at RBR on a
 *     38.4 MHz reference, plan/ws051/tests/m3-5330-20261007) are what the
 *     calculation gives, and the readout of the registers as Linux left
 *     them gives the same words and the same link clock;
 *   - the calculation and the frequency agree with Linux v6.8.12's own
 *     text (host-dkl-linux.c) for every DP rate and an HDMI clock sweep on
 *     the three reference clocks;
 *   - the DKL PLL's enable powers, programs and enables Alder Lake-P's
 *     PORTTC PLL enable through the port's DKL window, and the
 *     Thunderbolt PLL keeps the reference's description;
 *   - a Type-C port takes Alder Lake-P's DKL buffer tables.
 *
 *   sh plan/ws051/tests/host-dkl.sh
 */

#include "drivers/gpu/i915/display/modeset-internal.h"
#include "drivers/gpu/i915/display/takeover-internal.h"
#include "drivers/gpu/i915/display/clock.h"
#include "drivers/gpu/i915/display/state.h"
#include "drivers/gpu/i915/tests/display/host-test.h"

#include "host-dkl-linux.h"

#include <stdio.h>
#include <string.h>

/* How many registers the fake keeps. */
#define FAKE_REGS		256

/* The 5330's TC2 (DDI port E, Type-C port 1 from 0). */
#define M3_TC_PORT		TC_PORT_2
#define M3_REF_KHZ		38400
#define M3_PORT_CLOCK		162000

/* The 5330's TC2 DKL window and its bank index register (bank 2 holds the PLL). */
#define M3_WINDOW		0x169000u
#define M3_HIP_INDEX		0x1010a0u
#define M3_HIP_BANK2		(2u << 8)

/*
 * The fake display registers: a value per register an access named, the
 * writes counted, and the last value written to the bank index register
 * before each window access (the bank the access went to).
 */
struct fake_regs {
	uint32_t reg[FAKE_REGS];
	uint32_t value[FAKE_REGS];
	unsigned regs;
	unsigned writes;
};

/* The fake every test uses, reset by fake_reset(). */
static struct fake_regs fake;

static void fake_reset(void);
static uint32_t *fake_slot(uint32_t reg);
static uint32_t fake_get(uint32_t reg);
static void fake_set(uint32_t reg, uint32_t value);
static void emit_write32(void *ctx, uint32_t reg, uint32_t value);
static uint32_t emit_rmw32(void *ctx, uint32_t reg, uint32_t clear, uint32_t set);
static uint32_t emit_read32(void *ctx, uint32_t reg);
static void emit_step(void *ctx, const char *name);
static void emit_error(void *ctx, const char *what);
static void error_hook(void *ctx, const char *what);
static void bind_device(struct drm_i915_private *i915, struct i915_lcd_emit *emit);
static void words_of(const struct intel_dpll_hw_state *state, struct host_dkl_words *words);
static int words_equal(const struct host_dkl_words *a, const struct host_dkl_words *b);
static void test_m3_calc(void);
static void test_m3_readout(void);
static int sweep_case(int clock, int is_hdmi, int ref_khz);
static void test_linux_sweep(void);
static void test_enable(void);
static void test_tbt_describe(void);
static void test_buf_trans(void);
static void test_pool(void);

/*
 * The 5330's TC PLL 2 words, as debugfs i915_shared_dplls_info reported
 * them (mg_pll_div2 there is div1).
 */
static const struct host_dkl_words m3_words = {
	0x100u, 0xa00u, 0x6200u, 0x84269u, 0x1c0027u, 0x40002000u, 0x5e000000u, 0x52u
};

/* The steps and errors the text reported, counted. */
static unsigned steps;
static unsigned errors;

/*
 * The world the Linux text's errors are counted in (drv_i915_lcd_error()
 * reports into the bound world, which hands each to error_hook).  Zeroed,
 * bound once by main().
 */
static struct i915_lcd_world error_world;

/* The world whose PLL pool test_pool() builds (ws051-p004b).  Zeroed, used once. */
static struct i915_lcd_world pool_world;

/*
 * Runs the checks and reports the tally.
 */
int
main(int argc, char **argv)
{
	int report;
	int verbose_asked;

	/* -v prints every check. */
	verbose_asked = 0;
	if (argc > 1)
		verbose_asked = (strcmp(argv[1], "-v") == 0);
	if (verbose_asked)
		i915_host_verbose = 1;

	/* Counts the Linux text's errors. */
	drv_i915_lcd_error_bind(&error_world, error_hook, NULL);

	/* Runs each part. */
	test_m3_calc();
	test_m3_readout();
	test_linux_sweep();
	test_enable();
	test_tbt_describe();
	test_buf_trans();
	test_pool();

	/* Reports the tally. */
	report = i915_host_report("host-dkl");
	return report;
}

/* Empties the fake registers. */
static void
fake_reset(void)
{
	/* Nothing is held. */
	memset(&fake, 0, sizeof(fake));
	steps = 0;
	errors = 0;
}

/* Finds a register's slot in the fake, making one for a new register. */
static uint32_t *
fake_slot(
	uint32_t reg)
{
	unsigned index;

	/* Looks for the register. */
	for (index = 0; index < fake.regs; index++) {
		if (fake.reg[index] == reg)
			return &fake.value[index];
	}

	/* A full fake ends the test. */
	if (fake.regs == FAKE_REGS) {
		fprintf(stderr, "host-dkl: the fake ran out of registers\n");
		return &fake.value[0];
	}

	/* Makes a slot holding 0. */
	fake.reg[fake.regs] = reg;
	fake.value[fake.regs] = 0;
	fake.regs++;

	/* Succeeded: the new slot. */
	return &fake.value[fake.regs - 1];
}

/* Reads a fake register. */
static uint32_t
fake_get(
	uint32_t reg)
{
	uint32_t *slot;

	/* Finds the slot. */
	slot = fake_slot(reg);

	/* Succeeded: the value. */
	return *slot;
}

/* Sets a fake register. */
static void
fake_set(
	uint32_t reg,
	uint32_t value)
{
	uint32_t *slot;

	/* Finds the slot and stores the value. */
	slot = fake_slot(reg);
	*slot = value;
}

/* The write hook of the fake device. */
static void
emit_write32(
	void *ctx,
	uint32_t reg,
	uint32_t value)
{
	UNUSED_PARAMETER(ctx);

	/* Stores and counts the write. */
	fake_set(reg, value);
	fake.writes++;
}

/* The read-modify-write hook of the fake device. */
static uint32_t
emit_rmw32(
	void *ctx,
	uint32_t reg,
	uint32_t clear,
	uint32_t set)
{
	uint32_t old;

	UNUSED_PARAMETER(ctx);

	/* Changes the register and counts the write. */
	old = fake_get(reg);
	fake_set(reg, (old & ~clear) | set);
	fake.writes++;

	/* Succeeded: the value before. */
	return old;
}

/* The read hook of the fake device. */
static uint32_t
emit_read32(
	void *ctx,
	uint32_t reg)
{
	uint32_t value;

	UNUSED_PARAMETER(ctx);

	/* Reads the register. */
	value = fake_get(reg);

	/* Succeeded: the value. */
	return value;
}

/* The step hook: counts an unported callee the text reached. */
static void
emit_step(
	void *ctx,
	const char *name)
{
	UNUSED_PARAMETER(ctx);

	/* Counts and shows the step. */
	steps++;
	if (i915_host_verbose)
		printf("  step: %s\n", name);
}

/* The error hook: counts an error of the text. */
static void
emit_error(
	void *ctx,
	const char *what)
{
	UNUSED_PARAMETER(ctx);

	/* Counts and shows the error. */
	errors++;
	printf("  error: %s", what);
}

/* The error sink of the Linux text: counts the error as the error hook does. */
static void
error_hook(
	void *ctx,
	const char *what)
{
	/* Counts it like an error of the device. */
	emit_error(ctx, what);
}

/* Binds a device view to the fake registers: no waits, no power hooks, no Type-C hooks. */
static void
bind_device(
	struct drm_i915_private *i915,
	struct i915_lcd_emit *emit)
{
	/* The register hooks and the reporters only. */
	memset(emit, 0, sizeof(*emit));
	emit->model = 1;
	emit->write32 = emit_write32;
	emit->rmw32 = emit_rmw32;
	emit->read32 = emit_read32;
	emit->step = emit_step;
	emit->error = emit_error;

	/* The device of the 5330's reference clock. */
	memset(i915, 0, sizeof(*i915));
	i915->emit = emit;
	i915->display.dpll.ref_clks.nssc = M3_REF_KHZ;
}

/* Copies a PLL state's DKL words into plain words. */
static void
words_of(
	const struct intel_dpll_hw_state *state,
	struct host_dkl_words *words)
{
	/* One word per register. */
	words->refclkin_ctl = state->mg_refclkin_ctl;
	words->clktop2_coreclkctl1 = state->mg_clktop2_coreclkctl1;
	words->clktop2_hsclkctl = state->mg_clktop2_hsclkctl;
	words->div0 = state->mg_pll_div0;
	words->div1 = state->mg_pll_div1;
	words->ssc = state->mg_pll_ssc;
	words->bias = state->mg_pll_bias;
	words->tdc_coldst_bias = state->mg_pll_tdc_coldst_bias;
}

/* Tells whether two sets of words are the same. */
static int
words_equal(
	const struct host_dkl_words *a,
	const struct host_dkl_words *b)
{
	int difference;

	/* The words have no padding: compares them whole. */
	difference = memcmp(a, b, sizeof(*a));
	if (difference != 0)
		return 0;

	/* Succeeded: the same words. */
	return 1;
}

/* The 5330's TC PLL 2 at RBR: the calculation gives Linux's programmed words. */
static void
test_m3_calc(void)
{
	struct intel_dpll_hw_state state;
	struct host_dkl_words words;
	int calc_result;
	int freq;

	/* Computes the words of a 4-lane RBR DP link on a 38.4 MHz reference. */
	calc_result = drv_i915_dkl_pll_calc(M3_PORT_CLOCK, 0, M3_REF_KHZ, &state);
	i915_host_check(calc_result == 0, "m3: the DKL PLL words of RBR on 38.4 MHz are computed");

	/* The words are those of the 5330's TC PLL 2. */
	words_of(&state, &words);
	i915_host_check(words.refclkin_ctl == m3_words.refclkin_ctl, "m3: refclkin_ctl 0x100");
	i915_host_check(words.clktop2_coreclkctl1 == m3_words.clktop2_coreclkctl1, "m3: clktop2_coreclkctl1 0xa00");
	i915_host_check(words.clktop2_hsclkctl == m3_words.clktop2_hsclkctl, "m3: clktop2_hsclkctl 0x6200");
	i915_host_check(words.div0 == m3_words.div0, "m3: pll_div0 0x84269");
	i915_host_check(words.div1 == m3_words.div1, "m3: pll_div1 0x1c0027");
	i915_host_check(words.ssc == m3_words.ssc, "m3: pll_ssc 0x40002000");
	i915_host_check(words.bias == m3_words.bias, "m3: pll_bias 0x5e000000");
	i915_host_check(words.tdc_coldst_bias == m3_words.tdc_coldst_bias, "m3: pll_tdc_coldst_bias 0x52");

	/* The words give the link clock back. */
	freq = drv_i915_dkl_pll_freq(M3_REF_KHZ, &state);
	i915_host_check(freq == M3_PORT_CLOCK, "m3: the words give 162000 kHz");
}

/*
 * The 5330's TC PLL 2 as Linux left it (the window read with bank 2
 * selected, plan/ws051/tests/m3-5330-20261007/range-0x160000.txt, and
 * PORTTC2_PLL_ENABLE 0xcc000000): the readout keeps the PLL's fields and
 * gives debugfs's words.
 */
static void
test_m3_readout(void)
{
	struct drm_i915_private i915;
	struct i915_lcd_emit emit;
	struct dpll_info info;
	struct intel_shared_dpll pll;
	struct intel_dpll_hw_state state;
	struct host_dkl_words words;
	bool enabled;
	int freq;

	/* The fake device with the registers as the dump shows them. */
	fake_reset();
	bind_device(&i915, &emit);
	fake_set(0x46040u, 0xcc000000u);
	fake_set(M3_WINDOW + 0x12cu, 0x00000101u);
	fake_set(M3_WINDOW + 0x0d4u, 0x0000621du);
	fake_set(M3_WINDOW + 0x0d8u, 0x10080a10u);
	fake_set(M3_WINDOW + 0x200u, 0x7e284269u);
	fake_set(M3_WINDOW + 0x204u, 0x0cdcc427u);
	fake_set(M3_WINDOW + 0x210u, 0x400020ffu);
	fake_set(M3_WINDOW + 0x214u, 0xde000000u);
	fake_set(M3_WINDOW + 0x218u, 0x00000052u);

	/* TC PLL 2 as Alder Lake-P's pool describes it. */
	memset(&pll, 0, sizeof(pll));
	drv_i915_dkl_pll_describe(&info, M3_TC_PORT);
	pll.info = &info;
	i915_host_check(info.id == DPLL_ID_ICL_MGPLL2, "m3 readout: TC2's PLL is TC PLL 2 (id 4)");
	i915_host_check(strcmp(info.name, "TC PLL 2") == 0, "m3 readout: TC2's PLL is named TC PLL 2");

	/* Reads the PLL out. */
	memset(&state, 0, sizeof(state));
	enabled = info.funcs->get_hw_state(&i915, &pll, &state);
	i915_host_check(enabled, "m3 readout: TC PLL 2 reads as enabled (PORTTC2_PLL_ENABLE at 0x46040)");

	/* The bank index selected bank 2 for TC2. */
	i915_host_check(fake_get(M3_HIP_INDEX) == M3_HIP_BANK2, "m3 readout: the bank index selects bank 2 for TC2");

	/* The words are debugfs's. */
	words_of(&state, &words);
	i915_host_check(words_equal(&words, &m3_words), "m3 readout: the read words are debugfs's TC PLL 2 words");

	/* The link clock is the link's. */
	freq = info.funcs->get_freq(&i915, &pll, &state);
	i915_host_check(freq == M3_PORT_CLOCK, "m3 readout: the read words give 162000 kHz");
	i915_host_check(errors == 0, "m3 readout: no error of the text");

	/* A PLL whose enable bit is clear is not read. */
	fake_set(0x46040u, 0u);
	enabled = info.funcs->get_hw_state(&i915, &pll, &state);
	i915_host_check(!enabled, "m3 readout: TC PLL 2 with PLL_ENABLE clear reads as disabled");
}

/*
 * Compares one link clock with Linux's text: 1 when both refuse it, 0 when
 * both give the same words and the same frequency, -1 (and a failed check
 * naming the case) when they differ.
 */
static int
sweep_case(
	int clock,
	int is_hdmi,
	int ref_khz)
{
	struct intel_dpll_hw_state state;
	struct host_dkl_words words;
	struct host_dkl_words linux_words;
	int zed_result;
	int linux_result;
	int zed_freq;
	int linux_freq;
	int same_words;
	const char *kind;
	char what[160];

	/* Names the output kind for a failure. */
	kind = "DP";
	if (is_hdmi)
		kind = "HDMI";

	/* Computes the words both ways. */
	zed_result = drv_i915_dkl_pll_calc(clock, is_hdmi, ref_khz, &state);
	linux_result = host_dkl_linux_calc(clock, is_hdmi, ref_khz, &linux_words);
	words_of(&state, &words);

	/* A clock one refuses and the other does not is a difference. */
	if ((zed_result != 0) != (linux_result != 0)) {
		snprintf(what, sizeof(what), "sweep: %s %d on %d: refusal differs (%d, Linux %d)",
			 kind, clock, ref_khz, zed_result, linux_result);
		i915_host_check(0, what);
		return -1;
	}

	/* A clock both refuse agrees. */
	if (zed_result != 0)
		return 1;

	/* Compares the words and the frequency they give. */
	zed_freq = drv_i915_dkl_pll_freq(ref_khz, &state);
	linux_freq = host_dkl_linux_freq(ref_khz, &linux_words);
	same_words = words_equal(&words, &linux_words);
	if (!same_words || zed_freq != linux_freq) {
		snprintf(what, sizeof(what), "sweep: %s %d on %d: words or frequency differ",
			 kind, clock, ref_khz);
		i915_host_check(0, what);
		return -1;
	}

	/* Succeeded: the same words and frequency. */
	return 0;
}

/*
 * The calculation and the frequency agree with Linux's text for every DP
 * rate and for HDMI clocks from 25 MHz to 600 MHz, every 250 kHz, on the
 * three reference clocks.
 */
static void
test_linux_sweep(void)
{
	static const int dp_rates[] = { 162000, 216000, 243000, 270000, 324000, 432000, 540000, 810000 };
	static const int refs[] = { 19200, 24000, 38400 };
	unsigned r;
	unsigned k;
	int clock;
	int outcome;
	unsigned compared;
	unsigned mismatches;
	unsigned refusals;
	char what[160];

	/* Compares every case on every reference clock. */
	compared = 0;
	mismatches = 0;
	refusals = 0;
	for (r = 0; r < sizeof(refs) / sizeof(refs[0]); r++) {
		/* The DP rates, then the HDMI clocks. */
		for (k = 0; k < sizeof(dp_rates) / sizeof(dp_rates[0]); k++) {
			outcome = sweep_case(dp_rates[k], 0, refs[r]);
			compared++;
			if (outcome < 0) {
				mismatches++;
			} else if (outcome > 0) {
				refusals++;
			}
		}

		/* The HDMI clocks. */
		for (clock = 25000; clock <= 600000; clock += 250) {
			outcome = sweep_case(clock, 1, refs[r]);
			compared++;
			if (outcome < 0) {
				mismatches++;
			} else if (outcome > 0) {
				refusals++;
			}
		}
	}

	/* One check for the whole sweep. */
	snprintf(what, sizeof(what), "sweep: %u cases (%u refused by both) agree with Linux's text, %u differ",
		 compared, refusals, mismatches);
	i915_host_check(mismatches == 0 && compared > 6900u, what);
}

/*
 * The DKL PLL's enable on Alder Lake-P: PORTTC2_PLL_ENABLE gets the power
 * request and the enable, the DKL window (bank 2) gets the words, and the
 * readout gives the words back.
 */
static void
test_enable(void)
{
	struct drm_i915_private i915;
	struct i915_lcd_emit emit;
	struct dpll_info info;
	struct intel_shared_dpll pll;
	struct intel_dpll_hw_state readback;
	struct host_dkl_words words;
	bool enabled;
	int calc_result;

	/* The fake device, with the window holding bits the PLL does not own. */
	fake_reset();
	bind_device(&i915, &emit);
	fake_set(M3_WINDOW + 0x200u, 0x7e000000u);
	fake_set(M3_WINDOW + 0x12cu, 0x00000001u);

	/* TC PLL 2 with the 5330's words. */
	memset(&pll, 0, sizeof(pll));
	drv_i915_dkl_pll_describe(&info, M3_TC_PORT);
	pll.info = &info;
	calc_result = drv_i915_dkl_pll_calc(M3_PORT_CLOCK, 0, M3_REF_KHZ, &pll.state.hw_state);
	i915_host_check(calc_result == 0, "enable: the words are computed");

	/* Turns it on. */
	info.funcs->enable(&i915, &pll);

	/* PORTTC2_PLL_ENABLE holds the power request and the enable. */
	i915_host_check((fake_get(0x46040u) & (PLL_POWER_ENABLE | PLL_ENABLE)) == (PLL_POWER_ENABLE | PLL_ENABLE),
			"enable: PORTTC2_PLL_ENABLE (0x46040) gets PLL_POWER_ENABLE and PLL_ENABLE");
	i915_host_check(fake_get(0x46034u) == 0u, "enable: MG_PLL2_ENABLE (0x46034, not Alder Lake-P's) is not touched");

	/* The words went through the bank 2 window and kept the bits the PLL does not own. */
	i915_host_check(fake_get(M3_HIP_INDEX) == M3_HIP_BANK2, "enable: the bank index selects bank 2 for TC2");
	i915_host_check(fake_get(M3_WINDOW + 0x200u) == (0x7e000000u | 0x84269u), "enable: DKL_PLL_DIV0 keeps the AFC bits and takes 0x84269");
	i915_host_check(fake_get(M3_WINDOW + 0x12cu) == 0x101u, "enable: DKL_REFCLKIN_CTL keeps bit 0 and takes 0x100");

	/* The readout gives the words back. */
	memset(&readback, 0, sizeof(readback));
	enabled = info.funcs->get_hw_state(&i915, &pll, &readback);
	words_of(&readback, &words);
	i915_host_check(enabled, "enable: the PLL reads as enabled");
	i915_host_check(words_equal(&words, &m3_words), "enable: the readout gives the written words back");

	/* Turns it off: the enable and the power request clear. */
	info.funcs->disable(&i915, &pll);
	i915_host_check((fake_get(0x46040u) & (PLL_POWER_ENABLE | PLL_ENABLE)) == 0u,
			"disable: PORTTC2_PLL_ENABLE loses PLL_ENABLE and PLL_POWER_ENABLE");
	i915_host_check(errors == 0, "enable / disable: no error of the text");
}

/* The Thunderbolt PLL keeps the reference's description, and a port outside TC1..TC6 is refused. */
static void
test_tbt_describe(void)
{
	struct dpll_info info;

	/* The Thunderbolt PLL. */
	drv_i915_tbt_pll_describe(&info);
	i915_host_check(info.id == DPLL_ID_ICL_TBTPLL, "tbt: the Thunderbolt PLL is id 2");
	i915_host_check(strcmp(info.name, "TBT PLL") == 0, "tbt: the Thunderbolt PLL is named TBT PLL");

	/* TC PLL 1 and TC PLL 4. */
	drv_i915_dkl_pll_describe(&info, TC_PORT_1);
	i915_host_check(info.id == DPLL_ID_ICL_MGPLL1, "describe: TC1's PLL is id 3");
	drv_i915_dkl_pll_describe(&info, TC_PORT_4);
	i915_host_check(info.id == DPLL_ID_ICL_MGPLL4, "describe: TC4's PLL is id 6");

	/* A port that is not Type-C is an error, described as TC PLL 1. */
	errors = 0;
	drv_i915_dkl_pll_describe(&info, TC_PORT_NONE);
	i915_host_check(errors == 1 && info.id == DPLL_ID_ICL_MGPLL1, "describe: a port that is not Type-C is an error");
}

/* A Type-C port of Alder Lake-P takes the DKL tables: DP by rate, HDMI Tiger Lake's. */
static void
test_buf_trans(void)
{
	struct drm_i915_private i915;
	struct i915_lcd_emit emit;
	struct intel_encoder encoder;
	struct intel_crtc crtc;
	struct intel_crtc_state crtc_state;
	const struct intel_ddi_buf_trans *trans;
	int n_entries;

	/* An encoder of DDI port E (TC2) on the fake device. */
	fake_reset();
	bind_device(&i915, &emit);
	memset(&encoder, 0, sizeof(encoder));
	memset(&crtc, 0, sizeof(crtc));
	memset(&crtc_state, 0, sizeof(crtc_state));
	encoder.base.dev = &i915.drm;
	encoder.port = PORT_TC2;
	crtc.base.dev = &i915.drm;
	crtc_state.uapi.crtc = &crtc.base;
	drv_i915_lcd_ms_bind_buf_trans(&encoder);

	/* RBR and HBR: the HBR table (entry 0 is 0x7, 0x0, 0x01). */
	crtc_state.output_types = BIT(INTEL_OUTPUT_DP);
	crtc_state.port_clock = 270000;
	trans = encoder.get_buf_trans(&encoder, &crtc_state, &n_entries);
	i915_host_check(n_entries == 10, "buf trans: ADL-P DKL HBR has 10 entries");
	i915_host_check(trans->entries[0].dkl.vswing == 0x7 && trans->entries[0].dkl.de_emphasis == 0x01,
			"buf trans: ADL-P DKL HBR entry 0 is { 0x7, 0x0, 0x01 }");

	/* HBR2: the HBR2 / HBR3 table (entry 1 is 0x5, 0x0, 0x04). */
	crtc_state.port_clock = 540000;
	trans = encoder.get_buf_trans(&encoder, &crtc_state, &n_entries);
	i915_host_check(n_entries == 10, "buf trans: ADL-P DKL HBR2/HBR3 has 10 entries");
	i915_host_check(trans->entries[1].dkl.vswing == 0x5 && trans->entries[1].dkl.de_emphasis == 0x04,
			"buf trans: ADL-P DKL HBR2/HBR3 entry 1 is { 0x5, 0x0, 0x04 }");

	/* HDMI: Tiger Lake's DKL table, whose default is its last entry. */
	crtc_state.output_types = BIT(INTEL_OUTPUT_HDMI);
	trans = encoder.get_buf_trans(&encoder, &crtc_state, &n_entries);
	i915_host_check(n_entries == 10 && trans->hdmi_default_entry == 9, "buf trans: HDMI on a Type-C port takes TGL's DKL table (default 9)");
	i915_host_check(trans->entries[9].dkl.de_emphasis == 0xA, "buf trans: TGL DKL HDMI entry 9 is { 0x0, 0x0, 0xA }");
}

/*
 * The device's PLL pool is Alder Lake-P's adlp_plls[] (ws051-p004b): DPLL 0
 * and 1, the Thunderbolt PLL and TC PLL 1 to 4, each at the place of its
 * id; a pipe's release clears it from every PLL, and a reset forgets the
 * pool.
 */
static void
test_pool(void)
{
	static const char *const names[I915_LCD_DPLL_POOL_SIZE] = {
		"DPLL 0", "DPLL 1", "TBT PLL", "TC PLL 1", "TC PLL 2", "TC PLL 3", "TC PLL 4"
	};
	struct drm_i915_private i915;
	int index;
	int ids_ok;
	int names_ok;
	int funcs_ok;
	int cleared;

	/* Binds a device view to the pool. */
	memset(&i915, 0, sizeof(i915));
	drv_i915_lcd_dpll_pool_bind(&pool_world, &i915);
	i915_host_check(i915.display.dpll.num_shared_dpll == 7, "pool: seven PLLs");
	i915_host_check(i915.display.dpll.shared_dplls == pool_world.i915_lcd_dpll_pool, "pool: the device view is the world's pool");

	/* Each PLL is at the place of its id, with its name and hooks. */
	ids_ok = 1;
	names_ok = 1;
	funcs_ok = 1;
	for (index = 0; index < I915_LCD_DPLL_POOL_SIZE; index++) {
		if ((int)pool_world.i915_lcd_dpll_pool[index].info->id != index)
			ids_ok = 0;
		if ((int)pool_world.i915_lcd_dpll_pool[index].index != index)
			ids_ok = 0;
		if (strcmp(pool_world.i915_lcd_dpll_pool[index].info->name, names[index]) != 0)
			names_ok = 0;
		if (pool_world.i915_lcd_dpll_pool[index].info->funcs == NULL)
			funcs_ok = 0;
	}
	i915_host_check(ids_ok, "pool: each PLL's id is its place (DPLL 0 .. TC PLL 4 = 0 .. 6)");
	i915_host_check(names_ok, "pool: the names of adlp_plls[]");
	i915_host_check(funcs_ok, "pool: every PLL has its hooks");
	i915_host_check(pool_world.i915_lcd_dpll_pool[DPLL_ID_ICL_TBTPLL].info->id == DPLL_ID_ICL_TBTPLL, "pool: the Thunderbolt PLL is id 2");
	i915_host_check(pool_world.i915_lcd_dpll_pool[I915_LCD_DPLL_POOL_FIRST_TC + 1].info->id == DPLL_ID_ICL_MGPLL2, "pool: TC2's PLL is id 4");

	/* A pipe's release clears it from every PLL. */
	for (index = 0; index < I915_LCD_DPLL_POOL_SIZE; index++) {
		pool_world.i915_lcd_dpll_pool_state[index].pipe_mask = 0x3;
		pool_world.i915_lcd_dpll_pool[index].state.pipe_mask = 0x3;
	}
	drv_i915_lcd_ms_release_pipe(&pool_world, PIPE_B);
	cleared = 1;
	for (index = 0; index < I915_LCD_DPLL_POOL_SIZE; index++) {
		if (pool_world.i915_lcd_dpll_pool_state[index].pipe_mask != 0x1)
			cleared = 0;
		if (pool_world.i915_lcd_dpll_pool[index].state.pipe_mask != 0x1)
			cleared = 0;
	}
	i915_host_check(cleared, "pool: releasing pipe B clears it from all seven PLLs and keeps pipe A");

	/* A reset forgets the pool, and the next bind builds it again. */
	drv_i915_lcd_dplls_reset(&pool_world);
	i915_host_check(pool_world.i915_lcd_dpll_pool_inited == 0, "pool: a reset forgets the pool");
	i915_host_check(pool_world.i915_lcd_dpll_pool[6].info == NULL, "pool: a reset clears the last Type-C PLL too");
	drv_i915_lcd_dpll_pool_bind(&pool_world, &i915);
	i915_host_check(pool_world.i915_lcd_dpll_pool[6].info->id == DPLL_ID_ICL_MGPLL4, "pool: the next bind builds TC PLL 4 again");
}
