/* -*- mode: c; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Intel AX211 private API89 scan-session coordinator.
 *
 * Sources of the values.  The register offsets, bit positions, command
 * numbers, firmware file record types and wire layouts in this file are facts
 * of the AX211 hardware and of its firmware interface.  Intel publishes no
 * register or host-command specification for this device, so the values were
 * collected with Intel's own driver headers as the primary source: Linux
 * iwlwifi at tag v6.17, under drivers/net/wireless/intel/iwlwifi/, in
 * fw/api/scan.h.  They were cross-checked against OpenBSD
 * sys/dev/pci/if_iwxreg.h and if_iwx.c at commit 0f464d413c50.  Each block of
 * values below names the header it was taken from.
 *
 * This file is not an adaptation of those headers.  It collects the interface
 * values, gives them names of its own, and adds what the zedBSD driver
 * established about each of them while it was brought up on AX211 hardware:
 * where the value is used, what it means to the device or to the firmware,
 * the order the device requires, and how the firmware behaved.  It is written
 * as a reference that a developer of an AX211 driver for another operating
 * system can use on its own.
 */

#include "intel-ax211-scan-session.h"
#include <kern/kcrt.h>

/*
 * The answer to a scan cancel.
 *
 * Source: Linux iwlwifi v6.17 fw/api/scan.h (enum
 * iwl_umac_scan_abort_status).  The answer is one 32-bit status word: 0
 * means the scan was stopped, 1 that the stop is under way and a scan
 * completion will follow, 2 that no scan was running.  The session treats
 * status 2 as the end of the scan at once; after 0 or 1 it still waits for
 * the completion notification, which then reports the cancelled outcome.
 */
#define AX211_SCAN_CANCEL_RESPONSE_SIZE 4U
#define AX211_SCAN_CANCEL_DONE 0U
#define AX211_SCAN_CANCEL_PENDING 1U
#define AX211_SCAN_CANCEL_NO_SCAN 2U

static int ax211_scan_session_scan_result(int result);
static int ax211_scan_session_live(const struct intel_ax211_scan_session *session);
static int ax211_scan_session_channel_profile(struct intel_ax211_scan_session *session, uint8_t channel);
static int ax211_scan_session_submit(struct intel_ax211_scan_session *session, uint8_t opcode, const void *payload, size_t payload_length, uint64_t now_us, uint64_t timeout_us, uint8_t phase);
static int ax211_scan_session_command_result(int result);
static int ax211_scan_session_ack(struct intel_ax211_scan_session *session, uint8_t required_phase, const uint8_t *event_bytes, size_t event_length, uint32_t hardware_epoch, uint64_t now_us, uint32_t *abort_status);
static int ax211_scan_session_terminal(struct intel_ax211_scan_session *session, int result);

/*
 * Implements the drv intel ax211 scan session init operation.
 */
int
drv_intel_ax211_scan_session_init(
	struct intel_ax211_scan_session *session,
	struct intel_ax211_command_transaction *commands,
	const struct intel_ax211_protocol_command_table *command_table,
	const struct intel_ax211_protocol_nvm *nvm,
	const struct intel_ax211_runtime_mcc *mcc,
	const uint8_t station_address[6],
	uint32_t hardware_epoch)
{
	int error;
	struct intel_ax211_protocol_command_table copied;
	int result;

	/* Checks the drv intel ax211 command pending count result. */
	if (session == NULL || commands == NULL || command_table == NULL ||
	    command_table->bytes == NULL || nvm == NULL ||
	    station_address == NULL || hardware_epoch == 0U ||
	    command_table->count != INTEL_AX211_PROTOCOL_API89_COMMAND_COUNT ||
	    !commands->initialized ||
	    commands->hardware_epoch != hardware_epoch ||
	    drv_intel_ax211_command_pending_count(commands) != 0U ||
	    drv_intel_ax211_command_is_poisoned(commands)) {
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_INVALID;
	}

	/* Reads which command versions the firmware offers. */
	kern_memset(session, 0, sizeof(*session));
	kern_memcpy(session->command_version_bytes, command_table->bytes,
	       sizeof(session->command_version_bytes));

	/* Checks the drv intel ax211 scan api89 validate result. */
	result = drv_intel_ax211_protocol_command_table_parse(
		session->command_version_bytes,
		sizeof(session->command_version_bytes), &copied);
	if (result != INTEL_AX211_PROTOCOL_OK ||
	    drv_intel_ax211_scan_api89_validate(&copied) !=
		    INTEL_AX211_SCAN_OK) {
		kern_memset(session, 0, sizeof(*session));

		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_UNSUPPORTED;
	}

	/* Checks the operation result. */
	result = drv_intel_ax211_scan_profile_from_nvm(
		nvm, mcc, station_address, &session->full_profile);
	if (result != INTEL_AX211_SCAN_OK) {
		kern_memset(session, 0, sizeof(*session));

		/* Obtains the ax211 scan session scan result result. */
		error = ax211_scan_session_scan_result(result);

		/* Failed. */
		return error;
	}

	session->commands = commands;
	session->command_table.bytes = session->command_version_bytes;
	session->command_table.count = copied.count;
	session->hardware_epoch = hardware_epoch;
	session->phase = INTEL_AX211_SCAN_SESSION_IDLE;
	session->initialized = 1U;

	/* Returns the computed result. */
	return INTEL_AX211_SCAN_SESSION_OK;
}

