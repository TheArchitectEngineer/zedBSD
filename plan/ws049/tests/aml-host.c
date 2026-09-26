/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test harness of the AML interpreter (WS049).
 *
 * It compiles src/drivers/acpi/aml-*.c with the host compiler, implements
 * aml-os.h over the host C library, and simulates the address spaces the
 * way acpiexec does: every byte reads as zero until something writes it.
 *
 *   aml-host [options] TABLE...
 *     --dump          print the namespace, one normalized line per node
 *     --eval PATH     evaluate PATH and print the result (repeatable)
 *     --devices       evaluate _HID _CID _UID _ADR _STA _CRS of every device
 *     --main          evaluate \MAIN and exit with 0 when it returned 0
 *     --notify PATH   print the notifications PATH receives (repeatable)
 *     --dynamic FILE  a table LoadTable may load, not loaded at start
 *     --stack         print the deepest stack use of the interpreter
 *     --budget BYTES  the stack budget (default 1 MiB)
 *     --quiet         do not print the interpreter's log
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <drivers/acpi/acpi.h>

#include "drivers/acpi/aml-internal.h"
#include "drivers/acpi/aml-os.h"

/*
 * The size of one simulated memory page.
 */
#define PAGE_SIZE 4096U

/*
 * The number of hash buckets of simulated pages.
 */
#define PAGE_BUCKETS 1024U

/*
 * The longest path the harness prints.
 */
#define PATH_MAX_LENGTH 512U

/*
 * How many of each repeatable option the harness keeps.
 */
#define OPTION_LIST_MAX 64U

/*
 * The options the harness understands.
 */
enum option_kind {
	OPTION_TABLE = 0,
	OPTION_DUMP,
	OPTION_EVAL,
	OPTION_DEVICES,
	OPTION_MAIN,
	OPTION_NOTIFY,
	OPTION_DYNAMIC,
	OPTION_STACK,
	OPTION_BUDGET,
	OPTION_QUIET
};

/*
 * One option name and whether a value follows it.
 */
struct option_name {
	const char *text;
	enum option_kind kind;
	int takes_value;
};

/*
 * What the command line asked for.
 */
struct harness_options {
	const char *tables[OPTION_LIST_MAX];
	const char *evaluations[OPTION_LIST_MAX];
	const char *notified[OPTION_LIST_MAX];
	const char *dynamic[OPTION_LIST_MAX];
	unsigned table_count;
	unsigned evaluation_count;
	unsigned notified_count;
	unsigned dynamic_count;
	int dump;
	int devices;
	int run_main;
	int stack;
};

/*
 * One table LoadTable may load, read from a file at start.
 */
struct dynamic_table {
	uint8_t *data;
	size_t length;
};

/*
 * One simulated page of an address space: zeros until written.
 */
struct page {
	uint64_t space;
	uint64_t number;
	uint8_t bytes[PAGE_SIZE];
	struct page *next;
};

/*
 * The option names.
 */
static const struct option_name option_names[] = {
	{ "--dump", OPTION_DUMP, 0 },
	{ "--eval", OPTION_EVAL, 1 },
	{ "--devices", OPTION_DEVICES, 0 },
	{ "--main", OPTION_MAIN, 0 },
	{ "--notify", OPTION_NOTIFY, 1 },
	{ "--dynamic", OPTION_DYNAMIC, 1 },
	{ "--stack", OPTION_STACK, 0 },
	{ "--budget", OPTION_BUDGET, 1 },
	{ "--quiet", OPTION_QUIET, 0 },
};

/*
 * The simulated pages of every space, by the hash of space and number.
 * Pages are created on the first access and live until the harness exits.
 */
static struct page *pages[PAGE_BUCKETS];

/*
 * The tables LoadTable may load, filled from --dynamic at start.
 */
static struct dynamic_table dynamic_tables[OPTION_LIST_MAX];
static unsigned dynamic_table_count;

/*
 * Whether the interpreter's log is printed.
 */
static int log_enabled = 1;

/*
 * The stack budget the interpreter is given.
 */
static size_t stack_budget = 1024U * 1024U;

/*
 * The simulated time Sleep and Stall have added, in 100-nanosecond units.
 */
static uint64_t slept;

