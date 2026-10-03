/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A register model of what the eDP first stage touches (dp-fake-hw.c): the
 * PCH panel power sequencer 0, AUX channel A with a DP sink behind it (the
 * DPCD and an EDID EEPROM on I2C-over-AUX), a clock, and the power-domain
 * references, locks and delayed work of struct i915_dp_env.
 *
 * It models the register contract the driver relies on (busy, done and
 * error bits, the message size, big-endian data packing, write-one-to-clear),
 * built from the register definitions and the DP AUX message format; it is
 * not a recording of the real GPU.  Faults are injected per transaction.
 *
 * Test support only: the host tests and the kernel test build link it; the
 * production kernel never does.  It uses no host library, so both can.
 */

#ifndef DRIVERS_GPU_I915_TESTS_DISPLAY_DP_FAKE_HW_H
#define DRIVERS_GPU_I915_TESTS_DISPLAY_DP_FAKE_HW_H

#include "../../display/internal.h"

#include <stddef.h>
#include <stdint.h>

struct i915_dp_world;

/* The fault an AUX transaction meets. */
enum i915_dp_fake_fault {
	I915_DP_FAKE_OK = 0,
	I915_DP_FAKE_NATIVE_DEFER,      /* reply 0x20 */
	I915_DP_FAKE_NATIVE_NACK,       /* reply 0x10 */
	I915_DP_FAKE_I2C_DEFER,         /* reply 0x80 (I2C transactions only) */
	I915_DP_FAKE_I2C_NACK,          /* reply 0x40 (I2C transactions only) */
	I915_DP_FAKE_SHORT_REPLY,       /* a read returns fewer data bytes than asked (param = bytes) */
	I915_DP_FAKE_HW_TIMEOUT,        /* DONE + TIME_OUT_ERROR (no sink answer) */
	I915_DP_FAKE_RECEIVE_ERROR,     /* DONE + RECEIVE_ERROR */
	I915_DP_FAKE_STUCK_BUSY,        /* SEND_BUSY does not clear for param ms (0: 25 ms) */
	I915_DP_FAKE_BAD_SIZE_ZERO,     /* DONE with message size 0 */
	I915_DP_FAKE_BAD_SIZE_BIG,      /* DONE with message size 21 */
	I915_DP_FAKE_INVALID_REPLY,     /* reply code 0x30 (reserved) */
	I915_DP_FAKE_CORRUPT_DATA       /* a read's first data byte is inverted (EDID checksum failures) */
};

/* The length of the per-transaction fault script. */
#define I915_DP_FAKE_SCRIPT_MAX 64U

/*
 * One scripted AUX transaction: the fault it meets and its parameter.
 */
struct i915_dp_fake_step {
	uint8_t fault;
	uint8_t param;
};

/*
 * The register model of one eDP first stage.
 *
 * One instance stands for the panel power sequencer, the AUX channel and
 * the sink of one test run; drv_i915_dp_fake_init() starts it afresh and
 * drv_i915_dp_fake_bind_env() hands it to the driver.  Field names are the
 * ones the tests read.
 */
struct i915_dp_fake_hw {
	/* The model's clock. */
	uint64_t now_us;

	/* PPS 0; vdd_on_since_us = 0 while VDD is off. */
	uint32_t pp_control;
	uint32_t pp_on_delays;
	uint32_t pp_off_delays;
	uint64_t vdd_on_since_us;
	uint32_t south_chicken1;
	uint32_t south_dspclk_gate_d;

	/* AUX A, and the time a stuck-busy channel turns into a hardware timeout. */
	uint32_t aux_ctl;
	uint32_t aux_data[5];
	uint64_t stuck_until_us;

	/* The sink: DPCD, EDID EEPROM, I2C pointer; VDD must have been on sink_power_up_us before it answers. */
	uint8_t dpcd[0x800];
	uint8_t edid[512];
	unsigned edid_size;
	uint8_t i2c_offset;
	uint8_t i2c_segment;
	unsigned sink_power_up_us;

	/* The fault script: one entry per AUX transaction, then I915_DP_FAKE_OK; a non-zero fault_every_i2c_read hits every I2C read. */
	struct i915_dp_fake_step script[I915_DP_FAKE_SCRIPT_MAX];
	unsigned script_len;
	unsigned script_pos;
	int fault_every_i2c_read;

	/*
	 * The power-domain references the hardware side sees.  A reference put
	 * asynchronously stays counted while it is parked (100 ms), as in the
	 * real power layer.
	 */
	int refs_core;
	int refs_aux;
	int fail_power_get;
	int parked[2];
	uint64_t parked_due_us[2];
	unsigned async_parked;
	unsigned async_grabbed;
	unsigned async_released;

	/* The locks (I915_DP_LOCK_*) and the delayed work (I915_DP_WORK_*). */
	int lock_held[2];
	unsigned lock_acquisitions[2];
	unsigned lock_errors;
	int work_pending;
	uint64_t work_due_us;
	unsigned work_queued;
	unsigned work_cancelled;
	unsigned work_cancel_syncs;
	unsigned work_ran;

	/* What the model observed. */
	unsigned aux_transactions;
	unsigned aux_native_reads;
	unsigned aux_native_writes;
	unsigned aux_i2c_reads;
	unsigned aux_i2c_writes;
	unsigned aux_without_sink_power;        /* neither VDD nor panel power was on */
	unsigned aux_without_aux_power;         /* no AUX power-domain reference was held */
	unsigned aux_without_core_power;
	unsigned pp_writes_without_core_power;
	unsigned vdd_on_events;
	unsigned vdd_off_events;
	unsigned unknown_reg_reads;
	unsigned unknown_reg_writes;
	uint32_t last_unknown_reg;
	unsigned wait_timeouts;

	/* The world whose delayed work the model runs. */
	struct i915_dp_world *world;

	/* Told about every native DPCD write after it is stored (the sink's link training lives with the owner). */
	void (*on_dpcd_write)(void *ctx, unsigned addr, unsigned len);
	void *on_dpcd_write_ctx;
};

void drv_i915_dp_fake_init(struct i915_dp_fake_hw *hw, const uint8_t *dpcd_000, const uint8_t *dpcd_100, const uint8_t *dpcd_700, const uint8_t *edid, unsigned edid_size);
void drv_i915_dp_fake_script(struct i915_dp_fake_hw *hw, unsigned count, const uint8_t *faults, const uint8_t *params);
void drv_i915_dp_fake_bind_env(struct i915_dp_fake_hw *hw, struct i915_dp_env *env, struct i915_dp_world *world);
unsigned drv_i915_dp_fake_run_due(struct i915_dp_fake_hw *hw);
void drv_i915_dp_fake_flush_async(struct i915_dp_fake_hw *hw);

#endif
