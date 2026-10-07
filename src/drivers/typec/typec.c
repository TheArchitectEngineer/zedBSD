/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The USB Type-C connector layer (ws050-p002): the connector records the
 * connector driver publishes, their generations, and the listeners told of
 * each change.  The records are copied in and out under the layer's lock;
 * the listeners are called outside it.
 */

#include <stdarg.h>

#include <kern/kcrt.h>
#include <uapi/errno.h>

#include <drivers/typec/typec.h>

#include "typec-os.h"

/*
 * One registered listener and its argument.
 */
struct typec_listener_entry {
	drv_typec_listener listener;
	void *argument;
};

/*
 * A text being written into a caller's buffer: what has been written, and
 * the room.  The text stays terminated; what does not fit is dropped.
 */
struct typec_text {
	char *buffer;
	size_t size;
	size_t length;
};

/*
 * The connector records, one per connector the driver found.
 *
 * Written by drv_typec_connector_publish() and read by
 * drv_typec_connector_get(), both under the layer's lock.  Records past
 * typec_count are zero.
 */
static struct drv_typec_connector typec_connectors[DRV_TYPEC_CONNECTOR_MAX];

/*
 * How many connectors the driver found (0 until it reports them).
 *
 * Protected by the layer's lock.
 */
static unsigned typec_count;

/*
 * The generation stamped on the record published next.
 *
 * It only ever increases (from 1), so a reader holding a record knows it is
 * stale when the connector's current generation is larger.  Protected by
 * the layer's lock.
 */
static uint64_t typec_generation;

/*
 * The registered listeners.
 *
 * Entries are only added (drivers register once and stay), under the
 * layer's lock; a change is told to the ones registered when it is
 * published.
 */
static struct typec_listener_entry typec_listeners[DRV_TYPEC_LISTENER_MAX];

/*
 * How many listeners are registered.
 *
 * Protected by the layer's lock.
 */
static unsigned typec_listener_count;

/*
 * The operations waiting for the connector driver, oldest first: a ring of
 * typec_request_count entries from typec_request_first.
 *
 * Filled by the drv_typec_connector_* operations and emptied by the
 * connector driver's thread, both under the layer's lock.
 */
static struct drv_typec_request typec_requests[DRV_TYPEC_REQUEST_MAX];
static unsigned typec_request_first;
static unsigned typec_request_count;

/*
 * The serial given to the operation asked next.
 *
 * It only increases (from 1; 0 in a record means none was carried out),
 * under the layer's lock.
 */
static uint32_t typec_request_serial;

/*
 * The connector driver's wake-up, called (outside the lock) after an
 * operation is queued, and its argument; NULL until the driver names it.
 */
static void (*typec_kick)(void *argument);
static void *typec_kick_argument;

static int typec_request_put(struct drv_typec_request *request, uint32_t *serial);
static void typec_text_line(struct typec_text *text, unsigned index, const struct drv_typec_connector *connector);
static void typec_text_modes(struct typec_text *text, const char *name, const struct drv_typec_alt_mode_list *list);
static const char *typec_text_partner(enum drv_typec_partner_type type);
static const char *typec_text_power(enum drv_typec_power_operation operation);
static void typec_text_append(struct typec_text *text, const char *format, ...) __attribute__((format(printf, 2, 3)));

/*
 * Registers a listener of connector changes.
 *
 * Returns 0, EINVAL for no listener, or ENOSPC when the table is full.
 */
int
drv_typec_listener_register(
	drv_typec_listener listener,
	void *argument)
{
	/* Refuses a missing listener. */
	if (listener == NULL)
		return EINVAL;

	/* Adds it to the table, which has a fixed room. */
	drv_typec_os_lock();

	if (typec_listener_count == DRV_TYPEC_LISTENER_MAX) {
		drv_typec_os_unlock();
		return ENOSPC;
	}

	/* The next free entry. */
	typec_listeners[typec_listener_count].listener = listener;
	typec_listeners[typec_listener_count].argument = argument;
	typec_listener_count++;

	drv_typec_os_unlock();

	/* Succeeded: it is told of every change published from now on. */
	return 0;
}

/*
 * Reports how many connectors there are.
 */
unsigned
drv_typec_connector_count(void)
{
	unsigned count;

	/* Reads the count the driver set. */
	drv_typec_os_lock();

	count = typec_count;

	drv_typec_os_unlock();

	/* Reports it. */
	return count;
}

/*
 * Copies the record of a connector (0-based index).
 *
 * Returns 0, or ENOENT for a connector that is not there.
 */
