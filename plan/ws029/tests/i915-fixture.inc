/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host fixture shared by the i915 unit tests: a synthetic BAR0, a fake
 * physical page pool and the kernel services the production sources call.
 *
 * The register file models only the handshakes the driver relies on:
 * forcewake acknowledgments, the self-clearing reset bit, the GGTT flush
 * register and the Gen11 interrupt bank, selector and identity registers.
 */

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../src/drivers/gpu/i915/internal.h"
#include "../../../src/drivers/gpu/i915/linux/i915-commands.inc"

#include <drivers/gpu.h>
#include <kern/clock.h>
#include <kern/device-io.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/platform.h>
#include <kern/sched.h>

#include "bootloader/include/amd64-handoff.h"

/* BAR0 is 16 MiB on the target: registers below, the GTT table above. */
#define FIXTURE_BAR0_BYTES	(16U * 1024U * 1024U)
#define FIXTURE_REGS_BYTES	(FIXTURE_BAR0_BYTES / 2U)
#define FIXTURE_POOL_PAGES	8192U
#define FIXTURE_POOL_BASE	0x100000000ULL

/* The synthetic register half of BAR0. */
static uint8_t fixture_regs[FIXTURE_REGS_BYTES];

/* The synthetic GTT table half of BAR0. */
static uint8_t fixture_gtt[FIXTURE_REGS_BYTES];

/* Fake physical pages handed out by the pmem stub, addressed from FIXTURE_POOL_BASE. */
static uint8_t fixture_pool[FIXTURE_POOL_PAGES * I915_PAGE_BYTES] __attribute__((aligned(4096)));
static unsigned fixture_pool_next;
static unsigned fixture_pool_live;

/* The PCI configuration word carrying the GGTT size field. */
static uint16_t fixture_gmch_control;

/* The GMADR aperture BAR the scanout preservation reads; a zero base means no BAR2. */
static uint64_t fixture_gmadr_base;
static uint64_t fixture_gmadr_size;

/* The boot framebuffer handoff, returned only when the test marks one present. */
static struct zbl6_framebuffer fixture_framebuffer;
static unsigned fixture_framebuffer_present;

/* Makes forcewake acknowledgments or the reset bit stay pending. */
static unsigned fixture_forcewake_stuck;
static unsigned fixture_reset_stuck;

/* Counts device-side effects the tests assert on. */
static unsigned fixture_flushes;
static unsigned fixture_maps;
static unsigned fixture_irq_allocations;
static unsigned fixture_irq_established;
static unsigned fixture_log_lines;
static uint64_t fixture_ticks;

/* Nesting depth of the single modeled spinlock; a second acquisition is a driver bug. */
static unsigned fixture_spin_depth;

/* The handler the driver installed for its vector. */
static drv_pci_irq_handler_t fixture_irq_handler;

/* BAR0 must be claimed exactly once before either window is mapped. */
static unsigned fixture_bar0_claimed;

/* Mutexes are uncontended in the single-threaded fixture; depth catches recursion. */
static unsigned fixture_mutex_depth;

/* The PCI parent modeled for attach: identity, driver data, enable state, service. */
static void *fixture_driver_data;
static unsigned fixture_enable_saved;
static unsigned fixture_memory_enabled;
static unsigned fixture_bus_master;
static const struct drv_pci_service_interface *fixture_service;
static void *fixture_service_argument;
static const struct drv_pci_driver *fixture_driver;
static unsigned fixture_dma_provider;

/* Publication is recorded, not performed; the ops table is what the tests exercise. */
static const struct drv_gpu_ops *fixture_gpu_ops;
static void *fixture_gpu_private;
static int fixture_gpu_token;

/*
 * The execlists model: a submit-queue load is recorded per engine and run
 * later by fixture_run_engines(), which parses the context ring like the
 * command streamer would, writes status-buffer events and raises the
 * interrupts through the installed handler.
 */
static unsigned fixture_load_pending[I915_ENGINE_COUNT];
static uint32_t fixture_load_low[I915_ENGINE_COUNT];
static uint32_t fixture_load_high[I915_ENGINE_COUNT];
static unsigned fixture_engine_runs[I915_ENGINE_COUNT];
static unsigned fixture_engine_hung[I915_ENGINE_COUNT];
static unsigned fixture_batches;
static void *fixture_irq_argument;
static unsigned fixture_run_in_sleep;
static unsigned fixture_completions;
static int fixture_completion_error;
static uint32_t fixture_reset_domains;
static unsigned fixture_resets;

/* The interrupt model: pending bits per bank and the identity each bit reports. */
static uint32_t fixture_pending[I915_IRQ_BANKS];
static uint32_t fixture_identity[I915_IRQ_BANKS][32];
static unsigned fixture_identity_invalid;
static uint32_t fixture_selected[I915_IRQ_BANKS];

static void fixture_reset(void) __attribute__((unused));
static uint32_t fixture_load32(const volatile void *address);
static void fixture_store32(volatile void *address, uint32_t value);
static uint32_t fixture_register_offset(const volatile void *address);

