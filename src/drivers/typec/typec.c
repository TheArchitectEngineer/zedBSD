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
