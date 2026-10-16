/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The DisplayPort link of the display 12 and 13 code (xd-dp.c).
 *
 * A link is chosen before it is trained.  The rates a link can run at are
 * those both ends offer: the source's table up to the platform's and the
 * VBT's limit, the sink's standard rates up to its own limit and that of
 * any repeaters (or the list an eDP 1.4 panel gives).  For a mode, the
 * deepest colour first, then the lowest rate and the fewest lanes that
 * carry the mode's pixel rate is the link.  A link that fails to train
 * falls to the next lower shared rate, and below the lowest to half the
 * lanes at the highest rate.  The link's M/N values tell the transcoder
 * the stream's share of the link and the pixel clock against the link's
 * symbol clock.
 *
 * Training brings the sink's receivers up one after the other: any
 * repeaters (link training tunable PHY repeaters) from the highest
 * numbered down to repeater 1, then the sink.  Before it the repeaters are found and switched to
 * non-transparent mode, the sink's capabilities read again, and the
 * rate, lane count and coding written to the sink.  Each receiver is
 * trained in two phases: clock recovery with pattern 1, starting at the
 * lowest levels and following the levels the receiver asks for, and
 * channel equalization with the best pattern both ends know, again
 * following the receiver's requests.  The source's side of each step
 * (its signal levels, its pattern, the preparation of the port, the idle
 * pattern afterwards) is a hook of the caller; the sink's side is DPCD
 * access through another.
 *
 * Only 8b/10b links are trained: the platforms of this code do not drive
 * 128b/132b rates.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_XD_DP_H
#define DRIVERS_GPU_I915_DISPLAY_XD_DP_H

#include <stdint.h>

/* The most rates a list holds: every 8b/10b rate a source of these platforms offers. */
#define I915_XD_DP_RATES		8U

/* What drv_i915_xd_dp_rates() had to make up: a rate for a sink that offered none, a shared rate. */
#define I915_XD_DP_RATES_SINK_NONE	0x1U
#define I915_XD_DP_RATES_SHARED_NONE	0x2U

/* The kinds of note the training hands its caller. */
#define I915_XD_DP_NOTE_DEBUG		0U
#define I915_XD_DP_NOTE_ERROR		1U

/*
 * The rates both ends of a link offer: the source's, the sink's, and the
 * ones they share, each in increasing order (in kHz of the port clock).
 */
struct i915_xd_dp_rates {
	uint32_t source[I915_XD_DP_RATES];
	unsigned source_count;
	uint32_t sink[I915_XD_DP_RATES];
	unsigned sink_count;
	uint32_t common[I915_XD_DP_RATES];
	unsigned common_count;
};

/*
 * A link chosen for a mode: its rate, lane count and bits per pixel, the
 * data rate the mode needs and the one the link carries (kB/s).
 */
struct i915_xd_dp_choice {
	uint32_t rate_khz;
	unsigned lanes;
	unsigned bpp;
	uint32_t need_kbps;
	uint32_t carry_kbps;
};

/*
 * The M/N values of a stream on a link: the transfer unit size, the data
 * M and N (the stream's share of the link's payload) and the link M and
 * N (the pixel clock against the link's symbol clock).
 */
struct i915_xd_dp_m_n {
	uint32_t tu;
	uint32_t data_m;
	uint32_t data_n;
	uint32_t link_m;
	uint32_t link_n;
};

/*
 * The sink's side of a link: DPCD access through the link's AUX channel.
 *
 * read and write answer the bytes moved, or a negative error; probe
 * answers 0 when the sink is awake to answer at the address; read_caps
 * reads the receiver capabilities (and the extended ones a sink offers
 * in their place) and answers 0 or an error.
 */
struct i915_xd_dp_sink {
	int (*read)(void *context, uint32_t address, uint8_t *buffer, unsigned size);
	int (*write)(void *context, uint32_t address, const uint8_t *buffer, unsigned size);
	int (*probe)(void *context, uint32_t address);
	int (*read_caps)(void *context, uint8_t *caps);
	void *context;
};

/*
 * The source's side of a link: its signal levels from the four level
 * bytes, its training pattern from the pattern byte, the preparation of
 * the port before the sink is configured and the idle pattern after the
 * training (both may be NULL), a sleep of a range of microseconds, and
 * the notes of the training (may be NULL).
 */
