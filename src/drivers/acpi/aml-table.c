/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Definition blocks: loading the DSDT and the SSDTs into the namespace,
 * preparing the objects and initializing the devices after the load
 * (ACPI 6.5 section 6.5.1), and the Load, LoadTable and Unload operators.
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
 * The _STA bits the device initialization reads.
 */
#define STATUS_PRESENT		0x01U
#define STATUS_FUNCTIONING	0x08U

/*
 * The status of a device that has no _STA: present, enabled, shown and
 * functioning.
 */
#define STATUS_DEFAULT		0x0fU

/*
 * The longest string operand of LoadTable.
 */
#define LOAD_STRING_MAX		128U

/*
 * The loaded tables, oldest first.
 *
 * table_install() appends to the list, Unload takes a table out, and
 * drv_acpi_reset() empties it.  Method bodies point into the tables, so a
 * table stays as long as the names it created.  The interpreter lock
 * serializes every change.
 */
static struct drv_acpi_table *tables_first;
static struct drv_acpi_table *tables_last;

/*
 * The identifier the next loaded table gets.  It starts at 1 because 0
 * marks the predefined nodes, and it only grows.
 */
static uint32_t tables_next_id = 1;

/*
 * The operands of LoadTable, as strings, and its parameter data.
 */
struct load_table_request {
	char signature[LOAD_STRING_MAX];
	char oem_id[LOAD_STRING_MAX];
	char oem_table_id[LOAD_STRING_MAX];
	char root_path[LOAD_STRING_MAX];
	char parameter_path[LOAD_STRING_MAX];
	struct drv_acpi_object *parameter;
};

static int table_install(struct drv_acpi_eval *caller, const uint8_t *data, size_t length, struct drv_acpi_node *scope, struct drv_acpi_table **result);
static int table_check(const uint8_t *data, size_t length);
static void table_identify(struct drv_acpi_table *table);
static struct drv_acpi_table *table_loaded(const char *signature, const char *oem_id, const char *oem_table_id);
static int prepare_visitor(struct drv_acpi_node *node, unsigned depth, void *argument);
static void initialize_children(struct drv_acpi_node *parent, struct drv_acpi_node *skip_ini);
static void initialize_device(struct drv_acpi_node *node, struct drv_acpi_node *skip_ini);
static bool has_child(struct drv_acpi_node *node, const char *name);
static int op_load(struct drv_acpi_eval *eval, struct drv_acpi_object **result);
static int load_source(struct drv_acpi_eval *eval, struct drv_acpi_node *node, uint8_t **data, size_t *length);
static int op_load_table(struct drv_acpi_eval *eval, struct drv_acpi_object **result);
static int load_table_operands(struct drv_acpi_eval *eval, struct load_table_request *request);
static int load_table_install(struct drv_acpi_eval *eval, struct load_table_request *request, struct drv_acpi_object **result);
static int string_argument(struct drv_acpi_eval *eval, char *text, size_t size);
static int op_unload(struct drv_acpi_eval *eval, struct drv_acpi_object **result);
static void delete_owned(struct drv_acpi_node *node, uint32_t owner);
static void table_remove(struct drv_acpi_table *table);
static int handle_object(struct drv_acpi_table *table, struct drv_acpi_object **result);

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
	struct drv_acpi_thread storage;
	struct drv_acpi_thread *thread;
	struct drv_acpi_table *loaded;
	struct drv_acpi_eval entry;
	int error;

	/* Creates the namespace on first use. */
	error = drv_acpi_initialize_namespace();
	if (error != 0)
		return error;

	/* Loads the table at the root as one entry into the interpreter. */
	thread = drv_acpi_enter(&storage, __builtin_frame_address(0));
	kern_memset(&entry, 0, sizeof(entry));
	entry.thread = thread;
	error = table_install(&entry, table, length, drv_acpi_root(), &loaded);
	drv_acpi_leave(thread);
	if (error != 0)
		return error;

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
 * Prepares the objects of the loaded tables, as firmware expects once all
 * tables are in: evaluates the place of every operation region, and
 * resolves the names in packages that were defined after the package.
 */
