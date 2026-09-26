/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * AML objects: creation, reference counting, copying and the accessors
 * drivers read results with.
 */

#include <kern/kcrt.h>
#include <uapi/errno.h>

#include "aml-internal.h"
#include "aml-os.h"

/*
 * The width of an AML integer in bits: 32 when the DSDT's revision is below
 * 2, 64 otherwise.
 *
 * The DSDT decides it for every table, so it is set once when the DSDT is
 * loaded and read by every arithmetic operation afterwards.  It starts at
 * 64 so that objects made before any table is loaded keep their value.
 */
static unsigned integer_bits = 64;

static void object_free_contents(struct drv_acpi_object *object);
static int package_copy(struct drv_acpi_object *source, struct drv_acpi_object **result);

/*
 * Creates an integer object for a driver's method argument.
 */
struct drv_acpi_object *
drv_acpi_object_integer_new(
	uint64_t value)
{
	struct drv_acpi_object *object;

	/* Allocates the integer. */
	object = drv_acpi_object_new(DRV_ACPI_TYPE_INTEGER);
	if (object == NULL)
		return NULL;

	/* Keeps the value as the caller gave it. */
	object->value.integer = value;

	/* Succeeded: the caller holds the only reference. */
	return object;
}

/*
 * Creates a string object from a NUL-terminated text.
 */
struct drv_acpi_object *
drv_acpi_object_string_new(
	const char *text)
{
	struct drv_acpi_object *object;
	size_t length;

	/* Measures the text without its terminator. */
	length = kern_strlen(text);

	/* Copies the text into a new string. */
	object = drv_acpi_object_string_new_length(text, length);

	/* Reports the string, or NULL when memory ran out. */
	return object;
}

/*
 * Creates a buffer object holding a copy of the given bytes.
 */
struct drv_acpi_object *
drv_acpi_object_buffer_new(
	const void *bytes,
	size_t length)
{
	struct drv_acpi_object *object;

	/* Allocates the buffer object. */
	object = drv_acpi_object_new(DRV_ACPI_TYPE_BUFFER);
	if (object == NULL)
		return NULL;

	/* An empty buffer has no storage to allocate. */
	if (length == 0)
		return object;

	/* Allocates the storage. */
	object->value.buffer.bytes = drv_acpi_os_alloc(length);
	if (object->value.buffer.bytes == NULL) {
		drv_acpi_object_release(object);
		return NULL;
	}

	/* Fills the storage from the bytes, or with zeros when there are none. */
	object->value.buffer.length = length;
	if (bytes != NULL) {
		kern_memcpy(object->value.buffer.bytes, bytes, length);
	} else {
		kern_memset(object->value.buffer.bytes, 0, length);
	}

	/* Succeeded: the caller holds the only reference. */
	return object;
}

/*
 * Releases one reference and frees the object with the last one.
 */
void
drv_acpi_object_release(
	struct drv_acpi_object *object)
{
	/* Releasing nothing is allowed so that error paths stay simple. */
	if (object == NULL)
		return;

	/* The count drops by the reference this holder gave up. */
	object->references--;
	if (object->references != 0)
		return;

	/* Frees what the object owns, then the object. */
	object_free_contents(object);
	drv_acpi_os_free(object);
}

/*
 * Reports the type of an object.
 */
enum drv_acpi_type
drv_acpi_object_type(
	const struct drv_acpi_object *object)
{
	/* A missing object reads as uninitialized. */
	if (object == NULL)
		return DRV_ACPI_TYPE_UNINITIALIZED;

	/* Reports the stored type. */
	return (enum drv_acpi_type)object->type;
}

/*
 * Reports the value of an integer object, or zero for any other object.
 */
uint64_t
drv_acpi_object_integer(
	const struct drv_acpi_object *object)
{
	/* Anything but an integer has no integer value. */
	if (object == NULL || object->type != DRV_ACPI_TYPE_INTEGER)
		return 0;

	/* Reports the value. */
	return object->value.integer;
}

/*
 * Reports the text and length of a string object.
 */
const char *
drv_acpi_object_string(
	const struct drv_acpi_object *object,
	size_t *length)
{
	/* Anything but a string has no text. */
	if (object == NULL || object->type != DRV_ACPI_TYPE_STRING)
		return NULL;

	/* Reports the length when the caller asked for it. */
	if (length != NULL)
		*length = object->value.string.length;

	/* Reports the NUL-terminated text. */
	return object->value.string.text;
}

/*
 * Reports the bytes and length of a buffer object.
 */
