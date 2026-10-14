/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The VRAM allocator and the CPU's window onto the VRAM (stage P4m).
 *
 * The allocator hands out runs of the last usable VRAM region below the
 * fixed arenas the earlier stages place at its top (the first block, the
 * display's instance memory, the graphics and compute arenas), from the top
 * down.  Those stages keep their fixed places; new users (the display's
 * surfaces, the buffer objects of the GPU device) take their VRAM here.
 *
 * The window (BAR1) follows nouveau's GSP path (r535's BAR code): the
 * resource manager owns the window's root page table, whose address the
 * static information gives; the driver builds the lower levels for the
 * first part of the window in its own VRAM, writes the root's first entry
 * to point at them, and invalidates the hub's TLB with the root named.  A
 * buffer object is then mapped by writing its pages' entries and
 * invalidating again.  The CPU maps the window uncached.
 *
 * P4m does not fail the boot: when the window cannot be set up the later
 * stages run as before (they reach the VRAM through the register window,
 * PRAMIN) and only the users of the window find it missing.
 *
 * On the GPU the machine boots on, the firmware's console lies at the start
 * of the window and of the VRAM (the GOP's mapping in the window's physical
 * mode).  The resource manager makes the whole window virtual, so P4m maps
 * the window's first pages back onto the console before anything else
 * takes the window, as the vendor's driver does (open-gpu-kernel-modules,
 * MIT: osinit.c's console size and kern_bus_gm107.c's preserved console
 * mapping at offset 0).  The CPU's own mapping of the window starts after
 * the console, so the console's memory is never mapped twice.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/clock.h>
#include <kern/kcrt.h>
#include <kern/kmem.h>
#include <uapi/errno.h>

#include "bootloader/include/amd64-handoff.h"
#include "drivers/gpu/nvrtx/nvrtx-gpu.h"

/*
 * The VRAM the fixed arenas take at the top of the region (the compute
 * arena's reserve is the deepest), with 2 MiB of slack for their alignment.
 */
#define VRAM_FIXED_RESERVE		(NVRTX_COMPUTE_ARENA_RESERVE + 0x200000ULL)

/* The allocator's top is on this boundary. */
#define VRAM_HEAP_ALIGN			0x200000ULL

/* The largest part of the window the driver's tables map: one first-level directory entry (512 MiB). */
#define VRAM_BAR1_WINDOW_MAX		0x20000000ULL

/* The window's page tables are on a 64 KiB boundary, so one window position of PRAMIN starts them. */
#define VRAM_TABLES_ALIGN		0x10000ULL

/* Where the page tables start in the tables' block (after the root and the three directories). */
#define VRAM_TABLES_PTE_AT		0x4000ULL

/*
 * The window's mode register (Turing's BAR1 block, nouveau's tu102 bar):
 * bit 31 set is the virtual mode.  The bind's pending bits are in the
 * status register, polled when the mode is put back.
 */
#define VRAM_REG_BAR1_BLOCK		0xb80f40U
#define VRAM_BAR1_BLOCK_VIRTUAL		0x80000000U
#define VRAM_REG_BAR_STATUS		0xb80f50U
#define VRAM_BAR1_PENDING		0x00000003U
#define VRAM_BAR1_POLLS			1000U
#define VRAM_BAR1_POLL_US		10U

/* The console's VRAM is reserved in whole 64 KiB (osinit.c's alignment). */
#define VRAM_CONSOLE_ALIGN		0x10000ULL

/* The self-test's run, and the values written each way. */
#define VRAM_TEST_BYTES			0x10000U
#define VRAM_TEST_FILL			0x5a5a1234U
#define VRAM_TEST_WORD			0xc3c3abcdU
#define VRAM_TEST_WORD_AT		20U

static int vram_bar1_tables(struct nvrtx_gpu *gpu);
static int vram_console_map(struct nvrtx_gpu *gpu);
static int vram_bar1_cpu(struct nvrtx_gpu *gpu);
static int vram_bar1_test(struct nvrtx_gpu *gpu);
static int vram_write_ptes(struct nvrtx_gpu *gpu, uint64_t bar1, uint64_t vram, uint64_t size, int clear);

