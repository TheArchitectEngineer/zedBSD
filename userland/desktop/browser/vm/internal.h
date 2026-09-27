/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The inside of the heap, shared by the files of vm/ and by nobody else.
 */

#ifndef ZDESKTOP_BROWSER_VM_INTERNAL_H
#define ZDESKTOP_BROWSER_VM_INTERNAL_H

#include "vm/vm.h"

/* The size and alignment of a block of small cells. */
#define VM_BLOCK_SIZE		(64U * 1024U)

/* The alignment of every cell (the low bits of a boxed pointer are free). */
#define VM_CELL_ALIGN		16U

/* The most cells a block can hold (all of the smallest size). */
#define VM_BLOCK_CELLS_MAX	(VM_BLOCK_SIZE / VM_CELL_ALIGN)

/* How many size classes of small cells there are. */
#define VM_SIZE_CLASSES		29U

/* The largest small cell; anything larger gets an allocation of its own. */
#define VM_SMALL_MAX		4096U

/*
 * One 64 KiB block of cells of one size.
 *
 * The cells start at first_offset.  A cell is in use when its allocated
 * bit is set; the marked bits are only meaningful during a collection.
 * Free cells are chained through their first word in the heap's free list
 * for the block's size class.
 */
struct vm_block {
	struct vm_block *next;
	uint32_t cell_size;
	uint32_t cell_count;
	uint32_t first_offset;
	uint32_t used;
	uint32_t size_class;
	uint32_t reserved;
	uint8_t allocated[VM_BLOCK_CELLS_MAX / 8U];
	uint8_t marked[VM_BLOCK_CELLS_MAX / 8U];
};

/*
 * A cell too large for a block, with its own allocation.
 *
 * The header comes first and the cell follows at an aligned offset; the
 * large cells are kept sorted by address so a stack word can be looked up.
 */
struct vm_large {
	size_t size;
	int marked;
	int reserved;
};

/*
 * A tracer and the context it is called with.
 */
struct vm_tracer_entry {
	vm_tracer tracer;
	void *context;
};

/*
 * The atoms of a heap: an open-addressing table of interned strings.
 *
 * slots is a power of two long; an empty slot is NULL.  Atoms are never
 * removed, so no tombstones are needed.
 */
struct vm_atom_table {
	struct vm_string **slots;
	size_t capacity;
	size_t count;
};

/*
 * A garbage-collected heap.
 */
struct vm_heap {
	/* The blocks of small cells, and each size class's free cells. */
	struct vm_block *blocks;
	void *free_lists[VM_SIZE_CLASSES];

	/* The set of block addresses (open addressing, NULL is empty) for looking up stack words. */
	struct vm_block **block_set;
	size_t block_set_capacity;
	size_t block_count;

	/* The large cells' headers, sorted by address. */
	struct vm_large **large;
	size_t large_count;
	size_t large_capacity;

	/* The lowest and highest address of any cell, for rejecting stack words quickly. */
	uintptr_t lowest;
	uintptr_t highest;

	/* The byte counts that decide when to collect, and the limit on live bytes. */
	size_t live_bytes;
	size_t live_cells;
	size_t allocated_since;
	size_t threshold;
	size_t limit;

	/* The bottom of the C stack the collector scans up to. */
	const void *stack_base;

	/* The roots: slots holding a cell, and tracers of subsystems. */
	struct wb_vector roots;
	struct wb_vector tracers;

	/* The cells marked and not yet traced. */
	struct wb_vector mark_stack;

	/* Whether a collection is running (allocation is refused inside one). */
	int collecting;

	/* The counts for vm_heap_stats. */
	size_t collections;
	size_t freed_cells;

	/* The heap's atoms. */
	struct vm_atom_table atoms;

	/*
	 * The root of the shape tree (no properties), made by the first
	 * object and a registered root from then on, which keeps every shape
	 * made from it.
	 */
	struct vm_shape *root_shape;
};

/* The atom table (atom.c). */
void vm_atom_table_trace(struct vm_heap *heap);
void vm_atom_table_release(struct vm_heap *heap);

/* Strings (string.c). */
struct vm_string *vm_string_alloc(struct vm_heap *heap, size_t length, int wide);
unsigned char *vm_string_latin1_mutable(struct vm_string *string);
uint16_t *vm_string_units_mutable(struct vm_string *string);

#endif
