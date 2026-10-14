/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The resource manager's controls and object parameters for the address
 * space and the channel, and the commands a channel runs.
 *
 * A control is a 24-byte header (client, object, command, status, parameter
 * length, flags) followed by the parameters.  The parameters are written by
 * byte offset (the 570.144 layouts).  The commands are the host's methods in
 * a push buffer (a header word, then the data words) and the 8-byte entries
 * of the ring (GPFIFO) that point at push buffer segments.  Nothing here
 * touches the hardware.
 */

#include <stddef.h>
#include <stdint.h>

#include <uapi/errno.h>

#include "drivers/gpu/nvrtx/nvrtx-gsp.h"

/* The control header's fields. */
#define OBJECTS_CONTROL_CLIENT_AT	0U
#define OBJECTS_CONTROL_OBJECT_AT	4U
#define OBJECTS_CONTROL_COMMAND_AT	8U
#define OBJECTS_CONTROL_PARAMS_SIZE_AT	16U

/* The address space's parameters: the index (0, a new space) and the flags (bit 3: owned by the driver). */
#define OBJECTS_SPACE_FLAGS_AT		4U
#define OBJECTS_SPACE_EXTERNAL		(1U << 3)

/* The page-directory control's parameters. */
#define OBJECTS_PDIR_ADDRESS_AT		0U
#define OBJECTS_PDIR_ENTRIES_AT		8U
#define OBJECTS_PDIR_FLAGS_AT		12U
#define OBJECTS_PDIR_SPACE_AT		16U
#define OBJECTS_PDIR_ROOT_ENTRIES	4U

/* The device information table: the entry count, and the entries of 100 bytes whose words 2 and 3 are the engine type and the runlist. */
#define OBJECTS_DEVINFO_COUNT_AT	4U
#define OBJECTS_DEVINFO_ENTRIES_AT	12U
#define OBJECTS_DEVINFO_ENTRY_BYTES	100U
#define OBJECTS_DEVINFO_MAX		32U
#define OBJECTS_DEVINFO_TYPE_AT		8U
#define OBJECTS_DEVINFO_RUNLIST_AT	12U

/* The engine types: graphics, and the ten copy engines (the same numbers in both of the resource manager's numberings). */
#define OBJECTS_ENGINE_GRAPHICS		0x01U
#define OBJECTS_ENGINE_COPY_FIRST	0x09U
#define OBJECTS_ENGINE_COPY_LAST	0x12U

/* The channel's parameters. */
#define OBJECTS_CHAN_RING_AT		8U
#define OBJECTS_CHAN_RING_ENTRIES_AT	16U
#define OBJECTS_CHAN_FLAGS_AT		20U
#define OBJECTS_CHAN_SPACE_AT		28U
#define OBJECTS_CHAN_ENGINE_AT		128U
#define OBJECTS_CHAN_INSTANCE_AT	144U
#define OBJECTS_CHAN_USERD_AT		168U
#define OBJECTS_CHAN_RAMFC_AT		192U
#define OBJECTS_CHAN_METHODS_AT		216U
#define OBJECTS_CHAN_INTERNAL_AT	244U
#define OBJECTS_MEMORY_BASE_AT		0U
#define OBJECTS_MEMORY_SIZE_AT		8U
#define OBJECTS_MEMORY_SPACE_AT		16U
#define OBJECTS_MEMORY_CACHE_AT		20U
#define OBJECTS_SPACE_SYSTEM		1U
#define OBJECTS_SPACE_VRAM		2U

/* The channel's flags: USERD index in bits 10:8, USERD page in bits 20:12 with its fixed bit 21. */
#define OBJECTS_FLAG_USERD_INDEX_SHIFT	8U
#define OBJECTS_FLAG_USERD_PAGE_SHIFT	12U
#define OBJECTS_FLAG_USERD_PAGE_FIXED	(1U << 21)
#define OBJECTS_CHANNELS_PER_USERD	8U

/* The channel's internal flags: user privilege, and no error notifiers (type "none" = 1 in bits 3:2 and 5:4). */
#define OBJECTS_INTERNAL_NO_NOTIFIERS	((1U << 2) | (1U << 4))

