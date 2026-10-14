/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the Gen9 mode set's register sequences
 * (display/gen9-ddc.c, gen9-link.c, gen9-modeset.c, WS237 stage 3) on a
 * model of the hardware: the AUX channel with a DP sink (DPCD, deferrals,
 * I2C-over-AUX EDID, link training), the GMBUS with an HDMI sink, the
 * power wells, the DPLLs, the transcoders, the DDI buffers and the frame
 * counters.
 *
 *   sh plan/ws237/tests/run-gen9-modeset-host-test.sh
 *
 * The model answers as the PRM describes the registers; it checks what
 * the code writes and in which order, not the hardware's timing.
 */

#include "host-test.h"

#include "../../display/gen9-ddc.h"
#include "../../display/gen9-link.h"
#include "../../display/gen9-mode.h"
#include "../../display/gen9-modeset.h"
#include "../../device-info.h"
#include "../../intel/display-regs-gen9.h"
#include "../../intel/display-regs-gen9-modeset.h"

#include <uapi/errno.h>

#include <stdio.h>
#include <string.h>

/* How many registers and writes the model keeps. */
#define I915_HOST_REGISTERS	1024U
#define I915_HOST_LOG		8192U

/* The DP_TP_STATUS bit of the idle patterns' end. */
#define I915_HOST_IDLE_DONE	(1U << 25)

/* One sink on a port: a DP sink on the AUX channel, or an HDMI sink on the GMBUS. */
struct i915_host_sink {
	int dp;
	int hdmi;
	uint8_t dpcd[0x700];
	uint8_t edid[256];

	/* The deferrals left, the I2C offset, and the swing clock recovery needs. */
	unsigned defer_left;
	unsigned i2c_offset;
	unsigned cr_swing;
};

/* The model of the display hardware. */
struct i915_host_hw {
	uint32_t keys[I915_HOST_REGISTERS];
	uint32_t values[I915_HOST_REGISTERS];
	unsigned used;

	/* Every write, in order. */
	uint32_t log_offset[I915_HOST_LOG];
	uint32_t log_value[I915_HOST_LOG];
	unsigned log_count;

	/* The sinks of ports A to D, and the AUX transfers started. */
	struct i915_host_sink sinks[I915_SKL_PORTS];
	unsigned aux_transfers;

	/* The GMBUS: the pin, the device's port, the bytes left and the position, a NAK. */
	unsigned gmbus_pin;
	unsigned gmbus_left;
	unsigned gmbus_position;
	int gmbus_port;
	int gmbus_nak;

	/* Whether the frame counters run, and the time waited. */
	int frames_run;
	uint64_t waited_us;
};

static struct i915_host_hw i915_host_model;

static uint32_t i915_host_get(const struct i915_host_hw *hw, uint32_t offset);
static void i915_host_set(struct i915_host_hw *hw, uint32_t offset, uint32_t value);
static uint32_t i915_host_read(void *context, uint32_t offset);
static void i915_host_write(void *context, uint32_t offset, uint32_t value);
static void i915_host_udelay(void *context, unsigned microseconds);
static void i915_host_msleep(void *context, unsigned milliseconds);
static void i915_host_aux(struct i915_host_hw *hw, unsigned port, uint32_t control);
static unsigned i915_host_aux_native(struct i915_host_sink *sink, unsigned request, uint32_t address, const uint8_t *message, unsigned size, uint8_t *reply);
static unsigned i915_host_aux_i2c(struct i915_host_sink *sink, unsigned request, const uint8_t *message, unsigned size, uint8_t *reply);
static void i915_host_link_status(const struct i915_host_sink *sink, uint8_t *status);
static void i915_host_gmbus1(struct i915_host_hw *hw, uint32_t value);
static int i915_host_gmbus_port(unsigned pin);
static void i915_host_reset(void);
static void i915_host_add_dp_sink(unsigned port, unsigned cr_swing);
static void i915_host_add_hdmi_sink(unsigned port);
static int i915_host_written_at(uint32_t offset, uint32_t mask, uint32_t value);
static void i915_host_dp_port(struct i915_skl_dp_port *dp, unsigned port);
static void i915_host_check_ddc(void);
static void i915_host_check_power(void);
static void i915_host_check_dp_output(void);
static void i915_host_check_dp_failure(void);
static void i915_host_check_hdmi_output(void);
static void i915_host_check_watermarks(void);
static void i915_host_check_hotplug(void);

static const struct i915_skl_io i915_host_io = {
	i915_host_read,
	i915_host_write,
	i915_host_udelay,
	i915_host_msleep,
	&i915_host_model
};

/*
 * Checks the Gen9 mode set's register sequences on the model.
 */
int
main(void)
{
	int status;

	/* Each part. */
	i915_host_check_ddc();
	i915_host_check_power();
	i915_host_check_dp_output();
	i915_host_check_dp_failure();
	i915_host_check_hdmi_output();
	i915_host_check_watermarks();
	i915_host_check_hotplug();

	/* The tally. */
	status = i915_host_report("gen9_modeset_host_test");
	if (status != 0)
		return status;

	/* Succeeded: the sequences are the ones gen9-modeset.c describes. */
	return 0;
}

