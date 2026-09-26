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
 *     --methods       evaluate every method that takes no arguments, in
 *                     namespace order (the paths are listed first)
 *     --main          evaluate \MAIN and exit with 0 when it returned 0
 *     --notify PATH   print the notifications PATH receives (repeatable)
 *     --dynamic FILE  a table LoadTable may load, not loaded at start
 *     --firmware FILE load the tables from a simulated physical memory
 *                     (make-firmware.py): RSDP, XSDT, FADT, DSDT, SSDTs
 *     --reg           connect the address spaces (run _REG) after loading
 *     --init          initialize the devices (_INI by _STA) after loading
 *     --shared-pci    simulate one PCI configuration space for every
 *                     function, as acpiexec does (it keeps one buffer per
 *                     region address, and PCI regions all start at 0)
 *     --events        read the event hardware from the FADT (the firmware's,
 *                     or a simulated q35-like one) and enable the GPEs
 *     --ec            attach the Embedded Controller (PNP0C09)
 *     --ec-ram A=V    preset byte A of the simulated EC (repeatable)
 *     --gpe N         raise GPE N and handle the SCI (repeatable, in order)
 *     --power-button  press the fixed power button and handle the SCI
 *     --ec-query Q@G  queue EC query Q, raise the EC's GPE G, handle the SCI
 *     --global-lock   share a simulated FACS Global Lock with a simulated
 *                     firmware (with --events); after MAIN, print the lock
 *                     and handle the SCI firmware raised
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

#include "drivers/acpi/acpi-tables.h"
#include "drivers/acpi/acpi-text.h"
#include "drivers/acpi/aml-internal.h"
#include "drivers/acpi/aml-os.h"

#include "aml-host-hardware.h"

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
	OPTION_METHODS,
	OPTION_MAIN,
	OPTION_NOTIFY,
	OPTION_DYNAMIC,
	OPTION_FIRMWARE,
	OPTION_STACK,
	OPTION_SHARED_PCI,
	OPTION_EVENTS,
	OPTION_EC,
	OPTION_EC_RAM,
	OPTION_GPE,
	OPTION_POWER_BUTTON,
	OPTION_EC_QUERY,
	OPTION_GLOBAL_LOCK,
	OPTION_REG,
	OPTION_INIT,
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
	const char *firmware;
	unsigned table_count;
	unsigned evaluation_count;
	unsigned notified_count;
	unsigned dynamic_count;
	int dump;
	int devices;
	int methods;
	int run_main;
	int stack;
	int connect;
	int initialize;
	int events;
	int ec;
	int global_lock;
	const char *actions[OPTION_LIST_MAX];
	enum option_kind action_kinds[OPTION_LIST_MAX];
	unsigned action_count;
};

/*
 * One table LoadTable may load, read from a file at start.
 */
struct dynamic_table {
	uint8_t *data;
	size_t length;
};

/*
 * One piece of the simulated physical memory of --firmware: a file's bytes
 * at an address.
 */
