/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Operation regions and the fields over them (ACPI 6.5 sections 5.5.2.4
 * and 19.6.46): address space handlers, reading and writing field units
 * by their access width and update rule, index and bank fields, and
 * buffer fields.
 */

#include <kern/kcrt.h>
#include <uapi/errno.h>

#include "aml-internal.h"
#include "aml-os.h"

/*
 * The widest access a field unit makes, in bytes.
 */
#define ACCESS_BYTES_MAX 8U

/*
 * The largest field read or written at once, in bytes; larger ones are
 * refused rather than allocated.
 */
#define FIELD_BYTES_MAX 4096U

/*
 * One installed address space handler.
 */
struct region_handler {
	drv_acpi_region_handler_t handler;
	void *argument;
};

/*
 * The handler of each address space.
 *
 * A driver installs one with drv_acpi_region_install() before the regions
 * of that space are used; the entry stays for the life of the system.  An
 * empty entry makes every access to the space fail.
 */
static struct region_handler region_handlers[DRV_ACPI_SPACE_COUNT];

static int region_access(struct drv_acpi_eval *eval, struct drv_acpi_object *region, uint64_t offset, unsigned bytes, bool write, uint64_t *value);
static int region_resolve_pci(struct drv_acpi_object *region);
static int evaluate_found(struct drv_acpi_node *device, const char *name, bool own, uint64_t *value);
static unsigned field_access_bytes(const struct drv_acpi_field *field, uint64_t region_length);
static int field_span(struct drv_acpi_eval *eval, struct drv_acpi_object *field, unsigned *access, uint64_t *first, uint64_t *last);
static int unit_read(struct drv_acpi_eval *eval, struct drv_acpi_object *field, uint64_t byte_offset, unsigned bytes, uint64_t *value);
static int unit_write(struct drv_acpi_eval *eval, struct drv_acpi_object *field, uint64_t byte_offset, unsigned bytes, uint64_t value);
static int bits_to_object(const uint8_t *bytes, uint64_t bit_length, struct drv_acpi_object **result);
static int object_to_bits(struct drv_acpi_object *value, uint64_t bit_length, uint8_t *bytes);
static void bits_extract(const uint8_t *source, uint64_t bit_offset, uint64_t bit_length, uint8_t *destination);
static void bits_insert(uint8_t *destination, uint64_t bit_offset, uint64_t bit_length, const uint8_t *source);
static int named_field_write(struct drv_acpi_eval *eval, struct drv_acpi_node *node, uint64_t value);
static int named_field_read(struct drv_acpi_eval *eval, struct drv_acpi_node *node, uint64_t *value);

/*
 * Installs the handler of an address space.
 */
int
drv_acpi_region_install(
	enum drv_acpi_space space,
	drv_acpi_region_handler_t handler,
	void *argument)
{
	/* Refuses a space ACPI does not define. */
	if ((unsigned)space >= DRV_ACPI_SPACE_COUNT)
		return EINVAL;

	/* Refuses a second handler for the same space. */
	if (region_handlers[space].handler != NULL)
		return EBUSY;

	/* Installs it. */
	region_handlers[space].handler = handler;
	region_handlers[space].argument = argument;

	/* Succeeded. */
	return 0;
}

/*
 * Removes every address space handler, for the host tests that start the
 * interpreter over.
 */
void
drv_acpi_region_reset(void)
{
	/* Forgets every handler. */
	kern_memset(region_handlers, 0, sizeof(region_handlers));
}

/*
 * Reads a field unit and reports its value: an integer when it fits in
 * one, a buffer otherwise.
 */
int
drv_acpi_field_read(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object *field,
	struct drv_acpi_object **result)
{
	struct drv_acpi_field *unit;
	uint8_t *units;
	uint8_t *bits;
	uint64_t first;
	uint64_t last;
	uint64_t offset;
	uint64_t value;
	unsigned access;
	unsigned index;
	size_t span;
	size_t size;
	int error;

	/* Covers the field's bits with aligned accesses of one width. */
	unit = &field->value.field;
	error = field_span(eval, field, &access, &first, &last);
	if (error != 0)
		return error;
	span = (size_t)(last - first);
	size = (size_t)((unit->bit_length + 7U) / 8U);

	/* Allocates the bytes the accesses fill and the bytes of the field. */
	units = drv_acpi_os_alloc(span + size + 1U);
	if (units == NULL)
		return ENOMEM;
	kern_memset(units, 0, span + size + 1U);
	bits = units + span;

