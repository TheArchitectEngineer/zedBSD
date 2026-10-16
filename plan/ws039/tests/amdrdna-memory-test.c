/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the amdrdna driver's memory and interrupt words (stage A2):
 * the address pools, the place of the system-memory table, its entries and
 * a context's page entries, the split of an address over the four levels,
 * the invalidation request, the memory hubs' control words, a page fault's
 * decoding, and the interrupt ring's words and entries.  Where the Linux
 * driver left a register on the board (the dump of 2026-10-16: "cold" is
 * the firmware's state, "loaded" Linux's), the word the driver builds from
 * the cold value must be Linux's.
 *
 *   amdrdna-memory-test           the cases
 *   amdrdna-memory-test --hub     the memory hubs' register indices and the
 *                                 other registers of the stage, for
 *                                 Navi10's windows, one line each
 *
 * Each case prints "PASS name" or "FAIL name: why"; the exit status is 1
 * when any case failed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/gpu/amdrdna/amdrdna-hw.h"
#include "drivers/gpu/amdrdna/amdrdna-regs.h"

/* The case results. */
static int failures;

static void check(int condition, const char *name, const char *why);
static void test_pool(void);
static void test_gart(void);
static void test_entries(void);
static void test_hub_words(void);
static void test_fault(void);
static void test_interrupts(void);
static void test_hub_index(void);
static void print_hub(void);
static void navi10_block(struct amdrdna_ip *block, uint32_t window0, uint32_t window1, uint32_t window2);

int
main(
	int argc,
	char **argv)
{
	/* Prints the register table when asked. */
	if (argc == 2 && strcmp(argv[1], "--hub") == 0) {
		print_hub();
		return 0;
	}

	/* Runs the cases. */
	test_pool();
	test_gart();
	test_entries();
	test_hub_words();
	test_fault();
	test_interrupts();
	test_hub_index();

	/* Reports the whole result. */
	if (failures != 0) {
		printf("FAIL amdrdna-memory-test: %d case(s)\n", failures);
		return 1;
	}

	printf("PASS amdrdna-memory-test\n");
	return 0;
}

/* Records one case. */
static void
check(
	int condition,
	const char *name,
	const char *why)
{
	/* Prints the case's result and counts a failure. */
	if (condition) {
		printf("PASS %s\n", name);
	} else {
		printf("FAIL %s: %s\n", name, why);
		failures++;
	}
}

