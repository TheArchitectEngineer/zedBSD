/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Spreading and destructuring (ws074-p079): iterating a value for an
 * array pattern or a spread, copying an object's own enumerable properties
 * for an object spread and an object pattern's rest, and calling with the
 * elements of an array as the arguments.
 *
 * Iteration here is the built-in iteration of the values that have it
 * without a user's iterator: an array, an arguments object (their elements
 * by index, the length read at each step) and a string (by code point).
 * Anything else is a TypeError ("is not iterable") until Symbol.iterator
 * arrives (ws074-p028), which will widen vm_iter_start.
 */

#include "vm/internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* The most arguments a call with a spread passes. */
#define SPREAD_ARGUMENTS_MAX	65535U

/*
 * The state of one iteration of a value: the value, the next index (an
 * element's, or a string's code unit), and whether it has ended.  It is a
 * cell so that a register can hold it while the code between steps runs.
 */
struct spread_iterator {
	struct vm_cell cell;
	vm_value source;
	uint32_t index;
	int done;
};

static void spread_iterator_trace(struct vm_heap *heap, struct vm_cell *cell);
static int spread_length(struct vm_realm *realm, vm_value source, uint32_t *length);
static int spread_iterable(vm_value value);
static int spread_copy(struct vm_realm *realm, struct vm_object *target, vm_value source, vm_value excluded);
static int spread_is_excluded(struct vm_realm *realm, vm_value excluded, vm_value key, int *found);

/* The cell type of an iteration's state: it keeps the value it iterates. */
static const struct vm_cell_type spread_iterator_type = {
	"iterator", spread_iterator_trace, NULL
};

/*
 * Starts iterating a value (an array pattern's or a spread's): the state
 * in a new cell, or a TypeError for a value that cannot be iterated.
 */
int
vm_iter_start(
	struct vm_realm *realm,
	vm_value value,
	vm_value *iterator)
{
	struct spread_iterator *state;
	int iterable;
	int status;

	/* Only the values with a built-in iteration. */
	iterable = spread_iterable(value);
	if (!iterable) {
		status = vm_throw_type_error(realm, "value is not iterable");
		return status;
	}

	/* The state, from the first element. */
	state = vm_heap_alloc(realm->heap, &spread_iterator_type, sizeof(*state));
	if (state == NULL)
		return ENOMEM;
	state->source = value;
	state->index = 0;
	state->done = 0;

	/* Succeeded: the iteration's state. */
	*iterator = vm_value_cell(state);
	return 0;
}

/*
 * Takes the next value of an iteration; undefined once it has ended (an
 * array pattern's element past the end).  *done says whether it had.
 */
int
vm_iter_next(
	struct vm_realm *realm,
	vm_value iterator,
	vm_value *value,
	int *done)
{
	struct spread_iterator *state;
	struct vm_string *string;
	uint16_t units[2];
	uint32_t length;
	int is_string;
	int status;

	/* An ended iteration stays ended. */
	state = (struct spread_iterator *)vm_value_as_cell(iterator);
	*value = VM_VALUE_UNDEFINED;
	*done = 1;
	if (state->done)
		return 0;

	/* A string: the next code point (a surrogate pair together). */
	is_string = vm_value_is_string(state->source);
	if (is_string) {
		string = (struct vm_string *)vm_value_as_cell(state->source);
		if (state->index >= string->length) {
			state->done = 1;
			return 0;
		}

		/* The first unit, and a trailing surrogate that pairs with it. */
		units[0] = vm_string_at(string, state->index);
		length = 1;
		if (units[0] >= 0xD800U && units[0] <= 0xDBFFU && state->index + 1U < string->length) {
			units[1] = vm_string_at(string, state->index + 1U);
			if (units[1] >= 0xDC00U && units[1] <= 0xDFFFU)
				length = 2;
		}

		/* The code point's string. */
		string = vm_string_from_units(realm->heap, units, length);
		if (string == NULL)
			return ENOMEM;
		state->index += length;
		*value = vm_value_cell(string);
		*done = 0;
		return 0;
	}

	/* An array or an arguments object: the element at the index, while it is below the length. */
	status = spread_length(realm, state->source, &length);
	if (status != 0)
		return status;
	if (state->index >= length) {
		state->done = 1;
		return 0;
	}

	/* The element at the index. */
	status = vm_get(realm, state->source, vm_value_int32((int32_t)state->index), value);
	if (status != 0)
		return status;

	/* Succeeded: the next value. */
	state->index++;
	*done = 0;
	return 0;
}

/*
 * Makes an array of the values an iteration has left (an array pattern's
 * rest element).
 */
