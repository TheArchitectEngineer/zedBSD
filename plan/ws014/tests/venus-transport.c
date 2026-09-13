/* -*- mode: c; c-file-style: "linux"; tab-width: 8; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Exercises modern Venus PCI transport using a synchronous split-queue peer.
 *
 * Capability slices intentionally have subpage lengths. The fixture checks
 * whole-BAR mapping, queue wrap, malformed completion and reset-safe DMA.
 */

#include <stdint.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Host process typedefs stay distinct from the guest scheduler namespace. */
typedef int32_t tid_t;
#define sigset_t transport_kernel_sigset_t
#include "../../../src/drivers/gpu/venus/transport.c"
#undef sigset_t

/* Holds one deterministic PCI configuration image during each scenario. */
static uint8_t fixture_configuration[256];

/* Holds the complete modern register BAR which capability windows borrow. */
static uint8_t fixture_registers[16384];

/* Represents host visibility as a whole separately mapped BAR. */
static uint8_t fixture_aperture[8192];

/* Selects an oversized BAR to verify rejection before platform mapping. */
static unsigned fixture_oversized_aperture;

/* Records claims so initialization failures must unwind acquired BAR owners. */
static unsigned fixture_claims[6];

/* Counts complete BAR mappings, independently of their borrowed slices. */
static unsigned fixture_maps;

/* Counts live queue allocations until acknowledged reset permits freeing. */
static unsigned fixture_dma;

/* Selects the sole transport whose descriptors are visible to the peer. */
static struct venus_transport *fixture_transport;

/* Drops completions to make the bounded deadline path observable. */
static unsigned fixture_drop;

/* Corrupts one returned descriptor identity to test completion validation. */
static unsigned fixture_bad_descriptor;

/* Refuses reset acknowledgment so cleanup must preserve all DMA allocations. */
static unsigned fixture_reset_busy;

/* Advances synthetic time in finite steps without delaying the host test. */
static uint64_t fixture_clock;

/* Counts clock samples within one configured delayed-completion scenario. */
static uint32_t fixture_clock_calls;

/* Holds time fixed to exercise the pre-tick fallback without a real timer. */
static unsigned fixture_clock_frozen;

/* Models a running clock whose next millisecond needs many short polling loads. */
static unsigned fixture_clock_slow;

/* Publishes the delayed response only after this clock-sample count is reached. */
static uint32_t fixture_delayed_completion;

/* Selects the independently advertised optional EDID feature for negotiation coverage. */
static unsigned fixture_edid;

/* A capability payload is supplied independently from its descriptor's advertised length. */
static uint32_t fixture_capset_bytes;
static uint32_t fixture_vendor_magic;
static uint32_t fixture_vendor_flags;

/* Counts allocations to prove that committing retained jobs cannot allocate new storage. */
static unsigned fixture_calloc_calls;

/* Ends one directly invoked worker after either a control fault or a healthy idle snapshot. */
static unsigned fixture_watchdog_stop;

/* Models only references owned by transport publication and fault snapshots. */
static unsigned fixture_gpu_references;

/* Withdraws publication during the unlocked fault callback to exercise its retained snapshot. */
static unsigned fixture_withdraw_report;

/* Counts real-capacity publication after backend storage retirement. */
static unsigned fixture_capacity_wakes;
static unsigned fixture_capacity_available;
static unsigned fixture_callback_idle_context;

static void fixture_quiesce_proposal(void);
static void fixture_isolation(void);
static void fixture_prepare(struct venus_transport *transport);
static void fixture_capability(unsigned offset, unsigned next, unsigned length, unsigned type, unsigned bar, uint32_t start, uint32_t bytes);
static void fixture_complete(void);
static void fixture_complete_head(uint16_t head);
static void fixture_notifications(void);
static void fixture_display_events(enum drv_pci_irq_type type);
static void fixture_queue(void);
static void fixture_failures(void);
static void fixture_async_wait(void);
static void fixture_edid_feature(void);
static void fixture_strict_capability(void);
static void fixture_jobs(void);
static void fixture_job_failures(void);

#include "venus-transport-peer.inc"

/*
 * Reads a bounded byte from the synthetic conventional PCI configuration.
 */
int
drv_pci_device_config_read8(
	struct drv_pci_device *device,
	unsigned offset,
	uint8_t *byte)
{
	/* Every parser read must remain within the advertised configuration space. */
	(void)device;
	assert(offset < sizeof(fixture_configuration));
	*byte = fixture_configuration[offset];

	/* Succeeded: the transport receives one device-controlled configuration byte. */
	return 0;
}

/*
 * Reads a bounded word from the synthetic conventional PCI configuration.
 */
int
drv_pci_device_config_read32(
	struct drv_pci_device *device,
	unsigned offset,
	uint32_t *word)
{
	/* This peer exposes only conventional configuration with aligned word reads. */
	(void)device;
	assert(offset <= sizeof(fixture_configuration) - 4U);
	assert((offset & 3U) == 0U);
	*word = drv_venus_load32(fixture_configuration + offset);

	/* Succeeded: the parser receives the complete little-endian field. */
	return 0;
}

/*
 * Supplies the synthetic PCI parent's coherent DMA provider identity.
 */
struct drv_dma_device *
drv_pci_device_dma(
	struct drv_pci_device *device)
{
	/* No physical address translation is needed by this synchronous host peer. */
	(void)device;

	/* Succeeded: the nonnull provider identity permits queue allocation. */
	return (struct drv_dma_device *)&fixture_dma;
}

/*
 * Saves the PCI command ownership token before transport acquisition.
 */
int
drv_pci_device_save_enable_state(
	struct drv_pci_device *device,
	struct drv_pci_enable_state *state)
{
	/* The fixture token is consumed once by successful final cleanup. */
	(void)device;
	assert(state->private_data[0] == 0U);
	state->private_data[0] = 1U;

	/* Succeeded: the transport owns a restorable command state. */
	return 0;
}

/*
 * Restores the synthetic command state after device ownership has ended.
 */
int
drv_pci_device_restore_enable_state(
	struct drv_pci_device *device,
	struct drv_pci_enable_state *state)
{
	/* Each saved lease must be consumed exactly once. */
	(void)device;
	assert(state->private_data[0] == 1U);
	state->private_data[0] = 0U;

	/* Succeeded: no command-register lease remains owned. */
	return 0;
}

/*
 * Permits decoding of the fixture's mapped modern registers.
 */
int
drv_pci_device_enable_memory(
	struct drv_pci_device *device)
{
	/* Memory decoding has no asynchronous side effect in this peer. */
	(void)device;

	/* Succeeded: capability register mappings may be acquired. */
	return 0;
}

/*
 * Accepts ordered bus-master changes around coherent queue ownership.
 */
int
drv_pci_device_set_bus_master(
	struct drv_pci_device *device,
	bool enabled)
{
	/* Queue allocation and lifetime counters assert the meaningful ownership. */
	(void)device;
	(void)enabled;

	/* Succeeded: the synthetic PCI permission transition was accepted. */
	return 0;
}

/*
 * Supplies a register BAR and a distinct host-visible aperture descriptor.
 */
int
drv_pci_device_bar(
	const struct drv_pci_device *device,
	unsigned index,
	struct drv_pci_bar *bar)
{
	/* Only capability-declared BARs exist in this fixture. */
	(void)device;
	if (index != 2U && index != 4U)
		return EINVAL;

	/* Initializes complete BAR identity before selecting its bounded size. */
	memset(bar, 0, sizeof(*bar));
	bar->index = index;
	bar->type = DRV_PCI_BAR_MEMORY32;
	bar->bus_address = 0xf0800000U;
	bar->size = sizeof(fixture_registers);

	/* Host visibility remains separate from the small register mapping. */
	if (index == 4U) {
		bar->bus_address = 0xf0900000U;
		bar->size = sizeof(fixture_aperture);

		/* A 512MiB BAR exceeds the current 256MiB driver limit before any platform mapping. */
		if (fixture_oversized_aperture != 0U)
			bar->size = 512U * 1024U * 1024U;
	}

	/* Succeeded: the parser can validate the entire capability extent. */
	return 0;
}

/*
 * Claims one synthetic BAR without allowing duplicate ownership.
 */
int
drv_pci_device_claim_bar(
	struct drv_pci_device *device,
	unsigned index)
{
	/* Multiple capabilities must share one claim for their containing BAR. */
	(void)device;
	assert(index < 6U);
	assert(fixture_claims[index] == 0U);
	fixture_claims[index] = 1U;

	/* Succeeded: the transport retains the selected BAR. */
	return 0;
}

