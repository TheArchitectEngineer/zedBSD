/* -*- mode: c; c-file-style: "linux"; tab-width: 8; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Intel AX211 private Gen3 transport implementation.
 *
 * Sources of the values.  The register offsets, bit positions, command
 * numbers, firmware file record types and wire layouts in this file are facts
 * of the AX211 hardware and of its firmware interface.  Intel publishes no
 * register or host-command specification for this device, so the values were
 * collected with Intel's own driver headers as the primary source: Linux
 * iwlwifi at tag v6.17, under drivers/net/wireless/intel/iwlwifi/, in
 * iwl-csr.h, iwl-fh.h, iwl-prph.h and fw/api/cmdhdr.h.  They were
 * cross-checked against OpenBSD sys/dev/pci/if_iwxreg.h and if_iwx.c at
 * commit 0f464d413c50.  Each block of values below names the header it was
 * taken from.
 *
 * This file is not an adaptation of those headers.  It collects the interface
 * values, gives them names of its own, and adds what the zedBSD driver
 * established about each of them while it was brought up on AX211 hardware:
 * where the value is used, what it means to the device or to the firmware,
 * the order the device requires, and how the firmware behaved.  It is written
 * as a reference that a developer of an AX211 driver for another operating
 * system can use on its own.
 */

#include "intel-ax211-transport.h"
#include <kern/kcrt.h>

#include <stddef.h>
#include <stdint.h>

/*
 * The sizes of the DMA areas the transport checks, and the command ring's
 * identity.
 *
 * Sources: Linux iwlwifi v6.17 iwl-fh.h (descriptor and byte-count table
 * sizes) and fw/api/cmdhdr.h (command families).  The command ring is ring 0
 * of the device; its 256 TFDs of 256 bytes fill 64 KiB, and its byte-count
 * table has 1024 16-bit entries (2 KiB) of which the first 256 are used.
 * Each of the 256 command slots is 324 bytes (82944 bytes in all), and a
 * command too large for its slot goes through one 4 KiB external buffer.
 * The receive transfer ring holds 512 descriptors of 16 bytes and the
 * completion ring 512 entries of 32 bytes; the device's completion index is
 * one 16-bit word.  The first transfer buffer of a command is always 20
 * bytes: the device copies it into its own memory before it reads the
 * rest.  The command families are the "group" byte of the wide header: 0 is
 * the classic opcode space, and 1 is the same opcode space addressed through
 * the wide header.
 */
#define AX211_COMMAND_TFD_SIZE 65536U
#define AX211_COMMAND_BYTE_COUNT_SIZE 2048U
#define AX211_COMMAND_SLOTS_SIZE 82944U
#define AX211_COMMAND_EXTERNAL_SIZE 4096U
#define AX211_RX_TRANSFER_SIZE 8192U
#define AX211_RX_COMPLETION_SIZE 16384U
#define AX211_RX_STATUS_SIZE 2U
#define AX211_COMMAND_RING_ID 0U
#define AX211_COMMAND_FIRST_TRANSFER_SIZE 20U
#define AX211_COMMAND_FAMILY_CLASSIC 0U
#define AX211_COMMAND_FAMILY_CLASSIC_WIDE 1U

/*
 * The host-visible registers of the interrupt, doorbell and MSI-X logic.
 *
 * Source: Linux iwlwifi v6.17 iwl-csr.h, and iwl-fh.h for the receive free
 * index.  What the transport does with each:
 * - INTERRUPT_DELAY (0x004) is a byte written once, at ring setup, with the
 *   default 0x40; Linux documents the unit as 32 us, so the device delays an
 *   interrupt by up to 2048 us to coalesce causes.
 * - INTERRUPT_CAUSE (0x008) is the legacy cause register.  The driver never
 *   uses legacy interrupts, but writes all ones to it to clear causes left
 *   from an earlier owner before the firmware is loaded.
 * - FIRMWARE_FLAGS_CLEAR (0x05c) clears the firmware's handshake flags; the
 *   driver clears the radio-off flag (bit 1) and the commands-held flag
 *   (bit 2), each with its own write, before it lets the firmware run.
 * - WRITE_SHADOWING (0x0a8) controls the device's shadow copies of its
 *   doorbell registers, which take host writes while the MAC sleeps; the
 *   driver sets bits 31 and 19:0 at ring setup, the value OpenBSD writes.
 * - QUEUE_WRITE_INDEX (0x460) is the transmit doorbell: the ring number from
 *   bit 16 up and the new write index in bits 15:0.
 * - RX_FREE_INDEX (0x1c80) is the receive doorbell: the index up to which
 *   the device may fill receive buffers.  The activate-rx step writes 8
 *   after the firmware reported ALIVE; replenish then writes the
 *   index of the last returned buffer, rounded down to a multiple of eight,
 *   and only when it changed.
 * - The MSI-X block at 0x2800: QUEUE_CAUSES and EVENT_CAUSES hold the
 *   pending causes of the two cause registers and are cleared by writing
 *   back what was read; QUEUE_MASK and EVENT_MASK disable a cause when its
 *   bit is set; SELF_MASK_STATE (0x2810) re-enables a vector the device
 *   masked by itself when it raised it; RX_VECTOR_MAP (0x2880) and
 *   CAUSE_VECTOR_MAP (0x2890) are byte arrays that name the vector of each
 *   cause.
 */
#define AX211_CSR_INTERRUPT_DELAY 0x004U
#define AX211_CSR_INTERRUPT_CAUSE 0x008U
#define AX211_CSR_FIRMWARE_FLAGS_CLEAR 0x05cU
#define AX211_CSR_WRITE_SHADOWING 0x0a8U
#define AX211_CSR_QUEUE_WRITE_INDEX 0x460U
#define AX211_CSR_RX_FREE_INDEX 0x1c80U
#define AX211_CSR_MSIX_QUEUE_CAUSES 0x2800U
#define AX211_CSR_MSIX_QUEUE_MASK 0x2804U
#define AX211_CSR_MSIX_EVENT_CAUSES 0x2808U
#define AX211_CSR_MSIX_EVENT_MASK 0x280cU
#define AX211_CSR_MSIX_SELF_MASK_STATE 0x2810U
#define AX211_CSR_MSIX_RX_VECTOR_MAP 0x2880U
#define AX211_CSR_MSIX_CAUSE_VECTOR_MAP 0x2890U

/*
 * The values written to those registers.
 *
 * Source: Linux iwlwifi v6.17 iwl-csr.h.  A vector map byte holds the vector
 * number in bits 6:0; bit 7 set means the device does not clear the cause
 * by itself when it raises the vector, so the driver sees every cause it
 * acknowledges.  All causes go to vector 0, and SELF_MASK_VECTOR0 is the
 * bit of vector 0 in the self-mask state register.
 */
#define AX211_INTERRUPT_DELAY_DEFAULT 0x40U
#define AX211_WRITE_SHADOWING_ENABLE 0x800fffffU
#define AX211_FIRMWARE_FLAG_RADIO_OFF 0x00000002U
#define AX211_FIRMWARE_FLAG_COMMANDS_HELD 0x00000004U
#define AX211_VECTOR_MAP_MANUAL_CLEAR 0x80U
#define AX211_VECTOR_MAP_TARGET 0U
#define AX211_SELF_MASK_VECTOR0 1U

/*
 * The queue causes: the first of the two MSI-X cause registers.
 *
 * Source: Linux iwlwifi v6.17 iwl-csr.h (enum msix_fh_int_causes).  Bits
 * 15:0 are the receive queues, of which the transport routes queues 0 and 1.
 * Bits 16 and 17 are the two device-to-host DMA channels, bit 19 the
 * host-to-device channel, and bit 21 a DMA engine fault.  QUEUE_CAUSES_ROUTED
 * are the causes the transport unmasks and routes; the fault cause is the
 * one shared with the boot and runtime code through intel-ax211-transport.h.
 */
#define AX211_QUEUE_CAUSE_RX0 0x00000001U
#define AX211_QUEUE_CAUSE_RX1 0x00000002U
#define AX211_QUEUE_CAUSE_TO_HOST0 0x00010000U
#define AX211_QUEUE_CAUSE_TO_HOST1 0x00020000U
#define AX211_QUEUE_CAUSE_TO_DEVICE 0x00080000U
#define AX211_QUEUE_CAUSE_FAULT INTEL_AX211_TRANSPORT_QUEUE_CAUSE_FAULT
#define AX211_QUEUE_CAUSES_ROUTED                                              \
	(AX211_QUEUE_CAUSE_RX0 | AX211_QUEUE_CAUSE_RX1 |                       \
	 AX211_QUEUE_CAUSE_TO_HOST0 | AX211_QUEUE_CAUSE_TO_HOST1 |             \
	 AX211_QUEUE_CAUSE_TO_DEVICE | AX211_QUEUE_CAUSE_FAULT)

/*
 * The event causes: the second of the two MSI-X cause registers.
 *
 * Source: Linux iwlwifi v6.17 iwl-csr.h (enum msix_hw_int_causes),
 * cross-checked with OpenBSD if_iwxreg.h.  Bit 0 is raised when the firmware
 * reports ALIVE, bit 1 is a wake-up, bit 2
 * the end of a reset, bit 6 a thermal stop, bit 7 the radio switch, bit 8 a
 * periodic timer, bit 26 the transmit scheduler, bit 27 the transmit DMA
 * engine and bit 30 a host wake.  Bits 25, 29 and 5 are faults: a firmware
 * fault, a hardware fault, and a second firmware-fault cause (OpenBSD names
 * bit 5 a second software-error cause; Linux v6.17 names it the software
 * error of the later BZ family).  The driver routes all three and treats
 * each as fatal (INTEL_AX211_TRANSPORT_EVENT_FATAL_CAUSES).  While the
 * firmware is being loaded only ALIVE is unmasked; at runtime all of
 * EVENT_CAUSES_ROUTED are.
 */
#define AX211_EVENT_CAUSE_ALIVE INTEL_AX211_TRANSPORT_EVENT_CAUSE_ALIVE
#define AX211_EVENT_CAUSE_WAKE 0x00000002U
#define AX211_EVENT_CAUSE_RESET_FINISHED 0x00000004U
#define AX211_EVENT_CAUSE_FIRMWARE_FAULT_V2                                    \
	INTEL_AX211_TRANSPORT_EVENT_CAUSE_FIRMWARE_FAULT_V2
#define AX211_EVENT_CAUSE_THERMAL_STOP 0x00000040U
#define AX211_EVENT_CAUSE_RADIO_SWITCH 0x00000080U
#define AX211_EVENT_CAUSE_TIMER 0x00000100U
#define AX211_EVENT_CAUSE_FIRMWARE_FAULT                                       \
	INTEL_AX211_TRANSPORT_EVENT_CAUSE_FIRMWARE_FAULT
#define AX211_EVENT_CAUSE_SCHEDULER 0x04000000U
#define AX211_EVENT_CAUSE_TX_DMA 0x08000000U
#define AX211_EVENT_CAUSE_HARDWARE_FAULT                                       \
	INTEL_AX211_TRANSPORT_EVENT_CAUSE_HARDWARE_FAULT