struct memory_piece {
	uint64_t address;
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
	{ "--methods", OPTION_METHODS, 0 },
	{ "--main", OPTION_MAIN, 0 },
	{ "--notify", OPTION_NOTIFY, 1 },
	{ "--dynamic", OPTION_DYNAMIC, 1 },
	{ "--firmware", OPTION_FIRMWARE, 1 },
	{ "--stack", OPTION_STACK, 0 },
	{ "--shared-pci", OPTION_SHARED_PCI, 0 },
	{ "--events", OPTION_EVENTS, 0 },
	{ "--ec", OPTION_EC, 0 },
	{ "--ec-ram", OPTION_EC_RAM, 1 },
	{ "--gpe", OPTION_GPE, 1 },
	{ "--power-button", OPTION_POWER_BUTTON, 0 },
	{ "--ec-query", OPTION_EC_QUERY, 1 },
	{ "--global-lock", OPTION_GLOBAL_LOCK, 0 },
	{ "--reg", OPTION_REG, 0 },
	{ "--init", OPTION_INIT, 0 },
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
 * Whether every PCI function shares one simulated configuration space.
 */
static int shared_pci;

/*
 * The simulated physical memory of --firmware, and the tables found in it.
 * Both live until the harness exits.
 */
static struct memory_piece memory_pieces[OPTION_LIST_MAX];
static unsigned memory_piece_count;
static struct drv_acpi_firmware firmware;
static int firmware_loaded;

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

/*
 * Whether the interpreter lock is held.  The harness has one thread, so
 * the lock only checks that the interpreter takes and lets it go in pairs.
 */
static int lock_held;

static int parse_arguments(int argc, char **argv, struct harness_options *options);
static enum option_kind option_of(const char *text, int *takes_value);
static int install_spaces(const struct harness_options *options);
static int start_events(const struct harness_options *options);
static void run_actions(const struct harness_options *options);
static void print_fixed_event(enum drv_acpi_fixed_event event, void *argument);
static int load_tables(const struct harness_options *options);
static int install_notifications(const struct harness_options *options);
static int run_main(void);
static void print_sci(void);
static int read_file(const char *path, uint8_t **data, size_t *length);
static int load_file(const char *path);
static int load_firmware(const char *description);
static int read_memory(uint64_t address, void *buffer, size_t length, void *argument);
static int simulated_space(const struct drv_acpi_region_access *access, uint64_t *value, void *argument);
static uint64_t space_key(const struct drv_acpi_region_access *access, unsigned space);
static uint8_t *page_byte(uint64_t space, uint64_t address);
static int devices_visitor(struct drv_acpi_node *node, unsigned depth, void *argument);
static void print_notification(struct drv_acpi_node *node, uint32_t value, void *argument);
static int evaluate_and_print(struct drv_acpi_node *scope, const char *path, const char *label);
static int print_namespace(void);
static int evaluate_methods(void);
static int method_visitor(struct drv_acpi_node *node, unsigned depth, void *argument);

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
	error = install_spaces(&options);
	if (error != 0)
		return 2;

	/* Loads the tables and prepares their objects as the kernel does. */
	status = load_tables(&options);
	error = drv_acpi_initialize_objects();
	if (error != 0)
		fprintf(stderr, "initialize_objects: error %d\n", error);

	/* Connects the address spaces as the kernel does, when asked to. */
	if (options.connect) {
		error = drv_acpi_region_connect_all();
		if (error != 0)
			status = 1;
	}

	/* Initializes the devices as the kernel does, when asked to. */
	if (options.initialize)
		drv_acpi_initialize_devices();

	/* Installs the notification printers. */
	error = install_notifications(&options);
	if (error != 0)
		status = 1;

	/* Starts the events and the EC, then plays the hardware actions. */
	error = start_events(&options);
	if (error != 0)
		status = 1;
	run_actions(&options);

	/* Prints the namespace. */
	if (options.dump)
		print_namespace();

	/* Runs an ASL test's MAIN first, which returns 0 when every check passed. */
	if (options.run_main) {
		error = run_main();
		if (error != 0)
			status = 1;
	}

	/* Prints the Global Lock and handles the SCI its firmware raised. */
	if (options.global_lock) {
		printf("GLOBAL-LOCK 0x%x\n", (unsigned)*hardware_global_lock());
		print_sci();
		drv_acpi_events_process();
		print_sci();
	}

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

	/* Evaluates every method that takes no arguments. */
	if (options.methods) {
		error = evaluate_methods();
		if (error != 0)
			status = 1;
	}

	/* Reports how deep the interpreter went. */
	if (options.stack)
		printf("stack deepest %zu bytes\n", drv_acpi_stack_deepest());

