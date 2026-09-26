/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Definition blocks: loading the DSDT and the SSDTs into the namespace,
 * preparing the objects after the load, and the Load, LoadTable and
 * Unload operators.
 */

#include <kern/kcrt.h>
#include <uapi/errno.h>

#include "aml-internal.h"
#include "aml-os.h"

/*
 * The size of the header every definition block starts with.
 */
#define TABLE_HEADER_SIZE 36U

/*
 * The largest table the interpreter copies, which keeps a damaged length
 * from exhausting the kernel heap.
 */
#define TABLE_SIZE_MAX (16U * 1024U * 1024U)

/*
 * The loaded tables, oldest first.
 *
 * drv_acpi_load_table() appends to the list and drv_acpi_reset() empties
 * it; method bodies point into the tables, so a table stays as long as the
 * namespace does.  The interpreter lock serializes both.
 */
static struct drv_acpi_table *tables_first;
static struct drv_acpi_table *tables_last;

/*
 * The identifier the next loaded table gets.  It starts at 1 because 0
 * marks the predefined nodes, and it only grows.
 */
static uint32_t tables_next_id = 1;

static int table_check(const uint8_t *data, size_t length);
static void table_identify(struct drv_acpi_table *table);
static int prepare_visitor(struct drv_acpi_node *node, unsigned depth, void *argument);

/*
 * Loads one definition block (a DSDT or an SSDT) into the namespace.
 *
 * The interpreter keeps its own copy of the table.  A table that fails
 * part way leaves the names it created before the failure.
 */
int
drv_acpi_load_table(
	const void *table,
	size_t length)
{
	struct drv_acpi_table *loaded;
	struct drv_acpi_eval eval;
	int compared;
	int error;

	/* Creates the namespace on first use. */
	error = drv_acpi_initialize_namespace();
	if (error != 0)
		return error;

	/* Checks the header before trusting the length. */
	error = table_check(table, length);
	if (error != 0)
		return error;

	/* Copies the table and records who it is. */
	loaded = drv_acpi_os_alloc(sizeof(*loaded));
	if (loaded == NULL)
		return ENOMEM;
	kern_memset(loaded, 0, sizeof(*loaded));
	loaded->length = length;
	loaded->data = drv_acpi_os_alloc(length);
	if (loaded->data == NULL) {
		drv_acpi_os_free(loaded);
		return ENOMEM;
	}

	/* Copies the bytes and gives the table the next identifier. */
	kern_memcpy(loaded->data, table, length);
	table_identify(loaded);
	loaded->id = tables_next_id;
	tables_next_id++;

	/* The DSDT's revision sets the integer width of every table. */
	compared = kern_strcmp(loaded->signature, "DSDT");
	if (compared == 0) {
		if (loaded->revision < 2) {
			drv_acpi_integer_set_width(32);
		} else {
			drv_acpi_integer_set_width(64);
		}
	}

	/* Appends it to the list before running it, so it can find itself. */
	if (tables_last != NULL) {
		tables_last->next = loaded;
	} else {
		tables_first = loaded;
	}

	/* The new table is the tail the next one is appended after. */
	tables_last = loaded;

	/* Runs the table's term list at the root. */
	kern_memset(&eval, 0, sizeof(eval));
	eval.position = loaded->data + TABLE_HEADER_SIZE;
	eval.end = loaded->data + loaded->length;
	eval.scope = drv_acpi_root();
	eval.table = loaded;
	drv_acpi_stack_begin(__builtin_frame_address(0));
	error = drv_acpi_exec_term_list(&eval);
	drv_acpi_stack_begin(NULL);
	drv_acpi_object_release(eval.return_value);
	if (error != 0) {
		drv_acpi_os_log(
			"ACPI: %s %s stopped at offset 0x%lx (error %d)\n",
			loaded->signature,
			loaded->oem_table_id,
			(unsigned long)(eval.position - loaded->data),
			error);
		return error;
	}

	/* Succeeded. */
	return 0;
}

/*
 * Creates the namespace with its predefined names, once.
 */
