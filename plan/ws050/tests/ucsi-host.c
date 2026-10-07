/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the UCSI core and the Type-C layer (ws050-p002).
 *
 * src/drivers/typec/ucsi.c and typec.c are compiled with the host compiler
 * and run over a fake PPM: a C model that answers the commands as UCSI 1.2
 * (and 3.1 for the 2.x arrangement) describes, keeps its own copy of the
 * mailbox, and counts every break of the acknowledgement rules (a command
 * sent while a completion is not acknowledged, a change acknowledged that
 * was not indicated).  Scenarios: the start, a plug, an unplug, two
 * changes at once, a busy PPM, the 2.x arrangement with the orientation,
 * and the choice of the arrangement.  Prints "PASS name" or "FAIL name
 * ..." per check and exits with 1 when one failed.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <drivers/typec/typec.h>

#include "drivers/typec/typec-os.h"
#include "drivers/typec/ucsi.h"

/* The commands the fake PPM answers (UCSI 1.2 Table A-1). */
#define FAKE_PPM_RESET 0x01U
#define FAKE_ACK_CC_CI 0x04U
#define FAKE_SET_NOTIFICATION_ENABLE 0x05U
#define FAKE_GET_CAPABILITY 0x06U
#define FAKE_GET_CONNECTOR_CAPABILITY 0x07U
#define FAKE_GET_ALTERNATE_MODES 0x0CU
#define FAKE_GET_CAM_SUPPORTED 0x0DU
#define FAKE_GET_CURRENT_CAM 0x0EU
#define FAKE_GET_PDOS 0x10U
#define FAKE_GET_CONNECTOR_STATUS 0x12U

/* The CCI indicators (Table 3-2). */
#define FAKE_CCI_RESET (1U << 27)
#define FAKE_CCI_BUSY (1U << 28)
#define FAKE_CCI_ACK (1U << 29)
#define FAKE_CCI_ERROR (1U << 30)
#define FAKE_CCI_DONE (1U << 31)

/* The most connectors and modes of the model. */
#define FAKE_CONNECTORS 4U
#define FAKE_MODES 8U
#define FAKE_QUEUE 8U

/* The mailbox's room (the 2.x arrangement's). */
#define FAKE_MAILBOX 0x210U

/*
 * One connector of the fake PPM: what it can do, what is attached, and
 * what the attached partner and cable offer.
 */
struct fake_connector {
	uint32_t capability;
	bool connected;
	unsigned power_operation;
	bool provider;
	unsigned partner_flags;
	unsigned partner_type;
	uint32_t rdo;
	bool flipped;
	struct drv_typec_alt_mode modes[FAKE_MODES];
	unsigned mode_count;
	struct drv_typec_alt_mode partner_modes[FAKE_MODES];
	unsigned partner_mode_count;
	struct drv_typec_alt_mode cable_modes[FAKE_MODES];
	unsigned cable_mode_count;
	uint8_t supported;
	uint8_t current;
	uint32_t pdos[DRV_TYPEC_PDO_MAX];
	unsigned pdo_count;
	uint16_t change;
};

/*
 * The fake PPM: its mailbox, its connectors, the notifications enabled,
 * and the state of the acknowledgement rules.
 */
struct fake_ppm {
	const struct drv_ucsi_layout *layout;
	uint16_t version;
	uint8_t mailbox[FAKE_MAILBOX];
	unsigned connector_count;
	struct fake_connector connectors[FAKE_CONNECTORS];
	uint32_t optional_features;
	unsigned alt_mode_count;
	uint16_t notifications;

	/* A notification not yet taken by wait. */
	bool notify;

	/* A completion the OPM has not acknowledged. */
	bool completion_pending;

	/* The connector whose change is indicated and not acknowledged (0: none), and the changes not indicated yet. */
	unsigned change_indicated;
	unsigned queue[FAKE_QUEUE];
	unsigned queue_count;

	/* Waits a command stays busy, and the command and its CCI kept for then. */
	unsigned busy_waits;
	uint64_t busy_control;

