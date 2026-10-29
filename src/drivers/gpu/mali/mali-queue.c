/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The driver's own command stream: stream 0 of group slot 0, started on
 * an empty ring.
 *
 * The group runs in an address space of its own (slot 1), where its
 * ring buffer lives; the ring's two small blocks and the group's suspend
 * buffers live in the microcontroller's space.  Starting the stream on
 * an empty ring shows the whole path from the host to the firmware's
 * scheduler works: the group slot is programmed, its doorbell rung
 * through the global block, and the firmware acknowledges the group's
 * start and the stream's.  The stream rings its own doorbell, number 1,
 * when instructions are written to the ring.
 */

#include <stddef.h>
#include <stdint.h>

#include <kern/clock.h>
#include <kern/device-io.h>
#include <kern/klog.h>
#include <kern/sched.h>
#include <uapi/errno.h>

#include "drivers/gpu/mali/mali-private.h"
#include "drivers/gpu/mali/mali-regs.h"

/* The ring buffer's size (64 KiB), and the two blocks after it: the input page, then the output page. */
#define RING_BYTES		0x10000ULL
#define RING_BLOCKS_BYTES	0x2000ULL
#define RING_OUTPUT_OFFSET	0x1000U

/* The stream's doorbell: the first after the global one. */
#define QUEUE_DOORBELL		1U

/* The stream's priority among the group's streams. */
#define QUEUE_PRIORITY		1U

/* How long the firmware has to acknowledge the start (100 ms). */
#define START_WAIT_MS		100U

static int make_buffers(struct mali_device *device, const struct mali_group_interface *group);
static void program_stream(struct mali_device *device, struct mali_stream_interface *stream);
static void program_group(struct mali_device *device, struct mali_group_interface *group);
static int wait_stream_started(struct mali_stream_interface *stream, unsigned milliseconds);

/*
 * Starts stream 0 of group slot 0 on an empty ring, and reports the
 * firmware's answer.
 *
 * Procedure referenced:
 * Linux v7.3 drivers/gpu/drm/panthor/panthor_sched.c
 * (cs_slot_prog_locked, csg_slot_prog_locked, the start request of
 * tick_ctx_apply, csgs_upd_ctx_apply_locked).
 */
int
mali_queue_start(
	struct mali_device *device)
{
	struct mali_queue *queue;
	struct mali_group_interface *group;
	struct mali_stream_interface *stream;
	uint32_t request;
	uint32_t acknowledge;
	uint32_t state;
	int error;

	/* Only a running firmware schedules groups. */
	if (!device->firmware_running)
		return ENXIO;

	/* The first slot and its first stream. */
	queue = &device->queue;
	queue->group_slot = 0U;
	queue->stream = 0U;
	queue->doorbell = QUEUE_DOORBELL;
	group = &device->interfaces.groups[queue->group_slot];
	stream = &group->streams[queue->stream];

	/* The group's space, the ring and the firmware's blocks. */
	error = make_buffers(device, group);
	if (error != 0) {
		kern_logf("mali: queue: buffers not made (%d)\n", error);
		return error;
	}

	/* The stream first, then its group, which names the stream in its doorbell request. */
	program_stream(device, stream);
	program_group(device, group);

	/* Asks for the group's start and its endpoint configuration, and rings its doorbell. */
	request = mali_iface_read32(group->input, MALI_CSG_INPUT_REQUEST);
	acknowledge = mali_iface_read32(group->output, MALI_CSG_OUTPUT_ACK);
	request = mali_iface_update(request, MALI_CSG_STATE_START, MALI_CSG_STATE_MASK);
	request = mali_iface_toggle(request, acknowledge, MALI_CSG_ENDPOINT_CONFIG);
	mali_iface_write32(group->input, MALI_CSG_INPUT_REQUEST, request);
	mali_csf_ring_groups(device, 1U << queue->group_slot);

	/* Waits for the firmware to take both. */
	error = mali_csf_wait_group(device, queue->group_slot, MALI_CSG_STATE_MASK | MALI_CSG_ENDPOINT_CONFIG, START_WAIT_MS);
	if (error != 0) {
		kern_logf("mali: queue: group %u did not start (request %08x, acknowledge %08x, state %08x)\n",
			  queue->group_slot,
			  (unsigned)mali_iface_read32(group->input, MALI_CSG_INPUT_REQUEST),
			  (unsigned)mali_iface_read32(group->output, MALI_CSG_OUTPUT_ACK),
			  (unsigned)mali_iface_read32(group->output, MALI_CSG_OUTPUT_STATE));
		return error;
	}