const uint8_t *
drv_acpi_object_buffer(
	const struct drv_acpi_object *object,
	size_t *length)
{
	/* Anything but a buffer has no bytes. */
	if (object == NULL || object->type != DRV_ACPI_TYPE_BUFFER)
		return NULL;

	/* Reports the length when the caller asked for it. */
	if (length != NULL)
		*length = object->value.buffer.length;

	/* Reports the bytes, which are NULL for an empty buffer. */
	return object->value.buffer.bytes;
}

/*
 * Reports how many elements a package object has.
 */
unsigned
drv_acpi_object_package_count(
	const struct drv_acpi_object *object)
{
	/* Anything but a package has no elements. */
	if (object == NULL || object->type != DRV_ACPI_TYPE_PACKAGE)
		return 0;

	/* Reports the element count. */
	return object->value.package.count;
}

/*
 * Reports one element of a package object without adding a reference.
 */
struct drv_acpi_object *
drv_acpi_object_package_element(
	const struct drv_acpi_object *object,
	unsigned index)
{
	/* Anything but a package has no elements. */
	if (object == NULL || object->type != DRV_ACPI_TYPE_PACKAGE)
		return NULL;

	/* An index past the end names nothing. */
	if (index >= object->value.package.count)
		return NULL;

	/* Reports the element, which is NULL when it was never initialized. */
	return object->value.package.elements[index];
}

/*
 * Reports the namespace node a reference object names.
 */
struct drv_acpi_node *
drv_acpi_object_reference_node(
	const struct drv_acpi_object *object)
{
	/* Anything but a reference names no node. */
	if (object == NULL || object->type != DRV_ACPI_TYPE_REFERENCE)
		return NULL;

	/* A name that did not resolve before may resolve now. */
	if (object->value.reference.kind == DRV_ACPI_REFERENCE_NAME)
		drv_acpi_reference_resolve((struct drv_acpi_object *)object);

	/* Only a node reference names one. */
	if (object->value.reference.kind != DRV_ACPI_REFERENCE_NODE)
		return NULL;

	/* Reports the node. */
	return object->value.reference.node;
}

/*
 * Allocates a zero-filled object of one type with one reference.
 */
struct drv_acpi_object *
drv_acpi_object_new(
	enum drv_acpi_type type)
{
	struct drv_acpi_object *object;

	/* Allocates the object. */
	object = drv_acpi_os_alloc(sizeof(*object));
	if (object == NULL)
		return NULL;

	/* Starts with no value and the caller's reference. */
	kern_memset(object, 0, sizeof(*object));
	object->references = 1;
	object->type = (uint8_t)type;

	/* Succeeded: the caller holds the only reference. */
	return object;
}

/*
 * Creates a string object from counted text.
 */
struct drv_acpi_object *
drv_acpi_object_string_new_length(
	const char *text,
	size_t length)
{
	struct drv_acpi_object *object;

	/* Allocates the string object. */
	object = drv_acpi_object_new(DRV_ACPI_TYPE_STRING);
	if (object == NULL)
		return NULL;

	/* Allocates room for the text and its terminator. */
	object->value.string.text = drv_acpi_os_alloc(length + 1U);
	if (object->value.string.text == NULL) {
		drv_acpi_object_release(object);
		return NULL;
	}

	/* Copies the text and terminates it. */
	if (length != 0)
		kern_memcpy(object->value.string.text, text, length);
	object->value.string.text[length] = '\0';
	object->value.string.length = length;

	/* Succeeded: the caller holds the only reference. */
	return object;
}

/*
 * Creates a package with a number of uninitialized elements.
 */
struct drv_acpi_object *
drv_acpi_object_package_new(
	uint32_t count)
{
	struct drv_acpi_object *object;
	size_t size;

	/* Allocates the package object. */
	object = drv_acpi_object_new(DRV_ACPI_TYPE_PACKAGE);
	if (object == NULL)
		return NULL;

	/* An empty package has no element array. */
	if (count == 0)
		return object;

	/* Allocates the element array. */
	size = (size_t)count * sizeof(object->value.package.elements[0]);
	object->value.package.elements = drv_acpi_os_alloc(size);
	if (object->value.package.elements == NULL) {
		drv_acpi_object_release(object);
		return NULL;
	}

	/* Every element starts uninitialized. */
	kern_memset(object->value.package.elements, 0, size);
	object->value.package.count = count;

	/* Succeeded: the caller holds the only reference. */
	return object;
}

/*
 * Creates an empty reference object of one kind.
 */
struct drv_acpi_object *
drv_acpi_object_reference_new(
	enum drv_acpi_reference_kind kind)
{
	struct drv_acpi_object *object;

	/* Allocates the reference. */
	object = drv_acpi_object_new(DRV_ACPI_TYPE_REFERENCE);
	if (object == NULL)
		return NULL;