/*
 * Runs stage P4m: the VRAM allocator, then the window and its self-test.
 *
 * Returns 0 even when the window could not be set up (the stage's marks say
 * why); an error only when the allocator itself cannot be placed.
 */
int
nvrtx_memory_start(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_memory_state *memory;
	uint64_t base;
	uint64_t top;
	uint64_t heap_top;
	uint32_t region;
	int error;

	memory = &gpu->memory_state;

	/* Finds the last usable region; the fixed arenas take its top. */
	nvrtx_stage_mark("P4m begin");
	error = nvrtx_mmu_vram_region(gpu->static_info, &base, &top, &region);
	if (error != 0) {
		nvrtx_stage_mark("P4m fail: no usable VRAM region (%d)", error);
		return error;
	}

	/* A kept console holds the VRAM's start; the allocator stays above it. */
	if (gpu->console.kept != 0 && base < gpu->console.size)
		base = gpu->console.size;

	/* The allocator covers the rest of the region, below the fixed arenas. */
	if (top <= base || top - base <= VRAM_FIXED_RESERVE) {
		nvrtx_stage_mark("P4m fail: region %u is too small (%llx..%llx)", (unsigned)region, (unsigned long long)base, (unsigned long long)top);
		return ENOSPC;
	}

	/* Starts the allocator over the region below them. */
	heap_top = (top - VRAM_FIXED_RESERVE) & ~(VRAM_HEAP_ALIGN - 1U);
	mutex_lock(&memory->lock);

	nvrtx_heap_init(&memory->vram, base, heap_top);
	memory->ready = 1;

	mutex_unlock(&memory->lock);

	/* Shows where the allocator lies. */
	nvrtx_stage_mark("P4m vram: region %u, heap %llx..%llx (%llu MiB)",
	    (unsigned)region,
	    (unsigned long long)base,
	    (unsigned long long)heap_top,
	    (unsigned long long)((heap_top - base) >> 20));

	/* Builds the window's tables under the resource manager's root. */
	error = vram_bar1_tables(gpu);
	if (error != 0) {
		nvrtx_stage_mark("P4m: no bar1 window (%d); the later stages use PRAMIN", error);
		return 0;
	}

	/* Gives the firmware's console its place in the window back; the caller decides what a failure means. */
	error = vram_console_map(gpu);
	if (error != 0)
		return 0;

	/* Maps the window for the CPU. */
	error = vram_bar1_cpu(gpu);
	if (error != 0) {
		nvrtx_stage_mark("P4m: the CPU cannot map bar1 (%d)", error);
		return 0;
	}

	/* Checks the window both ways before anyone relies on it. */
	error = vram_bar1_test(gpu);
	if (error != 0) {
		mutex_lock(&memory->lock);

		memory->bar1_ready = 0;

		mutex_unlock(&memory->lock);

		return 0;
	}

	/* Succeeded: the allocator and the window are ready. */
	return 0;
}

/*
 * Gives back the CPU's mapping of the window and its claim.
 *
 * The VRAM itself needs no release: the GSP's unload and the end of the
 * resource manager leave it unused.
 */
void
nvrtx_memory_release(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_memory_state *memory;

	memory = &gpu->memory_state;

	/* Unmaps the window. */
	if (memory->bar1_mapped != 0) {
		drv_pci_device_unmap_bar(gpu->identity.pci, &memory->bar1_mapping);
		memory->bar1_mapped = 0;
	}

	/* Releases its claim. */
	if (memory->bar1_claimed != 0) {
		drv_pci_device_release_bar(gpu->identity.pci, gpu->identity.windows[1].bar);
		memory->bar1_claimed = 0;
	}

	/* Nobody may take VRAM or map it any more. */
	memory->bar1_ready = 0;
	memory->ready = 0;
}

/*
 * Takes a run of VRAM of whole pages for a buffer object, unmapped.
 *
 * align is a power of two of at least a page (smaller is raised to a page).
 * Returns ENODEV before P4m, or the allocator's ENOSPC.
 */