/*
 * Implements the drv intel ax211 scan session begin channel operation.
 */
int
drv_intel_ax211_scan_session_begin_channel(
	struct intel_ax211_scan_session *session,
	uint64_t common_generation,
	uint8_t channel,
	uint64_t now_us)
{
	int error;
	int result;

	/* Checks the ax211 scan session live result. */
	if (!ax211_scan_session_live(session) || common_generation == 0U)
		return INTEL_AX211_SCAN_SESSION_INVALID;

	/* Handles the session condition. */
	if (session->phase != INTEL_AX211_SCAN_SESSION_IDLE &&
	    session->phase != INTEL_AX211_SCAN_SESSION_TERMINAL) {
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_BUSY;
	}

	/* Handles the session condition. */
	if (session->phase == INTEL_AX211_SCAN_SESSION_TERMINAL &&
	    session->terminal_result == INTEL_AX211_SCAN_SESSION_TIMEOUT) {
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_BUSY;
	}

	/* Checks the drv intel ax211 command pending count result. */
	if (drv_intel_ax211_command_pending_count(session->commands) != 0U ||
	    drv_intel_ax211_command_is_poisoned(session->commands)) {
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_COMMAND;
	}

	/* Checks the operation result. */
	result = ax211_scan_session_channel_profile(session, channel);
	if (result != INTEL_AX211_SCAN_SESSION_OK)
		return result;

	/* Checks the operation result. */
	result = drv_intel_ax211_scan_request_encode(&session->channel_profile,
						     session->request);
	if (result != INTEL_AX211_SCAN_OK) {
		/* Obtains the ax211 scan session scan result result. */
		error = ax211_scan_session_scan_result(result);

		/* Failed. */
		return error;
	}

	/* Checks the operation result. */
	result = drv_intel_ax211_scan_begin(
		&session->scan, &session->command_table,
		&session->channel_profile, session->hardware_epoch, now_us);
	if (result != INTEL_AX211_SCAN_OK) {
		/* Obtains the ax211 scan session scan result result. */
		error = ax211_scan_session_scan_result(result);

		/* Failed. */
		return error;
	}

	/* Starts one channel's scan and waits for the acknowledgement. */
	session->common_generation = common_generation;
	session->channel = channel;
	session->terminal_result = INTEL_AX211_SCAN_SESSION_OK;

	/* Checks the operation result. */
	result = ax211_scan_session_submit(
		session, INTEL_AX211_SCAN_REQUEST_OPCODE, session->request,
		sizeof(session->request), now_us,
		INTEL_AX211_SCAN_SESSION_COMMAND_TIMEOUT_US,
		INTEL_AX211_SCAN_SESSION_WAIT_START_ACK);
	if (result != INTEL_AX211_SCAN_SESSION_OK) {
		kern_memset(&session->scan, 0, sizeof(session->scan));
		session->phase = INTEL_AX211_SCAN_SESSION_TERMINAL;
		session->terminal_result = (uint8_t)result;
	}

	/* Returns the computed result. */
	return result;
}

/*
 * Implements the drv intel ax211 scan session start ack operation.
 */
