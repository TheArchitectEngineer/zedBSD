/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The AML operators that compute values (ACPI 6.5 section 19.6), and the
 * implicit and explicit conversions between integers, strings and buffers
 * (section 19.3.5).
 */

#include <kern/kcrt.h>
#include <uapi/errno.h>

#include "aml-internal.h"
#include "aml-os.h"

/*
 * The comparison codes of Match.
 */
#define MATCH_TRUE	0U
#define MATCH_EQUAL	1U
#define MATCH_LESS_EQUAL	2U
#define MATCH_LESS	3U
#define MATCH_GREATER_EQUAL	4U
#define MATCH_GREATER	5U

/*
 * The end tag of a resource template: its tag byte, and its length with
 * the checksum byte.
 */
#define RESOURCE_END_TAG	0x79U
#define RESOURCE_END_LENGTH	2U

/*
 * How a buffer or an integer is written as a string.
 */
enum string_form {
	STRING_IMPLICIT = 0,
	STRING_HEX = 1,
	STRING_DECIMAL = 2
};

/*
 * The operands of one Match: the package, the two comparisons with their
 * objects, and the index the search starts at.  It holds a reference to
 * each object until match_release().
 */
struct match_request {
	struct drv_acpi_object *package;
	struct drv_acpi_object *first;
	struct drv_acpi_object *second;
	uint64_t first_operation;
	uint64_t second_operation;
	uint64_t start;
};

static int op_binary(struct drv_acpi_eval *eval, unsigned opcode, struct drv_acpi_object **result);
static int op_divide(struct drv_acpi_eval *eval, struct drv_acpi_object **result);
static int op_unary(struct drv_acpi_eval *eval, unsigned opcode, struct drv_acpi_object **result);
static int op_step(struct drv_acpi_eval *eval, unsigned opcode, struct drv_acpi_object **result);
static int op_logical(struct drv_acpi_eval *eval, unsigned opcode, struct drv_acpi_object **result);
static int op_compare(struct drv_acpi_eval *eval, unsigned opcode, struct drv_acpi_object **result);
static int op_store(struct drv_acpi_eval *eval, bool copy, struct drv_acpi_object **result);
static int op_concat(struct drv_acpi_eval *eval, struct drv_acpi_object **result);
static int op_concat_res(struct drv_acpi_eval *eval, struct drv_acpi_object **result);
static int op_size_of(struct drv_acpi_eval *eval, struct drv_acpi_object **result);
static int op_index(struct drv_acpi_eval *eval, struct drv_acpi_object **result);
static int op_ref_of(struct drv_acpi_eval *eval, struct drv_acpi_object **result);
static int op_cond_ref_of(struct drv_acpi_eval *eval, struct drv_acpi_object **result);
static int op_deref_of(struct drv_acpi_eval *eval, struct drv_acpi_object **result);
static int op_object_type(struct drv_acpi_eval *eval, struct drv_acpi_object **result);
static int op_match(struct drv_acpi_eval *eval, struct drv_acpi_object **result);
static int op_mid(struct drv_acpi_eval *eval, struct drv_acpi_object **result);
static int op_to_conversion(struct drv_acpi_eval *eval, unsigned opcode, struct drv_acpi_object **result);
static int op_to_string(struct drv_acpi_eval *eval, struct drv_acpi_object **result);
static int op_notify(struct drv_acpi_eval *eval, struct drv_acpi_object **result);
static int op_time(struct drv_acpi_eval *eval, unsigned opcode, struct drv_acpi_object **result);
static int op_fatal(struct drv_acpi_eval *eval, struct drv_acpi_object **result);
static uint64_t highest_set_bit(uint64_t operand);
static uint64_t lowest_set_bit(uint64_t operand);
static uint64_t from_bcd(uint64_t operand);
static uint64_t to_bcd(uint64_t operand);
static int copy_into_node(struct drv_acpi_node *node, struct drv_acpi_object *value);
static int concat_integers(struct drv_acpi_object *left, struct drv_acpi_object *right, struct drv_acpi_object **result);
static int concat_strings(struct drv_acpi_object *left, struct drv_acpi_object *right, struct drv_acpi_object **result);
static int concat_buffers(struct drv_acpi_object *left, struct drv_acpi_object *right, struct drv_acpi_object **result);
static int cond_reference(struct drv_acpi_eval *eval, struct drv_acpi_object **result);
static int target_type(struct drv_acpi_eval *eval, struct drv_acpi_target *target, enum drv_acpi_type *kind);
static int match_operands(struct drv_acpi_eval *eval, struct match_request *request);
static int match_search(const struct match_request *request, uint64_t *answer);
static void match_release(struct match_request *request);
static int explicit_integer(struct drv_acpi_object *source, struct drv_acpi_object **result);
static int explicit_string(struct drv_acpi_object *source, enum string_form form, struct drv_acpi_object **result);
static int reference_object(struct drv_acpi_eval *eval, struct drv_acpi_object *reference, struct drv_acpi_object **result);
static int finish(struct drv_acpi_eval *eval, struct drv_acpi_object *value, struct drv_acpi_object **result);
static int finish_integer(struct drv_acpi_eval *eval, uint64_t value, struct drv_acpi_object **result);
static int target_node(struct drv_acpi_eval *eval, struct drv_acpi_target *target, struct drv_acpi_node **result);
static int target_object(struct drv_acpi_eval *eval, struct drv_acpi_target *target, struct drv_acpi_object **result);
static int compare_objects(struct drv_acpi_object *left, struct drv_acpi_object *right, int *order);
static int match_one(struct drv_acpi_object *element, uint64_t operation, struct drv_acpi_object *match, bool *hit);
static int integer_string(uint64_t value, enum string_form form, struct drv_acpi_object **result);
static int buffer_string(const uint8_t *bytes, size_t length, enum string_form form, struct drv_acpi_object **result);
static size_t byte_text(uint8_t byte, enum string_form form, char *text);
static int string_integer(const char *text, size_t length, bool explicit_form, uint64_t *value);
static size_t template_length(const struct drv_acpi_object *buffer);
static enum drv_acpi_type object_type_of(struct drv_acpi_object *object);
static uint64_t logical(bool value);

/*
 * Evaluates one operator whose opcode has been read.
 *
 * Reports ENOSYS for an opcode that is not an operator.
 */
