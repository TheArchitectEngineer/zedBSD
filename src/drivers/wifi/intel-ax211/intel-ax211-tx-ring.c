/* -*- mode: c; c-file-style: "linux"; tab-width: 8; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Intel AX211 private Gen3 data transmit ring.
 *
 * Sources of the values.  The register offsets, bit positions, command
 * numbers, firmware file record types and wire layouts in this file are facts
 * of the AX211 hardware and of its firmware interface.  Intel publishes no
 * register or host-command specification for this device, so the values were
 * collected with Intel's own driver headers as the primary source: Linux
 * iwlwifi at tag v6.17, under drivers/net/wireless/intel/iwlwifi/, in
 * iwl-fh.h, fw/api/datapath.h and fw/api/tx.h.  They were cross-checked
 * against OpenBSD sys/dev/pci/if_iwxreg.h and if_iwx.c at commit
 * 0f464d413c50.  Each block of values below names the header it was taken
 * from.
 *
 * This file is not an adaptation of those headers.  It collects the interface
 * values, gives them names of its own, and adds what the zedBSD driver
 * established about each of them while it was brought up on AX211 hardware:
 * where the value is used, what it means to the device or to the firmware,
 * the order the device requires, and how the firmware behaved.  It is written
 * as a reference that a developer of an AX211 driver for another operating
 * system can use on its own.
 */

#include "intel-ax211-tx-ring.h"
#include <kern/kcrt.h>

/*
 * The field values of the queue setup command and of a TFD.
 *
 * Source: Linux iwlwifi v6.17 fw/api/datapath.h (queue operation 0 adds a
 * queue; size code 5 announces 256 descriptors, log2 of the count minus 3)
 * and iwl-fh.h (a TFD starts with the buffer count in its low 5 bits, then
 * one 10-byte entry per buffer from byte 2: a 16-bit length and a 64-bit
 * address; a byte-count table entry holds the length in its low 14 bits).
 * The narrow command header of the transmit command is 4 bytes.
 */
#define AX211_TX_QUEUE_OPERATION_ADD 0U
#define AX211_TX_QUEUE_CB_SIZE 5U
#define AX211_TX_TFD_NUM_TBS_MASK 0x1fU
#define AX211_TX_TFD_TB_OFFSET 2U
#define AX211_TX_TFD_TB_SIZE 10U
#define AX211_TX_BC_LENGTH_MASK 0x3fffU
#define AX211_TX_NARROW_HEADER_SIZE 4U

static int ax211_tx_ring_buffer_allocate(struct intel_ax211_frame_ring *ring, struct drv_dma_buffer *buffer, size_t size, size_t alignment);
static int ax211_tx_ring_buffer_valid(const struct drv_dma_buffer *buffer, size_t size, uint64_t alignment);
static void ax211_tx_ring_scrub(void *memory, size_t length);
static void ax211_tx_ring_allocations_release(struct intel_ax211_frame_ring *ring);
static void ax211_tx_ring_buffer_release(struct intel_ax211_frame_ring *ring, struct drv_dma_buffer *buffer);
static int ax211_tx_ring_valid(const struct intel_ax211_frame_ring *ring);
static int ax211_tx_queue_tid_valid(uint8_t tid);
static void ax211_tx_queue_command_encode(uint8_t *command, uint8_t station_id, uint8_t tid, uint64_t byte_count_address, uint64_t tfd_address);
static void ax211_tx_ring_put_le32(uint8_t *bytes, uint32_t value);
static void ax211_tx_ring_put_le64(uint8_t *bytes, uint64_t value);
static int ax211_tx_queue_config_valid(const struct intel_ax211_frame_ring *ring, const struct intel_ax211_tx_queue_config *config);
static int ax211_tx_protocol_result(int result);
static uint16_t ax211_tx_ring_get_le16(const uint8_t *bytes);
static int ax211_tx_ring_cookie_active(const struct intel_ax211_frame_ring *ring, uint64_t cookie);
static int ax211_tx_codec_result(int result);
static int ax211_tx_ring_slot_stage( struct intel_ax211_frame_ring *ring, struct intel_ax211_tx_ring_slot *slot, uint8_t index, const struct intel_ax211_tx_request *request, const struct intel_ax211_tx_prepared *prepared, uint64_t deadline);
static void ax211_tx_ring_put_le16(uint8_t *bytes, uint16_t value);
static void ax211_tx_ring_tfd_tb_encode(uint8_t *tfd, unsigned tb, uint16_t length, uint64_t address);
static void ax211_tx_ring_slot_scrub(struct intel_ax211_frame_ring *ring, uint8_t index);
static int ax211_tx_ring_sync_submission(struct intel_ax211_frame_ring *ring, uint8_t index, size_t command_length, size_t payload_length);
static int ax211_tx_ring_active_sequence(const struct intel_ax211_frame_ring *ring, uint16_t next_sequence);

/*
 * Implements the drv intel ax211 tx ring api89 validate operation.
 */