	/* Commands answered, waits that refreshed, and breaks of the rules. */
	unsigned commands;
	unsigned violations;
	char last_violation[160];
};

/* The fake PPM the transport's operations use. */
static struct fake_ppm fake;

/* The checks that failed. */
static int test_failures;

/* The listener's calls, and the last connector and generation it was told. */
static unsigned listened;
static unsigned listened_connector;
static uint64_t listened_generation;

/* Whether the Type-C layer's lock is held (the layer must not nest it). */
static bool locked;

/* Whether to print the driver's log. */
static bool verbose;

static void fake_reset(const struct drv_ucsi_layout *layout, uint16_t version);
static void fake_violation(const char *what);
static void fake_set(uint8_t *data, unsigned offset, unsigned width, uint32_t value);
static uint32_t fake_get(uint64_t control, unsigned offset, unsigned width);
static void fake_complete(uint32_t length);
static void fake_answer(uint64_t control);
static void fake_answer_status(unsigned number, uint8_t *message);
static uint32_t fake_answer_modes(uint64_t control, uint8_t *message);
static uint32_t fake_answer_pdos(uint64_t control, uint8_t *message);
static void fake_indicate_next(void);
static void fake_event(unsigned number);
static int fake_write(void *context, uint64_t control, const uint8_t *message_out, size_t length);
static int fake_read(void *context, bool refresh, uint32_t *cci, uint8_t *message_in, size_t size);
static int fake_wait(void *context, uint32_t milliseconds);
static void test_listener(void *argument, unsigned connector, uint64_t generation);
static void test_check(const char *name, bool condition, const char *detail);
static void test_start_1(struct drv_ucsi *ucsi, const struct drv_ucsi_transport *transport);
static void test_changes(struct drv_ucsi *ucsi);
static void test_busy(struct drv_ucsi *ucsi);
static void test_start_2(struct drv_ucsi *ucsi, const struct drv_ucsi_transport *transport);
static void test_layouts(void);

/*
 * Runs the scenarios.
 */
int
main(
	int argc,
	char **argv)
{
	static struct drv_ucsi ucsi;
	struct drv_ucsi_transport transport;
	int compared;
	int error;

	/* -v prints the driver's log. */
	compared = 1;
	if (argc > 1)
		compared = strcmp(argv[1], "-v");
	if (compared == 0)
		verbose = true;

	/* The fake PPM's operations, and the listener. */
	transport.write = fake_write;
	transport.read = fake_read;
	transport.wait = fake_wait;
	transport.context = &fake;
	error = drv_typec_listener_register(test_listener, NULL);
	test_check("listener", error == 0, "registered");

	/* The scenarios. */
	test_start_1(&ucsi, &transport);
	test_changes(&ucsi);
	test_busy(&ucsi);
	test_start_2(&ucsi, &transport);
	test_layouts();

	/* Reports whether every check passed. */
	if (test_failures != 0)
		return 1;

	/* Succeeded: every check passed. */
	return 0;
}

/*
 * Takes the layer's lock (the host has one thread; the test checks that
 * the lock is never taken twice).
 */
void
drv_typec_os_lock(void)
{
	/* A nested lock would deadlock in the kernel. */
	if (locked)
		fake_violation("the Type-C lock taken twice");
	locked = true;
}

/*
 * Releases the layer's lock.
 */
void
drv_typec_os_unlock(void)
{
	/* Released. */
	locked = false;
}

/*
 * Prints a line of the driver's log when asked to.
 */
void
drv_typec_os_log(
	const char *format,
	...)
{
	va_list arguments;

	/* Quiet unless -v. */
	if (!verbose)
		return;

	/* The line. */
	va_start(arguments, format);
	(void)vprintf(format, arguments);
	va_end(arguments);
}

/* Starts the fake PPM afresh with an arrangement and a version. */
static void
fake_reset(
	const struct drv_ucsi_layout *layout,
	uint16_t version)
{
	/* Nothing known, the version in VERSION. */
	memset(&fake, 0, sizeof(fake));
	fake.layout = layout;
	fake.version = version;
	fake.mailbox[layout->version_offset] = (uint8_t)(version & 0xFFU);
	fake.mailbox[layout->version_offset + 1U] = (uint8_t)(version >> 8);
}

