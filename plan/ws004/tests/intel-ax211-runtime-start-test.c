/* -*- mode: c; c-file-style: "linux"; tab-width: 8; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Intel AX211 runtime-session stop table (BUG-158).
 *
 * A live session's stop must drain the interrupt handler, quiesce RX DMA and
 * reset the controller before PCI bus mastering goes off, and must release
 * DMA only after bus mastering is off.  Turning bus mastering off while the
 * firmware still writes scan frames hung the integrated CNVi platform.
 * The fixture places a session directly in the running state and drives
 * only the stop and cleanup entry points; every other collaborator a
 * session start would need is a stub which must not be reached.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../../src/drivers/wifi/intel-ax211/intel-ax211-runtime-start.h"

#define TEST_TRACE_CAPACITY 256U

/*
 * What one table row makes a collaborator do.
 *
 * Each field names the step that fails or misbehaves; zero is the healthy
 * device.
 */
struct test_row {
	const char *name;
	int drain_fails;
	int quiesce_result;
	int stop_fails;
	int stop_times_out_master;
	int bus_master_fails;
	int expected_stop;
	const char *expected_trace;
	int expected_cleanup;
	const char *expected_cleanup_trace;
};

/*
 * One stop's fake device and the order its steps were seen in.
 *
 * It lives on the stack of one table row; active_fixture points at it for
 * the collaborators which receive no argument.
 */
struct test_fixture {
	struct intel_ax211_runtime_start session;
	struct intel_ax211_runtime_start_ops ops;
	struct intel_ax211_mmio mmio;
	struct intel_ax211_transport transport;
	const struct test_row *row;
	char trace[TEST_TRACE_CAPACITY];
	size_t trace_length;
	int stop_count;
	int dma_released;
};

/*
 * The fixture of the row being run.
 *
 * Set at the start of each row and cleared at its end; the stubs refuse to
 * run without it.
 */
static struct test_fixture *active_fixture;

static void test_trace(char value);
static int test_receive_epoch_begin(void *argument, uint32_t generation);
static int test_transport_bind(void *argument, struct intel_ax211_dma_resources *dma, struct intel_ax211_mmio *mmio, struct intel_ax211_transport *transport, uint32_t generation);
static int test_receive_event(void *argument, uint64_t deadline_us, uint8_t *bytes, size_t capacity, struct intel_ax211_boot_received_event *event);
static int test_publish_pnvm(void *argument, struct intel_ax211_dma_resources *dma);
static int test_post_alive(void *argument, const struct intel_ax211_protocol_alive *alive);
static int test_interrupt_drain(void *argument);
static int test_bus_master_disable(void *argument);
static int test_clock_us(void *argument, uint64_t *time_us);
static int test_nic_lock(void *argument);
static int test_nic_unlock(void *argument);
static void test_fixture_running(struct test_fixture *fixture, const struct test_row *row);
static void test_row_run(const struct test_row *row);

/*
 * Releases the fake DMA and records that the release happened.
 */
void
drv_intel_ax211_dma_release(
	struct intel_ax211_dma_resources *resources)
{
	assert(active_fixture != NULL);
	assert(!active_fixture->dma_released);

	test_trace('f');
	memset(resources, 0, sizeof(*resources));
	active_fixture->dma_released = 1;
}

/*
 * Resets the fake controller, optionally failing or missing master-disable.
 */
int
drv_intel_ax211_mmio_stop(
	struct intel_ax211_mmio *mmio)
{
	assert(active_fixture != NULL);
	assert(mmio == &active_fixture->mmio);

	test_trace('s');
	active_fixture->stop_count++;

	/* Fails only the first reset, so the cleanup retry succeeds. */
	if (active_fixture->row->stop_fails && active_fixture->stop_count == 1)
		return INTEL_AX211_MMIO_TIMEOUT;

	/* Misses the master-disable indication only on the first reset. */
	if (active_fixture->row->stop_times_out_master &&
	    active_fixture->stop_count == 1)
		mmio->master_disable_timed_out = 1;

	return INTEL_AX211_MMIO_OK;
}

/*
 * Stops RX DMA on the fake device with the row's quiesce result.
 */