int
drv_intel_ax211_tx_ring_api89_validate(
	const struct intel_ax211_protocol_command_table *table)
{
	struct intel_ax211_protocol_command_version version;
	int result;

	/* Handles the table availability. */
	if (table == NULL)
		return INTEL_AX211_TX_RING_INVALID;

	/* Checks the operation result. */
	result = drv_intel_ax211_tx_api89_validate(table);
	if (result != INTEL_AX211_TX_OK) {
		return result == INTEL_AX211_TX_INVALID
			       ? INTEL_AX211_TX_RING_INVALID
			       : INTEL_AX211_TX_RING_UNSUPPORTED;
	}

	/* Checks the operation result. */
	result = drv_intel_ax211_protocol_command_version_lookup(
		table, INTEL_AX211_TX_QUEUE_SETUP_FAMILY,
		INTEL_AX211_TX_QUEUE_SETUP_OPCODE, &version);
	if (result != INTEL_AX211_PROTOCOL_OK)
		return INTEL_AX211_TX_RING_UNSUPPORTED;

	/* Handles the version condition. */
	if (version.command_version !=
		    INTEL_AX211_TX_QUEUE_SETUP_COMMAND_VERSION ||
	    version.notification_version !=
		    INTEL_AX211_TX_QUEUE_SETUP_RESPONSE_VERSION) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_RING_UNSUPPORTED;
	}

	/* Returns the computed result. */
	return INTEL_AX211_TX_RING_OK;
}

/*
 * Implements the drv intel ax211 tx ring allocate operation.
 */
int
drv_intel_ax211_tx_ring_allocate(
	struct drv_dma_device *dma_device,
	const struct intel_ax211_tx_ring_ops *ops,
	void *ops_argument,
	struct intel_ax211_frame_ring *ring)
{
	size_t index;
	int result;

	/* Handles the dma device availability. */
	if (dma_device == NULL || ops == NULL || ops->sync_for_device == NULL ||
	    ops->write32 == NULL || ring == NULL || ring->allocated) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_RING_INVALID;
	}

	/* Checks the drv dma device is coherent result. */
	if (!drv_dma_device_is_coherent(dma_device))
		return INTEL_AX211_TX_RING_UNSUPPORTED;
	kern_memset(ring, 0, sizeof(*ring));
	ring->dma_device = dma_device;
	ring->ops = ops;
	ring->ops_argument = ops_argument;

	/* Checks the operation result. */
	result = ax211_tx_ring_buffer_allocate(
		ring, &ring->tfd, INTEL_AX211_TX_RING_TFD_RING_SIZE, 256U);
	if (result == INTEL_AX211_TX_RING_OK) {
		result = ax211_tx_ring_buffer_allocate(
			ring, &ring->byte_count,
			INTEL_AX211_TX_RING_BYTE_COUNT_SIZE, 128U);
	}

	index = 0U;
	/* Process each remaining element. */
	while (result == INTEL_AX211_TX_RING_OK &&
	       index < INTEL_AX211_TX_RING_DESCRIPTOR_SLOTS) {
		/* Checks the operation result. */
		result = ax211_tx_ring_buffer_allocate(
			ring, &ring->slot[index].command,
			INTEL_AX211_TX_RING_COMMAND_DMA_SIZE, 64U);
		if (result == INTEL_AX211_TX_RING_OK) {
			result = ax211_tx_ring_buffer_allocate(
				ring, &ring->slot[index].payload,
				INTEL_AX211_TX_RING_PAYLOAD_DMA_SIZE, 4U);
		}

		index++;
	}

	/* Checks the operation result. */
	if (result != INTEL_AX211_TX_RING_OK) {
		ax211_tx_ring_allocations_release(ring);
		kern_memset(ring, 0, sizeof(*ring));

		/* Failed. */
		return result;
	}

	ring->allocated = 1U;

	/* Returns the computed result. */
	return INTEL_AX211_TX_RING_OK;
}

/*
 * Implements the drv intel ax211 tx ring queue add build operation.
 */
int
drv_intel_ax211_tx_ring_queue_add_build(
	const struct intel_ax211_frame_ring *ring,
	uint8_t station_id,
	uint8_t tid,
	struct intel_ax211_tx_queue_config *config)
{
	struct intel_ax211_tx_queue_config candidate;

	/* Checks the ax211 tx ring valid result. */
	if (!ax211_tx_ring_valid(ring) || config == NULL || ring->enabled ||
	    ring->pending_count != 0U || ring->poisoned || station_id >= 32U ||
	    !ax211_tx_queue_tid_valid(tid)) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_RING_INVALID;
	}
	kern_memset(&candidate, 0, sizeof(candidate));
	candidate.tfd_address = ring->tfd.device_address;
	candidate.byte_count_address = ring->byte_count.device_address;
	candidate.station_id = station_id;
	candidate.tid = tid;
	ax211_tx_queue_command_encode(candidate.command, station_id, tid,
				      candidate.byte_count_address,
				      candidate.tfd_address);
	*config = candidate;
	/* Returns the computed result. */
	return INTEL_AX211_TX_RING_OK;
}

/*
 * Implements the drv intel ax211 tx ring queue add complete operation.
 */