#define AX211_EVENT_CAUSE_HOST_WAKE 0x40000000U
#define AX211_EVENT_CAUSES_ROUTED                                              \
	(AX211_EVENT_CAUSE_ALIVE | AX211_EVENT_CAUSE_WAKE |                    \
	 AX211_EVENT_CAUSE_RESET_FINISHED |                                    \
	 AX211_EVENT_CAUSE_FIRMWARE_FAULT_V2 | AX211_EVENT_CAUSE_THERMAL_STOP | \
	 AX211_EVENT_CAUSE_RADIO_SWITCH | AX211_EVENT_CAUSE_TIMER |            \
	 AX211_EVENT_CAUSE_FIRMWARE_FAULT | AX211_EVENT_CAUSE_SCHEDULER |      \
	 AX211_EVENT_CAUSE_TX_DMA | AX211_EVENT_CAUSE_HARDWARE_FAULT |         \
	 AX211_EVENT_CAUSE_HOST_WAKE)

/*
 * The periphery registers the transport uses.
 *
 * Source: Linux iwlwifi v6.17 iwl-prph.h (the interrupt mode register of the
 * upper CPU and its MSI-X bit) and iwl-fh.h (the receive DMA configuration
 * and status registers of the AX210 family and the idle bit).  All three are
 * reached at the upper-CPU periphery base plus their offset, while the
 * device is held awake.  INTERRUPT_MODE_MSIX is set once, before the vector
 * map is programmed, to select MSI-X signalling.  When the transport is
 * quiesced, writing zero to RX_DMA_CONFIG stops receive DMA, and the
 * transport waits up to 10 ms, polling every 10 us, for the idle bit of
 * RX_DMA_STATUS.
 */
#define AX211_PRPH_INTERRUPT_MODE 0xa05c00U
#define AX211_INTERRUPT_MODE_MSIX 0x02000000U
#define AX211_PRPH_RX_DMA_STATUS 0xa07824U
#define AX211_PRPH_RX_DMA_CONFIG 0xa07880U
#define AX211_RX_DMA_STATUS_IDLE 0x80000000U
#define AX211_RX_IDLE_TIMEOUT_US 10000U
#define AX211_RX_IDLE_POLL_US 10U

static int ax211_ops_valid(const struct intel_ax211_transport_ops *ops);
static int ax211_profile_valid(const struct intel_ax211_mmio_profile *profile);
static int ax211_memory_valid(const struct intel_ax211_transport_ring_memory *memory);
static int ax211_transport_valid(const struct intel_ax211_transport *transport);
static int ax211_mask_all(struct intel_ax211_transport *transport);
static int ax211_csr_write(struct intel_ax211_transport *transport, uint32_t offset, uint32_t value);
static int ax211_configure_msix_routes(struct intel_ax211_transport *transport);
static int ax211_csr_write8(struct intel_ax211_transport *transport, uint32_t offset, uint8_t value);
static void ax211_mask_all_best_effort(struct intel_ax211_transport *transport);
static int ax211_dma_sync(struct intel_ax211_transport *transport, enum intel_ax211_transport_dma_region region, size_t offset, size_t length, enum intel_ax211_transport_dma_direction direction);
static int ax211_csr_read(struct intel_ax211_transport *transport, uint32_t offset, uint32_t *value);
static int ax211_ack_raw(struct intel_ax211_transport *transport, uint32_t *queue_causes, uint32_t *hardware);
static int ax211_rx_descriptor_valid(uint16_t index, uint64_t device_address);
static int ax211_publish_rx_descriptor(struct intel_ax211_transport *transport, uint16_t index, uint64_t device_address);
static void ax211_set_published(struct intel_ax211_transport *transport, uint16_t index, int published);
static int ax211_all_rx_published(const struct intel_ax211_transport *transport);
static uint16_t ax211_get_le16(const uint8_t *bytes);
static int ax211_is_published(const struct intel_ax211_transport *transport, uint16_t index);
static void ax211_command_rollback(struct intel_ax211_transport *transport);
static int ax211_command_external_scrub(struct intel_ax211_transport *transport);
static int ax211_command_external_matches(const struct intel_ax211_transport *transport, const struct intel_ax211_ring_token *token);
static int ax211_poll_rx_idle(struct intel_ax211_transport *transport);

/*
 * Initializes one exact AX211 Gen3 transport without touching hardware.
 */
int
drv_intel_ax211_transport_init(
	struct intel_ax211_transport *transport,
	const struct intel_ax211_transport_ops *ops,
	void *argument,
	const struct intel_ax211_mmio_profile *profile,
	const struct intel_ax211_transport_ring_memory *memory)
{
	int result;

	/* Checks the ax211 ops valid result. */
	if (transport == NULL || !ax211_ops_valid(ops))
		return INTEL_AX211_TRANSPORT_INVALID;

	/* Checks the ax211 profile valid result. */
	if (!ax211_profile_valid(profile) || !ax211_memory_valid(memory))
		return INTEL_AX211_TRANSPORT_INVALID;

	/* Initializes an unpublished and fail-closed transport state. */
	kern_memset(transport, 0, sizeof(*transport));
	transport->ops = ops;
	transport->argument = argument;
	transport->profile = *profile;
	transport->memory = *memory;

	/* Checks the operation result. */
	result = drv_intel_ax211_ring_init(&transport->command_ring,
					   AX211_COMMAND_RING_ID,
					   INTEL_AX211_COMMAND_RING_SIZE);
	if (result != INTEL_AX211_SUCCESS) {
		kern_memset(transport, 0, sizeof(*transport));

		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_INVALID;
	}

	/* Returns the computed result. */
	return INTEL_AX211_TRANSPORT_OK;
}

/*
 * Routes every admitted AX211 cause to one non-auto-clearing MSI-X vector.
 */
int
drv_intel_ax211_transport_configure_msix(
	struct intel_ax211_transport *transport)
{
	int result;
	int unlock_result;

	/* Checks the ax211 transport valid result. */
	if (!ax211_transport_valid(transport))
		return INTEL_AX211_TRANSPORT_INVALID;

	/* Checks the operation status. */
	if (transport->quiesced || transport->failed)
		return INTEL_AX211_TRANSPORT_ORDER;

	/* Handles the transport condition. */
	if (transport->msix_configured)
		return INTEL_AX211_TRANSPORT_ORDER;

	/* Selects MSI-X inside UMAC while NIC ownership is held. */

	/* Checks the operation result. */
	result = transport->ops->wake_hold(transport->argument);
	if (result != 0)
		return INTEL_AX211_TRANSPORT_IO;
	result = transport->ops->prph_write32(
		transport->argument,
		transport->profile.upper_cpu_prph_base + AX211_PRPH_INTERRUPT_MODE,
		AX211_INTERRUPT_MODE_MSIX);

	/* Checks the operation result. */
	unlock_result = transport->ops->wake_release(transport->argument);
	if (result != 0 || unlock_result != 0) {
		transport->failed = 1U;
		transport->quiesced = 1U;

		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_IO;
	}

	/* Keeps every cause masked while the vector map is changed. */

	/* Checks the operation result. */
	result = ax211_mask_all(transport);
	if (result != INTEL_AX211_TRANSPORT_OK) {
		transport->failed = 1U;
		transport->quiesced = 1U;

		/* Failed. */
		return result;
	}

	/* Programs the single-vector routing table. */

	/* Checks the operation result. */
	result = ax211_configure_msix_routes(transport);
	if (result != INTEL_AX211_TRANSPORT_OK) {
		ax211_mask_all_best_effort(transport);
		transport->failed = 1U;
		transport->quiesced = 1U;

		/* Failed. */
		return result;
	}

	transport->msix_configured = 1U;

	/* Returns the computed result. */
	return INTEL_AX211_TRANSPORT_OK;
}

/*
 * Initializes the preallocated command and RX Gen3 ring memory.
 */
int
drv_intel_ax211_transport_initialize_rings(
	struct intel_ax211_transport *transport)
{
	uint32_t shadow;
	int core_result;
	int result;

	/* Checks the ax211 transport valid result. */
	if (!ax211_transport_valid(transport))
		return INTEL_AX211_TRANSPORT_INVALID;

	/* Checks the operation status. */
	if (transport->quiesced || transport->failed ||
	    transport->rings_initialized) {
		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_ORDER;
	}

	/* Clears every host-owned command and completion object. */
	kern_memset(transport->memory.command_tfd, 0,
	       transport->memory.command_tfd_size);
	kern_memset(transport->memory.command_byte_count, 0,
	       transport->memory.command_byte_count_size);
	kern_memset(transport->memory.command_slots, 0,
	       transport->memory.command_slots_size);
	kern_memset(transport->memory.command_external, 0,
	       transport->memory.command_external_size);
	kern_memset(transport->memory.rx_completion, 0,
	       transport->memory.rx_completion_size);
	kern_memset(transport->memory.rx_status, 0,
	       transport->memory.rx_status_size);
	kern_memset(transport->rx_published, 0, sizeof(transport->rx_published));

	/*
	 * Publishes the cleared command objects before any doorbell can ring.
	 */
	result =
		ax211_dma_sync(transport, INTEL_AX211_TRANSPORT_DMA_COMMAND_TFD,
			       0U, transport->memory.command_tfd_size,
			       INTEL_AX211_TRANSPORT_DMA_PREWRITE);

	/* Checks the operation result. */
	if (result == INTEL_AX211_TRANSPORT_OK) {
		result = ax211_dma_sync(
			transport, INTEL_AX211_TRANSPORT_DMA_COMMAND_BYTE_COUNT,
			0U, transport->memory.command_byte_count_size,
			INTEL_AX211_TRANSPORT_DMA_PREWRITE);
	}

	/* Checks the operation result. */
	if (result == INTEL_AX211_TRANSPORT_OK) {
		result = ax211_dma_sync(
			transport, INTEL_AX211_TRANSPORT_DMA_COMMAND_SLOTS, 0U,
			transport->memory.command_slots_size,
			INTEL_AX211_TRANSPORT_DMA_PREWRITE);
	}

	/* Checks the operation result. */
	if (result == INTEL_AX211_TRANSPORT_OK) {
		result = ax211_dma_sync(
			transport, INTEL_AX211_TRANSPORT_DMA_COMMAND_EXTERNAL,
			0U, transport->memory.command_external_size,
			INTEL_AX211_TRANSPORT_DMA_PREWRITE);
	}

	/* Checks the operation result. */
	if (result != INTEL_AX211_TRANSPORT_OK) {
		transport->failed = 1U;
		transport->quiesced = 1U;

		/* Failed. */
		return result;
	}

	/* Arms device-write completion and status memory for DMA. */

	/* Checks the operation result. */
	result = ax211_dma_sync(transport,
				INTEL_AX211_TRANSPORT_DMA_RX_COMPLETION, 0U,
				transport->memory.rx_completion_size,
				INTEL_AX211_TRANSPORT_DMA_PREREAD);
	if (result == INTEL_AX211_TRANSPORT_OK) {
		result = ax211_dma_sync(transport,
					INTEL_AX211_TRANSPORT_DMA_RX_STATUS, 0U,
					transport->memory.rx_status_size,
					INTEL_AX211_TRANSPORT_DMA_PREREAD);
	}

	/* Checks the operation result. */
	if (result != INTEL_AX211_TRANSPORT_OK) {
		transport->failed = 1U;
		transport->quiesced = 1U;

		/* Failed. */
		return result;
	}

	/* Resets host bookkeeping to command queue zero and RX index zero. */

	/* Handles the core result condition. */
	core_result = drv_intel_ax211_ring_init(&transport->command_ring,
						AX211_COMMAND_RING_ID,
						INTEL_AX211_COMMAND_RING_SIZE);
	if (core_result != INTEL_AX211_SUCCESS) {
		transport->failed = 1U;
		transport->quiesced = 1U;

		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_FAILED;
	}

	/* Starts the newly initialized hardware command queue at zero. */
	transport->command_write_sequence = 0U;