int
nvrtx_bo_alloc(
	struct nvrtx_gpu *gpu,
	uint64_t size,
	uint64_t align,
	struct nvrtx_bo *bo)
{
	struct nvrtx_memory_state *memory;
	uint64_t address;
	int error;

	memory = &gpu->memory_state;

	/* Whole pages, on at least a page boundary. */
	if (size == 0U)
		return EINVAL;
	size = (size + NVRTX_GSP_PAGE_SIZE - 1U) & ~((uint64_t)NVRTX_GSP_PAGE_SIZE - 1U);
	if (align < NVRTX_GSP_PAGE_SIZE)
		align = NVRTX_GSP_PAGE_SIZE;

	/* Takes the run from the allocator. */
	mutex_lock(&memory->lock);

	error = ENODEV;
	if (memory->ready != 0)
		error = nvrtx_heap_alloc(&memory->vram, size, align, &address);

	mutex_unlock(&memory->lock);

	/* Reports a missing allocator or a full one. */
	if (error != 0)
		return error;

	/* Succeeded: the run is the object's, not yet in the window. */
	bo->vram = address;
	bo->size = size;
	bo->bar1 = 0U;
	bo->bar1_mapped = 0;
	return 0;
}

/*
 * Maps a buffer object into the CPU's window.
 *
 * Returns ENODEV without a working window, or ENOSPC when the window has
 * no room.
 */
int
nvrtx_bo_map(
	struct nvrtx_gpu *gpu,
	struct nvrtx_bo *bo)
{
	struct nvrtx_memory_state *memory;
	uint64_t address;
	int error;

	memory = &gpu->memory_state;

	/* A mapped object stays where it is. */
	if (bo->bar1_mapped != 0)
		return 0;

	/* Takes a run of the window and points its pages at the object's VRAM. */
	mutex_lock(&memory->lock);

	error = ENODEV;
	if (memory->bar1_ready != 0)
		error = nvrtx_heap_alloc(&memory->bar1, bo->size, NVRTX_GSP_PAGE_SIZE, &address);
	if (error == 0) {
		error = vram_write_ptes(gpu, address, bo->vram, bo->size, 0);
		if (error != 0)
			(void)nvrtx_heap_free(&memory->bar1, address, bo->size);
	}

	mutex_unlock(&memory->lock);

	/* Reports a missing window, a full one or a write that failed. */
	if (error != 0)
		return error;

	/* Succeeded: the CPU reaches the object at its place in the window. */
	bo->bar1 = address;
	bo->bar1_mapped = 1;
	return 0;
}

/*
 * Frees a buffer object: unmaps it from the window, then gives its VRAM back.
 *
 * The caller has made sure the GPU no longer reads it.  A page entry that
 * cannot be cleared keeps the window's run taken, so it is never reused.
 */
void
nvrtx_bo_free(
	struct nvrtx_gpu *gpu,
	struct nvrtx_bo *bo)
{
	struct nvrtx_memory_state *memory;
	int error;

	memory = &gpu->memory_state;

	/* An object never allocated has nothing to give back. */
	if (bo->size == 0U)
		return;

	/* Clears the window's entries and returns both runs. */
	mutex_lock(&memory->lock);

	if (bo->bar1_mapped != 0) {
		error = vram_write_ptes(gpu, bo->bar1, bo->vram, bo->size, 1);
		if (error == 0)
			(void)nvrtx_heap_free(&memory->bar1, bo->bar1, bo->size);
	}

	if (memory->ready != 0)
		(void)nvrtx_heap_free(&memory->vram, bo->vram, bo->size);

	mutex_unlock(&memory->lock);

	/* Forgets the object, so a second free finds nothing. */
	bo->vram = 0U;
	bo->size = 0U;
	bo->bar1 = 0U;
	bo->bar1_mapped = 0;
}

/*
 * Reports where the CPU reads and writes a mapped buffer object.
 */