int
drv_intel_ax211_tx_ring_queue_add_complete(
	struct intel_ax211_frame_ring *ring,
	const struct intel_ax211_tx_queue_config *config,
	uint32_t hardware_generation,
	uint64_t connection_generation,
	const struct intel_ax211_protocol_message *message,
	const struct intel_ax211_protocol_pending_command *pending)
{
	int error;
	const uint8_t *payload;
	uint16_t queue;
	uint16_t write_pointer;
	int result;

	/* Checks the ax211 tx ring valid result. */
	if (!ax211_tx_ring_valid(ring) ||
	    !ax211_tx_queue_config_valid(ring, config) ||
	    hardware_generation == 0U || connection_generation == 0U ||
	    message == NULL || pending == NULL || ring->enabled ||
	    ring->pending_count != 0U || ring->poisoned) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_RING_INVALID;
	}

	/* Handles the pending condition. */
	if (pending->group != INTEL_AX211_TX_QUEUE_SETUP_FAMILY ||
	    pending->opcode != INTEL_AX211_TX_QUEUE_SETUP_OPCODE ||
	    pending->response_version !=
		    INTEL_AX211_TX_QUEUE_SETUP_RESPONSE_VERSION ||
	    pending->generation != hardware_generation ||
	    pending->minimum_response_length !=
		    INTEL_AX211_TX_QUEUE_SETUP_RESPONSE_SIZE ||
	    pending->maximum_response_length !=
		    INTEL_AX211_TX_QUEUE_SETUP_RESPONSE_SIZE) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_RING_INVALID;
	}

	/* Checks the operation result. */
	result = drv_intel_ax211_protocol_command_response_validate(message,
								    pending);
	if (result != INTEL_AX211_PROTOCOL_OK) {
		/* Obtains the ax211 tx protocol result result. */
		error = ax211_tx_protocol_result(result);

		/* Failed. */
		return error;
	}

	/* Handles the payload availability. */
	payload = message->payload;
	if (payload == NULL)
		return INTEL_AX211_TX_RING_MALFORMED;
	queue = ax211_tx_ring_get_le16(payload);
	write_pointer = ax211_tx_ring_get_le16(payload + 4U);

	/*
	 * API 89 hardware has been observed returning bit 0 in this field
	 * together with a valid queue number and write pointer.  Linux iwlwifi
	 * and OpenBSD iwx deliberately do not use the response flags as a
	 * completion status.  Match those production drivers so a valid
	 * dynamically assigned queue is not rejected locally.
	 */
	if (queue < INTEL_AX211_TX_QUEUE_MIN ||
	    queue > INTEL_AX211_TX_QUEUE_MAX ||
	    ax211_tx_ring_get_le16(payload + 6U) != 0U) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_RING_MALFORMED;
	}
	ring->hardware_generation = hardware_generation;
	ring->connection_generation = connection_generation;
	ring->queue = queue;
	ring->read_sequence =
		write_pointer & (INTEL_AX211_TX_RING_DESCRIPTOR_SLOTS - 1U);
	ring->write_sequence = ring->read_sequence;
	ring->station_id = config->station_id;
	ring->tid = config->tid;
	ring->enabled = 1U;
	ring->has_last_completion = 0U;

	/* Returns the computed result. */
	return INTEL_AX211_TX_RING_OK;
}

/*
 * Implements the drv intel ax211 tx ring submit operation.
 */
int
drv_intel_ax211_tx_ring_submit(
	struct intel_ax211_frame_ring *ring,
	const struct intel_ax211_tx_request *request,
	uint64_t now,
	uint64_t timeout,
	struct intel_ax211_tx_ring_handle *handle)
{
	int error;
	struct intel_ax211_tx_prepared prepared;
	struct intel_ax211_tx_ring_slot *slot;
	uint64_t deadline;
	uint32_t write_pointer;
	uint16_t sequence;
	uint8_t index;
	int result;

	/* Checks the ax211 tx ring valid result. */
	if (!ax211_tx_ring_valid(ring) || request == NULL || handle == NULL ||
	    timeout == 0U || now > UINT64_MAX - timeout) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_RING_INVALID;
	}

	/* Handles the ring condition. */
	if (!ring->enabled)
		return INTEL_AX211_TX_RING_NOT_READY;

	/* Handles the ring condition. */
	if (ring->poisoned)
		return INTEL_AX211_TX_RING_POISONED;

	/* Handles the request condition. */
	if (request->connection_generation != ring->connection_generation)
		return INTEL_AX211_TX_RING_STALE;

	/* Handles the ring condition. */
	if (ring->pending_count >= INTEL_AX211_TX_RING_INFLIGHT_LIMIT)
		return INTEL_AX211_TX_RING_FULL;

	/* Checks the ax211 tx ring cookie active result. */
	if (ax211_tx_ring_cookie_active(ring, request->cookie))
		return INTEL_AX211_TX_RING_DUPLICATE;

	/* Checks the operation result. */
	result = drv_intel_ax211_tx_prepare(request, &prepared);
	if (result != INTEL_AX211_TX_OK) {
		/* Obtains the ax211 tx codec result result. */
		error = ax211_tx_codec_result(result);

		/* Failed. */
		return error;
	}

	deadline = now + timeout;
	sequence = ring->write_sequence;
	index = (uint8_t)sequence;

	/* Handles the slot condition. */
	slot = &ring->slot[index];
	if (slot->active)
		return INTEL_AX211_TX_RING_MALFORMED;

	/* Checks the operation result. */
	result = ax211_tx_ring_slot_stage(ring, slot, index, request, &prepared,
					  deadline);
	if (result != INTEL_AX211_TX_RING_OK) {
		ax211_tx_ring_slot_scrub(ring, index);

		/* Failed. */
		return result;
	}

	/* Checks the operation result. */
	result = ax211_tx_ring_sync_submission(ring, index,
					       AX211_TX_NARROW_HEADER_SIZE +
						       prepared.command_length,
					       prepared.payload_length);
	if (result != INTEL_AX211_TX_RING_OK) {
		ax211_tx_ring_slot_scrub(ring, index);

		/* Failed. */
		return result;
	}

	slot->active = 1U;
	ring->pending_count++;
	ring->write_sequence = (uint16_t)(sequence + 1U);

	/* Checks the write32 result. */
	write_pointer = ((uint32_t)ring->queue << 16) | ring->write_sequence;
	if (ring->ops->write32(ring->ops_argument,
			       INTEL_AX211_TX_RING_WRITE_POINTER_REGISTER,
			       write_pointer) != 0) {
		slot->uncertain = 1U;
		ring->poisoned = 1U;
		ring->reset_barrier_required = 1U;

		/* Returns the computed result. */
		return INTEL_AX211_TX_RING_KICK_FAILED;
	}

	*handle = slot->handle;
	/* Returns the computed result. */
	return INTEL_AX211_TX_RING_OK;
}