int
drv_acpi_initialize_namespace(void)
{
	struct drv_acpi_node *root;
	int error;

	/* Nothing to do once the root exists. */
	root = drv_acpi_root();
	if (root != NULL)
		return 0;

	/* Creates the root and the predefined names. */
	error = drv_acpi_ns_init();
	if (error != 0)
		return error;

	/* Adds \_OSI, which is written in C. */
	error = drv_acpi_osi_install();
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Prepares the objects of the loaded tables: evaluates the place of every
 * operation region, as firmware expects once all tables are in.
 */
int
drv_acpi_initialize_objects(void)
{
	int error;

	/* Walks the whole namespace. */
	drv_acpi_stack_begin(__builtin_frame_address(0));
	error = drv_acpi_walk(NULL, prepare_visitor, NULL);
	drv_acpi_stack_begin(NULL);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Forgets every table and the whole namespace, for the host tests that
 * start the interpreter over.
 */
void
drv_acpi_reset(void)
{
	struct drv_acpi_table *table;

	/* Deletes the namespace first; it points into the tables. */
	drv_acpi_ns_reset();
	drv_acpi_region_reset();

	/* Frees every table. */
	while (tables_first != NULL) {
		table = tables_first;
		tables_first = table->next;
		drv_acpi_os_free(table->data);
		drv_acpi_os_free(table);
	}

	/* The list is empty, and integers are 64 bits until the next DSDT. */
	tables_last = NULL;
	drv_acpi_integer_set_width(64);
}

/*
 * Reports the oldest loaded table.
 */
struct drv_acpi_table *
drv_acpi_table_first(void)
{
	/* The list starts here. */
	return tables_first;
}

/*
 * Runs Load, LoadTable or Unload.
 */
int
drv_acpi_table_operator(
	struct drv_acpi_eval *eval,
	unsigned opcode,
	struct drv_acpi_object **result)
{
	UNUSED_PARAMETER(eval);
	UNUSED_PARAMETER(result);

	/* The dynamic table operators come with the table source of the kernel. */
	drv_acpi_os_log("ACPI: opcode 0x%x (dynamic tables) is not supported yet\n", opcode);
	return ENOTSUP;
}

/* Checks a definition block's header and checksum. */
static int
table_check(
	const uint8_t *data,
	size_t length)
{
	uint32_t declared;
	uint8_t sum;
	size_t index;

	/* Refuses a table shorter than its header. */
	if (length < TABLE_HEADER_SIZE)
		return EINVAL;

	/* Refuses a declared length that does not match. */
	declared = (uint32_t)data[4];
	declared |= (uint32_t)data[5] << 8;
	declared |= (uint32_t)data[6] << 16;
	declared |= (uint32_t)data[7] << 24;
	if (declared != length || declared > TABLE_SIZE_MAX)
		return EINVAL;

	/* Sums every byte; a wrong checksum is logged and tolerated, as elsewhere. */
	sum = 0;
	for (index = 0; index < length; index++)
		sum = (uint8_t)(sum + data[index]);
	if (sum != 0)
		drv_acpi_os_log("ACPI: table %.4s has a wrong checksum\n", (const char *)data);

	/* Succeeded. */
	return 0;
}

/* Copies the identifying fields out of a table's header. */
static void
table_identify(
	struct drv_acpi_table *table)
{
	/* The signature, the revision and the OEM identifiers. */
	kern_memcpy(table->signature, table->data, 4);
	table->signature[4] = '\0';
	table->revision = table->data[8];
	kern_memcpy(table->oem_id, table->data + 10, 6);
	table->oem_id[6] = '\0';
	kern_memcpy(table->oem_table_id, table->data + 16, 8);
	table->oem_table_id[8] = '\0';
}

/* Prepares one node's object after the tables are loaded. */
static int
prepare_visitor(
	struct drv_acpi_node *node,
	unsigned depth,
	void *argument)
{
	int error;

	UNUSED_PARAMETER(depth);
	UNUSED_PARAMETER(argument);

	/* Only regions have anything to prepare. */
	if (node->object == NULL || node->object->type != DRV_ACPI_TYPE_REGION)
		return 0;

	/* A region whose place cannot be evaluated stays unprepared until used. */
	error = drv_acpi_region_prepare(node->object);
	if (error != 0)
		return 0;

	/* Succeeded: goes on with the walk. */
	return 0;
}
