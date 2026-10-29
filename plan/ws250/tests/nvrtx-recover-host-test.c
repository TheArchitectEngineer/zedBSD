/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of src/drivers/gpu/nvrtx/recover.c: the sessions' channel
 * numbers are handed out after the last one, wrapping, and none is handed
 * out while all are in use; the culprit of a stalled graphics engine is
 * picked right on a model of the engine's scheduler (one context on the
 * engine, switched only at the end of a draw once its slice is used, a draw
 * that never ends never switched), with the engine asked which channel it
 * holds and without; what a stop left, and the stop's and reset's
 * parameters.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "drivers/gpu/nvrtx/nvrtx-work.h"

/* The checks run and the ones that failed. */
static unsigned checks;
static unsigned failures;

/* The model's channels: up to 33 (the sessions' 32 and the shared one), each with one run at a time. */
#define SIM_CHANNELS		33U
#define SIM_SLICE_MS		2U
#define SIM_RUN_MS		2000U
#define SIM_PERIOD_MS		250U
#define SIM_END_MS		20000U

/*
 * One channel of the model: its run (submitted at submit_ms, draws of
 * draw_ms each, or one draw that never ends), how far the run is, whether
 * its start stamp came and when, and how the next run follows (period_ms
 * after this one ended, 0 for none).
 */
struct sim_channel {
	int used;
	int forever;
	int pending;
	int started;
	int killed;
	uint64_t submit_ms;
	uint64_t start_ms;
	uint32_t draws;
	uint32_t draw_ms;
	uint32_t done_draws;
	uint32_t draw_left_ms;
	uint32_t period_ms;
	uint64_t next_ms;
	uint32_t ended_runs;
};

static void check(int condition, const char *what);
static void test_chid(void);
static void test_kill_state(void);
static void test_params(void);
static void test_scheduler(void);
static int sim_run(struct sim_channel *channels, int ask_engine, const char *what);

int
main(void)
{
	/* Each group of checks. */
	test_chid();
	test_kill_state();
	test_params();
	test_scheduler();

	/* The summary line. */
	printf("nvrtx-recover host test: %u checks, %u failed\n", checks, failures);
	if (failures != 0U)
		return 1;

	/* Succeeded: every check passed. */
	return 0;
}

/* Counts one check and tells a failed one. */
static void
check(
	int condition,
	const char *what)
{
	checks++;
	if (condition)
		return;

	/* A failed check is told. */
	failures++;
	printf("FAIL %s\n", what);
}

/* The next-fit numbers of the sessions' channels. */
static void
test_chid(void)
{
	uint32_t used;
	uint32_t last;
	int number;
	int index;

	/* The first number after the last one, from an empty set. */
	check(nvrtx_recover_chid_next(0U, NVRTX_WORK_SESSION_CHANNELS - 1U, NVRTX_WORK_SESSION_CHANNELS) == 0, "first is 0");
	check(nvrtx_recover_chid_next(0U, 0U, NVRTX_WORK_SESSION_CHANNELS) == 1, "after 0 comes 1");

	/* A number freed now is taken again only after every other free one. */
	used = 0U;
	last = NVRTX_WORK_SESSION_CHANNELS - 1U;
	for (index = 0; index < 5; index++) {
		number = nvrtx_recover_chid_next(used, last, NVRTX_WORK_SESSION_CHANNELS);
		check(number == index, "numbers in order");
		used |= 1U << (uint32_t)number;
		last = (uint32_t)number;
	}
	used &= ~(1U << 4);
	number = nvrtx_recover_chid_next(used, last, NVRTX_WORK_SESSION_CHANNELS);
	check(number == 5, "the number just freed waits");

	/* Wrapping past the end to a free number at the start. */
	used = 0xfffffffeU;
	check(nvrtx_recover_chid_next(used, 31U, 32U) == 0, "wraps to 0");
	used = 0x7fffffffU;
	check(nvrtx_recover_chid_next(used, 5U, 32U) == 31, "finds the last free");

	/* None while all are in use, and none for a count the bits cannot hold. */
	check(nvrtx_recover_chid_next(0xffffffffU, 3U, 32U) == -1, "all in use");
	check(nvrtx_recover_chid_next(0U, 0U, 0U) == -1, "no numbers");
	check(nvrtx_recover_chid_next(0U, 0U, 33U) == -1, "too many numbers");

	/* The last one itself when it is the only free one. */
	used = 0xffffffffU & ~(1U << 7);
	check(nvrtx_recover_chid_next(used, 7U, 32U) == 7, "the last one when it alone is free");
}

