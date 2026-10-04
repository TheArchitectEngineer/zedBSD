/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Exercises real AX211 command publication across slot and hardware wraps.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../../src/drivers/wifi/intel-ax211/intel-ax211-transport.h"

#define TEST_DOORBELL 0x460U
#define TEST_COMMAND_COUNT 65537U

/* Owns fake DMA and device callbacks for one serial transport lifecycle. */
struct test_fixture {
	struct intel_ax211_transport transport;
	struct intel_ax211_transport_ops ops;
	struct intel_ax211_transport_ring_memory memory;
	struct intel_ax211_mmio_profile profile;
	uint8_t tfd[65536];
	uint8_t byte_count[2048];
	uint8_t slots[82944];
	uint8_t external[4096];
	uint8_t rx_transfer[8192];
	uint8_t rx_completion[16384];
	uint8_t rx_status[2];
	uint8_t payload[320];
	uint64_t clock;
	uint32_t last_doorbell;
	uint32_t doorbells;
	int fail_doorbell;
	int fail_sync;
};

/* Keeps large fake DMA arrays off the stack; each test resets all state. */
static struct test_fixture fixture;

static int test_csr_read(void *argument, uint32_t offset, uint32_t *word);
static int test_csr_write(void *argument, uint32_t offset, uint32_t word);
static int test_csr_write8(void *argument, uint32_t offset, uint8_t byte);
static int test_nic_lock(void *argument);
static int test_nic_unlock(void *argument);
static int test_prph_read(void *argument, uint32_t address, uint32_t *word);
static int test_prph_write(void *argument, uint32_t address, uint32_t word);
static int test_dma_sync(void *argument, enum intel_ax211_transport_dma_region region, size_t offset, size_t length, enum intel_ax211_transport_dma_direction direction);
static int test_delay(void *argument, uint32_t duration);
static int test_clock(void *argument, uint64_t *now);
static void test_setup(void);
static void test_publish_complete(uint32_t count, int external);
static void test_wraps(void);
static void test_unpublished_commands(void);
static void test_ambiguous_write_and_reset(void);

/*
 * Runs bounded command-pointer and reset regression checks.
 */
int
main(
	void)
{
	/* Verifies slot reuse, rejection, and reset with the real transport. */
	test_wraps();
	test_unpublished_commands();
	test_ambiguous_write_and_reset();

	/* Reports the checked hardware boundaries and ownership cases. */
	puts("AX211 transport: 256/512/65536 wraps, abort, failure, reset PASS");

	/* Succeeded: every command doorbell obeyed the hardware contract. */
	return 0;
}

/* Supplies zeroed registers to initialization without emulating firmware. */
static int
test_csr_read(
	void *argument,
	uint32_t offset,
	uint32_t *word)
{
	/* Keeps unrelated register reads deterministic for ring setup. */
	(void)argument;
	(void)offset;
	*word = 0U;

	/* Succeeded: the caller received the fake register content. */
	return 0;
}

/* Records the actual doorbell passed by production publication code. */
static int
test_csr_write(
	void *argument,
	uint32_t offset,
	uint32_t word)
{
	struct test_fixture *state;

	/* Ignores setup registers that do not submit a command. */
	state = argument;
	if (offset != TEST_DOORBELL)
		return 0;

	/* Models a write that may reach hardware even if its callback fails. */
	state->last_doorbell = word;
	state->doorbells++;
	if (state->fail_doorbell) {
		state->fail_doorbell = 0;
		return -1;
	}

	/* Succeeded: one hardware pointer was published. */
	return 0;
}

/* Accepts the interrupt-coalescing byte written during ring setup. */
static int
test_csr_write8(
	void *argument,
	uint32_t offset,
	uint8_t byte)
{
	/* Leaves command publication entirely to the 32-bit callback. */
	(void)argument;
	(void)offset;
	(void)byte;

	/* Succeeded: the fake byte write needs no retained state. */
	return 0;
}

/* Gives the serial test exclusive access to the fake NIC. */
static int
test_nic_lock(
	void *argument)
{
	/* Requires no host lock because the fixture has one caller. */
	(void)argument;

	/* Succeeded: the caller owns fake NIC access. */
	return 0;
}

/* Releases fake NIC access without changing command ownership. */
static int
test_nic_unlock(
	void *argument)
{
	/* Leaves the hardware sequence under the transport's control. */
	(void)argument;

	/* Succeeded: fake NIC access is released. */
	return 0;
}