	/* Records what the reference points into; the caller fills in where. */
	object->value.reference.kind = (uint8_t)kind;

	/* Succeeded: the caller holds the only reference. */
	return object;
}

/*
 * Adds one reference for a new holder.
 */
void
drv_acpi_object_ref(
	struct drv_acpi_object *object)
{
	/* The new holder keeps the object alive until it releases it. */
	object->references++;
}

/*
 * Makes the copy a Store gives its target.
 *
 * Integers, strings, buffers and packages are copied by value, so that the
 * target and the source change independently afterwards.  Every other
 * object is shared.
 */
int
drv_acpi_object_copy(
	struct drv_acpi_object *source,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *copy;
	int error;

	/* Chooses the copy by the kind of value. */
	switch (source->type) {
	case DRV_ACPI_TYPE_INTEGER:
		copy = drv_acpi_object_integer_new(source->value.integer);
		break;
	case DRV_ACPI_TYPE_STRING:
		copy = drv_acpi_object_string_new_length(
			source->value.string.text,
			source->value.string.length);
		break;
	case DRV_ACPI_TYPE_BUFFER:
		copy = drv_acpi_object_buffer_new(
			source->value.buffer.bytes,
			source->value.buffer.length);
		break;
	case DRV_ACPI_TYPE_PACKAGE:
		error = package_copy(source, result);
		return error;
	default:
		drv_acpi_object_ref(source);
		*result = source;
		return 0;
	}

	/* Reports a copy that could not be allocated. */
	if (copy == NULL)
		return ENOMEM;

	/* Succeeded: the caller holds the copy. */
	*result = copy;
	return 0;
}

/*
 * Reports the mask of the bits an integer holds.
 */
uint64_t
drv_acpi_integer_mask(void)
{
	/* A 32-bit integer keeps the low half. */
	if (integer_bits == 32)
		return 0xffffffffULL;

	/* A 64-bit integer keeps every bit. */
	return ~0ULL;
}

/*
 * Reports the size of an integer in bytes.
 */
unsigned
drv_acpi_integer_bytes(void)
{
	/* Converts the width to bytes. */
	return integer_bits / 8U;
}

/*
 * Sets the integer width from the DSDT's revision.
 */
void
drv_acpi_integer_set_width(
	unsigned bits)
{
	/* Only the two widths ACPI defines exist. */
	if (bits == 32) {
		integer_bits = 32;
	} else {
		integer_bits = 64;
	}
}

/* Frees the storage one object owns and releases what it holds. */
static void
object_free_contents(
	struct drv_acpi_object *object)
{
	uint32_t index;

	/* Chooses what to free by the kind of object. */
	switch (object->type) {
	case DRV_ACPI_TYPE_STRING:
		drv_acpi_os_free(object->value.string.text);
		break;
	case DRV_ACPI_TYPE_BUFFER:
		drv_acpi_os_free(object->value.buffer.bytes);
		break;
	case DRV_ACPI_TYPE_PACKAGE:
		/* Releases every element the package holds. */
		for (index = 0; index < object->value.package.count; index++)
			drv_acpi_object_release(object->value.package.elements[index]);
		drv_acpi_os_free(object->value.package.elements);
		break;
	case DRV_ACPI_TYPE_REFERENCE:
		drv_acpi_object_release(object->value.reference.target);
		drv_acpi_os_free(object->value.reference.name);
		break;
	case DRV_ACPI_TYPE_BUFFER_FIELD:
		drv_acpi_object_release(object->value.buffer_field.buffer);
		break;
	case DRV_ACPI_TYPE_FIELD_UNIT:
		drv_acpi_object_release(object->value.field.connection);
		break;
	case DRV_ACPI_TYPE_METHOD:
		drv_acpi_object_release(object->value.method.serialization);
		break;
	default:
		break;
	}
}

/* Copies a package and every data element in it. */
static int
package_copy(
	struct drv_acpi_object *source,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *copy;
	struct drv_acpi_object *element;
	uint32_t index;
	int error;

	/* Allocates a package of the same size. */
	copy = drv_acpi_object_package_new(source->value.package.count);
	if (copy == NULL)
		return ENOMEM;

	/* Copies each element; an uninitialized one stays uninitialized. */
	for (index = 0; index < source->value.package.count; index++) {
		element = source->value.package.elements[index];
		if (element == NULL)
			continue;

		/* Copies the element the way a Store would. */
		error = drv_acpi_object_copy(element, &copy->value.package.elements[index]);
		if (error != 0) {
			drv_acpi_object_release(copy);
			return error;
		}
	}

	/* Succeeded: the caller holds the copy. */
	*result = copy;
	return 0;
}