static int parse_arguments(int argc, char **argv, struct harness_options *options);
static enum option_kind option_of(const char *text, int *takes_value);
static int install_spaces(void);
static int load_tables(const struct harness_options *options);
static int install_notifications(const struct harness_options *options);
static int run_main(void);
static int read_file(const char *path, uint8_t **data, size_t *length);
static int load_file(const char *path);
static int simulated_space(const struct drv_acpi_region_access *access, uint64_t *value, void *argument);
static uint64_t space_key(const struct drv_acpi_region_access *access);
static uint8_t *page_byte(uint64_t space, uint64_t address);
static int dump_visitor(struct drv_acpi_node *node, unsigned depth, void *argument);
static int devices_visitor(struct drv_acpi_node *node, unsigned depth, void *argument);
static void print_node_line(struct drv_acpi_node *node);
static void print_field(const char *path, const struct drv_acpi_object *object);
static void print_object(const struct drv_acpi_object *object);
static void print_package(const struct drv_acpi_object *object);
static void print_reference(const struct drv_acpi_object *object);
static void print_notification(struct drv_acpi_node *node, uint32_t value, void *argument);
static const char *space_name(unsigned space);
static void segment_name(const struct drv_acpi_node *node, char *text);
static int evaluate_and_print(struct drv_acpi_node *scope, const char *path, const char *label);

/*
 * Runs the harness.
 */
int
main(
	int argc,
	char **argv)
{
	struct harness_options options;
	unsigned index;
	int error;
	int status;

	/* Reads the options. */
	error = parse_arguments(argc, argv, &options);
	if (error != 0)
		return 2;

	/* Installs the simulated spaces before any AML runs. */
	error = install_spaces();
	if (error != 0)
		return 2;

	/* Loads the tables and prepares their objects as the kernel does. */
	status = load_tables(&options);
	error = drv_acpi_initialize_objects();
	if (error != 0)
		fprintf(stderr, "initialize_objects: error %d\n", error);

	/* Installs the notification printers. */
	error = install_notifications(&options);
	if (error != 0)
		status = 1;

	/* Prints the namespace. */
	if (options.dump)
		drv_acpi_walk(NULL, dump_visitor, NULL);

	/* Evaluates the paths the options named, in order. */
	for (index = 0; index < options.evaluation_count; index++) {
		/* Evaluates one path. */
		error = evaluate_and_print(NULL, options.evaluations[index], options.evaluations[index]);
		if (error != 0)
			status = 1;
	}

	/* Evaluates the identification objects of every device. */
	if (options.devices)
		drv_acpi_walk(NULL, devices_visitor, NULL);

	/* Runs an ASL test's MAIN, which returns 0 when every check passed. */
	if (options.run_main) {
		error = run_main();
		if (error != 0)
			status = 1;
	}

	/* Reports how deep the interpreter went. */
	if (options.stack)
		printf("stack deepest %zu bytes\n", drv_acpi_stack_deepest());

	/* Frees everything so that the leak checker sees a clean exit. */
	drv_acpi_reset();
	for (index = 0; index < dynamic_table_count; index++)
		free(dynamic_tables[index].data);

	/* Reports the outcome. */
	return status;
}

/*
 * Allocates interpreter memory from the host heap.
 */
void *
drv_acpi_os_alloc(
	size_t size)
{
	void *pointer;

	/* A zero-size request still gets a unique pointer. */
	if (size == 0)
		size = 1;

	/* Allocates it. */
	pointer = malloc(size);

	/* Reports the memory, or NULL. */
	return pointer;
}

/*
 * Frees interpreter memory.
 */
void
drv_acpi_os_free(
	void *pointer)
{
	/* Frees it. */
	free(pointer);
}

/*
 * Prints a line of the interpreter's log to standard error.
 */
void
drv_acpi_os_log(
	const char *format,
	...)
{
	va_list arguments;

	/* Drops the log when asked to be quiet. */
	if (!log_enabled)
		return;

	/* Prints it. */
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
}

/*
 * Reports the stack budget.
 */
size_t
drv_acpi_os_stack_budget(void)
{
	/* Reports the configured budget. */
	return stack_budget;
}

/*
 * Simulates a sleep by moving the simulated clock.
 */
void
drv_acpi_os_sleep(
	uint64_t milliseconds)
{
	/* Moves the clock without waiting. */
	slept += milliseconds * 10000U;
}

/*
 * Simulates a stall by moving the simulated clock.
 */
void
drv_acpi_os_stall(
	uint64_t microseconds)
{
	/* Moves the clock without waiting. */
	slept += microseconds * 10U;
}

