/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Exercises real clock preparation against an explicit firmware provider model. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <uapi/errno.h>

#include "drivers/gpu/bcm2711/bcm2711-private.h"
#include "drivers/platform/rpi4/rpi4-firmware.h"

/* Each case owns one clock's state and records mutations in provider order. */
static uint32_t clock_state;
static uint32_t clock_rate;
static unsigned rate_writes;
static unsigned state_writes;
static uint32_t rejected_tag;
static bool clamped;
static bool enable_refused;
static bool short_reply;
static bool wrong_clock;

static void fixture(uint32_t state, uint32_t rate);

/*
 * Checks startup from the real zero-rate/off state and refusal before MMIO.
 */
int
main(
	void)
{
	uint32_t hz;
	int error;

	/* Reproduces firmware reporting HSM off with no next-enable rate. */
	fixture(0, 0);
	error = bcm2711_clock_prepare(13, 120000000, &hz);
	assert(error == 0 && hz == 120000000);
	assert(rate_writes == 1 && state_writes == 1);

	/* Preserves a running display's higher rate without provider mutations. */
	fixture(1, 300000000);
	error = bcm2711_clock_prepare(13, 120000000, &hz);
	assert(error == 0 && hz == 300000000);
	assert(rate_writes == 0 && state_writes == 0);

	/* Enables the core's existing configured rate even when GET_STATE says off. */
	fixture(0, 200000000);
	error = bcm2711_clock_prepare(4, 0, &hz);
	assert(error == 0 && hz == 200000000);
	assert(rate_writes == 0 && state_writes == 1);

	/* Rejects nonexistent providers without changing their state. */
	fixture(2, 0);
	error = bcm2711_clock_prepare(13, 120000000, &hz);
	assert(error == ENODEV && hz == 0);
	assert(rate_writes == 0 && state_writes == 0);

	/* Stops before enable when firmware rejects a rate change. */
	fixture(0, 0);
	rejected_tag = 0x38002;
	error = bcm2711_clock_prepare(13, 120000000, &hz);
	assert(error == EIO && hz == 0 && state_writes == 0);

	/* A clamped rate below the HSM minimum cannot authorize MMIO. */
	fixture(0, 0);
	clamped = true;
	error = bcm2711_clock_prepare(13, 120000000, &hz);
	assert(error == EIO && hz == 0 && state_writes == 0);

	/* An acknowledged enable is insufficient until fresh state reads confirm it. */
	fixture(0, 0);
	enable_refused = true;
	error = bcm2711_clock_prepare(13, 120000000, &hz);
	assert(error == ENODEV && hz == 0);

	/* Refuses truncated SET_RATE replies before an enable request. */
	fixture(0, 0);
	short_reply = true;
	error = bcm2711_clock_prepare(13, 120000000, &hz);
	assert(error == EIO && hz == 0 && state_writes == 0);

	/* Refuses a SET_RATE response naming another provider. */
	fixture(0, 0);
	wrong_clock = true;
	error = bcm2711_clock_prepare(13, 120000000, &hz);
	assert(error == EIO && hz == 0 && state_writes == 0);

	/* Succeeded: provider transitions and failure guards behaved as specified. */
	puts("PASS firmware clock preparation (9 cases)");
	return 0;
}

/*
 * Models the documented mailbox request lengths, including SET_RATE's third word.
 */
int
drv_rpi4_firmware_property(
	uint32_t tag,
	uint32_t *values,
	unsigned request_words,
	unsigned capacity_words,
	uint32_t *answered)
{
	/* Injects a provider transport failure before acknowledging a mutation. */
	if (tag == rejected_tag)
		return EIO;

	/* Checks the request's owner and allows a full two-word response. */
	assert(values[0] == 13 || values[0] == 4);
	assert(capacity_words >= 2);
	*answered = 8;

	/* Models each operation without invoking any physical mailbox or MMIO. */
	switch (tag) {
	case 0x30001:
		assert(request_words == 1);
		values[1] = clock_state;
		break;
	case 0x30002:
		assert(request_words == 1);
		values[1] = clock_rate;
		break;
	case 0x38002:
		assert(request_words == 3 && capacity_words == 3);
		assert(values[2] == 1 && state_writes == 0);
		rate_writes++;
		clock_rate = values[1];

		/* Models firmware clamping without silently granting the minimum. */
		if (clamped)
			clock_rate = 100000000;

		/* Models a malformed mutation response after firmware has seen the tag. */
		if (short_reply)
			*answered = 4;

		/* Models an unrelated provider acknowledgement. */
		if (wrong_clock)
			values[0]++;
		break;
	case 0x38001:
		assert(request_words == 2 && values[1] == 1);
		assert(clock_rate != 0);
		state_writes++;

		/* A refused enable leaves GET_STATE off despite successful transport. */
		if (!enable_refused)
			clock_state = 1;
		values[1] = clock_state;
		break;
	default:
		assert(false);
		return EINVAL;
	}

	/* Succeeded: the caller receives this modeled provider response. */
	return 0;
}

/*
 * Discards unrelated clock-report output in this provider-only fixture.
 */
void
bcm2711_stage_mark(
	const char *family,
	const char *format,
	...)
{
	/* The preparation helper does not rely on stage logging. */
	(void)family;
	(void)format;

	/* Succeeded: no physical console is involved. */
	return;
}

/* Starts each request sequence with independent firmware state and failures. */
static void
fixture(
	uint32_t state,
	uint32_t rate)
{
	/* Clears mutations and restores the requested initial provider state. */
	clock_state = state;
	clock_rate = rate;
	rate_writes = 0;
	state_writes = 0;
	rejected_tag = 0;
	clamped = false;
	enable_refused = false;
	short_reply = false;
	wrong_clock = false;

	/* Succeeded: the next case starts without earlier provider history. */
	return;
}
