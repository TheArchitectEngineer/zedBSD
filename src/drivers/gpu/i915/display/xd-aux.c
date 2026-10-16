/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The AUX channels of the display 12 and 13 code (see xd-aux.h): one
 * transaction on one channel.
 */

#include "xd-aux.h"
#include "xd-io.h"

#include "../intel/display-regs-xd.h"

#include <uapi/errno.h>
#include <stddef.h>
#include <stdint.h>

/* The bytes one data word of the channel holds. */
#define I915_XD_AUX_WORD_BYTES		4U

/*
 * The patience of a transaction: how many times the channel is looked
 * at, a millisecond apart, for an earlier transaction to end; how long
 * one try may take before it counts as stalled; how many tries one
 * request gets; and how long the channel rests after a damaged reply
 * before the next try.  A try the sink did not answer goes again at
 * once, because the hardware already waited the longest timeout for it.
 *
 * Values referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_dp_aux.c (intel_dp_aux_xfer: the
 * three busy reads 1 ms apart, the five tries, the 400 us after a
 * receive error; intel_dp_aux_wait_done: the 10 ms wait).
 */
#define I915_XD_AUX_IDLE_LOOKS		3U
#define I915_XD_AUX_IDLE_PAUSE_US	1000U
#define I915_XD_AUX_TRY_WAIT_US		10000U
#define I915_XD_AUX_TRIES		5U
#define I915_XD_AUX_DAMAGED_PAUSE_US	400U

/*
 * The sync pulses that open a transaction, counted in pulses: the
 * ordinary one is the longest precharge (16) and the preamble (16); the
 * one after a fast wake is the hardware's default precharge (10) and
 * the preamble (8).  The control word holds each count less one.
 *
 * Values referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_dp_aux.c (intel_dp_aux_sync_len,
 * intel_dp_aux_fw_sync_len).
 */
#define I915_XD_AUX_SYNC_PULSES		32U
#define I915_XD_AUX_FAST_WAKE_PULSES	18U

static int i915_xd_aux_wait_idle(struct i915_xd_io *io, uint32_t control, uint32_t *status);
static uint32_t i915_xd_aux_send(struct i915_xd_aux *aux, struct i915_xd_io *io, const struct i915_xd_aux_message *message, uint32_t control);
static uint32_t i915_xd_aux_try(struct i915_xd_aux *aux, struct i915_xd_io *io, const struct i915_xd_aux_message *message, uint32_t control_word);
static void i915_xd_aux_load(struct i915_xd_io *io, unsigned channel, const uint8_t *request, unsigned count);
static void i915_xd_aux_unload(struct i915_xd_io *io, unsigned channel, uint8_t *reply, unsigned count);
static uint32_t i915_xd_aux_word(const uint8_t *bytes, unsigned count);
static void i915_xd_aux_bytes(uint32_t word, uint8_t *bytes, unsigned count);

/*
 * Prepares one AUX channel of a display.
 *
 * The channel is numbered as the display numbers them: 0 to 2 for A to
 * C, 3 to 8 for the Type-C channels.  It returns EINVAL for a channel
 * the display does not have, and leaves the channel unprepared.
 */
int
drv_i915_xd_aux_init(
	struct i915_xd_aux *aux,
	unsigned channel)
{
	/* Refuses a channel past the last one. */
	if (channel >= I915_XD_AUX_CHANNELS)
		return EINVAL;

	/* Names the channel; nobody listens for stalls yet. */
	aux->channel = channel;
	aux->stalled = NULL;
	aux->context = NULL;

	/* Starts the behaviour counters from nothing. */
	aux->transactions = 0U;
	aux->tries = 0U;
	aux->no_answers = 0U;
	aux->receive_errors = 0U;
	aux->stalls = 0U;

	/* Succeeded: the channel is ready for transactions. */
	return 0;
}

