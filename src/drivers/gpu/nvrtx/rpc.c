/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Calls to the GSP's resource manager, and the events it sends.
 *
 * A call is put in the command queue and the GSP is told by its doorbell.
 * Its reply comes back in the status queue with the same function and the
 * caller's sequence number; the reply is matched on both (design 2.6).  While
 * the driver waits, the GSP may send events: the CPU sequencer (a list of
 * register operations the driver performs at once) and others, which are
 * reported and dropped in this revision.  Only the worker calls these, one
 * call at a time.
 */

#include <stddef.h>
#include <stdint.h>

#include <kern/clock.h>
#include <kern/kmem.h>
#include <uapi/errno.h>

#include "drivers/gpu/nvrtx/nvrtx-gpu.h"

/* The GSP's doorbell for the command queue. */
#define RPC_REG_DOORBELL		(NVRTX_FALCON_GSP_BASE + 0xc00U)

/* The GSP falcon's CPU control, its alias, and the bits the sequencer uses. */
#define RPC_GSP_CPU_CONTROL		(NVRTX_FALCON_GSP_BASE + 0x100U)
#define RPC_GSP_CPU_ALIAS		(NVRTX_FALCON_GSP_BASE + 0x130U)
#define RPC_CPU_START			0x2U
#define RPC_CPU_HALTED			0x10U
#define RPC_CPU_ALIAS_ENABLED		0x40U

/* How long a send waits for room, how long a poll command waits by default, and the step. */
#define RPC_SEND_TIMEOUT_US		1000000U
#define RPC_POLL_DEFAULT_US		4000000U
#define RPC_HALT_TIMEOUT_US		2000000U
#define RPC_STEP_US			10U

/* The channel-error event's fields: engine, channel, kind of exception, faulting address and fault kind. */
#define RPC_ERROR_ENGINE_AT		0U
#define RPC_ERROR_CHANNEL_AT		4U
#define RPC_ERROR_TYPE_AT		16U
#define RPC_ERROR_FAULT_LOW_AT		28U
#define RPC_ERROR_FAULT_HIGH_AT		32U
#define RPC_ERROR_FAULT_KIND_AT		36U
#define RPC_ERROR_BYTES			40U

static int rpc_sequencer(struct nvrtx_gpu *gpu, uint8_t *payload, size_t length);
static int rpc_exchange(struct nvrtx_gpu *gpu, uint32_t function, size_t call_length, size_t *reply_length);

/*
 * Puts one call in the command queue and rings the doorbell.
 *
 * A call that waits for its reply takes the next sequence number; one that
 * does not (sent before the GSP runs) takes zero.  Returns ETIMEDOUT when the
 * queue had no room for a second.
 */
int
nvrtx_rpc_send(
	struct nvrtx_gpu *gpu,
	uint32_t function,
	const uint8_t *payload,
	size_t length,
	int with_sequence,
	uint32_t *sequence)
{
	uint32_t waited;
	int error;

	/* Numbers the call; zero means no reply is matched. */
	*sequence = 0U;
	if (with_sequence != 0) {
		gpu->call_sequence++;
		if (gpu->call_sequence == 0U)
			gpu->call_sequence = 1U;
		*sequence = gpu->call_sequence;
	}

	/* Puts the call in the queue, waiting for room while the GSP reads. */
	for (waited = 0U; waited <= RPC_SEND_TIMEOUT_US; waited += RPC_STEP_US) {
		error = nvrtx_channel_send(&gpu->channel, function, *sequence, payload, length);
		if (error != EAGAIN)
			break;

		kern_usleep_range(RPC_STEP_US, RPC_STEP_US * 2U);
	}

	/* A full queue that never drained, or a call too large. */
	if (error == EAGAIN)
		return ETIMEDOUT;
	if (error != 0)
		return error;

	/* Tells the GSP there is a call. */
	nvrtx_wr32(gpu, RPC_REG_DOORBELL, 0U);

	/* Succeeded: the call is queued. */
	return 0;
}

/*
 * Waits for the reply (or event) of a function, serving other events meanwhile.
 *
 * A nonzero sequence must match too.  The payload is left in gpu->reply with
 * its length.  Returns EIO when the reply carries an error result,
 * ETIMEDOUT when it did not come in time.
 */