/*
 * Returns one synthetic BAR claim during final transport cleanup.
 */
void
drv_pci_device_release_bar(
	struct drv_pci_device *device,
	unsigned index)
{
	/* Every release must correspond to an earlier successful claim. */
	(void)device;
	assert(index < 6U);
	assert(fixture_claims[index] == 1U);
	fixture_claims[index] = 0U;

	/* Succeeded: another driver could now claim the synthetic BAR. */
	return;
}

/*
 * Maps the complete modern register BAR exactly once.
 */
int
drv_pci_device_map_bar(
	struct drv_pci_device *device,
	unsigned index,
	unsigned flags,
	struct drv_pci_mapping *mapping)
{
	/* Subpage capability lengths must never become independent MMIO mappings. */
	(void)device;
	assert(index == 2U || index == 4U);
	assert((flags & DRV_PCI_MAP_WRITE) != 0U);
	mapping->type = DRV_PCI_BAR_MEMORY32;

	/* Register and host-memory BARs each receive one complete mapping owner. */
	if (index == 2U) {
		assert(fixture_maps == 0U);
		mapping->address = fixture_registers;
		mapping->size = sizeof(fixture_registers);
	} else {
		assert(fixture_maps == 1U);
		assert(fixture_oversized_aperture == 0U);
		mapping->address = fixture_aperture;
		mapping->size = sizeof(fixture_aperture);
	}

	/* Counts complete BAR owners independently of their borrowed views. */
	fixture_maps++;

	/* Succeeded: capability and blob views may borrow checked slices. */
	return 0;
}

/*
 * Releases the sole complete BAR mapping after reset and DMA retirement.
 */
void
drv_pci_device_unmap_bar(
	struct drv_pci_device *device,
	struct drv_pci_mapping *mapping)
{
	/* A borrowed slice must never be submitted as an independent mapping owner. */
	(void)device;
	assert(fixture_dma == 0U);

	/* The host aperture retires before the register mapping during normal teardown. */
	if (mapping->address == fixture_aperture) {
		assert(mapping->size == sizeof(fixture_aperture));
		assert(fixture_maps == 2U);
	} else {
		assert(mapping->address == fixture_registers);
		assert(mapping->size == sizeof(fixture_registers));
		assert(fixture_maps == 1U);
	}

	/* Invalidates the complete owner after checking its exact mapping extent. */
	memset(mapping, 0, sizeof(*mapping));
	fixture_maps--;

	/* Succeeded: no capability or blob view can outlive this complete BAR mapping. */
	return;
}

/*
 * Allocates coherent descriptor storage using directly addressable host memory.
 */
int
drv_dma_alloc_coherent(
	struct drv_dma_device *device,
	size_t bytes,
	size_t alignment,
	struct drv_dma_buffer *buffer)
{
	/* Heap alignment suffices for the naturally aligned queue index fixture. */
	(void)device;
	(void)alignment;
	buffer->address = calloc(1U, bytes);
	if (buffer->address == NULL)
		return ENOMEM;

	/* Counts each descriptor, command and response allocation separately. */
	buffer->device_address = (uintptr_t)buffer->address;
	buffer->size = bytes;
	fixture_dma++;

	/* Succeeded: the transport owns one device-visible allocation. */
	return 0;
}

/*
 * Releases coherent queue storage after the checked reset barrier.
 */
void
drv_dma_free_coherent(
	struct drv_dma_device *device,
	struct drv_dma_buffer *buffer)
{
	/* No storage may be freed while reset acknowledgment is withheld. */
	(void)device;
	assert(fixture_reset_busy == 0U);
	assert(fixture_dma != 0U);
	free(buffer->address);
	memset(buffer, 0, sizeof(*buffer));
	fixture_dma--;

	/* Succeeded: this coherent allocation has fully retired. */
	return;
}

/*
 * Reads one modern byte register from the synthetic BAR.
 */
uint8_t
kern_mmio_read8(
	const volatile void *address)
{
	uint8_t byte;

	/* Reading the Virtio ISR acknowledges this function without clearing device events. */
	byte = *(const volatile uint8_t *)address;
	if (address == fixture_registers + 4096U)
		fixture_registers[4096U] = 0U;

	/* Succeeded: ordinary byte registers retain their existing device state. */
	return byte;
}

/*
 * Reads one modern halfword register from the synthetic BAR.
 */
uint16_t
kern_mmio_read16(
	const volatile void *address)
{
	uint16_t word;

	/* Uses the production little-endian helper for fixture register storage. */
	word = drv_venus_load16(address);

	/* Succeeded: returns the selected queue register. */
	return word;
}

/*
 * Reads feature words according to the modern feature-selector register.
 */
uint32_t
kern_mmio_read32(
	const volatile void *address)
{
	uint32_t selector;
	uint32_t word;

	/* Feature word one advertises VERSION_1; word zero advertises Venus support. */
	if (address == fixture_registers + 4U) {
		selector = drv_venus_load32(fixture_registers);
		if (selector == 0U) {
			/* The optional EDID bit is separate from the mandatory Venus baseline. */
			if (fixture_edid != 0U)
				return 0x1bU;
			return 0x19U;
		}

		/* The higher feature word contains the modern protocol requirement. */
		return 1U;
	}

	/* Other fields read their currently initialized fixture bytes. */
	word = drv_venus_load32(address);

	/* Succeeded: the transport sees the selected modern configuration word. */
	return word;
}

/*
 * Writes device status while supporting a deliberately withheld reset.
 */
void
kern_mmio_write8(
	volatile void *address,
	uint8_t byte)
{
	/* A busy reset leaves DRIVER_OK visible and blocks safe queue retirement. */
	if (address == fixture_registers + 20U &&
	    byte == 0U &&
	    fixture_reset_busy != 0U)
		return;

	/* Updates the synthetic status visible to the next transport poll. */
	*(volatile uint8_t *)address = byte;

	/* Succeeded: the requested status is visible to subsequent register reads. */
	return;
}

/*
 * Writes a queue field or consumes a notification through the split-queue peer.
 */
void
kern_mmio_write16(
	volatile void *address,
	uint16_t word)
{
	/* A queue-zero notification permits the peer to read the published chain. */
	if (address == fixture_registers + 12288U) {
		assert(word == 0U);
		fixture_complete();
		return;
	}

	/* Ordinary queue configuration is retained in its little-endian BAR field. */
	drv_venus_store16((void *)address, word);

	/* Succeeded: the queue register contains the new configuration. */
	return;
}

/*
 * Writes one little-endian modern configuration field.
 */
void
kern_mmio_write32(
	volatile void *address,
	uint32_t word)
{
	/* Retains queue addresses and feature selections in fixture register memory. */
	drv_venus_store32((void *)address, word);

	/* Succeeded: the peer observes the updated register field. */
	return;
}

/*
 * Supplies monotonic time without making timeout tests wait in real time.
 */
uint64_t
clock_milliseconds(
	void *context)
{
	/* The callback argument carries no peer identity in this serialized fixture. */
	(void)context;

	/* Count samples independently of whether the modeled timer is advancing. */
	fixture_clock_calls++;

	/* Complete delayed host teardown only after the original total-poll cap. */
	if (fixture_delayed_completion != 0U) {
		/* Deliver the response once, after the transport has waited long enough. */
		if (fixture_clock_calls == fixture_delayed_completion) {
			fixture_delayed_completion = 0U;
			fixture_drop = 0U;
			fixture_complete();
		}
	}

	/* Frozen-clock scenarios rely exclusively on the finite fallback bound. */
	if (fixture_clock_frozen == 0U) {
		/* Ordinary failures advance in seconds; asynchronous waits use slow ticks. */
		if (fixture_clock_slow == 0U) {
			fixture_clock += 1000U;
		} else {
			/* Many fast polling iterations fit inside one real clock interval. */
			if (fixture_clock_calls % 1000000U == 0U)
				fixture_clock++;
		}
	}

	/* Succeeded: the transport receives this scenario's monotonic timestamp. */
	return fixture_clock;
}

/*
 * Preserves full I/O ordering boundaries in the sequential test peer.
 */
void
kern_io_barrier(void)
{
	/* Succeeded: every prior peer operation is already visible synchronously. */
	return;
}

/*
 * Preserves I/O read acquisition in the sequential test peer.
 */
void
kern_io_read_barrier(void)
{
	/* Succeeded: response bytes precede the synthetic completion publication. */
	return;
}

/*
 * Preserves I/O write publication in the sequential test peer.
 */
void
kern_io_write_barrier(void)
{
	/* Succeeded: the peer reads only after the explicit queue notification. */
	return;
}