/* Checks the AUX packing, DPCD reads and writes with deferrals, a missing sink, and both EDID paths. */
static void
i915_host_check_ddc(void)
{
	uint8_t bytes[16];
	uint8_t edid[256];
	uint32_t word;
	int error;

	i915_host_reset();
	i915_host_add_dp_sink(I915_SKL_PORT_B, 1U);
	i915_host_add_hdmi_sink(I915_SKL_PORT_C);

	/* The packing, the first byte highest. */
	bytes[0] = 0x12U;
	bytes[1] = 0x34U;
	bytes[2] = 0x56U;
	word = drv_i915_skl_aux_pack(bytes, 3U);
	i915_host_check(word == 0x12345600U, "aux pack three bytes");
	drv_i915_skl_aux_unpack(0xa1b2c3d4U, bytes, 2U);
	i915_host_check(bytes[0] == 0xa1U && bytes[1] == 0xb2U, "aux unpack two bytes");

	/* The receiver capabilities, after two deferrals. */
	i915_host_model.sinks[I915_SKL_PORT_B].defer_left = 2U;
	memset(bytes, 0, sizeof(bytes));
	error = drv_i915_skl_dpcd_read(&i915_host_io, I915_SKL_PORT_B, 0U, bytes, I915_DPCD_RECEIVER_CAP_SIZE);
	i915_host_check(error == 0 && bytes[0] == 0x12U && bytes[1] == 0x14U && bytes[2] == 0xc4U, "dpcd read after deferrals");
	i915_host_check(i915_host_model.aux_transfers == 3U, "dpcd read took three transfers");

	/* A write. */
	bytes[0] = 0x02U;
	error = drv_i915_skl_dpcd_write(&i915_host_io, I915_SKL_PORT_B, I915_DPCD_SET_POWER, bytes, 1U);
	i915_host_check(error == 0 && i915_host_model.sinks[I915_SKL_PORT_B].dpcd[I915_DPCD_SET_POWER] == 0x02U, "dpcd write");

	/* No sink on port D: five timeouts. */
	i915_host_model.aux_transfers = 0U;
	error = drv_i915_skl_dpcd_read(&i915_host_io, I915_SKL_PORT_D, 0U, bytes, 1U);
	i915_host_check(error == ETIMEDOUT && i915_host_model.aux_transfers == 5U, "dpcd read times out five times on an empty port");

	/* The EDID over AUX, both blocks. */
	memset(edid, 0, sizeof(edid));
	error = drv_i915_skl_aux_edid(&i915_host_io, I915_SKL_PORT_B, edid, 2U);
	i915_host_check(error == 0 && memcmp(edid, i915_host_model.sinks[I915_SKL_PORT_B].edid, sizeof(edid)) == 0, "aux edid two blocks");

	/* The EDID over the GMBUS of port C, both blocks, the clock gating back. */
	memset(edid, 0, sizeof(edid));
	error = drv_i915_skl_gmbus_edid(&i915_host_io, drv_i915_skl_gmbus_pin(I915_SKL_PORT_C), edid, 2U);
	i915_host_check(error == 0 && memcmp(edid, i915_host_model.sinks[I915_SKL_PORT_C].edid, sizeof(edid)) == 0, "gmbus edid two blocks");
	i915_host_check((i915_host_get(&i915_host_model, I915_SKL_SOUTH_DSPCLK_GATE_D) & I915_SKL_PCH_GMBUSUNIT_CLOCK_GATE_DISABLE) == 0U, "gmbus clock gating restored");
	i915_host_check(i915_host_written_at(I915_SKL_GMBUS1, I915_SKL_GMBUS_CYCLE_STOP, I915_SKL_GMBUS_CYCLE_STOP) >= 0, "gmbus ends with a stop");

	/* Nothing on port D's pin: a NAK, cleared. */
	i915_host_model.log_count = 0U;
	error = drv_i915_skl_gmbus_edid(&i915_host_io, drv_i915_skl_gmbus_pin(I915_SKL_PORT_D), edid, 1U);
	i915_host_check(error == ENXIO, "gmbus NAK on an empty pin");
	i915_host_check(i915_host_written_at(I915_SKL_GMBUS1, I915_SKL_GMBUS_SW_CLR_INT, I915_SKL_GMBUS_SW_CLR_INT) >= 0, "gmbus NAK cleared");
	i915_host_check(drv_i915_skl_gmbus_pin(I915_SKL_PORT_B) == 5U && drv_i915_skl_gmbus_pin(I915_SKL_PORT_A) == 0U, "gmbus pins");
}

/* Checks power well 2, a DDI IO well, and a DPLL's start and stop. */
static void
i915_host_check_power(void)
{
	struct i915_skl_dpll_words words;
	uint32_t ctrl1;
	unsigned writes;
	int error;

	i915_host_reset();

	/* Power well 2, then again with nothing written. */
	error = drv_i915_skl_pw2_on(&i915_host_io);
	i915_host_check(error == 0 && (i915_host_get(&i915_host_model, I915_SKL_PWR_WELL_CTL2) & I915_SKL_PW_2_STATE) != 0U, "pw2 on");
	writes = i915_host_model.log_count;
	error = drv_i915_skl_pw2_on(&i915_host_io);
	i915_host_check(error == 0 && i915_host_model.log_count == writes, "pw2 already on writes nothing");

	/* A DDI IO well. */
	error = drv_i915_skl_ddi_io_power(&i915_host_io, I915_SKL_PORT_C, 1);
	i915_host_check(error == 0 && (i915_host_get(&i915_host_model, I915_SKL_PWR_WELL_CTL2) & I915_SKL_PW_DDI_STATE(I915_SKL_PORT_C)) != 0U, "ddi io well on");
	(void)drv_i915_skl_ddi_io_power(&i915_host_io, I915_SKL_PORT_C, 0);
	i915_host_check((i915_host_get(&i915_host_model, I915_SKL_PWR_WELL_CTL2) & I915_SKL_PW_DDI_REQUEST(I915_SKL_PORT_C)) == 0U, "ddi io well request dropped");

	/* DPLL2 at 148.5 MHz HDMI, DPLL0's field kept. */
	i915_host_set(&i915_host_model, I915_SKL_DPLL_CTRL1, 0x0bU);
	(void)drv_i915_skl_dpll_hdmi(148500U, I915_SKL_REF_KHZ, &words);
	error = drv_i915_skl_dpll_on(&i915_host_io, 2U, &words);
	ctrl1 = i915_host_get(&i915_host_model, I915_SKL_DPLL_CTRL1);
	i915_host_check(error == 0 && ctrl1 == (0x0bU | (0x21U << 12)), "dpll2 CTRL1 field");
	i915_host_check(i915_host_get(&i915_host_model, 0x6c048U) == 0x80400173U && i915_host_get(&i915_host_model, 0x6c04cU) == 0x3a5U, "dpll2 CFGCR words");
	i915_host_check((i915_host_get(&i915_host_model, I915_SKL_DPLL2_CTL) & I915_SKL_DPLL_ENABLE) != 0U, "dpll2 enabled");
	drv_i915_skl_dpll_off(&i915_host_io, 2U);
	i915_host_check((i915_host_get(&i915_host_model, I915_SKL_DPLL2_CTL) & I915_SKL_DPLL_ENABLE) == 0U, "dpll2 stopped");

	/* DPLL0 is refused. */
	error = drv_i915_skl_dpll_on(&i915_host_io, 0U, &words);
	i915_host_check(error == EINVAL, "dpll0 refused");
}

