/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws074-p003: the host test of the VM heap, strings and atoms.
 *
 *   build/ws074-host/<variant>/host-heap
 *
 * Checks that the collector keeps what the stack, the roots, the tracers
 * and the traced cells reach, frees the rest, finalizes what it frees,
 * honours a limit, and survives a long random workload with its contents
 * intact; and that strings narrow, compare, hash and convert, and atoms
 * intern.  Prints one line per failed check and a summary.
 */

#include "vm/vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A test cell: two references and a checksum over a payload of bytes. */
struct test_node {
	struct vm_cell cell;
	struct test_node *left;
	struct test_node *right;
	uint32_t size;
	uint32_t seed;
};

static int failures;
static int checks;
static size_t finalized;

static void check(int condition, const char *what);
static void node_trace(struct vm_heap *heap, struct vm_cell *cell);
static void node_finalize(struct vm_heap *heap, struct vm_cell *cell);
static struct test_node *node_new(struct vm_heap *heap, uint32_t size, uint32_t seed);
static int node_intact(const struct test_node *node);
static void clobber_stack(void) __attribute__((noinline));
static void make_garbage(struct vm_heap *heap, int count) __attribute__((noinline));
static struct test_node *make_list(struct vm_heap *heap, int count) __attribute__((noinline));
static void array_tracer(struct vm_heap *heap, void *context);
static void test_stack_and_garbage(struct vm_heap *heap);
static void test_roots_and_lists(struct vm_heap *heap);
static void test_interior_and_large(struct vm_heap *heap);
static void large_garbage(struct vm_heap *heap, int count) __attribute__((noinline));
static void test_tracer(struct vm_heap *heap);
static void test_random(struct vm_heap *heap);
static void test_limit(void);
static void test_strings(struct vm_heap *heap);
static void test_atoms(struct vm_heap *heap);

static const struct vm_cell_type node_type = { "test-node", node_trace, node_finalize };

/* The cells the tracer test keeps in malloc'd memory. */
struct test_array {
	struct test_node **items;
	size_t count;
};

int
main(void)
{
	struct vm_heap *heap;
	int error;

	error = vm_heap_create(&heap, 0);
	check(error == 0, "heap: create");
	if (error != 0)
		return 1;
	vm_heap_set_stack_base(heap, __builtin_frame_address(0));

	test_stack_and_garbage(heap);
	test_roots_and_lists(heap);
	test_interior_and_large(heap);
	test_tracer(heap);
	test_strings(heap);
	test_atoms(heap);
	test_random(heap);
	vm_heap_destroy(heap);
	test_limit();

	printf("host-heap: %d checks, %d failed\n", checks, failures);
	if (failures != 0)
		return 1;

	return 0;
}

static void
check(
	int condition,
	const char *what)
{
	checks++;
	if (!condition) {
		failures++;
		printf("FAIL: %s\n", what);
	}
}

static void
node_trace(
	struct vm_heap *heap,
	struct vm_cell *cell)
{
	struct test_node *node;

	node = (struct test_node *)cell;
	if (node->left != NULL)
		vm_heap_mark(heap, &node->left->cell);
	if (node->right != NULL)
		vm_heap_mark(heap, &node->right->cell);
}

static void
node_finalize(
	struct vm_heap *heap,
	struct vm_cell *cell)
{
	UNUSED_PARAMETER(heap);
	UNUSED_PARAMETER(cell);
	finalized++;
}

/* Makes a node whose payload after the header is filled from seed. */
static struct test_node *
node_new(
	struct vm_heap *heap,
	uint32_t size,
	uint32_t seed)
{
	struct test_node *node;
	unsigned char *payload;
	uint32_t index;

	if (size < sizeof(*node))
		size = sizeof(*node);
	node = vm_heap_alloc(heap, &node_type, size);
	if (node == NULL)
		return NULL;
	node->size = size;
	node->seed = seed;
	payload = (unsigned char *)(node + 1);
	for (index = 0; index < size - sizeof(*node); index++)
		payload[index] = (unsigned char)(seed + index * 31U);
	return node;
}

static int
node_intact(
	const struct test_node *node)
{
	const unsigned char *payload;
	uint32_t index;

	if (node->cell.type != &node_type)
		return 0;
	payload = (const unsigned char *)(node + 1);
	for (index = 0; index < node->size - sizeof(*node); index++) {
		if (payload[index] != (unsigned char)(node->seed + index * 31U))
			return 0;
	}
	return 1;
}