	/* Reads each access unit into its place, lowest byte first. */
	for (offset = first; offset < last; offset += access) {
		/* Reads one unit. */
		error = unit_read(eval, field, offset, access, &value);
		if (error != 0) {
			drv_acpi_os_free(units);
			return error;
		}

		/* Lays its bytes out in order. */
		for (index = 0; index < access; index++)
			units[offset - first + index] = (uint8_t)(value >> (index * 8U));
	}

	/* Takes the field's bits out and makes the value. */
	bits_extract(units, unit->bit_offset - first * 8U, unit->bit_length, bits);
	error = bits_to_object(bits, unit->bit_length, result);
	drv_acpi_os_free(units);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Writes a value into a field unit.
 *
 * Accesses that cover bits outside the field fill those bits by the
 * field's update rule: the current contents, ones, or zeros.
 */
int
drv_acpi_field_write(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object *field,
	struct drv_acpi_object *value)
{
	struct drv_acpi_field *unit;
	uint8_t *units;
	uint8_t *bits;
	uint64_t first;
	uint64_t last;
	uint64_t offset;
	uint64_t word;
	uint64_t unit_start;
	uint64_t unit_end;
	uint64_t field_start;
	uint64_t field_end;
	unsigned update;
	unsigned access;
	unsigned index;
	size_t span;
	size_t size;
	int error;

	/* Covers the field's bits with aligned accesses of one width. */
	unit = &field->value.field;
	error = field_span(eval, field, &access, &first, &last);
	if (error != 0)
		return error;
	span = (size_t)(last - first);
	size = (size_t)((unit->bit_length + 7U) / 8U);

	/* Allocates the bytes of the accesses and of the field. */
	units = drv_acpi_os_alloc(span + size + 1U);
	if (units == NULL)
		return ENOMEM;
	kern_memset(units, 0, span + size + 1U);
	bits = units + span;

	/* Converts the value to the field's bits. */
	error = object_to_bits(value, unit->bit_length, bits);
	if (error != 0) {
		drv_acpi_os_free(units);
		return error;
	}

	/* Fills each unit the field covers only in part, by the update rule. */
	update = unit->flags & DRV_ACPI_FIELD_UPDATE_MASK;
	field_start = unit->bit_offset;
	field_end = field_start + unit->bit_length;
	for (offset = first; offset < last; offset += access) {
		unit_start = offset * 8U;
		unit_end = unit_start + access * 8U;

		/* A unit entirely inside the field needs nothing of its own. */
		if (unit_start >= field_start && unit_end <= field_end)
			continue;

		/* Keeps, sets or clears the bits outside the field. */
		if (update == DRV_ACPI_FIELD_UPDATE_PRESERVE) {
			error = unit_read(eval, field, offset, access, &word);
			if (error != 0) {
				drv_acpi_os_free(units);
				return error;
			}
		} else if (update == DRV_ACPI_FIELD_UPDATE_ONES) {
			word = ~0ULL;
		} else {
			word = 0;
		}

		/* Lays the unit's bytes out in order. */
		for (index = 0; index < access; index++)
			units[offset - first + index] = (uint8_t)(word >> (index * 8U));
	}

	/* Puts the field's bits in place and writes each unit. */
	bits_insert(units, field_start - first * 8U, unit->bit_length, bits);
	for (offset = first; offset < last; offset += access) {
		/* Assembles one unit, lowest byte first. */
		word = 0;
		for (index = 0; index < access; index++)
			word |= (uint64_t)units[offset - first + index] << (index * 8U);

		/* Writes it. */
		error = unit_write(eval, field, offset, access, word);
		if (error != 0) {
			drv_acpi_os_free(units);
			return error;
		}
	}

	/* Frees the staging bytes. */
	drv_acpi_os_free(units);

	/* Succeeded. */
	return 0;
}

/*
 * Reads a buffer field: an integer when it fits in one, a buffer otherwise.
 */
int
drv_acpi_buffer_field_read(
	struct drv_acpi_object *field,
	struct drv_acpi_object **result)
{
	struct drv_acpi_buffer_field *unit;
	uint8_t *bits;
	size_t size;
	int error;

	/* Allocates the bytes of the field. */
	unit = &field->value.buffer_field;
	size = (size_t)((unit->bit_length + 7U) / 8U);
	bits = drv_acpi_os_alloc(size + 1U);
	if (bits == NULL)
		return ENOMEM;
	kern_memset(bits, 0, size + 1U);