volatile uint8_t *
nvrtx_bo_cpu(
	struct nvrtx_gpu *gpu,
	const struct nvrtx_bo *bo)
{
	volatile uint8_t *window;

	/* The object's place in the uncached mapping of the window, which starts after the console. */
	window = (volatile uint8_t *)gpu->memory_state.bar1_mapping.address;

	/* Succeeded: the object's first byte. */
	return window + (bo->bar1 - gpu->memory_state.bar1_cpu_from);
}

/*
 * Reports the CPU physical address of a mapped buffer object, for a user mapping.
 */
uint64_t
nvrtx_bo_bus(
	struct nvrtx_gpu *gpu,
	const struct nvrtx_bo *bo)
{
	uint64_t physical;

	/* The object's place in the window's physical range. */
	physical = gpu->memory_state.bar1_mapping.physical_address + (bo->bar1 - gpu->memory_state.bar1_cpu_from);

	/* Succeeded: the address. */
	return physical;
}

/*
 * Decides, before the GSP boot (P0), whether the firmware's console on this
 * GPU is kept through it.
 *
 * The console is kept when the loader's picture starts the VRAM window, the
 * window is in its physical mode (so the picture is at the VRAM's start
 * too) and the picture is in the 32-bit format the display shows (B, G, R,
 * X in memory).  Returns 0 when the boot may go on (the console is kept, or
 * the GPU has none: the attach kept it only for nvrtx.gsp=1), EBUSY when
 * the console is on the GPU but cannot be kept.
 */
int
nvrtx_console_check(
	struct nvrtx_gpu *gpu)
{
	const struct nvrtx_device *identity;
	struct nvrtx_console *console;
	uint64_t bytes;
	uint32_t block;

	identity = &gpu->identity;
	console = &gpu->console;

	/* A GPU the firmware's picture is not on has no console to keep. */
	if (identity->gop_on_gpu == 0)
		return 0;

	/* Reads the window's mode as the firmware left it. */
	block = nvrtx_rd32(gpu, VRAM_REG_BAR1_BLOCK);
	console->bar1_block = block;

	/* Refuses a picture away from the window's start, which the resource manager does not keep. */
	if (identity->gop_offset != 0U) {
		nvrtx_stage_mark("P0 console at bar1 +%llx, not its start: cannot be kept", (unsigned long long)identity->gop_offset);
		return EBUSY;
	}

	/* Refuses a window the firmware put in its virtual mode: the picture's VRAM is unknown. */
	if ((block & VRAM_BAR1_BLOCK_VIRTUAL) != 0U) {
		nvrtx_stage_mark("P0 console: bar1 is virtual (%08x): cannot be kept", (unsigned)block);
		return EBUSY;
	}

	/* Refuses a format the display's window is not given and a picture of no size. */
	if (identity->gop_format != ZBL6_FRAMEBUFFER_BGRX8888 ||
	    identity->gop_width == 0U ||
	    identity->gop_height == 0U ||
	    identity->gop_stride < identity->gop_width) {
		nvrtx_stage_mark("P0 console %ux%u stride %u format %u: cannot be kept",
		    (unsigned)identity->gop_width,
		    (unsigned)identity->gop_height,
		    (unsigned)identity->gop_stride,
		    (unsigned)identity->gop_format);
		return EBUSY;
	}

	/* Describes the console and the VRAM it holds, in whole 64 KiB. */
	bytes = (uint64_t)identity->gop_stride * 4U * identity->gop_height;
	console->size = (bytes + VRAM_CONSOLE_ALIGN - 1U) & ~(VRAM_CONSOLE_ALIGN - 1U);
	console->width = identity->gop_width;
	console->height = identity->gop_height;
	console->pitch = identity->gop_stride * 4U;
	console->kept = 1;

	/* Succeeded: the boot keeps the console. */
	nvrtx_stage_mark("P0 console %ux%u pitch %u kept: %llu KiB at the VRAM's start",
	    (unsigned)console->width,
	    (unsigned)console->height,
	    (unsigned)console->pitch,
	    (unsigned long long)(console->size >> 10));
	return 0;
}