/* Overwrites the dead part of the stack so stale words do not keep garbage alive. */
static void
clobber_stack(void)
{
	volatile unsigned char area[64 * 1024];
	size_t index;

	for (index = 0; index < sizeof(area); index++)
		area[index] = 0;
}

static void
make_garbage(
	struct vm_heap *heap,
	int count)
{
	int index;

	for (index = 0; index < count; index++)
		node_new(heap, 48U + (uint32_t)(index % 200), (uint32_t)index);
}

static struct test_node *
make_list(
	struct vm_heap *heap,
	int count)
{
	struct test_node *head;
	struct test_node *node;
	int index;

	head = NULL;
	for (index = 0; index < count; index++) {
		node = node_new(heap, 64, (uint32_t)index);
		if (node == NULL)
			return NULL;
		node->left = head;
		head = node;
	}
	return head;
}

/* Garbage is freed and finalized; a node held in a local survives with its contents. */
static void
test_stack_and_garbage(
	struct vm_heap *heap)
{
	struct vm_heap_stats stats;
	struct test_node *volatile kept;
	size_t before;

	kept = node_new(heap, 200, 7);
	make_garbage(heap, 50000);
	clobber_stack();
	before = finalized;
	vm_heap_collect(heap);
	vm_heap_stats(heap, &stats);
	check(finalized - before >= 49000, "gc: garbage is finalized");
	check(stats.live_cells < 1000, "gc: garbage is freed");
	check(vm_heap_find_cell(heap, (uintptr_t)kept) == &kept->cell, "gc: a node on the stack stays");
	check(node_intact(kept), "gc: the kept node's contents are intact");
}

/* A long list kept by a root slot survives several collections, and dies with the root. */
static void
test_roots_and_lists(
	struct vm_heap *heap)
{
	struct test_node *root;
	struct test_node *node;
	size_t before;
	int count;
	int intact;
	int error;

	root = make_list(heap, 20000);
	error = vm_heap_add_root(heap, (struct vm_cell **)&root);
	check(error == 0, "roots: add");
	make_garbage(heap, 10000);
	vm_heap_collect(heap);
	make_garbage(heap, 10000);
	vm_heap_collect(heap);
	count = 0;
	intact = 1;
	for (node = root; node != NULL; node = node->left) {
		if (!node_intact(node))
			intact = 0;
		count++;
	}
	check(count == 20000 && intact, "roots: a rooted list of 20000 survives intact");

	vm_heap_remove_root(heap, (struct vm_cell **)&root);
	root = NULL;
	node = NULL;
	clobber_stack();
	before = finalized;
	vm_heap_collect(heap);
	check(finalized - before >= 19000, "roots: the list dies with its root");
}

/* The key the interior test hides addresses with, so the stack holds no copy of them. */
#define ADDRESS_KEY	((uintptr_t)0x5a5a5a5a5a5a5a5aULL)

/* Allocates a small and a large node, keeps them by inner pointers across a collection, and hands back hidden addresses. */
static void __attribute__((noinline))
interior_keep(
	struct vm_heap *heap,
	uintptr_t *small_key,
	uintptr_t *large_key)
{
	struct test_node *node;
	struct test_node *large;
	unsigned char *volatile inside_small;
	unsigned char *volatile inside_large;

	node = node_new(heap, 1000, 3);
	large = node_new(heap, 300000, 9);
	check(large != NULL, "large: allocated");
	inside_small = (unsigned char *)node + 500;
	inside_large = (unsigned char *)large + 200000;
	*small_key = (uintptr_t)node ^ ADDRESS_KEY;
	*large_key = (uintptr_t)large ^ ADDRESS_KEY;
	node = NULL;
	large = NULL;
	make_garbage(heap, 1000);
	vm_heap_collect(heap);
	node = (struct test_node *)vm_heap_find_cell(heap, (uintptr_t)inside_small);
	large = (struct test_node *)vm_heap_find_cell(heap, (uintptr_t)inside_large);
	check(node != NULL && node_intact(node) && node->seed == 3, "interior: small cell kept by an inner pointer");
	check(large != NULL && node_intact(large) && large->seed == 9, "interior: large cell kept by an inner pointer");
}