/* The method header: the opcode (incrementing) in bits 31:29, count 28:16, subchannel 15:13, method/4 in 11:0. */
#define OBJECTS_METHOD_INCREMENT	(1U << 29)

/* The host's methods and values for a fence. */
#define OBJECTS_HOST_NON_STALL		0x020U
#define OBJECTS_HOST_MEM_OP_A		0x028U
#define OBJECTS_HOST_SEM_ADDRESS_LOW	0x05cU
#define OBJECTS_HOST_SEM_EXECUTE	0x06cU
#define OBJECTS_SEM_RELEASE		0x1U
#define OBJECTS_SEM_RELEASE_WFI		(1U << 20)
#define OBJECTS_MEM_OP_MEMBAR		(5U << 27)

static void objects_clear(uint8_t *bytes, size_t length);
static size_t objects_method(uint8_t *push, size_t at, uint32_t method, uint32_t count);
static void objects_memory(uint8_t *params, size_t at, uint64_t base, uint64_t size, uint32_t space, uint32_t cache);

/*
 * Writes a control call: the header, then the parameters.
 *
 * Returns the call's length, or 0 when it does not fit.
 */
size_t
nvrtx_control_call(
	uint8_t *call,
	size_t capacity,
	uint32_t client,
	uint32_t object,
	uint32_t command,
	const uint8_t *parameters,
	size_t length)
{
	size_t at;

	/* Refuses a call that does not fit. */
	if (NVRTX_CONTROL_HEADER_BYTES + length > capacity)
		return 0U;

	/* Writes the header; the status and flags stay zero. */
	objects_clear(call, NVRTX_CONTROL_HEADER_BYTES);
	nvrtx_put32(call, OBJECTS_CONTROL_CLIENT_AT, client);
	nvrtx_put32(call, OBJECTS_CONTROL_OBJECT_AT, object);
	nvrtx_put32(call, OBJECTS_CONTROL_COMMAND_AT, command);
	nvrtx_put32(call, OBJECTS_CONTROL_PARAMS_SIZE_AT, (uint32_t)length);

	/* Copies the parameters. */
	for (at = 0U; at < length; at++)
		call[NVRTX_CONTROL_HEADER_BYTES + at] = parameters[at];

	/* Succeeded: the call's length. */
	return NVRTX_CONTROL_HEADER_BYTES + length;
}

/*
 * Writes the parameters of an address space whose page tables the driver owns.
 */
void
nvrtx_space_params(
	uint8_t *params)
{
	/* A new space (index 0), externally owned. */
	objects_clear(params, NVRTX_SPACE_PARAMS_BYTES);
	nvrtx_put32(params, OBJECTS_SPACE_FLAGS_AT, OBJECTS_SPACE_EXTERNAL);
}

/*
 * Writes the parameters that give an address space its root page directory in the VRAM.
 */
void
nvrtx_pdir_params(
	uint8_t *params,
	uint64_t root,
	uint32_t space)
{
	/* The root's VRAM address, its four entries, the VRAM aperture (0) and the space. */
	objects_clear(params, NVRTX_PDIR_PARAMS_BYTES);
	nvrtx_put64(params, OBJECTS_PDIR_ADDRESS_AT, root);
	nvrtx_put32(params, OBJECTS_PDIR_ENTRIES_AT, OBJECTS_PDIR_ROOT_ENTRIES);
	nvrtx_put32(params, OBJECTS_PDIR_FLAGS_AT, 0U);
	nvrtx_put32(params, OBJECTS_PDIR_SPACE_AT, space);
}

/*
 * Picks the copy engine for the first channel from the device information table.
 *
 * The graphics engine's runlist is found first; the first copy engine on
 * another runlist is chosen (a copy engine that shares the graphics runlist
 * belongs to it).  Returns ENOENT when there is none.
 */
int
nvrtx_devinfo_pick(
	const uint8_t *params,
	size_t length,
	struct nvrtx_engine_pick *pick)
{
	uint32_t count;
	uint32_t index;
	uint32_t type;
	uint32_t runlist;
	uint32_t graphics_runlist;
	size_t at;

	/* The table must hold its count and every entry it counts. */
	if (length < OBJECTS_DEVINFO_ENTRIES_AT)
		return EINVAL;
	count = nvrtx_get32(params, OBJECTS_DEVINFO_COUNT_AT);
	if (count > OBJECTS_DEVINFO_MAX || OBJECTS_DEVINFO_ENTRIES_AT + (size_t)count * OBJECTS_DEVINFO_ENTRY_BYTES > length)
		return EINVAL;