/*
 * Builds the control word that starts a message's transaction.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_dp_aux.c (skl_get_aux_send_ctl).
 *
 * Writing the word starts the transaction and clears the done flag and
 * both errors left by the previous one.  The hardware waits for the
 * sink as long as it can (the longest timeout, 4 ms on these displays,
 * longer than the 600 us the specification asks for).
 */
uint32_t
drv_i915_xd_aux_control(
	const struct i915_xd_aux_message *message)
{
	uint32_t control;

	/* Starts the transaction and clears what the previous one left. */
	control = I915_XD_AUX_CTL_BUSY |
		I915_XD_AUX_CTL_DONE |
		I915_XD_AUX_CTL_INTERRUPT |
		I915_XD_AUX_CTL_TIMEOUT_ERROR |
		I915_XD_AUX_CTL_RECEIVE_ERROR;

	/* Waits the longest for the sink. */
	control |= i915_xd_field_put(I915_XD_AUX_CTL_TIMEOUT, I915_XD_AUX_CTL_TIMEOUT_MAX);

	/* Says how many bytes of the data words go out. */
	control |= i915_xd_field_put(I915_XD_AUX_CTL_SIZE, message->request_bytes);

	/* Opens the transaction with the sync pulses, both counts less one. */
	control |= i915_xd_field_put(I915_XD_AUX_CTL_FW_SYNC_PULSE, I915_XD_AUX_FAST_WAKE_PULSES - 1U);
	control |= i915_xd_field_put(I915_XD_AUX_CTL_SYNC_PULSE, I915_XD_AUX_SYNC_PULSES - 1U);

	/* A Type-C port in Thunderbolt mode reaches its sink through the Thunderbolt IO. */
	if (message->thunderbolt != 0)
		control |= I915_XD_AUX_CTL_TBT_IO;

	/* The hardware appends the HDCP transmitter key when the request asks for it. */
	if (message->hdcp_key != 0)
		control |= I915_XD_AUX_CTL_HDCP_KEY;

	/* Succeeded: the control word. */
	return control;
}

/*
 * Runs one transaction on a channel: sends the request and unloads the
 * sink's reply.
 *
 * The channel must be free of an earlier transaction; one that is still
 * busy after three looks a millisecond apart is left alone and the
 * request is not sent.  The request then gets up to five tries.  A try
 * the sink did not answer, or one that stalled, goes again at once; a
 * try whose reply arrived damaged goes again after a short rest; the
 * first try the channel reports done and undamaged ends the
 * transaction.  The result says what the transaction ended with
 * (I915_XD_AUX_*), the last control word, and how many reply bytes were
 * unloaded.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_dp_aux.c (intel_dp_aux_xfer,
 * intel_dp_aux_wait_done).
 */
void
drv_i915_xd_aux_exchange(
	struct i915_xd_aux *aux,
	struct i915_xd_io *io,
	const struct i915_xd_aux_message *message,
	struct i915_xd_aux_result *result)
{
	uint32_t control_offset;
	uint32_t control;
	uint32_t status;
	unsigned size;
	int idle;

	/* Starts from a result that says nothing was received. */
	result->outcome = I915_XD_AUX_SENT;
	result->status = 0U;
	result->reply_bytes = 0U;
	aux->transactions++;

	/* Leaves a channel an earlier transaction still holds. */
	control_offset = I915_XD_DDI_AUX_CTL(aux->channel);
	idle = i915_xd_aux_wait_idle(io, control_offset, &status);
	if (idle != 0) {
		result->outcome = I915_XD_AUX_BUSY;
		result->status = status;
		return;
	}

	/* Refuses a request or a reply room the data words cannot hold. */
	if (message->request_bytes > I915_XD_AUX_MESSAGE_MAX ||
	    message->reply_room > I915_XD_AUX_MESSAGE_MAX) {
		result->outcome = I915_XD_AUX_TOO_LONG;
		result->status = status;
		return;
	}

	/* Sends the request until the channel is done with it or the tries run out. */
	control = drv_i915_xd_aux_control(message);
	status = i915_xd_aux_send(aux, io, message, control);
	result->status = status;