/* Makes and drops count large nodes. */
static void __attribute__((noinline))
large_garbage(
	struct vm_heap *heap,
	int count)
{
	int index;

	for (index = 0; index < count; index++)
		node_new(heap, 300000, (uint32_t)index);
}

/* A pointer into the middle of a cell keeps it, for small and large cells; without one they go. */
static void
test_interior_and_large(
	struct vm_heap *heap)
{
	struct vm_heap_stats stats;
	uintptr_t small_key;
	uintptr_t large_key;

	interior_keep(heap, &small_key, &large_key);
	clobber_stack();
	vm_heap_collect(heap);
	/*
	 * A conservative collector cannot promise to free one given cell (a stale
	 * stack word may keep it), so what is checked is that dropped large cells do
	 * not pile up: of 200 made and dropped, nearly all are freed.
	 */
	large_garbage(heap, 200);
	clobber_stack();
	vm_heap_collect(heap);
	vm_heap_stats(heap, &stats);
	check(stats.live_bytes < 20U * 300000U, "large: dropped large cells are freed");
	check(vm_heap_find_cell(heap, 12345) == NULL, "find: a small number is no cell");
}

static void
array_tracer(
	struct vm_heap *heap,
	void *context)
{
	struct test_array *array;
	size_t index;

	array = context;
	for (index = 0; index < array->count; index++)
		vm_heap_mark(heap, &array->items[index]->cell);
}

/* Cells held only in malloc'd memory survive through a tracer, and die without it. */
static void
test_tracer(
	struct vm_heap *heap)
{
	struct test_array array;
	size_t index;
	size_t before;
	int intact;

	array.count = 3000;
	array.items = malloc(array.count * sizeof(*array.items));
	for (index = 0; index < array.count; index++)
		array.items[index] = node_new(heap, 96, (uint32_t)index);
	vm_heap_add_tracer(heap, array_tracer, &array);
	make_garbage(heap, 20000);
	clobber_stack();
	vm_heap_collect(heap);
	intact = 1;
	for (index = 0; index < array.count; index++) {
		if (vm_heap_find_cell(heap, (uintptr_t)array.items[index]) == NULL || !node_intact(array.items[index]))
			intact = 0;
	}
	check(intact, "tracer: traced cells survive");

	vm_heap_remove_tracer(heap, array_tracer, &array);
	for (index = 0; index < array.count; index++)
		array.items[index] = NULL;
	free(array.items);
	clobber_stack();
	before = finalized;
	vm_heap_collect(heap);
	check(finalized - before >= 2900, "tracer: cells die without the tracer");
}

/* A random graph workload over many collections keeps everything reachable intact. */
static void
test_random(
	struct vm_heap *heap)
{
	struct vm_heap_stats stats;
	struct test_node *slots[512];
	struct test_node *node;
	unsigned long state;
	uint32_t size;
	int round;
	int index;
	int intact;

	memset(slots, 0, sizeof(slots));
	vm_heap_add_root(heap, (struct vm_cell **)&slots[0]);
	state = 12345;
	intact = 1;
	for (round = 0; round < 400000; round++) {
		state = state * 6364136223846793005UL + 1442695040888963407UL;
		index = (int)((state >> 33) % 512U);
		size = (uint32_t)((state >> 20) % 700U) + 32U;
		if ((state >> 60) == 0)
			size = 20000U + (uint32_t)((state >> 12) % 60000U);
		node = node_new(heap, size, (uint32_t)round);
		if (node == NULL) {
			intact = 0;
			break;
		}
		/* A node refers only to nodes that refer to nothing, so the reachable graph stays bounded. */
		if (slots[(index + 1) % 512] != NULL && slots[(index + 1) % 512]->left == NULL)
			node->left = slots[(index + 1) % 512];
		if ((round % 3) == 0)
			node->right = node_new(heap, 40, (uint32_t)round + 1U);
		slots[index] = node;
		if (round % 50000 == 0) {
			for (index = 0; index < 512; index++) {
				if (slots[index] != NULL && !node_intact(slots[index]))
					intact = 0;
			}
		}
	}
	for (index = 0; index < 512; index++) {
		if (slots[index] != NULL && !node_intact(slots[index]))
			intact = 0;
		if (slots[index] != NULL && slots[index]->left != NULL && !node_intact(slots[index]->left))
			intact = 0;
	}
	vm_heap_stats(heap, &stats);
	check(intact, "random: everything reachable stays intact");
	check(stats.collections > 5, "random: the workload collected several times");
	printf("host-heap: random workload: %zu collections, %zu live cells, %zu live bytes, %zu heap bytes\n",
	    stats.collections, stats.live_cells, stats.live_bytes, stats.heap_bytes);
	vm_heap_remove_root(heap, (struct vm_cell **)&slots[0]);
}