	/* Finds the graphics engine's runlist. */
	graphics_runlist = 0xffffffffU;
	for (index = 0U; index < count; index++) {
		at = OBJECTS_DEVINFO_ENTRIES_AT + (size_t)index * OBJECTS_DEVINFO_ENTRY_BYTES;
		type = nvrtx_get32(params, at + OBJECTS_DEVINFO_TYPE_AT);
		if (type == OBJECTS_ENGINE_GRAPHICS)
			graphics_runlist = nvrtx_get32(params, at + OBJECTS_DEVINFO_RUNLIST_AT);
	}

	pick->graphics_runlist = graphics_runlist;

	/* Takes the first copy engine on a runlist of its own. */
	for (index = 0U; index < count; index++) {
		at = OBJECTS_DEVINFO_ENTRIES_AT + (size_t)index * OBJECTS_DEVINFO_ENTRY_BYTES;
		type = nvrtx_get32(params, at + OBJECTS_DEVINFO_TYPE_AT);
		runlist = nvrtx_get32(params, at + OBJECTS_DEVINFO_RUNLIST_AT);
		if (type < OBJECTS_ENGINE_COPY_FIRST || type > OBJECTS_ENGINE_COPY_LAST)
			continue;
		if (runlist == graphics_runlist)
			continue;

		/* Succeeded: the copy engine and its runlist. */
		pick->engine = type;
		pick->runlist = runlist;
		return 0;
	}

	/* No copy engine of its own runlist. */
	return ENOENT;
}

/*
 * Writes the parameters of a channel.
 *
 * The ring is at its GPU virtual address; the instance block (whose first
 * 0x200 bytes are the RAMFC) and USERD are in the VRAM; the method buffer
 * is in system memory.  The channel is a user channel without error
 * notifiers.
 */
void
nvrtx_channel_params(
	uint8_t *params,
	const struct nvrtx_channel_setup *setup)
{
	uint32_t flags;

	/* Clears every field, the reserved ones included. */
	objects_clear(params, NVRTX_CHANNEL_PARAMS_BYTES);

	/* The ring: its address and its entry count. */
	nvrtx_put64(params, OBJECTS_CHAN_RING_AT, setup->ring_va);
	nvrtx_put32(params, OBJECTS_CHAN_RING_ENTRIES_AT, setup->ring_entries);

	/* A physical, user channel whose USERD place follows its number (eight to a page, the page fixed). */
	flags = (setup->chid % OBJECTS_CHANNELS_PER_USERD) << OBJECTS_FLAG_USERD_INDEX_SHIFT;
	flags |= (setup->chid / OBJECTS_CHANNELS_PER_USERD) << OBJECTS_FLAG_USERD_PAGE_SHIFT;
	flags |= OBJECTS_FLAG_USERD_PAGE_FIXED;
	nvrtx_put32(params, OBJECTS_CHAN_FLAGS_AT, flags);

	/* The address space and the engine. */
	nvrtx_put32(params, OBJECTS_CHAN_SPACE_AT, setup->space);
	nvrtx_put32(params, OBJECTS_CHAN_ENGINE_AT, setup->engine);

	/* The instance block, USERD and RAMFC in the VRAM, the method buffer in system memory. */
	objects_memory(params, OBJECTS_CHAN_INSTANCE_AT, setup->instance, NVRTX_INSTANCE_BYTES, OBJECTS_SPACE_VRAM, 1U);
	objects_memory(params, OBJECTS_CHAN_USERD_AT, setup->userd, NVRTX_USERD_BYTES, OBJECTS_SPACE_VRAM, 1U);
	objects_memory(params, OBJECTS_CHAN_RAMFC_AT, setup->instance, NVRTX_RAMFC_BYTES, OBJECTS_SPACE_VRAM, 1U);
	objects_memory(params, OBJECTS_CHAN_METHODS_AT, setup->methods, setup->methods_size, OBJECTS_SPACE_SYSTEM, 0U);

	/* User privilege, no error notifiers. */
	nvrtx_put32(params, OBJECTS_CHAN_INTERNAL_AT, OBJECTS_INTERNAL_NO_NOTIFIERS);
}