	/* Frees everything so that the leak checker sees a clean exit. */
	drv_acpi_reset();
	if (firmware_loaded)
		drv_acpi_firmware_release(&firmware);
	for (index = 0; index < memory_piece_count; index++)
		free(memory_pieces[index].data);
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
	/* Moves the clock without waiting; the simulated firmware runs meanwhile. */
	slept += milliseconds * 10000U;
	hardware_tick();
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
 * Reads a port: the simulated hardware, or plain memory elsewhere.
 */
int
drv_acpi_os_port_read(
	uint32_t port,
	unsigned width,
	uint32_t *value)
{
	uint8_t *byte;
	unsigned index;
	int modelled;

	/* The simulated hardware answers for its ports. */
	modelled = hardware_port(port, width, false, value);
	if (modelled)
		return 0;

	/* Anything else is the plain memory of the I/O space. */
	*value = 0;
	for (index = 0; index < width / 8U; index++) {
		/* Reads one byte. */
		byte = page_byte(DRV_ACPI_SPACE_SYSTEM_IO, port + index);
		if (byte == NULL)
			return 12;
		*value |= (uint32_t)*byte << (index * 8U);
	}

	/* Succeeded. */
	return 0;
}

/*
 * Writes a port: the simulated hardware, or plain memory elsewhere.
 */
int
drv_acpi_os_port_write(
	uint32_t port,
	unsigned width,
	uint32_t value)
{
	uint8_t *byte;
	unsigned index;
	int modelled;

	/* The simulated hardware answers for its ports. */
	modelled = hardware_port(port, width, true, &value);
	if (modelled)
		return 0;

	/* Anything else is the plain memory of the I/O space. */
	for (index = 0; index < width / 8U; index++) {
		/* Writes one byte. */
		byte = page_byte(DRV_ACPI_SPACE_SYSTEM_IO, port + index);
		if (byte == NULL)
			return 12;
		*byte = (uint8_t)(value >> (index * 8U));
	}

	/* Succeeded. */
	return 0;
}

/*
 * Takes the event lock; the harness has no interrupt to keep out.
 */
unsigned long
drv_acpi_os_event_lock(void)
{
	/* Nothing to take. */
	return 0;
}

/*
 * Lets the event lock go.
 */
void
drv_acpi_os_event_unlock(
	unsigned long state)
{
	UNUSED_PARAMETER(state);
}

/*
 * Takes the interpreter lock.
 */
void
drv_acpi_os_lock(void)
{
	/* A second take without a let-go is a bug in the interpreter. */
	if (lock_held) {
		fprintf(stderr, "aml-host: interpreter lock taken twice\n");
		abort();
	}

	/* Holds it. */
	lock_held = 1;
}

/*
 * Lets the interpreter lock go.
 */
void
drv_acpi_os_unlock(void)
{
	/* A let-go without a take is a bug in the interpreter. */
	if (!lock_held) {
		fprintf(stderr, "aml-host: interpreter lock let go twice\n");
		abort();
	}

	/* Lets it go. */
	lock_held = 0;
}

/*
 * Reports whether this thread holds the interpreter lock.
 */
bool
drv_acpi_os_lock_owned(void)
{
	/* The harness's one thread holds it whenever it is held. */
	if (lock_held)
		return true;
	return false;
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

	/* The firmware's tables come first when the tables were loaded from it. */
	if (firmware_loaded) {
		compared = drv_acpi_firmware_find(&firmware, signature, oem_id, oem_table_id, data, length);
		if (compared == 0)
			return 0;
	}

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
		case OPTION_METHODS:
			options->methods = 1;
			break;
		case OPTION_MAIN:
			options->run_main = 1;
			break;
		case OPTION_STACK:
			options->stack = 1;
			break;
		case OPTION_SHARED_PCI:
			shared_pci = 1;
			break;
		case OPTION_EVENTS:
			options->events = 1;
			break;
		case OPTION_EC:
			options->ec = 1;
			break;
		case OPTION_GLOBAL_LOCK:
			options->global_lock = 1;
			break;
		case OPTION_EC_RAM:
		case OPTION_GPE:
		case OPTION_POWER_BUTTON:
		case OPTION_EC_QUERY:
			/* The hardware actions run in the order given, after the loading. */
			options->actions[options->action_count % OPTION_LIST_MAX] = value;
			options->action_kinds[options->action_count % OPTION_LIST_MAX] = kind;
			options->action_count++;
			break;
		case OPTION_REG:
			options->connect = 1;
			break;
		case OPTION_INIT:
			options->initialize = 1;
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
		case OPTION_FIRMWARE:
			options->firmware = value;
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
install_spaces(
	const struct harness_options *options)
{
	unsigned space;
	int error;

	/* Every space is plain memory here, as acpiexec simulates it. */
	for (space = 0; space < DRV_ACPI_SPACE_COUNT; space++) {
		/* The EC's space is left to the EC driver when it attaches. */
		if (options->ec && space == DRV_ACPI_SPACE_EMBEDDED_CONTROL)
			continue;

		/* Installs one space. */
		error = drv_acpi_region_install((enum drv_acpi_space)space, simulated_space, (void *)(uintptr_t)space);
		if (error != 0)
			return error;
	}

	/* Succeeded. */
	return 0;
}

/*
 * Reads the event hardware from the firmware's FADT or a simulated one,
 * and attaches the EC, when the options ask.
 */
static int
start_events(
	const struct harness_options *options)
{
	uint8_t fadt[512];
	const uint8_t *table;
	size_t length;
	unsigned index;
	int address;
	int value;
	int fields;
	int error;

	/* Presets the EC's bytes first. */
	for (index = 0; index < options->action_count; index++) {
		/* Only the --ec-ram actions preset. */
		if (options->action_kinds[index] != OPTION_EC_RAM)
			continue;

		/* Stores the byte an "address=value" names. */
		fields = sscanf(options->actions[index], "%i=%i", &address, &value);
		if (fields == 2)
			hardware_ec_ram((uint8_t)address, (uint8_t)value);
	}

	/* Reads the event hardware. */
	if (options->events) {
		table = fadt;
		length = hardware_default_fadt(fadt, sizeof(fadt));
		if (firmware_loaded && firmware.fadt != NULL) {
			table = firmware.fadt;
			length = firmware.fadt_length;
		}

		/* Starts the events from it. */
		error = drv_acpi_events_init(table, length);
		if (error != 0) {
			fprintf(stderr, "events: initialization failed (error %d)\n", error);
			return error;
		}

		/* Prints the power button's presses. */
		error = drv_acpi_fixed_event_install(DRV_ACPI_EVENT_POWER_BUTTON, print_fixed_event, NULL);
		if (error != 0)
			fprintf(stderr, "events: no fixed power button (error %d)\n", error);
	}

	/* Shares the simulated FACS Global Lock. */
	if (options->global_lock) {
		error = drv_acpi_global_lock_attach(hardware_global_lock());
		if (error != 0)
			return error;
	}

	/* Attaches the EC. */
	if (options->ec) {
		error = drv_acpi_ec_attach();
		if (error != 0) {
			fprintf(stderr, "ec: attach failed (error %d)\n", error);
			return error;
		}
	}

	/* Succeeded. */
	return 0;
}

/* Prints whether the SCI has events pending, as the interrupt would find them. */
static void
print_sci(void)
{
	bool pending;

	/* Runs the interrupt's part and prints what it found. */
	pending = drv_acpi_sci_interrupt();
	if (pending) {
		printf("SCI pending\n");
	} else {
		printf("SCI none\n");
	}
}

/* Raises the events the options ask for, in order, and handles each SCI. */
static void
run_actions(
	const struct harness_options *options)
{
	unsigned index;
	int query;
	int gpe;
	int fields;
	bool pending;

	/* Plays each action. */
	for (index = 0; index < options->action_count; index++) {
		/* Raises the event in the simulated hardware. */
		switch (options->action_kinds[index]) {
		case OPTION_GPE:
			hardware_raise_gpe((unsigned)strtoul(options->actions[index], NULL, 0));
			break;
		case OPTION_POWER_BUTTON:
			hardware_raise_fixed(DRV_ACPI_EVENT_POWER_BUTTON);
			break;
		case OPTION_EC_QUERY:
			/* A "query@gpe" queues the query and raises the EC's GPE. */
			fields = sscanf(options->actions[index], "%i@%i", &query, &gpe);
			if (fields != 2)
				continue;
			hardware_ec_query((uint8_t)query);
			hardware_raise_gpe((unsigned)gpe);
			break;
		default:
			continue;
		}

		/* Handles the SCI as the kernel's interrupt and thread would. */
		pending = drv_acpi_sci_interrupt();
		if (!pending) {
			printf("SCI none\n");
			continue;
		}

		/* The thread's part runs the handlers. */
		printf("SCI pending\n");
		drv_acpi_events_process();
	}
}

/* Prints a fixed event. */
static void
print_fixed_event(
	enum drv_acpi_fixed_event event,
	void *argument)
{
	UNUSED_PARAMETER(argument);

	/* Prints its name. */
	if (event == DRV_ACPI_EVENT_POWER_BUTTON) {
		printf("FIXED power-button\n");
		return;
	}

	/* Any other fixed event by its number. */
	printf("FIXED %u\n", (unsigned)event);
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

	/* Loads the firmware's tables from the simulated memory. */
	if (options->firmware != NULL) {
		error = load_firmware(options->firmware);
		if (error != 0)
			status = 1;
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

/*
 * Reads the description of a simulated physical memory, then finds and
 * loads the tables in it as the kernel does.
 */
static int
load_firmware(
	const char *description)
{
	struct memory_piece *piece;
	unsigned long long address;
	unsigned long long rsdp;
	char line[1024];
	char path[1024];
	char *got;
	FILE *stream;
	int fields;
	int error;

	/* Opens the description. */
	stream = fopen(description, "r");
	if (stream == NULL) {
		perror(description);
		return 1;
	}

	/* Reads the RSDP's address and each piece of memory, a line at a time. */
	rsdp = 0;
	for (;;) {
		/* Stops at the end of the description. */
		got = fgets(line, sizeof(line), stream);
		if (got == NULL)
			break;

		/* The RSDP line. */
		fields = sscanf(line, "rsdp %llx", &rsdp);
		if (fields == 1)
			continue;

		/* A piece: an address and the file whose bytes are there. */
		fields = sscanf(line, "%llx %1023s", &address, path);
		if (fields != 2 || memory_piece_count == OPTION_LIST_MAX)
			continue;
		piece = &memory_pieces[memory_piece_count];
		error = read_file(path, &piece->data, &piece->length);
		if (error != 0)
			continue;
		piece->address = address;
		memory_piece_count++;
	}

	/* The description is read. */
	fclose(stream);

	/* Finds the tables from the RSDP. */
	error = drv_acpi_firmware_discover(rsdp, read_memory, NULL, &firmware);
	if (error != 0) {
		fprintf(stderr, "%s: no ACPI tables found (error %d)\n", description, error);
		return 1;
	}

	/* The record is released at exit and serves LoadTable meanwhile. */
	firmware_loaded = 1;

	/* Loads the DSDT and the SSDTs. */
	error = drv_acpi_firmware_load(&firmware);
	if (error != 0) {
		fprintf(stderr, "%s: the DSDT did not load (error %d)\n", description, error);
		return 1;
	}

	/* Succeeded. */
	return 0;
}

/* Reads the simulated physical memory; a range outside every piece cannot be read. */
static int
read_memory(
	uint64_t address,
	void *buffer,
	size_t length,
	void *argument)
{
	struct memory_piece *piece;
	unsigned index;

	UNUSED_PARAMETER(argument);

	/* Finds the piece that holds the whole range. */
	for (index = 0; index < memory_piece_count; index++) {
		piece = &memory_pieces[index];

		/* Skips a piece that does not hold it. */
		if (address < piece->address || address - piece->address > piece->length)
			continue;
		if (length > piece->length - (size_t)(address - piece->address))
			continue;

		/* Copies the bytes. */
		memcpy(buffer, piece->data + (address - piece->address), length);
		return 0;
	}

	/* Reports memory that is not there. */
	return 14;
}

/* Reads or writes a simulated address space. */
static int
simulated_space(
	const struct drv_acpi_region_access *access,
	uint64_t *value,
	void *argument)
{
	uint64_t space;
	uint32_t port_value;
	unsigned bytes;
	unsigned index;
	uint8_t *byte;
	int modelled;

	/* The simulated hardware answers for its I/O ports. */
	if ((uintptr_t)argument == DRV_ACPI_SPACE_SYSTEM_IO) {
		port_value = (uint32_t)*value;
		modelled = hardware_port((uint32_t)access->address, access->width, access->write, &port_value);
		if (modelled) {
			if (!access->write)
				*value = port_value;
			return 0;
		}
	}

	/* The argument is the number of the space the handler was installed for. */
	space = space_key(access, (unsigned)(uintptr_t)argument);

	/* A read starts from zero. */
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

/*
 * Keeps every address space apart, and every PCI function in a space of
 * its own unless --shared-pci asked for one for all.
 */
static uint64_t
space_key(
	const struct drv_acpi_region_access *access,
	unsigned space)
{
	uint64_t function;

	/* A space other than PCI configuration is keyed by its number alone. */
	if (space != DRV_ACPI_SPACE_PCI_CONFIG)
		return space;

	/* One configuration space for every function, as acpiexec simulates it. */
	if (shared_pci)
		return DRV_ACPI_SPACE_PCI_CONFIG;

	/* Combines the segment, bus, device and function. */
	function = (uint64_t)access->pci_segment << 16;
	function |= (uint64_t)access->pci_bus << 8;
	function |= (uint64_t)access->pci_device << 3;
	function |= access->pci_function;

	/* Leaves room below for the numbers of the spaces. */
	return (function + 1U) << 8;
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

/*
 * The paths of the methods --methods evaluates, collected before any runs,
 * because a method may load or unload tables and change the namespace.
 */
struct method_list {
	char **paths;
	size_t count;
	size_t capacity;
};

/* Lists every method without arguments, then evaluates each by its path. */
static int
evaluate_methods(void)
{
	struct method_list list;
	size_t index;

	/* Lists the methods. */
	memset(&list, 0, sizeof(list));
	drv_acpi_walk(NULL, method_visitor, &list);

	/* Evaluates each; a failure is printed and the next one runs. */
	for (index = 0; index < list.count; index++) {
		/* Evaluates one method. */
		evaluate_and_print(NULL, list.paths[index], list.paths[index]);
		free(list.paths[index]);
	}

	/* Frees the list. */
	free(list.paths);
	return 0;
}

/* Adds a method without arguments to the list. */
static int
method_visitor(
	struct drv_acpi_node *node,
	unsigned depth,
	void *argument)
{
	struct method_list *list;
	char path[PATH_MAX_LENGTH];
	char **grown;
	int error;

	UNUSED_PARAMETER(depth);

	/* Only methods without arguments are listed. */
	list = argument;
	if (node->object == NULL || node->object->type != DRV_ACPI_TYPE_METHOD)
		return 0;
	if (node->object->value.method.argument_count != 0)
		return 0;

	/* Writes the path. */
	error = drv_acpi_node_path(node, path, sizeof(path));
	if (error != 0)
		return 0;

	/* Grows the list when it is full. */
	if (list->count == list->capacity) {
		list->capacity = list->capacity * 2U + 64U;
		grown = realloc(list->paths, list->capacity * sizeof(list->paths[0]));
		if (grown == NULL)
			return -1;
		list->paths = grown;
	}

	/* Keeps a copy of the path. */
	list->paths[list->count] = strdup(path);
	if (list->paths[list->count] == NULL)
		return -1;
	list->count++;

	/* Goes on with the walk. */
	return 0;
}

/* Evaluates one path and prints the result under a label, as /dev/acpi writes it. */
static int
evaluate_and_print(
	struct drv_acpi_node *scope,
	const char *path,
	const char *label)
{
	struct drv_acpi_text text;
	int error;

	/* Writes the result line. */
	drv_acpi_text_init(&text);
	error = drv_acpi_text_evaluate(&text, scope, path, label);

	/* Prints it. */
	if (text.data != NULL)
		fputs(text.data, stdout);
	drv_acpi_text_release(&text);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Prints the namespace, one line per node, as /dev/acpi writes it. */
static int
print_namespace(void)
{
	struct drv_acpi_text text;
	int error;

	/* Writes the lines. */
	drv_acpi_text_init(&text);
	error = drv_acpi_text_namespace(&text);

	/* Prints them. */
	if (text.data != NULL)
		fputs(text.data, stdout);
	drv_acpi_text_release(&text);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
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