int
drv_intel_ax211_scan_session_start_ack(
	struct intel_ax211_scan_session *session,
	const uint8_t *event_bytes,
	size_t event_length,
	uint32_t hardware_epoch,
	uint64_t now_us)
{
	int error;
	int result;

	/* Checks the operation result. */
	result = ax211_scan_session_ack(
		session, INTEL_AX211_SCAN_SESSION_WAIT_START_ACK, event_bytes,
		event_length, hardware_epoch, now_us, NULL);
	if (result != INTEL_AX211_SCAN_SESSION_OK)
		return result;

	/* Checks the operation result. */
	result = drv_intel_ax211_scan_request_ack(
		&session->scan, session->hardware_epoch, now_us);
	if (result != INTEL_AX211_SCAN_OK) {
		/* Obtains the ax211 scan session terminal result. */
		error = ax211_scan_session_terminal(
			session, ax211_scan_session_scan_result(result));

		/* Failed. */
		return error;
	}

	session->phase = INTEL_AX211_SCAN_SESSION_RUNNING;

	/* Returns the computed result. */
	return INTEL_AX211_SCAN_SESSION_OK;
}

/*
 * Implements the drv intel ax211 scan session notification operation.
 */
int
drv_intel_ax211_scan_session_notification(
	struct intel_ax211_scan_session *session,
	const struct intel_ax211_protocol_message *message,
	uint64_t now_us,
	struct intel_ax211_scan_session_event *event)
{
	int error;
	struct intel_ax211_scan_event decoded;
	int result;

	/* Checks the ax211 scan session live result. */
	if (!ax211_scan_session_live(session) || message == NULL ||
	    event == NULL) {
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_INVALID;
	}

	/* Handles the session condition. */
	if (session->phase == INTEL_AX211_SCAN_SESSION_TERMINAL)
		return INTEL_AX211_SCAN_SESSION_DUPLICATE;

	/* Handles the session condition. */
	if (session->phase != INTEL_AX211_SCAN_SESSION_RUNNING &&
	    session->phase != INTEL_AX211_SCAN_SESSION_WAIT_ABORT_COMPLETE) {
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_OUT_OF_ORDER;
	}

	/* Handles the session condition. */
	if (session->phase == INTEL_AX211_SCAN_SESSION_WAIT_ABORT_COMPLETE &&
	    now_us >= session->command_deadline) {
		/* Obtains the ax211 scan session terminal result. */
		error = ax211_scan_session_terminal(
			session, INTEL_AX211_SCAN_SESSION_TIMEOUT);

		/* Failed. */
		return error;
	}

	kern_memset(&decoded, 0, sizeof(decoded));
	result = drv_intel_ax211_scan_event_accept(&session->scan, message,
						   now_us, &decoded);

	/* Checks the operation status. */
	result = ax211_scan_session_scan_result(result);
	if (result == INTEL_AX211_SCAN_SESSION_COMPLETE ||
	    result == INTEL_AX211_SCAN_SESSION_ABORTED ||
	    result == INTEL_AX211_SCAN_SESSION_FAILED ||
	    result == INTEL_AX211_SCAN_SESSION_TIMEOUT) {
		/* Checks the operation result. */
		if (result != INTEL_AX211_SCAN_SESSION_TIMEOUT)
			session->scan.abort_required = 0U;
		ax211_scan_session_terminal(session, result);
	}

	/* Checks the operation status. */
	if (result == INTEL_AX211_SCAN_SESSION_COMPLETE ||
	    result == INTEL_AX211_SCAN_SESSION_ABORTED ||
	    result == INTEL_AX211_SCAN_SESSION_FAILED ||
	    result == INTEL_AX211_SCAN_SESSION_OK) {
		kern_memset(event, 0, sizeof(*event));
		event->common_generation = session->common_generation;
		event->channel = session->channel;
		event->firmware = decoded;
	}

	/* Returns the computed result. */
	return result;
}

/*
 * Implements the drv intel ax211 scan session abort operation.
 */