	/* Takes the bits out of the buffer. */
	bits_extract(unit->buffer->value.buffer.bytes, unit->bit_offset, unit->bit_length, bits);

	/* A CreateField field is always a buffer; the others are integers when they fit. */
	if (unit->reads_buffer) {
		*result = drv_acpi_object_buffer_new(bits, size);
		error = 0;
		if (*result == NULL)
			error = ENOMEM;
	} else {
		error = bits_to_object(bits, unit->bit_length, result);
	}

	/* Frees the staging bytes and reports a value that could not be made. */
	drv_acpi_os_free(bits);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Writes a value into a buffer field.
 */
int
drv_acpi_buffer_field_write(
	struct drv_acpi_object *field,
	struct drv_acpi_object *value)
{
	struct drv_acpi_buffer_field *unit;
	uint8_t *bits;
	size_t size;
	int error;

	/* Allocates the bytes of the field. */
	unit = &field->value.buffer_field;
	size = (size_t)((unit->bit_length + 7U) / 8U);
	bits = drv_acpi_os_alloc(size + 1U);
	if (bits == NULL)
		return ENOMEM;
	kern_memset(bits, 0, size + 1U);

	/* Converts the value and puts its bits into the buffer. */
	error = object_to_bits(value, unit->bit_length, bits);
	if (error == 0)
		bits_insert(unit->buffer->value.buffer.bytes, unit->bit_offset, unit->bit_length, bits);
	drv_acpi_os_free(bits);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Evaluates the offset and length of a region defined while a table
 * loaded, the first time the region is used.
 */
int
drv_acpi_region_prepare(
	struct drv_acpi_object *region)
{
	struct drv_acpi_eval arguments;
	struct drv_acpi_table *table;
	int error;

	/* A region is prepared once. */
	if (region->value.region.evaluated)
		return 0;

	/* Runs the offset and length AML in the scope the region was defined in. */
	table = region->value.region.table;
	kern_memset(&arguments, 0, sizeof(arguments));
	arguments.position = region->value.region.arguments_start;
	arguments.end = region->value.region.arguments_end;
	arguments.scope = region->value.region.scope;
	arguments.table = table;
	error = drv_acpi_eval_integer(&arguments, &region->value.region.offset);
	if (error == 0)
		error = drv_acpi_eval_integer(&arguments, &region->value.region.length);
	if (error != 0) {
		drv_acpi_os_log("ACPI: region arguments could not be evaluated (error %d)\n", error);
		return error;
	}