/* Counts a break of the rules and keeps the last one's words. */
static void
fake_violation(
	const char *what)
{
	/* Counted. */
	fake.violations++;
	(void)snprintf(fake.last_violation, sizeof(fake.last_violation), "%s", what);
}

/* Writes a field of up to 32 bits at a bit offset of a little-endian message. */
static void
fake_set(
	uint8_t *data,
	unsigned offset,
	unsigned width,
	uint32_t value)
{
	unsigned bit;
	unsigned at;

	/* Each bit. */
	for (bit = 0; bit < width; bit++) {
		at = offset + bit;
		if ((value >> bit) & 1U)
			data[at / 8U] |= (uint8_t)(1U << (at % 8U));
		else
			data[at / 8U] &= (uint8_t)~(1U << (at % 8U));
	}
}

/* Reads a field of CONTROL. */
static uint32_t
fake_get(
	uint64_t control,
	unsigned offset,
	unsigned width)
{
	uint64_t mask;

	/* The bits. */
	mask = (1ULL << width) - 1U;
	return (uint32_t)((control >> offset) & mask);
}

/*
 * Completes a command: CCI with the completion, the length of MESSAGE IN
 * and the connector whose change is indicated; a notification when the
 * completion notification is on.
 */
static void
fake_complete(
	uint32_t length)
{
	uint32_t cci;

	/* CCI. */
	cci = FAKE_CCI_DONE | (length << 8) | (fake.change_indicated << 1);
	memcpy(&fake.mailbox[fake.layout->cci_offset], &cci, sizeof(cci));

	/* The OPM owes an acknowledgement, and is told. */
	fake.completion_pending = true;
	if ((fake.notifications & 1U) != 0)
		fake.notify = true;
}

/* Answers a command (not PPM_RESET or ACK_CC_CI) into MESSAGE IN. */
static void
fake_answer(
	uint64_t control)
{
	struct fake_connector *connector;
	uint8_t *message;
	unsigned command;
	unsigned number;
	uint32_t length;

	/* MESSAGE IN, emptied. */
	message = &fake.mailbox[fake.layout->message_in_offset];
	memset(message, 0, fake.layout->message_in_size);
	command = (unsigned)(control & 0xFFU);
	number = fake_get(control, 16, 7);
	connector = NULL;
	if (number >= 1U && number <= fake.connector_count)
		connector = &fake.connectors[number - 1U];
	length = 0;

	/* Each command's answer. */
	switch (command) {
	case FAKE_SET_NOTIFICATION_ENABLE:
		fake.notifications = (uint16_t)fake_get(control, 16, 16);
		break;
	case FAKE_GET_CAPABILITY:
		fake_set(message, 0, 32, 1U << 2);
		fake_set(message, 32, 7, fake.connector_count);
		fake_set(message, 40, 24, fake.optional_features);
		fake_set(message, 64, 8, fake.alt_mode_count);
		fake_set(message, 96, 16, 0x0300U);
		length = 16;
		break;
	case FAKE_GET_CONNECTOR_CAPABILITY:
		if (connector != NULL)
			fake_set(message, 0, 16, connector->capability);
		length = 2;
		break;
	case FAKE_GET_CONNECTOR_STATUS:
		fake_answer_status(number, message);
		length = 9;
		if (fake.version >= 0x0200U)
			length = 19;
		break;
	case FAKE_GET_ALTERNATE_MODES:
		length = fake_answer_modes(control, message);
		break;
	case FAKE_GET_CAM_SUPPORTED:
		if (connector != NULL)
			message[0] = connector->supported;
		length = 1;
		break;
	case FAKE_GET_CURRENT_CAM:
		if (connector != NULL)
			message[0] = connector->current;
		length = 1;
		break;
	case FAKE_GET_PDOS:
		length = fake_answer_pdos(control, message);
		break;
	default:
		fake_violation("a command the fake PPM does not know");
		break;
	}

	/* Done. */
	fake_complete(length);
}