/* What a stop left, and the stop's poll. */
static void
test_kill_state(void)
{
	/* Any sign that it stopped, then whether it was freed. */
	check(nvrtx_recover_kill_state(1, 0, 0, 1) == NVRTX_WORK_CHANNEL_FREED, "stopped and freed");
	check(nvrtx_recover_kill_state(0, 1, 0, 1) == NVRTX_WORK_CHANNEL_FREED, "reported and freed");
	check(nvrtx_recover_kill_state(0, 0, 1, 1) == NVRTX_WORK_CHANNEL_FREED, "reset and freed");
	check(nvrtx_recover_kill_state(1, 0, 0, 0) == NVRTX_WORK_CHANNEL_STOPPED, "stopped, not freed");
	check(nvrtx_recover_kill_state(0, 0, 0, 1) == NVRTX_WORK_CHANNEL_STUCK, "no sign is stuck even when freed");
	check(nvrtx_recover_kill_state(0, 0, 0, 0) == NVRTX_WORK_CHANNEL_STUCK, "no sign is stuck");

	/* The poll: stopped when the GPU no longer runs it, or none of its runs is left. */
	check(nvrtx_recover_stopped(NVRTX_WORK_CHANNEL_FREED, 1), "freed is stopped");
	check(nvrtx_recover_stopped(NVRTX_WORK_CHANNEL_STOPPED, 1), "stopped is stopped");
	check(nvrtx_recover_stopped(NVRTX_WORK_CHANNEL_NONE, 1), "no channel is stopped");
	check(nvrtx_recover_stopped(NVRTX_WORK_CHANNEL_READY, 0), "ready and idle is stopped");
	check(!nvrtx_recover_stopped(NVRTX_WORK_CHANNEL_READY, 1), "ready and running is not");
	check(!nvrtx_recover_stopped(NVRTX_WORK_CHANNEL_LOST, 0), "lost is not");
	check(!nvrtx_recover_stopped(NVRTX_WORK_CHANNEL_KILLING, 0), "being stopped is not");
	check(!nvrtx_recover_stopped(NVRTX_WORK_CHANNEL_STUCK, 0), "stuck is not");
}

/* The stop's and the reset's parameters, byte by byte. */
static void
test_params(void)
{
	uint8_t params[16];
	uint8_t expected[16];
	size_t length;

	/* The stop: one byte, at once. */
	memset(params, 0xa5, sizeof(params));
	length = nvrtx_recover_stop_params(params);
	check(length == 1U && params[0] == 1U, "stop at once, one byte");

	/* The reset: the engine, subdevice 0, the default reason, the pending report. */
	memset(params, 0xa5, sizeof(params));
	memset(expected, 0, sizeof(expected));
	expected[0] = 1U;
	length = nvrtx_recover_reset_params(params, 1U, 0);
	check(length == 16U && memcmp(params, expected, 16U) == 0, "reset of engine 1, no report");
	expected[12] = 1U;
	length = nvrtx_recover_reset_params(params, 1U, 1);
	check(length == 16U && memcmp(params, expected, 16U) == 0, "reset of engine 1, a report pending");
	memset(expected, 0, sizeof(expected));
	expected[0] = 0x78U;
	expected[1] = 0x56U;
	expected[2] = 0x34U;
	expected[3] = 0x12U;
	length = nvrtx_recover_reset_params(params, 0x12345678U, 0);
	check(length == 16U && memcmp(params, expected, 16U) == 0, "the engine low byte first");
}