/* Checks a 1080p DP output on port B, pipe B, DPLL1, and its way back. */
static void
i915_host_check_dp_output(void)
{
	struct i915_skl_output_config config;
	struct i915_skl_dp_port dp;
	const struct i915_host_sink *sink;
	uint32_t value;
	int ctrl1_at;
	int ctrl2_at;
	int buffer_at;
	int function_at;
	int enable_at;
	int control_at;
	int surface_at;
	int error;

	i915_host_reset();
	i915_host_add_dp_sink(I915_SKL_PORT_B, 1U);
	i915_host_model.frames_run = 1;
	sink = &i915_host_model.sinks[I915_SKL_PORT_B];

	/* The configuration of 1080p on RBR x4. */
	memset(&config, 0, sizeof(config));
	config.type = I915_SKL_OUTPUT_DP;
	config.port = I915_SKL_PORT_B;
	config.pipe = 1U;
	config.dpll = 1U;
	(void)drv_i915_skl_pick_mode(sink->edid, 128U, 540000U, &config.mode);
	(void)drv_i915_skl_dp_link(I915_SKL_DP_HBR2_KHZ, 4U, (uint32_t)config.mode.clock_khz, &config.link);
	(void)drv_i915_skl_dpll_dp(config.link.port_khz, &config.dpll_words);
	config.sku = I915_SKL_SKU_ULT;
	config.platform = I915_PLATFORM_KABYLAKE;
	config.surf = 0x01000000U;
	config.stride_units = 1920U * 4U / 64U;
	i915_host_dp_port(&dp, I915_SKL_PORT_B);

	/* The output on. */
	error = drv_i915_skl_output_on(&i915_host_io, &config, &dp);
	i915_host_check(error == 0, "dp output on");

	/* The sink: RBR, four lanes with enhanced framing, training off, D0, 8b/10b. */
	i915_host_check(sink->dpcd[I915_DPCD_LINK_BW_SET] == I915_DPCD_LINK_BW_1_62 && sink->dpcd[I915_DPCD_LANE_COUNT_SET] == 0x84U, "dp sink link set");
	i915_host_check(sink->dpcd[I915_DPCD_TRAINING_PATTERN_SET] == 0U && sink->dpcd[I915_DPCD_SET_POWER] == I915_DPCD_SET_POWER_D0, "dp sink trained and on");
	i915_host_check(sink->dpcd[I915_DPCD_MAIN_LINK_CHANNEL_CODING_SET] == I915_DPCD_SET_ANSI_8B10B && sink->dpcd[I915_DPCD_TRAINING_LANE0_SET + 3U] == 0x01U, "dp sink coding and lane 3 swing 1");
	i915_host_check(dp.cr_tries == 1U && dp.eq_tries == 0U, "dp trained after one adjustment");

	/* The port: entry 4 (swing 1), on, four lanes; the transport sending pixels with enhanced framing. */
	value = i915_host_get(&i915_host_model, I915_SKL_DDI_BUF_CTL_A + 0x100U);
	i915_host_check(value == (I915_SKL_DDI_BUF_ENABLE | (4U << 24) | (3U << 1)), "dp buffer control");
	value = i915_host_get(&i915_host_model, I915_SKL_DP_TP_CTL_A + 0x100U);
	i915_host_check(value == (I915_SKL_DP_TP_ENABLE | I915_SKL_DP_TP_TRAIN_NORMAL | I915_SKL_DP_TP_ENHANCED_FRAME), "dp transport normal");

	/* The clocks: DPLL1 at RBR, port B on DPLL1 with the override, transcoder B from port B. */
	i915_host_check(((i915_host_get(&i915_host_model, I915_SKL_DPLL_CTRL1) >> 6) & 0x3fU) == 0x5U, "dp dpll1 at RBR");
	value = i915_host_get(&i915_host_model, I915_SKL_DPLL_CTRL2);
	i915_host_check((value & I915_SKL_DPLL_CTRL2_CLK_OFF(I915_SKL_PORT_B)) == 0U && ((value >> 3) & 0x7U) == ((1U << 1) | 1U), "dp port clock select");
	i915_host_check(i915_host_get(&i915_host_model, I915_SKL_TRANS_CLK_SEL_A + 4U) == (2U << 29), "dp transcoder clock");

	/* The transcoder: M/N, timings, the DDI function, running. */
	i915_host_check(i915_host_get(&i915_host_model, I915_SKL_DATA_M1_A + 0x1000U) == ((63U << 25) | 0x580000U), "dp data M with TU 64");
	i915_host_check(i915_host_get(&i915_host_model, I915_SKL_LINK_N1_A + 0x1000U) == 0x80000U, "dp link N");
	i915_host_check(i915_host_get(&i915_host_model, I915_SKL_TRANS_HTOTAL_A + 0x1000U) == ((2199U << 16) | 1919U), "dp htotal");
	i915_host_check(i915_host_get(&i915_host_model, I915_SKL_TRANS_VSYNC_A + 0x1000U) == ((1088U << 16) | 1083U), "dp vsync");
	i915_host_check(i915_host_get(&i915_host_model, I915_SKL_PIPESRC_A + 0x1000U) == ((1919U << 16) | 1079U), "dp pipe source");
	value = I915_SKL_TRANS_DDI_FUNC_ENABLE | (1U << 28) | (2U << 24) | I915_SKL_TRANS_DDI_PVSYNC | I915_SKL_TRANS_DDI_PHSYNC | (3U << 1);
	i915_host_check(i915_host_get(&i915_host_model, I915_SKL_TRANS_DDI_FUNC_CTL_A + 0x1000U) == value, "dp DDI function");
	i915_host_check(i915_host_get(&i915_host_model, I915_SKL_TRANSCONF_A + 0x1000U) == (I915_SKL_TRANSCONF_ENABLE | I915_SKL_TRANSCONF_STATE), "dp transcoder running");
	i915_host_check(i915_host_get(&i915_host_model, I915_SKL_TRANS_MSA_MISC_A + 0x1000U) == 0x21U, "dp MSA");

	/* The plane: on, XRGB8888, plane gamma off, the stride and size, the surface. */
	i915_host_check(i915_host_get(&i915_host_model, I915_SKL_PLANE_CTL_1_A + 0x1000U) == (I915_SKL_PLANE_CTL_ENABLE | (4U << 24) | (1U << 13)), "dp plane control");
	i915_host_check(i915_host_get(&i915_host_model, I915_SKL_PLANE_STRIDE_1_A + 0x1000U) == 120U && i915_host_get(&i915_host_model, I915_SKL_PLANE_SIZE_1_A + 0x1000U) == ((1079U << 16) | 1919U), "dp plane stride and size");
	i915_host_check(i915_host_get(&i915_host_model, I915_SKL_PLANE_SURF_1_A + 0x1000U) == 0x01000000U, "dp plane surface");

	/* The order: DPLL, port clock, buffer, DDI function, transcoder, plane control, surface. */
	ctrl1_at = i915_host_written_at(I915_SKL_DPLL_CTRL1, 0U, 0U);
	ctrl2_at = i915_host_written_at(I915_SKL_DPLL_CTRL2, 0U, 0U);
	buffer_at = i915_host_written_at(I915_SKL_DDI_BUF_CTL_A + 0x100U, I915_SKL_DDI_BUF_ENABLE, I915_SKL_DDI_BUF_ENABLE);
	function_at = i915_host_written_at(I915_SKL_TRANS_DDI_FUNC_CTL_A + 0x1000U, 0U, 0U);
	enable_at = i915_host_written_at(I915_SKL_TRANSCONF_A + 0x1000U, I915_SKL_TRANSCONF_ENABLE, I915_SKL_TRANSCONF_ENABLE);
	control_at = i915_host_written_at(I915_SKL_PLANE_CTL_1_A + 0x1000U, 0U, 0U);
	surface_at = i915_host_written_at(I915_SKL_PLANE_SURF_1_A + 0x1000U, 0U, 0U);
	i915_host_check(ctrl1_at >= 0 && ctrl1_at < ctrl2_at && ctrl2_at < buffer_at && buffer_at < function_at, "dp order up to the DDI function");
	i915_host_check(function_at < enable_at && enable_at < control_at && control_at < surface_at, "dp order up to the surface");

	/* The way back. */
	drv_i915_skl_output_off(&i915_host_io, &config, &dp);
	i915_host_check(i915_host_get(&i915_host_model, I915_SKL_PLANE_CTL_1_A + 0x1000U) == 0U && i915_host_get(&i915_host_model, I915_SKL_TRANSCONF_A + 0x1000U) == 0U, "dp off plane and transcoder");
	i915_host_check((i915_host_get(&i915_host_model, I915_SKL_TRANS_DDI_FUNC_CTL_A + 0x1000U) & (I915_SKL_TRANS_DDI_FUNC_ENABLE | (7U << 28))) == 0U, "dp off DDI function");
	i915_host_check(sink->dpcd[I915_DPCD_SET_POWER] == I915_DPCD_SET_POWER_D3 && i915_host_get(&i915_host_model, I915_SKL_TRANS_CLK_SEL_A + 4U) == 0U, "dp off sink D3 and clock");
	i915_host_check((i915_host_get(&i915_host_model, I915_SKL_DDI_BUF_CTL_A + 0x100U) & I915_SKL_DDI_BUF_ENABLE) == 0U, "dp off buffer");
	i915_host_check((i915_host_get(&i915_host_model, I915_SKL_DPLL_CTRL2) & I915_SKL_DPLL_CTRL2_CLK_OFF(I915_SKL_PORT_B)) != 0U, "dp off port clock");
	i915_host_check((i915_host_get(&i915_host_model, I915_SKL_DPLL1_CTL) & I915_SKL_DPLL_ENABLE) == 0U, "dp off dpll1");
	i915_host_check((i915_host_get(&i915_host_model, I915_SKL_PWR_WELL_CTL2) & I915_SKL_PW_DDI_REQUEST(I915_SKL_PORT_B)) == 0U, "dp off io well");
}