	/* Initializes RX consumers and the unpublished credit marker. */
	transport->rx_head = 0U;
	transport->rx_tail = 0U;
	transport->rx_pending = 0U;
	transport->rx_active = 0U;
	transport->rx_last_credit = 0xffffU;

	/* Applies the proven interrupt-coalescing and shadow-register setup. */

	/* Checks the operation result. */
	result = ax211_csr_write8(transport, AX211_CSR_INTERRUPT_DELAY,
				  AX211_INTERRUPT_DELAY_DEFAULT);
	if (result != INTEL_AX211_TRANSPORT_OK) {
		transport->failed = 1U;
		transport->quiesced = 1U;

		/* Failed. */
		return result;
	}

	/* Checks the operation result. */
	result = ax211_csr_read(transport, AX211_CSR_WRITE_SHADOWING,
				&shadow);
	if (result != INTEL_AX211_TRANSPORT_OK) {
		transport->failed = 1U;
		transport->quiesced = 1U;

		/* Failed. */
		return result;
	}

	shadow |= AX211_WRITE_SHADOWING_ENABLE;

	/* Checks the operation result. */
	result = ax211_csr_write(transport, AX211_CSR_WRITE_SHADOWING,
				 shadow);
	if (result != INTEL_AX211_TRANSPORT_OK) {
		transport->failed = 1U;
		transport->quiesced = 1U;

		/* Failed. */
		return result;
	}

	transport->rings_initialized = 1U;
	transport->rx_dma_idle = 0U;

	/* Returns the computed result. */
	return INTEL_AX211_TRANSPORT_OK;
}

/*
 * Clears stale startup state and enables only firmware-load causes.
 */
int
drv_intel_ax211_transport_enable_firmware_interrupts(
	struct intel_ax211_transport *transport)
{
	uint32_t queue_causes;
	uint32_t hardware;
	int result;

	/* Checks the ax211 transport valid result. */
	if (!ax211_transport_valid(transport))
		return INTEL_AX211_TRANSPORT_INVALID;

	/* Checks the operation status. */
	if (!transport->msix_configured || !transport->rings_initialized ||
	    transport->quiesced || transport->failed) {
		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_ORDER;
	}

	/* Clears legacy and MSI-X stale state before clearing handshakes. */

	/* Checks the operation result. */
	result = ax211_csr_write(transport, AX211_CSR_INTERRUPT_CAUSE, UINT32_MAX);
	if (result == INTEL_AX211_TRANSPORT_OK)
		result = ax211_mask_all(transport);
	if (result == INTEL_AX211_TRANSPORT_OK)
		result = ax211_ack_raw(transport, &queue_causes, &hardware);
	if (result != INTEL_AX211_TRANSPORT_OK) {
		ax211_mask_all_best_effort(transport);
		transport->failed = 1U;
		transport->quiesced = 1U;

		/* Failed. */
		return result;
	}

	/* Clears firmware RF-kill and blocked-command handshake bits. */

	/* Checks the operation result. */
	result = ax211_csr_write(transport, AX211_CSR_FIRMWARE_FLAGS_CLEAR,
				 AX211_FIRMWARE_FLAG_RADIO_OFF);
	if (result == INTEL_AX211_TRANSPORT_OK) {
		result = ax211_csr_write(transport, AX211_CSR_FIRMWARE_FLAGS_CLEAR,
					 AX211_FIRMWARE_FLAG_COMMANDS_HELD);
	}

	/* Checks the operation result. */
	if (result == INTEL_AX211_TRANSPORT_OK)
		result = ax211_csr_write(transport, AX211_CSR_INTERRUPT_CAUSE, UINT32_MAX);
	if (result != INTEL_AX211_TRANSPORT_OK) {
		ax211_mask_all_best_effort(transport);
		transport->failed = 1U;
		transport->quiesced = 1U;

		/* Failed. */
		return result;
	}

	/* Re-acknowledges before exposing ALIVE and flow-handler delivery. */

	/* Checks the operation result. */
	result = ax211_ack_raw(transport, &queue_causes, &hardware);
	if (result != INTEL_AX211_TRANSPORT_OK) {
		ax211_mask_all_best_effort(transport);
		transport->failed = 1U;
		transport->quiesced = 1U;

		/* Failed. */
		return result;
	}

	/* Checks the operation result. */
	result = ax211_csr_write(transport, AX211_CSR_MSIX_EVENT_MASK,
				 ~AX211_EVENT_CAUSE_ALIVE);
	if (result == INTEL_AX211_TRANSPORT_OK) {
		result = ax211_csr_write(transport,
					 AX211_CSR_MSIX_QUEUE_MASK,
					 ~AX211_QUEUE_CAUSES_ROUTED);
	}

	/* Checks the operation result. */
	if (result != INTEL_AX211_TRANSPORT_OK) {
		ax211_mask_all_best_effort(transport);
		transport->failed = 1U;
		transport->quiesced = 1U;

		/* Failed. */
		return result;
	}

	transport->enabled_event_causes = AX211_EVENT_CAUSE_ALIVE;
	transport->enabled_queue_causes = AX211_QUEUE_CAUSES_ROUTED;
	transport->interrupts_enabled = 1U;
	transport->firmware_load_mode = 1U;

	/* Returns the computed result. */
	return INTEL_AX211_TRANSPORT_OK;
}

/*
 * Enables the complete proven AX211 runtime cause set.
 */
int
drv_intel_ax211_transport_enable_runtime_interrupts(
	struct intel_ax211_transport *transport)
{
	int result;

	/* Checks the ax211 transport valid result. */
	if (!ax211_transport_valid(transport))
		return INTEL_AX211_TRANSPORT_INVALID;

	/* Checks the operation status. */
	if (!transport->msix_configured || !transport->rings_initialized ||
	    transport->quiesced || transport->failed) {
		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_ORDER;
	}

	/* Publishes both unmasked-cause sets before recording runtime mode. */

	/* Checks the operation result. */
	result = ax211_csr_write(transport, AX211_CSR_MSIX_QUEUE_MASK,
				 ~AX211_QUEUE_CAUSES_ROUTED);
	if (result == INTEL_AX211_TRANSPORT_OK) {
		result = ax211_csr_write(transport,
					 AX211_CSR_MSIX_EVENT_MASK,
					 ~AX211_EVENT_CAUSES_ROUTED);
	}

	/* Checks the operation result. */
	if (result != INTEL_AX211_TRANSPORT_OK) {
		ax211_mask_all_best_effort(transport);
		transport->failed = 1U;
		transport->quiesced = 1U;

		/* Failed. */
		return result;
	}

	transport->enabled_queue_causes = AX211_QUEUE_CAUSES_ROUTED;
	transport->enabled_event_causes = AX211_EVENT_CAUSES_ROUTED;
	transport->interrupts_enabled = 1U;
	transport->firmware_load_mode = 0U;

	/* Returns the computed result. */
	return INTEL_AX211_TRANSPORT_OK;
}

/*
 * Masks every MSI-X cause before later device or DMA shutdown.
 */
int
drv_intel_ax211_transport_disable_interrupts(
	struct intel_ax211_transport *transport)
{
	int result;

	/* Checks the ax211 transport valid result. */
	if (!ax211_transport_valid(transport))
		return INTEL_AX211_TRANSPORT_INVALID;

	/* Handles the transport condition. */
	if (!transport->msix_configured)
		return INTEL_AX211_TRANSPORT_ORDER;

	/* Masks flow-handler causes before hardware causes. */
	result = ax211_mask_all(transport);
	transport->enabled_queue_causes = 0U;
	transport->enabled_event_causes = 0U;
	transport->interrupts_enabled = 0U;
	transport->firmware_load_mode = 0U;
	if (result != INTEL_AX211_TRANSPORT_OK) {
		transport->failed = 1U;

		/* Failed. */
		return result;
	}

	/* Returns the computed result. */
	return INTEL_AX211_TRANSPORT_OK;
}

/*
 * Acknowledges one MSI-X snapshot and returns only currently admitted causes.
 */
int
drv_intel_ax211_transport_interrupt_claim(
	struct intel_ax211_transport *transport,
	struct intel_ax211_transport_causes *causes)
{
	uint32_t queue_causes;
	uint32_t hardware;
	int result;

	/* Checks the ax211 transport valid result. */
	if (!ax211_transport_valid(transport) || causes == NULL)
		return INTEL_AX211_TRANSPORT_INVALID;

	/* Checks the operation status. */
	if (!transport->msix_configured || !transport->interrupts_enabled ||
	    transport->quiesced || transport->failed) {
		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_ORDER;
	}

	/* Captures and W1C-acknowledges both cause banks before dispatch. */

	/* Checks the operation result. */
	result = ax211_ack_raw(transport, &queue_causes, &hardware);
	if (result != INTEL_AX211_TRANSPORT_OK) {
		transport->failed = 1U;

		/* Failed. */
		return result;
	}

	causes->queue_causes = queue_causes & transport->enabled_queue_causes;
	causes->hardware = hardware & transport->enabled_event_causes;
	causes->raw_queue_causes = queue_causes;
	causes->raw_hardware = hardware;

	/* Returns the computed result. */
	return INTEL_AX211_TRANSPORT_OK;
}

/*
 * Rearms the single non-auto-clearing MSI-X vector after dispatch.
 */
int
drv_intel_ax211_transport_interrupt_rearm(
	struct intel_ax211_transport *transport)
{
	int result;

	/* Checks the ax211 transport valid result. */
	if (!ax211_transport_valid(transport))
		return INTEL_AX211_TRANSPORT_INVALID;

	/* Checks the operation status. */
	if (!transport->msix_configured || !transport->interrupts_enabled ||
	    transport->quiesced || transport->failed) {
		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_ORDER;
	}

	/* Checks the operation result. */
	result = ax211_csr_write(transport, AX211_CSR_MSIX_SELF_MASK_STATE,
				 AX211_SELF_MASK_VECTOR0);
	if (result != INTEL_AX211_TRANSPORT_OK) {
		transport->failed = 1U;
		transport->quiesced = 1U;
	}

	/* Returns the computed result. */
	return result;
}

/*
 * Encodes and DMA-publishes one exact Gen3 RX transfer descriptor.
 */
int
drv_intel_ax211_transport_publish_rx_descriptor(
	struct intel_ax211_transport *transport,
	uint16_t index,
	uint64_t device_address)
{
	int error;

	/* Checks the ax211 transport valid result. */
	if (!ax211_transport_valid(transport))
		return INTEL_AX211_TRANSPORT_INVALID;

	/* Checks the operation status. */
	if (!transport->rings_initialized || transport->rx_active ||
	    transport->quiesced || transport->failed) {
		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_ORDER;
	}

	/* Checks the ax211 rx descriptor valid result. */
	if (!ax211_rx_descriptor_valid(index, device_address))
		return INTEL_AX211_TRANSPORT_INVALID;

	/* Obtains the ax211 publish rx descriptor result. */
	error =
		ax211_publish_rx_descriptor(transport, index, device_address);

	/* Returns the computed result. */
	return error;
}

/*
 * Gives firmware its first RX credit after hardware ALIVE configured RFH.
 */
int
drv_intel_ax211_transport_activate_rx(
	struct intel_ax211_transport *transport)
{
	int result;

	/* Checks the ax211 transport valid result. */
	if (!ax211_transport_valid(transport))
		return INTEL_AX211_TRANSPORT_INVALID;

	/* Checks the operation status. */
	if (!transport->rings_initialized || transport->rx_active ||
	    transport->quiesced || transport->failed) {
		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_ORDER;
	}

