/*
 * WS031 Linux-parity -- the ACPI notifier chain the i915 OpRegion code registers its receive callback on
 * (register_acpi_notifier / unregister_acpi_notifier / acpi_notifier_call_chain).  zedBSD project code.
 *
 * Contract followed (Linux v6.8.12 drivers/acpi/event.c + kernel/notifier.c, checked against the public sources; no
 * text is copied -- those files are GPL-2.0, this is an independent implementation of the interface contract):
 *   - register: priority order (higher first, equal priority after the existing ones); the same block twice -> -EEXIST.
 *   - unregister: removes the block; a block not on the chain -> -ENOENT.
 *   - call chain: each callback in order; a result with NOTIFY_STOP_MASK ends the walk; the chain's result is the last
 *     callback's (NOTIFY_DONE when nobody was called).  acpi_notifier_call_chain() returns -EINVAL when that result is
 *     NOTIFY_BAD, else 0.
 *   - blocking: callbacks run in a context that may sleep; register / unregister are serialised against a running call
 *     chain, so after unregister returns the callback is not running and is not called again.
 * ADAPTATION: one kernel mutex serialises everything (Linux uses an rwsem: concurrent dispatches may overlap there).
 * A callback must not register / unregister on this chain (it would deadlock, as with the rwsem).
 *
 * The event source is separate: today only synthetic events (tests); an ACPI (AML Notify) source is connected later
 * to the same parity_acpi_notifier_call_chain() entry.
 */
#ifndef PARITY_OPREGION_SERVICE_H
#define PARITY_OPREGION_SERVICE_H

#include <stdint.h>

struct notifier_block {
	int (*notifier_call)(struct notifier_block *nb, unsigned long action, void *data);
	struct notifier_block *next;
	int priority;
};

#define NOTIFY_DONE      0x0000
#define NOTIFY_OK        0x0001
#define NOTIFY_STOP_MASK 0x8000
#define NOTIFY_BAD       (NOTIFY_STOP_MASK | 0x0002)

/* struct acpi_bus_event: acpi_device_class (char[20]) and acpi_bus_id (char[8]) */
struct acpi_bus_event {
	char device_class[20];
	char bus_id[8];
	uint32_t type;
	uint32_t data;
};

/* what one dispatch did, kept apart: the callbacks' result, the dispatch's return value, how many ran */
struct parity_acpi_dispatch {
	const char *event_source;       /* "SYNTHETIC" today; "ACPI" once an AML Notify source exists */
	int callback_result;            /* the chain's result (NOTIFY_*) */
	int dispatch_result;            /* acpi_notifier_call_chain(): -EINVAL for NOTIFY_BAD, else 0 */
	unsigned calls;
};

void parity_acpi_notifier_init(void);
int parity_register_acpi_notifier(struct notifier_block *nb);
int parity_unregister_acpi_notifier(struct notifier_block *nb);
int parity_acpi_notifier_call_chain(const char *device_class, const char *bus_id, uint32_t type, uint32_t data,
	const char *event_source, struct parity_acpi_dispatch *out);
unsigned parity_acpi_notifier_count(void);

#endif /* PARITY_OPREGION_SERVICE_H */