/* Checks that a sink whose clock never recovers leaves the port, the clock and the DPLL off. */
static void
i915_host_check_dp_failure(void)
{
	struct i915_skl_output_config config;
	struct i915_skl_dp_port dp;
	int error;

	i915_host_reset();
	i915_host_add_dp_sink(I915_SKL_PORT_D, 4U);

	/* 1024x768 on RBR x1, which cannot train. */
	memset(&config, 0, sizeof(config));
	config.type = I915_SKL_OUTPUT_DP;
	config.port = I915_SKL_PORT_D;
	config.pipe = 2U;
	config.dpll = 3U;
	(void)drv_i915_skl_pick_mode(NULL, 0U, 540000U, &config.mode);
	(void)drv_i915_skl_dp_link(I915_SKL_DP_RBR_KHZ, 4U, (uint32_t)config.mode.clock_khz, &config.link);
	(void)drv_i915_skl_dpll_dp(config.link.port_khz, &config.dpll_words);
	config.sku = I915_SKL_SKU_H;
	config.platform = I915_PLATFORM_SKYLAKE;
	i915_host_dp_port(&dp, I915_SKL_PORT_D);

	/* The training fails and everything is stopped again. */
	error = drv_i915_skl_output_on(&i915_host_io, &config, &dp);
	i915_host_check(error == EIO, "dp failed training reports EIO");
	i915_host_check(dp.cr_tries == 5U, "dp failed training stops after five same requests");
	i915_host_check((i915_host_get(&i915_host_model, I915_SKL_DDI_BUF_CTL_A + 0x300U) & I915_SKL_DDI_BUF_ENABLE) == 0U, "dp failed buffer off");
	i915_host_check((i915_host_get(&i915_host_model, I915_SKL_DPLL3_CTL) & I915_SKL_DPLL_ENABLE) == 0U, "dp failed dpll3 off");
	i915_host_check((i915_host_get(&i915_host_model, I915_SKL_DPLL_CTRL2) & I915_SKL_DPLL_CTRL2_CLK_OFF(I915_SKL_PORT_D)) != 0U, "dp failed port clock off");
	i915_host_check(i915_host_get(&i915_host_model, I915_SKL_TRANSCONF_A + 0x2000U) == 0U, "dp failed transcoder untouched");
}