/* The address pools: give, hold, take with alignment, merge, and the limits. */
static void
test_pool(void)
{
	struct amdrdna_pool *pool;
	uint64_t at;
	uint64_t first;
	uint64_t second;
	unsigned index;
	int error;

	pool = calloc(1U, sizeof(*pool));
	if (pool == NULL) {
		check(0, "pool.alloc", "no memory");
		return;
	}

	/* The CPU pool of Navi10's plan: 8 MiB past the picture to 256 MiB, without the low 32 MiB and the table. */
	amdrdna_pool_init(pool, 0x800000U, 0x10000000U);
	error = amdrdna_pool_give(pool, 0x800000U, 0x10000000U - 0x800000U);
	check(error == 0 && pool->span_count == 1U && pool->free_bytes == 0xf800000U, "pool.give", "the whole run is not free");
	amdrdna_pool_hold(pool, 0U, 0x2000000U);
	check(pool->span_count == 1U && pool->spans[0].start == 0x2000000U && pool->free_bytes == 0xe000000U,
	    "pool.hold.low", "the training range is not cut off the bottom");
	amdrdna_pool_hold(pool, 0x5000000U, 0x1000U);
	check(pool->span_count == 2U && pool->spans[0].end == 0x5000000U && pool->spans[1].start == 0x5001000U,
	    "pool.hold.inside", "a range inside does not split the run");
	amdrdna_pool_hold(pool, 0xff00000U, 0x1000000U);
	check(pool->spans[1].end == 0xff00000U, "pool.hold.top", "a range past the end is not clipped");

	/* Takes a page, then an aligned 1 MiB run. */
	error = amdrdna_pool_take(pool, 0x1000U, 0x1000U, &first);
	check(error == 0 && first == 0x2000000U, "pool.take.first", "the first page is not the lowest");
	error = amdrdna_pool_take(pool, 0x100000U, 0x100000U, &second);
	check(error == 0 && second == 0x2100000U && pool->span_count == 3U,
	    "pool.take.aligned", "the aligned run is not at the next 1 MiB, or the gap is lost");

	/* Gives both back: the runs merge again. */
	error = amdrdna_pool_give(pool, second, 0x100000U);
	check(error == 0 && pool->span_count == 2U, "pool.give.merge", "the run did not merge with both neighbours");
	error = amdrdna_pool_give(pool, first, 0x1000U);
	check(error == 0 && pool->span_count == 2U && pool->spans[0].start == 0x2000000U && pool->free_bytes == 0xdeff000U,
	    "pool.give.merge.low", "the page did not merge back");

	/* Refusals: an overlap, a run outside, an empty run, a bad alignment, a run too large. */
	error = amdrdna_pool_give(pool, 0x3000000U, 0x1000U);
	check(error == EINVAL, "pool.give.overlap", "a free run was given twice");
	error = amdrdna_pool_give(pool, 0x10000000U, 0x1000U);
	check(error == EINVAL, "pool.give.outside", "a run past the pool was taken in");
	error = amdrdna_pool_take(pool, 0U, 0x1000U, &at);
	check(error == EINVAL, "pool.take.empty", "an empty run was handed out");
	error = amdrdna_pool_take(pool, 0x1000U, 0x3000U, &at);
	check(error == EINVAL, "pool.take.alignment", "an alignment that is not a power of two was taken");
	error = amdrdna_pool_take(pool, 0x10000000U, 0x1000U, &at);
	check(error == ENOMEM, "pool.take.large", "a run larger than the pool was handed out");

	/* The span limit: every other page taken until the list is full. */
	amdrdna_pool_init(pool, 0U, 0x1000000U);
	(void)amdrdna_pool_give(pool, 0U, 0x1000000U);
	for (index = 0U; index < AMDRDNA_POOL_SPANS; index++)
		amdrdna_pool_hold(pool, (uint64_t)index * 0x2000U + 0x1000U, 0x1000U);
	check(pool->span_count == AMDRDNA_POOL_SPANS, "pool.limit.count", "the list did not fill");
	error = amdrdna_pool_give(pool, 0x1000U, 0x1000U);
	check(error == 0 && pool->span_count == AMDRDNA_POOL_SPANS - 1U, "pool.limit.merge", "a merge needs no new run");
	free(pool);
}

/* The place of the system-memory table and its entries. */
static void
test_gart(void)
{
	uint64_t start;
	uint64_t entry;
	int error;

	/* Navi10 of the board: the frame buffer at 512 GiB, 6128 MiB; Linux put the table's reach at 0. */
	error = amdrdna_gart_place(0x8000000000ULL, 0x817effffffULL, 0x20000000U, &start);
	check(error == 0 && start == 0U, "gart.place.board", "the reach is not at 0 as Linux placed it");

	/* A frame buffer at 0 leaves only the top: the reach goes below the 2^47 hole, 4 GiB aligned. */
	error = amdrdna_gart_place(0U, 0x17effffffULL, 0x20000000U, &start);
	check(error == 0 && start == 0x7fff00000000ULL, "gart.place.top", "the reach is not at the top below the hole");

	/* A reach no room holds is refused. */
	error = amdrdna_gart_place(0x1000U, 0x7ffffffff000ULL, 0x20000000U, &start);
	check(error == ENOSPC, "gart.place.full", "a reach without room was placed");

	/* A present system entry: present, system, coherent, executable, readable, writable, uncached. */
	entry = amdrdna_gart_system_entry(0x123456789abcULL);
	check(entry == 0x0003123456789077ULL, "gart.entry.system", "the system entry's bits differ");

	/* An unused entry: only the dummy page's address. */
	entry = amdrdna_gart_unused_entry(0x1234567fffULL);
	check(entry == 0x1234567000ULL, "gart.entry.unused", "the unused entry carries bits");
}

