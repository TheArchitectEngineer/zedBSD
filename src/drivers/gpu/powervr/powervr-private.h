/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The PowerVR driver's own state, shared by its early stage, the memory
 * the firmware runs in and the later stage that starts the firmware.
 */

#ifndef KERN_DRIVERS_GPU_POWERVR_PRIVATE_H
#define KERN_DRIVERS_GPU_POWERVR_PRIVATE_H

#include <stddef.h>
#include <stdint.h>

#include <kern/pmem.h>

#include "drivers/gpu/powervr/powervr.h"
#include "drivers/gpu/powervr/firmware-image.h"
#include "drivers/gpu/powervr/mips-table.h"

/*
 * One block of memory the firmware's processor sees.
 *
 * The block is one physically contiguous run below the core's DMA limit,
 * mapped at heap_offset in the processor's page table.  The host reads
 * and writes it only through view, its uncached alias, with plain loads
 * and stores, so the core and the host see the same bytes without cache
 * maintenance.  A block with no view holds nothing.
 */
struct powervr_object {
	struct kern_pmem run;
	volatile uint8_t *view;
	uint32_t heap_offset;
	uint32_t size;
};

/*
 * The firmware's heap: the processor's page table and the next free
 * offset.
 *
 * Blocks at a place of their own (the code, the data, the three
 * configuration records) are placed first; the rest follow the highest
 * of those below the configuration area.  Nothing is reused while the
 * firmware runs, so the cursor only grows.
 */
struct powervr_heap {
	struct powervr_object table;
	uint32_t next_offset;
	unsigned bus_bits;
	uint64_t dma_limit;
};

/* Asks for a block anywhere in the heap's general part. */
#define POWERVR_HEAP_ANYWHERE		0xffffffffU

/*
 * Everything the running firmware needs from the host.
 *
 * File holds the firmware file as read, kept for as long as the core so
 * that a reset can load the program again without the root file system.
 * The command buffers and records are blocks of the heap; the interrupt
 * count is moved only by the interrupt handler and read by the later
 * stage.
 */
struct powervr_firmware {
	uint8_t *file;
	size_t file_size;
	struct powervr_fw_image image;
	struct powervr_heap heap;
	struct powervr_object code;
	struct powervr_object data;
	struct powervr_object connection;
	struct powervr_object os_init;
	struct powervr_object system_init;
	struct powervr_object kccb_control;
	struct powervr_object kccb;
	struct powervr_object kccb_returns;
	struct powervr_object fwccb_control;
	struct powervr_object fwccb;
	struct powervr_object os_data;
	struct powervr_object system_data;
	struct powervr_object power_sync;
	struct powervr_object hwr_info;
	struct powervr_object mmu_sync;
	struct powervr_object fault_page;
	struct powervr_object utilisation;
	struct powervr_object runtime;
	struct powervr_object trace_control;
	struct powervr_object trace_buffer;
	uint32_t fwccb_read;
	volatile uint32_t interrupts;
	int interrupt_installed;
};

/*
 * The board's one core.
 *
 * Filled by the early stage on the boot CPU; registers is NULL until the
 * core is kept.  The later stage owns the firmware state from the moment
 * its thread starts, and nothing else touches it until then.
 */
struct powervr_core {
	struct drv_powervr_description description;
	volatile uint8_t *registers;
	uint64_t core_rate;
	uint32_t branch;
	uint32_t version;
	uint32_t units;
	uint32_t config;
	struct powervr_firmware firmware;
};

/* Reads and writes the core's registers. */
uint32_t powervr_read32(const struct powervr_core *core, uint32_t offset);
uint64_t powervr_read64(const struct powervr_core *core, uint32_t offset);
void powervr_write32(struct powervr_core *core, uint32_t offset, uint32_t value);
void powervr_write64(struct powervr_core *core, uint32_t offset, uint64_t value);

/*
 * Releases the core's registers and powers it down: the resets held, the
 * clocks stopped in reverse and the domain off.
 */
void powervr_power_down(struct powervr_core *core);

/*
 * Starts the later stage on a thread of its own: it waits for the root
 * file system, loads the firmware and starts it.
 */
int powervr_late_start(struct powervr_core *core);

/* Sets up the heap and its empty page table. */
int powervr_heap_init(struct powervr_heap *heap, unsigned bus_bits, uint64_t dma_limit);

/* Releases the page table; every block must be gone first. */
void powervr_heap_fini(struct powervr_heap *heap);

/*
 * Makes a zeroed block of at least size bytes and maps it, at
 * heap_offset or anywhere (POWERVR_HEAP_ANYWHERE).
 */
int powervr_object_create(struct powervr_heap *heap, uint32_t size, uint32_t heap_offset, enum powervr_mips_caching caching, struct powervr_object *object);

/* Unmaps a block and returns its memory; a block that was never made is left alone. */
void powervr_object_destroy(struct powervr_heap *heap, struct powervr_object *object);

/* Reports the firmware processor's address of a byte of a block. */
uint32_t powervr_object_address(const struct powervr_object *object, uint32_t offset);

/* Reports the physical address of a byte of a block. */
uint64_t powervr_object_physical(const struct powervr_object *object, uint32_t offset);

/* Stores a 32-bit field into a block. */
void powervr_object_put32(struct powervr_object *object, uint32_t offset, uint32_t value);

/* Stores a 64-bit field into a block. */
void powervr_object_put64(struct powervr_object *object, uint32_t offset, uint64_t value);

/* Loads a 32-bit field from a block. */
uint32_t powervr_object_get32(const struct powervr_object *object, uint32_t offset);

/* Copies bytes of the kernel's into a block. */
void powervr_object_fill(struct powervr_object *object, uint32_t offset, const uint8_t *bytes, size_t length);

#endif
