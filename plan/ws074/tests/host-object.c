/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws074-p022: the host test of the engine's values, objects, shapes,
 * arrays, native functions and realm.
 *
 *   host-object
 *
 * Prints one line per failed check and a summary.
 */

#include "vm/vm.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;
static int checks;

static void check(int condition, const char *what);
static void test_values(void);
static void test_properties(struct vm_realm *realm);
static void test_arrays(struct vm_realm *realm);
static void test_keys(struct vm_realm *realm);
static void test_functions(struct vm_realm *realm);
static void test_collection(struct vm_heap *heap, struct vm_realm *realm);
static int native_sum(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int native_throw(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static vm_value key(struct vm_realm *realm, const char *name);

int
main(void)
{
	struct vm_heap *heap;
	struct vm_realm *realm;
	int error;

	test_values();

	error = vm_heap_create(&heap, 0);
	check(error == 0, "heap: create");
	if (error != 0)
		return 1;
	vm_heap_set_stack_base(heap, __builtin_frame_address(0));
	error = vm_realm_create(heap, &realm);
	check(error == 0, "realm: create");
	if (error != 0)
		return 1;

	test_properties(realm);
	test_arrays(realm);
	test_keys(realm);
	test_functions(realm);
	test_collection(heap, realm);

	vm_realm_destroy(realm);
	vm_heap_destroy(heap);
	printf("host-object: %d checks, %d failed\n", checks, failures);
	return failures != 0;
}

static void
check(
	int condition,
	const char *what)
{
	checks++;
	if (condition)
		return;
	failures++;
	printf("FAIL %s\n", what);
}

/* The NaN-boxing: every kind of value round-trips and is told apart. */
static void
test_values(void)
{
	static const double doubles[] = { 0.5, -0.0, 1e300, -1e-300, 4294967296.0, 2147483648.0, -2147483649.0 };
	vm_value value;
	vm_value nan_a;
	vm_value nan_b;
	double number;
	size_t index;
	int32_t ints[5];
	int item;

	ints[0] = 0;
	ints[1] = 1;
	ints[2] = -1;
	ints[3] = 2147483647;
	ints[4] = -2147483647 - 1;
	for (item = 0; item < 5; item++) {
		value = vm_value_int32(ints[item]);
		check(vm_value_is_int32(value) && vm_value_is_number(value) && !vm_value_is_double(value), "value: int32 is an int32");
		check(!vm_value_is_cell(value) && !vm_value_is_boolean(value), "value: int32 is not a cell or a boolean");
		check(vm_value_as_int32(value) == ints[item], "value: int32 round-trips");
	}
	for (index = 0; index < sizeof(doubles) / sizeof(doubles[0]); index++) {
		value = vm_value_double(doubles[index]);
		number = vm_value_as_double(value);
		check(vm_value_is_double(value) && !vm_value_is_int32(value) && !vm_value_is_cell(value), "value: double is a double");
		check(memcmp(&number, &doubles[index], sizeof(number)) == 0, "value: double round-trips bit for bit");
	}
	value = vm_value_double(INFINITY);
	check(vm_value_is_double(value) && vm_value_as_double(value) == INFINITY, "value: infinity");
	nan_a = vm_value_double(NAN);
	nan_b = vm_value_double(-NAN);
	check(nan_a == nan_b && vm_value_is_double(nan_a) && isnan(vm_value_as_double(nan_a)), "value: NaN is canonical");
	check(vm_value_is_int32(vm_value_number(42.0)), "value: number 42 is an int32");
	check(vm_value_is_double(vm_value_number(-0.0)), "value: number -0 is a double");
	check(vm_value_is_double(vm_value_number(0.25)), "value: number 0.25 is a double");
	check(vm_value_is_double(vm_value_number(3e9)), "value: number 3e9 is a double");
	check(vm_value_is_boolean(VM_VALUE_TRUE) && vm_value_is_boolean(VM_VALUE_FALSE), "value: booleans");
	check(!vm_value_is_boolean(VM_VALUE_NULL) && !vm_value_is_boolean(VM_VALUE_UNDEFINED), "value: null, undefined are not booleans");
	check(!vm_value_is_cell(VM_VALUE_NULL) && !vm_value_is_cell(VM_VALUE_UNDEFINED) && !vm_value_is_cell(VM_VALUE_EMPTY) &&
	    !vm_value_is_cell(VM_VALUE_TRUE), "value: constants are not cells");
	check(!vm_value_is_number(VM_VALUE_NULL) && !vm_value_is_number(VM_VALUE_TRUE), "value: constants are not numbers");
	value = vm_value_cell((void *)(uintptr_t)0x7fff12345670ULL);
	check(vm_value_is_cell(value) && !vm_value_is_number(value), "value: a pointer is a cell");
}

/* Named properties: define, get, set, attributes, delete, prototypes. */
static void
test_properties(
	struct vm_realm *realm)
{
	struct vm_heap *heap;
	struct vm_object *object;
	struct vm_object *child;
	struct vm_property property;
	struct vm_accessor *accessor;
	vm_value value;
	int found;
	int done;
	int deleted;
	int error;
	int index;
	char name[16];

	heap = realm->heap;
	object = vm_object_create(heap, realm->object_prototype);
	check(object != NULL, "object: create");
	error = vm_object_define(heap, object, key(realm, "a"), vm_value_int32(1), VM_PROPERTY_DEFAULT);
	check(error == 0, "object: define a");
	error = vm_object_define(heap, object, key(realm, "b"), vm_value_int32(2), VM_PROPERTY_DEFAULT);
	vm_object_get(object, key(realm, "a"), &value);
	check(value == vm_value_int32(1), "object: get a");
	vm_object_get(object, key(realm, "b"), &value);
	check(value == vm_value_int32(2), "object: get b");
	vm_object_get(object, key(realm, "zzz"), &value);
	check(value == VM_VALUE_UNDEFINED, "object: a missing property is undefined");

	/* Two objects built the same way share the shape. */
	child = vm_object_create(heap, realm->object_prototype);
	vm_object_define(heap, child, key(realm, "a"), vm_value_int32(5), VM_PROPERTY_DEFAULT);
	vm_object_define(heap, child, key(realm, "b"), vm_value_int32(6), VM_PROPERTY_DEFAULT);
	check(child->shape == object->shape, "shape: same order, same shape");

	/* Assignment writes a writable property and makes a new one. */
	error = vm_object_set(heap, object, key(realm, "a"), vm_value_int32(10), &done);
	vm_object_get(object, key(realm, "a"), &value);
	check(error == 0 && done && value == vm_value_int32(10), "set: writes a");
	error = vm_object_set(heap, object, key(realm, "c"), vm_value_int32(3), &done);
	found = vm_object_get_own(object, key(realm, "c"), &property);
	check(error == 0 && done && found && property.attributes == VM_PROPERTY_DEFAULT, "set: makes c");

	/* A read-only property refuses assignment, but define still changes it. */
	vm_object_define(heap, object, key(realm, "ro"), vm_value_int32(7), VM_PROPERTY_ENUMERABLE);
	vm_object_set(heap, object, key(realm, "ro"), vm_value_int32(8), &done);
	vm_object_get(object, key(realm, "ro"), &value);
	check(!done && value == vm_value_int32(7), "set: read-only refuses");
	vm_object_define(heap, object, key(realm, "ro"), vm_value_int32(9), VM_PROPERTY_ENUMERABLE);
	vm_object_get(object, key(realm, "ro"), &value);
	check(value == vm_value_int32(9), "define: changes a read-only value");

	/* Attributes change without moving the other values. */
	vm_object_define(heap, object, key(realm, "b"), vm_value_int32(20), VM_PROPERTY_WRITABLE);
	found = vm_object_get_own(object, key(realm, "b"), &property);
	check(found && property.attributes == VM_PROPERTY_WRITABLE && *property.value == vm_value_int32(20), "define: new attributes");
	vm_object_get(object, key(realm, "a"), &value);
	check(value == vm_value_int32(10), "define: other values stay");

	/* Delete: configurable goes, non-configurable stays, the rest keep their values. */
	vm_object_delete(heap, object, key(realm, "a"), &deleted);
	found = vm_object_get_own(object, key(realm, "a"), &property);
	check(deleted && !found, "delete: a is gone");
	vm_object_get(object, key(realm, "c"), &value);
	check(value == vm_value_int32(3), "delete: c keeps its value");
	vm_object_delete(heap, object, key(realm, "b"), &deleted);
	found = vm_object_get_own(object, key(realm, "b"), &property);
	check(!deleted && found, "delete: non-configurable b stays");
	vm_object_delete(heap, object, key(realm, "nothing"), &deleted);
	check(deleted, "delete: a missing property counts as deleted");

	/* Prototype chains: reads go through, writes shadow, a read-only prototype property blocks. */
	child = vm_object_create(heap, object);
	vm_object_get(child, key(realm, "c"), &value);
	check(value == vm_value_int32(3), "prototype: read through");
	vm_object_set(heap, child, key(realm, "c"), vm_value_int32(30), &done);
	vm_object_get(child, key(realm, "c"), &value);
	check(done && value == vm_value_int32(30), "prototype: set shadows");
	vm_object_get(object, key(realm, "c"), &value);
	check(value == vm_value_int32(3), "prototype: the prototype keeps its value");
	vm_object_set(heap, child, key(realm, "ro"), vm_value_int32(1), &done);
	found = vm_object_get_own(child, key(realm, "ro"), &property);
	check(!done && !found, "prototype: a read-only inherited property blocks the set");

	/* Accessors: the property holds the pair, and a set does not write over it. */
	accessor = vm_accessor_create(heap, VM_VALUE_UNDEFINED, VM_VALUE_UNDEFINED);
	vm_object_define(heap, object, key(realm, "acc"), vm_value_cell(accessor), VM_PROPERTY_ACCESSOR | VM_PROPERTY_CONFIGURABLE);
	vm_object_get(child, key(realm, "acc"), &value);
	check(value == vm_value_cell(accessor), "accessor: get reports the pair");
	vm_object_set(heap, child, key(realm, "acc"), vm_value_int32(1), &done);
	check(!done, "accessor: set leaves it to the caller");

	/* Not extensible: no new properties, existing ones still written. */
	object->flags |= VM_OBJECT_NOT_EXTENSIBLE;
	vm_object_set(heap, object, key(realm, "new"), vm_value_int32(1), &done);
	check(!done, "extensible: no new property");
	vm_object_set(heap, object, key(realm, "c"), vm_value_int32(4), &done);
	check(done, "extensible: existing property written");

	/* Many properties. */
	child = vm_object_create(heap, NULL);
	for (index = 0; index < 200; index++) {
		snprintf(name, sizeof(name), "p%d", index);
		vm_object_define(heap, child, key(realm, name), vm_value_int32(index), VM_PROPERTY_DEFAULT);
	}
	found = 1;
	for (index = 0; index < 200; index++) {
		snprintf(name, sizeof(name), "p%d", index);
		vm_object_get(child, key(realm, name), &value);
		if (value != vm_value_int32(index))
			found = 0;
	}
	check(found && vm_shape_count(child->shape) == 200, "object: 200 properties");
}

/* Elements and arrays: dense, holes, sparse, length. */
static void
test_arrays(
	struct vm_realm *realm)
{
	struct vm_heap *heap;
	struct vm_object *array;
	struct vm_property property;
	vm_value value;
	int found;
	int deleted;
	int index;
	int ok;

	heap = realm->heap;
	array = vm_array_create(heap, realm->array_prototype);
	check(array != NULL && array->prototype == realm->array_prototype, "array: create");
	vm_object_get(array, key(realm, "length"), &value);
	check(value == vm_value_int32(0), "array: length 0");
	for (index = 0; index < 100; index++)
		vm_object_define(heap, array, vm_value_int32(index), vm_value_int32(index * 2), VM_PROPERTY_DEFAULT);
	vm_object_get(array, key(realm, "length"), &value);
	check(array->length == 100 && value == vm_value_int32(100), "array: length follows 100 elements");
	ok = 1;
	for (index = 0; index < 100; index++) {
		vm_object_get(array, vm_value_int32(index), &value);
		if (value != vm_value_int32(index * 2))
			ok = 0;
	}
	check(ok, "array: elements read back");

	/* A hole. */
	vm_object_delete(heap, array, vm_value_int32(50), &deleted);
	found = vm_object_get_own(array, vm_value_int32(50), &property);
	check(deleted && !found && array->length == 100, "array: delete makes a hole, length stays");
	vm_object_get(array, vm_value_int32(50), &value);
	check(value == VM_VALUE_UNDEFINED, "array: a hole reads undefined");

	/* Far past the end: kept in a slot, length follows. */
	vm_object_define(heap, array, vm_value_int32(1000000), vm_value_int32(7), VM_PROPERTY_DEFAULT);
	vm_object_get(array, vm_value_int32(1000000), &value);
	check(value == vm_value_int32(7) && array->length == 1000001 && array->element_capacity < 10000, "array: sparse index in a slot");

	/* An index with other attributes. */
	vm_object_define(heap, array, vm_value_int32(3), vm_value_int32(33), VM_PROPERTY_ENUMERABLE);
	found = vm_object_get_own(array, vm_value_int32(3), &property);
	check(found && property.attributes == VM_PROPERTY_ENUMERABLE && *property.value == vm_value_int32(33), "array: index with attributes");

	/* Setting the length shorter deletes past it. */
	vm_object_define(heap, array, key(realm, "length"), vm_value_int32(10), VM_PROPERTY_WRITABLE);
	vm_object_get(array, key(realm, "length"), &value);
	found = vm_object_get_own(array, vm_value_int32(1000000), &property);
	check(array->length == 10 && value == vm_value_int32(10) && !found, "array: shorter length deletes");
	found = vm_object_get_own(array, vm_value_int32(20), &property);
	check(!found, "array: element past the length is gone");
	found = vm_object_get_own(array, vm_value_int32(9), &property);
	check(found, "array: element before the length stays");
}

/* Keys: canonical index strings, and the order of own keys. */
static void
test_keys(
	struct vm_realm *realm)
{
	struct vm_heap *heap;
	struct vm_object *object;
	struct vm_symbol *symbol;
	struct vm_string *string;
	struct wb_vector keys;
	vm_value value;
	vm_value *list;
	int error;

	heap = realm->heap;
	string = vm_string_from_utf8(heap, "17", 2);
	vm_key_from_string(heap, string, &value);
	check(value == vm_value_int32(17), "key: \"17\" is the index 17");
	string = vm_string_from_utf8(heap, "017", 3);
	vm_key_from_string(heap, string, &value);
	check(vm_value_is_cell(value), "key: \"017\" is a string");
	string = vm_string_from_utf8(heap, "4294967294", 10);
	vm_key_from_string(heap, string, &value);
	check(vm_value_is_cell(value), "key: an index past int32 is a string for now");
	string = vm_string_from_utf8(heap, "abc", 3);
	vm_key_from_string(heap, string, &value);
	check(value == key(realm, "abc"), "key: a string key is its atom");

	/* Order: indices ascending, strings as added, symbols as added. */
	object = vm_object_create(heap, NULL);
	symbol = vm_symbol_create(heap, VM_VALUE_UNDEFINED);
	vm_object_define(heap, object, key(realm, "z"), VM_VALUE_TRUE, VM_PROPERTY_DEFAULT);
	vm_object_define(heap, object, vm_value_cell(symbol), VM_VALUE_TRUE, VM_PROPERTY_DEFAULT);
	vm_object_define(heap, object, vm_value_int32(5), VM_VALUE_TRUE, VM_PROPERTY_DEFAULT);
	vm_object_define(heap, object, key(realm, "a"), VM_VALUE_TRUE, VM_PROPERTY_DEFAULT);
	vm_object_define(heap, object, vm_value_int32(1), VM_VALUE_TRUE, VM_PROPERTY_DEFAULT);
	vm_object_define(heap, object, vm_value_int32(100000), VM_VALUE_TRUE, VM_PROPERTY_ENUMERABLE);
	wb_vector_init(&keys, sizeof(vm_value));
	error = vm_object_own_keys(heap, object, &keys);
	list = keys.items;
	check(error == 0 && keys.count == 6, "keys: six keys");
	if (keys.count == 6) {
		check(list[0] == vm_value_int32(1) && list[1] == vm_value_int32(5) && list[2] == vm_value_int32(100000),
		    "keys: indices ascending");
		check(list[3] == key(realm, "z") && list[4] == key(realm, "a"), "keys: strings in the order added");
		check(list[5] == vm_value_cell(symbol), "keys: the symbol last");
	}
	wb_vector_release(&keys);
}

/* Native functions: name, length, call, throw. */
static void
test_functions(
	struct vm_realm *realm)
{
	struct vm_function *sum;
	struct vm_function *thrower;
	struct vm_property property;
	vm_value args[3];
	vm_value value;
	int status;
	int found;

	sum = vm_function_create_native(realm, "sum", 2, native_sum);
	check(sum != NULL && sum->object.prototype == realm->function_prototype, "function: create");
	found = vm_object_get_own(&sum->object, key(realm, "length"), &property);
	check(found && *property.value == vm_value_int32(2) && property.attributes == VM_PROPERTY_CONFIGURABLE, "function: length");
	vm_object_get(&sum->object, key(realm, "name"), &value);
	check(vm_value_is_cell(value) && vm_string_equal_ascii((struct vm_string *)vm_value_as_cell(value), "sum"), "function: name");
	check(vm_value_is_callable(vm_value_cell(sum)) && !vm_value_is_callable(vm_value_cell(realm->global)), "function: callable");
	args[0] = vm_value_int32(2);
	args[1] = vm_value_double(0.5);
	args[2] = vm_value_int32(40);
	status = vm_call(realm, vm_value_cell(sum), VM_VALUE_UNDEFINED, args, 3, &value);
	check(status == 0 && value == vm_value_number(42.5), "function: call sums");
	thrower = vm_function_create_native(realm, "thrower", 0, native_throw);
	status = vm_call(realm, vm_value_cell(thrower), VM_VALUE_UNDEFINED, NULL, 0, &value);
	check(status == VM_THROWN && realm->exception == vm_value_int32(99), "function: throw");
	status = vm_call(realm, vm_value_int32(1), VM_VALUE_UNDEFINED, NULL, 0, &value);
	check(status == VM_THROWN, "function: calling a number throws");
	check(vm_value_is_callable(vm_value_cell(realm->function_prototype)), "realm: Function.prototype is callable");
	vm_object_get(realm->global, key(realm, "globalThis"), &value);
	check(value == vm_value_cell(realm->global), "realm: globalThis");
	check(realm->array_prototype->prototype == realm->object_prototype &&
	    realm->function_prototype->prototype == realm->object_prototype && realm->object_prototype->prototype == NULL,
	    "realm: the prototype chains");
}

/* Objects reachable from the realm survive a collection; unreachable ones are freed. */
static void
test_collection(
	struct vm_heap *heap,
	struct vm_realm *realm)
{
	struct vm_heap_stats before;
	struct vm_heap_stats after;
	struct vm_object *kept;
	struct vm_object *array;
	vm_value value;
	int index;
	int ok;

	kept = vm_object_create(heap, realm->object_prototype);
	vm_object_define(heap, realm->global, key(realm, "kept"), vm_value_cell(kept), VM_PROPERTY_DEFAULT);
	array = vm_array_create(heap, realm->array_prototype);
	vm_object_define(heap, kept, key(realm, "list"), vm_value_cell(array), VM_PROPERTY_DEFAULT);
	for (index = 0; index < 50; index++) {
		struct vm_object *inner;

		inner = vm_object_create(heap, NULL);
		vm_object_define(heap, inner, key(realm, "n"), vm_value_int32(index), VM_PROPERTY_DEFAULT);
		vm_object_define(heap, array, vm_value_int32(index), vm_value_cell(inner), VM_PROPERTY_DEFAULT);
	}
	kept = NULL;
	array = NULL;
	for (index = 0; index < 1000; index++)
		(void)vm_object_create(heap, NULL);
	vm_heap_stats(heap, &before);
	vm_heap_collect(heap);
	vm_heap_stats(heap, &after);
	check(after.live_cells < before.live_cells, "collect: garbage is freed");

	vm_object_get(realm->global, key(realm, "kept"), &value);
	kept = (struct vm_object *)vm_value_as_cell(value);
	vm_object_get(kept, key(realm, "list"), &value);
	array = (struct vm_object *)vm_value_as_cell(value);
	ok = array->length == 50;
	for (index = 0; ok && index < 50; index++) {
		vm_object_get(array, vm_value_int32(index), &value);
		vm_object_get((struct vm_object *)vm_value_as_cell(value), key(realm, "n"), &value);
		if (value != vm_value_int32(index))
			ok = 0;
	}
	check(ok, "collect: objects reachable from the global survive");
	printf("host-object: collection freed %zu cells, %zu live\n", before.live_cells - after.live_cells, after.live_cells);
}

static int
native_sum(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	double total;
	unsigned index;

	(void)realm;
	(void)this_value;
	total = 0;
	for (index = 0; index < count; index++)
		total += vm_value_as_number(args[index]);
	*result = vm_value_number(total);
	return 0;
}

static int
native_throw(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	(void)this_value;
	(void)args;
	(void)count;
	(void)result;
	return vm_throw(realm, vm_value_int32(99));
}

static vm_value
key(
	struct vm_realm *realm,
	const char *name)
{
	return vm_key_from_ascii(realm->heap, name);
}
