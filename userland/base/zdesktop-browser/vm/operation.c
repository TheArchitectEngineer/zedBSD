/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * JavaScript's operations on values that the interpreter's instructions
 * use: the conversions (ToBoolean, ToNumber, ToString, ToPropertyKey), the
 * strict equality, addition and the less-than relation, and getting and
 * putting a property of any value.
 *
 * The first pass covers primitives and plain objects.  An object's
 * conversion to a primitive (valueOf, toString, Symbol.toPrimitive) needs
 * the built-ins (ws074-p026) and is "NaN" or "[object Object]" until then;
 * a double's string is the shortest that reads back, in C's %g form, until
 * the Number-to-String algorithm arrives with them.  The errors are thrown
 * as strings until the Error objects exist.
 */

#include "vm/internal.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int operation_number_string(struct vm_realm *realm, double number, struct vm_string **string);
static double operation_parse_number(const struct vm_string *string);
static int operation_is_string(vm_value value);
static int operation_throw_text(struct vm_realm *realm, const char *text);

/*
 * Converts a value to a truth value (ToBoolean).
 */
int
vm_to_boolean(
	vm_value value)
{
	struct vm_string *string;
	double number;
	int is_number;
	int is_string;

	/* The false constants. */
	if (value == VM_VALUE_FALSE || value == VM_VALUE_NULL || value == VM_VALUE_UNDEFINED)
		return 0;
	if (value == VM_VALUE_TRUE)
		return 1;

	/* A number is false when zero or NaN. */
	is_number = vm_value_is_number(value);
	if (is_number) {
		number = vm_value_as_number(value);
		if (number == 0.0 || number != number)
			return 0;
		return 1;
	}

	/* A string is false when empty. */
	is_string = operation_is_string(value);
	if (is_string) {
		string = (struct vm_string *)vm_value_as_cell(value);
		if (string->length == 0)
			return 0;
		return 1;
	}

	/* Objects and symbols are true. */
	return 1;
}

/*
 * Converts a value to a number (ToNumber).
 */
int
vm_to_number(
	struct vm_realm *realm,
	vm_value value,
	double *number)
{
	struct vm_cell *cell;
	int is_number;
	int is_cell;

	/* A number is itself. */
	is_number = vm_value_is_number(value);
	if (is_number) {
		*number = vm_value_as_number(value);
		return 0;
	}

	/* true is one. */
	if (value == VM_VALUE_TRUE) {
		*number = 1.0;
		return 0;
	}

	/* false and null are zero. */
	if (value == VM_VALUE_FALSE || value == VM_VALUE_NULL) {
		*number = 0.0;
		return 0;
	}

	/* undefined (and any other constant) is NaN. */
	is_cell = vm_value_is_cell(value);
	if (!is_cell) {
		*number = NAN;
		return 0;
	}

	/* A string is read as a numeral. */
	cell = vm_value_as_cell(value);
	if (cell->type == &vm_string_type) {
		*number = operation_parse_number((const struct vm_string *)cell);
		return 0;
	}

	/* A symbol cannot become a number. */
	if (cell->type == &vm_symbol_type) {
		(void)vm_throw_type_error(realm, "Cannot convert a Symbol value to a number");
		return VM_THROWN;
	}

	/* An object is NaN until ToPrimitive arrives with the built-ins. */
	*number = NAN;
	return 0;
}

/*
 * Converts a value to a string (ToString).
 */
int
vm_to_string(
	struct vm_realm *realm,
	vm_value value,
	struct vm_string **string)
{
	struct vm_cell *cell;
	const char *text;
	int is_number;
	int error;

	/* A number's numeral. */
	is_number = vm_value_is_number(value);
	if (is_number) {
		error = operation_number_string(realm, vm_value_as_number(value), string);
		return error;
	}

	/* The constants' names. */
	text = NULL;
	if (value == VM_VALUE_TRUE)
		text = "true";
	if (value == VM_VALUE_FALSE)
		text = "false";
	if (value == VM_VALUE_NULL)
		text = "null";
	if (value == VM_VALUE_UNDEFINED || value == VM_VALUE_EMPTY)
		text = "undefined";