/* A context's page entries, the split of an address, and the invalidation request. */
static void
test_entries(void)
{
	unsigned index[AMDRDNA_VM_LEVELS];
	uint64_t entry;
	uint32_t word;

	/* A leaf of VRAM: the address and the access bits the caller chose. */
	entry = amdrdna_vm_leaf_entry(0x17ef00123ULL, AMDRDNA_PAGE_PRESENT | AMDRDNA_PAGE_READ | AMDRDNA_PAGE_WRITE);
	check(entry == 0x17ef00061ULL, "vm.leaf", "the leaf's address or bits differ");

	/* A directory entry in VRAM and one in system memory. */
	entry = amdrdna_vm_table_entry(0x100000ULL, false);
	check(entry == 0x100001ULL, "vm.table.vram", "a VRAM directory entry differs");
	entry = amdrdna_vm_table_entry(0x2345678c0ULL, true);
	check(entry == 0x2345678c7ULL, "vm.table.system", "a system directory entry differs");

	/* The split: 0x0000_7f80_4020_1000 is 0xff at the root, 0x1 at both middle levels, 0x1 at the leaf. */
	amdrdna_vm_split(0x00007f8040201000ULL, index);
	check(index[0] == 0xffU && index[1] == 0x1U && index[2] == 0x1U && index[3] == 0x1U,
	    "vm.split", "the four indices differ");
	amdrdna_vm_split(0x0000ffffffffffffULL, index);
	check(index[0] == 0x1ffU && index[1] == 0x1ffU && index[2] == 0x1ffU && index[3] == 0x1ffU,
	    "vm.split.top", "the last page's indices differ");

	/* The invalidation request of context 0 and of context 15. */
	word = amdrdna_invalidate_word(0U);
	check(word == 0x00f80001U, "vm.invalidate.0", "the request for context 0 differs");
	word = amdrdna_invalidate_word(15U);
	check(word == 0x00f88000U, "vm.invalidate.15", "the request for context 15 differs");
}

/* The memory hubs' control words, against what Linux left on the board. */
static void
test_hub_words(void)
{
	uint32_t word;

	/* The media hub's cache control: firmware 0x00080602, Linux 0x00080e01. */
	word = amdrdna_hub_cache_word(0x00080602U);
	check(word == 0x00080e01U, "hub.cache.board", "the cache word differs from Linux's");

	/* The media hub's context 0: firmware 0x007ffe80, Linux 0x007ffe01. */
	word = amdrdna_hub_system_context_word(0x007ffe80U);
	check(word == 0x007ffe01U, "hub.context0.board", "context 0's word differs from Linux's");

	/* The cache control words 3 to 5 and the flush word. */
	check(amdrdna_hub_cache_word_3() == 0x80130009U, "hub.cache3", "the third cache word differs");
	check(amdrdna_hub_cache_word_4() == 0x00000001U, "hub.cache4", "the fourth cache word differs");
	check(amdrdna_hub_cache_word_5() == 0x00003fe0U, "hub.cache5", "the fifth cache word differs");
	check(amdrdna_hub_cache_flush_word(0x00000000U) == 0x00000003U, "hub.cache.flush", "the flush word differs");

	/* The translation control from zero: on, mode 3, advanced, uncached. */
	word = amdrdna_hub_translation_word(0U);
	check(word == 0x00001859U, "hub.translation", "the translation word differs");
	word = amdrdna_hub_translation_word(0x000007a0U);
	check(word == 0x00001fd9U, "hub.translation.keep", "the translation word does not keep the other bits");

	/* A user context from the firmware's value: four levels, defaults, no retry, on. */
	word = amdrdna_hub_user_context_word(0x007ffe80U);
	check(word == 0x007ffe07U, "hub.context.user", "a user context's word differs");
	word = amdrdna_hub_user_context_word(0U);
	check(word == 0x00555407U, "hub.context.user.zero", "a user context's word from zero differs");

	/* The fault words: defaults, retry, re-arm, and the context interrupts on and off. */
	check(amdrdna_hub_fault_default_word(0U) == 0x00001ffcU, "hub.fault.default", "the default-page bits differ");
	check(amdrdna_hub_fault_retry_word(0U) == 0x00040000U, "hub.fault.retry", "the retry bit differs");
	check(amdrdna_hub_fault_rearm_word(0x00001ffcU) == 0x00001ffdU, "hub.fault.rearm", "the re-arm word differs");
	check(amdrdna_hub_fault_interrupt_word(0x00555407U, true) == 0x007ffe07U, "hub.fault.interrupts.on", "the interrupt bits differ");
	check(amdrdna_hub_fault_interrupt_word(0x007ffe07U, false) == 0x00555407U, "hub.fault.interrupts.off", "the interrupt bits stay");

	/* The aperture of the board's frame buffer: Linux wrote 0x200000 and 0x205fbf. */
	check((0x8000000000ULL >> AMDRDNA_HUB_APERTURE_SHIFT) == 0x200000U &&
	    ((0x8000000000ULL + 0x17f0ULL * 0x100000U - 1U) >> AMDRDNA_HUB_APERTURE_SHIFT) == 0x205fbfU,
	    "hub.aperture.board", "the aperture words differ from Linux's");
}