/*
 * Reads the clock in 100-nanosecond units: host time plus simulated sleeps.
 */
uint64_t
drv_acpi_os_timer(void)
{
	struct timespec now;
	uint64_t ticks;

	/* Reads the host's monotonic clock. */
	clock_gettime(CLOCK_MONOTONIC, &now);

	/* Converts it and adds the simulated time. */
	ticks = (uint64_t)now.tv_sec * 10000000U + (uint64_t)now.tv_nsec / 100U + slept;
	return ticks;
}

/*
 * Reports the identity of the evaluating thread; the harness has one.
 */
const void *
drv_acpi_os_thread(void)
{
	/* Any fixed non-NULL address will do. */
	return &slept;
}

/*
 * Finds a table LoadTable asks for among the --dynamic tables.
 */
int
drv_acpi_os_table(
	const char *signature,
	const char *oem_id,
	const char *oem_table_id,
	const uint8_t **data,
	size_t *length)
{
	struct dynamic_table *table;
	unsigned index;
	int compared;

	/* Compares each table's header; an empty identifier matches any. */
	for (index = 0; index < dynamic_table_count; index++) {
		table = &dynamic_tables[index];

		/* Skips a table with another signature. */
		compared = memcmp(table->data, signature, 4);
		if (compared != 0)
			continue;

		/* Skips a table with another OEM ID. */
		compared = strncmp((const char *)table->data + 10, oem_id, 6);
		if (oem_id[0] != '\0' && compared != 0)
			continue;

		/* Skips a table with another OEM table ID. */
		compared = strncmp((const char *)table->data + 16, oem_table_id, 8);
		if (oem_table_id[0] != '\0' && compared != 0)
			continue;

		/* Reports the matching table. */
		*data = table->data;
		*length = table->length;
		return 0;
	}

	/* Reports that no table matches. */
	return 2;
}

/* Reads the command line into the options. */
static int
parse_arguments(
	int argc,
	char **argv,
	struct harness_options *options)
{
	enum option_kind kind;
	const char *value;
	int takes_value;
	int index;

	/* Starts with nothing asked for. */
	memset(options, 0, sizeof(*options));

	/* Reads each argument. */
	for (index = 1; index < argc; index++) {
		/* Finds what the argument is and takes its value. */
		kind = option_of(argv[index], &takes_value);
		value = argv[index];
		if (takes_value) {
			/* Refuses an option whose value is missing. */
			if (index + 1 >= argc) {
				fprintf(stderr, "%s needs a value\n", argv[index]);
				return 1;
			}

			/* The value is the next argument. */
			index++;
			value = argv[index];
		}

		/* Records it. */
		switch (kind) {
		case OPTION_DUMP:
			options->dump = 1;
			break;
		case OPTION_DEVICES:
			options->devices = 1;
			break;
		case OPTION_MAIN:
			options->run_main = 1;
			break;
		case OPTION_STACK:
			options->stack = 1;
			break;
		case OPTION_QUIET:
			log_enabled = 0;
			break;
		case OPTION_BUDGET:
			stack_budget = (size_t)strtoul(value, NULL, 0);
			break;
		case OPTION_EVAL:
			options->evaluations[options->evaluation_count % OPTION_LIST_MAX] = value;
			options->evaluation_count++;
			break;
		case OPTION_NOTIFY:
			options->notified[options->notified_count % OPTION_LIST_MAX] = value;
			options->notified_count++;
			break;
		case OPTION_DYNAMIC:
			options->dynamic[options->dynamic_count % OPTION_LIST_MAX] = value;
			options->dynamic_count++;
			break;
		default:
			options->tables[options->table_count % OPTION_LIST_MAX] = value;
			options->table_count++;
			break;
		}
	}

	/* Refuses more of a repeatable option than the harness keeps. */
	if (options->table_count > OPTION_LIST_MAX || options->evaluation_count > OPTION_LIST_MAX)
		return 1;
	if (options->notified_count > OPTION_LIST_MAX || options->dynamic_count > OPTION_LIST_MAX)
		return 1;

	/* Succeeded. */
	return 0;
}