/* Checks a 1080p HDMI output on port C, pipe C, DPLL2. */
static void
i915_host_check_hdmi_output(void)
{
	struct i915_skl_output_config config;
	uint32_t trans;
	uint32_t value;
	int error;

	i915_host_reset();
	i915_host_add_hdmi_sink(I915_SKL_PORT_C);

	/* The configuration. */
	memset(&config, 0, sizeof(config));
	config.type = I915_SKL_OUTPUT_HDMI;
	config.port = I915_SKL_PORT_C;
	config.pipe = 2U;
	config.dpll = 2U;
	(void)drv_i915_skl_pick_mode(i915_host_model.sinks[I915_SKL_PORT_C].edid, 128U, 300000U, &config.mode);
	(void)drv_i915_skl_dpll_hdmi((uint32_t)config.mode.clock_khz, I915_SKL_REF_KHZ, &config.dpll_words);
	config.sku = I915_SKL_SKU_ULT;
	config.platform = I915_PLATFORM_KABYLAKE;
	config.surf = 0x02000000U;
	config.stride_units = 120U;
	config.rgbx = 1;

	/* The output on. */
	error = drv_i915_skl_output_on(&i915_host_io, &config, NULL);
	i915_host_check(error == 0, "hdmi output on");

	/* The buffer: entry 9 holds the table's entry 8, its boost, on. */
	trans = I915_SKL_DDI_BUF_TRANS_A + 2U * I915_SKL_DDI_BUF_TRANS_STRIDE + 9U * 8U;
	i915_host_check(i915_host_get(&i915_host_model, trans) == 0x80003015U && i915_host_get(&i915_host_model, trans + 4U) == 0xcdU, "hdmi translation entry 9");
	value = i915_host_get(&i915_host_model, I915_SKL_DISPIO_CR_TX_BMU_CR0);
	i915_host_check((value & I915_SKL_BALANCE_LEG_MASK(I915_SKL_PORT_C)) == (1U << I915_SKL_BALANCE_LEG_SHIFT(I915_SKL_PORT_C)), "hdmi boost");
	i915_host_check((i915_host_get(&i915_host_model, I915_SKL_DDI_BUF_CTL_A + 0x200U) & I915_SKL_DDI_BUF_ENABLE) != 0U, "hdmi buffer on");

	/* Display WA #1143: the override toggled on transcoder C's chicken register and off again. */
	i915_host_check(i915_host_written_at(I915_SKL_CHICKEN_TRANS_A + 4U, 3U << 18, 3U << 18) >= 0, "hdmi WA 1143 override on");
	i915_host_check((i915_host_get(&i915_host_model, I915_SKL_CHICKEN_TRANS_A + 4U) & (3U << 18)) == 0U, "hdmi WA 1143 override off");

	/* The transcoder: HDMI mode on port C, no M/N, running; the plane in RGBX order. */
	value = I915_SKL_TRANS_DDI_FUNC_ENABLE | (2U << 28) | I915_SKL_TRANS_DDI_PVSYNC | I915_SKL_TRANS_DDI_PHSYNC;
	i915_host_check(i915_host_get(&i915_host_model, I915_SKL_TRANS_DDI_FUNC_CTL_A + 0x2000U) == value, "hdmi DDI function");
	i915_host_check(i915_host_get(&i915_host_model, I915_SKL_DATA_M1_A + 0x2000U) == 0U, "hdmi no M/N");
	i915_host_check((i915_host_get(&i915_host_model, I915_SKL_TRANSCONF_A + 0x2000U) & I915_SKL_TRANSCONF_STATE) != 0U, "hdmi transcoder running");
	i915_host_check((i915_host_get(&i915_host_model, I915_SKL_PLANE_CTL_1_A + 0x2000U) & I915_SKL_PLANE_CTL_ORDER_RGBX) != 0U, "hdmi plane RGBX");
	i915_host_check(((i915_host_get(&i915_host_model, I915_SKL_DPLL_CTRL1) >> 12) & 0x3fU) == 0x21U, "hdmi dpll2 in HDMI mode");
}

/* Checks the words a pipe's watermarks and DDB are written as. */
static void
i915_host_check_watermarks(void)
{
	static const uint16_t latency[8] = { 2U, 4U, 10U, 18U, 28U, 40U, 60U, 90U };
	struct i915_skl_pipe_wm pipes[3];

	i915_host_reset();

	/* One 1080p pipe B. */
	memset(pipes, 0, sizeof(pipes));
	pipes[1].active = 1;
	pipes[1].hdisplay = 1920U;
	pipes[1].htotal = 2200U;
	pipes[1].pixel_khz = 148500U;
	pipes[1].plane_width = 1920U;
	(void)drv_i915_skl_wm_compute(pipes, latency);
	drv_i915_skl_write_pipe_wm(&i915_host_io, 1U, &pipes[1]);

	/* The plane's levels 0 and 7, the DDB, the cursor's DDB, the line time. */
	i915_host_check(i915_host_get(&i915_host_model, I915_SKL_PLANE_WM_1_A_0 + 0x1000U) == (0x80000000U | 4U), "wm level 0 word");
	i915_host_check(i915_host_get(&i915_host_model, I915_SKL_PLANE_WM_1_A_0 + 0x1000U + 28U) == (0x80000000U | (7U << 14) | 107U), "wm level 7 word");
	i915_host_check(i915_host_get(&i915_host_model, I915_SKL_PLANE_BUF_CFG_1_A + 0x1000U) == (859U << 16), "wm plane DDB");
	i915_host_check(i915_host_get(&i915_host_model, I915_SKL_CUR_BUF_CFG_A + 0x1000U) == ((891U << 16) | 860U), "wm cursor DDB");
	i915_host_check(i915_host_get(&i915_host_model, I915_SKL_CUR_WM_A_0 + 0x1000U) == 0U, "wm cursor level 0 off");
	i915_host_check(i915_host_get(&i915_host_model, I915_SKL_WM_LINETIME_A + 4U) == 119U, "wm line time");
}

/* Checks the hotplug enables, the present and attached ports, and the vertical blank wait. */
static void
i915_host_check_hotplug(void)
{
	int error;

	i915_host_reset();

	/* The enables. */
	drv_i915_skl_hotplug_enable(&i915_host_io);
	i915_host_check(i915_host_get(&i915_host_model, I915_SKL_PCH_PORT_HOTPLUG) == ((1U << 4) | (1U << 12) | (1U << 20)), "hotplug enables");

	/* Ports B and D on the board, a sink on C. */
	i915_host_set(&i915_host_model, I915_SKL_SFUSE_STRAP, I915_SKL_SFUSE_STRAP_DDIB_DETECTED | I915_SKL_SFUSE_STRAP_DDID_DETECTED);
	i915_host_set(&i915_host_model, I915_SKL_SDEISR, I915_SKL_SDE_PORTC_HOTPLUG);
	i915_host_check(drv_i915_skl_ports_present(&i915_host_io) == ((1U << 1) | (1U << 3)), "ports present");
	i915_host_check(drv_i915_skl_ports_connected(&i915_host_io) == (1U << 2), "ports connected");

	/* A running pipe's blank comes; a stopped one's does not. */
	i915_host_model.frames_run = 1;
	error = drv_i915_skl_wait_vblank(&i915_host_io, 1U);
	i915_host_check(error == 0, "vblank of a running pipe");
	i915_host_model.frames_run = 0;
	error = drv_i915_skl_wait_vblank(&i915_host_io, 1U);
	i915_host_check(error == ETIMEDOUT, "vblank of a stopped pipe times out");
}