int
drv_intel_ax211_scan_session_abort(
	struct intel_ax211_scan_session *session,
	uint64_t common_generation,
	uint64_t now_us)
{
	int error;
	uint8_t payload[INTEL_AX211_SCAN_CANCEL_SIZE];
	int result;

	/* Checks the ax211 scan session live result. */
	if (!ax211_scan_session_live(session) || common_generation == 0U)
		return INTEL_AX211_SCAN_SESSION_INVALID;

	/* Handles the common generation condition. */
	if (common_generation != session->common_generation)
		return INTEL_AX211_SCAN_SESSION_STALE;

	/* Handles the session condition. */
	if (session->phase == INTEL_AX211_SCAN_SESSION_TERMINAL &&
	    !session->scan.abort_required) {
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_DUPLICATE;
	}

	/* Handles the session condition. */
	if (session->phase != INTEL_AX211_SCAN_SESSION_RUNNING &&
	    !(session->phase == INTEL_AX211_SCAN_SESSION_TERMINAL &&
	      session->scan.abort_required)) {
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_OUT_OF_ORDER;
	}

	/* Checks the drv intel ax211 scan abort encode result. */
	if (drv_intel_ax211_scan_abort_encode(payload) != INTEL_AX211_SCAN_OK)
		return INTEL_AX211_SCAN_SESSION_FAILED;

	/* Checks the operation result. */
	result = ax211_scan_session_submit(
		session, INTEL_AX211_SCAN_CANCEL_OPCODE, payload,
		sizeof(payload), now_us,
		INTEL_AX211_SCAN_SESSION_ABORT_TIMEOUT_US,
		INTEL_AX211_SCAN_SESSION_WAIT_ABORT_ACK);
	if (result != INTEL_AX211_SCAN_SESSION_OK) {
		/* Obtains the ax211 scan session terminal result. */
		error = ax211_scan_session_terminal(session, result);

		/* Failed. */
		return error;
	}

	/* Returns the computed result. */
	return INTEL_AX211_SCAN_SESSION_OK;
}

/*
 * Implements the drv intel ax211 scan session abort ack operation.
 */
int
drv_intel_ax211_scan_session_abort_ack(
	struct intel_ax211_scan_session *session,
	const uint8_t *event_bytes,
	size_t event_length,
	uint32_t hardware_epoch,
	uint64_t now_us)
{
	int error;
	uint32_t abort_status;
	int result;

	/* Checks the operation result. */
	result = ax211_scan_session_ack(
		session, INTEL_AX211_SCAN_SESSION_WAIT_ABORT_ACK, event_bytes,
		event_length, hardware_epoch, now_us, &abort_status);
	if (result != INTEL_AX211_SCAN_SESSION_OK)
		return result;

	/* Handles the abort status condition. */
	if (abort_status != AX211_SCAN_CANCEL_DONE &&
	    abort_status != AX211_SCAN_CANCEL_PENDING &&
	    abort_status != AX211_SCAN_CANCEL_NO_SCAN) {
		/* Obtains the ax211 scan session terminal result. */
		error = ax211_scan_session_terminal(
			session, INTEL_AX211_SCAN_SESSION_COMMAND);

		/* Failed. */
		return error;
	}

	/* Handles the abort status condition. */
	if (abort_status == AX211_SCAN_CANCEL_NO_SCAN) {
		session->scan.abort_required = 0U;
		session->scan.phase = INTEL_AX211_SCAN_PHASE_TERMINAL;

		/* Obtains the ax211 scan session terminal result. */
		error = ax211_scan_session_terminal(
			session, INTEL_AX211_SCAN_SESSION_ABORTED);

		/* Failed. */
		return error;
	}

	/* SUCCESS and IN_PROGRESS both complete through SCAN_COMPLETE_UMAC. */
	session->scan.phase = INTEL_AX211_SCAN_PHASE_RUNNING;
	session->scan.scan_deadline = session->command_deadline;
	session->phase = INTEL_AX211_SCAN_SESSION_WAIT_ABORT_COMPLETE;

	/* Returns the computed result. */
	return INTEL_AX211_SCAN_SESSION_OK;
}

/*
 * Implements the drv intel ax211 scan session expire operation.
 */
