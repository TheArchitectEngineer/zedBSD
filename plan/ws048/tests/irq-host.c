/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Exercises production event/IRQ functions with posted acknowledgements and faults.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#define XHCI_RING_TRBS 256U
#define XHCI_TRB_CYCLE 1U
#define XHCI_ERDP 0x38U
#define XHCI_USBCMD 0U
#define XHCI_USBSTS 4U
#define XHCI_CMD_RUN 1U
#define XHCI_CMD_INTE 4U
#define XHCI_STS_EINT 8U
#define XHCI_STS_FATAL 4U
#define XHCI_STS_HCE 0x1000U
#define XHCI_SUSPEND_HALTED 2U
#define XHCI_PCI_COMMAND 4U
#define XHCI_PCI_COMMAND_INTX_DISABLE 0x400U
#define IO_XHCI_IRQ_ENTRY 1U
#define IO_XHCI_IRQ_OWNED 2U
#define IO_XHCI_IRQ_EVENT 3U

/* One production DMA event word layout. */
struct xhci_trb {
	uint32_t parameter_low, parameter_high, status, control;
};

/* An IRQ-local lock whose owner is checked by the fixture. */
struct spinlock {
	unsigned held;
};

/* One memory descriptor whose bus identity must survive every fault. */
struct buffer {
	uint64_t device_address;
};

/* Only fields accessed by the extracted production functions. */
struct xhci_controller {
	void *pci;
	volatile uint8_t *runtime, *operational;
	struct xhci_trb *events;
	struct buffer event_memory;
	struct spinlock active_lock;
	struct xhci_trb command_event;
	uint64_t command_address;
	uint32_t irq_fault_status;
	volatile unsigned irq_busy;
	unsigned event_dequeue, event_cycle, interrupter_enabled;
	unsigned suspend_state, controller_stopping, command_busy,
	    command_event_ready;
	unsigned dma_quiesced;
};

/* The fixture owns one controller throughout each independently reset scenario. */
static struct xhci_controller controller;

/* One real-sized ring reset between scenarios. */
static struct xhci_trb events[XHCI_RING_TRBS];

/* Modeled interrupter registers, owned by the reset fixture. */
static uint32_t runtime[16];

/* Modeled operational registers, owned by the reset fixture. */
static uint32_t operational[64];

/* Detects event lock recursion or acknowledgement outside the protected cursor. */
static unsigned event_locked;

/* Holds an IMAN write until a modeled readback flushes it. */
static unsigned posted;

/* Counts releases of EHB; no event is required to end an IRQ batch. */
static unsigned ended;

/* Counts capacity releases while a consumer retains EHB. */
static unsigned advanced;

/* Enables a perpetually refilling event producer for the IRQ budget scenario. */
static unsigned producer;

/* Counts port notifications without executing USB enumeration in IRQ. */
static unsigned ports;

/* Counts deferred drains, which must not run under command ownership. */
static unsigned drained;

/* Holds this function's PCI command; fault masking must preserve other bits. */
static uint16_t pci_command;

/* Counts operational MMIO reads to detect DMA/BAR use after inaccessible status. */
static unsigned reads;

/* Counts logs to detect repeated reporting of a sticky hardware fault. */
static unsigned logs;