/*
 * Keeps the bounded poll path callable without a real asynchronous device.
 */
void
kern_compiler_barrier(void)
{
	/* Succeeded: synthetic time, rather than compiler folding, ends the poll. */
	return;
}

/*
 * Accepts transport diagnostics without matching test outcomes to log wording.
 */
void
kern_logf(
	const char *format,
	...)
{
	/* State and allocation assertions carry this fixture's observable evidence. */
	(void)format;

	/* Succeeded: logging requires no fixture-side resource ownership. */
	return;
}

/*
 * Runs production transport acquisition, queue and rollback checks.
 */
int
main(void)
{
	/* Strict completion requires the exact paired profile rather than a guessed suffix. */
	fixture_quiesce_proposal();
	fixture_isolation();
	fixture_strict_capability();
	fixture_jobs();
	fixture_job_failures();

	/* Optional native metadata is accepted only when the device offers the EDID feature. */
	fixture_edid_feature();

	/* Checks complete BAR mapping and repeated descriptor/index reuse. */
	fixture_queue();

	/* Checks parser refusal, malformed completions and delayed reset cleanup. */
	fixture_failures();

	/* Distinguish asynchronously advancing time from a stopped pre-tick clock. */
	fixture_async_wait();

	/* IRQ completion is independently matched across concurrent renderer timelines. */
	fixture_notifications();

	/* Display invalidations remain independent from commands under both PCI delivery modes. */
	fixture_display_events(DRV_PCI_IRQ_INTX);
	fixture_display_events(DRV_PCI_IRQ_MSIX);

	/* No scenario may leave coherent memory or register mappings live. */
	assert(fixture_dma == 0U);
	assert(fixture_maps == 0U);
	puts("Venus transport: whole-BAR/capset/queue-wrap/async-wait/stalled-clock/timeout/reset PASS");

	/* Succeeded: the real transport satisfied its bounded peer contracts. */
	return 0;
}

/* Initializes capabilities with subpage slices sharing one complete register BAR. */
static void
fixture_prepare(
	struct venus_transport *transport)
{
	/* Resets each independent device image and its retained ownership counters. */
	memset(transport, 0, sizeof(*transport));
	memset(fixture_configuration, 0, sizeof(fixture_configuration));
	memset(fixture_registers, 0, sizeof(fixture_registers));
	memset(fixture_claims, 0, sizeof(fixture_claims));
	fixture_transport = transport;
	fixture_drop = 0U;
	fixture_bad_descriptor = 0U;
	fixture_reset_busy = 0U;
	fixture_oversized_aperture = 0U;
	fixture_clock = 0U;
	fixture_clock_calls = 0U;
	fixture_clock_frozen = 0U;
	fixture_clock_slow = 0U;
	fixture_delayed_completion = 0U;
	fixture_edid = 0U;
	fixture_capset_bytes = 32U;
	fixture_vendor_magic = 0U;
	fixture_vendor_flags = 0U;
	fixture_calloc_calls = 0U;
	fixture_watchdog_stop = 0U;
	fixture_withdraw_report = 0U;
	fixture_capacity_wakes = 0U;
	fixture_capacity_available = 0U;
	fixture_callback_idle_context = 0U;
	assert(fixture_gpu_references == 0U);
	fixture_runtime = 0U;
	fixture_peer_available = 0U;
	fixture_reports = 0U;
	fixture_irq_type = DRV_PCI_IRQ_MSIX;
	fixture_poll_wakes = 0U;

	/* Describes common, notify and device slices plus separate host visibility. */
	fixture_configuration[0x34U] = 0x40U;
	fixture_capability(0x40U, 0x50U, 16U, 1U, 2U, 0U, 2048U);
	fixture_capability(0x50U, 0x64U, 20U, 2U, 2U, 12288U, 2048U);
	drv_venus_store32(fixture_configuration + 0x60U, 4U);
	fixture_capability(0x64U, 0x74U, 16U, 4U, 2U, 8192U, 16U);
	fixture_capability(0x74U, 0x8cU, 24U, 8U, 4U, 0U, 8192U);
	fixture_configuration[0x79U] = 1U;
	fixture_capability(0x8cU, 0U, 16U, 3U, 2U, 4096U, 1U);

	/* Advertises one usable queue, one scanout and one Venus capability set. */
	drv_venus_store16(fixture_registers + 24U, 8U);
	drv_venus_store32(fixture_registers + 8192U + 8U, 1U);
	drv_venus_store32(fixture_registers + 8192U + 12U, 1U);

	/* Succeeded: the transport may parse and initialize this synthetic device. */
	return;
}

/* Encodes one synthetic vendor capability using its standardized wire prefix. */
static void
fixture_capability(
	unsigned offset,
	unsigned next,
	unsigned length,
	unsigned type,
	unsigned bar,
	uint32_t start,
	uint32_t bytes)
{
	/* Writes identity, next-link and the bounded window descriptor. */
	fixture_configuration[offset] = 9U;
	fixture_configuration[offset + 1U] = (uint8_t)next;
	fixture_configuration[offset + 2U] = (uint8_t)length;
	fixture_configuration[offset + 3U] = (uint8_t)type;
	fixture_configuration[offset + 4U] = (uint8_t)bar;
	drv_venus_store32(fixture_configuration + offset + 8U, start);
	drv_venus_store32(fixture_configuration + offset + 12U, bytes);

	/* Succeeded: the production parser can consume this capability. */
	return;
}

/* Consumes the actual published descriptor chain and writes a used-ring entry. */
static void
fixture_complete(void)
{
	struct venus_transport *transport;
	uint8_t *ring;
	uint16_t available;
	uint16_t head;

	/* A dropped chain remains device-owned until completion or checked reset. */
	if (fixture_drop != 0U)
		return;
	transport = fixture_transport;
	ring = transport->ring.address;
	available = drv_venus_load16(ring + VENUS_RING_AVAILABLE + 2U);
	assert(available == transport->available);
	assert(fixture_peer_available != available);
	head = drv_venus_load16(ring + VENUS_RING_AVAILABLE + 4U + (fixture_peer_available % transport->queue_size) * 2U);
	fixture_peer_available++;
	fixture_complete_head(head);
}

/* Completes a device-owned head independently from its availability order. */
static void
fixture_complete_head(
	uint16_t head)
{
	struct venus_transport *transport;
	struct venus_request *request;
	uint8_t *ring;
	uint8_t *response;
	uint32_t type;
	uint32_t bytes;
	uint16_t used;

	/* Only a published descriptor names device-owned request and response storage. */
	transport = fixture_transport;
	ring = transport->ring.address;
	assert(head % 2U == 0U && head < transport->queue_size);
	request = &transport->requests[head / 2U];
	assert(drv_venus_load64(ring + head * 16U) == request->request.device_address);
	assert(drv_venus_load64(ring + (head + 1U) * 16U) == request->response.device_address);
	assert(drv_venus_load16(ring + head * 16U + 12U) == 1U);
	assert(drv_venus_load16(ring + head * 16U + 14U) == head + 1U);
	assert(drv_venus_load16(ring + (head + 1U) * 16U + 12U) == 2U);

	/* Fence/context/ring echoes describe the exact submitted chain, never a canned slot zero. */
	type = drv_venus_load32(request->request.address);
	response = request->response.address;
	memset(response, 0, VENUS_RESPONSE_BYTES);
	memcpy(response + 4U, (uint8_t *)request->request.address + 4U, 20U);
	bytes = 24U;
	drv_venus_store32(response, 0x1100U);
	if (type == 0x108U) {
		bytes = 40U;
		drv_venus_store32(response, 0x1102U);
		drv_venus_store32(response + 24U, 4U);
		drv_venus_store32(response + 32U, fixture_capset_bytes);
	} else if (type == 0x109U) {
		bytes = 24U + fixture_capset_bytes;
		drv_venus_store32(response, 0x1103U);
		drv_venus_store32(response + 24U + 160U, fixture_vendor_magic);
		drv_venus_store32(response + 24U + 164U, fixture_vendor_flags);
	}

	/* Device used order is independent of the driver's last observed used index. */
	used = drv_venus_load16(ring + VENUS_RING_USED + 2U);
	if (fixture_bad_descriptor != 0U)
		head = 1U;
	drv_venus_store32(ring + VENUS_RING_USED + 4U + (used % transport->queue_size) * 8U, head);
	drv_venus_store32(ring + VENUS_RING_USED + 8U + (used % transport->queue_size) * 8U, bytes);
	drv_venus_store16(ring + VENUS_RING_USED + 2U, (uint16_t)(used + 1U));
}