	/* Reports a transaction the channel never finished. */
	if ((status & I915_XD_AUX_CTL_DONE) == 0U) {
		result->outcome = I915_XD_AUX_UNFINISHED;
		return;
	}

	/* Reports a reply that arrived damaged on the last try. */
	if ((status & I915_XD_AUX_CTL_RECEIVE_ERROR) != 0U) {
		result->outcome = I915_XD_AUX_RECEIVE_ERROR;
		return;
	}

	/* Reports a sink that did not answer the last try. */
	if ((status & I915_XD_AUX_CTL_TIMEOUT_ERROR) != 0U) {
		result->outcome = I915_XD_AUX_NO_ANSWER;
		return;
	}

	/*
	 * Refuses a reply of no bytes or of more than the data words hold,
	 * which the hardware must never report; nothing is unloaded.
	 */
	size = i915_xd_field_get(I915_XD_AUX_CTL_SIZE, status);
	if (size == 0U || size > I915_XD_AUX_MESSAGE_MAX) {
		result->outcome = I915_XD_AUX_BAD_SIZE;
		result->reply_bytes = size;
		return;
	}

	/* Unloads no more of the reply than the caller has room for. */
	if (size > message->reply_room)
		size = message->reply_room;

	i915_xd_aux_unload(io, aux->channel, message->reply, size);

	/* Succeeded: the reply is in the caller's room. */
	result->reply_bytes = size;
}

/*
 * Waits for an earlier transaction to leave the channel.
 *
 * The control word is read three times, a millisecond apart.  It returns
 * 0 with the idle word, or EBUSY with the word read once more after the
 * last look.
 */
static int
i915_xd_aux_wait_idle(
	struct i915_xd_io *io,
	uint32_t control,
	uint32_t *status)
{
	unsigned look;

	/* Looks at the busy flag until it is clear or the looks run out. */
	for (look = 0U; look < I915_XD_AUX_IDLE_LOOKS; look++) {
		*status = drv_i915_xd_io_read(io, control);
		if ((*status & I915_XD_AUX_CTL_BUSY) == 0U)
			return 0;

		(void)drv_i915_xd_io_delay(io, I915_XD_AUX_IDLE_PAUSE_US);
	}

	/* Reports the channel still busy, with the word as it reads now. */
	*status = drv_i915_xd_io_read(io, control);
	return EBUSY;
}

/*
 * Sends a request up to five times, until a try ends done and
 * undamaged, and reports the control word of the last try.
 */
static uint32_t
i915_xd_aux_send(
	struct i915_xd_aux *aux,
	struct i915_xd_io *io,
	const struct i915_xd_aux_message *message,
	uint32_t control)
{
	uint32_t status;
	unsigned attempt;

	/* Tries the request until the channel is done with it. */
	status = 0U;
	for (attempt = 0U; attempt < I915_XD_AUX_TRIES; attempt++) {
		status = i915_xd_aux_try(aux, io, message, control);

		/* A try the sink did not answer goes again at once. */
		if ((status & I915_XD_AUX_CTL_TIMEOUT_ERROR) != 0U) {
			aux->no_answers++;
			continue;
		}

		/* A damaged reply rests the channel before the next try. */
		if ((status & I915_XD_AUX_CTL_RECEIVE_ERROR) != 0U) {
			aux->receive_errors++;
			(void)drv_i915_xd_io_delay(io, I915_XD_AUX_DAMAGED_PAUSE_US);
			continue;
		}

		/* A try the channel finished ends the transaction. */
		if ((status & I915_XD_AUX_CTL_DONE) != 0U)
			return status;
	}

	/* Succeeded: the control word of the last try, which the caller judges. */
	return status;
}

/*
 * Runs one try of a request: loads the data words, starts the channel,
 * waits for it to finish, and acknowledges the done flag and the errors
 * by writing them back.  It reports the control word the wait ended
 * with.
 */