int
drv_intel_ax211_transport_quiesce(
	struct intel_ax211_transport *transport)
{
	assert(active_fixture != NULL);
	assert(transport == &active_fixture->transport);

	test_trace('q');

	/* The cleanup retry sees an already idle transport. */
	if (active_fixture->stop_count != 0)
		return INTEL_AX211_TRANSPORT_OK;

	return active_fixture->row->quiesce_result;
}

/*
 * Retires the fake command ring after the reset.
 */
int
drv_intel_ax211_transport_command_after_device_reset(
	struct intel_ax211_transport *transport)
{
	assert(active_fixture != NULL);
	assert(transport == &active_fixture->transport);

	test_trace('z');
	return INTEL_AX211_TRANSPORT_OK;
}

/*
 * The collaborators below belong to a session start, which this table
 * never runs.
 */
int
drv_intel_ax211_dma_prepare_boot(
	struct drv_dma_device *device,
	const uint8_t *firmware_bytes,
	size_t firmware_length,
	const struct intel_ax211_firmware_manifest *manifest,
	uint16_t hardware_revision,
	struct intel_ax211_dma_resources *resources)
{
	(void)device;
	(void)firmware_bytes;
	(void)firmware_length;
	(void)manifest;
	(void)hardware_revision;
	(void)resources;
	assert(0);
	return -1;
}

/*
 * Never reached: a session start publishes the PNVM.
 */
int
drv_intel_ax211_dma_prepare_pnvm(
	const uint8_t *pnvm_bytes,
	size_t pnvm_length,
	const struct intel_ax211_pnvm_manifest *manifest,
	struct intel_ax211_dma_resources *resources)
{
	(void)pnvm_bytes;
	(void)pnvm_length;
	(void)manifest;
	(void)resources;
	assert(0);
	return -1;
}

/*
 * Never reached: a session start retires the boot images.
 */
void
drv_intel_ax211_dma_release_boot_images(
	struct intel_ax211_dma_resources *resources)
{
	(void)resources;
	assert(0);
}

/*
 * Never reached: a session start loads the firmware files.
 */
int
drv_intel_ax211_firmware_files_load(
	struct intel_ax211_firmware_files *files)
{
	(void)files;
	assert(0);
	return -1;
}

/*
 * Releases firmware files the running fixture never loaded.
 */
void
drv_intel_ax211_firmware_files_release(
	struct intel_ax211_firmware_files *files)
{
	(void)files;
	assert(0);
}

/*
 * Never reached: a session start prepares the card.
 */
int
drv_intel_ax211_mmio_prepare_card_hw(
	struct intel_ax211_mmio *mmio)
{
	(void)mmio;
	assert(0);
	return INTEL_AX211_MMIO_IO;
}

/*
 * Never reached: a session start resets before booting.
 */
int
drv_intel_ax211_mmio_sw_reset(
	struct intel_ax211_mmio *mmio)
{
	(void)mmio;
	assert(0);
	return INTEL_AX211_MMIO_IO;
}

/*
 * Never reached: a session start powers the controller up.
 */
int
drv_intel_ax211_mmio_apm_init(
	struct intel_ax211_mmio *mmio)
{
	(void)mmio;
	assert(0);
	return INTEL_AX211_MMIO_IO;
}

/*
 * Never reached: a session start publishes the context info.
 */
int
drv_intel_ax211_mmio_publish_gen3(
	struct intel_ax211_mmio *mmio,
	const struct intel_ax211_mmio_boot *boot)
{
	(void)mmio;
	(void)boot;
	assert(0);
	return INTEL_AX211_MMIO_IO;
}

/*
 * Never reached: a session start configures MSI-X.
 */
int
drv_intel_ax211_transport_configure_msix(
	struct intel_ax211_transport *transport)
{
	(void)transport;
	assert(0);
	return INTEL_AX211_TRANSPORT_IO;
}

/*
 * Never reached: a session start initializes the rings.
 */
int
drv_intel_ax211_transport_initialize_rings(
	struct intel_ax211_transport *transport)
{
	(void)transport;
	assert(0);
	return INTEL_AX211_TRANSPORT_IO;
}

/*
 * Never reached: a session start publishes RX descriptors.
 */