int
drv_typec_connector_get(
	unsigned index,
	struct drv_typec_connector *connector)
{
	/* Copies the record of a connector that is there. */
	drv_typec_os_lock();

	if (index >= typec_count) {
		drv_typec_os_unlock();
		return ENOENT;
	}

	/* The copy. */
	kern_memcpy(connector, &typec_connectors[index], sizeof(*connector));

	drv_typec_os_unlock();

	/* Succeeded: the caller holds a copy at its generation. */
	return 0;
}

/*
 * Sets how many connectors the connector driver found, emptying every
 * record (at the driver's start and after its PPM is reset).
 */
void
drv_typec_connectors_reset(
	unsigned count)
{
	/* Keeps no more than the table holds. */
	if (count > DRV_TYPEC_CONNECTOR_MAX) {
		drv_typec_os_log("typec: %u connectors, keeping %u\n", count, DRV_TYPEC_CONNECTOR_MAX);
		count = DRV_TYPEC_CONNECTOR_MAX;
	}

	/* Empties the records and sets the count. */
	drv_typec_os_lock();

	kern_memset(typec_connectors, 0, sizeof(typec_connectors));
	typec_count = count;

	drv_typec_os_unlock();
}

/*
 * Publishes a new record of a connector (0-based index) and tells the
 * listeners which connector changed and at which generation.
 *
 * Returns 0, or ENOENT for a connector that is not there.
 */
int
drv_typec_connector_publish(
	unsigned index,
	const struct drv_typec_connector *connector)
{
	struct typec_listener_entry listeners[DRV_TYPEC_LISTENER_MAX];
	unsigned listener_count;
	unsigned listener;
	uint64_t generation;

	/*
	 * Stores the record under a new generation and takes a copy of the
	 * listeners, which are called after the lock is released.
	 */
	drv_typec_os_lock();

	if (index >= typec_count) {
		drv_typec_os_unlock();
		return ENOENT;
	}

	/* The record under its generation, and the listeners as they are now. */
	typec_generation++;
	generation = typec_generation;
	kern_memcpy(&typec_connectors[index], connector, sizeof(typec_connectors[index]));
	typec_connectors[index].generation = generation;
	listener_count = typec_listener_count;
	kern_memcpy(listeners, typec_listeners, sizeof(listeners));

	drv_typec_os_unlock();

	/* Tells each listener. */
	for (listener = 0; listener < listener_count; listener++)
		listeners[listener].listener(listeners[listener].argument, index, generation);

	/* Succeeded: the record is current at its new generation. */
	return 0;
}

/*
 * Asks a connector (0-based index) to swap to a data role, accepting the
 * partner's swaps from then on.
 *
 * Returns 0 with the operation's serial, ENOENT for a connector that is
 * not there, EINVAL for a role that is not one, or EBUSY when too many
 * operations wait.
 */
int
drv_typec_connector_set_data_role(
	unsigned index,
	enum drv_typec_data_role role,
	uint32_t *serial)
{
	struct drv_typec_request request;
	int error;

	/* Refuses a role that is not one. */
	if (role != DRV_TYPEC_DATA_DFP && role != DRV_TYPEC_DATA_UFP)
		return EINVAL;

	/* The operation, queued. */
	kern_memset(&request, 0, sizeof(request));
	request.kind = DRV_TYPEC_REQUEST_DATA_ROLE;
	request.connector = index;
	request.value = (unsigned)role;
	error = typec_request_put(&request, serial);
	if (error != 0)
		return error;

	/* Succeeded: the connector driver carries it out. */
	return 0;
}

/*
 * Asks a connector (0-based index) to swap to a power role, accepting the
 * partner's swaps from then on.
 *
 * Returns 0 with the operation's serial, ENOENT, EINVAL or EBUSY.
 */
int
drv_typec_connector_set_power_role(
	unsigned index,
	enum drv_typec_power_role role,
	uint32_t *serial)
{
	struct drv_typec_request request;
	int error;

	/* Refuses a role that is not one. */
	if (role != DRV_TYPEC_ROLE_SINK && role != DRV_TYPEC_ROLE_SOURCE)
		return EINVAL;

	/* The operation, queued. */
	kern_memset(&request, 0, sizeof(request));
	request.kind = DRV_TYPEC_REQUEST_POWER_ROLE;
	request.connector = index;
	request.value = (unsigned)role;
	error = typec_request_put(&request, serial);
	if (error != 0)
		return error;

	/* Succeeded: the connector driver carries it out. */
	return 0;
}