int
nvrtx_rpc_wait(
	struct nvrtx_gpu *gpu,
	uint32_t function,
	uint32_t sequence,
	uint32_t timeout_us,
	size_t *length)
{
	uint32_t got_function;
	uint32_t got_sequence;
	uint32_t result;
	uint32_t waited;
	int error;

	/* Takes messages until the one waited for arrives or the time is spent. */
	waited = 0U;
	while (waited <= timeout_us) {
		/* Waits a step when nothing is there. */
		error = nvrtx_channel_peek(&gpu->channel, &got_function, &got_sequence, length, &result);
		if (error == EAGAIN) {
			kern_usleep_range(RPC_STEP_US, RPC_STEP_US * 2U);
			waited += RPC_STEP_US;
			continue;
		}

		if (error != 0)
			return error;

		/* Takes the message. */
		error = nvrtx_channel_receive(&gpu->channel, gpu->reply, NVRTX_STAGING_BYTES, length);
		if (error != 0)
			return error;

		/* The message waited for ends the wait. */
		if (got_function == function && (sequence == 0U || got_sequence == sequence)) {
			if (result != 0U) {
				nvrtx_stage_mark("rpc %u: result %08x", (unsigned)function, (unsigned)result);
				return EIO;
			}

			return 0;
		}

		/* The CPU sequencer is performed at once. */
		if (got_function == NVRTX_EVENT_SEQUENCER) {
			error = rpc_sequencer(gpu, gpu->reply, *length);
			if (error != 0)
				return error;

			continue;
		}

		/* A channel error is reported with its channel, its kind and the faulting address (P6). */
		if (got_function == NVRTX_EVENT_CHANNEL_ERROR && *length >= RPC_ERROR_BYTES) {
			nvrtx_stage_mark("P6 channel error: engine %x chid %u type %u fault %08x%08x kind %u",
			    (unsigned)nvrtx_get32(gpu->reply, RPC_ERROR_ENGINE_AT),
			    (unsigned)nvrtx_get32(gpu->reply, RPC_ERROR_CHANNEL_AT),
			    (unsigned)nvrtx_get32(gpu->reply, RPC_ERROR_TYPE_AT),
			    (unsigned)nvrtx_get32(gpu->reply, RPC_ERROR_FAULT_HIGH_AT),
			    (unsigned)nvrtx_get32(gpu->reply, RPC_ERROR_FAULT_LOW_AT),
			    (unsigned)nvrtx_get32(gpu->reply, RPC_ERROR_FAULT_KIND_AT));
			continue;
		}

		/* The firmware's own prints are dropped; other events are reported and dropped. */
		if (got_function != NVRTX_EVENT_LIBOS_PRINT)
			nvrtx_stage_mark("rpc: message %x (%zu bytes) while waiting for %x", (unsigned)got_function, *length, (unsigned)function);
	}

	/* The message never came. */
	return ETIMEDOUT;
}

/*
 * Allocates one object under the driver's client and checks the status the firmware returns.
 */
int
nvrtx_rm_allocate(
	struct nvrtx_gpu *gpu,
	uint32_t parent,
	uint32_t handle,
	uint32_t class_number,
	const uint8_t *parameters,
	size_t length)
{
	struct nvrtx_object object;
	size_t call_length;
	size_t reply_length;
	uint32_t status;
	int error;

	/* Builds the call. */
	object.client = NVRTX_HANDLE_CLIENT;
	object.parent = parent;
	object.handle = handle;
	object.class_number = class_number;
	call_length = nvrtx_alloc_call(gpu->call, NVRTX_CALL_PAYLOAD_MAX, &object, parameters, length);
	if (call_length == 0U)
		return E2BIG;

	/* Sends it and waits for the reply. */
	error = rpc_exchange(gpu, NVRTX_CALL_ALLOCATE, call_length, &reply_length);
	if (error != 0)
		return error;

	/* The reply's status is the resource manager's verdict. */
	if (reply_length < NVRTX_ALLOC_HEADER_BYTES)
		return EIO;
	status = nvrtx_get32(gpu->reply, NVRTX_ALLOC_STATUS_AT);
	if (status != 0U) {
		nvrtx_stage_mark("rm: class %04x object %08x status %08x", (unsigned)class_number, (unsigned)handle, (unsigned)status);
		return EIO;
	}

	/* Succeeded: the object exists. */
	return 0;
}

/*
 * Sends a control to an object and checks the status the firmware returns.
 *
 * The reply's parameters are left at *reply (inside gpu->reply) with their length.
 */
int
nvrtx_rm_control(
	struct nvrtx_gpu *gpu,
	uint32_t client,
	uint32_t object,
	uint32_t command,
	const uint8_t *parameters,
	size_t length,
	const uint8_t **reply,
	size_t *reply_length)
{
	size_t call_length;
	size_t got;
	uint32_t status;
	int error;

	/* Builds the call. */
	call_length = nvrtx_control_call(gpu->call, NVRTX_CALL_PAYLOAD_MAX, client, object, command, parameters, length);
	if (call_length == 0U)
		return E2BIG;

	/* Sends it and waits for the reply. */
	error = rpc_exchange(gpu, NVRTX_CALL_CONTROL, call_length, &got);
	if (error != 0)
		return error;

	/* The reply's status is the resource manager's verdict. */
	if (got < NVRTX_CONTROL_HEADER_BYTES)
		return EIO;
	status = nvrtx_get32(gpu->reply, NVRTX_CONTROL_STATUS_AT);
	if (status != 0U) {
		nvrtx_stage_mark("rm: control %08x status %08x", (unsigned)command, (unsigned)status);
		return EIO;
	}

	/* Succeeded: the reply's parameters follow its header. */
	*reply = gpu->reply + NVRTX_CONTROL_HEADER_BYTES;
	*reply_length = got - NVRTX_CONTROL_HEADER_BYTES;
	return 0;
}

