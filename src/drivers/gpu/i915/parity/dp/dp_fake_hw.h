/*
 * WS031 Linux-parity — GPU-free register model of what the eDP first stage
 * touches: the PCH panel power sequencer 0, AUX channel A with a DP sink behind
 * it (DPCD + an EDID EEPROM on I2C-over-AUX), a clock and power-domain
 * reference counters.  zedBSD project code; test support only.
 *
 * It is a model of the REGISTER CONTRACT the driver relies on (busy / done /
 * error bits, message size, big-endian data packing, write-one-to-clear), built
 * from the register definitions and the DP AUX message format -- not a recording
 * of the real GPU.  Faults are injected per transaction.
 */
#ifndef PARITY_DP_FAKE_HW_H
#define PARITY_DP_FAKE_HW_H

#include <stdint.h>
#include <stddef.h>
#include "parity_edp.h"

enum dp_fake_fault {
	DP_FAKE_OK = 0,
	DP_FAKE_NATIVE_DEFER,      /* reply 0x20 */
	DP_FAKE_NATIVE_NACK,       /* reply 0x10 */
	DP_FAKE_I2C_DEFER,         /* reply 0x80 (I2C transactions only) */
	DP_FAKE_I2C_NACK,          /* reply 0x40 (I2C transactions only) */
	DP_FAKE_SHORT_REPLY,       /* a read returns fewer data bytes than asked (param = bytes) */
	DP_FAKE_HW_TIMEOUT,        /* DONE + TIME_OUT_ERROR (no sink answer) */
	DP_FAKE_RECEIVE_ERROR,     /* DONE + RECEIVE_ERROR */
	DP_FAKE_STUCK_BUSY,        /* SEND_BUSY never clears */
	DP_FAKE_BAD_SIZE_ZERO,     /* DONE with message size 0 */
	DP_FAKE_BAD_SIZE_BIG,      /* DONE with message size 21 */
	DP_FAKE_INVALID_REPLY,     /* reply code 0x30 (reserved) */
	DP_FAKE_CORRUPT_DATA,      /* a read's first data byte is inverted (EDID checksum failures) */
};

#define DP_FAKE_SCRIPT_MAX 64u

struct dp_fake_hw {
	/* clock */
	uint64_t now_us;
	/* PPS 0 */
	uint32_t pp_control, pp_on_delays, pp_off_delays;
	uint64_t vdd_on_since_us;          /* 0 = off */
	uint32_t south_chicken1, south_dspclk_gate_d;
	/* AUX A */
	uint32_t aux_ctl, aux_data[5];
	/* the sink */
	uint8_t dpcd[0x800];
	uint8_t edid[512];
	unsigned edid_size;
	uint8_t i2c_offset, i2c_segment;
	unsigned sink_power_up_us;         /* VDD must have been on this long before the sink answers */
	/* fault script: consumed one entry per AUX transaction, then DP_FAKE_OK for ever */
	struct { uint8_t fault; uint8_t param; } script[DP_FAKE_SCRIPT_MAX];
	unsigned script_len, script_pos;
	int fault_every_i2c_read;          /* != 0: that fault on EVERY I2C read (persistent corruption etc.) */
	/* power domains */
	int refs_core, refs_aux;
	int fail_power_get;
	/* observations */
	unsigned aux_transactions, aux_native_reads, aux_native_writes, aux_i2c_reads, aux_i2c_writes;
	unsigned aux_without_sink_power;   /* transaction attempted while neither VDD nor panel power was on */
	unsigned aux_without_aux_power;    /* ... while no AUX power-domain reference was held */
	unsigned aux_without_core_power;
	unsigned pp_writes_without_core_power;
	unsigned vdd_on_events, vdd_off_events;
	unsigned unknown_reg_reads, unknown_reg_writes;
	uint32_t last_unknown_reg;
	unsigned wait_timeouts;
};

void dp_fake_init(struct dp_fake_hw *hw, const uint8_t *dpcd_000, const uint8_t *dpcd_100,
	const uint8_t *dpcd_700, const uint8_t *edid, unsigned edid_size);
void dp_fake_script(struct dp_fake_hw *hw, unsigned n, const uint8_t *faults, const uint8_t *params);
void dp_fake_bind_env(struct dp_fake_hw *hw, struct parity_dp_env *env);

#endif /* PARITY_DP_FAKE_HW_H */