	/* Checks the ax211 all rx published result. */
	if (!ax211_all_rx_published(transport))
		return INTEL_AX211_TRANSPORT_ORDER;

	/* Republishes the complete ring after firmware has configured RFH. */
	result =
		ax211_dma_sync(transport, INTEL_AX211_TRANSPORT_DMA_RX_TRANSFER,
			       0U, transport->memory.rx_transfer_size,
			       INTEL_AX211_TRANSPORT_DMA_PREWRITE);

	/* Checks the operation result. */
	if (result == INTEL_AX211_TRANSPORT_OK) {
		result = ax211_csr_write(transport,
					 AX211_CSR_RX_FREE_INDEX, 8U);
	}

	/* Checks the operation result. */
	if (result != INTEL_AX211_TRANSPORT_OK) {
		transport->failed = 1U;
		transport->quiesced = 1U;

		/* Failed. */
		return result;
	}

	transport->rx_last_credit = 8U;
	transport->rx_active = 1U;

	/* Returns the computed result. */
	return INTEL_AX211_TRANSPORT_OK;
}

/*
 * Refreshes the bounded Gen3 RX completion head from DMA status memory.
 */
int
drv_intel_ax211_transport_rx_refresh(
	struct intel_ax211_transport *transport)
{
	uint16_t hardware_head;
	int result;

	/* Checks the ax211 transport valid result. */
	if (!ax211_transport_valid(transport))
		return INTEL_AX211_TRANSPORT_INVALID;

	/* Checks the operation status. */
	if (!transport->rx_active || transport->quiesced || transport->failed)
		return INTEL_AX211_TRANSPORT_ORDER;

	/* Acquires the device-written status word before decoding its head. */

	/* Checks the operation result. */
	result = ax211_dma_sync(transport, INTEL_AX211_TRANSPORT_DMA_RX_STATUS,
				0U, AX211_RX_STATUS_SIZE,
				INTEL_AX211_TRANSPORT_DMA_POSTREAD);
	if (result != INTEL_AX211_TRANSPORT_OK) {
		transport->failed = 1U;
		transport->quiesced = 1U;

		/* Failed. */
		return result;
	}

	hardware_head = ax211_get_le16(transport->memory.rx_status);

	/* Checks the operation result. */
	result = ax211_dma_sync(transport, INTEL_AX211_TRANSPORT_DMA_RX_STATUS,
				0U, AX211_RX_STATUS_SIZE,
				INTEL_AX211_TRANSPORT_DMA_PREREAD);
	if (result != INTEL_AX211_TRANSPORT_OK) {
		transport->failed = 1U;
		transport->quiesced = 1U;

		/* Failed. */
		return result;
	}

	hardware_head &= 0x0fffU;
	hardware_head &= INTEL_AX211_TRANSPORT_RX_DESCRIPTOR_COUNT - 1U;
	transport->rx_head = hardware_head;

	/* Returns the computed result. */
	return INTEL_AX211_TRANSPORT_OK;
}

/*
 * Acquires one device-written RX completion without advancing host credit.
 */
int
drv_intel_ax211_transport_rx_next(
	struct intel_ax211_transport *transport,
	struct intel_ax211_transport_rx_completion *completion)
{
	const uint8_t *descriptor;
	size_t offset;
	uint16_t buffer_id;
	uint8_t flags;
	int core_result;
	int result;

	/* Checks the ax211 transport valid result. */
	if (!ax211_transport_valid(transport) || completion == NULL)
		return INTEL_AX211_TRANSPORT_INVALID;

	/* Handles the transport condition. */
	if (transport->rx_pending)
		return INTEL_AX211_TRANSPORT_ORDER;

	/* Refreshes the device head before testing for one available entry. */

	/* Checks the operation result. */
	result = drv_intel_ax211_transport_rx_refresh(transport);
	if (result != INTEL_AX211_TRANSPORT_OK)
		return result;

	/* Handles the transport condition. */
	if (transport->rx_tail == transport->rx_head)
		return INTEL_AX211_TRANSPORT_STALE;

	/* Acquires and decodes exactly one completion descriptor. */
	offset = (size_t)transport->rx_tail *
		 INTEL_AX211_TRANSPORT_RX_COMPLETION_SIZE;

	/* Checks the operation result. */
	result = ax211_dma_sync(transport,
				INTEL_AX211_TRANSPORT_DMA_RX_COMPLETION, offset,
				INTEL_AX211_TRANSPORT_RX_COMPLETION_SIZE,
				INTEL_AX211_TRANSPORT_DMA_POSTREAD);
	if (result != INTEL_AX211_TRANSPORT_OK)
		return result;
	descriptor = transport->memory.rx_completion + offset;

	/* Handles the core result condition. */
	core_result = drv_intel_ax211_rx_completion_descriptor_decode(
		descriptor, &buffer_id, &flags);
	if (core_result != INTEL_AX211_SUCCESS ||
	    buffer_id >= INTEL_AX211_TRANSPORT_RX_DESCRIPTOR_COUNT) {
		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_INVALID;
	}

	/* Checks the ax211 is published result. */
	if (!ax211_is_published(transport, buffer_id))
		return INTEL_AX211_TRANSPORT_STALE;

	/* Transfers that buffer slot to the caller until it is replenished. */
	ax211_set_published(transport, buffer_id, 0);
	transport->rx_pending = 1U;
	transport->rx_pending_index = transport->rx_tail;
	transport->rx_pending_buffer = buffer_id;
	completion->completion_index = transport->rx_tail;
	completion->buffer_id = buffer_id;
	completion->flags = flags;

	/* Returns the computed result. */
	return INTEL_AX211_TRANSPORT_OK;
}

/*
 * Replenishes the pending RX buffer and advances aligned firmware credit.
 */
int
drv_intel_ax211_transport_rx_replenish(
	struct intel_ax211_transport *transport,
	uint64_t device_address)
{
	size_t completion_offset;
	uint16_t credit;
	uint16_t previous;
	int result;

	/* Checks the ax211 transport valid result. */
	if (!ax211_transport_valid(transport))
		return INTEL_AX211_TRANSPORT_INVALID;

	/* Checks the operation status. */
	if (!transport->rx_pending || transport->quiesced || transport->failed)
		return INTEL_AX211_TRANSPORT_ORDER;

	/* Handles the transport condition. */
	if (transport->rx_pending_index != transport->rx_tail)
		return INTEL_AX211_TRANSPORT_STALE;

	/* Checks the ax211 rx descriptor valid result. */
	if (!ax211_rx_descriptor_valid(transport->rx_pending_buffer,
				       device_address)) {
		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_INVALID;
	}

	/* Re-encodes only the buffer owned by the pending completion. */

	/* Checks the operation result. */
	result = ax211_publish_rx_descriptor(
		transport, transport->rx_pending_buffer, device_address);
	if (result != INTEL_AX211_TRANSPORT_OK) {
		transport->failed = 1U;
		transport->quiesced = 1U;

		/* Failed. */
		return result;
	}

	completion_offset = (size_t)transport->rx_pending_index *
			    INTEL_AX211_TRANSPORT_RX_COMPLETION_SIZE;

	/* Checks the operation result. */
	result = ax211_dma_sync(
		transport, INTEL_AX211_TRANSPORT_DMA_RX_COMPLETION,
		completion_offset, INTEL_AX211_TRANSPORT_RX_COMPLETION_SIZE,
		INTEL_AX211_TRANSPORT_DMA_PREREAD);
	if (result != INTEL_AX211_TRANSPORT_OK) {
		transport->failed = 1U;
		transport->quiesced = 1U;

		/* Failed. */
		return result;
	}

	/* Advances the host tail only after both DMA slots are device-owned. */
	previous = transport->rx_tail;
	transport->rx_tail =
		(uint16_t)((transport->rx_tail + 1U) &
			   (INTEL_AX211_TRANSPORT_RX_DESCRIPTOR_COUNT - 1U));
	transport->rx_pending = 0U;
	credit = transport->rx_tail == 0U
			 ? INTEL_AX211_TRANSPORT_RX_DESCRIPTOR_COUNT - 1U
			 : transport->rx_tail - 1U;
	credit &= (uint16_t)~7U;

	/* Handles the credit condition. */
	if (credit == transport->rx_last_credit)
		return INTEL_AX211_TRANSPORT_OK;

	/* Reports only a complete group of eight replenished descriptors. */

	/* Checks the operation result. */
	result = ax211_csr_write(transport, AX211_CSR_RX_FREE_INDEX,
				 credit);
	if (result != INTEL_AX211_TRANSPORT_OK) {
		transport->rx_tail = previous;
		transport->rx_pending = 1U;
		transport->failed = 1U;
		transport->quiesced = 1U;

		/* Failed. */
		return result;
	}

	transport->rx_last_credit = credit;

	/* Returns the computed result. */
	return INTEL_AX211_TRANSPORT_OK;
}

/*
 * Reserves and DMA-publishes one command without ringing its doorbell.
 */
int
drv_intel_ax211_transport_command_prepare_inline(
	struct intel_ax211_transport *transport,
	const struct intel_ax211_command_id *command,
	const void *payload,
	size_t payload_length,
	struct intel_ax211_ring_token *token)
{
	struct intel_ax211_command_id wire_command;
	struct intel_ax211_ring_token reserved;
	struct intel_ax211_tfd_buffer buffer[2];
	uint8_t *slot;
	uint8_t *tfd;
	uint64_t slot_address;
	size_t header_size;
	size_t slot_offset;
	size_t tfd_offset;
	size_t total_length;
	size_t buffer_count;
	int core_result;
	int result;

	/* Checks the ax211 transport valid result. */
	if (!ax211_transport_valid(transport) || command == NULL ||
	    token == NULL) {
		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_INVALID;
	}

	/* Checks the operation status. */
	if (!transport->rings_initialized || !transport->interrupts_enabled ||
	    transport->quiesced || transport->failed ||
	    transport->command_prepared || transport->command_reset_required) {
		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_ORDER;
	}

	/* Handles the payload availability. */
	if ((payload == NULL && payload_length != 0U) ||
	    payload_length > INTEL_AX211_TRANSPORT_COMMAND_INLINE_PAYLOAD_MAX) {
		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_INVALID;
	}

	/* Reserves a stable queue token before selecting its DMA slots. */

	/* Handles the core result condition. */
	core_result = drv_intel_ax211_ring_reserve(&transport->command_ring,
						   &reserved);
	if (core_result == INTEL_AX211_FULL)
		return INTEL_AX211_TRANSPORT_FULL;

	/* Handles the core result condition. */
	if (core_result != INTEL_AX211_SUCCESS)
		return INTEL_AX211_TRANSPORT_INVALID;
	slot_offset = (size_t)reserved.index *
		      INTEL_AX211_TRANSPORT_COMMAND_SLOT_SIZE;
	tfd_offset = (size_t)reserved.index * INTEL_AX211_TRANSFER_DESCRIPTOR_SIZE;
	slot = transport->memory.command_slots + slot_offset;
	tfd = transport->memory.command_tfd + tfd_offset;

	/*
	 * API 89 rejects the original four-byte group-zero command form. Legacy
	 * logical commands are therefore carried by LONG_GROUP on the wire,
	 * just as the pinned OpenBSD implementation does for firmware API
	 * >= 50.
	 */
	kern_memset(slot, 0, INTEL_AX211_TRANSPORT_COMMAND_SLOT_SIZE);
	header_size = INTEL_AX211_WIDE_COMMAND_HEADER_SIZE;