int
drv_intel_ax211_transport_publish_rx_descriptor(
	struct intel_ax211_transport *transport,
	uint16_t index,
	uint64_t device_address)
{
	(void)transport;
	(void)index;
	(void)device_address;
	assert(0);
	return INTEL_AX211_TRANSPORT_IO;
}

/*
 * Never reached: a session start enables firmware interrupts.
 */
int
drv_intel_ax211_transport_enable_firmware_interrupts(
	struct intel_ax211_transport *transport)
{
	(void)transport;
	assert(0);
	return INTEL_AX211_TRANSPORT_IO;
}

/*
 * Never reached: a session start enables runtime interrupts.
 */
int
drv_intel_ax211_transport_enable_runtime_interrupts(
	struct intel_ax211_transport *transport)
{
	(void)transport;
	assert(0);
	return INTEL_AX211_TRANSPORT_IO;
}

/*
 * Never reached: the running fixture has no command transaction.
 */
int
drv_intel_ax211_command_transaction_init(
	struct intel_ax211_command_transaction *transaction,
	struct intel_ax211_transport *transport,
	size_t max_pending,
	uint32_t hardware_epoch)
{
	(void)transaction;
	(void)transport;
	(void)max_pending;
	(void)hardware_epoch;
	assert(0);
	return INTEL_AX211_COMMAND_INVALID;
}

/*
 * Never reached: the running fixture submits no command.
 */
int
drv_intel_ax211_command_submit(
	struct intel_ax211_command_transaction *transaction,
	const struct intel_ax211_command_request *request,
	uint64_t now,
	uint64_t timeout,
	struct intel_ax211_command_handle *handle)
{
	(void)transaction;
	(void)request;
	(void)now;
	(void)timeout;
	(void)handle;
	assert(0);
	return INTEL_AX211_COMMAND_INVALID;
}

/*
 * Never reached: the running fixture submits no command.
 */
int
drv_intel_ax211_command_submit_nvm_access_complete(
	struct intel_ax211_command_transaction *transaction,
	uint64_t now,
	uint64_t timeout,
	struct intel_ax211_command_handle *handle)
{
	(void)transaction;
	(void)now;
	(void)timeout;
	(void)handle;
	assert(0);
	return INTEL_AX211_COMMAND_INVALID;
}

/*
 * Never reached: the running fixture completes no command.
 */
int
drv_intel_ax211_command_complete(
	struct intel_ax211_command_transaction *transaction,
	const uint8_t *event_bytes,
	size_t event_length,
	uint32_t hardware_epoch,
	void *response,
	size_t response_capacity,
	size_t *response_length)
{
	(void)transaction;
	(void)event_bytes;
	(void)event_length;
	(void)hardware_epoch;
	(void)response;
	(void)response_capacity;
	(void)response_length;
	assert(0);
	return INTEL_AX211_COMMAND_INVALID;
}

/*
 * Never reached: the running fixture times out no command.
 */
int
drv_intel_ax211_command_timeout_oldest(
	struct intel_ax211_command_transaction *transaction,
	uint64_t now,
	struct intel_ax211_command_handle *handle)
{
	(void)transaction;
	(void)now;
	(void)handle;
	assert(0);
	return INTEL_AX211_COMMAND_INVALID;
}

/*
 * Never reached: the running fixture has no command transaction to reset.
 */
int
drv_intel_ax211_command_after_device_reset(
	struct intel_ax211_command_transaction *transaction,
	uint32_t hardware_epoch)
{
	(void)transaction;
	(void)hardware_epoch;
	assert(0);
	return INTEL_AX211_COMMAND_INVALID;
}

/*
 * Runs every stop row.
 */
