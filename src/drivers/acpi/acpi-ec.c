/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Embedded Controller (ACPI 6.5 section 12): the EmbeddedControl
 * address space its operation regions live in, and the _Qxx queries it
 * raises through its GPE (lid, buttons, battery, thermal events).
 *
 * The EC is the PNP0C09 device of the namespace: its _CRS gives the data
 * port and the command/status port, _GPE its GPE, and _GLK whether the
 * Global Lock guards it.  Every transaction runs under the interpreter
 * lock, which keeps AML and the query handler from mixing their bytes.
 */

#include <kern/kcrt.h>
#include <uapi/errno.h>

#include <drivers/acpi/acpi.h>

#include "aml-internal.h"
#include "aml-os.h"

/*
 * The bits of the EC status register.
 */
#define EC_STATUS_OBF		0x01U
#define EC_STATUS_IBF		0x02U
#define EC_STATUS_SCI_EVT	0x20U

/*
 * The EC commands.
 */
#define EC_COMMAND_READ		0x80U
#define EC_COMMAND_WRITE	0x81U
#define EC_COMMAND_QUERY	0x84U

/*
 * How long the EC may take to empty or fill a buffer, in 10-microsecond
 * polls: half a second, as slow controllers need.
 */
#define EC_POLLS		50000U

/*
 * How many queries one GPE may drain, which bounds an EC that raises
 * SCI_EVT without end.
 */
#define EC_QUERIES_MAX		32U

/*
 * The EISA identifier of PNP0C09 as _HID encodes it.
 */
#define EC_EISA_ID		0x090cd041ULL

/*
 * The resource descriptors _CRS gives the ports with.
 */
#define RESOURCE_IO		0x47U
#define RESOURCE_FIXED_IO	0x4bU
#define RESOURCE_END		0x79U

/*
 * The embedded controller the driver found.
 *
 * drv_acpi_ec_attach() fills it once; attached says it did.  The ports
 * are used only under the interpreter lock.
 */
static struct {
	struct drv_acpi_node *device;
	uint16_t data_port;
	uint16_t command_port;
	unsigned gpe;
	uint8_t global_lock;
	uint8_t has_gpe;
	uint8_t attached;
} ec;

static int find_visitor(struct drv_acpi_node *node, unsigned depth, void *argument);
static bool is_ec(struct drv_acpi_node *node);
static int read_ports(struct drv_acpi_node *device);
static int ec_region(const struct drv_acpi_region_access *access, uint64_t *value, void *argument);
static void ec_gpe(unsigned gpe, void *argument);
static int ec_transaction(uint8_t command, uint8_t address, bool has_address, bool write, uint8_t *data);
static int ec_exchange(uint8_t command, uint8_t address, bool has_address, bool write, uint8_t *data);
static int ec_send(uint16_t port, uint8_t byte);
static int ec_receive(uint8_t *byte);
static int wait_status(uint8_t mask, uint8_t wanted);
static uint8_t read_status(void);
static void run_query(uint8_t query);

/*
 * Finds the embedded controller in the namespace, installs the handler of
 * its address space (which runs its _REG) and the handler of its GPE.
 */
int
drv_acpi_ec_attach(void)
{
	struct drv_acpi_node *found;
	uint64_t value;
	int error;

	/* Finds the PNP0C09 device. */
	found = NULL;
	drv_acpi_walk(NULL, find_visitor, &found);
	if (found == NULL)
		return ENODEV;
	ec.device = found;

	/* Reads its ports from _CRS. */
	error = read_ports(found);
	if (error != 0) {
		drv_acpi_os_log("ACPI: the EC's _CRS gives no ports (error %d)\n", error);
		return error;
	}

	/* Reads its GPE, and whether the Global Lock guards it. */
	error = drv_acpi_evaluate_integer(found, "_GPE", &value);
	if (error == 0) {
		ec.gpe = (unsigned)value;
		ec.has_gpe = 1;
	}

	/* _GLK says whether the firmware shares the EC under the Global Lock. */
	error = drv_acpi_evaluate_integer(found, "_GLK", &value);
	if (error == 0 && value != 0)
		ec.global_lock = 1;
	ec.attached = 1;

	/* Installs the address space; the EC's _REG runs now. */
	error = drv_acpi_region_install(DRV_ACPI_SPACE_EMBEDDED_CONTROL, ec_region, NULL);
	if (error != 0)
		return error;

	/* Installs the GPE handler, which drains the EC's queries. */
	if (ec.has_gpe) {
		error = drv_acpi_gpe_install(ec.gpe, true, ec_gpe, NULL);
		if (error != 0)
			drv_acpi_os_log("ACPI: the EC's GPE 0x%x cannot be handled (error %d)\n", ec.gpe, error);
	}

	/* Succeeded. */
	drv_acpi_os_log("ACPI: EC at ports 0x%x/0x%x, GPE 0x%x\n", ec.data_port, ec.command_port, ec.gpe);
	return 0;
}