	/* Handles the command condition. */
	if (command->group == AX211_COMMAND_FAMILY_CLASSIC) {
		wire_command = *command;
		wire_command.group = AX211_COMMAND_FAMILY_CLASSIC_WIDE;
		wire_command.version = 0U;
		core_result = drv_intel_ax211_wide_command_encode(
			slot, &wire_command, (uint16_t)payload_length,
			&reserved);
	} else {
		core_result = drv_intel_ax211_wide_command_encode(
			slot, command, (uint16_t)payload_length, &reserved);
	}

	/* Handles the core result condition. */
	if (core_result != INTEL_AX211_SUCCESS) {
		ax211_command_rollback(transport);

		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_INVALID;
	}

	/* Handles the payload length condition. */
	if (payload_length != 0U)
		kern_memcpy(slot + header_size, payload, payload_length);

	/* Encodes one TFD pointing at the stable command slot. */
	if (slot_offset >
	    UINT64_MAX - transport->memory.command_slots_device_address) {
		kern_memset(slot, 0, INTEL_AX211_TRANSPORT_COMMAND_SLOT_SIZE);
		ax211_command_rollback(transport);

		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_INVALID;
	}

	slot_address = transport->memory.command_slots_device_address +
		       (uint64_t)slot_offset;
	total_length = header_size + payload_length;
	buffer[0].address = slot_address;
	buffer[0].length = (uint16_t)total_length;
	buffer_count = 1U;

	/* Handles the total length condition. */
	if (total_length > AX211_COMMAND_FIRST_TRANSFER_SIZE) {
		buffer[0].length = AX211_COMMAND_FIRST_TRANSFER_SIZE;
		buffer[1].address =
			slot_address + AX211_COMMAND_FIRST_TRANSFER_SIZE;
		buffer[1].length =
			(uint16_t)(total_length -
				   AX211_COMMAND_FIRST_TRANSFER_SIZE);
		buffer_count = 2U;
	}

	/* Handles the core result condition. */
	core_result = drv_intel_ax211_tfd_encode(tfd, buffer, buffer_count);
	if (core_result != INTEL_AX211_SUCCESS) {
		kern_memset(slot, 0, INTEL_AX211_TRANSPORT_COMMAND_SLOT_SIZE);
		ax211_command_rollback(transport);

		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_INVALID;
	}

	/* Publishes command bytes before the TFD which references them. */

	/* Checks the operation result. */
	result = ax211_dma_sync(transport,
				INTEL_AX211_TRANSPORT_DMA_COMMAND_SLOTS,
				slot_offset, header_size + payload_length,
				INTEL_AX211_TRANSPORT_DMA_PREWRITE);
	if (result == INTEL_AX211_TRANSPORT_OK) {
		result = ax211_dma_sync(transport,
					INTEL_AX211_TRANSPORT_DMA_COMMAND_TFD,
					tfd_offset, INTEL_AX211_TRANSFER_DESCRIPTOR_SIZE,
					INTEL_AX211_TRANSPORT_DMA_PREWRITE);
	}

	/* Checks the operation result. */
	if (result != INTEL_AX211_TRANSPORT_OK) {
		kern_memset(slot, 0, INTEL_AX211_TRANSPORT_COMMAND_SLOT_SIZE);
		kern_memset(tfd, 0, INTEL_AX211_TRANSFER_DESCRIPTOR_SIZE);
		ax211_command_rollback(transport);

		/* Failed. */
		return result;
	}

	/* Exposes the token only after both DMA objects are fully visible. */
	transport->command_prepared_token = reserved;
	transport->command_prepared = 1U;
	*token = reserved;
	/* Returns the computed result. */
	return INTEL_AX211_TRANSPORT_OK;
}

/*
 * Reserves one command and publishes a complete wide command from the controller-owned 4 KiB external DMA buffer.  The single buffer remains owned by this token until completion, an un-rung abort, or proven reset.
 */
int
drv_intel_ax211_transport_command_prepare_external(
	struct intel_ax211_transport *transport,
	const struct intel_ax211_command_id *command,
	const void *payload,
	size_t payload_length,
	struct intel_ax211_ring_token *token)
{
	struct intel_ax211_command_id wire_command;
	struct intel_ax211_ring_token reserved;
	struct intel_ax211_tfd_buffer buffer[2];
	uint8_t *external;
	uint8_t *tfd;
	size_t header_size;
	size_t tfd_offset;
	size_t total_length;
	int core_result;
	int result;
	int scrub_result;

	/* Checks the ax211 transport valid result. */
	if (!ax211_transport_valid(transport) || command == NULL ||
	    token == NULL) {
		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_INVALID;
	}

	/* Checks the operation status. */
	if (!transport->rings_initialized || !transport->interrupts_enabled ||
	    transport->quiesced || transport->failed ||
	    transport->command_prepared || transport->command_reset_required) {
		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_ORDER;
	}

	/* Handles the transport condition. */
	if (transport->command_external_active)
		return INTEL_AX211_TRANSPORT_FULL;

	/* Handles the payload availability. */
	if (payload == NULL ||
	    payload_length <=
		    INTEL_AX211_TRANSPORT_COMMAND_INLINE_PAYLOAD_MAX ||
	    payload_length > INTEL_AX211_TRANSPORT_COMMAND_EXTERNAL_PAYLOAD_MAX) {
		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_INVALID;
	}

	/* Handles the core result condition. */
	core_result = drv_intel_ax211_ring_reserve(&transport->command_ring,
						   &reserved);
	if (core_result == INTEL_AX211_FULL)
		return INTEL_AX211_TRANSPORT_FULL;

	/* Handles the core result condition. */
	if (core_result != INTEL_AX211_SUCCESS)
		return INTEL_AX211_TRANSPORT_INVALID;
	tfd_offset = (size_t)reserved.index * INTEL_AX211_TRANSFER_DESCRIPTOR_SIZE;
	external = transport->memory.command_external;
	tfd = transport->memory.command_tfd + tfd_offset;

	kern_memset(external, 0, transport->memory.command_external_size);
	header_size = INTEL_AX211_WIDE_COMMAND_HEADER_SIZE;

	/* Handles the command condition. */
	if (command->group == AX211_COMMAND_FAMILY_CLASSIC) {
		wire_command = *command;
		wire_command.group = AX211_COMMAND_FAMILY_CLASSIC_WIDE;
		wire_command.version = 0U;
		core_result = drv_intel_ax211_wide_command_encode(
			external, &wire_command, (uint16_t)payload_length,
			&reserved);
	} else {
		core_result = drv_intel_ax211_wide_command_encode(
			external, command, (uint16_t)payload_length, &reserved);
	}

	/* Handles the core result condition. */
	if (core_result != INTEL_AX211_SUCCESS) {
		ax211_command_rollback(transport);

		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_INVALID;
	}

	kern_memcpy(external + header_size, payload, payload_length);
	total_length = header_size + payload_length;
	buffer[0].address = transport->memory.command_external_device_address;
	buffer[0].length = AX211_COMMAND_FIRST_TRANSFER_SIZE;
	buffer[1].address = transport->memory.command_external_device_address +
			    AX211_COMMAND_FIRST_TRANSFER_SIZE;
	buffer[1].length =
		(uint16_t)(total_length - AX211_COMMAND_FIRST_TRANSFER_SIZE);

	/* Handles the core result condition. */
	core_result = drv_intel_ax211_tfd_encode(tfd, buffer, 2U);
	if (core_result != INTEL_AX211_SUCCESS) {
		kern_memset(external, 0, transport->memory.command_external_size);
		ax211_command_rollback(transport);

		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_INVALID;
	}

	/* Checks the operation result. */
	result = ax211_dma_sync(
		transport, INTEL_AX211_TRANSPORT_DMA_COMMAND_EXTERNAL, 0U,
		total_length, INTEL_AX211_TRANSPORT_DMA_PREWRITE);
	if (result == INTEL_AX211_TRANSPORT_OK) {
		result = ax211_dma_sync(transport,
					INTEL_AX211_TRANSPORT_DMA_COMMAND_TFD,
					tfd_offset, INTEL_AX211_TRANSFER_DESCRIPTOR_SIZE,
					INTEL_AX211_TRANSPORT_DMA_PREWRITE);
	}

	/* Checks the operation result. */
	if (result != INTEL_AX211_TRANSPORT_OK) {
		kern_memset(external, 0, transport->memory.command_external_size);
		kern_memset(tfd, 0, INTEL_AX211_TRANSFER_DESCRIPTOR_SIZE);

		/* Handles the scrub result condition. */
		scrub_result = ax211_command_external_scrub(transport);
		if (scrub_result == INTEL_AX211_TRANSPORT_OK) {
			scrub_result = ax211_dma_sync(
				transport,
				INTEL_AX211_TRANSPORT_DMA_COMMAND_TFD,
				tfd_offset, INTEL_AX211_TRANSFER_DESCRIPTOR_SIZE,
				INTEL_AX211_TRANSPORT_DMA_PREWRITE);
		}

		ax211_command_rollback(transport);

		/* Handles the scrub result condition. */
		if (scrub_result != INTEL_AX211_TRANSPORT_OK) {
			transport->failed = 1U;
			transport->quiesced = 1U;

			/* Returns the computed result. */
			return scrub_result;
		}

		/* Failed. */
		return result;
	}

	transport->command_external_token = reserved;
	transport->command_external_active = 1U;
	transport->command_prepared_token = reserved;
	transport->command_prepared = 1U;
	*token = reserved;
	/* Returns the computed result. */
	return INTEL_AX211_TRANSPORT_OK;
}

/*
 * Rings one previously prepared command after logical metadata is installed.
 */
int
drv_intel_ax211_transport_command_publish(
	struct intel_ax211_transport *transport,
	const struct intel_ax211_ring_token *token)
{
	uint32_t write_pointer;
	int valid;
	int result;

	/* Refuses an uninitialized transport or a missing prepared token. */
	valid = ax211_transport_valid(transport);
	if (!valid || token == NULL)
		return INTEL_AX211_TRANSPORT_INVALID;

	/* Requires an active queue and the DMA publication made by prepare. */
	if (!transport->rings_initialized ||
	    !transport->interrupts_enabled ||
	    transport->quiesced ||
	    transport->failed ||
	    !transport->command_prepared) {
		return INTEL_AX211_TRANSPORT_ORDER;
	}

	/* Rejects a token that does not own the prepared DMA command. */
	if (token->queue != transport->command_prepared_token.queue ||
	    token->index != transport->command_prepared_token.index) {
		return INTEL_AX211_TRANSPORT_STALE;
	}

	/*
	 * Advances the hardware pointer modulo 65536 independently of the
	 * 256 DMA slots. A failed write may already have reached hardware,
	 * so only a proven device reset may discard this sequence.
	 */
	transport->command_write_sequence =
	    (uint16_t)(transport->command_write_sequence + 1U);
	write_pointer = ((uint32_t)token->queue << 16) |
			transport->command_write_sequence;
	result = ax211_csr_write(
	    transport,
	    AX211_CSR_QUEUE_WRITE_INDEX,
	    write_pointer);
	transport->command_prepared = 0U;
	if (result != INTEL_AX211_TRANSPORT_OK) {
		/* Prevents reuse until reset resolves the ambiguous doorbell. */
		transport->command_reset_required = 1U;
		return INTEL_AX211_TRANSPORT_AMBIGUOUS;
	}

	/* Succeeded: firmware can consume the prepared command once. */
	return INTEL_AX211_TRANSPORT_OK;
}