/* A heap with a limit refuses cells past it and recovers after they die. */
static void
test_limit(void)
{
	struct vm_heap *heap;
	struct test_node *root;
	struct test_node *node;
	int count;

	vm_heap_create(&heap, 2U * 1024U * 1024U);
	vm_heap_set_stack_base(heap, __builtin_frame_address(0));
	root = NULL;
	vm_heap_add_root(heap, (struct vm_cell **)&root);
	count = 0;
	for (;;) {
		node = node_new(heap, 1000, (uint32_t)count);
		if (node == NULL)
			break;
		node->left = root;
		root = node;
		count++;
		if (count > 100000)
			break;
	}
	check(count > 1000 && count < 3000, "limit: allocation stops near the limit");
	root = NULL;
	node = NULL;
	clobber_stack();
	node = node_new(heap, 1000, 1);
	check(node != NULL, "limit: allocation works again once the cells died");
	vm_heap_destroy(heap);
}

static void
test_strings(
	struct vm_heap *heap)
{
	static const uint16_t wide_units[] = { 'a', 0x3042, 'b' };
	static const uint16_t narrow_units[] = { 'd', 'i', 'v', 0xe9 };
	struct vm_string *narrow;
	struct vm_string *from_units;
	struct vm_string *wide;
	struct vm_string *joined;
	struct wb_buffer buffer;

	narrow = vm_string_from_utf8(heap, "div\xc3\xa9", 5);
	from_units = vm_string_from_units(heap, narrow_units, 4);
	wide = vm_string_from_units(heap, wide_units, 3);
	check(narrow != NULL && narrow->length == 4 && (narrow->flags & VM_STRING_WIDE) == 0, "string: UTF-8 narrows to Latin-1");
	check(vm_string_equal(narrow, from_units), "string: equal across makers");
	check(vm_string_hash(narrow) == vm_string_hash(from_units), "string: equal strings hash alike");
	check((wide->flags & VM_STRING_WIDE) != 0 && vm_string_at(wide, 1) == 0x3042, "string: wide unit");
	joined = vm_string_concat(heap, narrow, wide);
	check(joined != NULL && joined->length == 7 && vm_string_at(joined, 3) == 0xe9 && vm_string_at(joined, 5) == 0x3042,
	    "string: concat narrow and wide");
	check(vm_string_compare(narrow, wide) > 0, "string: compare by units");
	check(vm_string_equal_ascii(vm_string_from_utf8(heap, "body", 4), "body"), "string: equal to ASCII");
	wb_buffer_init(&buffer);
	vm_string_to_utf8(joined, &buffer);
	check(strcmp(wb_buffer_string(&buffer), "div\xc3\xa9" "a\xe3\x81\x82" "b") == 0, "string: to UTF-8");
	wb_buffer_release(&buffer);
}

static void
test_atoms(
	struct vm_heap *heap)
{
	static const uint16_t units[] = { 's', 'p', 'a', 'n' };
	struct vm_string *first;
	struct vm_string *second;
	struct vm_string *found;
	char name[32];
	int index;
	int same;

	first = vm_atom_from_ascii(heap, "span");
	second = vm_atom_from_units(heap, units, 4);
	check(first != NULL && first == second, "atom: same characters give the same atom");
	found = vm_atom_find_units(heap, units, 3);
	check(found == NULL, "atom: a prefix is not found");
	same = 1;
	for (index = 0; index < 5000; index++) {
		snprintf(name, sizeof(name), "name-%d", index);
		if (vm_atom_from_ascii(heap, name) == NULL)
			same = 0;
	}
	make_garbage(heap, 1000);
	clobber_stack();
	vm_heap_collect(heap);
	found = vm_atom_from_ascii(heap, "name-4999");
	check(same && found != NULL && vm_string_equal_ascii(found, "name-4999"), "atom: many atoms survive collections");
	check(vm_atom_from_ascii(heap, "span") == first, "atom: still the same after collections");
}