int
drv_intel_ax211_scan_session_expire(
	struct intel_ax211_scan_session *session,
	uint64_t now_us)
{
	int error;
	struct intel_ax211_command_handle expired;
	int result;

	/* Checks the ax211 scan session live result. */
	if (!ax211_scan_session_live(session))
		return INTEL_AX211_SCAN_SESSION_INVALID;

	/* Handles the session condition. */
	if (session->phase == INTEL_AX211_SCAN_SESSION_TERMINAL)
		return INTEL_AX211_SCAN_SESSION_DUPLICATE;

	/* Handles the session condition. */
	if (session->phase == INTEL_AX211_SCAN_SESSION_WAIT_START_ACK ||
	    session->phase == INTEL_AX211_SCAN_SESSION_WAIT_ABORT_ACK) {
		/* Handles the now us condition. */
		if (now_us < session->command_deadline)
			return INTEL_AX211_SCAN_SESSION_OK;
		kern_memset(&expired, 0, sizeof(expired));

		/* Checks the operation result. */
		result = drv_intel_ax211_command_timeout_oldest(
			session->commands, now_us, &expired);
		if (result == INTEL_AX211_COMMAND_PENDING)
			return INTEL_AX211_SCAN_SESSION_OK;
		if (result != INTEL_AX211_COMMAND_TIMEOUT) {
			/* Obtains the ax211 scan session terminal result. */
			error = ax211_scan_session_terminal(
				session,
				ax211_scan_session_command_result(result));

			/* Failed. */
			return error;
		}

		/* Handles the expired condition. */
		if (expired.token.queue !=
			    session->command_handle.token.queue ||
		    expired.token.index !=
			    session->command_handle.token.index ||
		    expired.generation != session->command_handle.generation) {
			/* Obtains the ax211 scan session terminal result. */
			error = ax211_scan_session_terminal(
				session, INTEL_AX211_SCAN_SESSION_OUT_OF_ORDER);

			/* Failed. */
			return error;
		}

		/* Obtains the ax211 scan session terminal result. */
		error = ax211_scan_session_terminal(
			session, INTEL_AX211_SCAN_SESSION_TIMEOUT);

		/* Failed. */
		return error;
	}

	/* Handles the session condition. */
	if (session->phase == INTEL_AX211_SCAN_SESSION_WAIT_ABORT_COMPLETE) {
		/* Handles the now us condition. */
		if (now_us < session->command_deadline)
			return INTEL_AX211_SCAN_SESSION_OK;
		session->scan.phase = INTEL_AX211_SCAN_PHASE_TERMINAL;
		session->scan.abort_required = 1U;

		/* Obtains the ax211 scan session terminal result. */
		error = ax211_scan_session_terminal(
			session, INTEL_AX211_SCAN_SESSION_TIMEOUT);

		/* Failed. */
		return error;
	}

	/* Handles the session condition. */
	if (session->phase != INTEL_AX211_SCAN_SESSION_RUNNING)
		return INTEL_AX211_SCAN_SESSION_OUT_OF_ORDER;
	result = drv_intel_ax211_scan_expire(&session->scan, now_us);

	/* Checks the operation result. */
	result = ax211_scan_session_scan_result(result);
	if (result == INTEL_AX211_SCAN_SESSION_TIMEOUT) {
		/* Obtains the ax211 scan session terminal result. */
		error = ax211_scan_session_terminal(session, result);

		/* Failed. */
		return error;
	}

	/* Returns the computed result. */
	return result;
}

/* Supports the ax211 scan session scan result operation. */
static int
ax211_scan_session_scan_result(
	int result)
{
	/* Dispatch the selected operation case. */
	switch (result) {
	case INTEL_AX211_SCAN_OK:
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_OK;
	case INTEL_AX211_SCAN_INVALID:
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_INVALID;
	case INTEL_AX211_SCAN_UNSUPPORTED:
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_UNSUPPORTED;
	case INTEL_AX211_SCAN_STALE:
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_STALE;
	case INTEL_AX211_SCAN_DUPLICATE:
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_DUPLICATE;
	case INTEL_AX211_SCAN_OUT_OF_ORDER:
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_OUT_OF_ORDER;
	case INTEL_AX211_SCAN_TIMED_OUT:
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_TIMEOUT;
	case INTEL_AX211_SCAN_COMPLETE:
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_COMPLETE;
	case INTEL_AX211_SCAN_ABORTED:
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_ABORTED;
	case INTEL_AX211_SCAN_TRUNCATED:
	case INTEL_AX211_SCAN_OVERSIZED:
	case INTEL_AX211_SCAN_FAILED:
	default:
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_FAILED;
	}
}

/* Supports the ax211 scan session live operation. */
static int
ax211_scan_session_live(
	const struct intel_ax211_scan_session *session)
{
	/* Handles the session availability. */
	if (session == NULL || !session->initialized ||
	    session->commands == NULL || session->hardware_epoch == 0U) {
		/* Succeeded. */
		return 0;
	}

	/* Returns the computed result. */
	return session->commands->initialized &&
	       session->commands->hardware_epoch == session->hardware_epoch;
}

