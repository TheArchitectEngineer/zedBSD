/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The shared execution engine of zdesktop-browser (plan/ws074/design.md
 * §11): the garbage-collected heap, strings and atoms, and later the
 * values, objects, bytecode and interpreter that JavaScript and Wasm share.
 *
 * The heap is non-moving mark-and-sweep.  Its roots are found by scanning
 * the C stack conservatively (every word that points into a live cell
 * keeps it) and by the tracers and root slots its users register; inside
 * the heap each cell's type traces the cells it refers to exactly.  A cell
 * held only from memory the collector cannot see (malloc'd arrays, arenas)
 * must be reported by a tracer, or it is freed under its holder.
 *
 * One heap serves one tab; nothing is shared between heaps.
 */

#ifndef ZDESKTOP_BROWSER_VM_H
#define ZDESKTOP_BROWSER_VM_H

#include "base/base.h"

#include <stddef.h>
#include <stdint.h>

struct vm_heap;
struct vm_cell;

/*
 * The behavior every cell of one kind shares.
 *
 * trace marks the cells a cell refers to (with vm_heap_mark); finalize
 * frees what a dead cell owns outside the heap and must not allocate in
 * the heap.  Either may be NULL.  A type lives as long as the program:
 * cells point to it.
 */
struct vm_cell_type {
	const char *name;
	void (*trace)(struct vm_heap *heap, struct vm_cell *cell);
	void (*finalize)(struct vm_heap *heap, struct vm_cell *cell);
};

/*
 * The header at the start of every cell.
 *
 * A cell's own structure begins with this header, so a pointer to the
 * structure is a pointer to its cell.
 */
struct vm_cell {
	const struct vm_cell_type *type;
};

/*
 * A string in the heap: Latin-1 (one byte per unit) or UTF-16.
 *
 * The characters follow the header; vm_string_latin1 and vm_string_units
 * find them.  Strings never change after they are made.  An atom is a
 * string interned in its heap's atom table: two atoms with the same
 * characters are the same cell, so atoms compare by pointer.
 */
struct vm_string {
	struct vm_cell cell;
	uint32_t length;
	uint32_t hash;
	uint32_t flags;
	uint32_t reserved;
};

/* The string's characters are UTF-16 code units rather than Latin-1 bytes. */
#define VM_STRING_WIDE		0x1U

/* The string is its heap's atom for these characters. */
#define VM_STRING_ATOM		0x2U

/* The hash field holds the string's hash. */
#define VM_STRING_HASHED	0x4U

/*
 * A function that marks cells a subsystem holds outside the heap.
 */
typedef void (*vm_tracer)(struct vm_heap *heap, void *context);

/*
 * What the heap has done, for tests and diagnostics.
 */
struct vm_heap_stats {
	size_t live_bytes;
	size_t live_cells;
	size_t heap_bytes;
	size_t collections;
	size_t freed_cells;
};

/* The heap (heap.c). */
int vm_heap_create(struct vm_heap **heap, size_t limit);
void vm_heap_destroy(struct vm_heap *heap);
void vm_heap_set_stack_base(struct vm_heap *heap, const void *base);
void *vm_heap_alloc(struct vm_heap *heap, const struct vm_cell_type *type, size_t size);
void vm_heap_collect(struct vm_heap *heap);
void vm_heap_mark(struct vm_heap *heap, struct vm_cell *cell);
void vm_heap_mark_word(struct vm_heap *heap, uintptr_t word);
int vm_heap_add_root(struct vm_heap *heap, struct vm_cell **slot);
void vm_heap_remove_root(struct vm_heap *heap, struct vm_cell **slot);
int vm_heap_add_tracer(struct vm_heap *heap, vm_tracer tracer, void *context);
void vm_heap_remove_tracer(struct vm_heap *heap, vm_tracer tracer, void *context);
struct vm_cell *vm_heap_find_cell(struct vm_heap *heap, uintptr_t word);
void vm_heap_stats(const struct vm_heap *heap, struct vm_heap_stats *stats);

/* Strings (string.c). */
extern const struct vm_cell_type vm_string_type;
struct vm_string *vm_string_from_latin1(struct vm_heap *heap, const unsigned char *bytes, size_t length);
struct vm_string *vm_string_from_units(struct vm_heap *heap, const uint16_t *units, size_t length);
struct vm_string *vm_string_from_utf8(struct vm_heap *heap, const char *bytes, size_t length);
struct vm_string *vm_string_concat(struct vm_heap *heap, const struct vm_string *left, const struct vm_string *right);
const unsigned char *vm_string_latin1(const struct vm_string *string);
const uint16_t *vm_string_units(const struct vm_string *string);
uint16_t vm_string_at(const struct vm_string *string, size_t index);
uint32_t vm_string_hash(struct vm_string *string);
int vm_string_equal(const struct vm_string *left, const struct vm_string *right);
int vm_string_equal_ascii(const struct vm_string *string, const char *ascii);
int vm_string_compare(const struct vm_string *left, const struct vm_string *right);
int vm_string_to_utf8(const struct vm_string *string, struct wb_buffer *buffer);

/* Atoms (atom.c). */
struct vm_string *vm_atom(struct vm_heap *heap, struct vm_string *string);
struct vm_string *vm_atom_from_ascii(struct vm_heap *heap, const char *ascii);
struct vm_string *vm_atom_from_units(struct vm_heap *heap, const uint16_t *units, size_t length);
struct vm_string *vm_atom_find_units(struct vm_heap *heap, const uint16_t *units, size_t length);

#endif