struct i915_xd_dp_source {
	void (*levels)(void *context, const uint8_t set[4]);
	void (*pattern)(void *context, uint8_t pattern);
	void (*prepare)(void *context);
	void (*idle)(void *context);
	void (*sleep)(void *context, unsigned min_us, unsigned max_us);
	void (*note)(void *context, unsigned kind, const char *text);
	void *context;
};

/*
 * One training of a link.
 *
 * The caller fills in the link it wants, what the source can do, and the
 * storage the sink's capabilities live in between trainings (the
 * receiver capabilities, re-read by the training; the repeaters' common
 * and per-repeater capabilities).  The training leaves the levels of
 * each lane in set and the number of repeaters it trained in repeaters.
 */
struct i915_xd_dp_link {
	/* The link: rate, lanes, enhanced framing, and whether the sink ignores the stream's timing attributes. */
	uint32_t rate_khz;
	unsigned lanes;
	int enhanced;
	int msa_ignore;

	/* The index into an eDP 1.4 sink's rate list, or -1 to set the rate by its code. */
	int rate_index;

	/* The source: its highest swing and pre-emphasis levels, per-lane levels, patterns 3 and 4. */
	uint8_t swing_max;
	uint8_t preemph_max;
	int per_lane;
	int tps3;
	int tps4;

	/* Whether repeaters are looked for (not on eDP), and whether the link is up (repeaters keep their mode). */
	int find_repeaters;
	int active;

	/* The sink's capabilities the caller keeps. */
	uint8_t *caps;
	uint8_t *repeater_caps;
	uint8_t (*repeater_phy_caps)[3];

	/* The two sides. */
	struct i915_xd_dp_sink sink;
	struct i915_xd_dp_source source;

	/* What the training leaves: the level byte of each lane, and the repeaters trained. */
	uint8_t set[4];
	unsigned repeaters;
};

uint8_t drv_i915_xd_dp_rate_code(uint32_t rate_khz);
uint32_t drv_i915_xd_dp_code_rate(uint8_t code);
unsigned drv_i915_xd_dp_rates(uint32_t source_max_khz, uint32_t vbt_max_khz, uint8_t sink_code, uint8_t repeater_code, struct i915_xd_dp_rates *rates);
unsigned drv_i915_xd_dp_edp_rates(const uint8_t *table, uint32_t *rates, unsigned room);
int drv_i915_xd_dp_fallback(const uint32_t *common, unsigned count, uint32_t rate_khz, unsigned lanes, uint32_t *max_rate_khz, unsigned *max_lanes);
uint32_t drv_i915_xd_dp_need_kbps(uint32_t clock_khz, unsigned bpp);
uint32_t drv_i915_xd_dp_carry_kbps(uint32_t rate_khz, unsigned lanes);
uint32_t drv_i915_xd_dp_stream_kbps(uint32_t clock_khz, unsigned bpp_x16, uint32_t overhead_ppm);
unsigned drv_i915_xd_dp_symbol_bits(uint32_t rate_khz);
uint32_t drv_i915_xd_dp_symbol_khz(uint32_t rate_khz);
int drv_i915_xd_dp_choose(uint32_t clock_khz, unsigned max_bpc, unsigned min_bpc, const uint32_t *common, unsigned count, uint32_t rate_limit_khz, unsigned lane_limit, struct i915_xd_dp_choice *choice);
void drv_i915_xd_dp_m_n(unsigned bpp_x16, unsigned lanes, uint32_t clock_khz, uint32_t rate_khz, uint32_t overhead_ppm, struct i915_xd_dp_m_n *m_n);
int drv_i915_xd_dp_clock_recovered(const uint8_t *status, unsigned lanes);
int drv_i915_xd_dp_equalized(const uint8_t *status, unsigned lanes);
int drv_i915_xd_dp_read_status(const struct i915_xd_dp_sink *sink, unsigned hop, uint8_t *status);
int drv_i915_xd_dp_train(struct i915_xd_dp_link *link);
void drv_i915_xd_dp_train_end(struct i915_xd_dp_link *link);

#endif /* DRIVERS_GPU_I915_DISPLAY_XD_DP_H */