/*
 * Implements the drv intel ax211 tx ring complete operation.
 */
int
drv_intel_ax211_tx_ring_complete(
	struct intel_ax211_frame_ring *ring,
	const struct intel_ax211_protocol_message *message,
	struct intel_ax211_tx_ring_retired *retired)
{
	int error;
	struct intel_ax211_tx_completion completion;
	struct intel_ax211_tx_ring_retired candidate;
	struct intel_ax211_tx_ring_slot *slot;
	uint16_t expected_sequence;
	uint8_t expected_index;
	int result;

	/* Checks the ax211 tx ring valid result. */
	if (!ax211_tx_ring_valid(ring) || message == NULL || retired == NULL)
		return INTEL_AX211_TX_RING_INVALID;

	/* Handles the ring condition. */
	if (!ring->enabled)
		return INTEL_AX211_TX_RING_NOT_READY;

	/* Handles the ring condition. */
	if (ring->poisoned)
		return INTEL_AX211_TX_RING_POISONED;

	/* Checks the operation result. */
	result = drv_intel_ax211_tx_completion_decode(
		message, ring->hardware_generation, ring->queue, message->index,
		&completion);
	if (result != INTEL_AX211_TX_OK) {
		/* Obtains the ax211 tx codec result result. */
		error = ax211_tx_codec_result(result);

		/* Failed. */
		return error;
	}

	/* Handles the ring condition. */
	if (ring->pending_count == 0U) {
		/* Handles the ring condition. */
		if (ring->has_last_completion &&
		    completion.scheduler_sequence ==
			    ring->last_completion_sequence &&
		    message->index ==
			    (uint8_t)(ring->last_completion_sequence - 1U)) {
			/* Returns the computed result. */
			return INTEL_AX211_TX_RING_DUPLICATE;
		}

		/* Returns the computed result. */
		return INTEL_AX211_TX_RING_STALE;
	}

	/* Handles the message condition. */
	expected_index = (uint8_t)ring->read_sequence;
	if (message->index != expected_index) {
		/* Handles the ring condition. */
		if (ring->slot[message->index].active)
			return INTEL_AX211_TX_RING_OUT_OF_ORDER;

		/* Handles the ring condition. */
		if (ring->has_last_completion &&
		    completion.scheduler_sequence ==
			    ring->last_completion_sequence &&
		    message->index ==
			    (uint8_t)(ring->last_completion_sequence - 1U)) {
			/* Returns the computed result. */
			return INTEL_AX211_TX_RING_DUPLICATE;
		}

		/* Returns the computed result. */
		return INTEL_AX211_TX_RING_STALE;
	}

	/* Handles the slot condition. */
	slot = &ring->slot[expected_index];
	if (!slot->active ||
	    slot->handle.hardware_generation != ring->hardware_generation ||
	    slot->handle.connection_generation != ring->connection_generation) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_RING_MALFORMED;
	}

	/* Handles the completion condition. */
	expected_sequence = (uint16_t)(slot->handle.scheduler_sequence + 1U);
	if (completion.scheduler_sequence != expected_sequence) {
		/* Handles the ax211 tx ring active sequence condition. */
		if (ax211_tx_ring_active_sequence(
			    ring, (uint16_t)completion.scheduler_sequence)) {
			/* Returns the computed result. */
			return INTEL_AX211_TX_RING_OUT_OF_ORDER;
		}

		/* Handles the ring condition. */
		if (ring->has_last_completion &&
		    completion.scheduler_sequence ==
			    ring->last_completion_sequence) {
			/* Returns the computed result. */
			return INTEL_AX211_TX_RING_DUPLICATE;
		}

		/* Returns the computed result. */
		return INTEL_AX211_TX_RING_STALE;
	}

	/* Handles the completion condition. */
	if (completion.byte_count != slot->frame_length)
		return INTEL_AX211_TX_RING_MALFORMED;
	kern_memset(&candidate, 0, sizeof(candidate));
	candidate.handle = slot->handle;
	candidate.sequence_control = completion.sequence_control;
	candidate.byte_count = completion.byte_count;
	candidate.acknowledged = completion.acknowledged;
	candidate.rts_retries = completion.rts_retries;
	candidate.frame_retries = completion.frame_retries;
	ax211_tx_ring_slot_scrub(ring, expected_index);
	ring->pending_count--;
	ring->read_sequence = expected_sequence;
	ring->last_completion_sequence = expected_sequence;
	ring->has_last_completion = 1U;
	*retired = candidate;
	/* Returns the computed result. */
	return candidate.acknowledged ? INTEL_AX211_TX_RING_OK
				      : INTEL_AX211_TX_RING_TX_FAILED;
}

/*
 * Implements the drv intel ax211 tx ring timeout oldest operation.
 */
int
drv_intel_ax211_tx_ring_timeout_oldest(
	struct intel_ax211_frame_ring *ring,
	uint64_t now,
	struct intel_ax211_tx_ring_handle *handle)
{
	struct intel_ax211_tx_ring_slot *slot;
	uint8_t index;

	/* Checks the ax211 tx ring valid result. */
	if (!ax211_tx_ring_valid(ring) || handle == NULL)
		return INTEL_AX211_TX_RING_INVALID;

	/* Handles the ring condition. */
	if (!ring->enabled)
		return INTEL_AX211_TX_RING_NOT_READY;

	/* Handles the ring condition. */
	if (ring->poisoned)
		return INTEL_AX211_TX_RING_POISONED;