/* Supports the ax211 scan session channel profile operation. */
static int
ax211_scan_session_channel_profile(
	struct intel_ax211_scan_session *session,
	uint8_t channel)
{
	size_t index;

	/* Process each remaining element. */
	for (index = 0U; index < session->full_profile.channel_count; index++) {
		/* Handles the session condition. */
		if (session->full_profile.channel[index] == channel) {
			kern_memset(&session->channel_profile, 0,
			       sizeof(session->channel_profile));
			kern_memcpy(session->channel_profile.station_address,
			       session->full_profile.station_address, 6U);
			session->channel_profile.channel_width_mhz =
				session->full_profile.channel_width_mhz;
			session->channel_profile.channel[0] = channel;
			session->channel_profile.channel_count = 1U;

			/* Returns the computed result. */
			return INTEL_AX211_SCAN_SESSION_OK;
		}
	}

	/* Returns the computed result. */
	return INTEL_AX211_SCAN_SESSION_UNSUPPORTED;
}

/* Supports the ax211 scan session submit operation. */
static int
ax211_scan_session_submit(
	struct intel_ax211_scan_session *session,
	uint8_t opcode,
	const void *payload,
	size_t payload_length,
	uint64_t now_us,
	uint64_t timeout_us,
	uint8_t phase)
{
	struct intel_ax211_command_request request;
	int result;

	/* Handles the now us condition. */
	if (now_us > UINT64_MAX - timeout_us)
		return INTEL_AX211_SCAN_SESSION_INVALID;
	kern_memset(&request, 0, sizeof(request));
	request.command.group = INTEL_AX211_SCAN_FAMILY_CLASSIC_WIDE;
	request.command.opcode = opcode;

	/* API89 layouts are 17/1; the Gen3 wide-header wire version is zero. */
	request.command.version = 0U;
	request.payload = payload;
	request.payload_length = payload_length;
	request.response_version = 0U;

	/* Handles the opcode condition. */
	if (opcode == INTEL_AX211_SCAN_CANCEL_OPCODE) {
		request.minimum_response_length =
			AX211_SCAN_CANCEL_RESPONSE_SIZE;
		request.maximum_response_length =
			AX211_SCAN_CANCEL_RESPONSE_SIZE;
	} else {
		request.minimum_response_length = 0U;
		request.maximum_response_length = 0U;
	}

	result = drv_intel_ax211_command_submit(session->commands, &request,
						now_us, timeout_us,
						&session->command_handle);

	/* Checks the operation result. */
	result = ax211_scan_session_command_result(result);
	if (result != INTEL_AX211_SCAN_SESSION_OK)
		return result;
	session->command_deadline = now_us + timeout_us;
	session->phase = phase;

	/* Returns the computed result. */
	return INTEL_AX211_SCAN_SESSION_OK;
}

/* Supports the ax211 scan session command result operation. */
static int
ax211_scan_session_command_result(
	int result)
{
	/* Dispatch the selected operation case. */
	switch (result) {
	case INTEL_AX211_COMMAND_OK:
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_OK;
	case INTEL_AX211_COMMAND_STALE:
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_STALE;
	case INTEL_AX211_COMMAND_DUPLICATE:
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_DUPLICATE;
	case INTEL_AX211_COMMAND_OUT_OF_ORDER:
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_OUT_OF_ORDER;
	case INTEL_AX211_COMMAND_TIMEOUT:
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_TIMEOUT;
	case INTEL_AX211_COMMAND_FULL:
	case INTEL_AX211_COMMAND_PENDING:
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_BUSY;
	case INTEL_AX211_COMMAND_INVALID:
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_INVALID;
	default:
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_COMMAND;
	}
}