/*
 * Aborts only a DMA-visible command whose doorbell was never attempted.
 */
int
drv_intel_ax211_transport_command_abort_prepared(
	struct intel_ax211_transport *transport,
	const struct intel_ax211_ring_token *token)
{
	size_t slot_offset;
	size_t tfd_offset;
	uint16_t newest;
	int external;
	int result;

	/* Checks the ax211 transport valid result. */
	if (!ax211_transport_valid(transport) || token == NULL)
		return INTEL_AX211_TRANSPORT_INVALID;

	/* Checks the operation status. */
	if (!transport->command_prepared || transport->failed)
		return INTEL_AX211_TRANSPORT_ORDER;

	/* Handles the token condition. */
	if (token->queue != transport->command_prepared_token.queue ||
	    token->index != transport->command_prepared_token.index) {
		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_STALE;
	}

	/* Handles the newest condition. */
	newest = (uint16_t)((transport->command_ring.head - 1U) &
			    (transport->command_ring.capacity - 1U));
	if (newest != token->index)
		return INTEL_AX211_TRANSPORT_STALE;
	external = ax211_command_external_matches(transport, token);

	/* Scrubs and republishes the un-rung slot before making it reusable. */
	slot_offset =
		(size_t)token->index * INTEL_AX211_TRANSPORT_COMMAND_SLOT_SIZE;
	tfd_offset = (size_t)token->index * INTEL_AX211_TRANSFER_DESCRIPTOR_SIZE;
	kern_memset(transport->memory.command_slots + slot_offset, 0,
	       INTEL_AX211_TRANSPORT_COMMAND_SLOT_SIZE);
	kern_memset(transport->memory.command_tfd + tfd_offset, 0,
	       INTEL_AX211_TRANSFER_DESCRIPTOR_SIZE);

	/* Handles the external condition. */
	result = INTEL_AX211_TRANSPORT_OK;
	if (external)
		result = ax211_command_external_scrub(transport);
	if (result == INTEL_AX211_TRANSPORT_OK) {
		result = ax211_dma_sync(
			transport, INTEL_AX211_TRANSPORT_DMA_COMMAND_SLOTS,
			slot_offset, INTEL_AX211_TRANSPORT_COMMAND_SLOT_SIZE,
			INTEL_AX211_TRANSPORT_DMA_PREWRITE);
	}

	/* Checks the operation result. */
	if (result == INTEL_AX211_TRANSPORT_OK) {
		result = ax211_dma_sync(transport,
					INTEL_AX211_TRANSPORT_DMA_COMMAND_TFD,
					tfd_offset, INTEL_AX211_TRANSFER_DESCRIPTOR_SIZE,
					INTEL_AX211_TRANSPORT_DMA_PREWRITE);
	}

	/* Checks the operation result. */
	if (result != INTEL_AX211_TRANSPORT_OK) {
		transport->failed = 1U;
		transport->quiesced = 1U;

		/* Failed. */
		return result;
	}

	ax211_command_rollback(transport);

	/* Handles the external condition. */
	if (external) {
		kern_memset(&transport->command_external_token, 0,
		       sizeof(transport->command_external_token));
		transport->command_external_active = 0U;
	}

	kern_memset(&transport->command_prepared_token, 0,
	       sizeof(transport->command_prepared_token));
	transport->command_prepared = 0U;
	transport->command_reset_required = 0U;

	/* Returns the computed result. */
	return INTEL_AX211_TRANSPORT_OK;
}

/*
 * Convenience wrapper for callers which need no pre-doorbell metadata step.
 */
int
drv_intel_ax211_transport_command_submit_inline(
	struct intel_ax211_transport *transport,
	const struct intel_ax211_command_id *command,
	const void *payload,
	size_t payload_length,
	struct intel_ax211_ring_token *token)
{
	int error;
	int result;

	/* Checks the operation result. */
	result = drv_intel_ax211_transport_command_prepare_inline(
		transport, command, payload, payload_length, token);
	if (result != INTEL_AX211_TRANSPORT_OK)
		return result;

	/* Obtains the drv intel ax211 transport command publish result. */
	error =
		drv_intel_ax211_transport_command_publish(transport, token);

	/* Returns the computed result. */
	return error;
}

/*
 * Retires one in-order command token and scrubs its reusable DMA slots.
 */
int
drv_intel_ax211_transport_command_complete(
	struct intel_ax211_transport *transport,
	const struct intel_ax211_ring_token *token)
{
	size_t slot_offset;
	size_t tfd_offset;
	int external;
	int core_result;
	int result;

	/* Checks the ax211 transport valid result. */
	if (!ax211_transport_valid(transport) || token == NULL)
		return INTEL_AX211_TRANSPORT_INVALID;

	/* Checks the operation status. */
	if (transport->quiesced || transport->failed)
		return INTEL_AX211_TRANSPORT_ORDER;

	/* Handles the transport condition. */
	if (transport->command_prepared &&
	    token->queue == transport->command_prepared_token.queue &&
	    token->index == transport->command_prepared_token.index) {
		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_ORDER;
	}

	/* Validates the oldest token before touching its reusable DMA slot. */
	if (transport->command_ring.used == 0U ||
	    token->queue != transport->command_ring.queue ||
	    token->index != (uint8_t)transport->command_ring.tail) {
		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_STALE;
	}
	slot_offset =
		(size_t)token->index * INTEL_AX211_TRANSPORT_COMMAND_SLOT_SIZE;
	tfd_offset = (size_t)token->index * INTEL_AX211_TRANSFER_DESCRIPTOR_SIZE;
	external = ax211_command_external_matches(transport, token);

	/* Scrubs retired host-command bytes before the slot can be reused. */
	kern_memset(transport->memory.command_slots + slot_offset, 0,
	       INTEL_AX211_TRANSPORT_COMMAND_SLOT_SIZE);
	kern_memset(transport->memory.command_tfd + tfd_offset, 0,
	       INTEL_AX211_TRANSFER_DESCRIPTOR_SIZE);

	/* Handles the external condition. */
	result = INTEL_AX211_TRANSPORT_OK;
	if (external)
		result = ax211_command_external_scrub(transport);
	if (result == INTEL_AX211_TRANSPORT_OK) {
		result = ax211_dma_sync(
			transport, INTEL_AX211_TRANSPORT_DMA_COMMAND_SLOTS,
			slot_offset, INTEL_AX211_TRANSPORT_COMMAND_SLOT_SIZE,
			INTEL_AX211_TRANSPORT_DMA_PREWRITE);
	}

	/* Checks the operation result. */
	if (result == INTEL_AX211_TRANSPORT_OK) {
		result = ax211_dma_sync(transport,
					INTEL_AX211_TRANSPORT_DMA_COMMAND_TFD,
					tfd_offset, INTEL_AX211_TRANSFER_DESCRIPTOR_SIZE,
					INTEL_AX211_TRANSPORT_DMA_PREWRITE);
	}

	/* Checks the operation result. */
	if (result != INTEL_AX211_TRANSPORT_OK) {
		transport->failed = 1U;
		transport->quiesced = 1U;

		/* Failed. */
		return result;
	}

	/* Retires the slot only after its scrub is DMA-visible. */
	core_result =
		drv_intel_ax211_ring_complete(&transport->command_ring, token);

	/* Handles the core result condition. */
	if (core_result != INTEL_AX211_SUCCESS) {
		transport->failed = 1U;
		transport->quiesced = 1U;

		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_FAILED;
	}

	/* Handles the external condition. */
	if (external) {
		kern_memset(&transport->command_external_token, 0,
		       sizeof(transport->command_external_token));
		transport->command_external_active = 0U;
	}

	/* Returns the computed result. */
	return INTEL_AX211_TRANSPORT_OK;
}

/*
 * Returns the exact number of reserved, not-yet-retired hardware slots.
 */
size_t
drv_intel_ax211_transport_command_pending_count(
	const struct intel_ax211_transport *transport)
{
	/* Checks the ax211 transport valid result. */
	if (!ax211_transport_valid(transport))
		return 0U;

	/* Returns the computed result. */
	return transport->command_ring.used;
}

/*
 * Returns the oldest hardware-owned command token without retiring it.
 */
int
drv_intel_ax211_transport_command_oldest(
	const struct intel_ax211_transport *transport,
	struct intel_ax211_ring_token *token)
{
	/* Checks the ax211 transport valid result. */
	if (!ax211_transport_valid(transport) || token == NULL)
		return INTEL_AX211_TRANSPORT_INVALID;

	/* Handles the transport condition. */
	if (transport->command_ring.used == 0U)
		return INTEL_AX211_TRANSPORT_STALE;
	token->queue = transport->command_ring.queue;
	token->index = transport->command_ring.tail;

	/* Returns the computed result. */
	return INTEL_AX211_TRANSPORT_OK;
}

/*
 * Scrubs and resets command DMA only after the controller reset boundary.
 */
int
drv_intel_ax211_transport_command_after_device_reset(
	struct intel_ax211_transport *transport)
{
	int core_result;
	int result;

	/* Checks the ax211 transport valid result. */
	if (!ax211_transport_valid(transport))
		return INTEL_AX211_TRANSPORT_INVALID;

	/* Checks the operation status. */
	if (!transport->quiesced && !transport->failed)
		return INTEL_AX211_TRANSPORT_ORDER;

	/*
	 * The caller's reset guarantee makes every prior doorbell unobservable.
	 */
	kern_memset(transport->memory.command_slots, 0,
	       transport->memory.command_slots_size);
	kern_memset(transport->memory.command_tfd, 0,
	       transport->memory.command_tfd_size);
	kern_memset(transport->memory.command_byte_count, 0,
	       transport->memory.command_byte_count_size);
	kern_memset(transport->memory.command_external, 0,
	       transport->memory.command_external_size);

	/* Checks the operation result. */
	result = ax211_dma_sync(transport,
				INTEL_AX211_TRANSPORT_DMA_COMMAND_SLOTS, 0U,
				transport->memory.command_slots_size,
				INTEL_AX211_TRANSPORT_DMA_PREWRITE);
	if (result == INTEL_AX211_TRANSPORT_OK) {
		result = ax211_dma_sync(transport,
					INTEL_AX211_TRANSPORT_DMA_COMMAND_TFD,
					0U, transport->memory.command_tfd_size,
					INTEL_AX211_TRANSPORT_DMA_PREWRITE);
	}

	/* Checks the operation result. */
	if (result == INTEL_AX211_TRANSPORT_OK) {
		result = ax211_dma_sync(
			transport, INTEL_AX211_TRANSPORT_DMA_COMMAND_BYTE_COUNT,
			0U, transport->memory.command_byte_count_size,
			INTEL_AX211_TRANSPORT_DMA_PREWRITE);
	}

	/* Checks the operation result. */
	if (result == INTEL_AX211_TRANSPORT_OK)
		result = ax211_command_external_scrub(transport);
	if (result != INTEL_AX211_TRANSPORT_OK) {
		transport->failed = 1U;

		/* Failed. */
		return result;
	}

	/* Handles the core result condition. */
	core_result = drv_intel_ax211_ring_init(&transport->command_ring,
						AX211_COMMAND_RING_ID,
						INTEL_AX211_COMMAND_RING_SIZE);
	if (core_result != INTEL_AX211_SUCCESS) {
		transport->failed = 1U;

		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_FAILED;
	}

	/* Discards prior doorbells only after the proven hardware reset. */
	transport->command_write_sequence = 0U;