/* Reports which option an argument is, or a table when it is none. */
static enum option_kind
option_of(
	const char *text,
	int *takes_value)
{
	size_t index;
	int compared;

	/* Compares the argument with each option name. */
	*takes_value = 0;
	for (index = 0; index < sizeof(option_names) / sizeof(option_names[0]); index++) {
		/* Reports the matching option. */
		compared = strcmp(text, option_names[index].text);
		if (compared == 0) {
			*takes_value = option_names[index].takes_value;
			return option_names[index].kind;
		}
	}

	/* Anything else is a table file. */
	return OPTION_TABLE;
}

/* Installs the simulated handler for every address space. */
static int
install_spaces(void)
{
	unsigned space;
	int error;

	/* Every space is plain memory here, as acpiexec simulates it. */
	for (space = 0; space < DRV_ACPI_SPACE_COUNT; space++) {
		/* Installs one space. */
		error = drv_acpi_region_install((enum drv_acpi_space)space, simulated_space, NULL);
		if (error != 0)
			return error;
	}

	/* Succeeded. */
	return 0;
}

/* Reads the dynamic tables and loads the others in order. */
static int
load_tables(
	const struct harness_options *options)
{
	struct dynamic_table *table;
	unsigned index;
	int error;
	int status;

	/* Reads each table LoadTable may load. */
	status = 0;
	for (index = 0; index < options->dynamic_count; index++) {
		/* Reads one. */
		table = &dynamic_tables[dynamic_table_count];
		error = read_file(options->dynamic[index], &table->data, &table->length);
		if (error != 0) {
			status = 1;
			continue;
		}

		/* Keeps it only when it has a whole header. */
		if (table->length < 36U) {
			free(table->data);
			status = 1;
			continue;
		}

		/* Counts it. */
		dynamic_table_count++;
	}

	/* Loads each table in order. */
	for (index = 0; index < options->table_count; index++) {
		/* Loads one. */
		error = load_file(options->tables[index]);
		if (error != 0)
			status = 1;
	}

	/* Reports whether every table loaded. */
	return status;
}

/* Installs a printer of notifications on each node the options named. */
static int
install_notifications(
	const struct harness_options *options)
{
	struct drv_acpi_node *node;
	unsigned index;
	int error;

	/* Resolves each path and installs the printer. */
	for (index = 0; index < options->notified_count; index++) {
		/* Resolves one path. */
		error = drv_acpi_lookup(NULL, options->notified[index], &node);
		if (error != 0) {
			fprintf(stderr, "%s: no such node\n", options->notified[index]);
			return error;
		}

		/* Installs the printer. */
		error = drv_acpi_notify_install(node, print_notification, NULL);
		if (error != 0)
			return error;
	}

	/* Succeeded. */
	return 0;
}

/* Runs \MAIN and reports whether it returned zero. */
static int
run_main(void)
{
	struct drv_acpi_object *result;
	uint64_t value;
	int error;

	/* Evaluates it. */
	error = drv_acpi_evaluate(NULL, "\\MAIN", NULL, 0, &result);
	if (error != 0) {
		printf("MAIN failed: error %d\n", error);
		return 1;
	}

	/* Checks what it returned: zero is success, anything else the failing check. */
	value = drv_acpi_object_integer(result);
	drv_acpi_object_release(result);
	if (value != 0) {
		printf("MAIN returned 0x%llx\n", (unsigned long long)value);
		return 1;
	}

	/* Succeeded. */
	printf("MAIN passed\n");
	return 0;
}

/* Reads a whole file into memory the caller frees. */
static int
read_file(
	const char *path,
	uint8_t **data,
	size_t *length)
{
	FILE *stream;
	uint8_t *bytes;
	long size;
	size_t count;

	/* Opens the file. */
	stream = fopen(path, "rb");
	if (stream == NULL) {
		perror(path);
		return 1;
	}

	/* Measures it. */
	fseek(stream, 0, SEEK_END);
	size = ftell(stream);
	fseek(stream, 0, SEEK_SET);
	if (size < 0) {
		fclose(stream);
		return 1;
	}

	/* Allocates room for it. */
	bytes = malloc((size_t)size + 1U);
	if (bytes == NULL) {
		fclose(stream);
		return 1;
	}

	/* Reads it whole. */
	count = fread(bytes, 1, (size_t)size, stream);
	fclose(stream);
	if (count != (size_t)size) {
		free(bytes);
		return 1;
	}

	/* Succeeded. */
	*data = bytes;
	*length = (size_t)size;
	return 0;
}