	/* A string is itself. */
	if (text == NULL) {
		cell = vm_value_as_cell(value);
		if (cell->type == &vm_string_type) {
			*string = (struct vm_string *)cell;
			return 0;
		}

		/* A symbol cannot become a string implicitly. */
		if (cell->type == &vm_symbol_type) {
			(void)vm_throw_type_error(realm, "Cannot convert a Symbol value to a string");
			return VM_THROWN;
		}

		/* An object, until ToPrimitive arrives with the built-ins. */
		text = "[object Object]";
	}

	/* The name as a string. */
	*string = vm_string_from_utf8(realm->heap, text, strlen(text));
	if (*string == NULL)
		return ENOMEM;

	/* Succeeded: the string. */
	return 0;
}

/*
 * Converts a value to a property key (ToPropertyKey): an index, an atom
 * or a symbol.
 */
int
vm_to_key(
	struct vm_realm *realm,
	vm_value value,
	vm_value *key)
{
	struct vm_string *string;
	struct vm_cell *cell;
	double number;
	int32_t whole;
	int is_number;
	int is_cell;
	int error;

	/* A number that is an index below 2^31 is that index. */
	is_number = vm_value_is_number(value);
	if (is_number) {
		number = vm_value_as_number(value);
		if (number >= 0.0 && number <= 2147483647.0) {
			whole = (int32_t)number;
			if ((double)whole == number && !(whole == 0 && 1.0 / number < 0.0)) {
				*key = vm_value_int32(whole);
				return 0;
			}
		}
	}

	/* A symbol is itself. */
	is_cell = vm_value_is_cell(value);
	if (is_cell) {
		cell = vm_value_as_cell(value);
		if (cell->type == &vm_symbol_type) {
			*key = value;
			return 0;
		}
	}

	/* Anything else through its string. */
	error = vm_to_string(realm, value, &string);
	if (error != 0)
		return error;
	error = vm_key_from_string(realm->heap, string, key);
	if (error != 0)
		return error;

	/* Succeeded: the key. */
	return 0;
}

/*
 * Tells whether two values are strictly equal (===).
 */
int
vm_strict_equals(
	vm_value left,
	vm_value right)
{
	struct vm_string *left_string;
	struct vm_string *right_string;
	double left_value;
	double right_value;
	int left_number;
	int right_number;
	int left_is_string;
	int right_is_string;

	/* Numbers compare by value (NaN is unequal to itself, +0 equals -0). */
	left_number = vm_value_is_number(left);
	right_number = vm_value_is_number(right);
	if (left_number && right_number) {
		left_value = vm_value_as_number(left);
		right_value = vm_value_as_number(right);
		if (left_value == right_value)
			return 1;
		return 0;
	}

	/* Strings compare by their characters. */
	left_is_string = operation_is_string(left);
	right_is_string = operation_is_string(right);
	if (left_is_string && right_is_string) {
		left_string = (struct vm_string *)vm_value_as_cell(left);
		right_string = (struct vm_string *)vm_value_as_cell(right);
		return vm_string_equal(left_string, right_string);
	}

	/* Everything else is the same value or not. */
	if (left == right)
		return 1;

	/* Different values. */
	return 0;
}

/*
 * Adds two values (+): strings join when either is a string, numbers add
 * otherwise.
 */
int
vm_add(
	struct vm_realm *realm,
	vm_value left,
	vm_value right,
	vm_value *result)
{
	struct vm_string *left_string;
	struct vm_string *right_string;
	struct vm_string *joined;
	double left_number;
	double right_number;
	int64_t sum;
	int left_int32;
	int right_int32;
	int is_string;
	int error;

	/* Two int32s add without leaving int32 most of the time. */
	left_int32 = vm_value_is_int32(left);
	right_int32 = vm_value_is_int32(right);
	if (left_int32 && right_int32) {
		sum = (int64_t)vm_value_as_int32(left) + (int64_t)vm_value_as_int32(right);
		*result = vm_value_number((double)sum);
		return 0;
	}

	/* A string on either side joins the two as strings. */
	is_string = operation_is_string(left);
	if (!is_string)
		is_string = operation_is_string(right);
	if (is_string) {
		error = vm_to_string(realm, left, &left_string);
		if (error != 0)
			return error;
		error = vm_to_string(realm, right, &right_string);
		if (error != 0)
			return error;
		joined = vm_string_concat(realm->heap, left_string, right_string);
		if (joined == NULL)
			return ENOMEM;
		*result = vm_value_cell(joined);
		return 0;
	}

	/* Otherwise the numbers. */
	error = vm_to_number(realm, left, &left_number);
	if (error != 0)
		return error;
	error = vm_to_number(realm, right, &right_number);
	if (error != 0)
		return error;

	/* Succeeded: the sum. */
	*result = vm_value_number(left_number + right_number);
	return 0;
}