/*
 * Asks a connector (0-based index) to be reset.
 *
 * Returns 0 with the operation's serial, ENOENT, EINVAL or EBUSY.
 */
int
drv_typec_connector_reset(
	unsigned index,
	enum drv_typec_reset kind,
	uint32_t *serial)
{
	struct drv_typec_request request;
	int error;

	/* Refuses a kind that is not one. */
	if (kind != DRV_TYPEC_RESET_HARD && kind != DRV_TYPEC_RESET_DATA)
		return EINVAL;

	/* The operation, queued. */
	kern_memset(&request, 0, sizeof(request));
	request.kind = DRV_TYPEC_REQUEST_RESET;
	request.connector = index;
	request.value = (unsigned)kind;
	error = typec_request_put(&request, serial);
	if (error != 0)
		return error;

	/* Succeeded: the connector driver carries it out. */
	return 0;
}

/*
 * Asks a connector (0-based index) to enter one of its Alternate Modes
 * (an index into its connector_modes) with a mode-specific configuration.
 *
 * Returns 0 with the operation's serial, ENOENT, EINVAL for a mode the
 * connector does not list, or EBUSY.
 */
int
drv_typec_connector_enter_mode(
	unsigned index,
	unsigned mode,
	uint32_t configuration,
	uint32_t *serial)
{
	struct drv_typec_request request;
	int error;

	/* Refuses a mode beyond any list. */
	if (mode >= DRV_TYPEC_ALT_MODE_MAX)
		return EINVAL;

	/* The operation, queued. */
	kern_memset(&request, 0, sizeof(request));
	request.kind = DRV_TYPEC_REQUEST_ENTER_MODE;
	request.connector = index;
	request.mode = mode;
	request.configuration = configuration;
	error = typec_request_put(&request, serial);
	if (error != 0)
		return error;

	/* Succeeded: the connector driver carries it out. */
	return 0;
}

/*
 * Asks a connector (0-based index) to leave one of its Alternate Modes.
 *
 * Returns 0 with the operation's serial, ENOENT, EINVAL or EBUSY.
 */
int
drv_typec_connector_exit_mode(
	unsigned index,
	unsigned mode,
	uint32_t *serial)
{
	struct drv_typec_request request;
	int error;

	/* Refuses a mode beyond any list. */
	if (mode >= DRV_TYPEC_ALT_MODE_MAX)
		return EINVAL;

	/* The operation, queued. */
	kern_memset(&request, 0, sizeof(request));
	request.kind = DRV_TYPEC_REQUEST_EXIT_MODE;
	request.connector = index;
	request.mode = mode;
	error = typec_request_put(&request, serial);
	if (error != 0)
		return error;

	/* Succeeded: the connector driver carries it out. */
	return 0;
}

/*
 * Names the connector driver's wake-up, called after each operation is
 * queued.
 */
void
drv_typec_operator_set(
	void (*kick)(void *argument),
	void *argument)
{
	/* Kept for the operations asked from now on. */
	drv_typec_os_lock();

	typec_kick = kick;
	typec_kick_argument = argument;

	drv_typec_os_unlock();
}

/*
 * Takes the oldest waiting operation.  Returns true with it in *request,
 * false when none waits.
 */
bool
drv_typec_request_take(
	struct drv_typec_request *request)
{
	/* The oldest, out of the ring. */
	drv_typec_os_lock();

	if (typec_request_count == 0) {
		drv_typec_os_unlock();
		return false;
	}

	/* The oldest, out of the ring. */
	*request = typec_requests[typec_request_first];
	typec_request_first = (typec_request_first + 1U) % DRV_TYPEC_REQUEST_MAX;
	typec_request_count--;

	drv_typec_os_unlock();

	/* Succeeded: the connector driver carries it out. */
	return true;
}

/*
 * Notes an operation's outcome (its serial and errno value) in its
 * connector's record without a new generation: the connector driver reads
 * the connector again and publishes it, which tells the listeners.
 *
 * Returns 0, or ENOENT for a connector that is not there any more.
 */
int
drv_typec_request_finish(
	const struct drv_typec_request *request,
	int error)
{
	/* The outcome in the record. */
	drv_typec_os_lock();

	if (request->connector >= typec_count) {
		drv_typec_os_unlock();
		return ENOENT;
	}

	/* Its serial and outcome. */
	typec_connectors[request->connector].request_serial = request->serial;
	typec_connectors[request->connector].request_error = error;

	drv_typec_os_unlock();

	/* Succeeded: the next published record carries it. */
	return 0;
}