/* Supports the ax211 scan session ack operation. */
static int
ax211_scan_session_ack(
	struct intel_ax211_scan_session *session,
	uint8_t required_phase,
	const uint8_t *event_bytes,
	size_t event_length,
	uint32_t hardware_epoch,
	uint64_t now_us,
	uint32_t *abort_status)
{
	int error;
	struct intel_ax211_event event;
	uint8_t response[AX211_SCAN_CANCEL_RESPONSE_SIZE];
	void *response_bytes;
	size_t response_capacity;
	size_t response_length;
	int result;

	/* Checks the ax211 scan session live result. */
	if (!ax211_scan_session_live(session) || event_bytes == NULL ||
	    hardware_epoch == 0U) {
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_INVALID;
	}

	/* Handles the hardware epoch condition. */
	if (hardware_epoch != session->hardware_epoch)
		return INTEL_AX211_SCAN_SESSION_STALE;

	/* Handles the session condition. */
	if (session->phase == INTEL_AX211_SCAN_SESSION_TERMINAL)
		return INTEL_AX211_SCAN_SESSION_DUPLICATE;

	/* Handles the session condition. */
	if (session->phase != required_phase) {
		/* Handles the required phase condition. */
		if ((required_phase ==
			     INTEL_AX211_SCAN_SESSION_WAIT_START_ACK &&
		     session->phase == INTEL_AX211_SCAN_SESSION_RUNNING) ||
		    (required_phase ==
			     INTEL_AX211_SCAN_SESSION_WAIT_ABORT_ACK &&
		     session->phase ==
			     INTEL_AX211_SCAN_SESSION_WAIT_ABORT_COMPLETE)) {
			/* Returns the computed result. */
			return INTEL_AX211_SCAN_SESSION_DUPLICATE;
		}

		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_OUT_OF_ORDER;
	}

	/* Handles the abort status availability. */
	if ((required_phase == INTEL_AX211_SCAN_SESSION_WAIT_ABORT_ACK) !=
	    (abort_status != NULL)) {
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_INVALID;
	}

	/* Handles the now us condition. */
	if (now_us >= session->command_deadline) {
		/* Obtains the drv intel ax211 scan session expire result. */
		error =
			drv_intel_ax211_scan_session_expire(session, now_us);

		/* Failed. */
		return error;
	}

	/* Checks the drv intel ax211 event decode result. */
	if (drv_intel_ax211_event_decode(event_bytes, event_length, &event) !=
	    INTEL_AX211_SUCCESS) {
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_COMMAND;
	}

	/* Handles the event condition. */
	if (event.queue != session->command_handle.token.queue ||
	    event.index != session->command_handle.token.index) {
		/* Returns the computed result. */
		return INTEL_AX211_SCAN_SESSION_OUT_OF_ORDER;
	}
	response_bytes = NULL;

	/* Handles the abort status availability. */
	response_capacity = 0U;
	if (abort_status != NULL) {
		kern_memset(response, 0, sizeof(response));
		response_bytes = response;
		response_capacity = sizeof(response);
	}

	response_length = 0U;
	result = drv_intel_ax211_command_complete(
		session->commands, event_bytes, event_length, hardware_epoch,
		response_bytes, response_capacity, &response_length);

	/* Checks the operation result. */
	result = ax211_scan_session_command_result(result);
	if (result != INTEL_AX211_SCAN_SESSION_OK) {
		/* Obtains the ax211 scan session terminal result. */
		error = ax211_scan_session_terminal(session, result);

		/* Failed. */
		return error;
	}

	/* Handles the abort status availability. */
	if (abort_status == NULL && response_length != 0U) {
		/* Obtains the ax211 scan session terminal result. */
		error = ax211_scan_session_terminal(
			session, INTEL_AX211_SCAN_SESSION_COMMAND);

		/* Failed. */
		return error;
	}

	/* Handles the abort status availability. */
	if (abort_status != NULL) {
		/* Handles the response length condition. */
		if (response_length != sizeof(response)) {
			/* Obtains the ax211 scan session terminal result. */
			error = ax211_scan_session_terminal(
				session, INTEL_AX211_SCAN_SESSION_COMMAND);

			/* Failed. */
			return error;
		}

		*abort_status = (uint32_t)response[0U] |
				((uint32_t)response[1U] << 8) |
				((uint32_t)response[2U] << 16) |
				((uint32_t)response[3U] << 24);
	}

	/* Returns the computed result. */
	return INTEL_AX211_SCAN_SESSION_OK;
}

/* Supports the ax211 scan session terminal operation. */
static int
ax211_scan_session_terminal(
	struct intel_ax211_scan_session *session,
	int result)
{
	session->phase = INTEL_AX211_SCAN_SESSION_TERMINAL;
	session->terminal_result = (uint8_t)result;

	/* Returns the computed result. */
	return result;
}