/*
 * Writes the push buffer of one fence: release a semaphore after the work, then a system memory barrier.
 *
 * Returns the length in bytes (a multiple of 4).
 */
size_t
nvrtx_push_fence(
	uint8_t *push,
	uint64_t semaphore,
	uint32_t value)
{
	size_t at;

	/* The semaphore's address and value. */
	at = objects_method(push, 0U, OBJECTS_HOST_SEM_ADDRESS_LOW, 3U);
	nvrtx_put32(push, at, (uint32_t)(semaphore & 0xfffffffcU));
	nvrtx_put32(push, at + 4U, (uint32_t)(semaphore >> 32));
	nvrtx_put32(push, at + 8U, value);
	at += 12U;

	/* Releases it once the work before it is done. */
	at = objects_method(push, at, OBJECTS_HOST_SEM_EXECUTE, 1U);
	nvrtx_put32(push, at, OBJECTS_SEM_RELEASE | OBJECTS_SEM_RELEASE_WFI);
	at += 4U;

	/* Makes the release visible to the CPU: a system memory barrier. */
	at = objects_method(push, at, OBJECTS_HOST_MEM_OP_A, 4U);
	nvrtx_put32(push, at, 0U);
	nvrtx_put32(push, at + 4U, 0U);
	nvrtx_put32(push, at + 8U, 0U);
	nvrtx_put32(push, at + 12U, OBJECTS_MEM_OP_MEMBAR);
	at += 16U;

	/* Raises the non-stalling interrupt (the driver polls, so nothing waits on it). */
	at = objects_method(push, at, OBJECTS_HOST_NON_STALL, 1U);
	nvrtx_put32(push, at, 0U);
	at += 4U;

	/* Succeeded: the push buffer's length. */
	return at;
}

/*
 * Writes one ring entry pointing at a push buffer segment of a number of words.
 */
void
nvrtx_ring_entry(
	uint8_t *entry,
	uint64_t va,
	uint32_t words)
{
	/* The low word: the segment's address bits 31:2 (fetched unconditionally). */
	nvrtx_put32(entry, 0U, (uint32_t)(va & 0xfffffffcU));

	/* The high word: the address bits 39:32, the main level, and the length in words. */
	nvrtx_put32(entry, 4U, (uint32_t)((va >> 32) & 0xffU) | (words << 10));
}

/*
 * Writes the parameters of the free call for one object of the driver's client.
 */
void
nvrtx_free_params(
	uint8_t *params,
	uint32_t client,
	uint32_t object)
{
	/* The client, no parent, the object, and a zero status. */
	objects_clear(params, NVRTX_FREE_PARAMS_BYTES);
	nvrtx_put32(params, 0U, client);
	nvrtx_put32(params, 8U, object);
}

/* Clears a range of bytes. */
static void
objects_clear(
	uint8_t *bytes,
	size_t length)
{
	size_t at;

	/* Zeroes each byte. */
	for (at = 0U; at < length; at++)
		bytes[at] = 0U;
}

/* Writes an incrementing method header for subchannel 0 and returns where its data goes. */
static size_t
objects_method(
	uint8_t *push,
	size_t at,
	uint32_t method,
	uint32_t count)
{
	/* The header: incrementing, the count, subchannel 0, the method's word address. */
	nvrtx_put32(push, at, OBJECTS_METHOD_INCREMENT | (count << 16) | (method >> 2));

	/* Succeeded: the data follows the header. */
	return at + 4U;
}

/* Writes one memory description of the channel's parameters. */
static void
objects_memory(
	uint8_t *params,
	size_t at,
	uint64_t base,
	uint64_t size,
	uint32_t space,
	uint32_t cache)
{
	/* The base, the size, the address space and the cache attribute. */
	nvrtx_put64(params, at + OBJECTS_MEMORY_BASE_AT, base);
	nvrtx_put64(params, at + OBJECTS_MEMORY_SIZE_AT, size);
	nvrtx_put32(params, at + OBJECTS_MEMORY_SPACE_AT, space);
	nvrtx_put32(params, at + OBJECTS_MEMORY_CACHE_AT, cache);
}