static void reset_fixture(void);
static uint32_t rd32(volatile uint8_t *base, unsigned offset);
static void wr32(volatile uint8_t *base, unsigned offset, uint32_t word);
static void wr64(volatile uint8_t *base, unsigned offset, uint64_t word);
static void event_lock(struct xhci_controller *c);
static void event_unlock(struct xhci_controller *c);
static void kern_io_read_barrier(void);
static void io_stats_record(unsigned operation, unsigned amount);
static unsigned atomic_raw_fetch_add_relaxed(volatile unsigned *word, unsigned amount);
static unsigned atomic_raw_fetch_add_release(volatile unsigned *word, unsigned amount);
static unsigned long spin_lock_irqsave(struct spinlock *lock);
static void spin_unlock_irqrestore(struct spinlock *lock, unsigned long irq);
static int transfer_claim(struct xhci_controller *c, const struct xhci_trb *event);
static uint64_t xhci_event_pointer(const struct xhci_trb *event);
static int drv_xhci_command_completion_matches(uint64_t expected, uint64_t actual);
static void port_change_defer(struct xhci_controller *c);
static void xhci_completion_drain(struct xhci_controller *c);
static int drv_pci_device_config_read16(void *device, unsigned offset, uint16_t *word);
static int drv_pci_device_config_write16(void *device, unsigned offset, uint16_t word);
static void kern_logf(const char *format, ...);
static void xhci_event_dequeue_publish_locked(struct xhci_controller *c, unsigned clear_busy);
static int event_take(struct xhci_controller *c, struct xhci_trb *out);
static void xhci_interrupter_write_locked(struct xhci_controller *c, unsigned acknowledge);
static void xhci_irq_fail(struct xhci_controller *c, uint32_t status);
static int xhci_irq(void *argument);

/*
 * Verifies IRQ acknowledgement, batching, command handoff and fatal containment.
 */
int
main(
	void)
{
	unsigned scenario;
	int handled;
	unsigned first_reads;

	/* Empty EINT must flush IP and clear EHB before the dispatcher may EOI. */
	reset_fixture();
	handled = xhci_irq(&controller);
	assert(handled == 1);
	assert(posted == 0U);
	assert(ended == 1U);
	assert(advanced == 0U);
	assert(controller.irq_busy == 0U);

	/* A command event must be handed back without command-recursive callbacks. */
	reset_fixture();
	controller.command_busy = 1U;
	controller.command_address = 0x400001000ULL;
	events[0].parameter_low = 0x1000U;
	events[0].parameter_high = 4U;
	events[0].status = 1U << 24;
	events[0].control = (33U << 10) | 1U;
	handled = xhci_irq(&controller);
	assert(handled == 1);
	assert(controller.command_event_ready == 1U);
	assert(drained == 0U);
	assert(advanced == 1U);
	assert(ended == 1U);

	/* A producer that never empties must return after one ring of IRQ work. */
	reset_fixture();
	producer = 1U;
	events[0].control = (34U << 10) | 1U;
	handled = xhci_irq(&controller);
	assert(handled == 1);
	assert(advanced == XHCI_RING_TRBS);
	assert(ports == XHCI_RING_TRBS);
	assert(ended == 1U);
	assert(controller.event_cycle == 0U);

	/* Fatal status, controller error and absent BAR must never inspect event DMA. */
	for (scenario = 0; scenario < 3U; scenario++) {
		reset_fixture();
		if (scenario == 0U) {
			operational[1] = XHCI_STS_HCE | XHCI_STS_EINT;
		} else if (scenario == 1U) {
			operational[1] = XHCI_STS_FATAL | XHCI_STS_EINT;
		} else {
			operational[1] = UINT32_MAX;
		}
		handled = xhci_irq(&controller);
		assert(handled == 1);
		assert(controller.irq_fault_status != 0U);
		assert(controller.controller_stopping == 1U);
		assert(controller.dma_quiesced == 0U);
		assert(advanced == 0U);
		assert(ended == 0U);
		assert(pci_command == 0x542U);
		assert(controller.irq_busy == 0U);
		first_reads = reads;
		handled = xhci_irq(&controller);
		assert(handled == 0);
		assert(reads == first_reads);
		assert(logs == 1U);
		if (scenario == 2U)
			assert(first_reads == 1U);
	}

	/* Shared IRQs without EINT and suspended controllers leave DMA untouched. */
	reset_fixture();
	operational[1] = 0U;
	handled = xhci_irq(&controller);
	assert(handled == 0);
	assert(advanced == 0U && ended == 0U);
	reset_fixture();
	controller.suspend_state = XHCI_SUSPEND_HALTED;
	handled = xhci_irq(&controller);
	assert(handled == 0);
	assert(reads == 0U);
	assert(advanced == 0U && ended == 0U);

	/* Reports checks of exact production functions rather than a copied algorithm. */
	puts("production xHCI IRQ/event path: PASS (8 scenarios)");

	/* Succeeded: all bounded register/event scenarios met their assertions. */
	return 0;
}

