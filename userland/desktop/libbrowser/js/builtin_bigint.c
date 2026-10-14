/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * BigInt (ws074-p230): the BigInt function (not a constructor), BigInt.asIntN
 * and BigInt.asUintN, and BigInt.prototype with toString, toLocaleString and
 * valueOf.  The arithmetic is in vm/bigint.c.
 */

#include "js/builtin.h"

#include <errno.h>
#include <math.h>
#include <string.h>

static int bigint_call(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int bigint_as_int_n(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int bigint_as_uint_n(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int bigint_to_string(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int bigint_to_locale_string(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int bigint_value_of(struct vm_realm *realm, vm_value this_value, const vm_value *args, unsigned count, vm_value *result);
static int bigint_this(struct vm_realm *realm, vm_value this_value, struct vm_bigint **big);
static int bigint_as_n(struct vm_realm *realm, const vm_value *args, unsigned count, int is_signed, vm_value *result);

/* Installs BigInt and its prototype. */
int
js_builtin_install_bigint(
	struct vm_realm *realm)
{
	struct vm_function *constructor;
	struct vm_object *prototype;
	int error;

	/* The prototype, rooted in the realm before the function that refers to it. */
	prototype = vm_object_create(realm->heap, realm->object_prototype);
	if (prototype == NULL)
		return ENOMEM;
	realm->intrinsics[VM_INTRINSIC_BIGINT_PROTOTYPE] = prototype;
	error = js_builtin_constructor(realm, "BigInt", 1, bigint_call, NULL, prototype, &constructor);
	if (error != 0)
		return error;

	/* The functions. */
	error = js_builtin_method(realm, &constructor->object, "asIntN", 2, bigint_as_int_n);
	if (error != 0)
		return error;
	error = js_builtin_method(realm, &constructor->object, "asUintN", 2, bigint_as_uint_n);
	if (error != 0)
		return error;
	error = js_builtin_method(realm, prototype, "toString", 0, bigint_to_string);
	if (error != 0)
		return error;
	error = js_builtin_method(realm, prototype, "toLocaleString", 0, bigint_to_locale_string);
	if (error != 0)
		return error;
	error = js_builtin_method(realm, prototype, "valueOf", 0, bigint_value_of);
	if (error != 0)
		return error;
	error = js_builtin_tag(realm, prototype, "BigInt");
	if (error != 0)
		return error;

	/* Succeeded: BigInt is installed. */
	return 0;
}

/* BigInt(value): a number that is an integer, a string, a boolean or a BigInt becomes a BigInt. */
static int
bigint_call(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct vm_string *string;
	struct wb_buffer text;
	vm_value primitive;
	double number;
	int status;

	UNUSED_PARAMETER(this_value);

	/* The value as a primitive. */
	status = vm_to_primitive(realm, js_argument(args, count, 0), VM_HINT_NUMBER, &primitive);
	if (status != 0)
		return status;

	/* A BigInt is itself. */
	if (vm_value_as_bigint(primitive) != NULL) {
		*result = primitive;
		return 0;
	}

	/* A number must be an integer. */
	if (vm_value_is_number(primitive)) {
		number = vm_value_as_number(primitive);
		if (number != number || isinf(number) || number != floor(number)) {
			status = vm_throw_range_error(realm, "The number cannot be converted to a BigInt because it is not an integer");
			return status;
		}
		status = vm_bigint_from_double(realm->heap, number, result);
		return status;
	}

	/* A boolean is 0 or 1. */
	if (primitive == VM_VALUE_TRUE || primitive == VM_VALUE_FALSE) {
		status = vm_bigint_from_int64(realm->heap, primitive == VM_VALUE_TRUE, result);
		return status;
	}

	/* A string is read as a numeral. */
	if (vm_value_is_string(primitive)) {
		string = (struct vm_string *)vm_value_as_cell(primitive);
		wb_buffer_init(&text);
		status = vm_string_to_utf8(string, &text);
		if (status == 0)
			status = vm_bigint_from_text(realm->heap, wb_buffer_string(&text), text.length, result);
		wb_buffer_release(&text);
		if (status == EINVAL) {
			status = vm_throw_error(realm, VM_ERROR_SYNTAX, "Cannot convert the string to a BigInt");
			return status;
		}
		return status;
	}

	/* undefined, null and symbols cannot. */
	status = vm_throw_type_error(realm, "Cannot convert the value to a BigInt");
	return status;
}

/* The shared body of asIntN and asUintN: (bits, bigint). */
static int
bigint_as_n(
	struct vm_realm *realm,
	const vm_value *args,
	unsigned count,
	int is_signed,
	vm_value *result)
{
	struct vm_bigint *big;
	vm_value argument;
	double bits;
	int status;

	/* The bit count as an integer index. */
	status = vm_to_number(realm, js_argument(args, count, 0), &bits);
	if (status != 0)
		return status;
	if (bits != bits)
		bits = 0.0;
	bits = trunc(bits);
	if (bits < 0.0 || bits > 9007199254740991.0) {
		status = vm_throw_range_error(realm, "Invalid value: not (convertible to) a safe integer");
		return status;
	}

	/* The BigInt (converted as BigInt() does). */
	argument = js_argument(args, count, 1);
	status = bigint_call(realm, VM_VALUE_UNDEFINED, &argument, 1, &argument);
	if (status != 0)
		return status;
	big = vm_value_as_bigint(argument);
	status = vm_bigint_as_n(realm, (uint64_t)bits, is_signed, big, result);
	return status;
}

static int
bigint_as_int_n(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	UNUSED_PARAMETER(this_value);
	return bigint_as_n(realm, args, count, 1, result);
}

static int
bigint_as_uint_n(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	UNUSED_PARAMETER(this_value);
	return bigint_as_n(realm, args, count, 0, result);
}

/* Finds the BigInt a method's this value is: a BigInt or a BigInt wrapper. */
static int
bigint_this(
	struct vm_realm *realm,
	vm_value this_value,
	struct vm_bigint **big)
{
	struct vm_object *object;
	int status;

	*big = vm_value_as_bigint(this_value);
	if (*big != NULL)
		return 0;
	if (vm_value_is_object(this_value)) {
		object = (struct vm_object *)vm_value_as_cell(this_value);
		if (object->kind == VM_KIND_BIGINT) {
			*big = vm_value_as_bigint(object->internal);
			if (*big != NULL)
				return 0;
		}
	}
	status = vm_throw_type_error(realm, "BigInt.prototype method called on a value that is not a BigInt");
	return status;
}

/* BigInt.prototype.toString(radix). */
static int
bigint_to_string(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct vm_bigint *big;
	struct vm_string *string;
	struct wb_buffer text;
	vm_value radix_value;
	double radix;
	int status;

	status = bigint_this(realm, this_value, &big);
	if (status != 0)
		return status;

	/* The radix, 10 by default. */
	radix = 10.0;
	radix_value = js_argument(args, count, 0);
	if (radix_value != VM_VALUE_UNDEFINED) {
		status = vm_to_number(realm, radix_value, &radix);
		if (status != 0)
			return status;
		radix = trunc(radix);
	}
	if (radix != radix || radix < 2.0 || radix > 36.0) {
		status = vm_throw_range_error(realm, "toString() radix must be between 2 and 36");
		return status;
	}

	/* The digits. */
	wb_buffer_init(&text);
	status = vm_bigint_to_text(big, (int)radix, &text);
	if (status != 0) {
		wb_buffer_release(&text);
		return status;
	}
	string = vm_string_from_utf8(realm->heap, wb_buffer_string(&text), text.length);
	wb_buffer_release(&text);
	if (string == NULL)
		return ENOMEM;
	*result = vm_value_cell(string);
	return 0;
}

/* BigInt.prototype.toLocaleString(): the decimal digits. */
static int
bigint_to_locale_string(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);
	return bigint_to_string(realm, this_value, NULL, 0, result);
}

/* BigInt.prototype.valueOf(). */
static int
bigint_value_of(
	struct vm_realm *realm,
	vm_value this_value,
	const vm_value *args,
	unsigned count,
	vm_value *result)
{
	struct vm_bigint *big;
	int status;

	UNUSED_PARAMETER(args);
	UNUSED_PARAMETER(count);
	status = bigint_this(realm, this_value, &big);
	if (status != 0)
		return status;
	*result = vm_value_cell(big);
	return 0;
}