	/* Handles the ring condition. */
	if (ring->pending_count == 0U)
		return INTEL_AX211_TX_RING_NOT_READY;
	index = (uint8_t)ring->read_sequence;

	/* Handles the slot condition. */
	slot = &ring->slot[index];
	if (!slot->active)
		return INTEL_AX211_TX_RING_MALFORMED;
	*handle = slot->handle;
	/* Handles the now condition. */
	if (now < slot->handle.deadline)
		return INTEL_AX211_TX_RING_PENDING;
	slot->uncertain = 1U;
	ring->poisoned = 1U;
	ring->reset_barrier_required = 1U;

	/* Returns the computed result. */
	return INTEL_AX211_TX_RING_TIMEOUT;
}

/*
 * Implements the drv intel ax211 tx ring reset operation.
 */
int
drv_intel_ax211_tx_ring_reset(
	struct intel_ax211_frame_ring *ring,
	int dma_quiesced)
{
	size_t index;

	/* Checks the ax211 tx ring valid result. */
	if (!ax211_tx_ring_valid(ring) ||
	    (dma_quiesced != 0 && dma_quiesced != 1)) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_RING_INVALID;
	}

	/* Handles the ring condition. */
	if ((ring->enabled || ring->pending_count != 0U ||
	     ring->reset_barrier_required) &&
	    !dma_quiesced) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_RING_BARRIER_REQUIRED;
	}
	/* Process each remaining element. */
	for (index = 0U; index < INTEL_AX211_TX_RING_DESCRIPTOR_SLOTS; index++)
		ax211_tx_ring_slot_scrub(ring, (uint8_t)index);
	ax211_tx_ring_scrub(ring->tfd.address, ring->tfd.size);
	ax211_tx_ring_scrub(ring->byte_count.address, ring->byte_count.size);
	ring->connection_generation = 0U;
	ring->hardware_generation = 0U;
	ring->read_sequence = 0U;
	ring->write_sequence = 0U;
	ring->last_completion_sequence = 0U;
	ring->pending_count = 0U;
	ring->queue = 0U;
	ring->station_id = 0U;
	ring->tid = 0U;
	ring->enabled = 0U;
	ring->poisoned = 0U;
	ring->reset_barrier_required = 0U;
	ring->has_last_completion = 0U;

	/* Returns the computed result. */
	return INTEL_AX211_TX_RING_OK;
}

/*
 * Implements the drv intel ax211 tx ring release operation.
 */
int
drv_intel_ax211_tx_ring_release(
	struct intel_ax211_frame_ring *ring,
	int dma_quiesced)
{
	int result;

	/* Checks the ax211 tx ring valid result. */
	if (!ax211_tx_ring_valid(ring) ||
	    (dma_quiesced != 0 && dma_quiesced != 1)) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_RING_INVALID;
	}

	/* Checks the operation result. */
	result = drv_intel_ax211_tx_ring_reset(ring, dma_quiesced);
	if (result != INTEL_AX211_TX_RING_OK)
		return result;
	ax211_tx_ring_allocations_release(ring);
	kern_memset(ring, 0, sizeof(*ring));

	/* Returns the computed result. */
	return INTEL_AX211_TX_RING_OK;
}

/* Supports the ax211 tx ring buffer allocate operation. */
static int
ax211_tx_ring_buffer_allocate(
	struct intel_ax211_frame_ring *ring,
	struct drv_dma_buffer *buffer,
	size_t size,
	size_t alignment)
{
	int error;

	/* Checks the operation status. */
	error = drv_dma_alloc_coherent(ring->dma_device, size, alignment,
				       buffer);
	if (error != 0)
		return INTEL_AX211_TX_RING_NO_MEMORY;

	/* Checks the ax211 tx ring buffer valid result. */
	if (!ax211_tx_ring_buffer_valid(buffer, size, alignment)) {
		/* Handles the address availability. */
		if (buffer->address != NULL && buffer->size != 0U)
			ax211_tx_ring_scrub(buffer->address, buffer->size);
		drv_dma_free_coherent(ring->dma_device, buffer);
		kern_memset(buffer, 0, sizeof(*buffer));

		/* Returns the computed result. */
		return INTEL_AX211_TX_RING_IO_ERROR;
	}

	kern_memset(buffer->address, 0, buffer->size);

	/* Returns the computed result. */
	return INTEL_AX211_TX_RING_OK;
}

/* Supports the ax211 tx ring buffer valid operation. */
static int
ax211_tx_ring_buffer_valid(
	const struct drv_dma_buffer *buffer,
	size_t size,
	uint64_t alignment)
{
	/* Handles the buffer availability. */
	if (buffer == NULL || buffer->address == NULL || buffer->size != size ||
	    buffer->device_address == 0U || alignment == 0U ||
	    (alignment & (alignment - 1U)) != 0U ||
	    (buffer->device_address & (alignment - 1U)) != 0U) {
		/* Succeeded. */
		return 0;
	}

	/* Handles the buffer condition. */
	if (buffer->device_address > UINT64_MAX - (uint64_t)(size - 1U))
		return 0;

	/* Reports operation failure. */
	return 1;
}

/* Supports the ax211 tx ring scrub operation. */
static void
ax211_tx_ring_scrub(
	void *memory,
	size_t length)
{
	volatile uint8_t *bytes;

	/* Handles the memory availability. */
	if (memory == NULL)
		return;
	bytes = memory;
	/* Process each remaining element. */
	while (length != 0U) {
		*bytes++ = 0U;
		length--;
	}
}

/* Supports the ax211 tx ring allocations release operation. */
static void
ax211_tx_ring_allocations_release(
	struct intel_ax211_frame_ring *ring)
{
	size_t index;