/*
 * Writes every connector record as text, one line each, for the diagnostic
 * /dev/typec: whether something is attached, the partner, the power, the
 * plug's orientation, the Alternate Modes and the partner's PDOs.
 *
 * Returns the length of the text (without its terminating NUL), which is
 * cut short when the buffer is too small.
 */
size_t
drv_typec_text(
	char *buffer,
	size_t size)
{
	struct drv_typec_connector connector;
	struct typec_text text;
	unsigned count;
	unsigned index;
	int error;

	/* An empty text in the caller's buffer. */
	text.buffer = buffer;
	text.size = size;
	text.length = 0;
	if (size != 0)
		buffer[0] = '\0';

	/* One line for each connector, from a copy of its record. */
	count = drv_typec_connector_count();
	for (index = 0; index < count; index++) {
		/* A connector gone meanwhile (the PPM was reset) ends the text. */
		error = drv_typec_connector_get(index, &connector);
		if (error != 0)
			break;

		/* Its line. */
		typec_text_line(&text, index, &connector);
	}

	/* Succeeded: the length of what was written. */
	return text.length;
}

/*
 * Queues an operation under a new serial and wakes the connector driver.
 * Returns 0, ENOENT for a connector that is not there, or EBUSY when the
 * queue is full.
 */
static int
typec_request_put(
	struct drv_typec_request *request,
	uint32_t *serial)
{
	void (*kick)(void *argument);
	void *argument;
	unsigned slot;

	/* Into the ring, under a new serial. */
	drv_typec_os_lock();

	if (request->connector >= typec_count) {
		drv_typec_os_unlock();
		return ENOENT;
	}

	/* A full ring takes no more. */
	if (typec_request_count == DRV_TYPEC_REQUEST_MAX) {
		drv_typec_os_unlock();
		return EBUSY;
	}

	/* The operation under its serial, at the ring's end. */
	typec_request_serial++;
	request->serial = typec_request_serial;
	slot = (typec_request_first + typec_request_count) % DRV_TYPEC_REQUEST_MAX;
	typec_requests[slot] = *request;
	typec_request_count++;
	kick = typec_kick;
	argument = typec_kick_argument;

	drv_typec_os_unlock();

	/* The connector driver's thread wakes for it. */
	if (kick != NULL)
		kick(argument);

	/* Succeeded: the caller knows the operation by its serial. */
	if (serial != NULL)
		*serial = request->serial;
	return 0;
}

/* Writes the line of one connector. */
static void
typec_text_line(
	struct typec_text *text,
	unsigned index,
	const struct drv_typec_connector *connector)
{
	const char *separator;
	const char *cable;
	const char *role;
	unsigned mode;
	unsigned pdo;

	/* The connector's number as UCSI counts (from 1), and whether something is attached. */
	typec_text_append(text, "connector %u:", index + 1U);
	if (!connector->connected) {
		typec_text_append(text, " detached");
	} else {
		role = "sink";
		if (connector->power_role == DRV_TYPEC_ROLE_SOURCE)
			role = "source";
		typec_text_append(text, " attached partner=%s", typec_text_partner(connector->partner_type));
		typec_text_append(text, " power=%s role=%s", typec_text_power(connector->power_operation), role);
	}

	/* What is carried to the partner, each as a word of its own. */
	if ((connector->partner_flags & DRV_TYPEC_PARTNER_USB) != 0)
		typec_text_append(text, " usb");
	if ((connector->partner_flags & DRV_TYPEC_PARTNER_ALT_MODE) != 0)
		typec_text_append(text, " alt-mode");
	if ((connector->partner_flags & DRV_TYPEC_PARTNER_USB4) != 0)
		typec_text_append(text, " usb4");

	/* The plug's orientation, when the connector driver knows it. */
	if (connector->orientation == DRV_TYPEC_ORIENTATION_NORMAL) {
		typec_text_append(text, " orientation=normal");
	} else if (connector->orientation == DRV_TYPEC_ORIENTATION_FLIPPED) {
		typec_text_append(text, " orientation=flipped");
	}

	/* The contract's Request Data Object, when there is one. */
	if (connector->request_data_object != 0)
		typec_text_append(text, " rdo=0x%08x", (unsigned)connector->request_data_object);

	/* The Alternate Modes of the connector, the partner and the cable. */
	typec_text_modes(text, "modes", &connector->connector_modes);
	typec_text_modes(text, "partner-modes", &connector->partner_modes);
	typec_text_modes(text, "cable-modes", &connector->cable_modes);

	/* The connector modes it is in, by their SVIDs. */
	for (mode = 0; mode < connector->current_mode_count; mode++) {
		/* An index the list does not hold names no mode. */
		if (connector->current_modes[mode] >= connector->connector_modes.count)
			continue;

		/* The mode's SVID, the first after the word. */
		separator = ",";
		if (mode == 0)
			separator = " current=";
		typec_text_append(text, "%s%04x", separator, (unsigned)connector->connector_modes.modes[connector->current_modes[mode]].svid);
	}

	/* The partner's Power Data Objects. */
	for (pdo = 0; pdo < connector->partner_pdo_count; pdo++) {
		/* The PDO, the first after the word. */
		separator = ",";
		if (pdo == 0)
			separator = " pdos=";
		typec_text_append(text, "%s0x%08x", separator, (unsigned)connector->partner_pdos[pdo]);
	}

	/* What the cable reports of itself. */
	if (connector->cable.known) {
		cable = "passive";
		if (connector->cable.active)
			cable = "active";
		typec_text_append(text, " cable=%s speed=%llu current=%umA", cable, (unsigned long long)connector->cable.speed_bps, connector->cable.current_ma);
	}

	/* The last operation carried out, and its outcome. */
	if (connector->request_serial != 0)
		typec_text_append(text, " request=%u error=%d", (unsigned)connector->request_serial, connector->request_error);

	/* The generation the record was published at, which ends the line. */
	typec_text_append(text, " generation=%llu\n", (unsigned long long)connector->generation);
}

