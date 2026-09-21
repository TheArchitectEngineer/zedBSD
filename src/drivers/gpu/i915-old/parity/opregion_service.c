/*
 * WS031 Linux-parity -- the ACPI notifier chain (see opregion_service.h).  zedBSD project code.
 */
#include "../internal.h"
#include <kern/klog.h>
#include <kern/lock.h>
#include <string.h>
#include <errno.h>
#include "opregion_service.h"

static struct mutex chain_lock;
static int chain_lock_live;
static struct notifier_block *chain_head;

void
parity_acpi_notifier_init(void)
{
	if (!chain_lock_live) {
		(void)mutex_init(&chain_lock, LOCK_RANK_DEVICE, "parity-acpi-notifier");
		chain_lock_live = 1;
	}
}

int
parity_register_acpi_notifier(struct notifier_block *nb)
{
	struct notifier_block **nl;
	int rc = 0;

	if (nb == 0 || nb->notifier_call == 0)
		return -EINVAL;
	parity_acpi_notifier_init();
	mutex_lock(&chain_lock);
	for (nl = &chain_head; *nl != 0; nl = &(*nl)->next) {
		if (*nl == nb) {
			kern_logf("i915: parity WARN acpi notifier callback already registered\n");
			rc = -EEXIST;
			break;
		}
		if (nb->priority > (*nl)->priority)
			break;
	}
	if (rc == 0) {
		nb->next = *nl;
		*nl = nb;
	}
	mutex_unlock(&chain_lock);
	return rc;
}

int
parity_unregister_acpi_notifier(struct notifier_block *nb)
{
	struct notifier_block **nl;
	int rc = -ENOENT;

	parity_acpi_notifier_init();
	/* taking the lock waits for a running call chain: afterwards the callback neither runs nor is called again */
	mutex_lock(&chain_lock);
	for (nl = &chain_head; *nl != 0; nl = &(*nl)->next) {
		if (*nl == nb) {
			*nl = nb->next;
			nb->next = 0;
			rc = 0;
			break;
		}
	}
	mutex_unlock(&chain_lock);
	return rc;
}

int
parity_acpi_notifier_call_chain(const char *device_class, const char *bus_id, uint32_t type, uint32_t data,
	const char *event_source, struct parity_acpi_dispatch *out)
{
	struct acpi_bus_event event;
	struct notifier_block *nb;
	int ret = NOTIFY_DONE;
	unsigned calls = 0u;

	memset(&event, 0, sizeof(event));
	if (device_class != 0)
		strncpy(event.device_class, device_class, sizeof(event.device_class) - 1u);
	if (bus_id != 0)
		strncpy(event.bus_id, bus_id, sizeof(event.bus_id) - 1u);
	event.type = type;
	event.data = data;
	parity_acpi_notifier_init();
	mutex_lock(&chain_lock);
	for (nb = chain_head; nb != 0; nb = nb->next) {
		ret = nb->notifier_call(nb, 0ul, &event);
		calls++;
		if ((ret & NOTIFY_STOP_MASK) != 0)
			break;
	}
	mutex_unlock(&chain_lock);
	if (out != 0) {
		out->event_source = event_source != 0 ? event_source : "SYNTHETIC";
		out->callback_result = ret;
		out->dispatch_result = ret == NOTIFY_BAD ? -EINVAL : 0;
		out->calls = calls;
	}
	return ret == NOTIFY_BAD ? -EINVAL : 0;
}

unsigned
parity_acpi_notifier_count(void)
{
	struct notifier_block *nb;
	unsigned n = 0u;

	parity_acpi_notifier_init();
	mutex_lock(&chain_lock);
	for (nb = chain_head; nb != 0; nb = nb->next)
		n++;
	mutex_unlock(&chain_lock);
	return n;
}