/* Resets all hardware and ownership state between scenarios. */
static void
reset_fixture(
	void)
{
	/* Starts the modeled controller with accessible registers and no event. */
	memset(&controller, 0, sizeof(controller));
	memset(runtime, 0, sizeof(runtime));
	memset(operational, 0, sizeof(operational));
	memset(events, 0, sizeof(events));
	controller.pci = &controller;
	controller.runtime = (volatile uint8_t *)runtime;
	controller.operational = (volatile uint8_t *)operational;
	controller.events = events;
	controller.event_memory.device_address = 0x400001000ULL;
	controller.event_cycle = 1U;
	controller.interrupter_enabled = 1U;
	operational[1] = XHCI_STS_EINT;
	operational[0] = XHCI_CMD_RUN | XHCI_CMD_INTE;

	/* Resets observations and the retained PCI enable state. */
	event_locked = 0U;
	posted = 0U;
	ended = 0U;
	advanced = 0U;
	producer = 0U;
	ports = 0U;
	drained = 0U;
	reads = 0U;
	logs = 0U;
	pci_command = 0x142U;
}

/* Models posted register completion at the controller readback. */
static uint32_t
rd32(
	volatile uint8_t *base,
	unsigned offset)
{
	/* Reading IMAN completes the pending acknowledgement. */
	if (base == controller.runtime && offset == 0x20U)
		posted = 0U;

	/* Counts use of an operational BAR independently of IRQ admission. */
	if (base == controller.operational)
		reads++;

	/* Reports the modeled register contents. */
	return *(volatile uint32_t *)(base + offset);
}

/* Queues acknowledgement without implicitly completing PCI posted writes. */
static void
wr32(
	volatile uint8_t *base,
	unsigned offset,
	uint32_t word)
{
	/* Only the actual interrupt cause should be acknowledged. */
	if (base == controller.operational && offset == XHCI_USBSTS) {
		assert(word == XHCI_STS_EINT);
		operational[1] &= ~word;
	} else {
		*(volatile uint32_t *)(base + offset) = word;
	}

	/* Holds IMAN writes posted until the production helper reads it back. */
	if (base == controller.runtime && offset == 0x20U)
		posted = 1U;
}

/* Models cursor publication and a producer that can refill released capacity. */
static void
wr64(
	volatile uint8_t *base,
	unsigned offset,
	uint64_t word)
{
	/* Requires acknowledgement completion and protected cursor ownership. */
	assert(base == controller.runtime && offset == XHCI_ERDP);
	assert(event_locked == 1U);
	assert(posted == 0U);

	/* Distinguishes retaining EHB from the final batch acknowledgement. */
	if ((word & 8U) != 0U) {
		ended++;
	} else {
		advanced++;

		/* Publishes another event as soon as the consumer returns capacity. */
		if (producer != 0U) {
			events[controller.event_dequeue].control =
			    (34U << 10) | controller.event_cycle;
		}
	}
}

/* Rejects event-lock recursion in every extracted function. */
static void
event_lock(
	struct xhci_controller *c)
{
	(void)c;

	/* The single modeled IRQ consumer must not reenter its own lock. */
	assert(event_locked == 0U);
	event_locked = 1U;
}

/* Releases the modeled shared cursor owner. */
static void
event_unlock(
	struct xhci_controller *c)
{
	(void)c;

	/* The consumer must own the cursor it releases. */
	assert(event_locked == 1U);
	event_locked = 0U;
}

/* Places no transport work in the host model's DMA ordering barrier. */
static void
kern_io_read_barrier(
	void)
{
}

/* Leaves tracing outside the model's protocol assertions. */
static void
io_stats_record(
	unsigned operation,
	unsigned amount)
{
	(void)operation;
	(void)amount;
}