/* Returns the fixture to its power-on state between scenarios. */
static void
fixture_reset(void)
{
	memset(fixture_regs, 0, sizeof(fixture_regs));
	memset(fixture_gtt, 0, sizeof(fixture_gtt));
	memset(fixture_pool, 0, sizeof(fixture_pool));
	memset(fixture_pending, 0, sizeof(fixture_pending));
	memset(fixture_identity, 0, sizeof(fixture_identity));
	memset(fixture_selected, 0, sizeof(fixture_selected));
	fixture_pool_next = 0U;
	fixture_pool_live = 0U;
	fixture_gmch_control = (uint16_t)(3U << BDW_GMCH_GGMS_SHIFT);
	fixture_gmadr_base = 0U;
	fixture_gmadr_size = 0U;
	memset(&fixture_framebuffer, 0, sizeof(fixture_framebuffer));
	fixture_framebuffer_present = 0U;
	fixture_forcewake_stuck = 0U;
	fixture_reset_stuck = 0U;
	fixture_flushes = 0U;
	fixture_maps = 0U;
	fixture_irq_allocations = 0U;
	fixture_irq_established = 0U;
	fixture_log_lines = 0U;
	fixture_identity_invalid = 0U;
	fixture_driver_data = NULL;
	fixture_enable_saved = 0U;
	fixture_memory_enabled = 0U;
	fixture_bus_master = 0U;
	fixture_service = NULL;
	fixture_service_argument = NULL;
	fixture_gpu_ops = NULL;
	fixture_gpu_private = NULL;
	memset(fixture_load_pending, 0, sizeof(fixture_load_pending));
	memset(fixture_engine_runs, 0, sizeof(fixture_engine_runs));
	memset(fixture_engine_hung, 0, sizeof(fixture_engine_hung));
	fixture_batches = 0U;
	fixture_irq_argument = NULL;
	fixture_run_in_sleep = 0U;
	fixture_completions = 0U;
	fixture_completion_error = 0;
	fixture_reset_domains = 0U;
	fixture_resets = 0U;
	fixture_bar0_claimed = 0U;
}