int
drv_acpi_eval_operator(
	struct drv_acpi_eval *eval,
	unsigned opcode,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *object;
	int error;

	/* Chooses the operator by its opcode. */
	switch (opcode) {
	case DRV_ACPI_OP_ADD:
	case DRV_ACPI_OP_SUBTRACT:
	case DRV_ACPI_OP_MULTIPLY:
	case DRV_ACPI_OP_SHIFT_LEFT:
	case DRV_ACPI_OP_SHIFT_RIGHT:
	case DRV_ACPI_OP_AND:
	case DRV_ACPI_OP_NAND:
	case DRV_ACPI_OP_OR:
	case DRV_ACPI_OP_NOR:
	case DRV_ACPI_OP_XOR:
	case DRV_ACPI_OP_MOD:
		error = op_binary(eval, opcode, result);
		break;
	case DRV_ACPI_OP_DIVIDE:
		error = op_divide(eval, result);
		break;
	case DRV_ACPI_OP_NOT:
	case DRV_ACPI_OP_FIND_SET_LEFT_BIT:
	case DRV_ACPI_OP_FIND_SET_RIGHT_BIT:
	case DRV_ACPI_OP_FROM_BCD:
	case DRV_ACPI_OP_TO_BCD:
		error = op_unary(eval, opcode, result);
		break;
	case DRV_ACPI_OP_INCREMENT:
	case DRV_ACPI_OP_DECREMENT:
		error = op_step(eval, opcode, result);
		break;
	case DRV_ACPI_OP_LAND:
	case DRV_ACPI_OP_LOR:
	case DRV_ACPI_OP_LNOT:
		error = op_logical(eval, opcode, result);
		break;
	case DRV_ACPI_OP_LEQUAL:
	case DRV_ACPI_OP_LGREATER:
	case DRV_ACPI_OP_LLESS:
		error = op_compare(eval, opcode, result);
		break;
	case DRV_ACPI_OP_STORE:
		error = op_store(eval, false, result);
		break;
	case DRV_ACPI_OP_COPY_OBJECT:
		error = op_store(eval, true, result);
		break;
	case DRV_ACPI_OP_CONCAT:
		error = op_concat(eval, result);
		break;
	case DRV_ACPI_OP_CONCAT_RES:
		error = op_concat_res(eval, result);
		break;
	case DRV_ACPI_OP_SIZE_OF:
		error = op_size_of(eval, result);
		break;
	case DRV_ACPI_OP_INDEX:
		error = op_index(eval, result);
		break;
	case DRV_ACPI_OP_REF_OF:
		error = op_ref_of(eval, result);
		break;
	case DRV_ACPI_OP_COND_REF_OF:
		error = op_cond_ref_of(eval, result);
		break;
	case DRV_ACPI_OP_DEREF_OF:
		error = op_deref_of(eval, result);
		break;
	case DRV_ACPI_OP_OBJECT_TYPE:
		error = op_object_type(eval, result);
		break;
	case DRV_ACPI_OP_MATCH:
		error = op_match(eval, result);
		break;
	case DRV_ACPI_OP_MID:
		error = op_mid(eval, result);
		break;
	case DRV_ACPI_OP_TO_BUFFER:
	case DRV_ACPI_OP_TO_INTEGER:
	case DRV_ACPI_OP_TO_HEX_STRING:
	case DRV_ACPI_OP_TO_DECIMAL_STRING:
		error = op_to_conversion(eval, opcode, result);
		break;
	case DRV_ACPI_OP_TO_STRING:
		error = op_to_string(eval, result);
		break;
	case DRV_ACPI_OP_NOTIFY:
		error = op_notify(eval, result);
		break;
	case DRV_ACPI_OP_SLEEP:
	case DRV_ACPI_OP_STALL:
	case DRV_ACPI_OP_TIMER:
		error = op_time(eval, opcode, result);
		break;
	case DRV_ACPI_OP_FATAL:
		error = op_fatal(eval, result);
		break;
	case DRV_ACPI_OP_DEBUG:
		/* The debug object read as a value is itself. */
		object = drv_acpi_object_new(DRV_ACPI_TYPE_DEBUG);
		if (object == NULL)
			return ENOMEM;
		*result = object;
		return 0;
	case DRV_ACPI_OP_ACQUIRE:
	case DRV_ACPI_OP_RELEASE:
	case DRV_ACPI_OP_SIGNAL:
	case DRV_ACPI_OP_WAIT:
	case DRV_ACPI_OP_RESET:
		error = drv_acpi_sync_operator(eval, opcode, result);
		break;
	case DRV_ACPI_OP_LOAD:
	case DRV_ACPI_OP_LOAD_TABLE:
	case DRV_ACPI_OP_UNLOAD:
		error = drv_acpi_table_operator(eval, opcode, result);
		break;
	default:
		return ENOSYS;
	}

	/* Reports a failed operator. */
	if (error != 0)
		return error;

	/* Succeeded: the caller holds the value. */
	return 0;
}

/*
 * Converts an object to an integer by the implicit rules.
 *
 * A string is read as hexadecimal digits up to the first that is not one;
 * a buffer gives its first bytes, lowest first.
 */
int
drv_acpi_convert_integer(
	struct drv_acpi_object *object,
	uint64_t *value)
{
	uint64_t result;
	size_t length;
	size_t width;
	size_t index;
	int error;

	/* Chooses the conversion by the source's type. */
	switch (object->type) {
	case DRV_ACPI_TYPE_INTEGER:
		*value = object->value.integer;
		return 0;
	case DRV_ACPI_TYPE_STRING:
		error = string_integer(
			object->value.string.text,
			object->value.string.length,
			false,
			value);
		return error;
	case DRV_ACPI_TYPE_BUFFER:
		/* Takes as many bytes as an integer holds. */
		length = object->value.buffer.length;
		width = drv_acpi_integer_bytes();
		if (length > width)
			length = width;
		result = 0;
		for (index = 0; index < length; index++)
			result |= (uint64_t)object->value.buffer.bytes[index] << (index * 8U);
		*value = result;
		return 0;
	default:
		break;
	}

	/* Reports a source that has no integer value. */
	drv_acpi_os_log("ACPI: an object of type %u used as an integer\n", (unsigned)object->type);
	return EINVAL;
}

/*
 * Converts an object to a buffer by the implicit rules.
 *
 * An integer gives its bytes, lowest first; a string gives its characters
 * and its terminator.  A buffer is shared.
 */
int
drv_acpi_convert_buffer(
	struct drv_acpi_object *object,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *buffer;
	uint8_t bytes[8];
	uint64_t value;
	unsigned length;
	unsigned index;

	/* Chooses the conversion by the source's type. */
	switch (object->type) {
	case DRV_ACPI_TYPE_BUFFER:
		drv_acpi_object_ref(object);
		*result = object;
		return 0;
	case DRV_ACPI_TYPE_INTEGER:
		/* Lays the integer out lowest byte first. */
		value = object->value.integer;
		length = drv_acpi_integer_bytes();
		for (index = 0; index < length; index++)
			bytes[index] = (uint8_t)(value >> (index * 8U));
		buffer = drv_acpi_object_buffer_new(bytes, length);
		break;
	case DRV_ACPI_TYPE_STRING:
		buffer = drv_acpi_object_buffer_new(
			object->value.string.text,
			object->value.string.length + 1U);
		break;
	default:
		drv_acpi_os_log("ACPI: an object of type %u used as a buffer\n", (unsigned)object->type);
		return EINVAL;
	}

	/* Reports a buffer that could not be allocated. */
	if (buffer == NULL)
		return ENOMEM;

	/* Succeeded. */
	*result = buffer;
	return 0;
}

/*
 * Converts an object to a string by the implicit rules.
 *
 * An integer gives all its hexadecimal digits; a buffer gives each byte
 * as 0x and two hexadecimal digits, separated by spaces.  A string is
 * shared.
 */
int
drv_acpi_convert_string(
	struct drv_acpi_object *object,
	struct drv_acpi_object **result)
{
	int error;

	/* Chooses the conversion by the source's type. */
	switch (object->type) {
	case DRV_ACPI_TYPE_STRING:
		drv_acpi_object_ref(object);
		*result = object;
		return 0;
	case DRV_ACPI_TYPE_INTEGER:
		error = integer_string(object->value.integer, STRING_IMPLICIT, result);
		return error;
	case DRV_ACPI_TYPE_BUFFER:
		error = buffer_string(
			object->value.buffer.bytes,
			object->value.buffer.length,
			STRING_IMPLICIT,
			result);
		return error;
	default:
		break;
	}

	/* Reports a source that has no string value. */
	drv_acpi_os_log("ACPI: an object of type %u used as a string\n", (unsigned)object->type);
	return EINVAL;
}

/* Runs a two-operand integer operator with a target. */
static int
op_binary(
	struct drv_acpi_eval *eval,
	unsigned opcode,
	struct drv_acpi_object **result)
{
	uint64_t left;
	uint64_t right;
	uint64_t value;
	unsigned width;
	int error;

	/* Evaluates both operands as integers. */
	error = drv_acpi_eval_integer(eval, &left);
	if (error != 0)
		return error;
	error = drv_acpi_eval_integer(eval, &right);
	if (error != 0)
		return error;
	width = drv_acpi_integer_bytes() * 8U;

	/* Computes the operation. */
	switch (opcode) {
	case DRV_ACPI_OP_ADD:
		value = left + right;
		break;
	case DRV_ACPI_OP_SUBTRACT:
		value = left - right;
		break;
	case DRV_ACPI_OP_MULTIPLY:
		value = left * right;
		break;
	case DRV_ACPI_OP_SHIFT_LEFT:
		/* A shift by the width or more leaves nothing. */
		value = 0;
		if (right < width)
			value = left << right;
		break;
	case DRV_ACPI_OP_SHIFT_RIGHT:
		/* A shift by the width or more leaves nothing. */
		value = 0;
		if (right < width)
			value = (left & drv_acpi_integer_mask()) >> right;
		break;
	case DRV_ACPI_OP_AND:
		value = left & right;
		break;
	case DRV_ACPI_OP_NAND:
		value = ~(left & right);
		break;
	case DRV_ACPI_OP_OR:
		value = left | right;
		break;
	case DRV_ACPI_OP_NOR:
		value = ~(left | right);
		break;
	case DRV_ACPI_OP_XOR:
		value = left ^ right;
		break;
	default:
		/* Mod refuses a zero divisor. */
		if (right == 0) {
			drv_acpi_os_log("ACPI: Mod by zero\n");
			return EDOM;
		}

		/* The remainder. */
		value = left % right;
		break;
	}