/* Checks initialized capability slices and repeated used/available index turnover. */
static void
fixture_queue(void)
{
	struct venus_transport transport;
	uint8_t command[24];
	uint8_t response[24];
	uint32_t bytes;
	unsigned index;
	int error;

	/* Initializes through the actual parser, feature handshake and split queue. */
	fixture_prepare(&transport);
	error = drv_venus_transport_start(&transport, NULL);
	assert(error == 0);
	assert(fixture_maps == 2U);
	assert(fixture_dma == 9U);
	assert(transport.capset_size == 32U);
	assert(transport.notify.mapping.address == fixture_registers + 12288U);
	assert(transport.configuration.mapping.address == fixture_registers + 8192U);

	/* Crosses both the eight-entry ring boundary and the low index-byte rollover. */
	drv_venus_header(command, 0x201U, 1U);
	for (index = 0; index < 300U; index++) {
		error = drv_venus_transport_command(&transport, command, sizeof(command), response, sizeof(response), &bytes);
		assert(error == 0);
		assert(bytes == 24U);
	}

	/* All issued commands, including initialization, completed exactly once. */
	assert(transport.available == 301U);
	assert(transport.used == 301U);
	error = drv_venus_transport_stop(&transport);
	assert(error == 0);
	assert(fixture_claims[2] == 0U);
	assert(fixture_claims[4] == 0U);

	/* Succeeded: complete BAR ownership and queue reuse both retired cleanly. */
	return;
}

/* Checks malformed capability and completion refusal plus reset-safe timeout cleanup. */
static void
fixture_failures(void)
{
	struct venus_transport transport;
	uint8_t command[24];
	uint8_t response[24];
	uint32_t bytes;
	int error;

	/* A cyclic capability list must fail before mapping or queue allocation. */
	fixture_prepare(&transport);
	fixture_configuration[0x41U] = 0x40U;
	error = drv_venus_transport_start(&transport, NULL);
	assert(error == EINVAL);
	error = drv_venus_transport_stop(&transport);
	assert(error == 0);
	assert(fixture_claims[2] == 0U);
	assert(fixture_dma == 0U);

	/* A 256-MiB aperture is rejected before unsafe subrange relocation is possible. */
	fixture_prepare(&transport);
	fixture_oversized_aperture = 1U;
	error = drv_venus_transport_start(&transport, NULL);
	assert(error == EOPNOTSUPP);
	assert(fixture_maps == 1U);
	assert(fixture_dma == 0U);
	error = drv_venus_transport_stop(&transport);
	assert(error == 0);
	assert(fixture_maps == 0U);

	/* A mismatched used descriptor poisons the queue instead of accepting its reply. */
	fixture_prepare(&transport);
	error = drv_venus_transport_start(&transport, NULL);
	assert(error == 0);
	fixture_bad_descriptor = 1U;
	drv_venus_header(command, 0x201U, 1U);
	error = drv_venus_transport_command(&transport, command, sizeof(command), response, sizeof(response), &bytes);
	assert(error == EIO);
	assert(transport.failed != 0U);
	assert(fixture_dma == 9U);
	error = drv_venus_transport_stop(&transport);
	assert(error == 0);

	/* Missing completion retains every persistent queue allocation until reset. */
	fixture_prepare(&transport);
	error = drv_venus_transport_start(&transport, NULL);
	assert(error == 0);
	fixture_drop = 1U;
	error = drv_venus_transport_command(&transport, command, sizeof(command), response, sizeof(response), &bytes);
	assert(error == ETIMEDOUT);
	assert(fixture_dma == 9U);

	/* A failed reset cannot release even one of the retained coherent allocations. */
	fixture_reset_busy = 1U;
	error = drv_venus_transport_stop(&transport);
	assert(error == EBUSY);
	assert(fixture_dma == 9U);
	assert(fixture_maps == 2U);

	/* The eventual acknowledgment permits the same cleanup operation to finish. */
	fixture_reset_busy = 0U;
	error = drv_venus_transport_stop(&transport);
	assert(error == 0);
	assert(fixture_dma == 0U);
	assert(fixture_maps == 0U);

	/* Succeeded: parser and queue failures preserved correct cleanup ownership. */
	return;
}

/* Wait through delayed host completion while preserving the stopped-clock guard. */
static void
fixture_async_wait(void)
{
	struct venus_transport transport;
	uint8_t command[32];
	uint8_t response[24];
	uint32_t bytes;
	int error;

	/* Establish the normal device before delaying one host resource-unmap reply. */
	fixture_prepare(&transport);
	error = drv_venus_transport_start(&transport, NULL);
	assert(error == 0);

	/* A slow advancing clock permits more than the old aggregate poll cap. */
	fixture_drop = 1U;
	fixture_clock_slow = 1U;
	fixture_clock_calls = 0U;
	fixture_delayed_completion = VENUS_WAIT_POLLS + 4U;
	memset(command, 0, sizeof(command));
	drv_venus_header(command, 0x209U, 1U);
	drv_venus_store32(command + 24U, 3U);
	error = drv_venus_transport_command(&transport, command, sizeof(command), response, sizeof(response), &bytes);
	assert(error == 0);
	assert(fixture_clock_calls > VENUS_WAIT_POLLS);
	assert(transport.failed == 0U);
	assert(transport.available == transport.used);
	assert(fixture_dma == 9U);

	/* Completed asynchronous teardown permits ordinary transport release. */
	error = drv_venus_transport_stop(&transport);
	assert(error == 0);
	assert(fixture_dma == 0U);

	/* Freeze the clock only after initialization has completed successfully. */
	fixture_prepare(&transport);
	error = drv_venus_transport_start(&transport, NULL);
	assert(error == 0);
	fixture_drop = 1U;
	fixture_clock_frozen = 1U;
	fixture_clock_calls = 0U;
	error = drv_venus_transport_command(&transport, command, sizeof(command), response, sizeof(response), &bytes);
	assert(error == ETIMEDOUT);
	assert(fixture_clock_calls >= VENUS_WAIT_POLLS);
	assert(fixture_clock_calls < VENUS_WAIT_POLLS + 16U);
	assert(transport.failed != 0U);
	assert(fixture_dma == 9U);

	/* An unacknowledged reset still cannot free timed-out device-owned DMA. */
	fixture_reset_busy = 1U;
	error = drv_venus_transport_stop(&transport);
	assert(error == EBUSY);
	assert(fixture_dma == 9U);

	/* Only the eventual reset acknowledgment returns the retained allocations. */
	fixture_reset_busy = 0U;
	error = drv_venus_transport_stop(&transport);
	assert(error == 0);
	assert(fixture_dma == 0U);

	/* Succeeded: running and stopped clocks both preserve finite safe ownership. */
	return;
}

/* Negotiates the optional EDID feature without requiring it from older hosts. */
static void
fixture_edid_feature(void)
{
	struct venus_transport transport;
	int error;

	/* An offering host gets precisely the implemented mandatory-plus-EDID feature subset. */
	fixture_prepare(&transport);
	fixture_edid = 1U;
	error = drv_venus_transport_start(&transport, (struct drv_pci_device *)&transport);
	assert(error == 0);
	assert(transport.features == 0x1bU);
	error = drv_venus_transport_stop(&transport);
	assert(error == 0);

	/* A host without EDID retains the existing required-feature-only negotiation. */
	fixture_prepare(&transport);
	error = drv_venus_transport_start(&transport, (struct drv_pci_device *)&transport);
	assert(error == 0);
	assert(transport.features == 0x19U);
	error = drv_venus_transport_stop(&transport);
	assert(error == 0);

	/* Succeeded: optional discovery neither invents support nor makes it mandatory. */
	return;
}