/* Supplies idle fake peripheral registers to the transport callbacks. */
static int
test_prph_read(
	void *argument,
	uint32_t address,
	uint32_t *word)
{
	/* Does not synthesize command completions or firmware behavior. */
	(void)argument;
	(void)address;
	*word = 0U;

	/* Succeeded: the fake peripheral register is zero. */
	return 0;
}

/* Accepts peripheral setup without advancing any command sequence. */
static int
test_prph_write(
	void *argument,
	uint32_t address,
	uint32_t word)
{
	/* Retains no peripheral state needed by these publication tests. */
	(void)argument;
	(void)address;
	(void)word;

	/* Succeeded: the fake peripheral accepted the write. */
	return 0;
}

/* Injects one DMA visibility failure before a doorbell can be issued. */
static int
test_dma_sync(
	void *argument,
	enum intel_ax211_transport_dma_region region,
	size_t offset,
	size_t length,
	enum intel_ax211_transport_dma_direction direction)
{
	struct test_fixture *state;

	/* Uses the real transport's allocated arrays as coherent host memory. */
	state = argument;
	(void)region;
	(void)offset;
	(void)length;
	(void)direction;

	/* Fails once so the production rollback can publish its scrub. */
	if (state->fail_sync) {
		state->fail_sync = 0;
		return -1;
	}

	/* Succeeded: CPU writes are visible to the fake device. */
	return 0;
}

/* Advances the fake clock without sleeping during bounded setup. */
static int
test_delay(
	void *argument,
	uint32_t duration)
{
	struct test_fixture *state;

	/* Makes the transport's deadline checks deterministic. */
	state = argument;
	state->clock += duration;

	/* Succeeded: simulated time advanced by the requested interval. */
	return 0;
}

/* Reports the fake time used by the checked transport callbacks. */
static int
test_clock(
	void *argument,
	uint64_t *now)
{
	struct test_fixture *state;

	/* Returns the time advanced only by the fixture's delay callback. */
	state = argument;
	*now = state->clock;

	/* Succeeded: the caller received the current simulated time. */
	return 0;
}

/* Initializes real transport state with correctly sized fake DMA memory. */
static void
test_setup(
	void)
{
	int result;

	/* Clears the preceding lifecycle and every fake DMA ownership marker. */
	memset(&fixture, 0, sizeof(fixture));

	/* Supplies all required device operations while observing real writes. */
	fixture.ops.csr_read32 = test_csr_read;
	fixture.ops.csr_write32 = test_csr_write;
	fixture.ops.csr_write8 = test_csr_write8;
	fixture.ops.nic_lock = test_nic_lock;
	fixture.ops.nic_unlock = test_nic_unlock;
	fixture.ops.prph_read32 = test_prph_read;
	fixture.ops.prph_write32 = test_prph_write;
	fixture.ops.dma_sync = test_dma_sync;
	fixture.ops.delay_us = test_delay;
	fixture.ops.clock_us = test_clock;

	/* Uses the existing SO/GF transport profile without changing policy. */
	fixture.profile.mac_type = INTEL_AX211_MMIO_MAC_SO;
	fixture.profile.rf_type = INTEL_AX211_MMIO_RF_GF;
	fixture.profile.umac_prph_offset = INTEL_AX211_MMIO_UMAC_PRPH_OFFSET;

	/* Assigns separate storage to every production DMA allocation. */
	fixture.memory.command_tfd = fixture.tfd;
	fixture.memory.command_tfd_size = sizeof(fixture.tfd);
	fixture.memory.command_byte_count = fixture.byte_count;
	fixture.memory.command_byte_count_size = sizeof(fixture.byte_count);
	fixture.memory.command_slots = fixture.slots;
	fixture.memory.command_slots_size = sizeof(fixture.slots);
	fixture.memory.command_slots_device_address = 0x100000U;
	fixture.memory.command_external = fixture.external;
	fixture.memory.command_external_size = sizeof(fixture.external);
	fixture.memory.command_external_device_address = 0x200000U;
	fixture.memory.rx_transfer = fixture.rx_transfer;
	fixture.memory.rx_transfer_size = sizeof(fixture.rx_transfer);
	fixture.memory.rx_completion = fixture.rx_completion;
	fixture.memory.rx_completion_size = sizeof(fixture.rx_completion);
	fixture.memory.rx_status = fixture.rx_status;
	fixture.memory.rx_status_size = sizeof(fixture.rx_status);

	/* Initializes the real transport and its hardware-ring bookkeeping. */
	result = drv_intel_ax211_transport_init(
	    &fixture.transport,
	    &fixture.ops,
	    &fixture,
	    &fixture.profile,
	    &fixture.memory);
	assert(result == INTEL_AX211_TRANSPORT_OK);
	assert(fixture.transport.command_write_sequence == 0U);

	/* Publishes clean rings through the production initialization path. */
	result = drv_intel_ax211_transport_initialize_rings(&fixture.transport);
	assert(result == INTEL_AX211_TRANSPORT_OK);
	fixture.transport.interrupts_enabled = 1U;
}