/* Writes GET_CONNECTOR_STATUS's answer (1.2 Table 4-42, 3.1 Table 6-43). */
static void
fake_answer_status(
	unsigned number,
	uint8_t *message)
{
	struct fake_connector *connector;

	/* A connector that is there. */
	if (number < 1U || number > fake.connector_count)
		return;
	connector = &fake.connectors[number - 1U];

	/* The change, then the state; reading it clears the change. */
	fake_set(message, 0, 16, connector->change);
	connector->change = 0;
	fake_set(message, 16, 3, connector->power_operation);
	fake_set(message, 19, 1, connector->connected);
	fake_set(message, 20, 1, connector->provider);
	fake_set(message, 21, 8, connector->partner_flags);
	fake_set(message, 29, 3, connector->partner_type);
	fake_set(message, 32, 32, connector->rdo);
	if (fake.version >= 0x0200U)
		fake_set(message, 86, 1, connector->flipped);
}

/*
 * Writes GET_ALTERNATE_MODES's answer (Table 4-24, 4-26): the modes of the
 * recipient from the offset, as many as asked and there are.  Returns the
 * length of the answer.
 */
static uint32_t
fake_answer_modes(
	uint64_t control,
	uint8_t *message)
{
	struct fake_connector *connector;
	const struct drv_typec_alt_mode *modes;
	unsigned recipient;
	unsigned number;
	unsigned offset;
	unsigned wanted;
	unsigned count;
	unsigned index;

	/* The fields. */
	recipient = fake_get(control, 16, 3);
	number = fake_get(control, 24, 7);
	offset = fake_get(control, 32, 8);
	wanted = fake_get(control, 40, 2) + 1U;
	if (wanted > 2U)
		fake_violation("GET_ALTERNATE_MODES asked for more than two");
	if (number < 1U || number > fake.connector_count)
		return 0;
	connector = &fake.connectors[number - 1U];

	/* The recipient's list. */
	modes = connector->modes;
	count = connector->mode_count;
	if (recipient == 1U) {
		modes = connector->partner_modes;
		count = connector->partner_mode_count;
	} else if (recipient == 2U) {
		modes = connector->cable_modes;
		count = connector->cable_mode_count;
	}

	/* The modes from the offset. */
	for (index = 0; index < wanted && offset + index < count; index++) {
		fake_set(message, index * 48U, 16, modes[offset + index].svid);
		fake_set(message, index * 48U + 16U, 32, modes[offset + index].vdo);
	}

	/* Six bytes a mode. */
	return index * 6U;
}

/*
 * Writes GET_PDOS's answer (Table 4-34, 4-36): the partner's PDOs from the
 * offset.  Returns the length of the answer.
 */
static uint32_t
fake_answer_pdos(
	uint64_t control,
	uint8_t *message)
{
	struct fake_connector *connector;
	unsigned number;
	unsigned offset;
	unsigned wanted;
	unsigned partner;
	unsigned index;

	/* The fields. */
	number = fake_get(control, 16, 7);
	offset = fake_get(control, 24, 8);
	wanted = fake_get(control, 32, 2) + 1U;
	partner = fake_get(control, 23, 1);
	if (partner == 0)
		fake_violation("GET_PDOS without the partner bit");
	if (offset + wanted - 1U > 7U)
		fake_violation("GET_PDOS past the seventh");
	if (number < 1U || number > fake.connector_count)
		return 0;
	connector = &fake.connectors[number - 1U];

	/* The PDOs from the offset. */
	for (index = 0; index < wanted && offset + index < connector->pdo_count; index++)
		fake_set(message, index * 32U, 32, connector->pdos[offset + index]);
	return index * 4U;
}