/* Reads a little-endian word from fixture storage. */
static uint32_t
fixture_load32(
	const volatile void *address)
{
	const volatile uint8_t *bytes;

	bytes = address;
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

/* Stores a little-endian word into fixture storage. */
static void
fixture_store32(
	volatile void *address,
	uint32_t value)
{
	volatile uint8_t *bytes;

	bytes = address;
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	bytes[2] = (uint8_t)(value >> 16);
	bytes[3] = (uint8_t)(value >> 24);
}

/* Converts a mapped register address back to its BAR0 offset. */
static uint32_t
fixture_register_offset(
	const volatile void *address)
{
	const volatile uint8_t *bytes;

	bytes = address;
	assert(bytes >= fixture_regs && bytes < fixture_regs + sizeof(fixture_regs));
	return (uint32_t)(bytes - fixture_regs);
}

/*
 * Reads a register, presenting the pending summary and identity model.
 */
uint32_t
kern_mmio_read32(
	const volatile void *address)
{
	uint32_t offset;
	uint32_t value;
	unsigned bank;

	offset = fixture_register_offset(address);
	value = fixture_load32(address);

	/* The master register shows a bank summary bit while that bank has pending sources. */
	if (offset == GEN11_GFX_MSTR_IRQ) {
		for (bank = 0U; bank < I915_IRQ_BANKS; bank++) {
			if (fixture_pending[bank] != 0U)
				value |= GEN11_GT_DW_IRQ(bank);
		}
	}

	/* Bank words report the modeled pending sources. */
	for (bank = 0U; bank < I915_IRQ_BANKS; bank++) {
		if (offset == GEN11_GT_INTR_DW(bank))
			value = fixture_pending[bank];
	}

	return value;
}

/*
 * Writes a register, applying the modeled hardware reactions.
 */
void
kern_mmio_write32(
	volatile void *address,
	uint32_t value)
{
	uint32_t offset;
	unsigned bank;
	unsigned bit;

	offset = fixture_register_offset(address);

	/* A submit-queue load is recorded for the engine and run later. */
	if (offset == RING_EXECLIST_CONTROL(I915_RCS0_BASE) || offset == RING_EXECLIST_CONTROL(I915_BCS0_BASE)) {
		unsigned engine_index;
		uint32_t base;

		engine_index = I915_ENGINE_BCS0;
		base = I915_BCS0_BASE;
		if (offset == RING_EXECLIST_CONTROL(I915_RCS0_BASE)) {
			engine_index = I915_ENGINE_RCS0;
			base = I915_RCS0_BASE;
		}
		assert(value == EL_CTRL_LOAD);
		fixture_load_low[engine_index] = fixture_load32(fixture_regs + RING_EXECLIST_SQ_CONTENTS(base));
		fixture_load_high[engine_index] = fixture_load32(fixture_regs + RING_EXECLIST_SQ_CONTENTS(base) + 4U);
		fixture_load_pending[engine_index] = 1U;
		return;
	}

	/* A stop request makes the streamer idle at once; a reset request is answered ready. */
	if (offset == RING_MI_MODE(I915_RCS0_BASE) || offset == RING_MI_MODE(I915_BCS0_BASE)) {
		uint32_t mode;

		mode = fixture_load32(address);
		mode &= ~(value >> 16);
		mode |= value & (value >> 16);
		if ((value & (STOP_RING << 16)) != 0U && (value & STOP_RING) != 0U)
			mode |= MODE_IDLE;
		if ((value & (STOP_RING << 16)) != 0U && (value & STOP_RING) == 0U)
			mode &= ~MODE_IDLE;
		fixture_store32(address, mode);
		return;
	}

	if (offset == RING_RESET_CTL(I915_RCS0_BASE) || offset == RING_RESET_CTL(I915_BCS0_BASE)) {
		uint32_t control;

		control = fixture_load32(address);
		if ((value & (RESET_CTL_REQUEST_RESET << 16)) != 0U) {
			if ((value & RESET_CTL_REQUEST_RESET) != 0U) {
				control |= RESET_CTL_REQUEST_RESET | RESET_CTL_READY_TO_RESET;
			} else {
				control &= ~(RESET_CTL_REQUEST_RESET | RESET_CTL_READY_TO_RESET);
			}
		}
		fixture_store32(address, control);
		return;
	}

	/* A masked forcewake request is acknowledged unless the scenario holds it. */
	if (offset == FORCEWAKE_GT_GEN9 || offset == FORCEWAKE_RENDER_GEN9) {
		uint32_t ack_offset;

		ack_offset = FORCEWAKE_ACK_RENDER_GEN9;
		if (offset == FORCEWAKE_GT_GEN9)
			ack_offset = FORCEWAKE_ACK_GT_GEN9;
		fixture_store32(address, value);
		if ((value & (FORCEWAKE_KERNEL << 16)) != 0U && fixture_forcewake_stuck == 0U) {
			fixture_store32(fixture_regs + ack_offset, value & FORCEWAKE_KERNEL);
		}
		return;
	}

	/* The reset request completes immediately unless the scenario holds it; a reset unhangs engines. */
	if (offset == GEN6_GDRST) {
		fixture_reset_domains |= value;
		fixture_resets++;
		if (fixture_reset_stuck != 0U) {
			fixture_store32(address, value);
		} else {
			fixture_store32(address, 0U);
			memset(fixture_engine_hung, 0, sizeof(fixture_engine_hung));
		}
		return;
	}

	/* Flush requests are counted and the enable bit self-clears. */
	if (offset == GFX_FLSH_CNTL_GEN6) {
		fixture_flushes++;
		fixture_store32(address, 0U);
		return;
	}

	/* Selecting a bank bit latches that source's identity. */
	for (bank = 0U; bank < I915_IRQ_BANKS; bank++) {
		if (offset == GEN11_IIR_REG_SELECTOR(bank)) {
			fixture_selected[bank] = value;
			for (bit = 0U; bit < 32U; bit++) {
				if ((value & (1U << bit)) != 0U && fixture_identity_invalid == 0U) {
					fixture_store32(fixture_regs + GEN11_INTR_IDENTITY_REG(bank),
						fixture_identity[bank][bit] | GEN11_INTR_DATA_VALID);
				}
			}
			return;
		}

		/* Writing the valid bit back clears the identity. */
		if (offset == GEN11_INTR_IDENTITY_REG(bank)) {
			if ((value & GEN11_INTR_DATA_VALID) != 0U)
				fixture_store32(address, 0U);
			return;
		}

		/* Writing pending bits acknowledges them. */
		if (offset == GEN11_GT_INTR_DW(bank)) {
			fixture_pending[bank] &= ~value;
			return;
		}
	}

	fixture_store32(address, value);
}

/*
 * Stores one 64-bit GTT entry into the synthetic table window.
 */
void
kern_mmio_write64(
	volatile void *address,
	uint64_t value)
{
	volatile uint8_t *bytes;
	unsigned index;

	bytes = address;
	assert(bytes >= fixture_gtt && bytes + 8U <= fixture_gtt + sizeof(fixture_gtt));
	for (index = 0U; index < 8U; index++)
		bytes[index] = (uint8_t)(value >> (8U * index));
}

/*
 * Reads one 64-bit GTT entry from the synthetic table window.
 */
uint64_t
kern_mmio_read64(
	const volatile void *address)
{
	const volatile uint8_t *bytes;
	uint64_t value;
	unsigned index;

	bytes = address;
	value = 0U;
	for (index = 0U; index < 8U; index++)
		value |= (uint64_t)bytes[index] << (8U * index);
	return value;
}

/* Ordering barriers have no effect in the sequential fixture. */
void
kern_io_barrier(void)
{
	return;
}

void
kern_io_read_barrier(void)
{
	return;
}

void
kern_io_write_barrier(void)
{
	return;
}

void
kern_compiler_barrier(void)
{
	return;
}

/*
 * Advances synthetic time by one tick per sample so bounded waits end quickly.
 */
uint64_t
sched_ticks(void)
{
	fixture_ticks++;
	return fixture_ticks;
}

static void fixture_run_engines(void);

/*
 * A sleeping poll loop lets the modeled engines run when the scenario asks.
 */
void
sched_sleep(
	uint64_t timeout_tick)
{
	(void)timeout_tick;
	if (fixture_run_in_sleep != 0U)
		fixture_run_engines();
}

/* Wait queues: sleeping runs the modeled engines so a drain or poll can make progress. */
void
waitq_init(
	struct wait_queue *queue,
	const char *name)
{
	(void)name;
	memset(queue, 0, sizeof(*queue));
}

uint64_t
waitq_sequence(
	const struct wait_queue *queue)
{
	return queue->sequence;
}

void
waitq_wake_all(
	struct wait_queue *queue)
{
	queue->sequence++;
}

int
waitq_sleep(
	struct wait_queue *queue,
	struct spinlock *condition_lock,
	uint64_t observed,
	uint64_t deadline,
	unsigned flags)
{
	(void)queue;
	(void)observed;
	(void)deadline;
	(void)flags;

	/* The condition lock is released while the modeled engines run, as the kernel would. */
	spin_unlock(condition_lock);
	assert(fixture_run_in_sleep != 0U);
	fixture_run_engines();
	spin_lock(condition_lock);
	return 0;
}

/*
 * Records framework completions the request code delivers.
 */
void
drv_gpu_complete(
	struct drv_gpu_completion *completion,
	int error)
{
	(void)completion;
	fixture_completions++;
	fixture_completion_error = error;
}

void
drv_gpu_capacity_changed(
	struct drv_gpu_device *device)
{
	(void)device;
}

/* Resolves a GGTT byte offset to its backing fixture memory through the synthetic table. */
static uint8_t *
fixture_ggtt_resolve(
	uint32_t offset)
{
	uint64_t entry;
	uint8_t *page;

	entry = kern_mmio_read64(fixture_gtt + (size_t)(offset / I915_PAGE_BYTES) * 8U);
	assert((entry & GEN8_PAGE_PRESENT) != 0U);
	page = kern_pmem_to_kernel((hal_physaddr_t)(entry & I915_PPGTT_ADDRESS_MASK));
	assert(page != NULL);
	return page + (offset % I915_PAGE_BYTES);
}

/* Walks a four-level table from its top page to resolve one virtual address. */
static uint8_t *
fixture_ppgtt_resolve(
	uint64_t pml4,
	uint64_t va)
{
	uint64_t *table;
	uint64_t entry;
	unsigned level;

	table = kern_pmem_to_kernel((hal_physaddr_t)pml4);
	assert(table != NULL);
	for (level = 3U; level > 0U; level--) {
		entry = table[(va >> (12U + 9U * level)) & 511U];
		assert((entry & GEN8_PAGE_PRESENT) != 0U);
		table = kern_pmem_to_kernel((hal_physaddr_t)(entry & I915_PPGTT_ADDRESS_MASK));
		assert(table != NULL);
	}
	entry = table[(va >> 12) & 511U];
	assert((entry & GEN8_PAGE_PRESENT) != 0U);
	return (uint8_t *)kern_pmem_to_kernel((hal_physaddr_t)(entry & I915_PPGTT_ADDRESS_MASK)) + (va & 0xfffU);
}

/* Executes one command sequence; returns the dwords consumed, or 0 on a hang or batch end. */
static unsigned
fixture_execute(
	const uint32_t *dwords,
	unsigned available,
	uint64_t pml4,
	uint8_t *pphwsp,
	unsigned engine_index,
	unsigned *user_interrupt,
	unsigned *ended)
{
	uint32_t command;
	uint32_t opcode;
	uint64_t target;
	uint8_t *destination;
	unsigned length;

	*ended = 0U;
	command = dwords[0];
	opcode = command >> 23;
	length = 1U;

	/* Memory-interface commands the driver and tests emit. */
	if ((command >> 29) == 0U) {
		if (command == MI_NOOP || opcode == 0x08U || opcode == 0x05U) {
			length = 1U;
		} else if (command == MI_USER_INTERRUPT) {
			*user_interrupt += 1U;
		} else if (command == (MI_BATCH_BUFFER_END | 1U) || command == MI_BATCH_BUFFER_END) {
			*ended = 1U;
			length = 1U;
		} else if (opcode == 0x20U) {
			/* MI_STORE_DWORD_IMM: address is either GGTT or the context's space. */
			length = (command & 0xffU) + 2U;
			target = ((uint64_t)dwords[2] << 32) | dwords[1];
			if ((command & MI_USE_GGTT) != 0U) {
				destination = fixture_ggtt_resolve((uint32_t)target);
			} else {
				destination = fixture_ppgtt_resolve(pml4, target);
			}
			memcpy(destination, &dwords[3], 4U);
		} else if (opcode == 0x26U) {
			/* MI_FLUSH_DW with a post-sync store writes one dword. */
			length = (command & 0x3fU) + 2U;
			if ((command & MI_FLUSH_DW_OP_STOREDW) != 0U) {
				if ((command & MI_FLUSH_DW_STORE_INDEX) != 0U) {
					destination = pphwsp + (dwords[1] & 0xfffU);
				} else if ((dwords[1] & MI_FLUSH_DW_USE_GTT) != 0U) {
					destination = fixture_ggtt_resolve(dwords[1] & ~7U);
				} else {
					destination = fixture_ppgtt_resolve(pml4, (((uint64_t)dwords[2] << 32) | dwords[1]) & ~7ULL);
				}
				memcpy(destination, &dwords[3], 4U);
			}
		} else if (opcode == 0x31U) {
			/* MI_BATCH_BUFFER_START: the batch runs in the context's space until its end. */
			uint8_t *batch;
			unsigned consumed;
			unsigned batch_ended;
			unsigned position;

			length = 3U;
			target = ((uint64_t)dwords[2] << 32) | dwords[1];
			fixture_batches++;
			position = 0U;
			batch_ended = 0U;
			while (batch_ended == 0U && position < 16384U) {
				batch = fixture_ppgtt_resolve(pml4, target + (uint64_t)position * 4U);
				consumed = fixture_execute((const uint32_t *)batch, 16384U - position, pml4, pphwsp, engine_index, user_interrupt, &batch_ended);
				if (consumed == 0U && batch_ended == 0U) {
					fixture_engine_hung[engine_index] = 1U;
					return 0U;
				}
				position += consumed;
			}
			*ended = 0U;
		} else if (opcode == 0x1cU) {
			/* MI_SEMAPHORE_WAIT never satisfied models a hang. */
			fixture_engine_hung[engine_index] = 1U;
			return 0U;
		} else if (opcode == 0x22U) {
			length = (command & 0xffU) + 2U;
		} else {
			fprintf(stderr, "fixture: unknown MI command 0x%08x\n", command);
			abort();
		}
	} else if ((command >> 29) == 2U && ((command >> 22) & 0x7fU) == 0x53U) {
		/* XY_SRC_COPY_BLT: rows of 32-bit pixels are copied through the context's space. */
		unsigned rows;
		unsigned width;
		unsigned row;
		uint32_t dst_pitch;
		uint32_t src_pitch;
		uint64_t dst;
		uint64_t src;

		length = (command & 0xffU) + 2U;
		dst_pitch = dwords[1] & 0xffffU;
		rows = dwords[3] >> 16;
		width = dwords[3] & 0xffffU;
		dst = ((uint64_t)dwords[5] << 32) | dwords[4];
		src_pitch = dwords[7] & 0xffffU;
		src = ((uint64_t)dwords[9] << 32) | dwords[8];
		for (row = 0U; row < rows; row++) {
			memcpy(fixture_ppgtt_resolve(pml4, dst + (uint64_t)row * dst_pitch),
			    fixture_ppgtt_resolve(pml4, src + (uint64_t)row * src_pitch), (size_t)width * 4U);
		}
	} else if ((command >> 29) == 2U && ((command >> 22) & 0x7fU) == 0x50U) {
		/* XY_COLOR_BLT: rows of 32-bit pixels are filled with one value. */
		unsigned rows;
		unsigned width;
		unsigned row;
		unsigned column;
		uint32_t pitch;
		uint64_t dst;
		uint32_t *line;

		length = (command & 0xffU) + 2U;
		pitch = dwords[1] & 0xffffU;
		rows = dwords[3] >> 16;
		width = dwords[3] & 0xffffU;
		dst = ((uint64_t)dwords[5] << 32) | dwords[4];
		for (row = 0U; row < rows; row++) {
			line = (uint32_t *)fixture_ppgtt_resolve(pml4, dst + (uint64_t)row * pitch);
			for (column = 0U; column < width; column++)
				line[column] = dwords[6];
		}
	} else if ((command >> 29) == 3U && ((command >> 24) & 0x1fU) == 0x1aU) {
		/* PIPE_CONTROL: a qword post-sync write stores one dword. */
		length = (command & 0xffU) + 2U;
		if ((dwords[1] & PIPE_CONTROL_QW_WRITE) != 0U) {
			if ((dwords[1] & PIPE_CONTROL_STORE_DATA_INDEX) != 0U) {
				destination = pphwsp + (dwords[2] & 0xfffU);
			} else if ((dwords[1] & PIPE_CONTROL_GLOBAL_GTT_IVB) != 0U) {
				destination = fixture_ggtt_resolve(dwords[2] & ~7U);
			} else {
				destination = fixture_ppgtt_resolve(pml4, (((uint64_t)dwords[3] << 32) | dwords[2]) & ~7ULL);
			}
			memcpy(destination, &dwords[4], 4U);
		}
	} else {
		fprintf(stderr, "fixture: unknown command 0x%08x\n", command);
		abort();
	}

	assert(length <= available);
	return length;
}

/* Runs every recorded submit-queue load like the command streamer would. */
static void
fixture_run_engines(void)
{
	unsigned engine_index;
	uint32_t base;
	uint32_t ggtt_image;
	uint8_t *image;
	uint32_t *state;
	uint32_t sw_id;
	uint8_t *ring;
	uint32_t head;
	uint32_t tail;
	uint64_t pml4;
	unsigned user_interrupt;
	unsigned consumed;
	unsigned ended;
	uint32_t *hwsp;
	uint32_t write;
	uint32_t identity_sources;
	unsigned bit;

	for (engine_index = 0U; engine_index < I915_ENGINE_COUNT; engine_index++) {
		if (fixture_load_pending[engine_index] == 0U || fixture_engine_hung[engine_index] != 0U)
			continue;
		fixture_load_pending[engine_index] = 0U;
		fixture_engine_runs[engine_index]++;
		base = I915_BCS0_BASE;
		if (engine_index == I915_ENGINE_RCS0)
			base = I915_RCS0_BASE;

		/* The descriptor names the image; the image names the ring and the address space. */
		assert((fixture_load_low[engine_index] & GEN8_CTX_VALID) != 0U);
		ggtt_image = fixture_load_low[engine_index] & ~0xfffU;
		sw_id = (fixture_load_high[engine_index] >> 5) & 0x7ffU;
		image = fixture_ggtt_resolve(ggtt_image);
		state = (uint32_t *)(image + LRC_STATE_OFFSET);
		head = state[CTX_RING_HEAD] & HEAD_ADDR;
		tail = state[CTX_RING_TAIL] & TAIL_ADDR;
		pml4 = ((uint64_t)state[CTX_PDP0_UDW] << 32) | state[CTX_PDP0_LDW];
		user_interrupt = 0U;

		/* The ring is consumed from head to tail with wrap-around. */
		while (head != tail) {
			ring = fixture_ggtt_resolve(state[CTX_RING_START] + head);
			consumed = fixture_execute((const uint32_t *)ring, (I915_RING_BYTES - head) / 4U, pml4, image, engine_index, &user_interrupt, &ended);
			if (consumed == 0U)
				break;
			head = (head + consumed * 4U) % I915_RING_BYTES;
		}

		/* A hung engine leaves head behind and raises nothing. */
		if (fixture_engine_hung[engine_index] != 0U) {
			state[CTX_RING_HEAD] = head;
			continue;
		}

		state[CTX_RING_HEAD] = head;

		/* The status page receives a promotion and a completion event. */
		hwsp = (uint32_t *)fixture_ggtt_resolve(fixture_load32(fixture_regs + RING_HWS_PGA(base)));
		write = hwsp[I915_CSB_WRITE_INDEX] & 0xffU;
		write = (write + 1U) % I915_CSB_ENTRIES;
		hwsp[I915_HWS_CSB_BUF0_INDEX + 2U * write] = (sw_id << 15) | GEN12_CTX_STATUS_SWITCHED_TO_NEW_QUEUE;
		hwsp[I915_HWS_CSB_BUF0_INDEX + 2U * write + 1U] = GEN12_IDLE_CTX_ID << 15;
		write = (write + 1U) % I915_CSB_ENTRIES;
		hwsp[I915_HWS_CSB_BUF0_INDEX + 2U * write] = GEN12_IDLE_CTX_ID << 15;
		hwsp[I915_HWS_CSB_BUF0_INDEX + 2U * write + 1U] = sw_id << 15;
		hwsp[I915_CSB_WRITE_INDEX] = write;

		/* The interrupt identity carries the context switch and any user interrupt. */
		identity_sources = GT_CONTEXT_SWITCH_INTERRUPT;
		if (user_interrupt != 0U)
			identity_sources |= GT_RENDER_USER_INTERRUPT;
		bit = engine_index;
		fixture_pending[0] |= 1U << bit;
		fixture_identity[0][bit] = ((engine_index == I915_ENGINE_RCS0 ? RENDER_CLASS : COPY_ENGINE_CLASS) << 16) | identity_sources;
		if (fixture_irq_handler != NULL && fixture_irq_argument != NULL)
			(void)fixture_irq_handler(fixture_irq_argument);
	}
}

/*
 * Counts diagnostics without matching outcomes to log wording.
 */
void
kern_logf(
	const char *format,
	...)
{
	va_list arguments;

	fixture_log_lines++;
	va_start(arguments, format);
	if (getenv("I915_FIXTURE_VERBOSE") != NULL)
		vfprintf(stderr, format, arguments);
	va_end(arguments);
}

/* Heap allocation stands in for kernel memory. */
void *
kern_calloc(
	size_t count,
	size_t size)
{
	return calloc(count, size);
}

void
kern_free(
	void *pointer)
{
	free(pointer);
}

/*
 * Hands out fake physical pages with addresses inside the GPU's 39-bit range.
 */
int
kern_pmem_alloc_limited(
	size_t size,
	size_t alignment,
	uint64_t max_address,
	size_t boundary,
	struct kern_pmem *run)
{
	unsigned pages;

	(void)boundary;
	assert(alignment == I915_PAGE_BYTES);
	assert(max_address == I915_DMA_MAX_ADDRESS);
	pages = (unsigned)((size + I915_PAGE_BYTES - 1U) / I915_PAGE_BYTES);
	if (fixture_pool_next + pages > FIXTURE_POOL_PAGES)
		return ENOMEM;

	run->paddr = (hal_physaddr_t)(FIXTURE_POOL_BASE + (uint64_t)fixture_pool_next * I915_PAGE_BYTES);
	run->size = (size_t)pages * I915_PAGE_BYTES;
	fixture_pool_next += pages;
	fixture_pool_live += pages;
	return 0;
}

int
kern_pmem_free(
	struct kern_pmem *run)
{
	unsigned pages;

	pages = (unsigned)(run->size / I915_PAGE_BYTES);
	assert(fixture_pool_live >= pages);
	fixture_pool_live -= pages;
	return 0;
}

void *
kern_pmem_to_kernel(
	hal_physaddr_t address)
{
	uint64_t offset;

	if ((uint64_t)address < FIXTURE_POOL_BASE)
		return NULL;
	offset = (uint64_t)address - FIXTURE_POOL_BASE;
	if (offset >= sizeof(fixture_pool))
		return NULL;
	return fixture_pool + offset;
}

/* Locks are uncontended in the single-threaded fixture. */
void
spin_init(
	struct spinlock *lock,
	enum lock_rank rank,
	const char *name)
{
	(void)rank;
	(void)name;
	memset(lock, 0, sizeof(*lock));
}

void
spin_lock(
	struct spinlock *lock)
{
	(void)lock;
	assert(fixture_spin_depth == 0U);
	fixture_spin_depth = 1U;
}

void
spin_unlock(
	struct spinlock *lock)
{
	(void)lock;
	assert(fixture_spin_depth == 1U);
	fixture_spin_depth = 0U;
}

unsigned long
spin_lock_irqsave(
	struct spinlock *lock)
{
	spin_lock(lock);
	return 1UL;
}

void
spin_unlock_irqrestore(
	struct spinlock *lock,
	unsigned long enabled)
{
	assert(enabled == 1UL);
	spin_unlock(lock);
}

/*
 * Supplies the GGTT size field of the synthetic configuration space.
 */
int
drv_pci_device_config_read16(
	struct drv_pci_device *device,
	unsigned offset,
	uint16_t *value)
{
	(void)device;
	assert(offset == SNB_GMCH_CTRL);
	*value = fixture_gmch_control;
	return 0;
}

/*
 * Describes BAR0 as a 16 MiB 64-bit memory BAR, and BAR2 as the GMADR aperture
 * when a test has configured one for the scanout preservation path.
 */
int
drv_pci_device_bar(
	const struct drv_pci_device *device,
	unsigned index,
	struct drv_pci_bar *bar)
{
	(void)device;

	/* BAR0 carries the register and GTT halves the rest of the driver maps. */
	if (index == GEN4_GTTMMADR_BAR) {
		memset(bar, 0, sizeof(*bar));
		bar->index = index;
		bar->type = DRV_PCI_BAR_MEMORY64;
		bar->bus_address = 0x6054000000ULL;
		bar->size = FIXTURE_BAR0_BYTES;
		return 0;
	}

	/* BAR2 is the display aperture; a zero size means the test left it absent. */
	if (index == GEN4_GMADR_BAR && fixture_gmadr_size != 0U) {
		memset(bar, 0, sizeof(*bar));
		bar->index = index;
		bar->type = DRV_PCI_BAR_MEMORY64;
		bar->bus_address = fixture_gmadr_base;
		bar->size = fixture_gmadr_size;
		return 0;
	}

	return EINVAL;
}

/*
 * Returns the boot framebuffer handoff only for the name the driver asks for
 * and only when a test has populated one.
 */
void *
kern_boot_handoff(
	const char *name)
{
	assert(name != NULL);
	if (strcmp(name, "pcat.framebuffer") != 0)
		return NULL;
	if (fixture_framebuffer_present == 0U)
		return NULL;
	return &fixture_framebuffer;
}

int
drv_pci_device_claim_bar(
	struct drv_pci_device *device,
	unsigned index)
{
	(void)device;
	assert(index == GEN4_GTTMMADR_BAR);
	if (fixture_bar0_claimed != 0U)
		return EBUSY;
	fixture_bar0_claimed = 1U;
	return 0;
}

void
drv_pci_device_release_bar(
	struct drv_pci_device *device,
	unsigned index)
{
	(void)device;
	assert(index == GEN4_GTTMMADR_BAR);
	assert(fixture_bar0_claimed == 1U);
	assert(fixture_maps == 0U);
	fixture_bar0_claimed = 0U;
}

/*
 * Maps either half of the synthetic BAR0.
 */
int
drv_pci_device_map_bar_region(
	struct drv_pci_device *device,
	unsigned index,
	uint64_t offset,
	size_t size,
	unsigned flags,
	struct drv_pci_mapping *mapping)
{
	(void)device;
	assert(index == GEN4_GTTMMADR_BAR);
	assert((flags & DRV_PCI_MAP_NOCACHE) != 0U);
	memset(mapping, 0, sizeof(*mapping));
	if (offset == 0U) {
		assert(size == FIXTURE_REGS_BYTES);
		mapping->address = fixture_regs;
	} else {
		assert(offset == FIXTURE_REGS_BYTES);
		assert(size <= FIXTURE_REGS_BYTES);
		mapping->address = fixture_gtt;
	}
	mapping->size = size;
	mapping->type = DRV_PCI_BAR_MEMORY64;
	fixture_maps++;
	return 0;
}

void
drv_pci_device_unmap_bar(
	struct drv_pci_device *device,
	struct drv_pci_mapping *mapping)
{
	(void)device;
	assert(fixture_maps != 0U);
	fixture_maps--;
	memset(mapping, 0, sizeof(*mapping));
}

/*
 * Allocates one synthetic MSI vector.
 */
int
drv_pci_device_allocate_irqs(
	struct drv_pci_device *device,
	unsigned flags,
	unsigned minimum,
	unsigned maximum,
	struct drv_pci_irq *irq,
	unsigned *count)
{
	(void)device;
	assert(flags == DRV_PCI_IRQ_ALLOW_MSI);
	assert(minimum == 1U && maximum == 1U);
	memset(irq, 0, sizeof(*irq));
	irq->type = DRV_PCI_IRQ_MSI;
	irq->vector = 45U;
	*count = 1U;
	fixture_irq_allocations++;
	return 0;
}

void
drv_pci_device_free_irqs(
	struct drv_pci_device *device,
	struct drv_pci_irq *irq,
	unsigned count)
{
	(void)device;
	(void)irq;
	assert(count == 1U);
	assert(fixture_irq_allocations != 0U);
	fixture_irq_allocations--;
}

int
drv_pci_device_establish_irq(
	struct drv_pci_device *device,
	const struct drv_pci_irq *irq,
	drv_pci_irq_handler_t handler,
	void *argument,
	const char *name,
	void **cookie)
{
	(void)device;
	(void)irq;
	(void)name;
	assert(handler != NULL);
	fixture_irq_handler = handler;
	fixture_irq_argument = argument;
	*cookie = argument;
	fixture_irq_established++;
	return 0;
}

int
drv_pci_device_disestablish_irq_checked(
	struct drv_pci_device *device,
	void *cookie)
{
	(void)device;
	assert(cookie != NULL);
	assert(fixture_irq_established != 0U);
	fixture_irq_established--;
	return 0;
}

int
mutex_init(
	struct mutex *mutex,
	enum lock_rank rank,
	const char *name)
{
	(void)rank;
	(void)name;
	memset(mutex, 0, sizeof(*mutex));
	return 0;
}

void
mutex_lock(
	struct mutex *mutex)
{
	(void)mutex;
	assert(fixture_mutex_depth == 0U);
	fixture_mutex_depth = 1U;
}

void
mutex_unlock(
	struct mutex *mutex)
{
	(void)mutex;
	assert(fixture_mutex_depth == 1U);
	fixture_mutex_depth = 0U;
}

uint16_t
drv_pci_device_product(
	const struct drv_pci_device *device)
{
	(void)device;
	return 0x46a8U;
}

uint8_t
drv_pci_device_revision(
	const struct drv_pci_device *device)
{
	(void)device;
	return 0x0cU;
}

struct drv_dma_device *
drv_pci_device_dma(
	struct drv_pci_device *device)
{
	(void)device;
	return (struct drv_dma_device *)&fixture_dma_provider;
}

int
drv_pci_device_save_enable_state(
	struct drv_pci_device *device,
	struct drv_pci_enable_state *state)
{
	(void)device;
	assert(state->private_data[0] == 0U);
	state->private_data[0] = 1U;
	fixture_enable_saved = 1U;
	return 0;
}

int
drv_pci_device_restore_enable_state(
	struct drv_pci_device *device,
	struct drv_pci_enable_state *state)
{
	(void)device;
	assert(state->private_data[0] == 1U);
	state->private_data[0] = 0U;
	fixture_enable_saved = 0U;
	fixture_memory_enabled = 0U;
	return 0;
}

int
drv_pci_device_enable_memory(
	struct drv_pci_device *device)
{
	(void)device;
	fixture_memory_enabled = 1U;
	return 0;
}

int
drv_pci_device_set_bus_master(
	struct drv_pci_device *device,
	bool enabled)
{
	(void)device;
	fixture_bus_master = enabled ? 1U : 0U;
	return 0;
}

int
drv_pci_device_set_driver_data(
	struct drv_pci_device *device,
	void *data)
{
	(void)device;
	fixture_driver_data = data;
	return 0;
}

void *
drv_pci_device_driver_data(
	const struct drv_pci_device *device)
{
	(void)device;
	return fixture_driver_data;
}

int
drv_pci_device_set_service(
	struct drv_pci_device *device,
	const struct drv_pci_service_interface *interface,
	void *argument)
{
	(void)device;
	assert(fixture_service == NULL);
	fixture_service = interface;
	fixture_service_argument = argument;
	return 0;
}

int
drv_pci_driver_register(
	struct drv_pci_driver *driver)
{
	fixture_driver = driver;
	return 0;
}

int
drv_gpu_register(
	const struct drv_gpu_ops *ops,
	void *private_data,
	struct drv_gpu_device **result)
{
	assert(fixture_gpu_ops == NULL);
	assert(ops->version == DRV_GPU_INTERFACE_VERSION);
	assert(ops->size == sizeof(*ops));
	fixture_gpu_ops = ops;
	fixture_gpu_private = private_data;
	*result = (struct drv_gpu_device *)&fixture_gpu_token;
	return 0;
}

int
drv_gpu_unregister(
	struct drv_gpu_device *device)
{
	assert(device == (struct drv_gpu_device *)&fixture_gpu_token);
	assert(fixture_gpu_ops != NULL);
	fixture_gpu_ops = NULL;
	fixture_gpu_private = NULL;
	return 0;
}

/* Prepares a device whose register and table windows are already mapped. */
static void fixture_device(struct i915_device *device) __attribute__((unused));

static void
fixture_device(
	struct i915_device *device)
{
	memset(device, 0, sizeof(*device));
	device->regs.address = fixture_regs;
	device->regs.size = FIXTURE_REGS_BYTES;
	device->regs.type = DRV_PCI_BAR_MEMORY64;
	spin_init(&device->irq_lock, LOCK_RANK_DEVICE, "fixture");
}

/*
 * WS031 integration: i915.c attaches the native Vulkan executor. These stubs
 * keep the WS029 native-stream fixtures linking without pulling the executor.
 */
int
drv_i915_vk_attach(struct i915_device *device, struct i915_vk_device **out)
{
	(void)device;
	*out = NULL;
	return 0;
}

void
drv_i915_vk_detach(struct i915_vk_device *vk)
{
	(void)vk;
}

int
drv_i915_vk_open(struct i915_vk_device *vk, struct i915_session *gpu, struct i915_vk_session **out)
{
	(void)vk;
	(void)gpu;
	*out = NULL;
	return 0;
}

void
drv_i915_vk_close(struct i915_vk_session *session)
{
	(void)session;
}

int
drv_i915_vk_command(struct i915_vk_session *session, const void *wire, size_t bytes, void *reply, size_t *reply_bytes)
{
	(void)session;
	(void)wire;
	(void)bytes;
	(void)reply;
	(void)reply_bytes;
	return 0;
}