	index = INTEL_AX211_TX_RING_DESCRIPTOR_SLOTS;
	/* Process each remaining element. */
	while (index != 0U) {
		index--;
		ax211_tx_ring_buffer_release(ring, &ring->slot[index].payload);
		ax211_tx_ring_buffer_release(ring, &ring->slot[index].command);
	}

	ax211_tx_ring_buffer_release(ring, &ring->byte_count);
	ax211_tx_ring_buffer_release(ring, &ring->tfd);
}

/* Supports the ax211 tx ring buffer release operation. */
static void
ax211_tx_ring_buffer_release(
	struct intel_ax211_frame_ring *ring,
	struct drv_dma_buffer *buffer)
{
	/* Handles the address availability. */
	if (buffer->address != NULL) {
		ax211_tx_ring_scrub(buffer->address, buffer->size);
		drv_dma_free_coherent(ring->dma_device, buffer);
	}

	kern_memset(buffer, 0, sizeof(*buffer));
}

/* Supports the ax211 tx ring valid operation. */
static int
ax211_tx_ring_valid(
	const struct intel_ax211_frame_ring *ring)
{
	size_t index;

	/* Checks the ax211 tx ring buffer valid result. */
	if (ring == NULL || !ring->allocated || ring->dma_device == NULL ||
	    ring->ops == NULL || ring->ops->sync_for_device == NULL ||
	    ring->ops->write32 == NULL ||
	    !ax211_tx_ring_buffer_valid(
		    &ring->tfd, INTEL_AX211_TX_RING_TFD_RING_SIZE, 256U) ||
	    !ax211_tx_ring_buffer_valid(&ring->byte_count,
					INTEL_AX211_TX_RING_BYTE_COUNT_SIZE,
					128U) ||
	    ring->pending_count > INTEL_AX211_TX_RING_INFLIGHT_LIMIT) {
		/* Succeeded. */
		return 0;
	}
	/* Process each remaining element. */
	for (index = 0U; index < INTEL_AX211_TX_RING_DESCRIPTOR_SLOTS; index++) {
		/* Checks the ax211 tx ring buffer valid result. */
		if (!ax211_tx_ring_buffer_valid(
			    &ring->slot[index].command,
			    INTEL_AX211_TX_RING_COMMAND_DMA_SIZE, 64U) ||
		    !ax211_tx_ring_buffer_valid(
			    &ring->slot[index].payload,
			    INTEL_AX211_TX_RING_PAYLOAD_DMA_SIZE, 4U)) {
			/* Succeeded. */
			return 0;
		}
	}

	/* Reports operation failure. */
	return 1;
}

/* Supports the ax211 tx queue tid valid operation. */
static int
ax211_tx_queue_tid_valid(
	uint8_t tid)
{
	/* Returns the computed result. */
	return tid < 8U || tid == INTEL_AX211_TX_RING_MANAGEMENT_TID;
}

/* Supports the ax211 tx queue command encode operation. */
static void
ax211_tx_queue_command_encode(
	uint8_t *command,
	uint8_t station_id,
	uint8_t tid,
	uint64_t byte_count_address,
	uint64_t tfd_address)
{
	kern_memset(command, 0, INTEL_AX211_TX_QUEUE_SETUP_COMMAND_SIZE);
	ax211_tx_ring_put_le32(command, AX211_TX_QUEUE_OPERATION_ADD);
	ax211_tx_ring_put_le32(command + 4U, UINT32_C(1) << station_id);
	command[8U] = tid;
	ax211_tx_ring_put_le32(command + 12U, 0U);
	ax211_tx_ring_put_le32(command + 16U, AX211_TX_QUEUE_CB_SIZE);
	ax211_tx_ring_put_le64(command + 20U, byte_count_address);
	ax211_tx_ring_put_le64(command + 28U, tfd_address);
}

/* Supports the ax211 tx ring put le32 operation. */
static void
ax211_tx_ring_put_le32(
	uint8_t *bytes,
	uint32_t value)
{
	bytes[0U] = (uint8_t)value;
	bytes[1U] = (uint8_t)(value >> 8);
	bytes[2U] = (uint8_t)(value >> 16);
	bytes[3U] = (uint8_t)(value >> 24);
}

/* Supports the ax211 tx ring put le64 operation. */
static void
ax211_tx_ring_put_le64(
	uint8_t *bytes,
	uint64_t value)
{
	ax211_tx_ring_put_le32(bytes, (uint32_t)value);
	ax211_tx_ring_put_le32(bytes + 4U, (uint32_t)(value >> 32));
}

/* Supports the ax211 tx queue config valid operation. */
static int
ax211_tx_queue_config_valid(
	const struct intel_ax211_frame_ring *ring,
	const struct intel_ax211_tx_queue_config *config)
{
	int error;
	uint8_t expected[INTEL_AX211_TX_QUEUE_SETUP_COMMAND_SIZE];

	/* Checks the ax211 tx queue tid valid result. */
	if (config == NULL || config->station_id >= 32U ||
	    !ax211_tx_queue_tid_valid(config->tid) ||
	    config->tfd_address != ring->tfd.device_address ||
	    config->byte_count_address != ring->byte_count.device_address) {
		/* Succeeded. */
		return 0;
	}
	ax211_tx_queue_command_encode(expected, config->station_id, config->tid,
				      config->byte_count_address,
				      config->tfd_address);

	/* Computes the function result. */
	error =
		kern_memcmp(expected, config->command, sizeof(expected)) == 0;

	/* Returns the computed result. */
	return error;
}