/* Sets one channel of the model up. */
static void
sim_channel_set(
	struct sim_channel *channel,
	uint64_t first_ms,
	uint32_t draws,
	uint32_t draw_ms,
	uint32_t period_ms,
	int forever)
{
	memset(channel, 0, sizeof(*channel));
	channel->used = 1;
	channel->forever = forever;
	channel->draws = draws;
	channel->draw_ms = draw_ms;
	channel->period_ms = period_ms;
	channel->next_ms = first_ms;
}

/* The culprit on the model, in each case, with the engine asked and without. */
static void
test_scheduler(void)
{
	struct sim_channel channels[SIM_CHANNELS];
	int ask;
	int stops;

	/* Each case twice: with the engine's current context, then with the start stamps alone. */
	for (ask = 1; ask >= 0; ask--) {
		/* A compositor's short runs every 16 ms and a draw that never ends from 100 ms on. */
		memset(channels, 0, sizeof(channels));
		sim_channel_set(&channels[0], 0U, 3U, 2U, 16U, 0);
		sim_channel_set(&channels[1], 100U, 1U, 1U, 0U, 1);
		stops = sim_run(channels, ask, "one culprit");
		check(stops == 1 && channels[1].killed && !channels[0].killed, "one culprit: only it is stopped");
		check(channels[0].ended_runs > 500U, "one culprit: the compositor goes on");

		/* A victim with long runs (preempted between its draws) submitted before the culprit. */
		memset(channels, 0, sizeof(channels));
		sim_channel_set(&channels[0], 0U, 40U, 1U, 5U, 0);
		sim_channel_set(&channels[5], 101U, 1U, 1U, 0U, 1);
		stops = sim_run(channels, ask, "victim begun first");
		check(stops == 1 && channels[5].killed && !channels[0].killed, "victim begun first: only the culprit is stopped");

		/* Two culprits and a compositor. */
		memset(channels, 0, sizeof(channels));
		sim_channel_set(&channels[0], 0U, 3U, 2U, 16U, 0);
		sim_channel_set(&channels[2], 100U, 1U, 1U, 0U, 1);
		sim_channel_set(&channels[3], 150U, 1U, 1U, 0U, 1);
		stops = sim_run(channels, ask, "two culprits");
		check(stops == 2 && channels[2].killed && channels[3].killed && !channels[0].killed, "two culprits: both stopped, the compositor not");

		/* The culprit on the shared work channel. */
		memset(channels, 0, sizeof(channels));
		sim_channel_set(&channels[0], 0U, 3U, 2U, 16U, 0);
		sim_channel_set(&channels[SIM_CHANNELS - 1U], 300U, 1U, 1U, 0U, 1);
		stops = sim_run(channels, ask, "shared culprit");
		check(stops == 1 && channels[SIM_CHANNELS - 1U].killed && !channels[0].killed, "shared culprit: the shared channel is stopped");

		/* Long but finite work (1.5 s draws) is never stopped. */
		memset(channels, 0, sizeof(channels));
		sim_channel_set(&channels[0], 0U, 3U, 2U, 16U, 0);
		sim_channel_set(&channels[1], 50U, 1U, 1500U, 4000U, 0);
		stops = sim_run(channels, ask, "long finite work");
		check(stops == 0, "long finite work: nothing is stopped");
	}

	/* No run waited its time, and a grace after a stop. */
	memset(channels, 0, sizeof(channels));
	check(nvrtx_recover_culprit(NULL, 0U, 0U, 0U, SIM_RUN_MS * 1000U) == -1, "no channels");
}

/*
 * Runs the model for SIM_END_MS: the engine one millisecond at a time, the
 * reaper's look every period (stopping the culprit it picks).  A pick of a
 * channel whose work ends is a failure.  Returns the stops made.
 */