/* A page fault's decoding. */
static void
test_fault(void)
{
	struct amdrdna_interrupt interrupt;
	struct amdrdna_vm_fault fault;

	/* A write fault of the graphics hub at 0x1045_6789_a000, context 3. */
	memset(&interrupt, 0, sizeof(interrupt));
	interrupt.client = AMDRDNA_CLIENT_GRAPHICS_HUB;
	interrupt.space_id = 7U;
	interrupt.data[0] = 0x456789aU;
	interrupt.data[1] = 0x21U;
	amdrdna_vm_fault_decode(&interrupt, 0x00341311U, &fault);
	check(fault.hub == AMDRDNA_HUB_GRAPHICS && fault.fault_address == 0x10456789a000ULL, "fault.address", "the hub or address differs");
	check(fault.writing && !fault.reading && !fault.executing && !fault.retried && fault.access_bits == 0x20U && fault.space_id == 7U,
	    "fault.access", "the access kind differs");
	check(fault.more_behind && fault.unmapped && fault.unit == 0x9U && fault.unit_wrote && fault.context == 3U &&
	    fault.denied == 1U && fault.walker_error == 0U,
	    "fault.latched", "the latched fields differ");

	/* The media hub's own client. */
	interrupt.client = AMDRDNA_CLIENT_MEDIA_HUB;
	amdrdna_vm_fault_decode(&interrupt, 0U, &fault);
	check(fault.hub == AMDRDNA_HUB_MEDIA, "fault.media", "the media hub's fault is not its own");
}