int
drv_acpi_initialize_objects(void)
{
	struct drv_acpi_thread storage;
	struct drv_acpi_thread *thread;
	int error;

	/* Walks the whole namespace inside one entry. */
	thread = drv_acpi_enter(&storage, __builtin_frame_address(0));
	error = drv_acpi_walk(NULL, prepare_visitor, NULL);
	drv_acpi_leave(thread);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Initializes the devices (ACPI 6.5 section 6.5.1): runs \_INI and
 * \_SB._INI, then walks the devices, processors and thermal zones parents
 * first.  A device whose _STA says it is present runs its _INI and has its
 * children initialized; one that is only functioning has its children
 * initialized; one that is neither is skipped with everything below it.
 */
void
drv_acpi_initialize_devices(void)
{
	struct drv_acpi_thread storage;
	struct drv_acpi_thread *thread;
	struct drv_acpi_object *result;
	struct drv_acpi_node *system_bus;
	int error;

	/* Runs the whole initialization as one entry. */
	thread = drv_acpi_enter(&storage, __builtin_frame_address(0));

	/* Runs the global \_INI when firmware has one. */
	result = NULL;
	error = drv_acpi_evaluate(NULL, "\\_INI", NULL, 0, &result);
	drv_acpi_object_release(result);
	if (error != 0 && error != ENOENT)
		drv_acpi_os_log("ACPI: \\_INI failed (error %d)\n", error);

	/* Runs \_SB._INI before any other device's. */
	result = NULL;
	system_bus = NULL;
	error = drv_acpi_lookup(NULL, "\\_SB_", &system_bus);
	if (error == 0) {
		error = drv_acpi_evaluate(NULL, "\\_SB_._INI", NULL, 0, &result);
		drv_acpi_object_release(result);
		if (error != 0 && error != ENOENT)
			drv_acpi_os_log("ACPI: \\_SB._INI failed (error %d)\n", error);
	}

	/* Walks the devices below the root; \_SB's _INI has run already. */
	initialize_children(drv_acpi_root(), system_bus);
	drv_acpi_leave(thread);
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
	int error;

	/* Chooses the operator. */
	switch (opcode) {
	case DRV_ACPI_OP_LOAD:
		error = op_load(eval, result);
		break;
	case DRV_ACPI_OP_LOAD_TABLE:
		error = op_load_table(eval, result);
		break;
	default:
		error = op_unload(eval, result);
		break;
	}

	/* Reports a failed operator. */
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/*
 * Copies a definition block, adds it to the list and runs its term list
 * in a scope.
 */
static int
table_install(
	struct drv_acpi_eval *caller,
	const uint8_t *data,
	size_t length,
	struct drv_acpi_node *scope,
	struct drv_acpi_table **result)
{
	struct drv_acpi_table *loaded;
	struct drv_acpi_eval eval;
	int compared;
	int error;

	/* Checks the header before trusting the length. */
	error = table_check(data, length);
	if (error != 0)
		return error;

	/* Allocates the record. */
	loaded = drv_acpi_os_alloc(sizeof(*loaded));
	if (loaded == NULL)
		return ENOMEM;
	kern_memset(loaded, 0, sizeof(*loaded));

	/* Allocates the copy of the table. */
	loaded->length = length;
	loaded->data = drv_acpi_os_alloc(length);
	if (loaded->data == NULL) {
		drv_acpi_os_free(loaded);
		return ENOMEM;
	}

	/* Copies the bytes and gives the table the next identifier. */
	kern_memcpy(loaded->data, data, length);
	table_identify(loaded);
	loaded->id = tables_next_id;
	tables_next_id++;

	/* The DSDT's revision sets the integer width of every table. */
	compared = kern_strcmp(loaded->signature, "DSDT");
	if (compared == 0 && loaded->revision < 2) {
		drv_acpi_integer_set_width(32);
	} else if (compared == 0) {
		drv_acpi_integer_set_width(64);
	}

	/* Appends it to the list before running it, so it can find itself. */
	if (tables_last != NULL) {
		tables_last->next = loaded;
	} else {
		tables_first = loaded;
	}

	/* The new table is the tail the next one is appended after. */
	tables_last = loaded;

	/* Runs the table's term list in the scope, as the caller's thread. */
	kern_memset(&eval, 0, sizeof(eval));
	eval.position = loaded->data + TABLE_HEADER_SIZE;
	eval.end = loaded->data + loaded->length;
	eval.scope = scope;
	eval.table = loaded;
	eval.thread = drv_acpi_eval_thread(caller);
	error = drv_acpi_exec_term_list(&eval);
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
	*result = loaded;
	return 0;
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

/* Finds a loaded table by its signature and OEM identifiers. */
static struct drv_acpi_table *
table_loaded(
	const char *signature,
	const char *oem_id,
	const char *oem_table_id)
{
	struct drv_acpi_table *table;
	int compared;

	/* Compares each loaded table's identifiers. */
	for (table = tables_first; table != NULL; table = table->next) {
		/* Skips a table with another signature. */
		compared = kern_strncmp(table->signature, signature, 4);
		if (compared != 0)
			continue;

		/* Skips a table with another OEM ID. */
		compared = kern_strncmp(table->oem_id, oem_id, 6);
		if (compared != 0)
			continue;

		/* Skips a table with another OEM table ID. */
		compared = kern_strncmp(table->oem_table_id, oem_table_id, 8);
		if (compared != 0)
			continue;

		/* Reports the loaded table. */
		return table;
	}

	/* Reports that no loaded table has the identifiers. */
	return NULL;
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

	/* A package resolves the names that were not there when it was built. */
	if (node->object != NULL && node->object->type == DRV_ACPI_TYPE_PACKAGE) {
		drv_acpi_package_resolve(node->object);
		return 0;
	}

	/* Only regions have anything else to prepare. */
	if (node->object == NULL || node->object->type != DRV_ACPI_TYPE_REGION)
		return 0;

	/* A region whose place cannot be evaluated stays unprepared until used. */
	error = drv_acpi_region_prepare(node->object);
	if (error != 0)
		return 0;

	/* Succeeded: goes on with the walk. */
	return 0;
}

/* Initializes the devices among a node's children and below them. */
static void
initialize_children(
	struct drv_acpi_node *parent,
	struct drv_acpi_node *skip_ini)
{
	struct drv_acpi_node *child;
	enum drv_acpi_type type;

	/* Visits each child in creation order. */
	for (child = parent->child; child != NULL; child = child->next) {
		type = drv_acpi_node_type(child);

		/* A device, a processor or a thermal zone is initialized by its status. */
		if (type == DRV_ACPI_TYPE_DEVICE ||
		    type == DRV_ACPI_TYPE_PROCESSOR ||
		    type == DRV_ACPI_TYPE_THERMAL_ZONE) {
			initialize_device(child, skip_ini);
			continue;
		}

		/* Anything else may still hold devices below it. */
		initialize_children(child, skip_ini);
	}
}

/* Initializes one device by its status, then the devices below it. */
static void
initialize_device(
	struct drv_acpi_node *node,
	struct drv_acpi_node *skip_ini)
{
	struct drv_acpi_object *result;
	uint64_t status;
	bool exists;
	int error;

	/* Reads the status; a device without _STA is present and functioning. */
	status = STATUS_DEFAULT;
	exists = has_child(node, "_STA");
	if (exists) {
		error = drv_acpi_evaluate_integer(node, "_STA", &status);
		if (error != 0) {
			drv_acpi_os_log("ACPI: _STA failed (error %d)\n", error);
			status = STATUS_DEFAULT;
		}
	}

	/* A device neither present nor functioning is skipped with its children. */
	if ((status & (STATUS_PRESENT | STATUS_FUNCTIONING)) == 0)
		return;

	/* A present device runs its _INI, unless it ran already. */
	exists = has_child(node, "_INI");
	if ((status & STATUS_PRESENT) != 0 &&
	    exists &&
	    node != skip_ini) {
		result = NULL;
		error = drv_acpi_evaluate(node, "_INI", NULL, 0, &result);
		drv_acpi_object_release(result);
		if (error != 0)
			drv_acpi_os_log("ACPI: _INI failed (error %d)\n", error);
	}

	/* Initializes the devices below it. */
	initialize_children(node, skip_ini);
}

/* Reports whether a node has a child of one name. */
static bool
has_child(
	struct drv_acpi_node *node,
	const char *name)
{
	struct drv_acpi_node *found;
	int error;

	/* Looks the name up from the node; only its own child counts. */
	error = drv_acpi_lookup(node, name, &found);
	if (error != 0)
		return false;
	if (found->parent != node)
		return false;

	/* Reports that the node has it. */
	return true;
}

/*
 * Runs a Load: loads the definition block an operation region, a field
 * unit or a buffer holds, and stores whether it loaded into the target
 * (Ones or zero, as ACPI 6.4 made the result a boolean).
 */
static int
op_load(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *outcome;
	struct drv_acpi_target target;
	struct drv_acpi_table *table;
	struct drv_acpi_name name;
	struct drv_acpi_node *node;
	uint8_t *data;
	size_t length;
	int error;

	/* Resolves the object that holds the table. */
	error = drv_acpi_stream_name(eval, &name);
	if (error != 0)
		return error;
	error = drv_acpi_ns_lookup(eval->scope, &name, true, &node);
	if (error != 0)
		return error;
	node = drv_acpi_ns_resolve_alias(node);

	/* Parses where the outcome goes. */
	error = drv_acpi_parse_target(eval, &target);
	if (error != 0)
		return error;

	/* Reads the table's bytes and loads them at the root. */
	error = load_source(eval, node, &data, &length);
	if (error == 0) {
		error = table_install(eval, data, length, drv_acpi_root(), &table);
		drv_acpi_os_free(data);
	}

	/* The outcome is Ones for a loaded table and zero for one that failed. */
	outcome = drv_acpi_object_integer_new(0);
	if (outcome == NULL) {
		drv_acpi_target_release(&target);
		return ENOMEM;
	}

	/* True is Ones at the integer width. */
	if (error == 0)
		outcome->value.integer = drv_acpi_integer_mask();

	/* Stores it. */
	error = drv_acpi_store(eval, outcome, &target);
	drv_acpi_target_release(&target);
	if (error != 0) {
		drv_acpi_object_release(outcome);
		return error;
	}

	/* Succeeded: the value of Load is its outcome. */
	*result = outcome;
	return 0;
}

/* Reads the bytes of the table a Load names, into memory the caller frees. */
static int
load_source(
	struct drv_acpi_eval *eval,
	struct drv_acpi_node *node,
	uint8_t **data,
	size_t *length)
{
	struct drv_acpi_object *object;
	struct drv_acpi_object *value;
	uint8_t header[TABLE_HEADER_SIZE];
	uint8_t *bytes;
	size_t size;
	int error;

	/* A region is read through its handler: the header first, for the length. */
	object = node->object;
	if (object != NULL && object->type == DRV_ACPI_TYPE_REGION) {
		error = drv_acpi_region_read(eval, object, 0, sizeof(header), header);
		if (error != 0)
			return error;

		/* Refuses a length the region cannot hold. */
		size = (size_t)header[4] | (size_t)header[5] << 8 | (size_t)header[6] << 16 | (size_t)header[7] << 24;
		if (size < TABLE_HEADER_SIZE ||
		    size > TABLE_SIZE_MAX ||
		    size > object->value.region.length)
			return EINVAL;

		/* Reads the whole table. */
		bytes = drv_acpi_os_alloc(size);
		if (bytes == NULL)
			return ENOMEM;
		error = drv_acpi_region_read(eval, object, 0, size, bytes);
		if (error != 0) {
			drv_acpi_os_free(bytes);
			return error;
		}

		/* Succeeded. */
		*data = bytes;
		*length = size;
		return 0;
	}

	/* A field unit or a buffer gives its value as a buffer. */
	error = drv_acpi_read_node(eval, node, &value);
	if (error != 0)
		return error;
	if (value->type != DRV_ACPI_TYPE_BUFFER || value->value.buffer.length < TABLE_HEADER_SIZE) {
		drv_acpi_object_release(value);
		return EINVAL;
	}

	/* The declared length must fit in the buffer. */
	bytes = value->value.buffer.bytes;
	size = (size_t)bytes[4] | (size_t)bytes[5] << 8 | (size_t)bytes[6] << 16 | (size_t)bytes[7] << 24;
	if (size < TABLE_HEADER_SIZE || size > value->value.buffer.length) {
		drv_acpi_object_release(value);
		return EINVAL;
	}

	/* Copies the table out. */
	*data = drv_acpi_os_alloc(size);
	if (*data == NULL) {
		drv_acpi_object_release(value);
		return ENOMEM;
	}

	/* The copy outlives the buffer, which may change. */
	kern_memcpy(*data, bytes, size);
	drv_acpi_object_release(value);

	/* Succeeded. */
	*length = size;
	return 0;
}

/*
 * Runs a LoadTable: loads a table the firmware lists but did not load,
 * found by its identifiers, at a path, and stores a parameter.  A table
 * that cannot be found gives zero.
 */
static int
op_load_table(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object **result)
{
	struct load_table_request *request;
	int error;

	/* The request lives on the heap: its strings would weigh on the AML stack. */
	request = drv_acpi_os_alloc(sizeof(*request));
	if (request == NULL)
		return ENOMEM;
	kern_memset(request, 0, sizeof(*request));

	/* Evaluates the six operands, then loads the table and reports its handle. */
	error = load_table_operands(eval, request);
	if (error == 0)
		error = load_table_install(eval, request, result);
	drv_acpi_object_release(request->parameter);
	drv_acpi_os_free(request);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Evaluates the operands of a LoadTable. */
static int
load_table_operands(
	struct drv_acpi_eval *eval,
	struct load_table_request *request)
{
	int error;

	/* The signature. */
	error = string_argument(eval, request->signature, sizeof(request->signature));
	if (error != 0)
		return error;

	/* The OEM ID. */
	error = string_argument(eval, request->oem_id, sizeof(request->oem_id));
	if (error != 0)
		return error;

	/* The OEM table ID. */
	error = string_argument(eval, request->oem_table_id, sizeof(request->oem_table_id));
	if (error != 0)
		return error;

	/* The path the table is loaded at. */
	error = string_argument(eval, request->root_path, sizeof(request->root_path));
	if (error != 0)
		return error;

	/* The path the parameter is stored at. */
	error = string_argument(eval, request->parameter_path, sizeof(request->parameter_path));
	if (error != 0)
		return error;

	/* The parameter. */
	error = drv_acpi_eval_data(eval, &request->parameter);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Finds, loads and parameterizes the table a LoadTable asks for. */
static int
load_table_install(
	struct drv_acpi_eval *eval,
	struct load_table_request *request,
	struct drv_acpi_object **result)
{
	struct drv_acpi_target target;
	struct drv_acpi_table *table;
	struct drv_acpi_node *scope;
	const uint8_t *data;
	size_t length;
	int error;

	/* A table the firmware does not list gives zero. */
	error = drv_acpi_os_table(request->signature, request->oem_id, request->oem_table_id, &data, &length);
	if (error != 0) {
		*result = drv_acpi_object_integer_new(0);
		if (*result == NULL)
			return ENOMEM;
		return 0;
	}

	/* Refuses a table that is loaded already. */
	table = table_loaded((const char *)data, (const char *)data + 10, (const char *)data + 16);
	if (table != NULL) {
		drv_acpi_os_log("ACPI: LoadTable of %s %s, which is loaded already\n", table->signature, table->oem_table_id);
		return EEXIST;
	}

	/* Finds the scope the table is loaded at: the root unless a path is given. */
	scope = drv_acpi_root();
	if (request->root_path[0] != '\0') {
		error = drv_acpi_lookup(eval->scope, request->root_path, &scope);
		if (error != 0)
			return error;
	}

	/* Loads the table there. */
	error = table_install(eval, data, length, scope, &table);
	if (error != 0)
		return error;

	/* Stores the parameter at its path, found from the table's scope. */
	if (request->parameter_path[0] != '\0') {
		kern_memset(&target, 0, sizeof(target));
		target.kind = DRV_ACPI_TARGET_NODE;
		error = drv_acpi_lookup(scope, request->parameter_path, &target.node);
		if (error == 0)
			error = drv_acpi_store(eval, request->parameter, &target);
		if (error != 0)
			return error;
	}

	/* Makes the handle. */
	error = handle_object(table, result);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Evaluates a TermArg that must be a string and copies it out. */
static int
string_argument(
	struct drv_acpi_eval *eval,
	char *text,
	size_t size)
{
	struct drv_acpi_object *object;
	size_t length;
	int error;

	/* Evaluates the operand. */
	error = drv_acpi_eval_data(eval, &object);
	if (error != 0)
		return error;

	/* Refuses anything but a string. */
	if (object->type != DRV_ACPI_TYPE_STRING) {
		drv_acpi_object_release(object);
		return EINVAL;
	}

	/* Copies as much as fits, terminated. */
	length = object->value.string.length;
	if (length >= size)
		length = size - 1U;
	kern_memcpy(text, object->value.string.text, length);
	text[length] = '\0';
	drv_acpi_object_release(object);

	/* Succeeded. */
	return 0;
}

/*
 * Runs an Unload: deletes every name the table of a handle created and
 * forgets the table.
 */
static int
op_unload(
	struct drv_acpi_eval *eval,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *handle;
	struct drv_acpi_target target;
	struct drv_acpi_table *table;
	int error;

	/* Reads the handle. */
	error = drv_acpi_parse_target(eval, &target);
	if (error != 0)
		return error;
	handle = NULL;
	if (target.kind == DRV_ACPI_TARGET_LOCAL) {
		handle = eval->frame->locals[target.index];
	} else if (target.kind == DRV_ACPI_TARGET_ARGUMENT) {
		handle = eval->frame->arguments[target.index];
	} else if (target.kind == DRV_ACPI_TARGET_NODE) {
		handle = drv_acpi_ns_resolve_alias(target.node)->object;
	}

	/* The handle stays in its slot or node; the target is done with. */
	drv_acpi_target_release(&target);

	/* Refuses anything but a handle of a loaded table. */
	if (handle == NULL || handle->type != DRV_ACPI_TYPE_DDB_HANDLE)
		return EINVAL;
	table = handle->value.ddb.table;
	if (table == NULL)
		return EINVAL;

	/* Deletes the table's names; the handle no longer names a table. */
	delete_owned(drv_acpi_root(), table->id);
	handle->value.ddb.table = NULL;
	table_remove(table);

	/* Succeeded: Unload has no value of its own. */
	*result = drv_acpi_object_integer_new(0);
	if (*result == NULL)
		return ENOMEM;
	return 0;
}

/* Deletes every node below a node that one table created. */
static void
delete_owned(
	struct drv_acpi_node *node,
	uint32_t owner)
{
	struct drv_acpi_node *child;
	struct drv_acpi_node *next;

	/* Visits each child; the next one is read first because a child may go. */
	for (child = node->child; child != NULL; child = next) {
		next = child->next;

		/* Deletes a node of the table with everything below it. */
		if (child->owner == owner) {
			drv_acpi_ns_delete(child);
			continue;
		}

		/* Looks below a node of another table. */
		delete_owned(child, owner);
	}
}

/* Takes a table out of the list and frees it. */
static void
table_remove(
	struct drv_acpi_table *table)
{
	struct drv_acpi_table *previous;
	struct drv_acpi_table *walk;

	/* Finds the table before it. */
	previous = NULL;
	for (walk = tables_first; walk != NULL && walk != table; walk = walk->next)
		previous = walk;

	/* Joins its neighbors. */
	if (previous != NULL) {
		previous->next = table->next;
	} else {
		tables_first = table->next;
	}

	/* Moves the tail back when the table was the last. */
	if (tables_last == table)
		tables_last = previous;

	/* Frees it. */
	drv_acpi_os_free(table->data);
	drv_acpi_os_free(table);
}

/* Makes the DDB handle object of a loaded table. */
static int
handle_object(
	struct drv_acpi_table *table,
	struct drv_acpi_object **result)
{
	struct drv_acpi_object *handle;

	/* Allocates the handle. */
	handle = drv_acpi_object_new(DRV_ACPI_TYPE_DDB_HANDLE);
	if (handle == NULL)
		return ENOMEM;

	/* Succeeded: it names the table. */
	handle->value.ddb.table = table;
	*result = handle;
	return 0;
}