/* Reads a table file and loads it. */
static int
load_file(
	const char *path)
{
	uint8_t *data;
	size_t length;
	int error;

	/* Reads the file. */
	error = read_file(path, &data, &length);
	if (error != 0)
		return error;

	/* Loads it. */
	error = drv_acpi_load_table(data, length);
	free(data);
	if (error != 0) {
		fprintf(stderr, "%s: load error %d\n", path, error);
		return 1;
	}

	/* Succeeded. */
	return 0;
}

/* Reads or writes a simulated address space. */
static int
simulated_space(
	const struct drv_acpi_region_access *access,
	uint64_t *value,
	void *argument)
{
	uint64_t space;
	unsigned bytes;
	unsigned index;
	uint8_t *byte;

	UNUSED_PARAMETER(argument);

	/* A read starts from zero. */
	space = space_key(access);
	bytes = access->width / 8U;
	if (!access->write)
		*value = 0;

	/* Moves the bytes lowest first. */
	for (index = 0; index < bytes; index++) {
		/* Finds the simulated byte. */
		byte = page_byte(space, access->address + index);
		if (byte == NULL)
			return 12;

		/* Stores or loads it. */
		if (access->write) {
			*byte = (uint8_t)(*value >> (index * 8U));
		} else {
			*value |= (uint64_t)*byte << (index * 8U);
		}
	}

	/* Succeeded. */
	return 0;
}

/* Keeps every PCI function in a space of its own, and every space apart. */
static uint64_t
space_key(
	const struct drv_acpi_region_access *access)
{
	uint64_t function;

	/* Combines the segment, bus, device and function. */
	function = (uint64_t)access->pci_segment << 16;
	function |= (uint64_t)access->pci_bus << 8;
	function |= (uint64_t)access->pci_device << 3;
	function |= access->pci_function;

	/* Leaves room below for the plain spaces. */
	return function << 8;
}

/* Finds the simulated byte at an address, creating its page. */
static uint8_t *
page_byte(
	uint64_t space,
	uint64_t address)
{
	struct page *page;
	uint64_t number;
	unsigned bucket;

	/* Finds the page in its bucket. */
	number = address / PAGE_SIZE;
	bucket = (unsigned)((number * 31U + space) % PAGE_BUCKETS);
	for (page = pages[bucket]; page != NULL; page = page->next) {
		/* Stops at the page of this space and number. */
		if (page->space == space && page->number == number)
			return &page->bytes[address % PAGE_SIZE];
	}

	/* Creates it, zero-filled. */
	page = calloc(1, sizeof(*page));
	if (page == NULL)
		return NULL;
	page->space = space;
	page->number = number;
	page->next = pages[bucket];
	pages[bucket] = page;

	/* Reports the byte. */
	return &page->bytes[address % PAGE_SIZE];
}

/* Prints one node of the namespace. */
static int
dump_visitor(
	struct drv_acpi_node *node,
	unsigned depth,
	void *argument)
{
	UNUSED_PARAMETER(depth);
	UNUSED_PARAMETER(argument);

	/* Prints the line and goes on into the children. */
	print_node_line(node);
	return 0;
}

/* Prints one node as "path type attributes", the form acpiexec-namespace.py writes. */
static void
print_node_line(
	struct drv_acpi_node *node)
{
	const struct drv_acpi_object *object;
	char path[PATH_MAX_LENGTH];
	char name[5];
	int error;

	/* Writes the path. */
	error = drv_acpi_node_path(node, path, sizeof(path));
	if (error != 0)
		strcpy(path, "(long)");

	/* A node without an object is untyped. */
	object = node->object;
	if (object == NULL) {
		printf("%s Untyped\n", path);
		return;
	}