/* Supplies the production counter's acquire-side observation. */
static unsigned
atomic_raw_fetch_add_relaxed(
	volatile unsigned *word,
	unsigned amount)
{
	unsigned previous;

	/* Reports the old owner count and applies this callback's increment. */
	previous = *word;
	*word += amount;

	/* Succeeded: reports the ownership count before this callback. */
	return previous;
}

/* Supplies the production counter's release-side observation. */
static unsigned
atomic_raw_fetch_add_release(
	volatile unsigned *word,
	unsigned amount)
{
	unsigned previous;

	/* Reports the old owner count before retiring this callback. */
	previous = *word;
	*word += amount;

	/* Succeeded: reports the count before retirement. */
	return previous;
}

/* Claims the modeled active lock inside already-disabled IRQ context. */
static unsigned long
spin_lock_irqsave(
	struct spinlock *lock)
{
	/* Rejects recursive fault admission. */
	assert(lock->held == 0U);
	lock->held = 1U;

	/* Succeeded: the modeled IRQ was already disabled. */
	return 0U;
}

/* Releases fault admission without changing modeled IRQ delivery. */
static void
spin_unlock_irqrestore(
	struct spinlock *lock,
	unsigned long irq)
{
	/* Confirms that the same IRQ state is restored. */
	assert(irq == 0U);
	assert(lock->held == 1U);
	lock->held = 0U;
}

/* Leaves transfer payload outside this event/IRQ model. */
static int
transfer_claim(
	struct xhci_controller *c,
	const struct xhci_trb *event)
{
	(void)c;
	(void)event;

	/* Succeeded: this fixture models no transfer buffers. */
	return 0;
}

/* Resolves the actual production layout of a completion descriptor pointer. */
static uint64_t
xhci_event_pointer(
	const struct xhci_trb *event)
{
	/* Supplies the two-word event identity used by the waiter. */
	return ((uint64_t)event->parameter_high << 32) | event->parameter_low;
}

/* Rejects completions that name a different command descriptor. */
static int
drv_xhci_command_completion_matches(
	uint64_t expected,
	uint64_t actual)
{
	/* A stale descriptor cannot retire the waiting owner's command. */
	if (expected != actual)
		return 0;

	/* Succeeded: the event names this command. */
	return 1;
}

/* Counts port notifications without enumerating devices in IRQ. */
static void
port_change_defer(
	struct xhci_controller *c)
{
	(void)c;

	/* Records one notification passed to the thread path. */
	ports++;
}

/* Requires deferred callbacks to wait for release of command/event ownership. */
static void
xhci_completion_drain(
	struct xhci_controller *c)
{
	/* No callback may enter under a retained command or event lock. */
	assert(c->command_busy == 0U);
	assert(event_locked == 0U);
	drained++;
}

/* Supplies one function's current PCI command. */
static int
drv_pci_device_config_read16(
	void *device,
	unsigned offset,
	uint16_t *word)
{
	/* Keeps fatal masking confined to the same PCI function. */
	assert(device == controller.pci && offset == XHCI_PCI_COMMAND);
	*word = pci_command;

	/* Succeeded: the function's configuration is accessible. */
	return 0;
}

/* Updates only the modeled PCI function, preserving the shared IRQ line. */
static int
drv_pci_device_config_write16(
	void *device,
	unsigned offset,
	uint16_t word)
{
	/* The IRQ fault path must target only this PCI function. */
	assert(device == controller.pci && offset == XHCI_PCI_COMMAND);
	pci_command = word;

	/* Succeeded: readback can now verify the function's masked INTx bit. */
	return 0;
}

/* Counts fault reports without using console or formatter code. */
static void
kern_logf(
	const char *format,
	...)
{
	(void)format;

	/* Repeated callbacks for a sticky fault must not flood the console. */
	logs++;
}

/* The runner extracts these implementations unchanged from the driver. */
#include "irq-under-test.inc"