	/* Stores and reports the result at the integer width. */
	error = finish_integer(eval, value & drv_acpi_integer_mask(), result);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Runs a Divide: remainder and quotient each go to their own target. */
static int
op_divide(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *remainder;
	struct drv_acpi_object *ignored;
	uint64_t dividend;
	uint64_t divisor;
	int error;

	/* Evaluates the dividend and the divisor. */
	error = drv_acpi_eval_integer(eval, &dividend);
	if (error != 0)
		return error;
	error = drv_acpi_eval_integer(eval, &divisor);
	if (error != 0)
		return error;

	/* Refuses a zero divisor. */
	if (divisor == 0) {
		drv_acpi_os_log("ACPI: Divide by zero\n");
		return EDOM;
	}

	/* Stores the remainder into the first target. */
	remainder = drv_acpi_object_integer_new(dividend % divisor);
	if (remainder == NULL)
		return ENOMEM;
	error = finish(eval, remainder, &ignored);
	if (error != 0)
		return error;
	drv_acpi_object_release(ignored);

	/* Stores and reports the quotient. */
	error = finish_integer(eval, dividend / divisor, result);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Runs Not, FindSetLeftBit, FindSetRightBit, FromBCD or ToBCD. */
static int
op_unary(
	struct drv_acpi_eval *eval,
	unsigned opcode,
	struct drv_acpi_object **result)
{
	uint64_t operand;
	uint64_t value;
	int error;

	/* Evaluates the operand as an integer. */
	error = drv_acpi_eval_integer(eval, &operand);
	if (error != 0)
		return error;
	operand &= drv_acpi_integer_mask();

	/* Computes the operation. */
	switch (opcode) {
	case DRV_ACPI_OP_NOT:
		value = ~operand;
		break;
	case DRV_ACPI_OP_FIND_SET_LEFT_BIT:
		value = highest_set_bit(operand);
		break;
	case DRV_ACPI_OP_FIND_SET_RIGHT_BIT:
		value = lowest_set_bit(operand);
		break;
	case DRV_ACPI_OP_FROM_BCD:
		value = from_bcd(operand);
		break;
	default:
		value = to_bcd(operand);
		break;
	}

	/* Stores and reports the result at the integer width. */
	error = finish_integer(eval, value & drv_acpi_integer_mask(), result);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Reports the one-based position of the highest set bit, or zero for none. */
static uint64_t
highest_set_bit(
	uint64_t operand)
{
	unsigned bit;

	/* Tries each bit from the top. */
	for (bit = 64; bit != 0; bit--) {
		/* Stops at the highest set bit. */
		if ((operand >> (bit - 1U)) & 1U)
			return bit;
	}

	/* Reports that no bit is set. */
	return 0;
}

/* Reports the one-based position of the lowest set bit, or zero for none. */
static uint64_t
lowest_set_bit(
	uint64_t operand)
{
	unsigned bit;

	/* Tries each bit from the bottom. */
	for (bit = 1; bit <= 64; bit++) {
		/* Stops at the lowest set bit. */
		if ((operand >> (bit - 1U)) & 1U)
			return bit;
	}

	/* Reports that no bit is set. */
	return 0;
}

/* Reads each nibble as a decimal digit, lowest first. */
static uint64_t
from_bcd(
	uint64_t operand)
{
	uint64_t value;
	uint64_t scale;

	/* Adds each digit at its decimal place. */
	value = 0;
	scale = 1;
	while (operand != 0) {
		value += (operand & 0x0fU) * scale;
		scale *= 10U;
		operand >>= 4;
	}

	/* Reports the number. */
	return value;
}

/* Writes each decimal digit as a nibble, lowest first. */
static uint64_t
to_bcd(
	uint64_t operand)
{
	uint64_t value;
	unsigned bit;

	/* Puts each digit in the next nibble while there is room. */
	value = 0;
	bit = 0;
	while (operand != 0 && bit < 64) {
		value |= (operand % 10U) << bit;
		operand /= 10U;
		bit += 4U;
	}

	/* Reports the packed digits. */
	return value;
}

/* Runs Increment or Decrement: the operand is read, changed and stored back. */
static int
op_step(
	struct drv_acpi_eval *eval,
	unsigned opcode,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *current;
	struct drv_acpi_object *changed;
	struct drv_acpi_target target;
	uint64_t value;
	int error;

	/* Parses the operand, which is also the target. */
	error = drv_acpi_parse_target(eval, &target);
	if (error != 0)
		return error;

	/* Reads its value. */
	error = target_object(eval, &target, &current);
	if (error != 0) {
		drv_acpi_target_release(&target);
		return error;
	}

	/* Converts the value to an integer. */
	error = drv_acpi_convert_integer(current, &value);
	drv_acpi_object_release(current);
	if (error != 0) {
		drv_acpi_target_release(&target);
		return error;
	}

	/* Moves the value by one. */
	if (opcode == DRV_ACPI_OP_INCREMENT) {
		value++;
	} else {
		value--;
	}

	/* Makes the new value at the integer width. */
	changed = drv_acpi_object_integer_new(value & drv_acpi_integer_mask());
	if (changed == NULL) {
		drv_acpi_target_release(&target);
		return ENOMEM;
	}

	/* Stores it back. */
	error = drv_acpi_store(eval, changed, &target);
	drv_acpi_target_release(&target);
	if (error != 0) {
		drv_acpi_object_release(changed);
		return error;
	}

	/* Succeeded: reports the new value. */
	*result = changed;
	return 0;
}

/* Runs LAnd, LOr or LNot. */
static int
op_logical(
	struct drv_acpi_eval *eval,
	unsigned opcode,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *object;
	uint64_t left;
	uint64_t right;
	bool value;
	int error;

	/* Evaluates the first operand. */
	error = drv_acpi_eval_integer(eval, &left);
	if (error != 0)
		return error;

	/* LNot has only the one. */
	if (opcode == DRV_ACPI_OP_LNOT) {
		value = false;
		if (left == 0)
			value = true;
	} else {
		/* Evaluates the second operand; AML does not short-circuit. */
		error = drv_acpi_eval_integer(eval, &right);
		if (error != 0)
			return error;

		/* Combines the two truths. */
		value = false;
		if (opcode == DRV_ACPI_OP_LAND) {
			if (left != 0 && right != 0)
				value = true;
		} else {
			if (left != 0 || right != 0)
				value = true;
		}
	}

	/* Makes the truth value. */
	object = drv_acpi_object_integer_new(logical(value));
	if (object == NULL)
		return ENOMEM;

	/* Succeeded. */
	*result = object;
	return 0;
}

/* Runs LEqual, LGreater or LLess. */
static int
op_compare(
	struct drv_acpi_eval *eval,
	unsigned opcode,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *left;
	struct drv_acpi_object *right;
	struct drv_acpi_object *object;
	bool value;
	int order;
	int error;

	/* Evaluates both operands as data. */
	error = drv_acpi_eval_data(eval, &left);
	if (error != 0)
		return error;
	error = drv_acpi_eval_data(eval, &right);
	if (error != 0) {
		drv_acpi_object_release(left);
		return error;
	}

	/* Compares them by the type of the first. */
	error = compare_objects(left, right, &order);
	drv_acpi_object_release(left);
	drv_acpi_object_release(right);
	if (error != 0)
		return error;

	/* Turns the order into the operator's truth. */
	value = false;
	if (opcode == DRV_ACPI_OP_LEQUAL && order == 0)
		value = true;
	if (opcode == DRV_ACPI_OP_LGREATER && order > 0)
		value = true;
	if (opcode == DRV_ACPI_OP_LLESS && order < 0)
		value = true;

	/* Makes the truth value. */
	object = drv_acpi_object_integer_new(logical(value));
	if (object == NULL)
		return ENOMEM;

	/* Succeeded. */
	*result = object;
	return 0;
}

/* Runs Store, or CopyObject when copy is set. */
static int
op_store(
	struct drv_acpi_eval *eval,
	bool copy,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *source;
	struct drv_acpi_target target;
	int error;

	/* Evaluates the source. */
	error = drv_acpi_eval_term_arg(eval, &source);
	if (error != 0)
		return error;

	/* Parses the destination. */
	error = drv_acpi_parse_target(eval, &target);
	if (error != 0) {
		drv_acpi_object_release(source);
		return error;
	}

	/* CopyObject to a named object replaces it; anything else is a Store. */
	if (copy && target.kind == DRV_ACPI_TARGET_NODE) {
		error = copy_into_node(target.node, source);
	} else {
		error = drv_acpi_store(eval, source, &target);
	}

	/* Lets go of the target and reports a failed store. */
	drv_acpi_target_release(&target);
	if (error != 0) {
		drv_acpi_object_release(source);
		return error;
	}

	/* Succeeded: the value of a Store is its source. */
	*result = source;
	return 0;
}

/* Replaces a named object with a copy of a value of any type, as CopyObject does. */
static int
copy_into_node(
	struct drv_acpi_node *node,
	struct drv_acpi_object *value)
{
	struct drv_acpi_object *duplicate;
	int error;

	/* Copies the value. */
	error = drv_acpi_object_copy(value, &duplicate);
	if (error != 0)
		return error;

	/* The node, or the one an alias stands for, takes the copy. */
	node = drv_acpi_ns_resolve_alias(node);
	drv_acpi_ns_attach(node, duplicate);
	drv_acpi_object_release(duplicate);

	/* Succeeded. */
	return 0;
}

/*
 * Runs a Concatenate.  The first operand's type decides the result: two
 * integers make a buffer of both, and strings and buffers are joined.
 */
static int
op_concat(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *left;
	struct drv_acpi_object *right;
	struct drv_acpi_object *joined;
	int error;

	/* Evaluates both operands as data. */
	error = drv_acpi_eval_data(eval, &left);
	if (error != 0)
		return error;
	error = drv_acpi_eval_data(eval, &right);
	if (error != 0) {
		drv_acpi_object_release(left);
		return error;
	}

	/* Joins them by the first operand's type. */
	switch (left->type) {
	case DRV_ACPI_TYPE_INTEGER:
		error = concat_integers(left, right, &joined);
		break;
	case DRV_ACPI_TYPE_STRING:
		error = concat_strings(left, right, &joined);
		break;
	case DRV_ACPI_TYPE_BUFFER:
		error = concat_buffers(left, right, &joined);
		break;
	default:
		drv_acpi_os_log("ACPI: Concatenate of an object of type %u\n", (unsigned)left->type);
		error = EINVAL;
		break;
	}

	/* Lets go of the operands and reports a failed join. */
	drv_acpi_object_release(left);
	drv_acpi_object_release(right);
	if (error != 0)
		return error;

	/* Stores and reports the joined value. */
	error = finish(eval, joined, result);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Joins two integers into a buffer, each laid out lowest byte first. */
static int
concat_integers(
	struct drv_acpi_object *left,
	struct drv_acpi_object *right,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *joined;
	uint64_t value;
	size_t width;
	size_t index;
	int error;

	/* Converts the second operand. */
	error = drv_acpi_convert_integer(right, &value);
	if (error != 0)
		return error;

	/* Allocates room for both. */
	width = drv_acpi_integer_bytes();
	joined = drv_acpi_object_buffer_new(NULL, width * 2U);
	if (joined == NULL)
		return ENOMEM;

	/* Lays out the first and then the second. */
	for (index = 0; index < width; index++) {
		joined->value.buffer.bytes[index] = (uint8_t)(left->value.integer >> (index * 8U));
		joined->value.buffer.bytes[width + index] = (uint8_t)(value >> (index * 8U));
	}

	/* Succeeded. */
	*result = joined;
	return 0;
}

/* Joins a string and the string form of another value. */
static int
concat_strings(
	struct drv_acpi_object *left,
	struct drv_acpi_object *right,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *converted;
	struct drv_acpi_object *joined;
	char *text;
	size_t length;
	int error;

	/* Converts the second operand. */
	error = drv_acpi_convert_string(right, &converted);
	if (error != 0)
		return error;

	/* Allocates room for both texts and the terminator. */
	length = left->value.string.length + converted->value.string.length;
	text = drv_acpi_os_alloc(length + 1U);
	if (text == NULL) {
		drv_acpi_object_release(converted);
		return ENOMEM;
	}

	/* Copies the first text and then the second. */
	kern_memcpy(text, left->value.string.text, left->value.string.length);
	kern_memcpy(text + left->value.string.length, converted->value.string.text, converted->value.string.length);
	text[length] = '\0';
	drv_acpi_object_release(converted);

	/* Makes the string from the joined text. */
	joined = drv_acpi_object_string_new_length(text, length);
	drv_acpi_os_free(text);
	if (joined == NULL)
		return ENOMEM;

	/* Succeeded. */
	*result = joined;
	return 0;
}

/* Joins a buffer and the buffer form of another value. */
static int
concat_buffers(
	struct drv_acpi_object *left,
	struct drv_acpi_object *right,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *converted;
	struct drv_acpi_object *joined;
	size_t left_length;
	size_t right_length;
	int error;

	/* Converts the second operand. */
	error = drv_acpi_convert_buffer(right, &converted);
	if (error != 0)
		return error;

	/* Allocates room for both. */
	left_length = left->value.buffer.length;
	right_length = converted->value.buffer.length;
	joined = drv_acpi_object_buffer_new(NULL, left_length + right_length);
	if (joined == NULL) {
		drv_acpi_object_release(converted);
		return ENOMEM;
	}

	/* Copies the first bytes and then the second. */
	if (left_length != 0)
		kern_memcpy(joined->value.buffer.bytes, left->value.buffer.bytes, left_length);
	if (right_length != 0)
		kern_memcpy(joined->value.buffer.bytes + left_length, converted->value.buffer.bytes, right_length);
	drv_acpi_object_release(converted);

	/* Succeeded. */
	*result = joined;
	return 0;
}

/*
 * Runs a ConcatenateResTemplate: the two resource templates without their
 * end tags, followed by one end tag with a zero checksum.
 */
static int
op_concat_res(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *left;
	struct drv_acpi_object *right;
	struct drv_acpi_object *joined;
	size_t left_length;
	size_t right_length;
	int error;

	/* Evaluates both templates. */
	error = drv_acpi_eval_data(eval, &left);
	if (error != 0)
		return error;
	error = drv_acpi_eval_data(eval, &right);
	if (error != 0) {
		drv_acpi_object_release(left);
		return error;
	}

	/* Refuses operands that are not buffers. */
	if (left->type != DRV_ACPI_TYPE_BUFFER || right->type != DRV_ACPI_TYPE_BUFFER) {
		drv_acpi_object_release(left);
		drv_acpi_object_release(right);
		return EINVAL;
	}

	/* Measures each template up to its end tag. */
	left_length = template_length(left);
	right_length = template_length(right);

	/* Allocates room for both and an end tag. */
	joined = drv_acpi_object_buffer_new(NULL, left_length + right_length + RESOURCE_END_LENGTH);
	if (joined == NULL) {
		drv_acpi_object_release(left);
		drv_acpi_object_release(right);
		return ENOMEM;
	}

	/* Joins them and ends the result with a fresh end tag. */
	if (left_length != 0)
		kern_memcpy(joined->value.buffer.bytes, left->value.buffer.bytes, left_length);
	if (right_length != 0)
		kern_memcpy(joined->value.buffer.bytes + left_length, right->value.buffer.bytes, right_length);
	joined->value.buffer.bytes[left_length + right_length] = RESOURCE_END_TAG;
	joined->value.buffer.bytes[left_length + right_length + 1U] = 0;
	drv_acpi_object_release(left);
	drv_acpi_object_release(right);

	/* Stores and reports the template. */
	error = finish(eval, joined, result);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Runs a SizeOf. */
static int
op_size_of(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *object;
	struct drv_acpi_object *size;
	struct drv_acpi_target target;
	uint64_t value;
	int error;

	/* Reads the object the operand names. */
	error = drv_acpi_parse_target(eval, &target);
	if (error != 0)
		return error;
	error = target_object(eval, &target, &object);
	drv_acpi_target_release(&target);
	if (error != 0)
		return error;

	/* Measures it by its type. */
	switch (object->type) {
	case DRV_ACPI_TYPE_STRING:
		value = object->value.string.length;
		break;
	case DRV_ACPI_TYPE_BUFFER:
		value = object->value.buffer.length;
		break;
	case DRV_ACPI_TYPE_PACKAGE:
		value = object->value.package.count;
		break;
	default:
		drv_acpi_os_log("ACPI: SizeOf an object of type %u\n", (unsigned)object->type);
		drv_acpi_object_release(object);
		return EINVAL;
	}

	/* The size is known; the object is no longer needed. */
	drv_acpi_object_release(object);

	/* Makes the size. */
	size = drv_acpi_object_integer_new(value);
	if (size == NULL)
		return ENOMEM;

	/* Succeeded. */
	*result = size;
	return 0;
}

/* Runs an Index: a reference to one element of a package, buffer or string. */
static int
op_index(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *source;
	struct drv_acpi_object *reference;
	uint64_t index;
	uint64_t count;
	int error;

	/* Evaluates the container and the index. */
	error = drv_acpi_eval_data(eval, &source);
	if (error != 0)
		return error;
	error = drv_acpi_eval_integer(eval, &index);
	if (error != 0) {
		drv_acpi_object_release(source);
		return error;
	}

	/* Measures the container. */
	switch (source->type) {
	case DRV_ACPI_TYPE_PACKAGE:
		count = source->value.package.count;
		break;
	case DRV_ACPI_TYPE_BUFFER:
		count = source->value.buffer.length;
		break;
	case DRV_ACPI_TYPE_STRING:
		count = source->value.string.length;
		break;
	default:
		drv_acpi_os_log("ACPI: Index into an object of type %u\n", (unsigned)source->type);
		drv_acpi_object_release(source);
		return EINVAL;
	}

	/* Refuses an index past the end. */
	if (index >= count) {
		drv_acpi_os_log("ACPI: Index %llu past the end (%llu)\n",
				(unsigned long long)index,
				(unsigned long long)count);
		drv_acpi_object_release(source);
		return EINVAL;
	}

	/* Makes the reference, which keeps the container alive. */
	reference = drv_acpi_object_reference_new(DRV_ACPI_REFERENCE_INDEX);
	if (reference == NULL) {
		drv_acpi_object_release(source);
		return ENOMEM;
	}

	/* Points it at the element; it takes over the reference to the container. */
	reference->value.reference.target = source;
	reference->value.reference.index = (uint32_t)index;

	/* Stores and reports the reference. */
	error = finish(eval, reference, result);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Runs a RefOf. */
static int
op_ref_of(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *reference;
	struct drv_acpi_object *object;
	struct drv_acpi_target target;
	int error;

	/* Parses what the reference is to. */
	error = drv_acpi_parse_target(eval, &target);
	if (error != 0)
		return error;

	/* A name gives a node reference. */
	if (target.kind == DRV_ACPI_TARGET_NODE) {
		reference = drv_acpi_object_reference_new(DRV_ACPI_REFERENCE_NODE);
		if (reference == NULL)
			return ENOMEM;
		reference->value.reference.node = target.node;
		*result = reference;
		return 0;
	}

	/* A reference target already is one. */
	if (target.kind == DRV_ACPI_TARGET_REFERENCE) {
		*result = target.reference;
		return 0;
	}

	/* A local or an argument gives a reference to what it holds. */
	error = target_object(eval, &target, &object);
	if (error != 0)
		return error;

	/* Passes on a reference the slot already holds. */
	if (object->type == DRV_ACPI_TYPE_REFERENCE) {
		*result = object;
		return 0;
	}

	/* Wraps any other object. */
	reference = drv_acpi_object_reference_new(DRV_ACPI_REFERENCE_OBJECT);
	if (reference == NULL) {
		drv_acpi_object_release(object);
		return ENOMEM;
	}

	/* It takes over the reference to the object. */
	reference->value.reference.target = object;

	/* Succeeded. */
	*result = reference;
	return 0;
}

/*
 * Runs a CondRefOf: stores a reference to the named object into the target
 * and reports true, or reports false when the name does not exist.
 */
static int
op_cond_ref_of(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *reference;
	struct drv_acpi_object *truth;
	struct drv_acpi_target target;
	bool named;
	int error;

	/* A name that does not exist is not an error here; anything else is RefOf of it. */
	named = drv_acpi_stream_at_name(eval);
	if (named) {
		error = cond_reference(eval, &reference);
	} else {
		error = op_ref_of(eval, &reference);
	}

	/* Reports an operand that could not be evaluated. */
	if (error != 0)
		return error;

	/* Parses the target. */
	error = drv_acpi_parse_target(eval, &target);
	if (error != 0) {
		drv_acpi_object_release(reference);
		return error;
	}

	/* Stores the reference only when the object exists. */
	error = 0;
	if (reference != NULL)
		error = drv_acpi_store(eval, reference, &target);
	drv_acpi_target_release(&target);
	if (error != 0) {
		drv_acpi_object_release(reference);
		return error;
	}

	/* Reports whether it exists. */
	truth = drv_acpi_object_integer_new(logical(reference != NULL));
	drv_acpi_object_release(reference);
	if (truth == NULL)
		return ENOMEM;

	/* Succeeded. */
	*result = truth;
	return 0;
}

/* Makes a reference to a named object, or reports NULL when the name does not exist. */
static int
cond_reference(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *reference;
	struct drv_acpi_name name;
	struct drv_acpi_node *node;
	int error;

	/* Reads the name. */
	*result = NULL;
	error = drv_acpi_stream_name(eval, &name);
	if (error != 0)
		return error;

	/* A name that does not resolve gives no reference. */
	error = drv_acpi_ns_lookup(eval->scope, &name, true, &node);
	if (error != 0)
		return 0;

	/* Makes the reference to the node, or to the one an alias stands for. */
	reference = drv_acpi_object_reference_new(DRV_ACPI_REFERENCE_NODE);
	if (reference == NULL)
		return ENOMEM;
	reference->value.reference.node = drv_acpi_ns_resolve_alias(node);

	/* Succeeded. */
	*result = reference;
	return 0;
}

/* Runs a DerefOf: the object a reference, or a path string, points at. */
static int
op_deref_of(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *operand;
	struct drv_acpi_object *value;
	struct drv_acpi_node *node;
	int error;

	/* Evaluates the operand. */
	error = drv_acpi_eval_term_arg(eval, &operand);
	if (error != 0)
		return error;

	/* A string is the path of a named object. */
	if (operand->type == DRV_ACPI_TYPE_STRING) {
		error = drv_acpi_lookup_path(eval->scope, operand->value.string.text, true, &node);
		drv_acpi_object_release(operand);
		if (error != 0)
			return error;

		/* Reads the named object. */
		error = drv_acpi_read_node(eval, node, result);
		return error;
	}

	/* Refuses anything else that is not a reference. */
	if (operand->type != DRV_ACPI_TYPE_REFERENCE) {
		drv_acpi_os_log("ACPI: DerefOf an object of type %u\n", (unsigned)operand->type);
		drv_acpi_object_release(operand);
		return EINVAL;
	}

	/* Follows the reference by its kind. */
	value = NULL;
	switch (operand->value.reference.kind) {
	case DRV_ACPI_REFERENCE_NODE:
		error = drv_acpi_read_node(eval, operand->value.reference.node, &value);
		break;
	case DRV_ACPI_REFERENCE_OBJECT:
		value = operand->value.reference.target;
		drv_acpi_object_ref(value);
		error = 0;
		break;
	case DRV_ACPI_REFERENCE_INDEX:
		error = drv_acpi_index_read(eval, operand, &value);
		break;
	default:
		error = EINVAL;
		break;
	}

	/* The reference is no longer needed. */
	drv_acpi_object_release(operand);
	if (error != 0)
		return error;

	/* Succeeded. */
	*result = value;
	return 0;
}

/* Runs an ObjectType. */
static int
op_object_type(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *type;
	struct drv_acpi_target target;
	enum drv_acpi_type kind;
	int error;

	/* Parses the operand without reading a field or invoking a method. */
	error = drv_acpi_parse_target(eval, &target);
	if (error != 0)
		return error;

	/* Finds the type of the object the operand stands for. */
	error = target_type(eval, &target, &kind);
	drv_acpi_target_release(&target);
	if (error != 0)
		return error;

	/* Makes the type number. */
	type = drv_acpi_object_integer_new((uint64_t)kind);
	if (type == NULL)
		return ENOMEM;

	/* Succeeded. */
	*result = type;
	return 0;
}

/* Finds the ObjectType of what a target stands for, looking through references. */
static int
target_type(
	struct drv_acpi_eval *eval,
	struct drv_acpi_target *target,
	enum drv_acpi_type *kind)
{
	struct drv_acpi_object *object;
	struct drv_acpi_node *node;
	int error;

	/* A name is the type of its object; the debug object is its own. */
	if (target->kind == DRV_ACPI_TARGET_NODE) {
		node = drv_acpi_ns_resolve_alias(target->node);
		*kind = object_type_of(node->object);
		return 0;
	} else if (target->kind == DRV_ACPI_TARGET_DEBUG) {
		*kind = DRV_ACPI_TYPE_DEBUG;
		return 0;
	}

	/* A local, an argument or a reference is looked into. */
	error = target_object(eval, target, &object);
	if (error != 0)
		return error;
	*kind = object_type_of(object);

	/* A node reference reports the type of the node's object. */
	node = drv_acpi_object_reference_node(object);
	if (node != NULL) {
		node = drv_acpi_ns_resolve_alias(node);
		*kind = object_type_of(node->object);
	}

	/* Succeeded. */
	drv_acpi_object_release(object);
	return 0;
}

/* Runs a Match: the index of the first package element that satisfies both tests. */
static int
op_match(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *found;
	struct match_request request;
	uint64_t answer;
	int error;

	/* Evaluates the package, the two tests and the start index. */
	kern_memset(&request, 0, sizeof(request));
	error = match_operands(eval, &request);
	if (error != 0) {
		match_release(&request);
		return error;
	}

	/* Searches the package. */
	error = match_search(&request, &answer);
	match_release(&request);
	if (error != 0)
		return error;

	/* Makes the answer: the index, or Ones when nothing matched. */
	found = drv_acpi_object_integer_new(answer);
	if (found == NULL)
		return ENOMEM;

	/* Succeeded. */
	*result = found;
	return 0;
}

/* Evaluates the operands of a Match. */
static int
match_operands(
	struct drv_acpi_eval *eval,
	struct match_request *request)
{
	int error;

	/* The package to search. */
	error = drv_acpi_eval_data(eval, &request->package);
	if (error != 0)
		return error;
	if (request->package->type != DRV_ACPI_TYPE_PACKAGE)
		return EINVAL;

	/* The first comparison and its object. */
	error = drv_acpi_stream_integer(eval, 1, &request->first_operation);
	if (error != 0)
		return error;
	error = drv_acpi_eval_data(eval, &request->first);
	if (error != 0)
		return error;

	/* The second comparison and its object. */
	error = drv_acpi_stream_integer(eval, 1, &request->second_operation);
	if (error != 0)
		return error;
	error = drv_acpi_eval_data(eval, &request->second);
	if (error != 0)
		return error;

	/* The index to start at. */
	error = drv_acpi_eval_integer(eval, &request->start);
	if (error != 0)
		return error;

	/* Refuses a comparison code Match does not define. */
	if (request->first_operation > MATCH_GREATER || request->second_operation > MATCH_GREATER)
		return EINVAL;

	/* Succeeded. */
	return 0;
}

/* Finds the first element from the start index that passes both tests. */
static int
match_search(
	const struct match_request *request,
	uint64_t *answer)
{
	struct drv_acpi_object *element;
	uint64_t index;
	bool first_hit;
	bool second_hit;
	int error;

	/* Tries each element in order. */
	*answer = drv_acpi_integer_mask();
	for (index = request->start; index < request->package->value.package.count; index++) {
		element = request->package->value.package.elements[index];

		/* Tests the first condition. */
		error = match_one(element, request->first_operation, request->first, &first_hit);
		if (error != 0)
			return error;
		if (!first_hit)
			continue;

		/* Tests the second condition. */
		error = match_one(element, request->second_operation, request->second, &second_hit);
		if (error != 0)
			return error;

		/* Reports the first element that passes both. */
		if (second_hit) {
			*answer = index;
			return 0;
		}
	}

	/* Succeeded: nothing matched, and the answer is Ones. */
	return 0;
}

/* Releases the objects a Match request holds. */
static void
match_release(
	struct match_request *request)
{
	/* Each may be NULL when evaluation stopped early. */
	drv_acpi_object_release(request->package);
	drv_acpi_object_release(request->first);
	drv_acpi_object_release(request->second);
}

/* Runs a Mid: part of a string or a buffer. */
static int
op_mid(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *source;
	struct drv_acpi_object *part;
	uint64_t index;
	uint64_t length;
	uint64_t available;
	int error;

	/* Evaluates the source, the index and the length. */
	error = drv_acpi_eval_data(eval, &source);
	if (error != 0)
		return error;
	error = drv_acpi_eval_integer(eval, &index);
	if (error == 0)
		error = drv_acpi_eval_integer(eval, &length);
	if (error != 0) {
		drv_acpi_object_release(source);
		return error;
	}

	/* Measures the source. */
	if (source->type == DRV_ACPI_TYPE_STRING) {
		available = source->value.string.length;
	} else if (source->type == DRV_ACPI_TYPE_BUFFER) {
		available = source->value.buffer.length;
	} else {
		drv_acpi_object_release(source);
		return EINVAL;
	}

	/* Keeps the part inside the source; past the end it is empty. */
	if (index > available)
		index = available;
	if (length > available - index)
		length = available - index;

	/* Copies the part. */
	if (source->type == DRV_ACPI_TYPE_STRING) {
		part = drv_acpi_object_string_new_length(source->value.string.text + index, (size_t)length);
	} else {
		part = drv_acpi_object_buffer_new(source->value.buffer.bytes + index, (size_t)length);
	}

	/* Lets go of the source and reports a part that could not be allocated. */
	drv_acpi_object_release(source);
	if (part == NULL)
		return ENOMEM;

	/* Stores and reports it. */
	error = finish(eval, part, result);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Runs ToBuffer, ToInteger, ToHexString or ToDecimalString. */
static int
op_to_conversion(
	struct drv_acpi_eval *eval,
	unsigned opcode,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *source;
	struct drv_acpi_object *converted;
	int error;

	/* Evaluates the operand as data. */
	error = drv_acpi_eval_data(eval, &source);
	if (error != 0)
		return error;

	/* Converts it by the operator. */
	switch (opcode) {
	case DRV_ACPI_OP_TO_BUFFER:
		error = drv_acpi_convert_buffer(source, &converted);
		break;
	case DRV_ACPI_OP_TO_INTEGER:
		error = explicit_integer(source, &converted);
		break;
	case DRV_ACPI_OP_TO_DECIMAL_STRING:
		error = explicit_string(source, STRING_DECIMAL, &converted);
		break;
	default:
		error = explicit_string(source, STRING_HEX, &converted);
		break;
	}

	/* Lets go of the source and reports a failed conversion. */
	drv_acpi_object_release(source);
	if (error != 0)
		return error;

	/* Stores and reports the result. */
	error = finish(eval, converted, result);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Converts a value to an integer as ToInteger does: a string is read in
 * decimal, or in hexadecimal after 0x.
 */
static int
explicit_integer(
	struct drv_acpi_object *source,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *integer;
	uint64_t value;
	int error;

	/* Reads a string explicitly and anything else by the implicit rules. */
	if (source->type == DRV_ACPI_TYPE_STRING) {
		error = string_integer(source->value.string.text, source->value.string.length, true, &value);
	} else {
		error = drv_acpi_convert_integer(source, &value);
	}

	/* Reports a value that could not be converted. */
	if (error != 0)
		return error;

	/* Makes the integer at the integer width. */
	integer = drv_acpi_object_integer_new(value & drv_acpi_integer_mask());
	if (integer == NULL)
		return ENOMEM;

	/* Succeeded. */
	*result = integer;
	return 0;
}

/*
 * Converts a value to a string as ToHexString or ToDecimalString does: a
 * string stays as it is, and integers and buffers are written out.
 */
static int
explicit_string(
	struct drv_acpi_object *source,
	enum string_form form,
	struct drv_acpi_object **result)
{
	int error;

	/* Chooses the conversion by the source's type. */
	switch (source->type) {
	case DRV_ACPI_TYPE_STRING:
		drv_acpi_object_ref(source);
		*result = source;
		return 0;
	case DRV_ACPI_TYPE_INTEGER:
		error = integer_string(source->value.integer, form, result);
		return error;
	case DRV_ACPI_TYPE_BUFFER:
		error = buffer_string(source->value.buffer.bytes, source->value.buffer.length, form, result);
		return error;
	default:
		break;
	}

	/* Reports a source that has no string form. */
	return EINVAL;
}

/* Runs a ToString: the characters of a buffer up to a NUL or a length. */
static int
op_to_string(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *source;
	struct drv_acpi_object *text;
	const uint8_t *terminator;
	uint64_t length;
	size_t available;
	int error;

	/* Evaluates the buffer and the length. */
	error = drv_acpi_eval_data(eval, &source);
	if (error != 0)
		return error;
	error = drv_acpi_eval_integer(eval, &length);
	if (error != 0) {
		drv_acpi_object_release(source);
		return error;
	}

	/* Refuses a source that is not a buffer. */
	if (source->type != DRV_ACPI_TYPE_BUFFER) {
		drv_acpi_object_release(source);
		return EINVAL;
	}

	/* Takes the bytes up to the first NUL, within the length. */
	available = source->value.buffer.length;
	if (length < available)
		available = (size_t)length;
	terminator = NULL;
	if (available != 0)
		terminator = kern_memchr(source->value.buffer.bytes, 0, available);
	if (terminator != NULL)
		available = (size_t)(terminator - source->value.buffer.bytes);
	text = drv_acpi_object_string_new_length((const char *)source->value.buffer.bytes, available);
	drv_acpi_object_release(source);
	if (text == NULL)
		return ENOMEM;

	/* Stores and reports the string. */
	error = finish(eval, text, result);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Runs a Notify. */
static int
op_notify(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object **result)
{
	struct drv_acpi_target target;
	struct drv_acpi_node *node;
	uint64_t value;
	int error;

	/* Resolves the object to notify and evaluates the value. */
	error = drv_acpi_parse_target(eval, &target);
	if (error != 0)
		return error;
	error = target_node(eval, &target, &node);
	drv_acpi_target_release(&target);
	if (error != 0)
		return error;
	error = drv_acpi_eval_integer(eval, &value);
	if (error != 0)
		return error;

	/* Hands the notification to the handlers. */
	error = drv_acpi_notify(node, (uint32_t)value);
	if (error != 0)
		return error;

	/* Succeeded: Notify has no value of its own. */
	*result = drv_acpi_object_integer_new(0);
	if (*result == NULL)
		return ENOMEM;
	return 0;
}

/* Runs Sleep, Stall or Timer. */
static int
op_time(
	struct drv_acpi_eval *eval,
	unsigned opcode,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *object;
	uint64_t value;
	int error;

	/* Timer reads the clock in 100-nanosecond units. */
	if (opcode == DRV_ACPI_OP_TIMER) {
		value = drv_acpi_os_timer();
	} else {
		/* Evaluates the duration. */
		error = drv_acpi_eval_integer(eval, &value);
		if (error != 0)
			return error;

		/* Sleeps in milliseconds or spins in microseconds. */
		if (opcode == DRV_ACPI_OP_SLEEP) {
			drv_acpi_sleep(value);
		} else {
			drv_acpi_os_stall(value);
		}

		/* Neither has a value of its own. */
		value = 0;
	}

	/* Makes the value. */
	object = drv_acpi_object_integer_new(value);
	if (object == NULL)
		return ENOMEM;

	/* Succeeded. */
	*result = object;
	return 0;
}

/* Runs a Fatal: firmware reports an error it cannot recover from. */
static int
op_fatal(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object **result)
{
	uint64_t type;
	uint64_t code;
	uint64_t argument;
	int error;

	/* Reads the type, the code and the argument. */
	error = drv_acpi_stream_integer(eval, 1, &type);
	if (error == 0)
		error = drv_acpi_stream_integer(eval, 4, &code);
	if (error == 0)
		error = drv_acpi_eval_integer(eval, &argument);
	if (error != 0)
		return error;

	/* Logs it; the system carries on as other operating systems do. */
	drv_acpi_os_log(
		"ACPI: Fatal type 0x%llx code 0x%llx argument 0x%llx\n",
		(unsigned long long)type,
		(unsigned long long)code,
		(unsigned long long)argument);

	/* Succeeded: Fatal has no value of its own. */
	*result = drv_acpi_object_integer_new(0);
	if (*result == NULL)
		return ENOMEM;
	return 0;
}

/* Stores a computed value into the target that follows and hands it back. */
static int
finish(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object *value,
	struct drv_acpi_object **result)
{
	struct drv_acpi_target target;
	int error;

	/* Parses the target. */
	error = drv_acpi_parse_target(eval, &target);
	if (error != 0) {
		drv_acpi_object_release(value);
		return error;
	}

	/* Stores the value into it. */
	error = drv_acpi_store(eval, value, &target);
	drv_acpi_target_release(&target);
	if (error != 0) {
		drv_acpi_object_release(value);
		return error;
	}

	/* Succeeded: the caller holds the value. */
	*result = value;
	return 0;
}

/* Makes an integer, stores it into the target that follows and hands it back. */
static int
finish_integer(
	struct drv_acpi_eval *eval,
	uint64_t value,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *object;
	int error;

	/* Makes the integer. */
	object = drv_acpi_object_integer_new(value);
	if (object == NULL)
		return ENOMEM;

	/* Stores and reports it. */
	error = finish(eval, object, result);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Finds the namespace node a SuperName stands for. */
static int
target_node(
	struct drv_acpi_eval *eval,
	struct drv_acpi_target *target,
	struct drv_acpi_node **result)
{
	struct drv_acpi_object *object;
	struct drv_acpi_node *node;
	int error;

	/* A name is the node. */
	if (target->kind == DRV_ACPI_TARGET_NODE) {
		*result = drv_acpi_ns_resolve_alias(target->node);
		return 0;
	}

	/* Anything else must hold a node reference. */
	error = target_object(eval, target, &object);
	if (error != 0)
		return error;
	node = drv_acpi_object_reference_node(object);
	drv_acpi_object_release(object);
	if (node == NULL)
		return EINVAL;

	/* Succeeded. */
	*result = drv_acpi_ns_resolve_alias(node);
	return 0;
}

/*
 * Reads the object a target stands for without converting it: a local's or
 * argument's object, a named object's value, or what a reference points at.
 */
static int
target_object(
	struct drv_acpi_eval *eval,
	struct drv_acpi_target *target,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *object;
	int error;

	/* Chooses the read by the kind of target. */
	switch (target->kind) {
	case DRV_ACPI_TARGET_LOCAL:
		object = eval->frame->locals[target->index];
		break;
	case DRV_ACPI_TARGET_ARGUMENT:
		object = eval->frame->arguments[target->index];
		break;
	case DRV_ACPI_TARGET_NODE:
		error = drv_acpi_read_node(eval, target->node, result);
		return error;
	case DRV_ACPI_TARGET_REFERENCE:
		error = reference_object(eval, target->reference, result);
		return error;
	default:
		return EINVAL;
	}

	/* A slot that was never set has no value. */
	if (object == NULL)
		return EINVAL;

	/* An argument that holds a node reference reads as the node's value. */
	if (target->kind == DRV_ACPI_TARGET_ARGUMENT &&
	    object->type == DRV_ACPI_TYPE_REFERENCE &&
	    object->value.reference.kind == DRV_ACPI_REFERENCE_NODE) {
		error = drv_acpi_read_node(eval, object->value.reference.node, result);
		return error;
	}

	/* Succeeded: the caller shares the object. */
	drv_acpi_object_ref(object);
	*result = object;
	return 0;
}

/* Reads what a reference points at. */
static int
reference_object(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object *reference,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *object;
	int error;

	/* Follows the reference by its kind. */
	switch (reference->value.reference.kind) {
	case DRV_ACPI_REFERENCE_NODE:
		error = drv_acpi_read_node(eval, reference->value.reference.node, result);
		return error;
	case DRV_ACPI_REFERENCE_INDEX:
		error = drv_acpi_index_read(eval, reference, result);
		return error;
	case DRV_ACPI_REFERENCE_OBJECT:
		object = reference->value.reference.target;
		break;
	default:
		return EINVAL;
	}

	/* Succeeded: the caller shares the object. */
	drv_acpi_object_ref(object);
	*result = object;
	return 0;
}

/*
 * Compares two objects by the type of the left one: integers by value,
 * strings and buffers byte by byte and then by length.
 */
static int
compare_objects(
	struct drv_acpi_object *left,
	struct drv_acpi_object *right,
	int *order)
{
	struct drv_acpi_object *converted;
	const uint8_t *left_bytes;
	const uint8_t *right_bytes;
	uint64_t value;
	size_t left_length;
	size_t right_length;
	size_t shorter;
	int compared;
	int error;

	/* Integers compare by value. */
	if (left->type == DRV_ACPI_TYPE_INTEGER) {
		error = drv_acpi_convert_integer(right, &value);
		if (error != 0)
			return error;

		/* Orders the two values. */
		*order = 0;
		if (left->value.integer < value)
			*order = -1;
		if (left->value.integer > value)
			*order = 1;
		return 0;
	}

	/* Strings and buffers compare their bytes. */
	if (left->type == DRV_ACPI_TYPE_STRING) {
		error = drv_acpi_convert_string(right, &converted);
		if (error != 0)
			return error;
		left_bytes = (const uint8_t *)left->value.string.text;
		left_length = left->value.string.length;
		right_bytes = (const uint8_t *)converted->value.string.text;
		right_length = converted->value.string.length;
	} else if (left->type == DRV_ACPI_TYPE_BUFFER) {
		error = drv_acpi_convert_buffer(right, &converted);
		if (error != 0)
			return error;
		left_bytes = left->value.buffer.bytes;
		left_length = left->value.buffer.length;
		right_bytes = converted->value.buffer.bytes;
		right_length = converted->value.buffer.length;
	} else {
		drv_acpi_os_log("ACPI: comparison of an object of type %u\n", (unsigned)left->type);
		return EINVAL;
	}

	/* Compares the common part, then the lengths. */
	shorter = left_length;
	if (right_length < shorter)
		shorter = right_length;
	compared = 0;
	if (shorter != 0)
		compared = kern_memcmp(left_bytes, right_bytes, shorter);
	drv_acpi_object_release(converted);

	/* Orders by the first differing byte, or by length when there is none. */
	*order = 0;
	if (compared < 0) {
		*order = -1;
	} else if (compared > 0) {
		*order = 1;
	} else if (left_length < right_length) {
		*order = -1;
	} else if (left_length > right_length) {
		*order = 1;
	}

	/* Succeeded. */
	return 0;
}

/* Tests one package element against one Match condition. */
static int
match_one(
	struct drv_acpi_object *element,
	uint64_t operation,
	struct drv_acpi_object *match,
	bool *hit)
{
	int order;
	int error;

	/* The always-true test needs no comparison. */
	*hit = false;
	if (operation == MATCH_TRUE) {
		*hit = true;
		return 0;
	}

	/* Only data elements can be compared; others never match. */
	if (element == NULL)
		return 0;
	if (element->type != DRV_ACPI_TYPE_INTEGER &&
	    element->type != DRV_ACPI_TYPE_STRING &&
	    element->type != DRV_ACPI_TYPE_BUFFER)
		return 0;

	/* Compares the element against the match object. */
	error = compare_objects(element, match, &order);
	if (error != 0)
		return 0;

	/* Applies the comparison code. */
	switch (operation) {
	case MATCH_EQUAL:
		if (order == 0)
			*hit = true;
		break;
	case MATCH_LESS_EQUAL:
		if (order <= 0)
			*hit = true;
		break;
	case MATCH_LESS:
		if (order < 0)
			*hit = true;
		break;
	case MATCH_GREATER_EQUAL:
		if (order >= 0)
			*hit = true;
		break;
	default:
		if (order > 0)
			*hit = true;
		break;
	}

	/* Succeeded. */
	return 0;
}

/*
 * Writes an integer as a string: all its hexadecimal digits for the
 * implicit form, 0x and the digits without leading zeros for ToHexString,
 * or decimal digits.
 */
static int
integer_string(
	uint64_t value,
	enum string_form form,
	struct drv_acpi_object **result)
{
	static const char digits[] = "0123456789ABCDEF";
	struct drv_acpi_object *object;
	char text[24];
	char reversed[24];
	size_t length;
	size_t count;
	unsigned width;

	/* Writes the digits lowest first. */
	count = 0;
	if (form == STRING_DECIMAL) {
		do {
			reversed[count] = digits[value % 10U];
			count++;
			value /= 10U;
		} while (value != 0);
	} else if (form == STRING_IMPLICIT) {
		/* Every digit of the integer, leading zeros included. */
		width = drv_acpi_integer_bytes() * 2U;
		while (count < width) {
			reversed[count] = digits[value & 0x0fU];
			count++;
			value >>= 4;
		}
	} else {
		do {
			reversed[count] = digits[value & 0x0fU];
			count++;
			value >>= 4;
		} while (value != 0);
	}

	/* Puts them in reading order behind any prefix. */
	length = 0;
	if (form == STRING_HEX) {
		text[0] = '0';
		text[1] = 'x';
		length = 2;
	}
	while (count != 0) {
		count--;
		text[length] = reversed[count];
		length++;
	}

	/* Makes the string. */
	object = drv_acpi_object_string_new_length(text, length);
	if (object == NULL)
		return ENOMEM;

	/* Succeeded. */
	*result = object;
	return 0;
}

/*
 * Writes a buffer as a string: 0x-prefixed bytes separated by spaces for
 * the implicit form and by commas for ToHexString, and decimal bytes
 * separated by commas.
 */
static int
buffer_string(
	const uint8_t *bytes,
	size_t length,
	enum string_form form,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *object;
	char *text;
	size_t used;
	size_t index;

	/* Allocates room for the longest form, five characters a byte. */
	text = drv_acpi_os_alloc(length * 5U + 1U);
	if (text == NULL)
		return ENOMEM;

	/* Writes each byte after the separator from the one before. */
	used = 0;
	for (index = 0; index < length; index++) {
		/* The implicit form separates with spaces, the others with commas. */
		if (index != 0 && form == STRING_IMPLICIT) {
			text[used] = ' ';
			used++;
		} else if (index != 0) {
			text[used] = ',';
			used++;
		}

		/* Writes the byte. */
		used += byte_text(bytes[index], form, text + used);
	}

	/* Makes the string. */
	object = drv_acpi_object_string_new_length(text, used);
	drv_acpi_os_free(text);
	if (object == NULL)
		return ENOMEM;

	/* Succeeded. */
	*result = object;
	return 0;
}

/* Writes one byte in decimal or in hexadecimal and reports how many characters it took. */
static size_t
byte_text(
	uint8_t byte,
	enum string_form form,
	char *text)
{
	static const char digits[] = "0123456789ABCDEF";
	size_t used;

	/* Decimal is written without leading zeros. */
	used = 0;
	if (form == STRING_DECIMAL) {
		/* The hundreds, when there are any. */
		if (byte >= 100) {
			text[used] = digits[byte / 100U];
			used++;
		}

		/* The tens, when the number has them. */
		if (byte >= 10) {
			text[used] = digits[(byte / 10U) % 10U];
			used++;
		}

		/* The units. */
		text[used] = digits[byte % 10U];
		used++;
		return used;
	}

	/* Hexadecimal bytes are written with 0x in front, implicit or not. */
	if (form != STRING_DECIMAL) {
		text[used] = '0';
		text[used + 1U] = 'x';
		used += 2U;
	}

	/* Hexadecimal is always two digits. */
	text[used] = digits[byte >> 4];
	text[used + 1U] = digits[byte & 0x0fU];
	used += 2U;

	/* Reports the length. */
	return used;
}

/*
 * Reads an integer from text.  The implicit form reads hexadecimal digits;
 * the explicit form of ToInteger reads decimal, or hexadecimal after 0x.
 * Both stop at the first character that is not a digit.
 */
static int
string_integer(
	const char *text,
	size_t length,
	bool explicit_form,
	uint64_t *value)
{
	uint64_t result;
	unsigned base;
	unsigned digit;
	size_t index;
	char character;

	/* Chooses the base and skips any prefix. */
	base = 16;
	index = 0;
	if (explicit_form) {
		/* Skips leading white space. */
		while (index < length && (text[index] == ' ' || text[index] == '\t'))
			index++;

		/* Takes 0x as a hexadecimal prefix; anything else is decimal. */
		base = 10;
		if (index + 1U < length &&
		    text[index] == '0' &&
		    (text[index + 1U] == 'x' || text[index + 1U] == 'X')) {
			base = 16;
			index += 2U;
		}
	}

	/* Accumulates the digits until one is not a digit of the base. */
	result = 0;
	for (; index < length; index++) {
		character = text[index];

		/* Decodes the character as a digit. */
		if (character >= '0' && character <= '9') {
			digit = (unsigned)(character - '0');
		} else if (character >= 'a' && character <= 'f') {
			digit = (unsigned)(character - 'a') + 10U;
		} else if (character >= 'A' && character <= 'F') {
			digit = (unsigned)(character - 'A') + 10U;
		} else {
			break;
		}

		/* Stops at a digit the base does not have. */
		if (digit >= base)
			break;
		result = result * base + digit;
	}

	/* Succeeded: the value is cut to the integer width. */
	*value = result & drv_acpi_integer_mask();
	return 0;
}

/* Measures a resource template up to, not including, its end tag. */
static size_t
template_length(
	const struct drv_acpi_object *buffer)
{
	size_t length;

	/* A template ends with a two-byte end tag. */
	length = buffer->value.buffer.length;
	if (length >= RESOURCE_END_LENGTH &&
	    buffer->value.buffer.bytes[length - RESOURCE_END_LENGTH] == RESOURCE_END_TAG)
		return length - RESOURCE_END_LENGTH;

	/* A buffer without one is taken whole. */
	return length;
}

/* Reports the ObjectType number of an object. */
static enum drv_acpi_type
object_type_of(
	struct drv_acpi_object *object)
{
	/* A missing object is uninitialized. */
	if (object == NULL)
		return DRV_ACPI_TYPE_UNINITIALIZED;

	/* The internal kinds fold into what ObjectType can report. */
	if (object->type == DRV_ACPI_TYPE_SCOPE)
		return DRV_ACPI_TYPE_UNINITIALIZED;
	if (object->type == DRV_ACPI_TYPE_ALIAS)
		return DRV_ACPI_TYPE_UNINITIALIZED;

	/* Reports the type. */
	return (enum drv_acpi_type)object->type;
}

/* Reports the integer AML uses for a truth value. */
static uint64_t
logical(
	bool value)
{
	/* True is all ones at the integer width. */
	if (value)
		return drv_acpi_integer_mask();

	/* False is zero. */
	return 0;
}