/* Indicates the next queued change when none is waiting for its acknowledgement. */
static void
fake_indicate_next(void)
{
	uint32_t cci;

	/* One change at a time (UCSI 1.2 section 4). */
	if (fake.change_indicated != 0 || fake.queue_count == 0)
		return;
	fake.change_indicated = fake.queue[0];
	memmove(&fake.queue[0], &fake.queue[1], (fake.queue_count - 1U) * sizeof(fake.queue[0]));
	fake.queue_count--;

	/* In CCI, with a notification when changes are notified. */
	memcpy(&cci, &fake.mailbox[fake.layout->cci_offset], sizeof(cci));
	cci = (cci & ~(0x7FU << 1)) | (fake.change_indicated << 1);
	memcpy(&fake.mailbox[fake.layout->cci_offset], &cci, sizeof(cci));
	if ((fake.notifications & (1U << 14)) != 0)
		fake.notify = true;
}

/* A connector changed (the test already set its new state and change bits). */
static void
fake_event(
	unsigned number)
{
	/* Queued, and indicated when nothing else is. */
	fake.queue[fake.queue_count++] = number;
	fake_indicate_next();
}

/* The transport's write: the fake PPM receives CONTROL. */
static int
fake_write(
	void *context,
	uint64_t control,
	const uint8_t *message_out,
	size_t length)
{
	unsigned command;
	unsigned acknowledged;
	unsigned change;
	uint32_t cci;

	/* The fake PPM is the file's; MESSAGE OUT is not used by these commands. */
	(void)context;
	(void)message_out;
	(void)length;

	/* CONTROL in the mailbox. */
	memcpy(&fake.mailbox[fake.layout->control_offset], &control, sizeof(control));
	command = (unsigned)(control & 0xFFU);
	fake.commands++;

	/* PPM_RESET: everything off, the reset completed (polled, no notification). */
	if (command == FAKE_PPM_RESET) {
		fake.notifications = 0;
		fake.completion_pending = false;
		fake.change_indicated = 0;
		cci = FAKE_CCI_RESET;
		memcpy(&fake.mailbox[fake.layout->cci_offset], &cci, sizeof(cci));
		return 0;
	}

	/* ACK_CC_CI: the acknowledgements it carries must be owed. */
	if (command == FAKE_ACK_CC_CI) {
		acknowledged = fake_get(control, 17, 1);
		change = fake_get(control, 16, 1);
		if (acknowledged != 0 && !fake.completion_pending)
			fake_violation("a completion acknowledged that was not owed");
		if (change != 0 && fake.change_indicated == 0)
			fake_violation("a change acknowledged that was not indicated");
		if (acknowledged != 0)
			fake.completion_pending = false;
		if (change != 0)
			fake.change_indicated = 0;

		/* Acknowledged; the next change, if any, is indicated with it. */
		cci = FAKE_CCI_ACK;
		memcpy(&fake.mailbox[fake.layout->cci_offset], &cci, sizeof(cci));
		fake_indicate_next();
		if ((fake.notifications & 1U) != 0)
			fake.notify = true;
		return 0;
	}

	/* Any other command: the previous completion must have been acknowledged. */
	if (fake.completion_pending)
		fake_violation("a command sent before the last completion was acknowledged");

	/* A busy PPM answers later. */
	if (fake.busy_waits != 0) {
		fake.busy_control = control;
		cci = FAKE_CCI_BUSY;
		memcpy(&fake.mailbox[fake.layout->cci_offset], &cci, sizeof(cci));
		if ((fake.notifications & 1U) != 0)
			fake.notify = true;
		return 0;
	}

	/* The answer. */
	fake_answer(control);
	return 0;
}

/* The transport's read: CCI and MESSAGE IN as the mailbox holds them. */
static int
fake_read(
	void *context,
	bool refresh,
	uint32_t *cci,
	uint8_t *message_in,
	size_t size)
{
	(void)context;
	(void)refresh;

	/* The bytes. */
	memcpy(cci, &fake.mailbox[fake.layout->cci_offset], sizeof(*cci));
	memcpy(message_in, &fake.mailbox[fake.layout->message_in_offset], size);
	return 0;
}