	/* Releases prepared and external ownership after the DMA scrub. */
	kern_memset(&transport->command_prepared_token, 0,
	       sizeof(transport->command_prepared_token));
	kern_memset(&transport->command_external_token, 0,
	       sizeof(transport->command_external_token));
	transport->command_prepared = 0U;
	transport->command_external_active = 0U;
	transport->command_reset_required = 0U;
	transport->command_reset_completed = 1U;

	/* Returns the computed result. */
	return INTEL_AX211_TRANSPORT_OK;
}

/*
 * Masks and acknowledges interrupts, then waits finitely for RX DMA idle.
 */
int
drv_intel_ax211_transport_quiesce(
	struct intel_ax211_transport *transport)
{
	uint32_t queue_causes;
	uint32_t hardware;
	int commands_outstanding;
	int result;
	int unlock_result;

	/* Checks the ax211 transport valid result. */
	if (!ax211_transport_valid(transport))
		return INTEL_AX211_TRANSPORT_INVALID;

	/* Handles the transport condition. */
	if (transport->quiesced && transport->rx_dma_idle)
		return INTEL_AX211_TRANSPORT_OK;

	/* Stops all new host submissions before changing device ownership. */
	transport->quiesced = 1U;
	commands_outstanding = transport->command_ring.used != 0U;

	/* Handles the transport condition. */
	if (transport->msix_configured) {
		result = ax211_mask_all(transport);
		transport->interrupts_enabled = 0U;
		transport->enabled_queue_causes = 0U;
		transport->enabled_event_causes = 0U;
		if (result != INTEL_AX211_TRANSPORT_OK) {
			transport->failed = 1U;

			/* Failed. */
			return result;
		}

		/* Checks the operation result. */
		result = ax211_ack_raw(transport, &queue_causes, &hardware);
		if (result != INTEL_AX211_TRANSPORT_OK) {
			transport->failed = 1U;

			/* Failed. */
			return result;
		}
	}

	/* A state which never published rings has no device DMA to stop. */
	if (!transport->rings_initialized) {
		transport->rx_dma_idle = 1U;

		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_OK;
	}

	/* Disables Gen3 RX DMA only inside one checked NIC ownership scope. */

	/* Checks the operation result. */
	result = transport->ops->wake_hold(transport->argument);
	if (result != 0) {
		transport->failed = 1U;

		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_IO;
	}

	/* Checks the operation result. */
	result = transport->ops->prph_write32(
		transport->argument,
		transport->profile.upper_cpu_prph_base +
			AX211_PRPH_RX_DMA_CONFIG,
		0U);
	if (result == 0)
		result = ax211_poll_rx_idle(transport);
	else
		result = INTEL_AX211_TRANSPORT_IO;

	/* Checks the operation result. */
	unlock_result = transport->ops->wake_release(transport->argument);
	if (result != INTEL_AX211_TRANSPORT_OK || unlock_result != 0) {
		transport->failed = 1U;
		transport->rx_dma_idle = 0U;

		/* Checks the operation result. */
		if (result != INTEL_AX211_TRANSPORT_OK)
			return result;

		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_IO;
	}

	transport->rx_dma_idle = 1U;

	/* Handles the commands outstanding condition. */
	if (commands_outstanding) {
		transport->failed = 1U;

		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_FAILED;
	}

	/* Returns the computed result. */
	return INTEL_AX211_TRANSPORT_OK;
}

/* Validates every checked operation required by this transport. */
static int
ax211_ops_valid(
	const struct intel_ax211_transport_ops *ops)
{
	/* Handles the ops availability. */
	if (ops == NULL)
		return 0;

	/* Handles the csr read32 availability. */
	if (ops->csr_read32 == NULL || ops->csr_write32 == NULL ||
	    ops->csr_write8 == NULL) {
		/* Succeeded. */
		return 0;
	}

	/* Handles the nic lock availability. */
	if (ops->wake_hold == NULL || ops->wake_release == NULL ||
	    ops->prph_read32 == NULL || ops->prph_write32 == NULL) {
		/* Succeeded. */
		return 0;
	}

	/* Handles the dma sync availability. */
	if (ops->dma_sync == NULL || ops->delay_us == NULL ||
	    ops->clock_us == NULL) {
		/* Succeeded. */
		return 0;
	}

	/* Reports operation failure. */
	return 1;
}

/* Validates the exact P038 SO-or-SOF plus GF MMIO profile. */
static int
ax211_profile_valid(
	const struct intel_ax211_mmio_profile *profile)
{
	/* Handles the profile availability. */
	if (profile == NULL)
		return 0;

	/* Handles the profile condition. */
	if (profile->mac_type != INTEL_AX211_MMIO_MAC_SO &&
	    profile->mac_type != INTEL_AX211_MMIO_MAC_SOF) {
		/* Succeeded. */
		return 0;
	}

	/* Handles the profile condition. */
	if (profile->rf_type != INTEL_AX211_MMIO_RF_GF || profile->concurrent_bands != 0U)
		return 0;

	/* Handles the profile condition. */
	if (profile->integrated != 0U ||
	    profile->upper_cpu_prph_base != INTEL_AX211_MMIO_UPPER_CPU_PRPH_BASE) {
		/* Succeeded. */
		return 0;
	}

	/* Reports operation failure. */
	return 1;
}

/* Validates every fixed Gen3 ring allocation and command-slot address. */
static int
ax211_memory_valid(
	const struct intel_ax211_transport_ring_memory *memory)
{
	/* Handles the memory availability. */
	if (memory == NULL)
		return 0;

	/* Handles the command tfd availability. */
	if (memory->command_tfd == NULL ||
	    memory->command_tfd_size != AX211_COMMAND_TFD_SIZE) {
		/* Succeeded. */
		return 0;
	}

	/* Handles the command byte count availability. */
	if (memory->command_byte_count == NULL ||
	    memory->command_byte_count_size != AX211_COMMAND_BYTE_COUNT_SIZE) {
		/* Succeeded. */
		return 0;
	}

	/* Handles the command slots availability. */
	if (memory->command_slots == NULL ||
	    memory->command_slots_size != AX211_COMMAND_SLOTS_SIZE ||
	    memory->command_slots_device_address == 0U ||
	    (memory->command_slots_device_address & 63U) != 0U ||
	    memory->command_slots_device_address >
		    UINT64_MAX - AX211_COMMAND_SLOTS_SIZE) {
		/* Succeeded. */
		return 0;
	}

	/* Handles the command external availability. */
	if (memory->command_external == NULL ||
	    memory->command_external_size != AX211_COMMAND_EXTERNAL_SIZE ||
	    memory->command_external_device_address == 0U ||
	    (memory->command_external_device_address & 63U) != 0U ||
	    memory->command_external_device_address >
		    UINT64_MAX - AX211_COMMAND_EXTERNAL_SIZE) {
		/* Succeeded. */
		return 0;
	}

	/* Handles the rx transfer availability. */
	if (memory->rx_transfer == NULL ||
	    memory->rx_transfer_size != AX211_RX_TRANSFER_SIZE) {
		/* Succeeded. */
		return 0;
	}

	/* Handles the rx completion availability. */
	if (memory->rx_completion == NULL ||
	    memory->rx_completion_size != AX211_RX_COMPLETION_SIZE) {
		/* Succeeded. */
		return 0;
	}

	/* Handles the rx status availability. */
	if (memory->rx_status == NULL ||
	    memory->rx_status_size != AX211_RX_STATUS_SIZE) {
		/* Succeeded. */
		return 0;
	}

	/* Reports operation failure. */
	return 1;
}

/* Validates one initialized private transport state. */
static int
ax211_transport_valid(
	const struct intel_ax211_transport *transport)
{
	/* Checks the ax211 ops valid result. */
	if (transport == NULL || !ax211_ops_valid(transport->ops))
		return 0;

	/* Checks the ax211 profile valid result. */
	if (!ax211_profile_valid(&transport->profile))
		return 0;

	/* Checks the ax211 memory valid result. */
	if (!ax211_memory_valid(&transport->memory))
		return 0;

	/* Reports operation failure. */
	return 1;
}

/* Masks both MSI-X cause banks in safe shutdown order. */
static int
ax211_mask_all(
	struct intel_ax211_transport *transport)
{
	int first_result;
	int second_result;

	first_result = ax211_csr_write(transport, AX211_CSR_MSIX_QUEUE_MASK,
				       UINT32_MAX);
	second_result = ax211_csr_write(
		transport, AX211_CSR_MSIX_EVENT_MASK, UINT32_MAX);

	/* Handles the first result condition. */
	if (first_result != INTEL_AX211_TRANSPORT_OK)
		return first_result;

	/* Returns the computed result. */
	return second_result;
}

/* Writes one checked 32-bit transport CSR. */
static int
ax211_csr_write(
	struct intel_ax211_transport *transport,
	uint32_t offset,
	uint32_t value)
{
	/* Checks the csr write32 result. */
	if (transport->ops->csr_write32(transport->argument, offset, value) !=
	    0) {
		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_IO;
	}

	/* Returns the computed result. */
	return INTEL_AX211_TRANSPORT_OK;
}

/*
 * Sends every routed cause to MSI-X vector 0.
 *
 * Each cause owns one byte of a vector map, at the position of its bit in
 * its cause register: bits 15:0 of the queue-cause register (the receive
 * queues) own bytes 0 to 15 of the receive vector map, bits 31:16 of that
 * register own bytes 0 to 15 of the cause vector map, and bit n of the
 * event-cause register owns byte 16 + n of the cause vector map.  The map is
 * derived from the routed cause sets, so a cause that is unmasked always has
 * a route.  The caller masks every cause first, so the order in which the
 * bytes are written does not matter.
 */
static int
ax211_configure_msix_routes(
	struct intel_ax211_transport *transport)
{
	uint32_t cause_bit;
	uint32_t cause_mask;
	uint32_t map_offset;
	int result;

	/* Routes the queue causes in ascending bit order. */
	for (cause_bit = 0U; cause_bit < 32U; cause_bit++) {
		cause_mask = UINT32_C(1) << cause_bit;

		/* Leaves a cause that is never unmasked without a route. */
		if ((AX211_QUEUE_CAUSES_ROUTED & cause_mask) == 0U)
			continue;

		/* Selects the receive map for a queue and the cause map above. */
		if (cause_bit < 16U) {
			map_offset = AX211_CSR_MSIX_RX_VECTOR_MAP + cause_bit;
		} else {
			map_offset = AX211_CSR_MSIX_CAUSE_VECTOR_MAP +
				     (cause_bit - 16U);
		}

		/* Sends the cause to vector 0 and leaves its clearing to us. */
		result = ax211_csr_write8(
			transport,
			map_offset,
			AX211_VECTOR_MAP_TARGET | AX211_VECTOR_MAP_MANUAL_CLEAR);

		/* Stops at the first byte the device did not take. */
		if (result != INTEL_AX211_TRANSPORT_OK)
			return result;
	}

	/* Routes the event causes in ascending bit order. */
	for (cause_bit = 0U; cause_bit < 32U; cause_bit++) {
		cause_mask = UINT32_C(1) << cause_bit;

		/* Leaves a cause that is never unmasked without a route. */
		if ((AX211_EVENT_CAUSES_ROUTED & cause_mask) == 0U)
			continue;

		/* Places the event causes after the sixteen DMA cause bytes. */
		map_offset = AX211_CSR_MSIX_CAUSE_VECTOR_MAP + 16U + cause_bit;

		/* Sends the cause to vector 0 and leaves its clearing to us. */
		result = ax211_csr_write8(
			transport,
			map_offset,
			AX211_VECTOR_MAP_TARGET | AX211_VECTOR_MAP_MANUAL_CLEAR);

		/* Stops at the first byte the device did not take. */
		if (result != INTEL_AX211_TRANSPORT_OK)
			return result;
	}

