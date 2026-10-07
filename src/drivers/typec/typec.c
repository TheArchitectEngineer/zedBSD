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

/* Writes the line of one connector. */
static void
typec_text_line(
	struct typec_text *text,
	unsigned index,
	const struct drv_typec_connector *connector)
{
	const char *separator;
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