/* Exercises actual IRQ draining across independent slots, contexts and fence timelines. */
static void
fixture_notifications(void)
{
	static const unsigned order[4] = {2U, 3U, 0U, 1U};
	struct venus_transport transport;
	struct drv_gpu_completion completions[5];
	uint64_t fences[4];
	uint16_t heads[4];
	uint8_t *packet;
	uint8_t *ring;
	unsigned index;
	unsigned slot;
	unsigned handled;
	int error;

	/* Pending asynchronous markers must leave one independent decoder chain available. */
	fixture_prepare(&transport);
	error = drv_venus_transport_start(&transport, NULL);
	assert(error == 0);
	fixture_drop = 1U;
	fixture_clock_frozen = 1U;
	memset(completions, 0, sizeof(completions));
	for (index = 0U; index < 3U; index++) {
		error = drv_venus_transport_submit(&transport, 17U + index, NULL, 0U, GPU_COMMAND_CONTEXT_FENCE, index + 1U, &completions[index]);
		assert(error == 0);
	}
	error = drv_venus_transport_submit(&transport, 20U, NULL, 0U, GPU_COMMAND_CONTEXT_FENCE, 4U, &completions[4]);
	assert(error == EAGAIN);
	assert(completions[4].calls == 0U);
	error = drv_venus_transport_submit(&transport, 20U, NULL, 0U, GPU_COMMAND_CONTEXT_FENCE, 0U, &completions[3]);
	assert(error == 0);

	/* The published ring identifies four distinct chains with FENCE and INFO_RING_IDX. */
	ring = transport.ring.address;
	for (index = 0U; index < 4U; index++) {
		heads[index] = drv_venus_load16(ring + VENUS_RING_AVAILABLE + 4U + ((fixture_peer_available + index) % transport.queue_size) * 2U);
		assert(heads[index] == index * 2U);
		packet = transport.requests[index].request.address;
		assert(drv_venus_load32(packet + 4U) == 3U);
		assert(drv_venus_load32(packet + 16U) == 17U + index);
		assert(packet[20U] == (index == 3U ? 0U : index + 1U));
		fences[index] = drv_venus_load64(packet + 8U);
		assert(fences[index] != 0U);
		if (index != 0U)
			assert(fences[index] > fences[index - 1U]);
		assert(completions[index].calls == 0U);
	}
	fixture_peer_available = transport.available;

	/* Host completion order crosses contexts and differs from descriptor publication order. */
	for (index = 0U; index < 4U; index++) {
		slot = order[index];
		fixture_complete_head(heads[slot]);
		handled = fixture_irq(fixture_irq_argument);
		assert(handled == 1U);
		assert(completions[slot].calls == 1U);
		assert(completions[slot].error == 0);
	}
	assert(transport.available == transport.used);
	handled = fixture_irq(fixture_irq_argument);
	assert(handled == 1U);
	for (index = 0U; index < 4U; index++)
		assert(completions[index].calls == 1U);
	assert(transport.interrupt_total == 5U);

	/* Ordinary runtime waits sleep and let the IRQ callback publish their response. */
	fixture_drop = 0U;
	fixture_runtime = 1U;
	{
		uint8_t command[24];
		uint8_t response[24];
		uint32_t bytes;
		drv_venus_header(command, 0x201U, 99U);
		error = drv_venus_transport_command(&transport, command, sizeof(command), response, sizeof(response), &bytes);
		assert(error == 0 && bytes == 24U);
	}
	assert(transport.sleep_total != 0U);
	fixture_runtime = 0U;
	error = drv_venus_transport_stop(&transport);
	assert(error == 0);
	assert(fixture_irq == NULL);
	assert(fixture_dma == 0U);
	puts("Venus IRQ: out-of-order contexts, exact-once callbacks, fence/ring identity, 3 markers + reserved decoder and runtime sleeping PASS");
}

/* Exercises topology invalidation without confusing it with command completion. */
static void
fixture_display_events(
	enum drv_pci_irq_type type)
{
	struct venus_transport transport;
	struct drv_gpu_completion completion;
	uint8_t *ring;
	uint64_t completed;
	unsigned long irq;
	uint16_t head;
	int handled;
	int error;

	/* Each PCI mechanism begins with one unacknowledged inventory generation. */
	fixture_prepare(&transport);
	fixture_irq_type = type;
	error = drv_venus_transport_start(&transport, NULL);
	assert(error == 0);
	assert(transport.topology_sequence == 1U);
	assert(transport.topology_overflow == 0U);
	assert(fixture_poll_wakes == 0U);
	completed = transport.completed_total;

	/* A pending renderer marker exposes accidental completion caused by a config-only IRQ. */
	memset(&completion, 0, sizeof(completion));
	fixture_drop = 1U;
	fixture_clock_frozen = 1U;
	error = drv_venus_transport_submit(&transport, 31U, NULL, 0U, GPU_COMMAND_CONTEXT_FENCE, 1U, &completion);
	assert(error == 0);
	ring = transport.ring.address;
	head = drv_venus_load16(ring + VENUS_RING_AVAILABLE + 4U + (fixture_peer_available % transport.queue_size) * 2U);

	/* A latched display event without an asserted INTx source belongs to another device. */
	if (type == DRV_PCI_IRQ_INTX) {
		drv_venus_store32(fixture_registers + 8192U, 1U);
		handled = fixture_irq(fixture_irq_argument);
		assert(handled == 0);
		assert(transport.interrupt_total == 0U);
		assert(transport.topology_sequence == 1U);
		assert(fixture_poll_wakes == 0U);
		assert(completion.calls == 0U);
	}

	/* MSI-X discovers a display event even when its shared vector has no ISR bits. */
	drv_venus_store32(fixture_registers + 8192U, 1U);
	if (type == DRV_PCI_IRQ_INTX)
		fixture_registers[4096U] = 2U;

	/* The event wakes inventory observers but cannot satisfy the pending renderer marker. */
	handled = fixture_irq(fixture_irq_argument);
	assert(handled == 1);
	assert(fixture_registers[4096U] == 0U);
	assert(transport.topology_sequence == 2U);
	assert(fixture_poll_wakes == 1U);
	assert(completion.calls == 0U);
	assert(transport.completed_total == completed);

	/* Clearing the modeled device event leaves an unrelated config bit harmless. */
	drv_venus_store32(fixture_registers + 8192U, 2U);
	handled = fixture_irq(fixture_irq_argument);
	if (type == DRV_PCI_IRQ_INTX) {
		assert(handled == 0);
	} else {
		assert(handled == 1);
	}

	/* Neither an unrelated event bit nor an idle shared vector invents readiness. */
	assert(transport.topology_sequence == 2U);
	assert(fixture_poll_wakes == 1U);
	assert(completion.calls == 0U);

	/* One IRQ carries both a real used descriptor and the next display invalidation. */
	fixture_complete_head(head);
	fixture_peer_available = transport.available;
	drv_venus_store32(fixture_registers + 8192U, 1U);
	fixture_registers[4096U] = 3U;
	if (type == DRV_PCI_IRQ_MSIX)
		fixture_registers[4096U] = 1U;

	/* Queue retirement and topology readiness each publish exactly their own result. */
	handled = fixture_irq(fixture_irq_argument);
	assert(handled == 1);
	assert(transport.topology_sequence == 3U);
	assert(fixture_poll_wakes == 2U);
	assert(completion.calls == 1U);
	assert(completion.error == 0);
	assert(transport.completed_total == completed + 1U);
	assert(transport.available == transport.used);

	/* A duplicate IRQ after device-event acknowledgement cannot retire the marker twice. */
	drv_venus_store32(fixture_registers + 8192U, 0U);
	handled = fixture_irq(fixture_irq_argument);
	if (type == DRV_PCI_IRQ_INTX) {
		assert(handled == 0);
	} else {
		assert(handled == 1);
	}

	/* The used-ring and inventory generations remain unchanged after idle delivery. */
	assert(completion.calls == 1U);
	assert(transport.topology_sequence == 3U);
	assert(fixture_poll_wakes == 2U);

	/* Place the next generation at the finite boundary without an impractical event loop. */
	irq = spin_lock_irqsave(&transport.queue_lock);

	transport.topology_sequence = UINT64_MAX - 1U;

	spin_unlock_irqrestore(&transport.queue_lock, irq);

	/* The last representable inventory generation remains valid and wakes observers. */
	fixture_registers[4096U] = 2U;
	handled = fixture_irq(fixture_irq_argument);
	assert(handled == 1);
	assert(transport.topology_sequence == UINT64_MAX);
	assert(transport.topology_overflow == 0U);
	assert(fixture_poll_wakes == 3U);

	/* Another event becomes a persistent error instead of reusing an acknowledged generation. */
	fixture_registers[4096U] = 2U;
	handled = fixture_irq(fixture_irq_argument);
	assert(handled == 1);
	assert(transport.topology_sequence == UINT64_MAX);
	assert(transport.topology_overflow == 1U);
	assert(fixture_poll_wakes == 4U);

	/* Query-side publication uses the same saturated state and the same unlocked wake boundary. */
	drv_venus_transport_display_changed(&transport);
	assert(transport.topology_sequence == UINT64_MAX);
	assert(transport.topology_overflow == 1U);
	assert(fixture_poll_wakes == 5U);
	assert(completion.calls == 1U);
	assert(fixture_reports == 0U);