/*
 * Compares two values (<): strings by their code units, anything else as
 * numbers (NaN compares false).
 */
int
vm_less(
	struct vm_realm *realm,
	vm_value left,
	vm_value right,
	vm_value *result)
{
	double left_number;
	double right_number;
	int left_is_string;
	int right_is_string;
	int order;
	int error;

	/* Two strings compare by their code units. */
	left_is_string = operation_is_string(left);
	right_is_string = operation_is_string(right);
	if (left_is_string && right_is_string) {
		order = vm_string_compare((struct vm_string *)vm_value_as_cell(left), (struct vm_string *)vm_value_as_cell(right));
		*result = vm_value_boolean(order < 0);
		return 0;
	}

	/* Anything else as numbers. */
	error = vm_to_number(realm, left, &left_number);
	if (error != 0)
		return error;
	error = vm_to_number(realm, right, &right_number);
	if (error != 0)
		return error;

	/* Succeeded: the comparison (false when either is NaN). */
	*result = vm_value_boolean(left_number < right_number);
	return 0;
}

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
	struct vm_string *string;
	struct vm_string *character;
	struct vm_cell *cell;
	vm_value length_key;
	uint32_t index;
	uint16_t unit;
	int is_index;
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
		string = (struct vm_string *)cell;
		length_key = vm_key_from_ascii(realm->heap, "length");
		if (key == length_key) {
			*result = vm_value_int32((int32_t)string->length);
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
		}

		/* Any other key of a string reads undefined. */
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
 * Puts a property of any value: an object's by assignment (calling an
 * accessor's setter); putting on undefined or null throws; on other
 * primitives nothing happens.  A refused assignment is ignored (sloppy
 * mode) until strict mode arrives with the compiler.
 */
int
vm_put(
	struct vm_realm *realm,
	vm_value base,
	vm_value key,
	vm_value value)
{
	struct vm_property property;
	struct vm_accessor *accessor;
	struct vm_object *object;
	struct vm_cell *cell;
	vm_value ignored;
	int is_cell;
	int found;
	int done;
	int status;

	/* undefined and null have no properties. */
	if (base == VM_VALUE_UNDEFINED || base == VM_VALUE_NULL) {
		status = vm_throw_type_error(realm, "Cannot set properties of undefined or null");
		return status;
	}

	/* A primitive takes no property. */
	is_cell = vm_value_is_cell(base);
	if (!is_cell)
		return 0;
	cell = vm_value_as_cell(base);
	if (cell->type == &vm_string_type || cell->type == &vm_symbol_type)
		return 0;

	/* An accessor on the chain: its setter is called with the value. */
	object = (struct vm_object *)cell;
	found = vm_object_find(object, key, &property);
	if (found && (property.attributes & VM_PROPERTY_ACCESSOR) != 0U) {
		accessor = (struct vm_accessor *)vm_value_as_cell(*property.value);
		if (accessor->setter == VM_VALUE_UNDEFINED)
			return 0;
		status = vm_call(realm, accessor->setter, base, &value, 1, &ignored);
		return status;
	}

	/* Otherwise the ordinary assignment. */
	status = vm_object_set(realm->heap, object, key, value, &done);
	if (status != 0)
		return status;

	/* Succeeded: the value is put (or refused in silence). */
	return 0;
}

/*
 * Throws a TypeError with a message (a string until Error objects exist).
 */