static int
sim_run(
	struct sim_channel *channels,
	int ask_engine,
	const char *what)
{
	struct nvrtx_recover_view views[SIM_CHANNELS];
	struct sim_channel *channel;
	uint64_t now;
	uint64_t kill_end;
	uint32_t resident;
	uint32_t slice;
	uint32_t index;
	uint32_t step;
	int culprit;
	int stops;
	int has_work;
	int at_boundary;

	resident = SIM_CHANNELS;
	slice = 0U;
	kill_end = 0U;
	stops = 0;
	for (now = 0U; now < SIM_END_MS; now++) {
		/* New runs are submitted when due. */
		for (index = 0U; index < SIM_CHANNELS; index++) {
			channel = &channels[index];
			if (channel->used == 0 || channel->killed || channel->pending || channel->next_ms != now)
				continue;
			channel->pending = 1;
			channel->started = 0;
			channel->done_draws = 0U;
			channel->draw_left_ms = channel->draw_ms;
			channel->submit_ms = now;
		}

		/* The engine switches at a draw's end once the slice is used, or when its channel has no work. */
		has_work = 0;
		if (resident < SIM_CHANNELS && channels[resident].pending && !channels[resident].killed)
			has_work = 1;
		at_boundary = 0;
		if (has_work && channels[resident].draw_left_ms == channels[resident].draw_ms && !(channels[resident].forever && channels[resident].started))
			at_boundary = 1;
		if (!has_work || (slice >= SIM_SLICE_MS && at_boundary)) {
			for (step = 1U; step <= SIM_CHANNELS; step++) {
				index = (resident + step) % SIM_CHANNELS;
				if (channels[index].used && channels[index].pending && !channels[index].killed) {
					if (index != resident)
						slice = 0U;
					resident = index;
					break;
				}
			}
		}

		/* The resident channel's draw runs a millisecond; a run begins (its start stamp) with its first draw. */
		if (resident < SIM_CHANNELS && channels[resident].pending && !channels[resident].killed) {
			channel = &channels[resident];
			if (!channel->started) {
				channel->started = 1;
				channel->start_ms = now;
			}
			slice++;
			if (!channel->forever) {
				channel->draw_left_ms--;
				if (channel->draw_left_ms == 0U) {
					channel->done_draws++;
					channel->draw_left_ms = channel->draw_ms;
				}
				if (channel->done_draws == channel->draws) {
					channel->pending = 0;
					channel->ended_runs++;
					if (channel->period_ms != 0U)
						channel->next_ms = now + channel->period_ms;
				}
			}
		}

		/* The reaper's look. */
		if (now % SIM_PERIOD_MS != 0U)
			continue;
		for (index = 0U; index < SIM_CHANNELS; index++) {
			channel = &channels[index];
			memset(&views[index], 0, sizeof(views[index]));
			if (channel->used == 0 || channel->killed || !channel->pending)
				continue;
			views[index].busy = 1;
			views[index].oldest_submit_us = channel->submit_ms * 1000U;
			views[index].started = channel->started;
			views[index].start_ns = channel->start_ms * 1000000U;
			views[index].stoppable = 1;
			if (ask_engine && index == resident)
				views[index].resident = 1;
		}
		culprit = nvrtx_recover_culprit(views, SIM_CHANNELS, now * 1000U, kill_end * 1000U, SIM_RUN_MS * 1000U);
		if (culprit < 0)
			continue;

		/* The pick is stopped: it must be a draw that never ends. */
		if (!channels[culprit].forever) {
			printf("FAIL %s: channel %d picked (engine %s)\n", what, culprit, ask_engine ? "asked" : "not asked");
			failures++;
		}
		channels[culprit].killed = 1;
		channels[culprit].pending = 0;
		kill_end = now + 20U;
		stops++;
	}

	/* Succeeded: the stops made. */
	return stops;
}