int
vm_iter_rest(
	struct vm_realm *realm,
	vm_value iterator,
	vm_value *array)
{
	struct vm_object *rest;
	vm_value value;
	int done;
	int status;

	/* The array, reachable from the C stack while values are taken. */
	rest = vm_array_create(realm->heap, realm->array_prototype);
	if (rest == NULL)
		return ENOMEM;

	/* Each value left, at the end of the array. */
	for (;;) {
		status = vm_iter_next(realm, iterator, &value, &done);
		if (status != 0)
			return status;
		if (done)
			break;
		status = vm_object_define(realm->heap, rest, vm_value_int32((int32_t)rest->length), value, VM_PROPERTY_DEFAULT);
		if (status != 0)
			return status;
	}

	/* Succeeded: the array of the rest. */
	*array = vm_value_cell(rest);
	return 0;
}

/*
 * Appends every value of an iterable to an array (a spread in an array
 * literal or in a call's arguments).
 */
int
vm_array_spread(
	struct vm_realm *realm,
	vm_value array,
	vm_value value)
{
	struct vm_object *target;
	vm_value iterator;
	vm_value element;
	int done;
	int status;

	/* The iteration of the value. */
	status = vm_iter_start(realm, value, &iterator);
	if (status != 0)
		return status;

	/* Each value at the end of the array. */
	target = (struct vm_object *)vm_value_as_cell(array);
	for (;;) {
		status = vm_iter_next(realm, iterator, &element, &done);
		if (status != 0)
			return status;
		if (done)
			break;
		status = vm_object_define(realm->heap, target, vm_value_int32((int32_t)target->length), element, VM_PROPERTY_DEFAULT);
		if (status != 0)
			return status;
	}

	/* Succeeded: the values are appended. */
	return 0;
}

/*
 * Copies the own enumerable properties of a value to an object (an object
 * literal's spread); undefined and null copy nothing.
 */
int
vm_copy_data_properties(
	struct vm_realm *realm,
	vm_value target,
	vm_value source)
{
	int status;

	/* Every property, none excluded. */
	status = spread_copy(realm, (struct vm_object *)vm_value_as_cell(target), source, VM_VALUE_UNDEFINED);
	if (status != 0)
		return status;

	/* Succeeded: the properties are copied. */
	return 0;
}

/*
 * Makes the object of an object pattern's rest: a new object with the own
 * enumerable properties of the source but those whose keys the array
 * excluded lists (the keys the pattern took).
 */
int
vm_object_rest(
	struct vm_realm *realm,
	vm_value source,
	vm_value excluded,
	vm_value *result)
{
	struct vm_object *rest;
	int status;

	/* The new object, reachable from the C stack while properties are read. */
	rest = vm_object_create(realm->heap, realm->object_prototype);
	if (rest == NULL)
		return ENOMEM;

	/* The properties that are not excluded. */
	status = spread_copy(realm, rest, source, excluded);
	if (status != 0)
		return status;

	/* Succeeded: the rest. */
	*result = vm_value_cell(rest);
	return 0;
}

/*
 * Calls a function (or, with construct, constructs with it as new.target)
 * with the elements of an array as the arguments: a call with a spread.
 */
int
vm_call_array(
	struct vm_realm *realm,
	vm_value function,
	vm_value this_value,
	vm_value array,
	int construct,
	vm_value *result)
{
	struct vm_object *list;
	vm_value *args;
	uint32_t count;
	uint32_t index;
	int callable;
	int status;

	/* A value that is not a function cannot be called. */
	callable = vm_value_is_callable(function);
	if (!callable) {
		status = vm_throw_type_error(realm, "value is not a function");
		return status;
	}

	/* How many arguments; too many is the error a call too deep gives. */
	list = (struct vm_object *)vm_value_as_cell(array);
	count = list->length;
	if (count > SPREAD_ARGUMENTS_MAX) {
		status = vm_throw_range_error(realm, "Maximum call stack size exceeded");
		return status;
	}

	/* The arguments, copied out of the array (the array stays in a register of the caller). */
	args = calloc((size_t)count + 1U, sizeof(vm_value));
	if (args == NULL)
		return ENOMEM;
	for (index = 0; index < count; index++) {
		status = vm_get(realm, array, vm_value_int32((int32_t)index), &args[index]);
		if (status != 0) {
			free(args);
			return status;
		}
	}

	/* The call, or the construction. */
	if (construct) {
		status = vm_construct(realm, function, args, count, function, result);
	} else {
		status = vm_call(realm, function, this_value, args, count, result);
	}

	/* The arguments are no longer needed. */
	free(args);
	if (status != 0)
		return status;

	/* Succeeded: the call's result. */
	return 0;
}

/* Marks the value an iteration's state iterates. */
static void
spread_iterator_trace(
	struct vm_heap *heap,
	struct vm_cell *cell)
{
	struct spread_iterator *state;

	/* The value. */
	state = (struct spread_iterator *)cell;
	vm_heap_mark_value(heap, state->source);
}