int
vm_throw_type_error(
	struct vm_realm *realm,
	const char *message)
{
	char text[256];
	int status;

	/* The error's name and message. */
	snprintf(text, sizeof(text), "TypeError: %s", message);
	status = operation_throw_text(realm, text);

	/* Reports the throw. */
	return status;
}

/*
 * Throws a RangeError with a message (a string until Error objects exist).
 */
int
vm_throw_range_error(
	struct vm_realm *realm,
	const char *message)
{
	char text[256];
	int status;

	/* The error's name and message. */
	snprintf(text, sizeof(text), "RangeError: %s", message);
	status = operation_throw_text(realm, text);

	/* Reports the throw. */
	return status;
}

/* Makes a number's string: the int32 digits, or the shortest %g form that reads back. */
static int
operation_number_string(
	struct vm_realm *realm,
	double number,
	struct vm_string **string)
{
	char text[40];
	double read_back;
	int precision;
	int whole;

	/* Whether the number is a whole number in int32's range. */
	whole = 0;
	if (number >= -2147483648.0 && number <= 2147483647.0 && (double)(int32_t)number == number)
		whole = 1;

	/* The special numbers, a whole number, or the fewest %g digits that read back as the same double. */
	if (number != number) {
		snprintf(text, sizeof(text), "NaN");
	} else if (number == INFINITY) {
		snprintf(text, sizeof(text), "Infinity");
	} else if (number == -INFINITY) {
		snprintf(text, sizeof(text), "-Infinity");
	} else if (number == 0.0) {
		snprintf(text, sizeof(text), "0");
	} else if (whole) {
		snprintf(text, sizeof(text), "%d", (int)(int32_t)number);
	} else {
		for (precision = 1; precision <= 17; precision++) {
			snprintf(text, sizeof(text), "%.*g", precision, number);
			read_back = strtod(text, NULL);
			if (read_back == number)
				break;
		}
	}

	/* The digits as a string. */
	*string = vm_string_from_utf8(realm->heap, text, strlen(text));
	if (*string == NULL)
		return ENOMEM;

	/* Succeeded: the numeral. */
	return 0;
}

/* Reads a string as a numeral (StringToNumber's common forms): blank is 0, anything unreadable NaN. */
static double
operation_parse_number(
	const struct vm_string *string)
{
	char text[64];
	char *end;
	double number;
	uint32_t index;
	uint16_t unit;
	size_t length;

	/* Only short ASCII numerals are read; anything else is NaN. */
	if (string->length >= sizeof(text))
		return NAN;
	length = 0;
	for (index = 0; index < string->length; index++) {
		unit = vm_string_at(string, index);
		if (unit >= 0x80U)
			return NAN;
		text[length] = (char)unit;
		length++;
	}

	/* The numeral ends there. */
	text[length] = '\0';

	/* Blank is zero; the numeral must be all there is besides blanks. */
	number = strtod(text, &end);
	if (end == text) {
		while (*end == ' ' || *end == '\t' || *end == '\n')
			end++;
		if (*end == '\0')
			return 0.0;
		return NAN;
	}
	while (*end == ' ' || *end == '\t' || *end == '\n')
		end++;
	if (*end != '\0')
		return NAN;

	/* The number read. */
	return number;
}

/* Tells whether a value is a string. */
static int
operation_is_string(
	vm_value value)
{
	struct vm_cell *cell;
	int is_cell;

	/* Only cells are strings. */
	is_cell = vm_value_is_cell(value);
	if (!is_cell)
		return 0;

	/* A string cell. */
	cell = vm_value_as_cell(value);
	if (cell->type == &vm_string_type)
		return 1;

	/* Another cell. */
	return 0;
}

/* Throws a text as a string value. */
static int
operation_throw_text(
	struct vm_realm *realm,
	const char *text)
{
	struct vm_string *string;
	int status;

	/* The text as a string; without memory, the exception is undefined. */
	string = vm_string_from_utf8(realm->heap, text, strlen(text));
	if (string == NULL) {
		status = vm_throw(realm, VM_VALUE_UNDEFINED);
		return status;
	}

	/* Throws it. */
	status = vm_throw(realm, vm_value_cell(string));

	/* Reports the throw. */
	return status;
}