/* The transport's wait: a notification at once, else the time passes (a busy command completes after its waits). */
static int
fake_wait(
	void *context,
	uint32_t milliseconds)
{
	(void)context;
	(void)milliseconds;

	/* A busy command: one wait less, answered after the last. */
	if (fake.busy_waits != 0) {
		fake.busy_waits--;
		if (fake.busy_waits == 0)
			fake_answer(fake.busy_control);
	}

	/* A notification, taken. */
	if (fake.notify) {
		fake.notify = false;
		return 1;
	}

	/* No notification. */
	return 0;
}

/* Counts the changes the layer tells. */
static void
test_listener(
	void *argument,
	unsigned connector,
	uint64_t generation)
{
	(void)argument;

	/* The call. */
	if (locked)
		fake_violation("a listener called under the lock");
	listened++;
	listened_connector = connector;
	listened_generation = generation;
}

/* Prints one check's result. */
static void
test_check(
	const char *name,
	bool condition,
	const char *detail)
{
	/* PASS, or FAIL with what was seen. */
	if (condition) {
		printf("PASS %s\n", name);
		return;
	}

	/* A failure, counted. */
	printf("FAIL %s: %s\n", name, detail);
	test_failures++;
}

/*
 * The start on UCSI 1.2: two connectors, the first attached to a partner
 * in DisplayPort Alternate Mode over a USB PD contract, the second empty.
 */
static void
test_start_1(
	struct drv_ucsi *ucsi,
	const struct drv_ucsi_transport *transport)
{
	struct drv_typec_connector record;
	struct fake_connector *first;
	char detail[320];
	int error;

	/* The fake PPM. */
	fake_reset(&drv_ucsi_layout_1, 0x0120U);
	fake.connector_count = 2;
	fake.optional_features = (1U << 2) | (1U << 4);
	fake.alt_mode_count = 2;
	first = &fake.connectors[0];
	first->capability = (1U << 2) | (1U << 5) | (1U << 6) | (1U << 7) | (1U << 8) | (1U << 9);
	first->connected = true;
	first->power_operation = 3;
	first->provider = false;
	first->partner_flags = 0x3U;
	first->partner_type = 2;
	first->rdo = 0x1304B12CU;
	first->modes[0].svid = 0xFF01U;
	first->modes[0].vdo = 0x001C0045U;
	first->modes[1].svid = 0x8087U;
	first->modes[1].vdo = 0x00000001U;
	first->mode_count = 2;
	first->partner_modes[0].svid = 0xFF01U;
	first->partner_modes[0].vdo = 0x000C0005U;
	first->partner_modes[1].svid = 0x1234U;
	first->partner_modes[1].vdo = 0x11U;
	first->partner_modes[2].svid = 0x5678U;
	first->partner_modes[2].vdo = 0x22U;
	first->partner_mode_count = 3;
	first->supported = 0x01U;
	first->current = 0;
	first->pdos[0] = 0x0801912CU;
	first->pdos[1] = 0x0002D12CU;
	first->pdos[2] = 0x0003C12CU;
	first->pdos[3] = 0x0004B12CU;
	first->pdos[4] = 0x00064145U;
	first->pdo_count = 5;
	fake.connectors[1].capability = (1U << 2) | (1U << 5) | (1U << 6);
	fake.connectors[1].current = 0xFFU;

	/* The start. */
	error = drv_ucsi_start(ucsi, transport, &drv_ucsi_layout_1, 0x0120U);
	(void)snprintf(detail, sizeof(detail), "error %d", error);
	test_check("start-1.x", error == 0, detail);
	test_check("start-count", drv_typec_connector_count() == 2U, "not 2 connectors");
	(void)snprintf(detail, sizeof(detail), "%u breaks, the last: %s", fake.violations, fake.last_violation);
	test_check("start-rules", fake.violations == 0, detail);
	test_check("start-notifications", (fake.notifications & ((1U << 14) | (1U << 11) | (1U << 8) | 1U)) == ((1U << 14) | (1U << 11) | (1U << 8) | 1U), "connect, partner, CAM or completion not on");

	/* The first connector's record. */
	(void)drv_typec_connector_get(0, &record);
	test_check("first-connected", record.connected && record.power_operation == DRV_TYPEC_POWER_PD && record.power_role == DRV_TYPEC_ROLE_SINK, "not attached, PD, sink");
	test_check("first-partner", record.partner_type == DRV_TYPEC_PARTNER_UFP && record.partner_flags == (DRV_TYPEC_PARTNER_USB | DRV_TYPEC_PARTNER_ALT_MODE) && record.request_data_object == 0x1304B12CU, "partner, flags or RDO");
	test_check("first-capability", record.capability == first->capability, "capability bits");
	test_check("first-connector-modes", record.connector_modes.count == 2U && record.connector_modes.modes[0].svid == DRV_TYPEC_SVID_DISPLAYPORT && record.connector_modes.modes[1].vdo == 1U, "the connector's modes");
	(void)snprintf(detail, sizeof(detail), "%u modes", record.partner_modes.count);
	test_check("first-partner-modes", record.partner_modes.count == 3U && record.partner_modes.modes[2].svid == 0x5678U && record.partner_modes.modes[2].vdo == 0x22U, detail);
	test_check("first-cable-modes", record.cable_modes.count == 0U, "a cable mode");
	test_check("first-current", record.current_mode_count == 1U && record.current_modes[0] == 0U && record.supported_modes[0] == 0x01U, "the current or supported modes");
	(void)snprintf(detail, sizeof(detail), "%u PDOs", record.partner_pdo_count);
	test_check("first-pdos", record.partner_pdo_count == 5U && record.partner_pdos[4] == 0x00064145U, detail);
	test_check("first-orientation", record.orientation == DRV_TYPEC_ORIENTATION_UNKNOWN, "a 1.x orientation");

	/* The second, empty. */
	(void)drv_typec_connector_get(1, &record);
	test_check("second-empty", !record.connected && record.partner_type == DRV_TYPEC_PARTNER_NONE && record.current_mode_count == 0U, "attached");
}