/* Stops the walk at the first PNP0C09 device. */
static int
find_visitor(
	struct drv_acpi_node *node,
	unsigned depth,
	void *argument)
{
	struct drv_acpi_node **found;
	bool matched;

	UNUSED_PARAMETER(depth);

	/* Only a device can be the EC. */
	if (node->object == NULL || node->object->type != DRV_ACPI_TYPE_DEVICE)
		return 0;

	/* Stops at the EC. */
	matched = is_ec(node);
	if (!matched)
		return 0;
	found = argument;
	*found = node;
	return -1;
}

/* Reports whether a device's _HID is PNP0C09, as an EISA identifier or a string. */
static bool
is_ec(
	struct drv_acpi_node *node)
{
	struct drv_acpi_object *hid;
	const char *text;
	bool matched;
	int compared;
	int error;

	/* Evaluates the _HID. */
	hid = NULL;
	error = drv_acpi_evaluate(node, "_HID", NULL, 0, &hid);
	if (error != 0 || hid == NULL)
		return false;

	/* Compares it in its form. */
	matched = false;
	if (hid->type == DRV_ACPI_TYPE_INTEGER && hid->value.integer == EC_EISA_ID) {
		matched = true;
	} else if (hid->type == DRV_ACPI_TYPE_STRING) {
		text = hid->value.string.text;
		compared = kern_strcmp(text, "PNP0C09");
		if (compared == 0)
			matched = true;
	}

	/* The _HID is no longer needed. */
	drv_acpi_object_release(hid);

	/* Reports the answer. */
	return matched;
}

/* Reads the data port and the command port from the EC's _CRS. */
static int
read_ports(
	struct drv_acpi_node *device)
{
	struct drv_acpi_object *resources;
	uint16_t ports[2];
	const uint8_t *bytes;
	size_t length;
	size_t offset;
	unsigned count;
	uint8_t tag;
	int error;

	/* Evaluates _CRS. */
	error = drv_acpi_evaluate(device, "_CRS", NULL, 0, &resources);
	if (error != 0)
		return error;
	if (resources == NULL || resources->type != DRV_ACPI_TYPE_BUFFER) {
		drv_acpi_object_release(resources);
		return EINVAL;
	}

	/* The resource template's bytes. */
	bytes = resources->value.buffer.bytes;
	length = resources->value.buffer.length;

	/* Takes the first two I/O ranges: the data port, then the command port. */
	count = 0;
	offset = 0;
	while (offset < length && count < 2U) {
		tag = bytes[offset];

		/* Ends at the end tag or at a large descriptor, which holds no port. */
		if (tag == RESOURCE_END || (tag & 0x80U) != 0)
			break;

		/* Takes the base of an I/O or fixed I/O descriptor. */
		if (tag == RESOURCE_IO && offset + 8U <= length) {
			ports[count] = (uint16_t)(bytes[offset + 2U] | bytes[offset + 3U] << 8);
			count++;
		} else if (tag == RESOURCE_FIXED_IO && offset + 4U <= length) {
			ports[count] = (uint16_t)((bytes[offset + 1U] | bytes[offset + 2U] << 8) & 0x3ffU);
			count++;
		}

		/* Steps over the small descriptor: its tag and its length. */
		offset += 1U + (tag & 0x07U);
	}

	/* The template is no longer needed. */
	drv_acpi_object_release(resources);

	/* Refuses a _CRS without both ports. */
	if (count != 2U)
		return ENOENT;

	/* Succeeded. */
	ec.data_port = ports[0];
	ec.command_port = ports[1];
	return 0;
}

/* Reads or writes the EC's address space for an operation region, a byte at a time. */
static int
ec_region(
	const struct drv_acpi_region_access *access,
	uint64_t *value,
	void *argument)
{
	uint8_t command;
	uint8_t byte;
	unsigned bytes;
	unsigned index;
	int error;

	UNUSED_PARAMETER(argument);

	/* Refuses an address beyond the EC's 256 bytes. */
	bytes = access->width / 8U;
	if (access->address + bytes > 256U)
		return EFAULT;

	/* A write sends Write Embedded Controller, a read Read Embedded Controller. */
	command = EC_COMMAND_READ;
	if (access->write)
		command = EC_COMMAND_WRITE;

	/* Moves each byte through one EC transaction. */
	if (!access->write)
		*value = 0;
	for (index = 0; index < bytes; index++) {
		/* Writes or reads one byte. */
		byte = (uint8_t)(*value >> (index * 8U));
		error = ec_transaction(command, (uint8_t)(access->address + index), true, access->write, &byte);
		if (error != 0)
			return error;

		/* Puts a read byte in its place. */
		if (!access->write)
			*value |= (uint64_t)byte << (index * 8U);
	}

	/* Succeeded. */
	return 0;
}

/*
 * Handles the EC's GPE: while the EC says it has an event, asks which one
 * and runs its _Qxx method.  It runs as an interpreter entry of its own,
 * so the queries do not mix with AML's accesses.
 */