/*
 * Frees one object of the driver's client.
 */
int
nvrtx_rm_free(
	struct nvrtx_gpu *gpu,
	uint32_t object)
{
	size_t reply_length;
	int error;

	/* Builds the call and waits for its reply. */
	nvrtx_free_params(gpu->call, NVRTX_HANDLE_CLIENT, object);
	error = rpc_exchange(gpu, NVRTX_CALL_FREE, NVRTX_FREE_PARAMS_BYTES, &reply_length);
	if (error != 0)
		return error;

	/* Succeeded: the object is gone. */
	return 0;
}

/* Sends the call built in gpu->call and waits for its numbered reply. */
static int
rpc_exchange(
	struct nvrtx_gpu *gpu,
	uint32_t function,
	size_t call_length,
	size_t *reply_length)
{
	uint32_t sequence;
	int error;

	/* Sends the call with the next sequence number. */
	error = nvrtx_rpc_send(gpu, function, gpu->call, call_length, 1, &sequence);
	if (error != 0)
		return error;

	/* Waits for the reply with the same number. */
	error = nvrtx_rpc_wait(gpu, function, sequence, NVRTX_CALL_TIMEOUT_US, reply_length);
	if (error != 0)
		return error;

	/* Succeeded: the reply is in gpu->reply. */
	return 0;
}

/* Performs the register operations of one CPU sequencer message, in order. */
static int
rpc_sequencer(
	struct nvrtx_gpu *gpu,
	uint8_t *payload,
	size_t length)
{
	struct nvrtx_seq_command command;
	uint32_t cursor;
	uint32_t current;
	uint32_t timeout;
	int error;

	/* Decodes and performs each command until the list is done. */
	cursor = 0U;
	for (;;) {
		error = nvrtx_sequencer_next(payload, length, &cursor, &command);
		if (error == ENOENT)
			break;
		if (error != 0) {
			nvrtx_stage_mark("sequencer: bad command at %u", (unsigned)cursor);
			return error;
		}

		/* Performs the command. */
		switch (command.opcode) {
		case NVRTX_SEQ_WRITE:
			nvrtx_wr32(gpu, command.argument[0], command.argument[1]);
			break;
		case NVRTX_SEQ_MODIFY:
			current = nvrtx_rd32(gpu, command.argument[0]);
			nvrtx_wr32(gpu, command.argument[0], (current & ~command.argument[1]) | command.argument[2]);
			break;
		case NVRTX_SEQ_POLL:
			timeout = command.argument[3];
			if (timeout == 0U)
				timeout = RPC_POLL_DEFAULT_US;
			(void)nvrtx_poll(gpu, command.argument[0], command.argument[1], command.argument[2], timeout);
			break;
		case NVRTX_SEQ_DELAY:
			kern_usleep_range(command.argument[0], command.argument[0] + 1U);
			break;
		case NVRTX_SEQ_STORE:
			current = nvrtx_rd32(gpu, command.argument[0]);
			error = nvrtx_sequencer_save(payload, length, command.argument[1], current);
			if (error != 0)
				return error;
			break;
		case NVRTX_SEQ_CORE_RESET:
			error = nvrtx_falcon_reset(gpu, &gpu->gsp_falcon);
			if (error != 0)
				return error;
			nvrtx_falcon_load_physical(gpu, &gpu->gsp_falcon);
			break;
		case NVRTX_SEQ_CORE_START:
			current = nvrtx_rd32(gpu, RPC_GSP_CPU_CONTROL);
			if ((current & RPC_CPU_ALIAS_ENABLED) != 0U) {
				nvrtx_wr32(gpu, RPC_GSP_CPU_ALIAS, RPC_CPU_START);
			} else {
				nvrtx_wr32(gpu, RPC_GSP_CPU_CONTROL, RPC_CPU_START);
			}
			break;
		case NVRTX_SEQ_CORE_WAIT_HALT:
			(void)nvrtx_poll(gpu, RPC_GSP_CPU_CONTROL, RPC_CPU_HALTED, RPC_CPU_HALTED, RPC_HALT_TIMEOUT_US);
			break;
		default:
			/* The resume after a suspend is not in this revision. */
			nvrtx_stage_mark("sequencer: opcode %u not supported", (unsigned)command.opcode);
			return ENOTSUP;
		}
	}

	/* Succeeded: every command was performed. */
	return 0;
}