	/* Completed commands and all IRQ ownership retire normally despite inventory overflow. */
	error = drv_venus_transport_stop(&transport);
	assert(error == 0);
	assert(fixture_irq == NULL);
	assert(fixture_dma == 0U);

	/* Reports the PCI mechanism whose production handler passed the independent peer. */
	if (type == DRV_PCI_IRQ_INTX) {
		puts("Venus topology INTx: read-clear, config-only, queue coexistence, saturation and unlocked poll wake PASS");
	} else {
		puts("Venus topology MSI-X: shared vector, config-only, queue coexistence, saturation and unlocked poll wake PASS");
	}

	/* Succeeded: display changes never substitute for native command completion. */
	return;
}

/* Checks exact profile negotiation and descriptor bounds using independently selected values. */
static void
fixture_strict_capability(
	void)
{
	static const uint32_t flags[5] = {3U, 1U, 2U, 7U, 3U};
	struct venus_transport transport;
	struct drv_gpu_completion completion;
	void *reservation;
	unsigned index;
	int error;

	/* Every same-length old or unknown profile must remain distinguishable from strict support. */
	for (index = 0U; index < 5U; index++) {
		fixture_prepare(&transport);
		fixture_capset_bytes = 168U;
		fixture_vendor_magic = 0x5a424453U;
		fixture_vendor_flags = flags[index];
		if (index == 4U)
			fixture_vendor_magic = 0x12345678U;
		error = drv_venus_transport_start(&transport, NULL);
		assert(error == 0);
		assert(transport.queue_size == 8U);
		assert(transport.slot_count == 4U);
		assert(fixture_dma == 9U);
		memset(&completion, 0, sizeof(completion));
		error = drv_venus_transport_job_reserve(&transport, 17U, 3U, &completion, &reservation);
		if (index == 0U || index == 3U) {
			assert(transport.strict_queue == 1U);
			assert(error == 0);
			error = drv_venus_transport_job_cancel(&transport, reservation, &completion, 0U);
			assert(error == 0);
		} else {
			assert(transport.strict_queue == 0U);
			assert(error == ENOTSUP);
			assert(reservation == NULL);
		}
		error = drv_venus_transport_stop(&transport);
		assert(error == 0);
	}

	/* A non-power-of-two advertised maximum chooses the largest smaller supported ring. */
	fixture_prepare(&transport);
	drv_venus_store16(fixture_registers + 24U, 48U);
	error = drv_venus_transport_start(&transport, NULL);
	assert(error == 0);
	assert(transport.queue_size == 32U);
	assert(transport.slot_count == 16U);
	assert(fixture_dma == 33U);
	error = drv_venus_transport_stop(&transport);
	assert(error == 0);

	/* Report the independently negotiated profile and queue bounds. */
	puts("Venus strict profile: exact flags/magic, legacy rejection, descriptor negotiation PASS");

	/* Succeeded: capabilities and negotiated queue size were not inferred from one another. */
	return;
}

/* Exercises full admission, control reserve, 16-bit wrap and arbitrary used order with real descriptors. */
static void
fixture_jobs(
	void)
{
	struct venus_transport transport;
	struct drv_gpu_completion completions[33];
	void *reservations[29];
	uint8_t *ring;
	uint8_t *packet;
	unsigned allocations;
	unsigned index;
	unsigned handled;
	unsigned capacity;
	uint16_t available;
	int error;

	/* The approved maximum is 64 descriptors, 32 independent DMA chains and four control chains. */
	fixture_prepare(&transport);
	drv_venus_store16(fixture_registers + 24U, 256U);
	fixture_capset_bytes = 168U;
	fixture_vendor_magic = 0x5a424453U;
	fixture_vendor_flags = 3U;
	error = drv_venus_transport_start(&transport, NULL);
	assert(error == 0);
	assert(transport.queue_size == 64U);
	assert(transport.slot_count == 32U);
	assert(fixture_dma == 65U);
	assert(drv_venus_load16(fixture_registers + 24U) == 64U);
	assert(VENUS_RING_AVAILABLE == 1024U);
	assert(VENUS_RING_USED == 1280U);
	assert(64U * 16U <= 1024U);
	assert(1024U + 6U + 64U * 2U <= 1280U);
	assert(1280U + 6U + 64U * 8U <= 4096U);

	/* Backend capacity is a nonblocking snapshot with backend-owned domain validation. */
	drv_venus_transport_set_gpu(&transport, (struct drv_gpu_device *)&fixture_gpu_references);
	error = drv_venus_transport_capacity(&transport, 3U, &capacity);
	assert(error == 0 && capacity == 28U);
	error = drv_venus_transport_capacity(&transport, 0U, &capacity);
	assert(error == EINVAL);
	error = drv_venus_transport_capacity(&transport, 64U, &capacity);
	assert(error == EINVAL);
	assert(fixture_capacity_wakes == 0U);

	/* Advance a drained ring near its 16-bit rollover without inventing device-owned entries. */
	fixture_drop = 1U;
	fixture_clock_frozen = 1U;
	memset(completions, 0, sizeof(completions));
	transport.available = 65530U;
	transport.used = 65530U;
	fixture_peer_available = 65530U;
	ring = transport.ring.address;
	drv_venus_store16(ring + 1026U, 65530U);
	drv_venus_store16(ring + 1282U, 65530U);
	available = transport.available;

	/* Reservations own capacity and callbacks while publishing no work to the host. */
	for (index = 0U; index < 28U; index++) {
		error = drv_venus_transport_job_reserve(&transport, 17U + index, 3U, &completions[index], &reservations[index]);
		assert(error == 0);
		assert(transport.available == available);
		assert(completions[index].calls == 0U);
	}

	/* The twenty-ninth GPU reservation fails before native acceptance or a callback obligation. */
	error = drv_venus_transport_job_reserve(&transport, 80U, 3U, &completions[32], &reservations[28]);
	assert(error == EAGAIN);
	assert(reservations[28] == NULL);
	assert(completions[32].calls == 0U);

	/* Unpublished reservations consume real storage but emit no false capacity wake. */
	error = drv_venus_transport_capacity(&transport, 3U, &capacity);
	assert(error == 0 && capacity == 0U);
	assert(fixture_capacity_wakes == 0U);

	/* Four CPU0 commands remain admissible even while every GPU-job slot is retained. */
	for (index = 28U; index < 32U; index++) {
		error = drv_venus_transport_submit(&transport, 17U + index, NULL, 0U, GPU_COMMAND_CONTEXT_FENCE, 0U, &completions[index]);
		assert(error == 0);
	}

	/* Commit consumes no allocation and preserves the complete prepared marker identity. */
	allocations = fixture_calloc_calls;
	for (index = 0U; index < 28U; index++) {
		error = drv_venus_transport_job_commit(&transport, reservations[index], &completions[index]);
		assert(error == 0);
		assert(fixture_calloc_calls == allocations);
		packet = transport.requests[index].request.address;
		assert(drv_venus_load32(packet) == 0x0207U);
		assert(drv_venus_load32(packet + 4U) == 3U);
		assert(drv_venus_load32(packet + 16U) == 17U + index);
		assert(packet[20U] == 3U);
		assert(drv_venus_load32(packet + 24U) == 0U);
		assert(drv_venus_load32(ring + index * 32U + 8U) == 32U);
	}
	assert(transport.available == 26U);
	fixture_peer_available = transport.available;

	/* Out-of-order host completion crosses every context and the wrapping used index. */
	for (index = 32U; index != 0U; index--) {
		fixture_callback_idle_context = 17U + index - 1U;
		fixture_complete_head((uint16_t)((index - 1U) * 2U));
		handled = fixture_irq(fixture_irq_argument);
		assert(handled == 1U);
		assert(completions[index - 1U].calls == 1U);
		assert(completions[index - 1U].error == 0);
	}
	fixture_callback_idle_context = 0U;
	assert(fixture_capacity_wakes == 32U);
	assert(fixture_capacity_available == 28U);
	assert(transport.used == 26U);
	assert(transport.used == transport.available);
	assert(fixture_calloc_calls == allocations);

	/* A retired token cannot act on recycled capacity or emit a second completion. */
	error = drv_venus_transport_job_commit(&transport, reservations[0], &completions[0]);
	assert(error == ESTALE);
	fixture_drop = 0U;
	error = drv_venus_transport_stop(&transport);
	assert(error == 0);

	/* Report reservation capacity and exact completion ownership. */
	puts("Venus jobs: 28 reservations + 4 control, no-allocation commit, reverse IRQ, 16-bit wrap PASS");

	/* Succeeded: full-capacity admission and IRQ retirement preserved every exact owner. */
	return;
}

