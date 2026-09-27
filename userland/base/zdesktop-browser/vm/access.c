/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Properties of any value as the language reaches them: getting, setting
 * (with strict mode's errors), deleting, the in and instanceof operators,
 * an object literal's definitions, the global variables of scripts, and
 * the enumeration of for-in.
 *
 * The primitives other than strings get their prototypes (Number.prototype
 * and the rest) with the built-ins (ws074-p026); until then their
 * properties read undefined.
 */

#include "vm/internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/*
 * The state of one for-in loop: the keys it will visit, found when it
 * started, and how far it has come.
 *
 * The keys are a malloc'd array freed with the cell; a key is checked
 * again before it is visited, so one deleted meanwhile is skipped.  The
 * cell lives in a register of the loop's frame and is never a value the
 * script sees.
 */
struct access_for_in {
	struct vm_cell cell;
	vm_value object;
	vm_value *keys;
	uint32_t count;
	uint32_t index;
};

static void access_for_in_trace(struct vm_heap *heap, struct vm_cell *cell);
static void access_for_in_finalize(struct vm_heap *heap, struct vm_cell *cell);
static int access_string_property(struct vm_realm *realm, struct vm_string *string, vm_value key, vm_value *result, int *found);
static int access_set_length(struct vm_realm *realm, struct vm_object *array, vm_value value, int strict);
static int access_refuse(struct vm_realm *realm, int strict, const char *message);
static int access_collect(struct vm_heap *heap, struct vm_object *object, struct wb_vector *keys);
static int access_key_listed(const struct wb_vector *keys, size_t count, vm_value key);
static int access_is_symbol(vm_value value);

/* The cell type of for-in states: they hold their object and keys. */
static const struct vm_cell_type access_for_in_type = {
	"for-in", access_for_in_trace, access_for_in_finalize
};

/*
 * Gets a property of any value: an object's through its chain (calling an
 * accessor's getter), a string's length and characters; reading from
 * undefined or null throws.
 */
int
vm_get(
	struct vm_realm *realm,
	vm_value base,
	vm_value key,
	vm_value *result)
{
	struct vm_property property;
	struct vm_accessor *accessor;
	struct vm_cell *cell;
	int is_cell;
	int found;
	int status;

	/* undefined and null have no properties. */
	*result = VM_VALUE_UNDEFINED;
	if (base == VM_VALUE_UNDEFINED || base == VM_VALUE_NULL) {
		status = vm_throw_type_error(realm, "Cannot read properties of undefined or null");
		return status;
	}

	/* Other primitives' prototypes arrive with the built-ins. */
	is_cell = vm_value_is_cell(base);
	if (!is_cell)
		return 0;

	/* A string's length and its characters. */
	cell = vm_value_as_cell(base);
	if (cell->type == &vm_string_type) {
		status = access_string_property(realm, (struct vm_string *)cell, key, result, &found);
		if (status != 0)
			return status;
		return 0;
	}

	/* A symbol's prototype arrives with the built-ins. */
	if (cell->type == &vm_symbol_type)
		return 0;

	/* An object's property, wherever on its chain. */
	found = vm_object_find((struct vm_object *)cell, key, &property);
	if (!found)
		return 0;

	/* A data property's value. */
	if ((property.attributes & VM_PROPERTY_ACCESSOR) == 0U) {
		*result = *property.value;
		return 0;
	}

	/* An accessor's getter, called with the base as this (no getter reads undefined). */
	accessor = (struct vm_accessor *)vm_value_as_cell(*property.value);
	if (accessor->getter == VM_VALUE_UNDEFINED)
		return 0;
	status = vm_call(realm, accessor->getter, base, NULL, 0, result);
	if (status != 0)
		return status;

	/* Succeeded: the getter's result. */
	return 0;
}

/*
 * Puts a property of any value as sloppy code does (a refused assignment
 * is ignored).
 */
int
vm_put(
	struct vm_realm *realm,
	vm_value base,
	vm_value key,
	vm_value value)
{
	int status;

	/* The assignment without strict mode's errors. */
	status = vm_set(realm, base, key, value, 0);
	if (status != 0)
		return status;

	/* Succeeded: the value is put (or refused in silence). */
	return 0;
}

/*
 * Puts a property of any value by assignment: an object's through an
 * accessor's setter or into a data property; undefined and null throw.  A
 * refused assignment (a read-only property, an accessor without a setter,
 * an object that takes no new property, a primitive) is ignored, or a
 * TypeError in strict code.
 */
int
vm_set(
	struct vm_realm *realm,
	vm_value base,
	vm_value key,
	vm_value value,
	int strict)
{
	struct vm_property property;
	struct vm_accessor *accessor;
	struct vm_object *object;
	vm_value ignored;
	vm_value length_key;
	int is_object;
	int found;
	int done;
	int status;

	/* undefined and null have no properties. */
	if (base == VM_VALUE_UNDEFINED || base == VM_VALUE_NULL) {
		status = vm_throw_type_error(realm, "Cannot set properties of undefined or null");
		return status;
	}

	/* A primitive takes no property. */
	is_object = vm_value_is_object(base);
	if (!is_object) {
		status = access_refuse(realm, strict, "Cannot create property on primitive value");
		return status;
	}

	/* An accessor on the chain: its setter is called with the value. */
	object = (struct vm_object *)vm_value_as_cell(base);
	found = vm_object_find(object, key, &property);
	if (found && (property.attributes & VM_PROPERTY_ACCESSOR) != 0U) {
		accessor = (struct vm_accessor *)vm_value_as_cell(*property.value);
		if (accessor->setter == VM_VALUE_UNDEFINED) {
			status = access_refuse(realm, strict, "Cannot set property which has only a getter");
			return status;
		}

		/* The setter, called with the value. */
		status = vm_call(realm, accessor->setter, base, &value, 1, &ignored);
		if (status != 0)
			return status;
		return 0;
	}

	/* A read-only data property refuses. */
	if (found && (property.attributes & VM_PROPERTY_WRITABLE) == 0U) {
		status = access_refuse(realm, strict, "Cannot assign to read only property");
		return status;
	}

	/* An array's own length is set by its rules. */
	length_key = vm_key_from_ascii(realm->heap, "length");
	if (length_key == VM_VALUE_EMPTY)
		return ENOMEM;
	if (found && property.holder == object && key == length_key && (object->flags & VM_OBJECT_ARRAY) != 0U) {
		status = access_set_length(realm, object, value, strict);
		if (status != 0)
			return status;
		return 0;
	}

	/* Otherwise the ordinary assignment; an object that takes no new property refuses. */
	status = vm_object_set(realm->heap, object, key, value, &done);
	if (status != 0)
		return status;
	if (!done) {
		status = access_refuse(realm, strict, "Cannot add property, object is not extensible");
		return status;
	}

	/* Succeeded: the value is put. */
	return 0;
}

/*
 * Deletes a property of any value (the delete operator) and stores whether
 * it is gone; a property that stays is a TypeError in strict code.
 */
int
vm_delete(
	struct vm_realm *realm,
	vm_value base,
	vm_value key,
	int strict,
	vm_value *result)
{
	struct vm_object *object;
	struct vm_string *string;
	vm_value ignored;
	int is_string;
	int is_object;
	int found;
	int deleted;
	int status;

	/* undefined and null have no properties. */
	*result = VM_VALUE_TRUE;
	if (base == VM_VALUE_UNDEFINED || base == VM_VALUE_NULL) {
		status = vm_throw_type_error(realm, "Cannot convert undefined or null to object");
		return status;
	}

	/* A string's length and characters stay; any other property of a primitive is not there. */
	is_object = vm_value_is_object(base);
	if (!is_object) {
		is_string = vm_value_is_string(base);
		found = 0;
		if (is_string) {
			string = (struct vm_string *)vm_value_as_cell(base);
			status = access_string_property(realm, string, key, &ignored, &found);
			if (status != 0)
				return status;
		}

		/* A string keeps its own properties. */
		if (found) {
			*result = VM_VALUE_FALSE;
			status = access_refuse(realm, strict, "Cannot delete property of a string");
			return status;
		}

		/* Any other property of a primitive is not there. */
		return 0;
	}

	/* An object's own property, unless it is not configurable. */
	object = (struct vm_object *)vm_value_as_cell(base);
	status = vm_object_delete(realm->heap, object, key, &deleted);
	if (status != 0)
		return status;
	if (!deleted) {
		*result = VM_VALUE_FALSE;
		status = access_refuse(realm, strict, "Cannot delete property");
		return status;
	}

	/* Succeeded: the property is gone. */
	return 0;
}

/*
 * Tells whether an object has a property on its chain (the in operator);
 * the right side must be an object.
 */
int
vm_in(
	struct vm_realm *realm,
	vm_value key,
	vm_value object,
	vm_value *result)
{
	struct vm_property property;
	vm_value property_key;
	int is_object;
	int found;
	int status;

	/* Only an object can be searched. */
	*result = VM_VALUE_FALSE;
	is_object = vm_value_is_object(object);
	if (!is_object) {
		status = vm_throw_type_error(realm, "Cannot use 'in' operator to search for a key in a primitive");
		return status;
	}

	/* The key as a property key. */
	status = vm_to_key(realm, key, &property_key);
	if (status != 0)
		return status;

	/* Succeeded: whether the chain has it. */
	found = vm_object_find((struct vm_object *)vm_value_as_cell(object), property_key, &property);
	*result = vm_value_boolean(found);
	return 0;
}

/*
 * Tells whether a value is an instance of a constructor (the instanceof
 * operator): whether the constructor's prototype is on the value's chain.
 */
int
vm_instanceof(
	struct vm_realm *realm,
	vm_value value,
	vm_value constructor,
	vm_value *result)
{
	struct vm_object *object;
	vm_value key;
	vm_value prototype;
	vm_value link;
	int callable;
	int is_object;
	int status;

	/* Only a callable right side answers instanceof. */
	*result = VM_VALUE_FALSE;
	callable = vm_value_is_callable(constructor);
	if (!callable) {
		status = vm_throw_type_error(realm, "Right-hand side of 'instanceof' is not callable");
		return status;
	}

	/* A primitive is an instance of nothing. */
	is_object = vm_value_is_object(value);
	if (!is_object)
		return 0;

	/* The constructor's prototype, which must be an object. */
	key = vm_key_from_ascii(realm->heap, "prototype");
	if (key == VM_VALUE_EMPTY)
		return ENOMEM;
	status = vm_get(realm, constructor, key, &prototype);
	if (status != 0)
		return status;
	is_object = vm_value_is_object(prototype);
	if (!is_object) {
		status = vm_throw_type_error(realm, "Function has non-object prototype in instanceof check");
		return status;
	}

	/* Walks the value's chain looking for it. */
	object = ((struct vm_object *)vm_value_as_cell(value))->prototype;
	while (object != NULL) {
		link = vm_value_cell(object);
		if (link == prototype) {
			*result = VM_VALUE_TRUE;
			return 0;
		}

		/* The next object of the chain. */
		object = object->prototype;
	}

	/* Succeeded: not an instance. */
	return 0;
}

/*
 * Defines a data property of an object literal (not an assignment: a
 * setter on the chain is not called).
 */
int
vm_define_data(
	struct vm_realm *realm,
	vm_value object,
	vm_value key,
	vm_value value)
{
	struct vm_object *target;
	int status;

	/* The property, writable, enumerable and configurable. */
	target = (struct vm_object *)vm_value_as_cell(object);
	status = vm_object_define(realm->heap, target, key, value, VM_PROPERTY_DEFAULT);
	if (status != 0)
		return status;

	/* Succeeded: the property is defined. */
	return 0;
}

/*
 * Defines the getter (or, with setter, the setter) of an object literal's
 * accessor property, keeping the other half when the property is an
 * accessor already.
 */
int
vm_define_accessor(
	struct vm_realm *realm,
	vm_value object,
	vm_value key,
	vm_value function,
	int setter)
{
	struct vm_property property;
	struct vm_accessor *accessor;
	struct vm_object *target;
	vm_value getter_function;
	vm_value setter_function;
	int found;
	int status;

	/* The other half, from an own accessor already there. */
	target = (struct vm_object *)vm_value_as_cell(object);
	getter_function = VM_VALUE_UNDEFINED;
	setter_function = VM_VALUE_UNDEFINED;
	found = vm_object_get_own(target, key, &property);
	if (found && (property.attributes & VM_PROPERTY_ACCESSOR) != 0U) {
		accessor = (struct vm_accessor *)vm_value_as_cell(*property.value);
		getter_function = accessor->getter;
		setter_function = accessor->setter;
	}

	/* This half. */
	if (setter) {
		setter_function = function;
	} else {
		getter_function = function;
	}

	/* The pair, as an enumerable and configurable accessor property. */
	accessor = vm_accessor_create(realm->heap, getter_function, setter_function);
	if (accessor == NULL)
		return ENOMEM;
	status = vm_object_define(realm->heap, target, key, vm_value_cell(accessor),
	    VM_PROPERTY_ACCESSOR | VM_PROPERTY_ENUMERABLE | VM_PROPERTY_CONFIGURABLE);
	if (status != 0)
		return status;

	/* Succeeded: the accessor is defined. */
	return 0;
}

/*
 * Reads a global variable; a missing one is a ReferenceError, or
 * undefined for typeof.
 */
int
vm_get_global(
	struct vm_realm *realm,
	vm_value key,
	int for_typeof,
	vm_value *result)
{
	struct vm_property property;
	int found;
	int status;

	/* A name the global object does not have. */
	*result = VM_VALUE_UNDEFINED;
	found = vm_object_find(realm->global, key, &property);
	if (!found) {
		if (for_typeof)
			return 0;
		status = vm_throw_not_defined(realm, key);
		return status;
	}

	/* The property's value (through a getter). */
	status = vm_get(realm, vm_value_cell(realm->global), key, result);
	if (status != 0)
		return status;

	/* Succeeded: the variable's value. */
	return 0;
}

/*
 * Assigns a global variable: sloppy code makes a missing one; strict code
 * may not (a ReferenceError).
 */
int
vm_put_global(
	struct vm_realm *realm,
	vm_value key,
	vm_value value,
	int strict)
{
	struct vm_property property;
	int found;
	int status;

	/* Strict code assigns only a variable that exists. */
	if (strict) {
		found = vm_object_find(realm->global, key, &property);
		if (!found) {
			status = vm_throw_not_defined(realm, key);
			return status;
		}
	}

	/* The assignment to the global object. */
	status = vm_set(realm, vm_value_cell(realm->global), key, value, strict);
	if (status != 0)
		return status;

	/* Succeeded: the variable has the value. */
	return 0;
}

/*
 * Declares a var of a script: a global property that cannot be deleted,
 * undefined unless the global object has the name already.
 */
int
vm_define_global_var(
	struct vm_realm *realm,
	vm_value key)
{
	struct vm_property property;
	int found;
	int status;

	/* A name the global object has keeps its value. */
	found = vm_object_get_own(realm->global, key, &property);
	if (found)
		return 0;

	/* A new variable: writable and enumerable, not configurable. */
	status = vm_object_define(realm->heap, realm->global, key, VM_VALUE_UNDEFINED,
	    VM_PROPERTY_WRITABLE | VM_PROPERTY_ENUMERABLE);
	if (status == EPERM) {
		status = vm_throw_type_error(realm, "Cannot declare a global variable");
		return status;
	}

	/* Any other failure of the definition. */
	if (status != 0)
		return status;

	/* Succeeded: the variable exists. */
	return 0;
}

/*
 * Declares a function of a script: the global property takes the
 * function; a property there already must be configurable, or a writable
 * and enumerable data property.
 */
int
vm_define_global_function(
	struct vm_realm *realm,
	vm_value key,
	vm_value function)
{
	struct vm_property property;
	uint32_t attributes;
	int found;
	int replaceable;
	int status;

	/* A property there already decides whether the declaration may replace it. */
	attributes = VM_PROPERTY_WRITABLE | VM_PROPERTY_ENUMERABLE;
	found = vm_object_get_own(realm->global, key, &property);
	if (found && (property.attributes & VM_PROPERTY_CONFIGURABLE) == 0U) {
		replaceable = 0;
		if ((property.attributes & VM_PROPERTY_ACCESSOR) == 0U &&
		    (property.attributes & VM_PROPERTY_WRITABLE) != 0U &&
		    (property.attributes & VM_PROPERTY_ENUMERABLE) != 0U)
			replaceable = 1;
		if (!replaceable) {
			status = vm_throw_type_error(realm, "Cannot redefine a global function");
			return status;
		}

		/* A variable of the script takes the function as its value. */
		attributes = property.attributes;
	}

	/* The property. */
	status = vm_object_define(realm->heap, realm->global, key, function, attributes);
	if (status == EPERM) {
		status = vm_throw_type_error(realm, "Cannot declare a global function");
		return status;
	}

	/* Any other failure of the definition. */
	if (status != 0)
		return status;

	/* Succeeded: the function is declared. */
	return 0;
}

/*
 * Deletes a global variable (delete of a bare name in sloppy code) and
 * stores whether it is gone.
 */
int
vm_delete_global(
	struct vm_realm *realm,
	vm_value key,
	vm_value *result)
{
	int status;

	/* The global object's property (a var of a script is not configurable and stays). */
	status = vm_delete(realm, vm_value_cell(realm->global), key, 0, result);
	if (status != 0)
		return status;

	/* Succeeded: whether it is gone. */
	return 0;
}

/*
 * Starts a for-in loop over a value: lists the enumerable string keys of
 * an object and its chain (a key shadowed by a nearer property is listed
 * once, by the nearer one), or a string's indices; undefined, null and the
 * other primitives list nothing.
 */
int
vm_for_in_start(
	struct vm_realm *realm,
	vm_value value,
	vm_value *iterator)
{
	struct access_for_in *state;
	struct wb_vector keys;
	struct vm_string *string;
	vm_value key;
	uint32_t index;
	int is_object;
	int is_string;
	int status;

	/* The keys of the object's chain, or of the string. */
	wb_vector_init(&keys, sizeof(vm_value));
	is_object = vm_value_is_object(value);
	is_string = vm_value_is_string(value);
	status = 0;
	if (is_object)
		status = access_collect(realm->heap, (struct vm_object *)vm_value_as_cell(value), &keys);
	if (is_string) {
		string = (struct vm_string *)vm_value_as_cell(value);
		for (index = 0; status == 0 && index < string->length; index++) {
			key = vm_value_int32((int32_t)index);
			status = wb_vector_push(&keys, &key);
		}
	}

	/* A failure leaves nothing behind. */
	if (status != 0) {
		wb_vector_release(&keys);
		return status;
	}

	/* The loop's state, which takes the keys over. */
	state = vm_heap_alloc(realm->heap, &access_for_in_type, sizeof(*state));
	if (state == NULL) {
		wb_vector_release(&keys);
		return ENOMEM;
	}

	/* The keys, from the first. */
	state->object = value;
	state->keys = keys.items;
	state->count = (uint32_t)keys.count;
	state->index = 0;

	/* Succeeded: the state as the loop's iterator. */
	*iterator = vm_value_cell(state);
	return 0;
}

/*
 * Moves a for-in loop to its next key (as a string), skipping the keys
 * deleted since it started; done when there is none left.
 */
int
vm_for_in_next(
	struct vm_realm *realm,
	vm_value iterator,
	vm_value *key,
	int *done)
{
	struct access_for_in *state;
	struct vm_property property;
	struct vm_string *string;
	vm_value candidate;
	int is_object;
	int found;
	int status;

	/* The state; anything else in the register is a fault of the code. */
	*done = 1;
	state = (struct access_for_in *)vm_value_as_cell(iterator);
	if (state->cell.type != &access_for_in_type)
		return EINVAL;

	/* The next key the object still has. */
	is_object = vm_value_is_object(state->object);
	while (state->index < state->count) {
		candidate = state->keys[state->index];
		state->index++;
		if (is_object) {
			found = vm_object_find((struct vm_object *)vm_value_as_cell(state->object), candidate, &property);
			if (!found)
				continue;
		}

		/* The key as a string. */
		status = vm_to_string(realm, candidate, &string);
		if (status != 0)
			return status;

		/* Succeeded: the key. */
		*key = vm_value_cell(string);
		*done = 0;
		return 0;
	}

	/* Succeeded: the loop is over. */
	return 0;
}

/* Marks what a for-in state holds: its object and its keys. */
static void
access_for_in_trace(
	struct vm_heap *heap,
	struct vm_cell *cell)
{
	struct access_for_in *state;
	uint32_t index;

	/* The object, then each key. */
	state = (struct access_for_in *)cell;
	vm_heap_mark_value(heap, state->object);
	for (index = 0; state->keys != NULL && index < state->count; index++)
		vm_heap_mark_value(heap, state->keys[index]);
}

/* Frees a dead for-in state's keys. */
static void
access_for_in_finalize(
	struct vm_heap *heap,
	struct vm_cell *cell)
{
	struct access_for_in *state;

	UNUSED_PARAMETER(heap);

	/* The array of keys. */
	state = (struct access_for_in *)cell;
	free(state->keys);
	state->keys = NULL;
}

/* Reads a string's own property: its length, or a character by index; found says whether it has the key. */
static int
access_string_property(
	struct vm_realm *realm,
	struct vm_string *string,
	vm_value key,
	vm_value *result,
	int *found)
{
	struct vm_string *character;
	vm_value length_key;
	uint32_t index;
	uint16_t unit;
	int is_index;

	/* The length. */
	*found = 0;
	*result = VM_VALUE_UNDEFINED;
	length_key = vm_key_from_ascii(realm->heap, "length");
	if (length_key == VM_VALUE_EMPTY)
		return ENOMEM;
	if (key == length_key) {
		*result = vm_value_int32((int32_t)string->length);
		*found = 1;
		return 0;
	}

	/* A character is a one-unit string. */
	is_index = vm_value_is_array_index(key, &index);
	if (is_index && index < string->length) {
		unit = vm_string_at(string, index);
		character = vm_string_from_units(realm->heap, &unit, 1);
		if (character == NULL)
			return ENOMEM;
		*result = vm_value_cell(character);
		*found = 1;
	}

	/* Succeeded: the property, or nothing. */
	return 0;
}

/* Sets an array's length by assignment: a number that is a valid length, or a RangeError. */
static int
access_set_length(
	struct vm_realm *realm,
	struct vm_object *array,
	vm_value value,
	int strict)
{
	double number;
	uint32_t length;
	int status;

	UNUSED_PARAMETER(strict);

	/* The new length must be a whole number in uint32's range (and in int32's for now). */
	status = vm_to_uint32(realm, value, &length);
	if (status != 0)
		return status;
	status = vm_to_number(realm, value, &number);
	if (status != 0)
		return status;
	if ((double)length != number || length > 0x7fffffffU) {
		status = vm_throw_range_error(realm, "Invalid array length");
		return status;
	}

	/* The array grows or loses its elements past the length. */
	status = vm_array_set_length(realm->heap, array, length);
	if (status != 0)
		return status;

	/* Succeeded: the length is set. */
	return 0;
}

/* Refuses an assignment or a deletion: nothing in sloppy code, a TypeError in strict code. */
static int
access_refuse(
	struct vm_realm *realm,
	int strict,
	const char *message)
{
	int status;

	/* Sloppy code ignores it. */
	if (!strict)
		return 0;

	/* Strict code throws. */
	status = vm_throw_type_error(realm, message);
	return status;
}

/* Lists the enumerable string keys of an object and its chain, each once, the nearest first. */
static int
access_collect(
	struct vm_heap *heap,
	struct vm_object *object,
	struct wb_vector *keys)
{
	struct wb_vector seen;
	struct wb_vector own;
	struct vm_property property;
	vm_value key;
	size_t index;
	size_t seen_before;
	int is_symbol;
	int listed;
	int found;
	int status;

	/* Every key met so far (enumerable or not, since a non-enumerable one still shadows). */
	wb_vector_init(&seen, sizeof(vm_value));
	wb_vector_init(&own, sizeof(vm_value));
	status = 0;

	/* Each object of the chain, the nearest first. */
	for (;
	     status == 0 && object != NULL;
	     object = object->prototype) {
		wb_vector_clear(&own);
		status = vm_object_own_keys(heap, object, &own);
		if (status != 0)
			break;

		/* Each own key: symbols are never listed, and one met on a nearer object is shadowed. */
		seen_before = seen.count;
		for (index = 0; status == 0 && index < own.count; index++) {
			key = *(vm_value *)wb_vector_at(&own, index);
			is_symbol = access_is_symbol(key);
			if (is_symbol)
				continue;
			listed = access_key_listed(&seen, seen_before, key);
			if (listed)
				continue;

			/* The key is met; it is listed when enumerable. */
			status = wb_vector_push(&seen, &key);
			if (status != 0)
				break;
			found = vm_object_get_own(object, key, &property);
			if (found && (property.attributes & VM_PROPERTY_ENUMERABLE) != 0U)
				status = wb_vector_push(keys, &key);
		}
	}

	/* The working lists are no longer needed. */
	wb_vector_release(&seen);
	wb_vector_release(&own);
	if (status != 0)
		return status;

	/* Succeeded: the keys are listed. */
	return 0;
}

/* Tells whether a key is among the first count keys of a list. */
static int
access_key_listed(
	const struct wb_vector *keys,
	size_t count,
	vm_value key)
{
	vm_value listed;
	size_t index;

	/* Keys are indices, atoms or symbols, which compare by value. */
	for (index = 0; index < count; index++) {
		listed = *(vm_value *)wb_vector_at(keys, index);
		if (listed == key)
			return 1;
	}

	/* Not listed. */
	return 0;
}

/* Tells whether a key is a symbol. */
static int
access_is_symbol(
	vm_value value)
{
	struct vm_cell *cell;
	int is_cell;

	/* Only cells are symbols. */
	is_cell = vm_value_is_cell(value);
	if (!is_cell)
		return 0;

	/* A symbol cell. */
	cell = vm_value_as_cell(value);
	if (cell->type == &vm_symbol_type)
		return 1;

	/* Another cell. */
	return 0;
}