static void
ec_gpe(
	unsigned gpe,
	void *argument)
{
	struct drv_acpi_thread storage;
	struct drv_acpi_thread *thread;
	uint8_t query;
	uint8_t status;
	unsigned count;
	int error;

	UNUSED_PARAMETER(gpe);
	UNUSED_PARAMETER(argument);

	/* Drains the queries, as a bounded number of rounds. */
	thread = drv_acpi_enter(&storage, __builtin_frame_address(0));
	for (count = 0; count < EC_QUERIES_MAX; count++) {
		/* Stops when the EC has nothing more. */
		status = read_status();
		if ((status & EC_STATUS_SCI_EVT) == 0)
			break;

		/* Asks for the event; zero means none after all. */
		query = 0;
		error = ec_transaction(EC_COMMAND_QUERY, 0, false, false, &query);
		if (error != 0 || query == 0)
			break;

		/* Runs its method. */
		run_query(query);
	}

	/* Leaves the interpreter. */
	drv_acpi_leave(thread);
}

/*
 * Makes one EC transaction: a command, an address when it has one, and a
 * byte written or read.  The Global Lock guards it when _GLK asks.
 */
static int
ec_transaction(
	uint8_t command,
	uint8_t address,
	bool has_address,
	bool write,
	uint8_t *data)
{
	int error;

	/* Takes the Global Lock for a controller that shares itself with the firmware. */
	if (ec.global_lock) {
		error = drv_acpi_global_lock(NULL, true);
		if (error != 0)
			return error;
	}

	/* Exchanges the bytes. */
	error = ec_exchange(command, address, has_address, write, data);

	/* Lets the Global Lock go and reports a failed exchange. */
	if (ec.global_lock)
		drv_acpi_global_lock(NULL, false);
	if (error != 0) {
		drv_acpi_os_log("ACPI: EC command 0x%x failed (error %d)\n", command, error);
		return error;
	}

	/* Succeeded. */
	return 0;
}

/* Exchanges the bytes of one EC transaction. */
static int
ec_exchange(
	uint8_t command,
	uint8_t address,
	bool has_address,
	bool write,
	uint8_t *data)
{
	int error;

	/* Sends the command. */
	error = ec_send(ec.command_port, command);
	if (error != 0)
		return error;

	/* Sends the address. */
	if (has_address) {
		error = ec_send(ec.data_port, address);
		if (error != 0)
			return error;
	}

	/* A write sends the byte and waits until the EC took it. */
	if (write) {
		error = ec_send(ec.data_port, *data);
		if (error != 0)
			return error;
		error = wait_status(EC_STATUS_IBF, 0);
		return error;
	}

	/* A read waits for the EC's byte and takes it. */
	error = ec_receive(data);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Sends one byte once the EC's input buffer is empty. */
static int
ec_send(
	uint16_t port,
	uint8_t byte)
{
	int error;

	/* Waits for room. */
	error = wait_status(EC_STATUS_IBF, 0);
	if (error != 0)
		return error;

	/* Writes the byte. */
	error = drv_acpi_os_port_write(port, 8, byte);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Takes one byte once the EC's output buffer is full. */
static int
ec_receive(
	uint8_t *byte)
{
	uint32_t value;
	int error;

	/* Waits for the byte. */
	error = wait_status(EC_STATUS_OBF, EC_STATUS_OBF);
	if (error != 0)
		return error;

	/* Reads it. */
	error = drv_acpi_os_port_read(ec.data_port, 8, &value);
	if (error != 0)
		return error;

	/* Succeeded. */
	*byte = (uint8_t)value;
	return 0;
}

/* Waits until the masked status bits are as wanted. */
static int
wait_status(
	uint8_t mask,
	uint8_t wanted)
{
	uint8_t status;
	unsigned poll;

	/* Polls the status register. */
	for (poll = 0; poll < EC_POLLS; poll++) {
		/* Reports success as soon as the bits are right. */
		status = read_status();
		if ((status & mask) == wanted)
			return 0;
		drv_acpi_os_stall(10);
	}

	/* Reports an EC that did not answer. */
	return ETIMEDOUT;
}

/* Reads the EC's status register. */
static uint8_t
read_status(void)
{
	uint32_t value;
	int error;

	/* A status that cannot be read reads as busy, which times out. */
	error = drv_acpi_os_port_read(ec.command_port, 8, &value);
	if (error != 0)
		return EC_STATUS_IBF;

	/* Reports it. */
	return (uint8_t)value;
}

/* Runs the _Qxx method of the EC for one query. */
static void
run_query(
	uint8_t query)
{
	static const char digits[] = "0123456789ABCDEF";
	struct drv_acpi_object *result;
	char name[5];
	int error;

	/* The method is _Q and the query in two upper-case hexadecimal digits. */
	name[0] = '_';
	name[1] = 'Q';
	name[2] = digits[query >> 4];
	name[3] = digits[query & 0x0fU];
	name[4] = '\0';

	/* Runs it; a query without a method is logged. */
	result = NULL;
	error = drv_acpi_evaluate(ec.device, name, NULL, 0, &result);
	drv_acpi_object_release(result);
	if (error != 0)
		drv_acpi_os_log("ACPI: EC query %s failed (error %d)\n", name, error);
}