/* Publishes and retires one real command while checking hardware and slots. */
static void
test_publish_complete(
	uint32_t count,
	int external)
{
	struct intel_ax211_command_id command = {0x0dU, 1U, 17U};
	struct intel_ax211_ring_token token;
	uint8_t *header;
	size_t slot;
	int result;

	/* Alternates both DMA publication paths using the same command queue. */
	if (external) {
		result = drv_intel_ax211_transport_command_prepare_external(
		    &fixture.transport,
		    &command,
		    fixture.payload,
		    sizeof(fixture.payload),
		    &token);
		assert(result == INTEL_AX211_TRANSPORT_OK);
		header = fixture.external;
	} else {
		result = drv_intel_ax211_transport_command_prepare_inline(
		    &fixture.transport,
		    &command,
		    NULL,
		    0U,
		    &token);
		assert(result == INTEL_AX211_TRANSPORT_OK);
		slot = (size_t)token.index * INTEL_AX211_TRANSPORT_COMMAND_SLOT_SIZE;
		header = fixture.slots + slot;
	}

	/* Keeps the DMA slot and command header index within the 256 slots. */
	assert(token.queue == 0U);
	assert(token.index == (uint8_t)(count - 1U));
	assert(header[2] == token.index);
	assert(header[3] == token.queue);
	assert(fixture.transport.command_ring.head == (count & 255U));

	/* Issues the actual production doorbell with no fixture-side fixup. */
	result = drv_intel_ax211_transport_command_publish(
	    &fixture.transport,
	    &token);
	assert(result == INTEL_AX211_TRANSPORT_OK);

	/* Identifies an incorrect hardware pointer before the assertion stops. */
	if (fixture.last_doorbell != (uint16_t)count) {
		fprintf(stderr, "command %u: doorbell=%u expected=%u\n",
			(unsigned)count,
			(unsigned)fixture.last_doorbell,
			(unsigned)(uint16_t)count);
	}

	/* Checks the 16-bit hardware contract independently of slot reuse. */
	assert(fixture.last_doorbell == (uint16_t)count);
	assert(fixture.doorbells == count);

	/* Retires and scrubs the real DMA slot before the next command. */
	result = drv_intel_ax211_transport_command_complete(
	    &fixture.transport,
	    &token);
	assert(result == INTEL_AX211_TRANSPORT_OK);
	assert(fixture.transport.command_ring.used == 0U);
}

/* Crosses two slot wraps and the complete 16-bit hardware-pointer wrap. */
static void
test_wraps(
	void)
{
	uint32_t count;

	/* Starts the hardware sequence at zero through real initialization. */
	test_setup();

	/* Alternates inline and external commands across every boundary. */
	for (count = 1U; count <= TEST_COMMAND_COUNT; count++)
		test_publish_complete(count, count & 1U);

	/* Retains the hardware sequence after slot completion at the wrap. */
	assert(fixture.transport.command_write_sequence == 1U);
}