/* Supports the ax211 tx protocol result operation. */
static int
ax211_tx_protocol_result(
	int result)
{
	/* Checks the operation result. */
	if (result == INTEL_AX211_PROTOCOL_STALE ||
	    result == INTEL_AX211_PROTOCOL_TOKEN_MISMATCH) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_RING_STALE;
	}

	/* Checks the operation result. */
	if (result == INTEL_AX211_PROTOCOL_UNSUPPORTED)
		return INTEL_AX211_TX_RING_UNSUPPORTED;

	/* Checks the operation status. */
	if (result == INTEL_AX211_PROTOCOL_FAILED)
		return INTEL_AX211_TX_RING_TX_FAILED;

	/* Checks the operation result. */
	if (result == INTEL_AX211_PROTOCOL_INVALID)
		return INTEL_AX211_TX_RING_INVALID;

	/* Returns the computed result. */
	return INTEL_AX211_TX_RING_MALFORMED;
}

/* Supports the ax211 tx ring get le16 operation. */
static uint16_t
ax211_tx_ring_get_le16(
	const uint8_t *bytes)
{
	/* Returns the computed result. */
	return (uint16_t)bytes[0U] | ((uint16_t)bytes[1U] << 8);
}

/* Supports the ax211 tx ring cookie active operation. */
static int
ax211_tx_ring_cookie_active(
	const struct intel_ax211_frame_ring *ring,
	uint64_t cookie)
{
	size_t index;

	/* Process each remaining element. */
	for (index = 0U; index < INTEL_AX211_TX_RING_DESCRIPTOR_SLOTS; index++) {
		/* Handles the ring condition. */
		if (ring->slot[index].active &&
		    ring->slot[index].handle.cookie == cookie) {
			/* Reports operation failure. */
			return 1;
		}
	}

	/* Succeeded. */
	return 0;
}

/* Supports the ax211 tx codec result operation. */
static int
ax211_tx_codec_result(
	int result)
{
	/* Checks the operation result. */
	if (result == INTEL_AX211_TX_STALE)
		return INTEL_AX211_TX_RING_STALE;

	/* Checks the operation result. */
	if (result == INTEL_AX211_TX_UNSUPPORTED)
		return INTEL_AX211_TX_RING_UNSUPPORTED;

	/* Checks the operation status. */
	if (result == INTEL_AX211_TX_FAILED)
		return INTEL_AX211_TX_RING_TX_FAILED;

	/* Checks the operation result. */
	if (result == INTEL_AX211_TX_INVALID)
		return INTEL_AX211_TX_RING_INVALID;

	/* Returns the computed result. */
	return INTEL_AX211_TX_RING_MALFORMED;
}

/* Supports the ax211 tx ring slot stage operation. */
static int
ax211_tx_ring_slot_stage(
	struct intel_ax211_frame_ring *ring,
	struct intel_ax211_tx_ring_slot *slot,
	uint8_t index,
	const struct intel_ax211_tx_request *request,
	const struct intel_ax211_tx_prepared *prepared,
	uint64_t deadline)
{
	uint8_t *command;
	uint8_t *payload;
	uint8_t *tfd;
	uint8_t *byte_count;
	size_t command_length;
	unsigned num_tbs;

	/* Handles the command length condition. */
	command_length = AX211_TX_NARROW_HEADER_SIZE + prepared->command_length;
	if (command_length <= INTEL_AX211_TX_RING_HEADER_COPY_SIZE ||
	    command_length > slot->command.size ||
	    command_length - INTEL_AX211_TX_RING_HEADER_COPY_SIZE >
		    INTEL_AX211_TX_RING_TB_SIZE_MAX ||
	    prepared->payload_length > slot->payload.size ||
	    prepared->payload_length > INTEL_AX211_TX_RING_TB_SIZE_MAX ||
	    prepared->payload_offset > request->length ||
	    prepared->payload_length >
		    request->length - prepared->payload_offset) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_RING_MALFORMED;
	}
	command = slot->command.address;
	payload = slot->payload.address;
	tfd = (uint8_t *)ring->tfd.address +
	      (size_t)index * INTEL_AX211_TX_RING_TFD_SIZE;
	byte_count = (uint8_t *)ring->byte_count.address +
		     (size_t)index * sizeof(uint16_t);
	kern_memset(command, 0, slot->command.size);
	kern_memset(payload, 0, slot->payload.size);
	kern_memset(tfd, 0, INTEL_AX211_TX_RING_TFD_SIZE);
	command[0U] = INTEL_AX211_TX_OPCODE;
	command[1U] = 0U;
	command[2U] = index;
	command[3U] = (uint8_t)(ring->queue & 0x1fU);
	kern_memcpy(command + AX211_TX_NARROW_HEADER_SIZE, prepared->command,
	       prepared->command_length);

	/* Handles the prepared condition. */
	if (prepared->payload_length != 0U) {
		kern_memcpy(payload, request->frame + prepared->payload_offset,
		       prepared->payload_length);
	}

	num_tbs = prepared->payload_length == 0U ? 2U : 3U;
	ax211_tx_ring_put_le16(tfd,
			       (uint16_t)(num_tbs & AX211_TX_TFD_NUM_TBS_MASK));
	ax211_tx_ring_tfd_tb_encode(tfd, 0U, INTEL_AX211_TX_RING_HEADER_COPY_SIZE,
				    slot->command.device_address);
	ax211_tx_ring_tfd_tb_encode(
		tfd, 1U,
		(uint16_t)(command_length - INTEL_AX211_TX_RING_HEADER_COPY_SIZE),
		slot->command.device_address +
			INTEL_AX211_TX_RING_HEADER_COPY_SIZE);

	/* Handles the prepared condition. */
	if (prepared->payload_length != 0U) {
		ax211_tx_ring_tfd_tb_encode(tfd, 2U,
					    (uint16_t)prepared->payload_length,
					    slot->payload.device_address);
	}

	/* Handles the prepared condition. */
	if (prepared->frame_length > AX211_TX_BC_LENGTH_MASK)
		return INTEL_AX211_TX_RING_MALFORMED;
	ax211_tx_ring_put_le16(byte_count, prepared->frame_length);
	kern_memset(&slot->handle, 0, sizeof(slot->handle));
	slot->handle.connection_generation = prepared->connection_generation;
	slot->handle.cookie = prepared->cookie;
	slot->handle.key_generation = prepared->key_generation;
	slot->handle.packet_number = prepared->packet_number;
	slot->handle.deadline = deadline;
	slot->handle.hardware_generation = ring->hardware_generation;
	slot->handle.scheduler_sequence = ring->write_sequence;
	slot->handle.index = index;
	slot->frame_length = prepared->frame_length;
	slot->uncertain = 0U;

	/* Returns the computed result. */
	return INTEL_AX211_TX_RING_OK;
}