/* Checks rollback, posted uncertainty and a stopped producer with no userspace waiter. */
static void
fixture_job_failures(
	void)
{
	struct venus_transport transport;
	struct drv_gpu_completion completion;
	void *reservation;
	uint16_t available;
	int error;

	/* Definite native rejection removes an unpublished token without signaling its fence. */
	fixture_prepare(&transport);
	fixture_capset_bytes = 168U;
	fixture_vendor_magic = 0x5a424453U;
	fixture_vendor_flags = 3U;
	error = drv_venus_transport_start(&transport, NULL);
	assert(error == 0);
	memset(&completion, 0, sizeof(completion));
	available = transport.available;
	error = drv_venus_transport_job_reserve(&transport, 17U, 1U, &completion, &reservation);
	assert(error == 0);
	error = drv_venus_transport_job_cancel(&transport, reservation, &completion, 0U);
	assert(error == 0);
	assert(completion.calls == 0U);
	assert(transport.available == available);
	error = drv_venus_transport_job_commit(&transport, reservation, &completion);
	assert(error == ESTALE);

	/* Explicit device failure remains observable even when no asynchronous callback exists. */
	drv_venus_transport_set_gpu(&transport, (struct drv_gpu_device *)&fixture_gpu_references);
	assert(fixture_gpu_references == 1U);
	drv_venus_transport_fail(&transport, EIO);
	assert(fixture_reports == 1U);
	assert(fixture_gpu_references == 1U);
	error = drv_venus_transport_stop(&transport);
	assert(error == 0);
	assert(fixture_gpu_references == 0U);

	/* Withdrawal during fault publication cannot retire the wrapper before the snapshot is released. */
	fixture_prepare(&transport);
	error = drv_venus_transport_start(&transport, NULL);
	assert(error == 0);
	drv_venus_transport_set_gpu(&transport, (struct drv_gpu_device *)&fixture_gpu_references);
	fixture_withdraw_report = 1U;
	drv_venus_transport_fail(&transport, EIO);
	assert(fixture_reports == 1U);
	assert(fixture_gpu_references == 0U);
	assert(transport.gpu == NULL);
	error = drv_venus_transport_stop(&transport);
	assert(error == 0);

	/* Failure before registration is reported when a retained wrapper is subsequently published. */
	fixture_prepare(&transport);
	error = drv_venus_transport_start(&transport, NULL);
	assert(error == 0);
	drv_venus_transport_fail(&transport, EIO);
	assert(fixture_reports == 0U);
	drv_venus_transport_set_gpu(&transport, (struct drv_gpu_device *)&fixture_gpu_references);
	assert(fixture_reports == 1U);
	assert(fixture_gpu_references == 1U);
	error = drv_venus_transport_stop(&transport);
	assert(error == 0);
	assert(fixture_gpu_references == 0U);

	/* Backend supervision never applies the old ten-second policy to common-managed jobs. */
	fixture_prepare(&transport);
	fixture_capset_bytes = 168U;
	fixture_vendor_magic = 0x5a424453U;
	fixture_vendor_flags = 3U;
	error = drv_venus_transport_start(&transport, NULL);
	assert(error == 0);
	memset(&completion, 0, sizeof(completion));
	error = drv_venus_transport_job_reserve(&transport, 17U, 1U, &completion, &reservation);
	assert(error == 0);
	fixture_clock += 60001U;
	fixture_watchdog_stop = 2U;
	venus_worker(&transport);
	assert(completion.calls == 0U);
	assert(transport.failed == 0U);
	assert(transport.requests[0].state == VENUS_SLOT_RESERVED);
	assert(drv_venus_transport_idle(&transport, 17U) == 0);
	assert(drv_venus_transport_idle(&transport, 18U) == 0);
	transport.stopping = 0U;
	fixture_watchdog_stop = 0U;

	/* Reserved native uncertainty stays retained until the common stop deadline escalates. */
	drv_venus_transport_fail(&transport, ETIMEDOUT);
	assert(completion.calls == 1U && completion.error == ETIMEDOUT);
	assert(transport.requests[0].state == VENUS_SLOT_QUARANTINED);
	assert(drv_venus_transport_idle(&transport, 17U) == ENODEV);
	assert(fixture_dma == 9U);
	error = drv_venus_transport_job_commit(&transport, reservation, &completion);
	assert(error == ESTALE);
	error = drv_venus_transport_stop(&transport);
	assert(error == 0);

	/* A token-validated fault retains backing while common policy requests local stop. */
	fixture_prepare(&transport);
	fixture_capset_bytes = 168U;
	fixture_vendor_magic = 0x5a424453U;
	fixture_vendor_flags = 3U;
	error = drv_venus_transport_start(&transport, NULL);
	assert(error == 0);
	fixture_drop = 1U;
	fixture_clock_frozen = 1U;
	memset(&completion, 0, sizeof(completion));
	error = drv_venus_transport_job_reserve(&transport, 17U, 1U, &completion, &reservation);
	assert(error == 0);
	error = drv_venus_transport_job_commit(&transport, reservation, &completion);
	assert(error == 0);
	error = drv_venus_transport_job_cancel(&transport, reservation, &completion, 0U);
	assert(error == ESTALE);
	error = drv_venus_transport_job_cancel(&transport, reservation, &completion, 1U);
	assert(error == 0);
	assert(completion.calls == 0U);
	assert(transport.failed == 0U);
	assert(transport.requests[0].state == VENUS_SLOT_POSTED);
	assert(drv_venus_transport_idle(&transport, 17U) == EAGAIN);

	/* Actual completion can retire a logically failed context without faulting its peers. */
	fixture_clock += 15000U;
	fixture_watchdog_stop = 2U;
	venus_worker(&transport);
	assert(transport.failed == 0U);
	transport.stopping = 0U;
	fixture_watchdog_stop = 0U;
	fixture_peer_available = transport.available;
	fixture_complete_head(0U);
	assert(fixture_irq(fixture_irq_argument) == 1U);
	assert(completion.calls == 1U && completion.error == 0);
	assert(drv_venus_transport_idle(&transport, 17U) == 0);
	assert(transport.failed == 0U);
	fixture_drop = 0U;
	error = drv_venus_transport_stop(&transport);
	assert(error == 0);

	/* Report autonomous fault ownership and registration lifetime boundaries. */
	puts("Venus job policy: capacity, common deadlines, late retirement, unknown reserved, isolated idle and global quarantine PASS");

	/* Succeeded: missing producer progress and uncertain work never became successful completion. */
	return;
}
/* Checks exact stop wire fields and separates native proof from old descriptor retirement. */
static void
fixture_quiesce_proposal(
	void)
{
	struct venus_transport transport;
	struct venus_request *pending;
	struct drv_gpu_completion completions[32];
	void *occupied[28];
	void *reserved;
	void *posted;
	uint8_t *packet;
	uint8_t *ring;
	unsigned quiesced;
	unsigned bad;
	unsigned slot;
	unsigned available;
	int error;

	/* Old profiles must refuse native quiescence without reserving or posting a command. */
	fixture_prepare(&transport);
	fixture_capset_bytes = 168U;
	fixture_vendor_magic = 0x5a424453U;
	fixture_vendor_flags = 3U;
	assert(drv_venus_transport_start(&transport, NULL) == 0);
	pending = NULL;
	quiesced = 0U;
	available = transport.available;
	assert(drv_venus_transport_quiesce(&transport, 17U, &pending, &quiesced) == ENOTSUP);
	assert(pending == NULL && quiesced == 0U && transport.available == available);
	assert(drv_venus_transport_stop(&transport) == 0);

	/* An independently successful response of another type or length is still not a stop ACK. */
	for (bad = 0U; bad < 3U; bad++) {
		fixture_prepare(&transport);
		drv_venus_store16(fixture_registers + 24U, 64U);
		fixture_capset_bytes = 168U;
		fixture_vendor_magic = 0x5a424453U;
		fixture_vendor_flags = 7U;
		assert(drv_venus_transport_start(&transport, NULL) == 0);
		assert(transport.strict_queue == 1U && transport.quiesce == 1U);
		fixture_drop = 1U;
		fixture_clock_frozen = 1U;
		memset(completions, 0, sizeof(completions));
		assert(drv_venus_transport_job_reserve(&transport, 17U, 3U, &completions[0], &reserved) == 0);
		assert(drv_venus_transport_job_reserve(&transport, 17U, 3U, &completions[1], &posted) == 0);
		assert(drv_venus_transport_job_commit(&transport, posted, &completions[1]) == 0);

		/* Posting one stop command is nonblocking and does not end either existing owner. */
		pending = NULL;
		quiesced = 0U;
		assert(drv_venus_transport_quiesce(&transport, 17U, &pending, &quiesced) == EAGAIN);
		assert(pending != NULL && pending->supervised == 1U && pending->completion == NULL);
		slot = (unsigned)(pending - transport.requests);
		packet = pending->request.address;
		ring = transport.ring.address;
		assert(drv_venus_load32(ring + slot * 32U + 8U) == 48U);
		assert(drv_venus_load32(packet) == 0x0207U);
		assert(drv_venus_load32(packet + 4U) == 3U);
		assert(drv_venus_load64(packet + 8U) != 0U);
		assert(drv_venus_load32(packet + 16U) == 17U && packet[20U] == 0U);
		assert(drv_venus_load32(packet + 24U) == 16U);
		assert(drv_venus_load32(packet + 32U) == 0x5a425351U);
		assert(drv_venus_load32(packet + 36U) == 1U);
		assert(drv_venus_load64(packet + 40U) == 0U);
		assert(completions[0].calls == 0U && completions[1].calls == 0U);
		assert(drv_venus_transport_quiesce(&transport, 17U, &pending, &quiesced) == EAGAIN);

		/* The PCI peer returns the exact stop descriptor, independently from older native work. */
		fixture_complete_head((uint16_t)(slot * 2U));
		if (bad == 1U)
			drv_venus_store32(pending->response.address, 0x1103U);
		if (bad == 2U)
			drv_venus_store32(ring + 1280U + 8U + (transport.used % transport.queue_size) * 8U, 25U);
		assert(fixture_irq(fixture_irq_argument) == 1U);
		error = drv_venus_transport_quiesce(&transport, 17U, &pending, &quiesced);
		if (bad != 0U) {
			assert(error == EIO && quiesced == 0U && pending != NULL);
			assert(completions[0].calls == 0U && completions[1].calls == 0U);
			drv_venus_transport_fail(&transport, EIO);
			assert(completions[0].calls == 1U && completions[0].error != 0);
			assert(completions[1].calls == 1U && completions[1].error != 0);
		} else {
			assert(error == 0 && quiesced == 1U && pending == NULL);
			assert(completions[0].calls == 0U && completions[1].calls == 0U);
			assert(drv_venus_transport_idle(&transport, 17U) == EAGAIN);

			/* Native idle alone cannot recycle a previously posted DMA descriptor or its callback. */
			fixture_complete_head(2U);
			assert(fixture_irq(fixture_irq_argument) == 1U);
			assert(completions[1].calls == 1U && completions[1].error == 0);

			/* An unpublished reservation no longer blocks idle; the core withdraws it through cancel. */
			assert(drv_venus_transport_idle(&transport, 17U) == 0);
			assert(drv_venus_transport_job_cancel(&transport, reserved, &completions[0], 0U) == 0);
			assert(completions[0].calls == 0U);
			assert(drv_venus_transport_idle(&transport, 17U) == 0);
		}
		fixture_drop = 0U;
		assert(drv_venus_transport_stop(&transport) == 0);
		assert(fixture_dma == 0U && fixture_maps == 0U);
	}

	/* Full control storage returns immediately and never overwrites another context's descriptor. */
	fixture_prepare(&transport);
	drv_venus_store16(fixture_registers + 24U, 64U);
	fixture_capset_bytes = 168U;
	fixture_vendor_magic = 0x5a424453U;
	fixture_vendor_flags = 7U;
	assert(drv_venus_transport_start(&transport, NULL) == 0);
	fixture_drop = 1U;
	fixture_clock_frozen = 1U;
	memset(completions, 0, sizeof(completions));
	for (slot = 0U; slot < 28U; slot++) {
		assert(drv_venus_transport_job_reserve(&transport, 80U, 3U, &completions[slot], &occupied[slot]) == 0);
	}
	for (slot = 28U; slot < 32U; slot++) {
		assert(drv_venus_transport_submit(&transport, 80U, NULL, 0U, GPU_COMMAND_CONTEXT_FENCE, 0U, &completions[slot]) == 0);
	}
	pending = NULL;
	quiesced = 0U;
	available = transport.available;
	assert(drv_venus_transport_quiesce(&transport, 17U, &pending, &quiesced) == EAGAIN);
	assert(pending == NULL && quiesced == 0U && transport.available == available);
	drv_venus_transport_fail(&transport, EIO);
	fixture_drop = 0U;
	assert(drv_venus_transport_stop(&transport) == 0);
	assert(fixture_dma == 0U && fixture_maps == 0U);

	puts("QUIESCE guest exact48B/old-profile refusal/NODATA24B/retained posted DMA/full capacity/no false ACK PASS");
}