/* Rejects invalid and aborted commands without consuming hardware credit. */
static void
test_unpublished_commands(
	void)
{
	struct intel_ax211_command_id command = {0x0dU, 1U, 17U};
	struct intel_ax211_ring_token token;
	struct intel_ax211_ring_token stale;
	int result;

	/* Starts an unused hardware queue before preparing a command. */
	test_setup();
	result = drv_intel_ax211_transport_command_prepare_inline(
	    &fixture.transport,
	    &command,
	    NULL,
	    0U,
	    &token);
	assert(result == INTEL_AX211_TRANSPORT_OK);

	/* Rejects a token that does not own the prepared slot. */
	stale = token;
	stale.index++;
	result = drv_intel_ax211_transport_command_publish(
	    &fixture.transport,
	    &stale);
	assert(result == INTEL_AX211_TRANSPORT_STALE);
	assert(fixture.doorbells == 0U);
	assert(fixture.transport.command_write_sequence == 0U);

	/* Rejects a missing token without dereferencing it or publishing. */
	result = drv_intel_ax211_transport_command_publish(
	    &fixture.transport,
	    NULL);
	assert(result == INTEL_AX211_TRANSPORT_INVALID);
	assert(fixture.doorbells == 0U);

	/* Aborts DMA preparation without consuming a hardware sequence. */
	result = drv_intel_ax211_transport_command_abort_prepared(
	    &fixture.transport,
	    &token);
	assert(result == INTEL_AX211_TRANSPORT_OK);
	assert(fixture.transport.command_write_sequence == 0U);
	assert(fixture.transport.command_ring.head == 0U);

	/* Rolls back one failed DMA sync before any doorbell can be written. */
	fixture.fail_sync = 1;
	result = drv_intel_ax211_transport_command_prepare_inline(
	    &fixture.transport,
	    &command,
	    NULL,
	    0U,
	    &token);
	assert(result == INTEL_AX211_TRANSPORT_IO);
	assert(fixture.transport.command_write_sequence == 0U);
	assert(fixture.doorbells == 0U);

	/* Uses the first hardware sequence after both unpublished attempts. */
	test_publish_complete(1U, 0);

	/* Rejects a duplicate publish after completion removed its ownership. */
	result = drv_intel_ax211_transport_command_publish(
	    &fixture.transport,
	    &token);
	assert(result == INTEL_AX211_TRANSPORT_ORDER);
	assert(fixture.transport.command_write_sequence == 1U);
	assert(fixture.doorbells == 1U);
}

/* Preserves an ambiguous doorbell until a proven device reset completes. */
static void
test_ambiguous_write_and_reset(
	void)
{
	struct intel_ax211_command_id command = {0x0dU, 1U, 17U};
	struct intel_ax211_ring_token token;
	int result;

	/* Prepares a command whose CSR callback will fail after observing it. */
	test_setup();
	result = drv_intel_ax211_transport_command_prepare_inline(
	    &fixture.transport,
	    &command,
	    NULL,
	    0U,
	    &token);
	assert(result == INTEL_AX211_TRANSPORT_OK);
	fixture.fail_doorbell = 1;

	/* Keeps the consumed sequence because callback failure is ambiguous. */
	result = drv_intel_ax211_transport_command_publish(
	    &fixture.transport,
	    &token);
	assert(result == INTEL_AX211_TRANSPORT_AMBIGUOUS);
	assert(fixture.last_doorbell == 1U);
	assert(fixture.transport.command_write_sequence == 1U);
	assert(fixture.transport.command_reset_required == 1U);
	assert(fixture.transport.command_prepared == 0U);

	/* Prevents another preparation before hardware reset settles the write. */
	result = drv_intel_ax211_transport_command_prepare_inline(
	    &fixture.transport,
	    &command,
	    NULL,
	    0U,
	    &token);
	assert(result == INTEL_AX211_TRANSPORT_ORDER);
	assert(fixture.doorbells == 1U);

	/* Refuses software-only reset while the transport is still active. */
	result = drv_intel_ax211_transport_command_after_device_reset(
	    &fixture.transport);
	assert(result == INTEL_AX211_TRANSPORT_ORDER);
	assert(fixture.transport.command_write_sequence == 1U);

	/* Models the caller's device reset and checks the real reset handler. */
	fixture.transport.quiesced = 1U;
	result = drv_intel_ax211_transport_command_after_device_reset(
	    &fixture.transport);
	assert(result == INTEL_AX211_TRANSPORT_OK);
	assert(fixture.transport.command_write_sequence == 0U);
	assert(fixture.transport.command_reset_required == 0U);
	assert(fixture.transport.command_ring.used == 0U);

	/* Starts a new hardware lifecycle after reset released the command. */
	fixture.transport.quiesced = 0U;
	fixture.doorbells = 0U;
	test_publish_complete(1U, 1);
}