/* Supports the ax211 tx ring put le16 operation. */
static void
ax211_tx_ring_put_le16(
	uint8_t *bytes,
	uint16_t value)
{
	bytes[0U] = (uint8_t)value;
	bytes[1U] = (uint8_t)(value >> 8);
}

/* Supports the ax211 tx ring tfd tb encode operation. */
static void
ax211_tx_ring_tfd_tb_encode(
	uint8_t *tfd,
	unsigned tb,
	uint16_t length,
	uint64_t address)
{
	size_t offset;

	offset = AX211_TX_TFD_TB_OFFSET + (size_t)tb * AX211_TX_TFD_TB_SIZE;
	ax211_tx_ring_put_le16(tfd + offset, length);
	ax211_tx_ring_put_le64(tfd + offset + 2U, address);
}

/* Supports the ax211 tx ring slot scrub operation. */
static void
ax211_tx_ring_slot_scrub(
	struct intel_ax211_frame_ring *ring,
	uint8_t index)
{
	struct intel_ax211_tx_ring_slot *slot;
	uint8_t *tfd;
	uint8_t *byte_count;

	/* Locates every buffer this slot owns. */
	slot = &ring->slot[index];
	tfd = (uint8_t *)ring->tfd.address +
	      (size_t)index * INTEL_AX211_TX_RING_TFD_SIZE;
	byte_count = (uint8_t *)ring->byte_count.address +
		     (size_t)index * sizeof(uint16_t);

	/* Erases them and returns the slot to the free state. */
	ax211_tx_ring_scrub(slot->command.address, slot->command.size);
	ax211_tx_ring_scrub(slot->payload.address, slot->payload.size);
	ax211_tx_ring_scrub(tfd, INTEL_AX211_TX_RING_TFD_SIZE);
	ax211_tx_ring_scrub(byte_count, sizeof(uint16_t));
	kern_memset(&slot->handle, 0, sizeof(slot->handle));
	slot->frame_length = 0U;
	slot->active = 0U;
	slot->uncertain = 0U;
}

/* Supports the ax211 tx ring sync submission operation. */
static int
ax211_tx_ring_sync_submission(
	struct intel_ax211_frame_ring *ring,
	uint8_t index,
	size_t command_length,
	size_t payload_length)
{
	struct intel_ax211_tx_ring_slot *slot;
	size_t tfd_offset;
	size_t byte_count_offset;

	slot = &ring->slot[index];
	tfd_offset = (size_t)index * INTEL_AX211_TX_RING_TFD_SIZE;
	byte_count_offset = (size_t)index * sizeof(uint16_t);

	/* Checks the sync for device result. */
	if (ring->ops->sync_for_device(ring->ops_argument, &slot->command, 0U,
				       command_length) != 0) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_RING_IO_ERROR;
	}

	/* Checks the sync for device result. */
	if (payload_length != 0U &&
	    ring->ops->sync_for_device(ring->ops_argument, &slot->payload, 0U,
				       payload_length) != 0) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_RING_IO_ERROR;
	}

	/* Checks the sync for device result. */
	if (ring->ops->sync_for_device(ring->ops_argument, &ring->tfd,
				       tfd_offset,
				       INTEL_AX211_TX_RING_TFD_SIZE) != 0) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_RING_IO_ERROR;
	}

	/* Checks the sync for device result. */
	if (ring->ops->sync_for_device(ring->ops_argument, &ring->byte_count,
				       byte_count_offset,
				       sizeof(uint16_t)) != 0) {
		/* Returns the computed result. */
		return INTEL_AX211_TX_RING_IO_ERROR;
	}

	/* Returns the computed result. */
	return INTEL_AX211_TX_RING_OK;
}

/* Supports the ax211 tx ring active sequence operation. */
static int
ax211_tx_ring_active_sequence(
	const struct intel_ax211_frame_ring *ring,
	uint16_t next_sequence)
{
	const struct intel_ax211_tx_ring_slot *slot;
	uint16_t sequence;
	uint16_t count;

	sequence = ring->read_sequence;
	count = 0U;
	/* Process each remaining element. */
	while (count < ring->pending_count) {
		/* Handles the slot condition. */
		slot = &ring->slot[(uint8_t)sequence];
		if (slot->active && (uint16_t)(slot->handle.scheduler_sequence +
					       1U) == next_sequence) {
			/* Reports operation failure. */
			return 1;
		}
		sequence = (uint16_t)(sequence + 1U);
		count++;
	}

	/* Succeeded. */
	return 0;
}