/* A plug, an unplug, and two changes at once. */
static void
test_changes(
	struct drv_ucsi *ucsi)
{
	struct drv_typec_connector record;
	struct fake_connector *first;
	struct fake_connector *second;
	uint64_t before;
	char detail[320];
	int error;

	/* A charger-like partner on the second connector: this side the source at 3 A. */
	first = &fake.connectors[0];
	second = &fake.connectors[1];
	(void)drv_typec_connector_get(1, &record);
	before = record.generation;
	second->connected = true;
	second->power_operation = 5;
	second->provider = true;
	second->partner_flags = 0x1U;
	second->partner_type = 2;
	second->change = (uint16_t)(1U << 14);
	fake_event(2);
	error = drv_ucsi_service(ucsi);
	(void)drv_typec_connector_get(1, &record);
	(void)snprintf(detail, sizeof(detail), "error %d, connected %d, power %d, role %d", error, record.connected, record.power_operation, record.power_role);
	test_check("plug", error == 0 && record.connected && record.power_operation == DRV_TYPEC_POWER_TYPEC_3A && record.power_role == DRV_TYPEC_ROLE_SOURCE, detail);
	test_check("plug-generation", record.generation > before && listened_connector == 1U && listened_generation == record.generation, "no newer generation told");
	test_check("plug-acknowledged", fake.change_indicated == 0 && fake.violations == 0, fake.last_violation);

	/* The first connector's partner goes. */
	first->connected = false;
	first->change = (uint16_t)(1U << 14);
	fake_event(1);
	error = drv_ucsi_service(ucsi);
	(void)drv_typec_connector_get(0, &record);
	test_check("unplug", error == 0 && !record.connected && record.partner_modes.count == 0U && record.partner_pdo_count == 0U && record.current_mode_count == 0U, "the partner's things kept");
	test_check("unplug-modes-kept", record.connector_modes.count == 2U, "the connector's own modes lost");