/* Writes a list of Alternate Modes as SVID/VDO pairs, nothing for an empty one. */
static void
typec_text_modes(
	struct typec_text *text,
	const char *name,
	const struct drv_typec_alt_mode_list *list)
{
	unsigned mode;

	/* Each mode, the first after the list's name. */
	for (mode = 0; mode < list->count; mode++) {
		/* The list's name before its first mode, a comma before the others. */
		if (mode == 0) {
			typec_text_append(text, " %s=", name);
		} else {
			typec_text_append(text, ",");
		}

		/* The mode. */
		typec_text_append(text, "%04x/%08x", (unsigned)list->modes[mode].svid, (unsigned)list->modes[mode].vdo);
	}
}

/* Names the kind of the attached partner. */
static const char *
typec_text_partner(
	enum drv_typec_partner_type type)
{
	/* The kinds of GET_CONNECTOR_STATUS. */
	switch (type) {
	case DRV_TYPEC_PARTNER_DFP:
		return "dfp";
	case DRV_TYPEC_PARTNER_UFP:
		return "ufp";
	case DRV_TYPEC_PARTNER_POWERED_CABLE:
		return "powered-cable";
	case DRV_TYPEC_PARTNER_POWERED_CABLE_UFP:
		return "powered-cable-ufp";
	case DRV_TYPEC_PARTNER_DEBUG_ACCESSORY:
		return "debug-accessory";
	case DRV_TYPEC_PARTNER_AUDIO_ACCESSORY:
		return "audio-accessory";
	default:
		break;
	}

	/* No partner, or a kind the layer does not name. */
	return "none";
}

/* Names how power is delivered. */
static const char *
typec_text_power(
	enum drv_typec_power_operation operation)
{
	/* The power operation modes of GET_CONNECTOR_STATUS. */
	switch (operation) {
	case DRV_TYPEC_POWER_USB_DEFAULT:
		return "usb-default";
	case DRV_TYPEC_POWER_BC:
		return "bc";
	case DRV_TYPEC_POWER_PD:
		return "pd";
	case DRV_TYPEC_POWER_TYPEC_1_5A:
		return "typec-1.5a";
	case DRV_TYPEC_POWER_TYPEC_3A:
		return "typec-3a";
	case DRV_TYPEC_POWER_TYPEC_5A:
		return "typec-5a";
	default:
		break;
	}

	/* A mode the PPM did not report. */
	return "unknown";
}

/* Appends formatted text, dropping what does not fit. */
static void
typec_text_append(
	struct typec_text *text,
	const char *format,
	...)
{
	va_list arguments;
	size_t room;
	int length;

	/* Nothing fits in a full buffer (the last byte is the terminator's). */
	if (text->length + 1U >= text->size)
		return;

	/* Formats after what is there; the terminator always fits. */
	room = text->size - text->length;
	va_start(arguments, format);
	length = kern_vsnprintf(text->buffer + text->length, room, format, arguments);
	va_end(arguments);
	if (length < 0)
		return;

	/* Moves past what fitted. */
	if ((size_t)length >= room) {
		text->length = text->size - 1U;
	} else {
		text->length += (size_t)length;
	}
}