/* Reports a register's value, 0 when never written. */
static uint32_t
i915_host_get(
	const struct i915_host_hw *hw,
	uint32_t offset)
{
	unsigned index;

	/* The register's slot. */
	for (index = 0U; index < hw->used; index++) {
		if (hw->keys[index] == offset)
			return hw->values[index];
	}

	/* Succeeded: a register never written reads 0. */
	return 0U;
}

/* Stores a register's value. */
static void
i915_host_set(
	struct i915_host_hw *hw,
	uint32_t offset,
	uint32_t value)
{
	unsigned index;

	/* The register's slot, or a new one. */
	for (index = 0U; index < hw->used; index++) {
		if (hw->keys[index] == offset) {
			hw->values[index] = value;
			return;
		}
	}
	if (hw->used == I915_HOST_REGISTERS) {
		fprintf(stderr, "model: register table full at 0x%x\n", offset);
		return;
	}

	/* Succeeded: a new register. */
	hw->keys[hw->used] = offset;
	hw->values[hw->used] = value;
	hw->used++;
}

/* Reads a register of the model: status registers answer as the hardware would. */
static uint32_t
i915_host_read(
	void *context,
	uint32_t offset)
{
	struct i915_host_hw *hw;
	uint32_t value;
	unsigned bytes;

	hw = context;

	/* The transports' idle patterns are always out. */
	if (offset >= I915_SKL_DP_TP_STATUS_A && offset <= I915_SKL_DP_TP_STATUS_A + 0x300U && ((offset - I915_SKL_DP_TP_STATUS_A) % 0x100U) == 0U)
		return I915_HOST_IDLE_DONE;

	/* The frame counters move when the pipes run. */
	if (offset >= I915_SKL_PIPE_FRMCOUNT_A && offset <= I915_SKL_PIPE_FRMCOUNT_A + 0x2000U && ((offset - I915_SKL_PIPE_FRMCOUNT_A) % 0x1000U) == 0U) {
		value = i915_host_get(hw, offset);
		if (hw->frames_run)
			i915_host_set(hw, offset, value + 1U);
		return value;
	}

	/* The GMBUS status: a NAK, a byte group ready, or the wait phase. */
	if (offset == I915_SKL_GMBUS2) {
		if (hw->gmbus_nak)
			return I915_SKL_GMBUS_SATOER;
		if (hw->gmbus_left != 0U)
			return I915_SKL_GMBUS_HW_RDY;
		return I915_SKL_GMBUS_HW_WAIT_PHASE;
	}

	/* The GMBUS data: four bytes of the EDID, the first lowest. */
	if (offset == I915_SKL_GMBUS3) {
		value = 0U;
		for (bytes = 0U; bytes < 4U && hw->gmbus_left != 0U; bytes++) {
			value |= (uint32_t)hw->sinks[hw->gmbus_port].edid[hw->gmbus_position % 256U] << (8U * bytes);
			hw->gmbus_position++;
			hw->gmbus_left--;
		}
		return value;
	}

	/* Succeeded: a stored register. */
	return i915_host_get(hw, offset);
}

/* Writes a register of the model, with the effects the hardware has. */
static void
i915_host_write(
	void *context,
	uint32_t offset,
	uint32_t value)
{
	struct i915_host_hw *hw;
	uint32_t requests;
	unsigned port;
	unsigned id;

	hw = context;

	/* The log. */
	if (hw->log_count < I915_HOST_LOG) {
		hw->log_offset[hw->log_count] = offset;
		hw->log_value[hw->log_count] = value;
		hw->log_count++;
	}

	/* The power wells follow their requests (each state bit is below its request). */
	if (offset == I915_SKL_PWR_WELL_CTL2) {
		requests = value & 0xaaaaaaaaU;
		i915_host_set(hw, offset, requests | (requests >> 1));
		return;
	}

	/* A DPLL locks when enabled. */
	for (id = 1U; id <= 3U; id++) {
		if ((id == 1U && offset == I915_SKL_DPLL1_CTL) || (id == 2U && offset == I915_SKL_DPLL2_CTL) || (id == 3U && offset == I915_SKL_DPLL3_CTL)) {
			i915_host_set(hw, offset, value);
			requests = i915_host_get(hw, I915_SKL_DPLL_STATUS) & ~I915_SKL_DPLL_LOCK(id);
			if ((value & I915_SKL_DPLL_ENABLE) != 0U)
				requests |= I915_SKL_DPLL_LOCK(id);
			i915_host_set(hw, I915_SKL_DPLL_STATUS, requests);
			return;
		}
	}

	/* A transcoder's state follows its enable. */
	if (offset >= I915_SKL_TRANSCONF_A && offset <= I915_SKL_TRANSCONF_A + 0x2000U && ((offset - I915_SKL_TRANSCONF_A) % 0x1000U) == 0U) {
		value &= ~I915_SKL_TRANSCONF_STATE;
		if ((value & I915_SKL_TRANSCONF_ENABLE) != 0U)
			value |= I915_SKL_TRANSCONF_STATE;
		i915_host_set(hw, offset, value);
		return;
	}

	/* A DDI buffer is idle when off. */
	if (offset >= I915_SKL_DDI_BUF_CTL_A && offset <= I915_SKL_DDI_BUF_CTL_A + 0x300U && ((offset - I915_SKL_DDI_BUF_CTL_A) % 0x100U) == 0U) {
		value &= ~I915_SKL_DDI_BUF_IS_IDLE;
		if ((value & I915_SKL_DDI_BUF_ENABLE) == 0U)
			value |= I915_SKL_DDI_BUF_IS_IDLE;
		i915_host_set(hw, offset, value);
		return;
	}

	/* An AUX control word: a start runs the transfer; else the status bits written as 1 clear. */
	if (offset >= I915_SKL_AUX_CTL_A && offset <= I915_SKL_AUX_CTL_A + 0x300U && ((offset - I915_SKL_AUX_CTL_A) % 0x100U) == 0U) {
		port = (offset - I915_SKL_AUX_CTL_A) / 0x100U;
		if ((value & I915_SKL_AUX_SEND_BUSY) != 0U) {
			i915_host_aux(hw, port, value);
			return;
		}
		requests = i915_host_get(hw, offset) & ~(value & (I915_SKL_AUX_DONE | I915_SKL_AUX_TIME_OUT_ERROR | I915_SKL_AUX_RECEIVE_ERROR));
		i915_host_set(hw, offset, requests);
		return;
	}

	/* The GMBUS: the pin, and the cycles. */
	if (offset == I915_SKL_GMBUS0) {
		hw->gmbus_pin = value & 0x7U;
		i915_host_set(hw, offset, value);
		return;
	}
	if (offset == I915_SKL_GMBUS1) {
		i915_host_gmbus1(hw, value);
		return;
	}

	/* Succeeded: a plain register. */
	i915_host_set(hw, offset, value);
}