	/* Then for the stream's own start. */
	error = wait_stream_started(stream, START_WAIT_MS);
	if (error != 0) {
		kern_logf("mali: queue: stream %u did not start (request %08x, acknowledge %08x, fault %08x, fatal %08x)\n",
			  queue->stream,
			  (unsigned)mali_iface_read32(stream->input, MALI_CS_INPUT_REQUEST),
			  (unsigned)mali_iface_read32(stream->output, MALI_CS_OUTPUT_ACK),
			  (unsigned)mali_iface_read32(stream->output, MALI_CS_OUTPUT_FAULT),
			  (unsigned)mali_iface_read32(stream->output, MALI_CS_OUTPUT_FATAL));
		return error;
	}

	/* An empty ring: the stream's doorbell must find nothing to run. */
	mali_csf_ring_doorbell(device, queue->doorbell);
	queue->running = 1;

	/* Says the stream runs, with the group's state word. */
	state = mali_iface_read32(group->output, MALI_CSG_OUTPUT_STATE);
	kern_logf("mali: queue: group %u stream %u started on an empty ring (group state %08x, ring at %llx, doorbell %u)\n",
		  queue->group_slot,
		  queue->stream,
		  (unsigned)state,
		  (unsigned long long)queue->ring.gpu_address,
		  queue->doorbell);

	/* Succeeded: the stream waits for instructions. */
	return 0;
}

/* Makes the group's space and the queue's four buffers. */
static int
make_buffers(
	struct mali_device *device,
	const struct mali_group_interface *group)
{
	struct mali_queue *queue;
	int error;

	/* The group's own space, empty, translating from now on. */
	queue = &device->queue;
	error = mali_as_init(device, &device->group, MALI_GROUP_ADDRESS_SPACE);
	if (error != 0)
		return error;

	device->group_next = MALI_GROUP_SPACE_START;
	error = mali_as_enable(device, &device->group);
	if (error != 0)
		return error;

	/* The ring the stream reads its instructions from, never run as code. */
	error = mali_group_alloc(device, &queue->ring, RING_BYTES, MALI_PT_WRITE);
	if (error != 0)
		return error;

	/* The ring's input and output blocks, where the firmware reads and writes them. */
	error = mali_mcu_alloc(device, &queue->ring_blocks, RING_BLOCKS_BYTES, MALI_PT_WRITE);
	if (error != 0)
		return error;

	/* The group's suspend buffer, when the firmware asks for one. */
	if (group->suspend_bytes != 0U) {
		error = mali_mcu_alloc(device, &queue->suspend, group->suspend_bytes, MALI_PT_WRITE);
		if (error != 0)
			return error;
	}

	/* And its protected-mode one, likewise. */
	if (group->protected_suspend_bytes != 0U) {
		error = mali_mcu_alloc(device, &queue->protected_suspend, group->protected_suspend_bytes, MALI_PT_WRITE);
		if (error != 0)
			return error;
	}

	/* Succeeded: every buffer is mapped. */
	return 0;
}

/*
 * Writes the stream's ring, its blocks, its doorbell and priority, and
 * asks for its start, with idle reports when it waits or runs dry.
 */
static void
program_stream(
	struct mali_device *device,
	struct mali_stream_interface *stream)
{
	struct mali_queue *queue;
	volatile uint8_t *input_block;
	uint32_t configuration;
	uint32_t request;
	uint32_t mask;

	/* An empty ring: nothing written, nothing read. */
	queue = &device->queue;
	input_block = queue->ring_blocks.view;
	mali_iface_write64(input_block, MALI_RING_INPUT_INSERT, 0U);
	mali_iface_write64(input_block, MALI_RING_INPUT_EXTRACT, 0U);

	/* The ring and its two blocks. */
	mali_iface_write64(stream->input, MALI_CS_INPUT_RING_BASE, queue->ring.gpu_address);
	mali_iface_write32(stream->input, MALI_CS_INPUT_RING_SIZE, (uint32_t)queue->ring.bytes);
	mali_iface_write64(stream->input, MALI_CS_INPUT_RING_INPUT, queue->ring_blocks.gpu_address);
	mali_iface_write64(stream->input, MALI_CS_INPUT_RING_OUTPUT, queue->ring_blocks.gpu_address + RING_OUTPUT_OFFSET);

	/* The priority and the doorbell the host rings for this stream. */
	configuration = QUEUE_PRIORITY;
	configuration |= queue->doorbell << MALI_CS_CONFIG_DOORBELL_SHIFT;
	mali_iface_write32(stream->input, MALI_CS_INPUT_CONFIG, configuration);

	/* Every answer raises the interrupt. */
	mali_iface_write32(stream->input, MALI_CS_INPUT_ACK_IRQ_MASK, 0xffffffffU);