static uint32_t
i915_xd_aux_try(
	struct i915_xd_aux *aux,
	struct i915_xd_io *io,
	const struct i915_xd_aux_message *message,
	uint32_t control_word)
{
	uint32_t control;
	uint32_t status;
	int waited;

	/* Loads the request and starts the channel. */
	control = I915_XD_DDI_AUX_CTL(aux->channel);
	aux->tries++;
	i915_xd_aux_load(io, aux->channel, message->request, message->request_bytes);
	drv_i915_xd_io_write(io, control, control_word);

	/* Waits for the channel to drop its busy flag; a stall is told to the owner. */
	status = 0U;
	waited = drv_i915_xd_io_poll(io, control, I915_XD_AUX_CTL_BUSY, 0U, I915_XD_AUX_TRY_WAIT_US, &status);
	if (waited == ETIMEDOUT) {
		aux->stalls++;

		/* The owner reports the stall while the channel can still be read. */
		if (aux->stalled != NULL)
			aux->stalled(aux->context, status);
	}

	/* Acknowledges the done flag and both errors, keeping the rest of the word read. */
	drv_i915_xd_io_write(io,
			     control,
			     status | I915_XD_AUX_CTL_DONE | I915_XD_AUX_CTL_TIMEOUT_ERROR | I915_XD_AUX_CTL_RECEIVE_ERROR);

	/* Succeeded: the word the try ended with. */
	return status;
}

/* Loads a request into the channel's data words, four bytes a word. */
static void
i915_xd_aux_load(
	struct i915_xd_io *io,
	unsigned channel,
	const uint8_t *request,
	unsigned count)
{
	uint32_t word;
	unsigned offset;

	/* Fills one data word for every four bytes, the last one partly. */
	for (offset = 0U; offset < count; offset += I915_XD_AUX_WORD_BYTES) {
		word = i915_xd_aux_word(request + offset, count - offset);
		drv_i915_xd_io_write(io, I915_XD_DDI_AUX_DATA(channel, offset / I915_XD_AUX_WORD_BYTES), word);
	}
}

/* Unloads a reply from the channel's data words, four bytes a word. */
static void
i915_xd_aux_unload(
	struct i915_xd_io *io,
	unsigned channel,
	uint8_t *reply,
	unsigned count)
{
	uint32_t word;
	unsigned offset;

	/* Reads one data word for every four bytes, the last one partly. */
	for (offset = 0U; offset < count; offset += I915_XD_AUX_WORD_BYTES) {
		word = drv_i915_xd_io_read(io, I915_XD_DDI_AUX_DATA(channel, offset / I915_XD_AUX_WORD_BYTES));
		i915_xd_aux_bytes(word, reply + offset, count - offset);
	}
}

/*
 * Makes a data word of up to four bytes: the channel sends the word's
 * highest byte first, so the first byte goes to the top.
 */
static uint32_t
i915_xd_aux_word(
	const uint8_t *bytes,
	unsigned count)
{
	uint32_t word;
	unsigned index;

	/* A word holds four bytes; the rest belong to the next word. */
	if (count > I915_XD_AUX_WORD_BYTES)
		count = I915_XD_AUX_WORD_BYTES;

	/* Places the bytes from the top of the word down; missing bytes are 0. */
	word = 0U;
	for (index = 0U; index < count; index++)
		word |= (uint32_t)bytes[index] << (8U * (I915_XD_AUX_WORD_BYTES - 1U - index));

	/* Succeeded: the word to load. */
	return word;
}

/* Takes up to four bytes out of a data word, the first from its top. */
static void
i915_xd_aux_bytes(
	uint32_t word,
	uint8_t *bytes,
	unsigned count)
{
	unsigned index;

	/* A word holds four bytes; the rest come from the next word. */
	if (count > I915_XD_AUX_WORD_BYTES)
		count = I915_XD_AUX_WORD_BYTES;

	/* Takes the bytes from the top of the word down. */
	for (index = 0U; index < count; index++)
		bytes[index] = (uint8_t)(word >> (8U * (I915_XD_AUX_WORD_BYTES - 1U - index)));
}