/* Counts a spin. */
static void
i915_host_udelay(
	void *context,
	unsigned microseconds)
{
	struct i915_host_hw *hw;

	hw = context;

	/* Succeeded: the time is only counted. */
	hw->waited_us += microseconds;
}

/* Counts a sleep. */
static void
i915_host_msleep(
	void *context,
	unsigned milliseconds)
{
	struct i915_host_hw *hw;

	hw = context;

	/* Succeeded: the time is only counted. */
	hw->waited_us += (uint64_t)milliseconds * 1000U;
}

/* Runs one AUX transfer: the message from the data registers, the sink's reply into them, the status. */
static void
i915_host_aux(
	struct i915_host_hw *hw,
	unsigned port,
	uint32_t control)
{
	struct i915_host_sink *sink;
	uint8_t message[20];
	uint8_t reply[20];
	uint32_t data;
	uint32_t address;
	unsigned size;
	unsigned request;
	unsigned replied;
	unsigned index;

	sink = &hw->sinks[port];
	data = I915_SKL_AUX_DATA_A + port * 0x100U;
	hw->aux_transfers++;

	/* No sink: a timeout. */
	if (!sink->dp) {
		i915_host_set(hw, I915_SKL_AUX_CTL_A + port * 0x100U, I915_SKL_AUX_DONE | I915_SKL_AUX_TIME_OUT_ERROR);
		return;
	}

	/* The message. */
	size = (control >> I915_SKL_AUX_MESSAGE_SIZE_SHIFT) & I915_SKL_AUX_MESSAGE_SIZE_MASK;
	for (index = 0U; index < size; index += 4U)
		drv_i915_skl_aux_unpack(i915_host_get(hw, data + index), message + index, size - index);
	request = message[0] >> 4;
	address = ((uint32_t)(message[0] & 0xfU) << 16) | ((uint32_t)message[1] << 8) | message[2];

	/* A deferral, a native transaction, or an I2C one. */
	memset(reply, 0, sizeof(reply));
	if (sink->defer_left != 0U) {
		sink->defer_left--;
		reply[0] = (uint8_t)(I915_DP_AUX_NATIVE_REPLY_DEFER << 4);
		replied = 1U;
	} else if ((request & 0x8U) != 0U) {
		replied = i915_host_aux_native(sink, request, address, message, size, reply);
	} else {
		replied = i915_host_aux_i2c(sink, request, message, size, reply);
	}

	/* The reply and the status. */
	for (index = 0U; index < replied; index += 4U)
		i915_host_set(hw, data + index, drv_i915_skl_aux_pack(reply + index, replied - index));
	i915_host_set(hw, I915_SKL_AUX_CTL_A + port * 0x100U, I915_SKL_AUX_DONE | (replied << I915_SKL_AUX_MESSAGE_SIZE_SHIFT));
}

/* Answers a native AUX read or write; reports the reply's bytes. */
static unsigned
i915_host_aux_native(
	struct i915_host_sink *sink,
	unsigned request,
	uint32_t address,
	const uint8_t *message,
	unsigned size,
	uint8_t *reply)
{
	uint8_t status[6];
	unsigned length;
	unsigned index;

	length = (unsigned)message[3] + 1U;
	reply[0] = 0U;

	/* A write: the bytes after the header. */
	if (request == I915_DP_AUX_NATIVE_WRITE) {
		for (index = 0U; index + 4U < size && address + index < sizeof(sink->dpcd); index++)
			sink->dpcd[address + index] = message[4U + index];
		return 1U;
	}

	/* A read: the link status comes from the training model. */
	i915_host_link_status(sink, status);
	memcpy(sink->dpcd + I915_DPCD_LANE0_1_STATUS, status, sizeof(status));
	for (index = 0U; index < length && address + index < sizeof(sink->dpcd); index++)
		reply[1U + index] = sink->dpcd[address + index];

	/* Succeeded: the ACK and the bytes. */
	return 1U + length;
}

/* Answers an I2C-over-AUX message to the DDC address; reports the reply's bytes. */
static unsigned
i915_host_aux_i2c(
	struct i915_host_sink *sink,
	unsigned request,
	const uint8_t *message,
	unsigned size,
	uint8_t *reply)
{
	unsigned length;
	unsigned index;

	reply[0] = 0U;

	/* An address-only message just ends or starts the transaction. */
	if (size == 3U)
		return 1U;

	/* A write of the offset. */
	length = (unsigned)message[3] + 1U;
	if ((request & ~I915_DP_AUX_I2C_MOT) == I915_DP_AUX_I2C_WRITE) {
		sink->i2c_offset = message[4];
		return 1U;
	}

	/* A read from the offset on. */
	for (index = 0U; index < length; index++) {
		reply[1U + index] = sink->edid[sink->i2c_offset % 256U];
		sink->i2c_offset++;
	}

	/* Succeeded: the ACK and the bytes. */
	return 1U + length;
}