	/* Writes the type and what identifies the object. */
	switch (object->type) {
	case DRV_ACPI_TYPE_SCOPE:
		printf("%s Scope\n", path);
		break;
	case DRV_ACPI_TYPE_DEVICE:
		printf("%s Device\n", path);
		break;
	case DRV_ACPI_TYPE_THERMAL_ZONE:
		printf("%s Thermal\n", path);
		break;
	case DRV_ACPI_TYPE_POWER_RESOURCE:
		printf("%s Power\n", path);
		break;
	case DRV_ACPI_TYPE_EVENT:
		printf("%s Event\n", path);
		break;
	case DRV_ACPI_TYPE_MUTEX:
		printf("%s Mutex\n", path);
		break;
	case DRV_ACPI_TYPE_PROCESSOR:
		printf("%s Processor id=%02X len=%02X addr=%llX\n",
		       path,
		       object->value.processor.id,
		       object->value.processor.block_length,
		       (unsigned long long)object->value.processor.block_address);
		break;
	case DRV_ACPI_TYPE_INTEGER:
		printf("%s Integer %llX\n", path, (unsigned long long)object->value.integer);
		break;
	case DRV_ACPI_TYPE_STRING:
		printf("%s String \"%s\"\n", path, object->value.string.text);
		break;
	case DRV_ACPI_TYPE_BUFFER:
		printf("%s Buffer len=%zX\n", path, object->value.buffer.length);
		break;
	case DRV_ACPI_TYPE_PACKAGE:
		printf("%s Package count=%X\n", path, (unsigned)object->value.package.count);
		break;
	case DRV_ACPI_TYPE_METHOD:
		printf("%s Method args=%u\n", path, (unsigned)object->value.method.argument_count);
		break;
	case DRV_ACPI_TYPE_REGION:
		printf("%s Region %s addr=%llX len=%llX\n",
		       path,
		       space_name(object->value.region.space),
		       (unsigned long long)object->value.region.offset,
		       (unsigned long long)object->value.region.length);
		break;
	case DRV_ACPI_TYPE_FIELD_UNIT:
		print_field(path, object);
		break;
	case DRV_ACPI_TYPE_BUFFER_FIELD:
		printf("%s BufferField off=%llX len=%llX\n",
		       path,
		       (unsigned long long)object->value.buffer_field.bit_offset,
		       (unsigned long long)object->value.buffer_field.bit_length);
		break;
	case DRV_ACPI_TYPE_ALIAS:
		segment_name(object->value.alias.target, name);
		printf("%s Alias target=%s\n", path, name);
		break;
	default:
		printf("%s Type%u\n", path, (unsigned)object->type);
		break;
	}
}

/* Prints a field unit's line by its kind. */
static void
print_field(
	const char *path,
	const struct drv_acpi_object *object)
{
	const struct drv_acpi_field *field;
	char first[5];
	char second[5];

	/* Names the nodes the field refers to. */
	field = &object->value.field;
	segment_name(field->region, first);
	segment_name(field->index, second);

	/* Writes the line by the kind of field. */
	if (field->kind == DRV_ACPI_FIELD_INDEX) {
		segment_name(field->data, first);
		printf("%s IndexField idx=%s dat=%s off=%X len=%X\n",
		       path, second, first,
		       (unsigned)field->bit_offset,
		       (unsigned)field->bit_length);
	} else if (field->kind == DRV_ACPI_FIELD_BANK) {
		printf("%s BankField rgn=%s bnk=%s off=%X len=%X\n",
		       path, first, second,
		       (unsigned)field->bit_offset,
		       (unsigned)field->bit_length);
	} else {
		printf("%s RegionField rgn=%s off=%X len=%X\n",
		       path, first,
		       (unsigned)field->bit_offset,
		       (unsigned)field->bit_length);
	}
}

/* Evaluates the identification objects of one device. */
static int
devices_visitor(
	struct drv_acpi_node *node,
	unsigned depth,
	void *argument)
{
	static const char *const names[] = { "_HID", "_CID", "_UID", "_ADR", "_STA", "_CRS" };
	struct drv_acpi_node *found;
	char path[PATH_MAX_LENGTH];
	char label[PATH_MAX_LENGTH + 8];
	size_t index;
	int error;

	UNUSED_PARAMETER(depth);
	UNUSED_PARAMETER(argument);

	/* Only devices are visited. */
	if (node->object == NULL || node->object->type != DRV_ACPI_TYPE_DEVICE)
		return 0;

	/* Writes the device's path for the labels. */
	error = drv_acpi_node_path(node, path, sizeof(path));
	if (error != 0)
		return 0;

	/* Evaluates each object the device itself has. */
	for (index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
		/* Skips an object the device does not have. */
		error = drv_acpi_lookup(node, names[index], &found);
		if (error != 0 || found->parent != node)
			continue;

		/* Evaluates it under the label "device.name". */
		snprintf(label, sizeof(label), "%s.%s", path, names[index]);
		evaluate_and_print(node, names[index], label);
	}

	/* Goes on into the children. */
	return 0;
}