int
main(void)
{
	static const struct test_row rows[] = {
		{ "healthy stop", 0, INTEL_AX211_TRANSPORT_OK, 0, 0, 0,
		  INTEL_AX211_RUNTIME_START_OK, "iqsMzf", -1, NULL },
		{ "outstanding command is retired by the reset", 0,
		  INTEL_AX211_TRANSPORT_FAILED, 0, 0, 0,
		  INTEL_AX211_RUNTIME_START_OK, "iqsMzf", -1, NULL },
		{ "RX DMA not idle still frees after reset", 0,
		  INTEL_AX211_TRANSPORT_TIMEOUT, 0, 0, 0,
		  INTEL_AX211_RUNTIME_START_TRANSPORT, "iqsMzf", -1, NULL },
		{ "drain failure keeps bus mastering and DMA", 1,
		  INTEL_AX211_TRANSPORT_OK, 0, 0, 0,
		  INTEL_AX211_RUNTIME_START_STOP_REQUIRED, "iqs",
		  INTEL_AX211_RUNTIME_START_OK, "iqsMzf" },
		{ "reset failure keeps bus mastering and DMA", 0,
		  INTEL_AX211_TRANSPORT_OK, 1, 0, 0,
		  INTEL_AX211_RUNTIME_START_STOP_REQUIRED, "iqs",
		  INTEL_AX211_RUNTIME_START_OK, "iqsMzf" },
		{ "missed master-disable defers bus mastering and DMA", 0,
		  INTEL_AX211_TRANSPORT_OK, 0, 1, 0,
		  INTEL_AX211_RUNTIME_START_STOP_REQUIRED, "iqs",
		  INTEL_AX211_RUNTIME_START_OK, "iqsMzf" },
		{ "refused bus-master disable keeps DMA", 0,
		  INTEL_AX211_TRANSPORT_OK, 0, 0, 1,
		  INTEL_AX211_RUNTIME_START_STOP_REQUIRED, "iqsM",
		  INTEL_AX211_RUNTIME_START_OK, "iqsMzf" }
	};
	size_t index;

	/* Runs each row on a fresh running session. */
	for (index = 0U; index < sizeof(rows) / sizeof(rows[0]); index++)
		test_row_run(&rows[index]);

	printf("intel ax211 runtime stop: PASS\n");
	return 0;
}

/* Appends one step to the active fixture's trace. */
static void
test_trace(
	char value)
{
	assert(active_fixture != NULL);
	assert(active_fixture->trace_length + 1U <
	       sizeof(active_fixture->trace));

	active_fixture->trace[active_fixture->trace_length] = value;
	active_fixture->trace_length++;
	active_fixture->trace[active_fixture->trace_length] = '\0';
}

/* Never reached: a session start begins a receive epoch. */
static int
test_receive_epoch_begin(
	void *argument,
	uint32_t generation)
{
	(void)argument;
	(void)generation;
	assert(0);
	return -1;
}

/* Never reached: a session start binds the transport. */
static int
test_transport_bind(
	void *argument,
	struct intel_ax211_dma_resources *dma,
	struct intel_ax211_mmio *mmio,
	struct intel_ax211_transport *transport,
	uint32_t generation)
{
	(void)argument;
	(void)dma;
	(void)mmio;
	(void)transport;
	(void)generation;
	assert(0);
	return -1;
}

/* Never reached: a session start receives firmware events. */
static int
test_receive_event(
	void *argument,
	uint64_t deadline_us,
	uint8_t *bytes,
	size_t capacity,
	struct intel_ax211_boot_received_event *event)
{
	(void)argument;
	(void)deadline_us;
	(void)bytes;
	(void)capacity;
	(void)event;
	assert(0);
	return -1;
}

/* Never reached: a session start publishes the PNVM. */
static int
test_publish_pnvm(
	void *argument,
	struct intel_ax211_dma_resources *dma)
{
	(void)argument;
	(void)dma;
	assert(0);
	return -1;
}

/* Never reached: a session start accepts ALIVE. */
static int
test_post_alive(
	void *argument,
	const struct intel_ax211_protocol_alive *alive)
{
	(void)argument;
	(void)alive;
	assert(0);
	return -1;
}

/* Drains the fake interrupt handler with the row's result. */
static int
test_interrupt_drain(
	void *argument)
{
	struct test_fixture *fixture;

	fixture = argument;
	assert(fixture == active_fixture);

	test_trace('i');

	/* Fails only the first drain, so the cleanup retry succeeds. */
	if (fixture->row->drain_fails && fixture->stop_count == 0)
		return -1;

	return 0;
}