	/* The start, with idle reports when it waits on a sync object or runs dry. */
	mask = MALI_CS_IDLE_SYNC_WAIT | MALI_CS_IDLE_EMPTY | MALI_CS_STATE_MASK;
	request = mali_iface_read32(stream->input, MALI_CS_INPUT_REQUEST);
	request = mali_iface_update(request, MALI_CS_IDLE_SYNC_WAIT | MALI_CS_IDLE_EMPTY | MALI_CS_STATE_START, mask);
	mali_iface_write32(stream->input, MALI_CS_INPUT_REQUEST, request);
}

/*
 * Writes the group slot's cores, endpoint, address space and suspend
 * buffers, and flips the stream's bit of its doorbell request so the
 * firmware reads the stream when the group starts.
 */
static void
program_group(
	struct mali_device *device,
	struct mali_group_interface *group)
{
	struct mali_queue *queue;
	uint64_t endpoint;
	unsigned shaders;
	unsigned tilers;
	uint32_t request;
	uint32_t acknowledge;

	/* Every core may run the group's compute, fragment and tiler work. */
	queue = &device->queue;
	mali_iface_write64(group->input, MALI_CSG_INPUT_ALLOW_COMPUTE, device->shader_present);
	mali_iface_write64(group->input, MALI_CSG_INPUT_ALLOW_FRAGMENT, device->shader_present);
	mali_iface_write32(group->input, MALI_CSG_INPUT_ALLOW_OTHER, (uint32_t)device->tiler_present);

	/* The most cores of each kind at once, at the highest priority. */
	shaders = (unsigned)__builtin_popcountll(device->shader_present);
	tilers = (unsigned)__builtin_popcountll(device->tiler_present);
	endpoint = shaders & MALI_CSG_ENDPOINT_CORES_MASK;
	endpoint |= (uint64_t)(shaders & MALI_CSG_ENDPOINT_CORES_MASK) << MALI_CSG_ENDPOINT_FRAGMENT_SHIFT;
	endpoint |= (uint64_t)(tilers & MALI_CSG_ENDPOINT_TILERS_MASK) << MALI_CSG_ENDPOINT_TILER_SHIFT;
	endpoint |= (uint64_t)MALI_CSG_PRIORITY_HIGHEST << MALI_CSG_ENDPOINT_PRIORITY_SHIFT;

	/* A firmware of interface 4.0 on takes the endpoint as a 64-bit word at its own place. */
	if (device->interfaces.version >= MALI_IFACE_ENDPOINT64_VERSION) {
		mali_iface_write64(group->input, MALI_CSG_INPUT_ENDPOINT64, endpoint);
	} else {
		mali_iface_write32(group->input, MALI_CSG_INPUT_ENDPOINT, (uint32_t)endpoint);
	}

	/* The address space the group's streams translate through. */
	mali_iface_write32(group->input, MALI_CSG_INPUT_CONFIG, device->group.slot);

	/* The suspend buffers, zero when the firmware asked for none. */
	mali_iface_write64(group->input, MALI_CSG_INPUT_SUSPEND, queue->suspend.gpu_address);
	mali_iface_write64(group->input, MALI_CSG_INPUT_PROTM_SUSPEND, queue->protected_suspend.gpu_address);

	/* Every answer raises the interrupt. */
	mali_iface_write32(group->input, MALI_CSG_INPUT_ACK_IRQ_MASK, 0xffffffffU);

	/* The stream's doorbell bit, flipped away from the last answer. */
	request = mali_iface_read32(group->input, MALI_CSG_INPUT_DOORBELL);
	acknowledge = mali_iface_read32(group->output, MALI_CSG_OUTPUT_DOORBELL_ACK);
	mali_iface_write32(group->input, MALI_CSG_INPUT_DOORBELL, mali_iface_toggle(request, acknowledge, 1U << queue->stream));
}

/* Polls the stream's acknowledge word until its state says started; ETIMEDOUT when it does not. */
static int
wait_stream_started(
	struct mali_stream_interface *stream,
	unsigned milliseconds)
{
	uint64_t deadline;
	uint64_t now;
	uint32_t acknowledge;

	/* Reads, then sleeps a tick, until started or the time is spent. */
	deadline = sched_ticks() + kern_ms_to_ticks(milliseconds) + 1U;
	for (;;) {
		/* The state field of the acknowledge word mirrors the request once taken. */
		kern_io_read_barrier();
		acknowledge = mali_iface_read32(stream->output, MALI_CS_OUTPUT_ACK);
		if ((acknowledge & MALI_CS_STATE_MASK) == MALI_CS_STATE_START)
			return 0;

		/* The time is spent. */
		now = sched_ticks();
		if (now >= deadline)
			break;

		/* One tick before the next read. */
		sched_sleep(now + 1U);
	}

	/* Succeeded in waiting, but the stream did not start. */
	return ETIMEDOUT;
}