/* The interrupt ring's words and an entry. */
static void
test_interrupts(void)
{
	struct amdrdna_interrupt interrupt;
	uint32_t words[AMDRDNA_INTERRUPT_ENTRY_BYTES / 4U];
	uint32_t word;
	uint32_t offset;
	bool overflowed;
	int error;

	/* The ring's control word from the firmware's 0x40610000; switched on it is Linux's 0x403301a1 less the clear bit. */
	error = amdrdna_interrupt_ring_word(0x40610000U, AMDRDNA_INTERRUPT_RING_BYTES, true, &word);
	check(error == 0 && word == 0xc03101a0U, "ih.ring.word", "the prepared control word differs");
	word = amdrdna_interrupt_switch_word(word, true) & ~0x80000000U;
	check(word == 0x403301a1U, "ih.ring.board", "the control word switched on differs from Linux's");
	word = amdrdna_interrupt_switch_word(0x403301a1U, false);
	check(word == 0x403101a0U, "ih.ring.off", "the control word switched off differs");

	/* A ring of another size, without a message, and the refused sizes. */
	error = amdrdna_interrupt_ring_word(0U, 0x1000U, false, &word);
	check(error == 0 && word == 0xc0110194U, "ih.ring.small", "a 4 KiB ring's word differs");
	error = amdrdna_interrupt_ring_word(0U, 0x3000U, true, &word);
	check(error == EINVAL, "ih.ring.size", "a size that is not a power of two was taken");
	error = amdrdna_interrupt_ring_word(0U, 16U, true, &word);
	check(error == EINVAL, "ih.ring.tiny", "a ring smaller than an entry was taken");

	/* The overflow acknowledgement. */
	check(amdrdna_interrupt_overflow_word(0x403301a1U, true) == 0xc03301a1U &&
	    amdrdna_interrupt_overflow_word(0xc03301a1U, false) == 0x403301a1U,
	    "ih.overflow", "the overflow bit differs");

	/* The doorbell words: dword 0x2f0, enabled; the range of two dwords. */
	check(amdrdna_interrupt_doorbell_word(AMDRDNA_DOORBELL_INTERRUPTS) == 0x100002f0U, "ih.doorbell", "the doorbell word differs");
	check(amdrdna_interrupt_doorbell_range_word(0U, AMDRDNA_DOORBELL_INTERRUPTS) == 0x00020bc0U, "ih.doorbell.range", "the range word differs");
	check(amdrdna_interrupt_control_word(0xffffffffU) == 0xfffffff6U, "ih.control", "the host bridge's control word differs");

	/* The write pointer: an offset, and an overflow. */
	offset = amdrdna_interrupt_offset(0x00000100U, AMDRDNA_INTERRUPT_RING_BYTES, &overflowed);
	check(offset == 0x100U && !overflowed, "ih.pointer", "the offset differs");
	offset = amdrdna_interrupt_offset(0x0003ffe1U, AMDRDNA_INTERRUPT_RING_BYTES, &overflowed);
	check(offset == 0x3ffe0U && overflowed, "ih.pointer.overflow", "the overflow or offset differs");

	/* An entry: client 0x1b source 0 ring 2 context 3 from the media hub, a timestamp, an ID, four data words. */
	words[0] = 0x8302001bU;
	words[1] = 0x89abcdefU;
	words[2] = 0x80001234U;
	words[3] = 0x00050007U;
	words[4] = 0x11111111U;
	words[5] = 0x22222222U;
	words[6] = 0x33333333U;
	words[7] = 0x44444444U;
	amdrdna_interrupt_decode(words, &interrupt);
	check(interrupt.client == 0x1bU && interrupt.source == 0U && interrupt.ring == 2U && interrupt.context == 3U &&
	    interrupt.from_media_hub,
	    "ih.entry.first", "the first word's fields differ");
	check(interrupt.timestamp == 0x123489abcdefULL && interrupt.space_id == 7U &&
	    interrupt.data[0] == 0x11111111U && interrupt.data[3] == 0x44444444U,
	    "ih.entry.rest", "the timestamp, the ID or the data differ");
}

/* The hub register indices: both hubs, every instance, the limits. */
static void
test_hub_index(void)
{
	struct amdrdna_ip graphics;
	struct amdrdna_ip media;

	navi10_block(&graphics, 0x1260U, 0xa000U, 0U);
	navi10_block(&media, 0x1a000U, 0U, 0U);
	check(amdrdna_hub_index(&graphics, AMDRDNA_HUB_GRAPHICS, AMDRDNA_HUB_CONTEXT_CONTROL, 0U) == 0x2880U,
	    "hub.index.context0", "the graphics hub's context 0 is not at 0x2880");
	check(amdrdna_hub_index(&media, AMDRDNA_HUB_MEDIA, AMDRDNA_HUB_FRAME_BUFFER_BOTTOM, 0U) == 0x1a86cU,
	    "hub.index.media", "the media hub's frame buffer word is not at 0x1a86c (the dump's)");
	check(amdrdna_hub_index(&graphics, AMDRDNA_HUB_GRAPHICS, AMDRDNA_HUB_INVALIDATE_RANGE_LOW, 17U) == 0x1260U + 0x1689U,
	    "hub.index.engine17", "engine 17's range word differs");
	check(amdrdna_hub_index(&graphics, AMDRDNA_HUB_GRAPHICS, AMDRDNA_HUB_CONTEXT_CONTROL, 16U) == 0U &&
	    amdrdna_hub_index(&graphics, AMDRDNA_HUB_GRAPHICS, AMDRDNA_HUB_INVALIDATE_DONE, 18U) == 0U &&
	    amdrdna_hub_index(&graphics, AMDRDNA_HUB_GRAPHICS, AMDRDNA_HUB_APERTURE_LOW, 1U) == 0U,
	    "hub.index.limits", "an instance past the last has an address");
	graphics.found = 0U;
	check(amdrdna_hub_index(&graphics, AMDRDNA_HUB_GRAPHICS, AMDRDNA_HUB_APERTURE_LOW, 0U) == 0U,
	    "hub.index.unlisted", "an unlisted block has an address");
}