/* Builds the lanes' status of the training model: clock recovery at the sink's swing, then equalisation at once. */
static void
i915_host_link_status(
	const struct i915_host_sink *sink,
	uint8_t *status)
{
	unsigned pattern;
	unsigned lanes;
	unsigned lane;
	uint8_t nibble;

	memset(status, 0, 6U);
	pattern = sink->dpcd[I915_DPCD_TRAINING_PATTERN_SET] & 0x3U;
	lanes = sink->dpcd[I915_DPCD_LANE_COUNT_SET] & 0x1fU;

	/* Each lane: the clock when its swing reaches the sink's, the rest on patterns 2 and 3 or after. */
	for (lane = 0U; lane < lanes && lane < 4U; lane++) {
		nibble = 0U;
		if ((sink->dpcd[I915_DPCD_TRAINING_LANE0_SET + lane] & 0x3U) >= sink->cr_swing)
			nibble = I915_DPCD_LANE_CR_DONE;
		if (nibble != 0U && pattern != 1U)
			nibble |= I915_DPCD_LANE_CHANNEL_EQ_DONE | I915_DPCD_LANE_SYMBOL_LOCKED;
		status[lane / 2U] |= (uint8_t)(nibble << (4U * (lane & 1U)));

		/* The request: the sink's swing (capped at 3), no pre-emphasis. */
		status[4U + lane / 2U] |= (uint8_t)(((sink->cr_swing > 3U) ? 3U : sink->cr_swing) << (4U * (lane & 1U)));
	}

	/* The lanes are aligned once equalised. */
	if (pattern != 1U && lanes != 0U && (status[0] & I915_DPCD_LANE_CHANNEL_EQ_DONE) != 0U)
		status[2] = I915_DPCD_INTERLANE_ALIGN_DONE;
}

/* Runs a GMBUS1 write: a clear, a stop, or a read with an index. */
static void
i915_host_gmbus1(
	struct i915_host_hw *hw,
	uint32_t value)
{
	int port;

	i915_host_set(hw, I915_SKL_GMBUS1, value);

	/* The software clear ends a NAK. */
	if ((value & I915_SKL_GMBUS_SW_CLR_INT) != 0U) {
		hw->gmbus_nak = 0;
		hw->gmbus_left = 0U;
		return;
	}

	/* Only a ready read starts anything. */
	if ((value & I915_SKL_GMBUS_SW_RDY) == 0U || (value & I915_SKL_GMBUS_SLAVE_READ) == 0U)
		return;

	/* A pin without a sink NAKs. */
	port = i915_host_gmbus_port(hw->gmbus_pin);
	if (port < 0 || !hw->sinks[port].hdmi) {
		hw->gmbus_nak = 1;
		return;
	}

	/* Succeeded: the read from the index. */
	hw->gmbus_port = port;
	hw->gmbus_position = (value >> I915_SKL_GMBUS_SLAVE_INDEX_SHIFT) & 0xffU;
	hw->gmbus_left = (value >> I915_SKL_GMBUS_BYTE_COUNT_SHIFT) & 0x1ffU;
}

/* Reports the port of a GMBUS pin, or -1. */
static int
i915_host_gmbus_port(
	unsigned pin)
{
	/* The three pins. */
	if (pin == I915_SKL_GMBUS_PIN_DPB)
		return (int)I915_SKL_PORT_B;
	if (pin == I915_SKL_GMBUS_PIN_DPC)
		return (int)I915_SKL_PORT_C;
	if (pin == I915_SKL_GMBUS_PIN_DPD)
		return (int)I915_SKL_PORT_D;

	/* Succeeded: no port. */
	return -1;
}

/* Empties the model: no registers, no sinks; power gate 2's fuses distributed. */
static void
i915_host_reset(void)
{
	/* Everything off. */
	memset(&i915_host_model, 0, sizeof(i915_host_model));

	/* Succeeded: the fuses follow power well 2 at once. */
	i915_host_set(&i915_host_model, I915_SKL_FUSE_STATUS, I915_SKL_FUSE_PG_DIST(2U));
}

/* Attaches a DP 1.2 sink of HBR2 x4 with TPS3 and enhanced framing, and a 1080p EDID. */
static void
i915_host_add_dp_sink(
	unsigned port,
	unsigned cr_swing)
{
	static const uint8_t dtd_1080p[18] = {
		0x02, 0x3a, 0x80, 0x18, 0x71, 0x38, 0x2d, 0x40, 0x58,
		0x2c, 0x45, 0x00, 0xc4, 0x8e, 0x21, 0x00, 0x00, 0x1e
	};
	struct i915_host_sink *sink;
	unsigned index;

	sink = &i915_host_model.sinks[port];
	sink->dp = 1;
	sink->cr_swing = cr_swing;

	/* The receiver capabilities. */
	sink->dpcd[I915_DPCD_REV] = 0x12U;
	sink->dpcd[I915_DPCD_MAX_LINK_RATE] = I915_DPCD_LINK_BW_5_4;
	sink->dpcd[I915_DPCD_MAX_LANE_COUNT] = (uint8_t)(4U | I915_DPCD_ENHANCED_FRAME_CAP | I915_DPCD_TPS3_SUPPORTED);

	/* Succeeded: an EDID of two blocks, 1080p first, the rest a pattern. */
	for (index = 0U; index < sizeof(sink->edid); index++)
		sink->edid[index] = (uint8_t)(index * 7U + port);
	memset(sink->edid + 54, 0, 72);
	memcpy(sink->edid + 54, dtd_1080p, sizeof(dtd_1080p));
}

/* Attaches an HDMI sink with the same EDID on the port's GMBUS pin. */
static void
i915_host_add_hdmi_sink(
	unsigned port)
{
	/* The EDID, without a DP side. */
	i915_host_add_dp_sink(port, 0U);
	i915_host_model.sinks[port].dp = 0;

	/* Succeeded: the GMBUS answers on the pin. */
	i915_host_model.sinks[port].hdmi = 1;
}

/* Reports the first write to a register whose masked value matches, as its position in the log, or -1. */
static int
i915_host_written_at(
	uint32_t offset,
	uint32_t mask,
	uint32_t value)
{
	unsigned index;

	/* Each write in order. */
	for (index = 0U; index < i915_host_model.log_count; index++) {
		if (i915_host_model.log_offset[index] == offset && (i915_host_model.log_value[index] & mask) == value)
			return (int)index;
	}

	/* Succeeded: never written so. */
	return -1;
}

/* Fills a DP port from its sink's receiver capabilities and the part's DP table. */
static void
i915_host_dp_port(
	struct i915_skl_dp_port *dp,
	unsigned port)
{
	memset(dp, 0, sizeof(*dp));
	dp->port = port;

	/* The capabilities, as the node reads them. */
	(void)drv_i915_skl_dpcd_read(&i915_host_io, port, 0U, dp->dpcd, sizeof(dp->dpcd));

	/* Succeeded: the table. */
	dp->trans = drv_i915_skl_buf_trans(I915_SKL_SKU_ULT, I915_PLATFORM_KABYLAKE, I915_SKL_OUTPUT_DP, port, &dp->trans_count);
}