	/* Reports that every routed cause now reaches vector 0. */
	return INTEL_AX211_TRANSPORT_OK;
}

/* Writes one checked vector-map or interrupt-delay byte. */
static int
ax211_csr_write8(
	struct intel_ax211_transport *transport,
	uint32_t offset,
	uint8_t value)
{
	/* Checks the csr write8 result. */
	if (transport->ops->csr_write8(transport->argument, offset, value) != 0)
		return INTEL_AX211_TRANSPORT_IO;

	/* Returns the computed result. */
	return INTEL_AX211_TRANSPORT_OK;
}

/* Makes a best-effort mask after a partly written vector map. */
static void
ax211_mask_all_best_effort(
	struct intel_ax211_transport *transport)
{
	(void)ax211_csr_write(transport, AX211_CSR_MSIX_QUEUE_MASK,
			      UINT32_MAX);
	(void)ax211_csr_write(transport, AX211_CSR_MSIX_EVENT_MASK,
			      UINT32_MAX);
}

/* Synchronizes one checked subrange of a fixed DMA object. */
static int
ax211_dma_sync(
	struct intel_ax211_transport *transport,
	enum intel_ax211_transport_dma_region region,
	size_t offset,
	size_t length,
	enum intel_ax211_transport_dma_direction direction)
{
	/* Checks the dma sync result. */
	if (transport->ops->dma_sync(transport->argument, region, offset,
				     length, direction) != 0) {
		/* Returns the computed result. */
		return INTEL_AX211_TRANSPORT_IO;
	}

	/* Returns the computed result. */
	return INTEL_AX211_TRANSPORT_OK;
}

/* Reads one checked transport CSR. */
static int
ax211_csr_read(
	struct intel_ax211_transport *transport,
	uint32_t offset,
	uint32_t *value)
{
	/* Checks the csr read32 result. */
	if (transport->ops->csr_read32(transport->argument, offset, value) != 0)
		return INTEL_AX211_TRANSPORT_IO;

	/* Returns the computed result. */
	return INTEL_AX211_TRANSPORT_OK;
}

/* Captures and W1C-acknowledges both raw MSI-X cause registers. */
static int
ax211_ack_raw(
	struct intel_ax211_transport *transport,
	uint32_t *queue_causes,
	uint32_t *hardware)
{
	int error;
	int result;

	/* Checks the operation result. */
	result = ax211_csr_read(transport, AX211_CSR_MSIX_QUEUE_CAUSES,
				queue_causes);
	if (result != INTEL_AX211_TRANSPORT_OK)
		return result;

	/* Checks the operation result. */
	result = ax211_csr_read(transport, AX211_CSR_MSIX_EVENT_CAUSES,
				hardware);
	if (result != INTEL_AX211_TRANSPORT_OK)
		return result;
	result = ax211_csr_write(transport, AX211_CSR_MSIX_QUEUE_CAUSES,
				 *queue_causes);
	/* Checks the operation result. */
	if (result != INTEL_AX211_TRANSPORT_OK)
		return result;

	/* Obtains the ax211 csr write result. */
	error = ax211_csr_write(
		transport, AX211_CSR_MSIX_EVENT_CAUSES, *hardware);

	/* Returns the computed result. */
	return error;
}

/* Validates one fixed-size RX buffer mapping before DMA publication. */
static int
ax211_rx_descriptor_valid(
	uint16_t index,
	uint64_t device_address)
{
	/* Checks the current index. */
	if (index >= INTEL_AX211_TRANSPORT_RX_DESCRIPTOR_COUNT ||
	    device_address == 0U || (device_address & 0xfffU) != 0U ||
	    device_address > UINT64_MAX - 4095U) {
		/* Succeeded. */
		return 0;
	}

	/* Reports operation failure. */
	return 1;
}

/* Encodes and synchronizes one caller-validated RX transfer descriptor. */
static int
ax211_publish_rx_descriptor(
	struct intel_ax211_transport *transport,
	uint16_t index,
	uint64_t device_address)
{
	uint8_t *descriptor;
	size_t offset;
	int core_result;
	int result;

	/*
	 * Keeps the private boundary safe if an internal caller is added later.
	 */
	if (!ax211_rx_descriptor_valid(index, device_address))
		return INTEL_AX211_TRANSPORT_INVALID;

	/* Encodes the device address into its stable ring slot. */
	offset = (size_t)index * INTEL_AX211_TRANSPORT_RX_DESCRIPTOR_SIZE;
	descriptor = transport->memory.rx_transfer + offset;

	/* Handles the core result condition. */
	core_result = drv_intel_ax211_rx_transfer_descriptor_encode(
		descriptor, index, device_address);
	if (core_result != INTEL_AX211_SUCCESS)
		return INTEL_AX211_TRANSPORT_INVALID;

	/* Makes the complete descriptor visible before marking it published. */
	result =
		ax211_dma_sync(transport, INTEL_AX211_TRANSPORT_DMA_RX_TRANSFER,
			       offset, INTEL_AX211_TRANSPORT_RX_DESCRIPTOR_SIZE,
			       INTEL_AX211_TRANSPORT_DMA_PREWRITE);

	/* Checks the operation result. */
	if (result != INTEL_AX211_TRANSPORT_OK) {
		kern_memset(descriptor, 0, INTEL_AX211_TRANSPORT_RX_DESCRIPTOR_SIZE);
		ax211_set_published(transport, index, 0);

		/* Failed. */
		return result;
	}

	ax211_set_published(transport, index, 1);

	/* Returns the computed result. */
	return INTEL_AX211_TRANSPORT_OK;
}

/* Updates one host-owned RX descriptor publication bit. */
static void
ax211_set_published(
	struct intel_ax211_transport *transport,
	uint16_t index,
	int published)
{
	uint8_t bit;
	uint8_t *byte;

	bit = (uint8_t)(1U << (index & 7U));

	/* Handles the published condition. */
	byte = &transport->rx_published[index >> 3];
	if (published)
		*byte |= bit;
	else
		*byte &= (uint8_t)~bit;
}

/* Requires every exact Gen3 RX descriptor before activation. */
static int
ax211_all_rx_published(
	const struct intel_ax211_transport *transport)
{
	size_t index;

	/* Verifies every full publication byte. */
	for (index = 0U; index < sizeof(transport->rx_published); index++) {
		/* Handles the transport condition. */
		if (transport->rx_published[index] != 0xffU)
			return 0;
	}

	/* Reports operation failure. */
	return 1;
}

/* Decodes one little-endian status word without alignment assumptions. */
static uint16_t
ax211_get_le16(
	const uint8_t *bytes)
{
	/* Returns the computed result. */
	return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

/* Tests one host-owned RX descriptor publication bit. */
static int
ax211_is_published(
	const struct intel_ax211_transport *transport,
	uint16_t index)
{
	uint8_t bit;

	bit = (uint8_t)(1U << (index & 7U));

	/* Returns the computed result. */
	return (transport->rx_published[index >> 3] & bit) != 0U;
}

/* Rolls back only the newest command reservation before its doorbell. */
static void
ax211_command_rollback(
	struct intel_ax211_transport *transport)
{
	/* Handles the transport condition. */
	if (transport->command_ring.used == 0U)
		return;
	transport->command_ring.head =
		(uint16_t)((transport->command_ring.head - 1U) &
			   (transport->command_ring.capacity - 1U));
	transport->command_ring.used--;
}

/* Scrubs the full external command object and publishes the scrub to DMA. */
static int
ax211_command_external_scrub(
	struct intel_ax211_transport *transport)
{
	int error;

	kern_memset(transport->memory.command_external, 0,
	       transport->memory.command_external_size);

	/* Obtains the ax211 dma sync result. */
	error = ax211_dma_sync(
		transport, INTEL_AX211_TRANSPORT_DMA_COMMAND_EXTERNAL, 0U,
		transport->memory.command_external_size,
		INTEL_AX211_TRANSPORT_DMA_PREWRITE);

	/* Returns the computed result. */
	return error;
}

/* Tests whether the one external DMA buffer still belongs to this token. */
static int
ax211_command_external_matches(
	const struct intel_ax211_transport *transport,
	const struct intel_ax211_ring_token *token)
{
	/* Handles the transport condition. */
	if (!transport->command_external_active)
		return 0;

	/* Returns the computed result. */
	return token->queue == transport->command_external_token.queue &&
	       token->index == transport->command_external_token.index;
}

/* Polls Gen3 RX-DMA idle through one observable 10-ms deadline. */
static int
ax211_poll_rx_idle(
	struct intel_ax211_transport *transport)
{
	uint64_t start;
	uint64_t deadline;
	uint64_t previous;
	uint64_t current;
	uint32_t status;
	uint32_t iteration;
	int result;

	/* Creates a checked absolute RX-idle deadline. */
	if (transport->ops->clock_us(transport->argument, &start) != 0)
		return INTEL_AX211_TRANSPORT_CLOCK;

	/* Handles the start condition. */
	if (start > UINT64_MAX - AX211_RX_IDLE_TIMEOUT_US)
		return INTEL_AX211_TRANSPORT_CLOCK;

	/* Handles the trace deadline availability. */
	deadline = start + AX211_RX_IDLE_TIMEOUT_US;
	if (transport->ops->trace_deadline != NULL) {
		transport->ops->trace_deadline(
			transport->argument, INTEL_AX211_TRANSPORT_WAIT_RX_IDLE,
			start, deadline);
	}

	/* Polls with independent elapsed-time and iteration bounds. */
	previous = start;
	/* Process each element required by the operation. */
	for (iteration = 0U;
	     iteration < AX211_RX_IDLE_TIMEOUT_US / AX211_RX_IDLE_POLL_US + 2U;
	     iteration++) {
		/* Checks the operation result. */
		result = transport->ops->prph_read32(
			transport->argument,
			transport->profile.upper_cpu_prph_base +
				AX211_PRPH_RX_DMA_STATUS,
			&status);
		if (result != 0)
			return INTEL_AX211_TRANSPORT_IO;

		/* Checks the operation status. */
		if ((status & AX211_RX_DMA_STATUS_IDLE) != 0U)
			return INTEL_AX211_TRANSPORT_OK;

		/* Checks the clock us result. */
		if (transport->ops->clock_us(transport->argument, &current) !=
		    0) {
			/* Returns the computed result. */
			return INTEL_AX211_TRANSPORT_CLOCK;
		}

		/* Handles the current condition. */
		if (current < previous)
			return INTEL_AX211_TRANSPORT_CLOCK;

		/* Handles the current condition. */
		if (current >= deadline)
			return INTEL_AX211_TRANSPORT_TIMEOUT;

		/* Checks the delay us result. */
		if (transport->ops->delay_us(transport->argument,
					     AX211_RX_IDLE_POLL_US) != 0) {
			/* Returns the computed result. */
			return INTEL_AX211_TRANSPORT_IO;
		}

		/* Checks the clock us result. */
		if (transport->ops->clock_us(transport->argument, &current) !=
		    0) {
			/* Returns the computed result. */
			return INTEL_AX211_TRANSPORT_CLOCK;
		}

		/* Handles the current condition. */
		if (current <= previous)
			return INTEL_AX211_TRANSPORT_CLOCK;
		previous = current;
	}

	/* Returns the computed result. */
	return INTEL_AX211_TRANSPORT_TIMEOUT;
}