/* Evaluates one path and prints the result under a label. */
static int
evaluate_and_print(
	struct drv_acpi_node *scope,
	const char *path,
	const char *label)
{
	struct drv_acpi_object *result;
	int error;

	/* Evaluates it. */
	error = drv_acpi_evaluate(scope, path, NULL, 0, &result);
	if (error != 0) {
		printf("%s = error %d\n", label, error);
		return error;
	}

	/* Prints the result. */
	printf("%s = ", label);
	print_object(result);
	printf("\n");
	drv_acpi_object_release(result);

	/* Succeeded. */
	return 0;
}

/* Prints an object on one line. */
static void
print_object(
	const struct drv_acpi_object *object)
{
	const uint8_t *bytes;
	size_t length;
	size_t index;

	/* A missing object prints as none. */
	if (object == NULL) {
		printf("None");
		return;
	}

	/* Prints by type. */
	switch (drv_acpi_object_type(object)) {
	case DRV_ACPI_TYPE_INTEGER:
		printf("Integer 0x%llX", (unsigned long long)drv_acpi_object_integer(object));
		break;
	case DRV_ACPI_TYPE_STRING:
		printf("String \"%s\"", drv_acpi_object_string(object, NULL));
		break;
	case DRV_ACPI_TYPE_BUFFER:
		/* The length and every byte. */
		bytes = drv_acpi_object_buffer(object, &length);
		printf("Buffer [%zu]", length);
		for (index = 0; index < length; index++)
			printf(" %02X", bytes[index]);
		break;
	case DRV_ACPI_TYPE_PACKAGE:
		print_package(object);
		break;
	case DRV_ACPI_TYPE_REFERENCE:
		print_reference(object);
		break;
	default:
		printf("Type%u", (unsigned)drv_acpi_object_type(object));
		break;
	}
}

/* Prints a package and its elements. */
static void
print_package(
	const struct drv_acpi_object *object)
{
	unsigned count;
	unsigned index;

	/* Prints the count, then each element separated by commas. */
	count = drv_acpi_object_package_count(object);
	printf("Package [%u] {", count);
	for (index = 0; index < count; index++) {
		/* Separates it from the element before. */
		if (index != 0)
			printf(",");

		/* Prints the element. */
		printf(" ");
		print_object(drv_acpi_object_package_element(object, index));
	}

	/* Closes the list. */
	printf(" }");
}

/* Prints a reference by the path of its node. */
static void
print_reference(
	const struct drv_acpi_object *object)
{
	struct drv_acpi_node *node;
	char path[PATH_MAX_LENGTH];
	int error;

	/* A reference to anything but a node has no path to print. */
	node = drv_acpi_object_reference_node(object);
	if (node == NULL) {
		printf("Reference");
		return;
	}

	/* Prints the path. */
	error = drv_acpi_node_path(node, path, sizeof(path));
	if (error != 0)
		strcpy(path, "(long)");
	printf("Reference %s", path);
}

/* Prints a notification a node received. */
static void
print_notification(
	struct drv_acpi_node *node,
	uint32_t value,
	void *argument)
{
	char path[PATH_MAX_LENGTH];
	int error;

	UNUSED_PARAMETER(argument);

	/* Prints the node and the value. */
	error = drv_acpi_node_path(node, path, sizeof(path));
	if (error != 0)
		strcpy(path, "(long)");
	printf("NOTIFY %s 0x%X\n", path, (unsigned)value);
}

/* Names an address space the way acpiexec does. */
static const char *
space_name(
	unsigned space)
{
	static const char *const names[] = {
		"SystemMemory", "SystemIO", "PCI_Config", "EmbeddedControl", "SMBus",
		"SystemCMOS", "PCIBARTarget", "IPMI", "GeneralPurposeIo", "GenericSerialBus",
		"PCC", "PlatformRtMechanism",
	};

	/* Names the spaces ACPI defines. */
	if (space < sizeof(names) / sizeof(names[0]))
		return names[space];

	/* Anything above is an OEM space. */
	return "OEM";
}

/* Writes the four characters of a node's name. */
static void
segment_name(
	const struct drv_acpi_node *node,
	char *text)
{
	/* A missing node has no name. */
	if (node == NULL) {
		strcpy(text, "????");
		return;
	}

	/* Unpacks the name, lowest byte first. */
	text[0] = (char)(node->name & 0xffU);
	text[1] = (char)((node->name >> 8) & 0xffU);
	text[2] = (char)((node->name >> 16) & 0xffU);
	text[3] = (char)((node->name >> 24) & 0xffU);
	text[4] = '\0';
}