	/* Two changes before the OPM looks: the first back, the second gone. */
	first->connected = true;
	first->change = (uint16_t)(1U << 14);
	second->connected = false;
	second->change = (uint16_t)(1U << 14);
	fake_event(1);
	fake_event(2);
	error = drv_ucsi_service(ucsi);
	(void)snprintf(detail, sizeof(detail), "error %d, queue %u, indicated %u, breaks %u (%s)", error, fake.queue_count, fake.change_indicated, fake.violations, fake.last_violation);
	test_check("two-changes", error == 0 && fake.queue_count == 0U && fake.change_indicated == 0U && fake.violations == 0U, detail);
	(void)drv_typec_connector_get(0, &record);
	test_check("two-changes-first", record.connected && record.partner_modes.count == 3U, "the first not read again");
	(void)drv_typec_connector_get(1, &record);
	test_check("two-changes-second", !record.connected, "the second not read again");
}

/* A PPM busy for a while on a command. */
static void
test_busy(
	struct drv_ucsi *ucsi)
{
	struct drv_typec_connector record;
	char detail[320];
	int error;

	/* The second connector attached again, the status busy for three waits. */
	fake.connectors[1].connected = true;
	fake.connectors[1].power_operation = 1;
	fake.connectors[1].change = (uint16_t)(1U << 14);
	fake_event(2);
	fake.busy_waits = 3;
	error = drv_ucsi_service(ucsi);
	(void)drv_typec_connector_get(1, &record);
	(void)snprintf(detail, sizeof(detail), "error %d, connected %d, breaks %u (%s)", error, record.connected, fake.violations, fake.last_violation);
	test_check("busy", error == 0 && record.connected && record.power_operation == DRV_TYPEC_POWER_USB_DEFAULT && fake.violations == 0U, detail);
}

/*
 * The start on the 2.x arrangement (version 2.0): one connector attached
 * with the plug flipped, carrying USB4, at 5 A.
 */
static void
test_start_2(
	struct drv_ucsi *ucsi,
	const struct drv_ucsi_transport *transport)
{
	struct drv_typec_connector record;
	struct fake_connector *only;
	char detail[320];
	int error;

	/* The fake PPM. */
	fake_reset(&drv_ucsi_layout_2, 0x0200U);
	fake.connector_count = 1;
	fake.optional_features = 0;
	only = &fake.connectors[0];
	only->capability = (1U << 2) | (1U << 6);
	only->connected = true;
	only->power_operation = 6;
	only->provider = false;
	only->partner_flags = 0x5U;
	only->partner_type = 1;
	only->flipped = true;
	only->current = 0xFFU;

	/* The start. */
	error = drv_ucsi_start(ucsi, transport, &drv_ucsi_layout_2, 0x0200U);
	(void)snprintf(detail, sizeof(detail), "error %d, breaks %u (%s)", error, fake.violations, fake.last_violation);
	test_check("start-2.x", error == 0 && fake.violations == 0U, detail);
	(void)drv_typec_connector_get(0, &record);
	test_check("2.x-orientation", record.orientation == DRV_TYPEC_ORIENTATION_FLIPPED, "not flipped");
	test_check("2.x-status", record.power_operation == DRV_TYPEC_POWER_TYPEC_5A && record.partner_flags == (DRV_TYPEC_PARTNER_USB | DRV_TYPEC_PARTNER_USB4) && record.partner_type == DRV_TYPEC_PARTNER_DFP, "5 A, USB4 or DFP");
	test_check("2.x-count", drv_typec_connector_count() == 1U, "not one connector");
}

/* The choice of the arrangement by the region's size. */
static void
test_layouts(void)
{
	/* 5330's 0x38-byte region is 1.x; 0x210 bytes hold 2.x; 0x20 holds neither. */
	test_check("layout-0x38", drv_ucsi_layout_select(0x38U) == &drv_ucsi_layout_1, "not 1.x");
	test_check("layout-0x210", drv_ucsi_layout_select(0x210U) == &drv_ucsi_layout_2, "not 2.x");
	test_check("layout-0x20", drv_ucsi_layout_select(0x20U) == NULL, "an arrangement");
}