/*
 * Puts the window back in the mode the firmware left it in, once the GSP
 * has stopped, so the console reaches its VRAM again without the resource
 * manager's tables.  Nothing is done for a console not kept or a mode that
 * did not change.
 */
void
nvrtx_console_restore(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_console *console;
	uint32_t block;
	uint32_t restored;
	uint32_t status;
	uint32_t poll;

	console = &gpu->console;

	/* A console not kept has nothing to restore. */
	if (console->kept == 0)
		return;

	/* A window still in the firmware's mode needs nothing. */
	block = nvrtx_rd32(gpu, VRAM_REG_BAR1_BLOCK);
	if (block == console->bar1_block)
		return;

	/* Writes the firmware's mode and waits for the bind to settle. */
	nvrtx_wr32(gpu, VRAM_REG_BAR1_BLOCK, console->bar1_block);
	status = 0U;
	for (poll = 0U; poll < VRAM_BAR1_POLLS; poll++) {
		status = nvrtx_rd32(gpu, VRAM_REG_BAR_STATUS);
		if ((status & VRAM_BAR1_PENDING) == 0U)
			break;

		kern_usleep_range(VRAM_BAR1_POLL_US, VRAM_BAR1_POLL_US);
	}

	/* Says what the window was put back to; the resource manager's mapping of the console is gone. */
	restored = nvrtx_rd32(gpu, VRAM_REG_BAR1_BLOCK);
	console->mapped = 0;
	nvrtx_stage_mark("P7 console: bar1 mode %08x again (was %08x, status %08x)",
	    (unsigned)restored,
	    (unsigned)block,
	    (unsigned)status);
}

/*
 * Builds the window's directories and page tables in VRAM of the allocator
 * and points the resource manager's root at them.
 */
static int
vram_bar1_tables(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_memory_state *memory;
	const struct nvrtx_window *window;
	uint8_t entry[8];
	uint8_t *image;
	uint64_t tables;
	size_t length;
	uint32_t low;
	uint32_t high;
	int error;

	memory = &gpu->memory_state;
	window = &gpu->identity.windows[1];

	/* The window must exist and hold at least one page table's reach. */
	if (window->present == 0 || window->size < NVRTX_MMU_RANGE_BYTES)
		return ENODEV;

	/* The resource manager's root must be a page of the VRAM. */
	memory->bar1_root = nvrtx_get64(gpu->static_info, NVRTX_STATIC_BAR1_ROOT_AT);
	if (memory->bar1_root == 0U || (memory->bar1_root & (NVRTX_GSP_PAGE_SIZE - 1U)) != 0U)
		return EINVAL;

	/* The tables map the window's first 512 MiB at most, in whole page tables. */
	memory->bar1_window = window->size;
	if (memory->bar1_window > VRAM_BAR1_WINDOW_MAX)
		memory->bar1_window = VRAM_BAR1_WINDOW_MAX;
	memory->bar1_window &= ~(NVRTX_MMU_RANGE_BYTES - 1U);

	/* Takes the tables' VRAM from the allocator. */
	length = nvrtx_mmu_gr_tables_bytes(memory->bar1_window);
	mutex_lock(&memory->lock);

	error = nvrtx_heap_alloc(&memory->vram, length, VRAM_TABLES_ALIGN, &tables);

	mutex_unlock(&memory->lock);

	/* Reports an allocator without room for them. */
	if (error != 0)
		return error;

	memory->bar1_tables = tables;

	/* Reads the root's first entry as the resource manager left it, to show it. */
	error = nvrtx_vram_read32(gpu, memory->bar1_root, &low);
	if (error == 0)
		error = nvrtx_vram_read32(gpu, memory->bar1_root + 4U, &high);
	if (error != 0)
		return error;

	memory->bar1_root_entry = ((uint64_t)high << 32) | low;
	nvrtx_stage_mark("P4m bar1: root %llx entry %llx, window %llu MiB, tables at %llx",
	    (unsigned long long)memory->bar1_root,
	    (unsigned long long)memory->bar1_root_entry,
	    (unsigned long long)(memory->bar1_window >> 20),
	    (unsigned long long)tables);

	/* Builds the tables in memory: every page entry invalid. */
	image = kern_malloc(length);
	if (image == NULL)
		return ENOMEM;

	error = nvrtx_mmu_gr_tables(image, tables, 0U, memory->bar1_window);
	if (error == 0)
		error = nvrtx_vram_write(gpu, tables, image, length);

	/* The root's first entry, as the builder made it for its own copy of the root. */
	kern_memcpy(entry, image, sizeof(entry));
	kern_free(image);
	if (error != 0)
		return error;

	/* Points the resource manager's root at the driver's second-level directory. */
	error = nvrtx_vram_write(gpu, memory->bar1_root, entry, sizeof(entry));
	if (error != 0)
		return error;

	/* Drops any stale translation of the window. */
	error = nvrtx_flush_tlb_bar(gpu, memory->bar1_root);
	if (error != 0)
		return error;

	/* Succeeded: the window's tables are in place, every page unmapped. */
	return 0;
}