	/* Succeeded: the region has its place. */
	region->value.region.evaluated = 1;
	return 0;
}

/* Reads or writes one access unit of a region through its space's handler. */
static int
region_access(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object *region,
	uint64_t offset,
	unsigned bytes,
	bool write,
	uint64_t *value)
{
	struct drv_acpi_region_access access;
	struct drv_acpi_region *place;
	struct region_handler *handler;
	unsigned index;
	uint8_t *data;
	int error;

	UNUSED_PARAMETER(eval);

	/* Prepares the region's place. */
	error = drv_acpi_region_prepare(region);
	if (error != 0)
		return error;
	place = &region->value.region;

	/* Refuses an access outside the region. */
	if (offset > place->length || bytes > place->length - offset) {
		drv_acpi_os_log(
			"ACPI: field access at 0x%llx past the region's 0x%llx bytes\n",
			(unsigned long long)offset,
			(unsigned long long)place->length);
		return EFAULT;
	}

	/* A data table region reads the table's bytes, which never change. */
	if (place->data != NULL) {
		/* Refuses a write to a table. */
		if (write)
			return EPERM;
		data = (uint8_t *)place->data + offset;
		*value = 0;
		for (index = 0; index < bytes; index++)
			*value |= (uint64_t)data[index] << (index * 8U);
		return 0;
	}

	/* Refuses a space ACPI does not define. */
	if (place->space >= DRV_ACPI_SPACE_COUNT) {
		drv_acpi_os_log("ACPI: region of unknown space 0x%x\n", (unsigned)place->space);
		return ENODEV;
	}

	/* Refuses a space that has no handler. */
	handler = &region_handlers[place->space];
	if (handler->handler == NULL) {
		drv_acpi_os_log("ACPI: no handler for address space %u\n", (unsigned)place->space);
		return ENODEV;
	}

	/* Finds the PCI function of a configuration region. */
	kern_memset(&access, 0, sizeof(access));
	if (place->space == DRV_ACPI_SPACE_PCI_CONFIG) {
		error = region_resolve_pci(region);
		if (error != 0)
			return error;
		access.pci_segment = place->pci_segment;
		access.pci_bus = place->pci_bus;
		access.pci_device = place->pci_device;
		access.pci_function = place->pci_function;
	}

	/* Hands the access to the handler. */
	access.address = place->offset + offset;
	access.width = bytes * 8U;
	access.write = write;
	error = handler->handler(&access, value, handler->argument);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Finds the PCI function a configuration region belongs to: the device
 * that contains the region gives the device and function (_ADR), and the
 * nearest scope above it that has them gives the bus (_BBN) and the
 * segment (_SEG).
 */
static int
region_resolve_pci(
	struct drv_acpi_object *region)
{
	struct drv_acpi_node *device;
	uint64_t address;
	uint64_t bus;
	uint64_t segment;
	int error;

	/* The function is found once. */
	if (region->value.region.pci_resolved)
		return 0;

	/* The device is the region's parent. */
	device = NULL;
	if (region->value.region.node != NULL)
		device = region->value.region.node->parent;
	if (device == NULL)
		return EINVAL;

	/* Reads the device and function from its address, zero when it has none. */
	address = 0;
	error = evaluate_found(device, "_ADR", true, &address);
	if (error != 0)
		return error;

	/* Reads the bus and the segment, zero when no scope has them. */
	bus = 0;
	error = evaluate_found(device, "_BBN", false, &bus);
	if (error != 0)
		return error;
	segment = 0;
	error = evaluate_found(device, "_SEG", false, &segment);
	if (error != 0)
		return error;

	/* Succeeded: remembers the function. */
	region->value.region.pci_segment = (uint16_t)segment;
	region->value.region.pci_bus = (uint8_t)bus;
	region->value.region.pci_device = (uint8_t)((address >> 16) & 0x1fU);
	region->value.region.pci_function = (uint8_t)(address & 0x07U);
	region->value.region.pci_resolved = 1;
	return 0;
}

/*
 * Evaluates an integer object found from a device by the search rules, or
 * leaves the value alone when there is none.  With own set, only an object
 * of the device itself counts.
 */
static int
evaluate_found(
	struct drv_acpi_node *device,
	const char *name,
	bool own,
	uint64_t *value)
{
	struct drv_acpi_node *found;
	int error;

	/* Searches from the device toward the root. */
	error = drv_acpi_lookup(device, name, &found);
	if (error == ENOENT)
		return 0;
	if (error != 0)
		return error;

	/* An object of another scope does not count when the device's own is asked for. */
	if (own && found->parent != device)
		return 0;

	/* Evaluates it. */
	error = drv_acpi_evaluate_integer(found, NULL, value);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Chooses the access width of a field in bytes.
 *
 * A field that asks for any access uses the narrowest width in which it
 * fits one aligned access inside the region, and bytes when none does.
 */
static unsigned
field_access_bytes(
	const struct drv_acpi_field *field,
	uint64_t region_length)
{
	uint64_t start;
	uint64_t end;
	unsigned bytes;

	/* Chooses by the access type. */
	switch (field->flags & DRV_ACPI_FIELD_ACCESS_MASK) {
	case DRV_ACPI_FIELD_ACCESS_WORD:
		return 2;
	case DRV_ACPI_FIELD_ACCESS_DWORD:
		return 4;
	case DRV_ACPI_FIELD_ACCESS_QWORD:
		return 8;
	case DRV_ACPI_FIELD_ACCESS_ANY:
		break;
	default:
		return 1;
	}

	/* Tries each width from the narrowest. */
	start = field->bit_offset;
	end = start + field->bit_length;
	for (bytes = 1; bytes <= ACCESS_BYTES_MAX; bytes *= 2U) {
		/* Takes the width when one aligned access holds the whole field. */
		if (start / (bytes * 8U) == (end - 1U) / (bytes * 8U) &&
		    (start / (bytes * 8U) + 1U) * bytes <= region_length)
			return bytes;
	}

	/* Falls back to byte accesses. */
	return 1;
}

/*
 * Chooses the access width of a field and the byte range of the accesses
 * that cover it.
 */
static int
field_span(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object *field,
	unsigned *access,
	uint64_t *first,
	uint64_t *last)
{
	struct drv_acpi_field *unit;
	struct drv_acpi_object *region;
	uint64_t region_length;
	uint64_t end;
	int error;

	UNUSED_PARAMETER(eval);

	/* A region's length bounds the width; an index field has no region. */
	unit = &field->value.field;
	region_length = ~0ULL;
	if (unit->kind != DRV_ACPI_FIELD_INDEX) {
		region = unit->region->object;
		if (region == NULL || region->type != DRV_ACPI_TYPE_REGION)
			return EINVAL;
		error = drv_acpi_region_prepare(region);
		if (error != 0)
			return error;
		region_length = region->value.region.length;
	}

	/* Refuses a field of no bits or too many. */
	if (unit->bit_length == 0 || unit->bit_length / 8U > FIELD_BYTES_MAX)
		return EINVAL;

	/* Refuses the buffer access that serial bus spaces need. */
	if ((unit->flags & DRV_ACPI_FIELD_ACCESS_MASK) == DRV_ACPI_FIELD_ACCESS_BUFFER) {
		drv_acpi_os_log("ACPI: buffer access fields are not supported\n");
		return ENOTSUP;
	}

	/* Covers the bits with aligned accesses. */
	*access = field_access_bytes(unit, region_length);
	*first = (unit->bit_offset / 8U) / *access * *access;
	end = ((uint64_t)unit->bit_offset + unit->bit_length + 7U) / 8U;
	*last = (end + *access - 1U) / *access * *access;

	/* Succeeded. */
	return 0;
}

/* Reads one access unit of a field. */
static int
unit_read(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object *field,
	uint64_t byte_offset,
	unsigned bytes,
	uint64_t *value)
{
	struct drv_acpi_field *unit;
	uint64_t mask;
	int error;

	/* Chooses the access by the kind of field. */
	unit = &field->value.field;
	switch (unit->kind) {
	case DRV_ACPI_FIELD_INDEX:
		/* Selects the unit through the index, then reads the data. */
		error = named_field_write(eval, unit->index, byte_offset);
		if (error != 0)
			return error;
		error = named_field_read(eval, unit->data, value);
		if (error != 0)
			return error;
		break;
	case DRV_ACPI_FIELD_BANK:
		/* Selects the bank, then reads the region. */
		error = named_field_write(eval, unit->index, unit->bank_value);
		if (error != 0)
			return error;
		error = region_access(eval, unit->region->object, byte_offset, bytes, false, value);
		if (error != 0)
			return error;
		break;
	default:
		error = region_access(eval, unit->region->object, byte_offset, bytes, false, value);
		if (error != 0)
			return error;
		break;
	}

	/* Keeps only the unit's bytes. */
	mask = ~0ULL;
	if (bytes < 8U)
		mask = (1ULL << (bytes * 8U)) - 1U;
	*value &= mask;

	/* Succeeded. */
	return 0;
}

/* Writes one access unit of a field. */
static int
unit_write(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object *field,
	uint64_t byte_offset,
	unsigned bytes,
	uint64_t value)
{
	struct drv_acpi_field *unit;
	int error;

	/* Chooses the access by the kind of field. */
	unit = &field->value.field;
	switch (unit->kind) {
	case DRV_ACPI_FIELD_INDEX:
		/* Selects the unit through the index, then writes the data. */
		error = named_field_write(eval, unit->index, byte_offset);
		if (error != 0)
			return error;
		error = named_field_write(eval, unit->data, value);
		if (error != 0)
			return error;
		break;
	case DRV_ACPI_FIELD_BANK:
		/* Selects the bank, then writes the region. */
		error = named_field_write(eval, unit->index, unit->bank_value);
		if (error != 0)
			return error;
		error = region_access(eval, unit->region->object, byte_offset, bytes, true, &value);
		if (error != 0)
			return error;
		break;
	default:
		error = region_access(eval, unit->region->object, byte_offset, bytes, true, &value);
		if (error != 0)
			return error;
		break;
	}

	/* Succeeded. */
	return 0;
}

/* Makes the value of a field from its bits: an integer when it fits, else a buffer. */
static int
bits_to_object(
	const uint8_t *bytes,
	uint64_t bit_length,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *object;
	uint64_t value;
	uint64_t integer_bits;
	size_t size;
	size_t index;

	/* A field no wider than an integer reads as one. */
	size = (size_t)((bit_length + 7U) / 8U);
	integer_bits = drv_acpi_integer_bytes() * 8U;
	if (bit_length <= integer_bits) {
		value = 0;
		for (index = 0; index < size; index++)
			value |= (uint64_t)bytes[index] << (index * 8U);
		object = drv_acpi_object_integer_new(value);
	} else {
		object = drv_acpi_object_buffer_new(bytes, size);
	}

	/* Reports a value that could not be allocated. */
	if (object == NULL)
		return ENOMEM;

	/* Succeeded. */
	*result = object;
	return 0;
}

/*
 * Converts a value to a field's bits: an integer gives its bytes, anything
 * else is converted to a buffer.  Bits the value does not have are zero.
 */
static int
object_to_bits(
	struct drv_acpi_object *value,
	uint64_t bit_length,
	uint8_t *bytes)
{
	struct drv_acpi_object *buffer;
	size_t size;
	size_t length;
	size_t index;
	int error;

	/* An integer is laid out lowest byte first. */
	size = (size_t)((bit_length + 7U) / 8U);
	if (value->type == DRV_ACPI_TYPE_INTEGER) {
		for (index = 0; index < size && index < 8U; index++)
			bytes[index] = (uint8_t)(value->value.integer >> (index * 8U));
		return 0;
	}

	/* Anything else becomes a buffer first. */
	error = drv_acpi_convert_buffer(value, &buffer);
	if (error != 0)
		return error;

	/* Copies as many bytes as the field has. */
	length = buffer->value.buffer.length;
	if (length > size)
		length = size;
	if (length != 0)
		kern_memcpy(bytes, buffer->value.buffer.bytes, length);
	drv_acpi_object_release(buffer);

	/* Succeeded. */
	return 0;
}

/* Copies a run of bits out of a byte string into the front of another. */
static void
bits_extract(
	const uint8_t *source,
	uint64_t bit_offset,
	uint64_t bit_length,
	uint8_t *destination)
{
	uint64_t index;
	uint64_t from;

	/* Copies bit by bit; fields are short and this is not a fast path. */
	for (index = 0; index < bit_length; index++) {
		from = bit_offset + index;

		/* Sets the destination bit when the source bit is set. */
		if ((source[from / 8U] >> (from % 8U)) & 1U)
			destination[index / 8U] |= (uint8_t)(1U << (index % 8U));
	}

	/* Clears the bits above the run in the last destination byte. */
	if (bit_length % 8U != 0)
		destination[bit_length / 8U] &= (uint8_t)((1U << (bit_length % 8U)) - 1U);
}

/* Copies the front bits of a byte string into a run of bits of another. */
static void
bits_insert(
	uint8_t *destination,
	uint64_t bit_offset,
	uint64_t bit_length,
	const uint8_t *source)
{
	uint64_t index;
	uint64_t to;
	uint8_t mask;

	/* Copies bit by bit. */
	for (index = 0; index < bit_length; index++) {
		to = bit_offset + index;
		mask = (uint8_t)(1U << (to % 8U));

		/* Sets or clears the destination bit. */
		if ((source[index / 8U] >> (index % 8U)) & 1U) {
			destination[to / 8U] |= mask;
		} else {
			destination[to / 8U] &= (uint8_t)~mask;
		}
	}
}

/* Writes an integer into the field unit a node holds. */
static int
named_field_write(
	struct drv_acpi_eval *eval,
	struct drv_acpi_node *node,
	uint64_t value)
{
	struct drv_acpi_object *integer;
	struct drv_acpi_object *field;
	int error;

	/* Refuses a selector that is not a field unit. */
	field = node->object;
	if (field == NULL || field->type != DRV_ACPI_TYPE_FIELD_UNIT)
		return EINVAL;

	/* Writes the value. */
	integer = drv_acpi_object_integer_new(value);
	if (integer == NULL)
		return ENOMEM;
	error = drv_acpi_field_write(eval, field, integer);
	drv_acpi_object_release(integer);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Reads the field unit a node holds as an integer. */
static int
named_field_read(
	struct drv_acpi_eval *eval,
	struct drv_acpi_node *node,
	uint64_t *value)
{
	struct drv_acpi_object *field;
	struct drv_acpi_object *object;
	int error;

	/* Refuses a data register that is not a field unit. */
	field = node->object;
	if (field == NULL || field->type != DRV_ACPI_TYPE_FIELD_UNIT)
		return EINVAL;

	/* Reads it and converts the value. */
	error = drv_acpi_field_read(eval, field, &object);
	if (error != 0)
		return error;
	error = drv_acpi_convert_integer(object, value);
	drv_acpi_object_release(object);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}