/* Turns fake bus mastering off with the row's result. */
static int
test_bus_master_disable(
	void *argument)
{
	struct test_fixture *fixture;

	fixture = argument;
	assert(fixture == active_fixture);

	test_trace('M');

	/* Refuses only the first disable, so the cleanup retry succeeds. */
	if (fixture->row->bus_master_fails && fixture->stop_count == 1)
		return -1;

	return 0;
}

/* Never reached: a session start reads the clock. */
static int
test_clock_us(
	void *argument,
	uint64_t *time_us)
{
	(void)argument;
	(void)time_us;
	assert(0);
	return -1;
}

/* Never reached: the running fixture holds no NIC lock. */
static int
test_nic_lock(
	void *argument)
{
	(void)argument;
	assert(0);
	return -1;
}

/* Never reached: the running fixture holds no NIC lock. */
static int
test_nic_unlock(
	void *argument)
{
	(void)argument;
	assert(0);
	return -1;
}

/*
 * Places a session in the running state with exposed DMA and a bound
 * transport, as a successful start leaves it.
 */
static void
test_fixture_running(
	struct test_fixture *fixture,
	const struct test_row *row)
{
	memset(fixture, 0, sizeof(*fixture));
	fixture->row = row;
	active_fixture = fixture;

	/* Installs the PCI seams the stop calls. */
	fixture->ops.boot.receive_epoch_begin = test_receive_epoch_begin;
	fixture->ops.boot.transport_bind = test_transport_bind;
	fixture->ops.boot.receive_event = test_receive_event;
	fixture->ops.boot.publish_pnvm = test_publish_pnvm;
	fixture->ops.boot.post_alive = test_post_alive;
	fixture->ops.boot.interrupt_drain = test_interrupt_drain;
	fixture->ops.boot.bus_master_disable = test_bus_master_disable;
	fixture->ops.boot.clock_us = test_clock_us;
	fixture->ops.nic_lock = test_nic_lock;
	fixture->ops.nic_unlock = test_nic_unlock;

	/* Leaves the session as a completed start does. */
	fixture->session.ops = &fixture->ops;
	fixture->session.argument = fixture;
	fixture->session.mmio = &fixture->mmio;
	fixture->session.transport = &fixture->transport;
	fixture->session.generation = 7U;
	fixture->session.dma_prepared = 1U;
	fixture->session.dma_exposed = 1U;
	fixture->session.hardware_touched = 1U;
	fixture->session.transport_bound = 1U;
	fixture->session.state = INTEL_AX211_RUNTIME_START_STATE_RUNNING;
}

/* Runs one row's stop, and its cleanup retry when the stop retained DMA. */
static void
test_row_run(
	const struct test_row *row)
{
	struct test_fixture fixture;
	size_t first_length;
	int result;

	/* Stops the running session. */
	test_fixture_running(&fixture, row);
	result = drv_intel_ax211_runtime_start_stop(&fixture.session);
	if (result != row->expected_stop ||
	    strcmp(fixture.trace, row->expected_trace) != 0) {
		fprintf(stderr, "%s: stop result=%d trace=%s\n", row->name,
			result, fixture.trace);
		assert(0);
	}

	/* A completed stop leaves nothing retained. */
	if (row->expected_cleanup_trace == NULL) {
		assert(fixture.dma_released);
		assert(fixture.session.state ==
		       INTEL_AX211_RUNTIME_START_STATE_IDLE);
		active_fixture = NULL;
		return;
	}

	/* A retained stop kept the DMA and bus mastering for the retry. */
	assert(!fixture.dma_released);
	assert(fixture.session.dma_prepared);
	assert(fixture.session.state ==
	       INTEL_AX211_RUNTIME_START_STATE_STOP_REQUIRED);

	/* Retries the stop and expects the full ordered sequence. */
	first_length = fixture.trace_length;
	result = drv_intel_ax211_runtime_start_cleanup(&fixture.session);
	if (result != row->expected_cleanup ||
	    strcmp(fixture.trace + first_length,
		   row->expected_cleanup_trace) != 0) {
		fprintf(stderr, "%s: cleanup result=%d trace=%s\n", row->name,
			result, fixture.trace + first_length);
		assert(0);
	}
	assert(fixture.dma_released);
	assert(fixture.session.state == INTEL_AX211_RUNTIME_START_STATE_IDLE);
	active_fixture = NULL;
}