/*
 * Points the window's first pages back at the console the firmware left at
 * the VRAM's start, page for page, before anything else takes the window.
 */
static int
vram_console_map(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_memory_state *memory;
	struct nvrtx_console *console;
	int error;

	memory = &gpu->memory_state;
	console = &gpu->console;

	/* A GPU without a kept console has nothing to map back. */
	if (console->kept == 0)
		return 0;

	/* The console must lie in the part of the window the tables cover. */
	if (console->size >= memory->bar1_window) {
		nvrtx_stage_mark("P4m fail: the console's %llu KiB do not fit the window", (unsigned long long)(console->size >> 10));
		return ENOSPC;
	}

	/* Maps the window's start onto the VRAM's start. */
	mutex_lock(&memory->lock);

	error = vram_write_ptes(gpu, 0U, 0U, console->size, 0);

	mutex_unlock(&memory->lock);

	/* Reports a console the window does not reach again. */
	if (error != 0) {
		nvrtx_stage_mark("P4m fail: the console's pages (%d)", error);
		return error;
	}

	/* Succeeded: the console's writes reach its VRAM again. */
	console->mapped = 1;
	nvrtx_stage_mark("P4m console: bar1 0..%llx on the VRAM's start again", (unsigned long long)console->size);
	return 0;
}

/*
 * Claims the window and maps it uncached for the CPU, after the console's
 * part; the allocator of the window starts empty after the console.
 */
static int
vram_bar1_cpu(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_memory_state *memory;
	uint64_t from;
	unsigned bar;
	int error;

	memory = &gpu->memory_state;
	bar = gpu->identity.windows[1].bar;

	/* The console's part of the window stays the console's. */
	from = 0U;
	if (gpu->console.mapped != 0)
		from = gpu->console.size;

	/* Claims the window's BAR. */
	error = drv_pci_device_claim_bar(gpu->identity.pci, bar);
	if (error != 0)
		return error;

	memory->bar1_claimed = 1;

	/* Maps the part the tables cover. */
	error = drv_pci_device_map_bar_region(gpu->identity.pci,
	    bar,
	    from,
	    (size_t)(memory->bar1_window - from),
	    DRV_PCI_MAP_READ | DRV_PCI_MAP_WRITE | DRV_PCI_MAP_NOCACHE,
	    &memory->bar1_mapping);
	if (error != 0)
		return error;

	memory->bar1_mapped = 1;
	memory->bar1_cpu_from = from;
	memory->bar1_bus = memory->bar1_mapping.physical_address;

	/* The window's runs are handed out from now on. */
	mutex_lock(&memory->lock);

	nvrtx_heap_init(&memory->bar1, from, memory->bar1_window);
	memory->bar1_ready = 1;

	mutex_unlock(&memory->lock);

	/* Succeeded: the CPU reaches the window. */
	return 0;
}

/*
 * Checks the window both ways on a run of its own: words written through
 * PRAMIN read back through the window, and a word written through the window
 * reads back through PRAMIN.
 */