/* Checks per-context quarantine, peer continuation, late chain reclaim and local supervised refusal. */
static void
fixture_isolation(
	void)
{
	struct venus_transport transport;
	struct venus_request *refused;
	struct drv_gpu_completion completions[4];
	void *reserved;
	void *posted;
	void *peer;
	void *marker;
	unsigned available;

	/* Two contexts share the strict transport; only one of them becomes unconfirmable. */
	fixture_prepare(&transport);
	drv_venus_store16(fixture_registers + 24U, 64U);
	fixture_capset_bytes = 168U;
	fixture_vendor_magic = 0x5a424453U;
	fixture_vendor_flags = 7U;
	assert(drv_venus_transport_start(&transport, NULL) == 0);
	fixture_drop = 1U;
	fixture_clock_frozen = 1U;
	memset(completions, 0, sizeof(completions));
	assert(drv_venus_transport_job_reserve(&transport, 17U, 3U, &completions[0], &reserved) == 0);
	assert(drv_venus_transport_job_reserve(&transport, 17U, 3U, &completions[1], &posted) == 0);
	assert(drv_venus_transport_job_commit(&transport, posted, &completions[1]) == 0);
	assert(drv_venus_transport_job_reserve(&transport, 18U, 3U, &completions[2], &peer) == 0);
	assert(drv_venus_transport_job_commit(&transport, peer, &completions[2]) == 0);

	/* Isolation needs a real context and error, ends only context 17's callbacks, and keeps the transport alive. */
	assert(drv_venus_transport_isolate(&transport, 0U, EIO) == EINVAL);
	assert(drv_venus_transport_isolate(&transport, 17U, 0) == EINVAL);
	assert(drv_venus_transport_isolate(&transport, 17U, EIO) == 0);
	assert(completions[0].calls == 1U && completions[0].error == EIO);
	assert(completions[1].calls == 1U && completions[1].error == EIO);
	assert(completions[2].calls == 0U && transport.failed == 0U);
	assert(transport.requests[0].state == VENUS_SLOT_QUARANTINED);
	assert(transport.requests[1].state == VENUS_SLOT_QUARANTINED);
	assert(drv_venus_transport_capacity(&transport, 3U, &available) == 0 && available == 25U);

	/* The peer context completes normally while the quarantined chains stay device-owned. */
	fixture_complete_head(4U);
	assert(fixture_irq(fixture_irq_argument) == 1U);
	assert(completions[2].calls == 1U && completions[2].error == 0);
	assert(transport.failed == 0U);

	/* A late return of the quarantined posted chain frees its slot without any further callback. */
	fixture_complete_head(2U);
	assert(fixture_irq(fixture_irq_argument) == 1U);
	assert(transport.failed == 0U);
	assert(transport.requests[1].state == VENUS_SLOT_FREE);
	assert(transport.requests[0].state == VENUS_SLOT_QUARANTINED);
	assert(completions[1].calls == 1U);
	assert(drv_venus_transport_capacity(&transport, 3U, &available) == 0 && available == 27U);

	/* A defined host refusal of a supervised marker retires that marker alone. */
	assert(drv_venus_transport_job_reserve(&transport, 19U, 3U, &completions[3], &marker) == 0);
	assert(drv_venus_transport_job_commit(&transport, marker, &completions[3]) == 0);
	refused = marker;
	fixture_complete_head((uint16_t)((unsigned)(refused - transport.requests) * 2U));
	drv_venus_store32(refused->response.address, 0x1204U);
	assert(fixture_irq(fixture_irq_argument) == 1U);
	assert(completions[3].calls == 1U && completions[3].error == EINVAL);
	assert(transport.failed == 0U);
	fixture_drop = 0U;
	assert(drv_venus_transport_stop(&transport) == 0);
	assert(fixture_dma == 0U && fixture_maps == 0U);
	puts("ISOLATE transport: per-context quarantine, peer continuation, late chain reclaim, local supervised refusal PASS");
}
