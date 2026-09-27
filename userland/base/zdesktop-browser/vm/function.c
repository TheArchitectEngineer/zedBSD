/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Functions (plan/ws074/design.md §11.6): the function cell, native
 * functions, calling one, and throwing.
 *
 * A function is an object with its realm and what runs when it is called.
 * The first pass has native functions only; bytecode functions and the
 * interpreter's frames arrive in ws074-p023, where vm_call gains its
 * second kind of callee.
 */

#include "vm/bytecode.h"
#include "vm/internal.h"

#include <errno.h>
#include <string.h>

static void function_trace(struct vm_heap *heap, struct vm_cell *cell);

/* The cell type of functions: objects that also hold their realm's objects through the realm's tracer. */
const struct vm_cell_type vm_function_type = {
	"function", function_trace, vm_object_finalize
};

/*
 * Makes a native function of a realm with its name and length properties
 * (neither writable nor enumerable, but configurable, as for built-ins);
 * NULL when out of memory.
 */
struct vm_function *
vm_function_create_native(
	struct vm_realm *realm,
	const char *name,
	unsigned length,
	vm_native native)
{
	struct vm_function *function;
	struct vm_string *text;
	vm_value key;
	int error;

	/* The cell, an object whose prototype is Function.prototype. */
	function = vm_heap_alloc(realm->heap, &vm_function_type, sizeof(*function));
	if (function == NULL)
		return NULL;
	error = vm_object_init(realm->heap, &function->object, realm->function_prototype);
	if (error != 0)
		return NULL;
	function->realm = realm;
	function->native = native;

	/* Its length: how many arguments it expects. */
	key = vm_key_from_ascii(realm->heap, "length");
	if (key == VM_VALUE_EMPTY)
		return NULL;
	error = vm_object_define(realm->heap, &function->object, key, vm_value_int32((int32_t)length), VM_PROPERTY_CONFIGURABLE);
	if (error != 0)
		return NULL;

	/* Its name. */
	text = vm_string_from_utf8(realm->heap, name, strlen(name));
	if (text == NULL)
		return NULL;
	key = vm_key_from_ascii(realm->heap, "name");
	if (key == VM_VALUE_EMPTY)
		return NULL;
	error = vm_object_define(realm->heap, &function->object, key, vm_value_cell(text), VM_PROPERTY_CONFIGURABLE);
	if (error != 0)
		return NULL;

	/* Succeeded: the function. */
	return function;
}

/*
 * Makes a bytecode function of a realm from a checked code unit, with its
 * name (the code's) and length (its parameter count); NULL when out of
 * memory.
 */
struct vm_function *
vm_function_create(
	struct vm_realm *realm,
	struct vm_code *code)
{
	struct vm_function *function;
	vm_value key;
	vm_value name;
	int error;

	/* The cell, an object whose prototype is Function.prototype, running the code. */
	function = vm_heap_alloc(realm->heap, &vm_function_type, sizeof(*function));
	if (function == NULL)
		return NULL;
	error = vm_object_init(realm->heap, &function->object, realm->function_prototype);
	if (error != 0)
		return NULL;
	function->realm = realm;
	function->code = code;

	/* Its length: how many parameters it declares. */
	key = vm_key_from_ascii(realm->heap, "length");
	if (key == VM_VALUE_EMPTY)
		return NULL;
	error = vm_object_define(realm->heap, &function->object, key, vm_value_int32((int32_t)code->parameter_count),
	    VM_PROPERTY_CONFIGURABLE);
	if (error != 0)
		return NULL;

	/* Its name: the code's, or the empty string. */
	name = VM_VALUE_EMPTY;
	if (code->name != NULL)
		name = vm_value_cell(code->name);
	if (name == VM_VALUE_EMPTY) {
		code->name = vm_string_from_utf8(realm->heap, "", 0);
		if (code->name == NULL)
			return NULL;
		name = vm_value_cell(code->name);
	}

	/* The name property. */
	key = vm_key_from_ascii(realm->heap, "name");
	if (key == VM_VALUE_EMPTY)
		return NULL;
	error = vm_object_define(realm->heap, &function->object, key, name, VM_PROPERTY_CONFIGURABLE);
	if (error != 0)
		return NULL;

	/* Succeeded: the function. */
	return function;
}

/*
 * Tells whether a value can be called.
 */
int
vm_value_is_callable(
	vm_value value)
{
	struct vm_cell *cell;
	int is_cell;

	/* Only cells are functions. */
	is_cell = vm_value_is_cell(value);
	if (!is_cell)
		return 0;

	/* A function cell. */
	cell = vm_value_as_cell(value);
	if (cell->type == &vm_function_type)
		return 1;

	/* Any other cell. */
	return 0;
}

/*
 * Calls a function with a this value and arguments and stores its result;
 * returns 0, VM_THROWN with the realm's exception set, or an errno value.
 */
int
vm_call(
	struct vm_realm *realm,
	vm_value callee,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct vm_function *function;
	int callable;
	int status;

	/* Only a function can be called (a TypeError when the realm has its constructors). */
	*result = VM_VALUE_UNDEFINED;
	callable = vm_value_is_callable(callee);
	if (!callable) {
		status = vm_throw(realm, VM_VALUE_UNDEFINED);
		return status;
	}

	/* A bytecode function runs in the interpreter. */
	function = (struct vm_function *)vm_value_as_cell(callee);
	if (function->code != NULL) {
		status = vm_interpret(function->realm, function, this_value, args, count, result);
		return status;
	}

	/* A function with neither has nothing to run. */
	if (function->native == NULL)
		return ENOSYS;

	/* Runs the native code in the function's own realm. */
	status = function->native(function->realm, this_value, args, count, result);
	if (status == VM_THROWN)
		return VM_THROWN;
	if (status != 0)
		return status;

	/* Succeeded: the result is stored. */
	return 0;
}

/*
 * Throws a value: it becomes the realm's exception.  Returns VM_THROWN, so
 * a native function can end with "return vm_throw(...)".
 */
int
vm_throw(
	struct vm_realm *realm,
	vm_value exception)
{
	/* The exception waits in the realm for whoever catches it. */
	realm->exception = exception;

	/* Reports that a value was thrown. */
	return VM_THROWN;
}

/* Marks what a function refers to: what every object refers to, and its code (its realm's objects are the realm's tracer's). */
static void
function_trace(
	struct vm_heap *heap,
	struct vm_cell *cell)
{
	struct vm_function *function;

	/* The object's shape, prototype, slots and elements. */
	vm_object_trace(heap, cell);

	/* The code unit it runs. */
	function = (struct vm_function *)cell;
	if (function->code != NULL)
		vm_heap_mark(heap, (struct vm_cell *)function->code);
}