static int
vram_bar1_test(
	struct nvrtx_gpu *gpu)
{
	struct nvrtx_bo bo;
	volatile uint32_t *words;
	uint32_t first;
	uint32_t last;
	uint32_t back;
	int error;

	/* Takes a run and maps it. */
	kern_memset(&bo, 0, sizeof(bo));
	error = nvrtx_bo_alloc(gpu, VRAM_TEST_BYTES, 0U, &bo);
	if (error == 0)
		error = nvrtx_bo_map(gpu, &bo);
	if (error != 0) {
		nvrtx_stage_mark("P4m fail: the test run (%d)", error);
		nvrtx_bo_free(gpu, &bo);
		return error;
	}

	/* Fills it through PRAMIN and reads its first and last words through the window. */
	error = nvrtx_vram_fill(gpu, bo.vram, VRAM_TEST_FILL, VRAM_TEST_BYTES);
	words = (volatile uint32_t *)(volatile void *)nvrtx_bo_cpu(gpu, &bo);
	first = words[0];
	last = words[VRAM_TEST_BYTES / 4U - 1U];

	/* Writes one word through the window and reads it back through PRAMIN. */
	words[VRAM_TEST_WORD_AT] = VRAM_TEST_WORD;
	(void)words[VRAM_TEST_WORD_AT];
	back = 0U;
	if (error == 0)
		error = nvrtx_vram_read32(gpu, bo.vram + VRAM_TEST_WORD_AT * 4U, &back);

	/* The run goes back either way. */
	nvrtx_bo_free(gpu, &bo);

	/* Reports a window that does not reach the VRAM. */
	if (error != 0 ||
	    first != VRAM_TEST_FILL ||
	    last != VRAM_TEST_FILL ||
	    back != VRAM_TEST_WORD) {
		nvrtx_stage_mark("P4m fail: bar1 reads %08x %08x, PRAMIN reads %08x (%d)", (unsigned)first, (unsigned)last, (unsigned)back, error);
		return EIO;
	}

	/* Succeeded: the window reaches the VRAM both ways. */
	nvrtx_stage_mark("P4m ok: bar1 %llu MiB at %llx, read back both ways",
	    (unsigned long long)(gpu->memory_state.bar1_window >> 20),
	    (unsigned long long)gpu->memory_state.bar1_bus);
	return 0;
}

/*
 * Writes the window's page entries for a run: each page of the window from
 * bar1 on points at the VRAM page of the same index from vram on, or is
 * cleared.  Then invalidates the hub's TLB.  The caller holds the memory lock.
 */
static int
vram_write_ptes(
	struct nvrtx_gpu *gpu,
	uint64_t bar1,
	uint64_t vram,
	uint64_t size,
	int clear)
{
	struct nvrtx_memory_state *memory;
	uint8_t *entries;
	uint64_t pages;
	uint64_t page;
	uint64_t entry;
	uint64_t at;
	int error;

	memory = &gpu->memory_state;
	pages = size / NVRTX_GSP_PAGE_SIZE;

	/* The page tables are one array of entries after the directories, a page table per 2 MiB in order. */
	at = memory->bar1_tables + VRAM_TABLES_PTE_AT + (bar1 / NVRTX_GSP_PAGE_SIZE) * 8U;

	/* Builds the entries in memory. */
	entries = kern_malloc((size_t)pages * 8U);
	if (entries == NULL)
		return ENOMEM;

	for (page = 0U; page < pages; page++) {
		entry = 0U;
		if (clear == 0)
			entry = nvrtx_mmu_vram_pte(vram + page * NVRTX_GSP_PAGE_SIZE);
		nvrtx_put64(entries, (size_t)page * 8U, entry);
	}

	/* Writes them through PRAMIN, then drops the hub's stale translations. */
	error = nvrtx_vram_write(gpu, at, entries, (size_t)pages * 8U);
	kern_free(entries);
	if (error != 0)
		return error;

	error = nvrtx_flush_tlb_bar(gpu, memory->bar1_root);
	if (error != 0)
		return error;

	/* Succeeded: the run is mapped (or unmapped). */
	return 0;
}