/*
 * Prints every hub register index for Navi10's windows ("hub HUB WHICH
 * INSTANCE 0xINDEX") and the stage's other registers ("other N 0xINDEX",
 * in the order the compare script names them).
 */
static void
print_hub(void)
{
	struct amdrdna_ip blocks[AMDRDNA_HUB_COUNT];
	unsigned hub;
	unsigned which;
	unsigned instance;
	unsigned count;

	navi10_block(&blocks[AMDRDNA_HUB_GRAPHICS], 0x1260U, 0xa000U, 0U);
	navi10_block(&blocks[AMDRDNA_HUB_MEDIA], 0x1a000U, 0U, 0U);
	for (hub = 0U; hub < AMDRDNA_HUB_COUNT; hub++) {
		for (which = 0U; which < AMDRDNA_HUB_REGISTER_COUNT; which++) {
			count = amdrdna_hub_instances((enum amdrdna_hub_register)which);
			for (instance = 0U; instance < count; instance++) {
				printf("hub %u %u %u 0x%05x\n", hub, which, instance,
				    (unsigned)amdrdna_hub_index(&blocks[hub], (enum amdrdna_hub)hub, (enum amdrdna_hub_register)which, instance));
			}
		}
	}

	/* NBIO window 2 (0xd20), HDP (0xf20), the interrupt controller (0x10a0). */
	printf("other 0 0x%05x\n", 0xd20U + AMDRDNA_NBIO_REG_DOORBELL_APERTURE);
	printf("other 1 0x%05x\n", 0xd20U + AMDRDNA_NBIO_REG_INTERRUPT_CONTROL);
	printf("other 2 0x%05x\n", 0xd20U + AMDRDNA_NBIO_REG_INTERRUPT_DUMMY);
	printf("other 3 0x%05x\n", 0xd20U + AMDRDNA_NBIO_REG_HOST_PATH_FLUSH);
	printf("other 4 0x%05x\n", 0xd20U + AMDRDNA_NBIO_REG_INTERRUPT_DOORBELLS);
	printf("other 5 0x%05x\n", 0xf20U + AMDRDNA_HDP_REG_READ_DROP);
	printf("other 6 0x%05x\n", 0xf20U + AMDRDNA_HDP_REG_MISC);
	printf("other 7 0x%05x\n", 0x10a0U + AMDRDNA_IH_REG_SPACE_TO_PROCESS);
	printf("other 8 0x%05x\n", 0x10a0U + AMDRDNA_IH_REG_RING_CONTROL);
	printf("other 9 0x%05x\n", 0x10a0U + AMDRDNA_IH_REG_RING_BASE);
	printf("other 10 0x%05x\n", 0x10a0U + AMDRDNA_IH_REG_RING_BASE_HIGH);
	printf("other 11 0x%05x\n", 0x10a0U + AMDRDNA_IH_REG_READ_POINTER);
	printf("other 12 0x%05x\n", 0x10a0U + AMDRDNA_IH_REG_WRITE_POINTER);
	printf("other 13 0x%05x\n", 0x10a0U + AMDRDNA_IH_REG_POINTER_COPY_HIGH);
	printf("other 14 0x%05x\n", 0x10a0U + AMDRDNA_IH_REG_POINTER_COPY_LOW);
	printf("other 15 0x%05x\n", 0x10a0U + AMDRDNA_IH_REG_DOORBELL);
	printf("other 16 0x%05x\n", (unsigned)AMDRDNA_NBIO_REG_VRAM_SIZE_MIB);
}

/* Fills a block with up to three windows. */
static void
navi10_block(
	struct amdrdna_ip *block,
	uint32_t window0,
	uint32_t window1,
	uint32_t window2)
{
	memset(block, 0, sizeof(*block));
	block->found = 1U;
	block->window_count = 3U;
	block->window[0] = window0;
	block->window[1] = window1;
	block->window[2] = window2;
}