/* Reads the length of an array or an arguments object. */
static int
spread_length(
	struct vm_realm *realm,
	vm_value source,
	uint32_t *length)
{
	struct vm_object *object;
	vm_value key;
	vm_value value;
	double number;
	int status;

	/* An array's length is its own. */
	object = (struct vm_object *)vm_value_as_cell(source);
	if (object->kind == VM_KIND_ARRAY) {
		*length = object->length;
		return 0;
	}

	/* An arguments object's is its length property. */
	key = vm_key_from_ascii(realm->heap, "length");
	if (key == VM_VALUE_EMPTY)
		return ENOMEM;
	status = vm_get(realm, source, key, &value);
	if (status != 0)
		return status;
	status = vm_to_number(realm, value, &number);
	if (status != 0)
		return status;

	/* A length that is not a whole number within range is cut to one. */
	*length = 0;
	if (number > 0.0 && number < 4294967295.0)
		*length = (uint32_t)number;

	/* Succeeded: the length. */
	return 0;
}

/* Tells whether a value has a built-in iteration: an array, an arguments object or a string. */
static int
spread_iterable(
	vm_value value)
{
	struct vm_object *object;
	int is_string;
	int is_object;

	/* A string. */
	is_string = vm_value_is_string(value);
	if (is_string)
		return 1;

	/* Only objects are left. */
	is_object = vm_value_is_object(value);
	if (!is_object)
		return 0;

	/* An array or an arguments object. */
	object = (struct vm_object *)vm_value_as_cell(value);
	if (object->kind == VM_KIND_ARRAY)
		return 1;
	if (object->kind == VM_KIND_ARGUMENTS)
		return 1;

	/* Nothing else in this pass. */
	return 0;
}

/*
 * Copies the own enumerable properties of a source to a target, but those
 * whose keys the array excluded lists (undefined for none); undefined and
 * null copy nothing, and a primitive copies its object's.
 */
static int
spread_copy(
	struct vm_realm *realm,
	struct vm_object *target,
	vm_value source,
	vm_value excluded)
{
	struct vm_descriptor descriptor;
	struct vm_object *object;
	struct vm_object *keys;
	struct wb_vector list;
	vm_value key;
	vm_value value;
	uint32_t index;
	int present;
	int found;
	int status;

	/* Nothing to copy from undefined and null. */
	if (source == VM_VALUE_UNDEFINED || source == VM_VALUE_NULL)
		return 0;

	/* The source's object. */
	status = vm_to_object(realm, source, &source);
	if (status != 0)
		return status;
	object = (struct vm_object *)vm_value_as_cell(source);

	/* Its own keys, kept in an array of the heap while getters run. */
	wb_vector_init(&list, sizeof(vm_value));
	status = vm_object_own_keys(realm->heap, object, &list);
	if (status != 0) {
		wb_vector_release(&list);
		return status;
	}

	/* The array of the keys, which the collector sees through the C stack. */
	keys = vm_array_create(realm->heap, realm->array_prototype);
	if (keys == NULL) {
		wb_vector_release(&list);
		return ENOMEM;
	}

	/* Copies each key into the heap array. */
	for (index = 0; index < list.count; index++) {
		key = *(vm_value *)wb_vector_at(&list, index);
		status = vm_object_define(realm->heap, keys, vm_value_int32((int32_t)index), key, VM_PROPERTY_DEFAULT);
		if (status != 0) {
			wb_vector_release(&list);
			return status;
		}
	}

	/* The keys are in the heap array now. */
	wb_vector_release(&list);

	/* Each key that is still an enumerable own property and not excluded. */
	for (index = 0; index < keys->length; index++) {
		status = vm_get(realm, vm_value_cell(keys), vm_value_int32((int32_t)index), &key);
		if (status != 0)
			return status;
		present = vm_get_own_descriptor(object, key, &descriptor);
		if (!present)
			continue;
		if ((descriptor.attributes & VM_PROPERTY_ENUMERABLE) == 0U)
			continue;

		/* A key the pattern took stays out. */
		status = spread_is_excluded(realm, excluded, key, &found);
		if (status != 0)
			return status;
		if (found)
			continue;

		/* The value (through a getter), as a data property of the target. */
		status = vm_get(realm, source, key, &value);
		if (status != 0)
			return status;
		status = vm_object_define(realm->heap, target, key, value, VM_PROPERTY_DEFAULT);
		if (status != 0)
			return status;
	}

	/* Succeeded: the properties are copied. */
	return 0;
}

/* Tells whether the array of excluded keys (undefined for none) lists a key. */
static int
spread_is_excluded(
	struct vm_realm *realm,
	vm_value excluded,
	vm_value key,
	int *found)
{
	struct vm_object *list;
	vm_value listed;
	uint32_t index;
	int status;

	/* Without a list nothing is excluded. */
	*found = 0;
	if (excluded == VM_VALUE_UNDEFINED)
		return 0;

	/* Each listed key, compared as a property key. */
	list = (struct vm_object *)vm_value_as_cell(excluded);
	for (index = 0; index < list->length; index++) {
		status = vm_get(realm, excluded, vm_value_int32((int32_t)index), &listed);
		if (status != 0)
			return status;
		status = vm_to_key(realm, listed, &listed);
		if (status != 0)
			return status;
		if (listed == key) {
			*found = 1;
			return 0;
		}
	}

	/* Succeeded: not listed. */
	return 0;
}
