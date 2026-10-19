/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the PCI core's INTx hook for a host that demultiplexes its
 * own pins (ws256-p021).
 *
 * The real PCI core (src/drivers/pci/pci.c) runs against fake hosts written
 * as the design asks of a host such as the PLDA one: the host owns one
 * interrupt line, reads a status word, runs each raised pin's line without
 * its own lock, acknowledges the status after every handler, and ends the
 * interrupt once.  Each case states what it proves in main().
 */

#include <drivers/pci/pci.h>
#include <kern/irq.h>
#include <kern/device-io.h>
#include <kern/kmem.h>
#include <kern/klog.h>

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The first device slot of a fake bus, and how many slots it has. */
#define TEST_SLOT_FIRST 2U
#define TEST_SLOT_COUNT 2U

/* The host's pins: vector v is status bit 24 + (v - 1), as on the PLDA host. */
#define TEST_PIN_COUNT 4U
#define TEST_PIN_SHIFT 24U

/* A status bit that is not a pin (the PLDA host's events and messages). */
#define TEST_EVENT_BIT (1U << 20)

/* How many fake timer ticks a host waits for a drain: one second of 1 ms. */
#define TEST_DRAIN_TICKS 1000U

/* The interrupt controller's number of the hookless bus's shared line. */
#define TEST_CONTROLLER_IRQ 33

/*
 * One pin entry in a fake host's table.
 *
 * run and line are empty while no line owns the pin; in_flight counts the
 * interrupts that read the entry and have not yet come back from run.
 */
struct fake_pin {
	drv_pci_intx_run_t run;
	void *line;
	unsigned in_flight;
};

/*
 * A fake host that demultiplexes its own pins.
 *
 * lock stands for the host's irq-save spin lock and guards the table, the
 * mask and the counters.  The device configuration space lives here too, so
 * two hosts can carry different devices on the same slots.
 */
struct fake_host {
	pthread_mutex_t lock;
	struct fake_pin pins[TEST_PIN_COUNT];
	/* Bit v - 1 set: the pin of vector v may interrupt. */
	unsigned enabled_pins;
	/* The status word the next interrupt reads. */
	uint32_t status;
	/* The status bits each slot's device raises, written by allocate_irqs. */
	unsigned slot_vector[TEST_SLOT_COUNT];
	uint8_t config[TEST_SLOT_COUNT][256];
	unsigned establish_calls;
	unsigned disestablish_calls;
	unsigned acknowledge_calls;
	/* Handler calls seen before the status was acknowledged in this interrupt. */
	unsigned handlers_before_acknowledge;
	/* The pin whose interrupt stops between in_flight++ and run, or zero. */
	unsigned park_vector;
	/* Set when the parked interrupt has stopped; cleared to let it go on. */
	unsigned parked;
	unsigned released;
	pthread_cond_t park_changed;
};

/*
 * One device's handler as the test sees it.
 *
 * remove_self asks the handler to take itself off from inside the dispatch,
 * and remove_result keeps what the core answered.
 */
struct handler_fixture {
	struct fake_host *host;
	struct drv_pci_device *device;
	void *cookie;
	unsigned calls;
	unsigned remove_self;
	int remove_result;
};

/* The arguments of the thread that delivers a parked interrupt. */
struct interrupt_thread {
	struct fake_host *host;
	kern_irq_ack_t acknowledge;
};

/*
 * The fake clock's ticks and the tick that releases a parked interrupt.
 *
 * Only the thread waiting for a drain advances the clock; zero in
 * release_tick means the parked interrupt is never let go by the clock.
 */
static unsigned clock_ticks;
static unsigned release_tick;
static struct fake_host *release_host;

/*
 * The interrupt controller as the core sees it on the hookless path.
 *
 * The test is single-threaded on this path, so no lock guards these.
 */
static kern_irq_handler_t controller_handler;
static void *controller_argument;
static unsigned controller_register_calls;
static unsigned controller_unregister_calls;
static unsigned controller_mask_calls;
static unsigned controller_unmask_calls;

/*
 * The ends of interrupt sent, counted by every path.  The last
 * acknowledgement is kept to prove the right token came back.
 */
static unsigned eoi_calls;
static kern_irq_ack_t last_acknowledge;
static pthread_mutex_t eoi_lock = PTHREAD_MUTEX_INITIALIZER;

static int config_read(void *context, const struct drv_pci_address *address, unsigned offset, unsigned width, uint32_t *result);
static int config_write(void *context, const struct drv_pci_address *address, unsigned offset, unsigned width, uint32_t value);
static int allocate_irqs(void *context, struct drv_pci_device *device, enum drv_pci_irq_type type, unsigned minimum, unsigned maximum, struct drv_pci_irq *irqs, unsigned *count);
static int host_intx_establish(void *context, unsigned vector, drv_pci_intx_run_t run, void *line);
static int host_intx_disestablish(void *context, unsigned vector, drv_pci_intx_run_t run, void *line);
static void host_intx_mask(void *context, unsigned vector);
static void host_intx_unmask(void *context, unsigned vector);
static void host_interrupt(struct fake_host *host, kern_irq_ack_t acknowledge);
static void *interrupt_thread_main(void *argument);
static void clock_tick(void);
static int fixture_handler(void *argument);
static void host_init(struct fake_host *host, unsigned first_vector, unsigned second_vector);
static struct drv_pci_bus *bus_create(uint16_t segment, const struct drv_pci_bus_ops *ops, struct fake_host *host);
static void device_attach(struct handler_fixture *fixture, struct fake_host *host, uint16_t segment, unsigned slot);
static void device_detach(struct handler_fixture *fixture);
static void test_two_pins_at_once(const struct drv_pci_bus_ops *ops);
static void test_event_and_empty_status(const struct drv_pci_bus_ops *ops);
static void test_two_hosts_same_pin(const struct drv_pci_bus_ops *ops);
static void test_drain(const struct drv_pci_bus_ops *ops);
static void test_establish_errors(void);
static void test_controller_path(void);

/*
 * Allocates kernel memory for the core.
 */
void *
kern_malloc(
	size_t size)
{
	void *memory;

	/* The host's allocator stands in for the kernel's. */
	memory = malloc(size);

	/* Succeeded or not, the caller checks for NULL. */
	return memory;
}

/*
 * Allocates zeroed kernel memory for the core.
 */
void *
kern_calloc(
	size_t count,
	size_t size)
{
	void *memory;

	/* The host's allocator stands in for the kernel's. */
	memory = calloc(count, size);

	/* Succeeded or not, the caller checks for NULL. */
	return memory;
}

/*
 * Frees kernel memory for the core.
 */
void
kern_free(
	void *pointer)
{
	/* The host's allocator stands in for the kernel's. */
	free(pointer);
}

/*
 * Drops a kernel log line; the test reads only its own assertions.
 */
void
kern_logf(
	const char *format,
	...)
{
	(void)format;
}

/*
 * Reports interrupts as enabled; the threads of the test stand for harts.
 */
bool
kern_irq_disable(
	void)
{
	/* Each caller gives back what it saw, which keeps enable balanced. */
	return true;
}

/*
 * Enables interrupts, which the fake harts do not model.
 */
void
kern_irq_enable(
	void)
{
}

/*
 * Masks a controller line: only the hookless path may get here.
 */
void
kern_irq_mask(
	int irq)
{
	/* A hooked host's pin never reaches the controller. */
	assert(irq == TEST_CONTROLLER_IRQ);
	controller_mask_calls++;
}

/*
 * Unmasks a controller line: only the hookless path may get here.
 */
void
kern_irq_unmask(
	int irq)
{
	/* A hooked host's pin never reaches the controller. */
	assert(irq == TEST_CONTROLLER_IRQ);
	controller_unmask_calls++;
}

/*
 * Counts an end of interrupt and keeps its token.
 */
void
kern_irq_send_eoi(
	kern_irq_ack_t acknowledge)
{
	/* Counts the end of interrupt with the token it carried. */
	pthread_mutex_lock(&eoi_lock);

	eoi_calls++;
	last_acknowledge = acknowledge;

	pthread_mutex_unlock(&eoi_lock);
}

/*
 * Stands in for a device-I/O barrier.
 */
void
kern_io_barrier(
	void)
{
}

/*
 * Stands in for a device-I/O read barrier.
 */
void
kern_io_read_barrier(
	void)
{
}

/*
 * Installs the controller line's handler: only the hookless path may get here.
 */
int
kern_irq_register(
	int irq,
	kern_irq_handler_t handler,
	void *argument)
{
	/* A hooked host's pin never reaches the controller. */
	assert(irq == TEST_CONTROLLER_IRQ);
	assert(controller_handler == NULL);

	/* Keeps the handler the controller would call. */
	controller_handler = handler;
	controller_argument = argument;
	controller_register_calls++;

	/* Succeeded: the controller line has its handler. */
	return 0;
}

/*
 * Removes the controller line's handler: only the hookless path may get here.
 */
int
kern_irq_unregister(
	int irq,
	kern_irq_handler_t handler,
	void *argument)
{
	/* The removal names the handler the core installed. */
	assert(irq == TEST_CONTROLLER_IRQ);
	assert(handler == controller_handler);
	assert(argument == controller_argument);

	/* Forgets the handler. */
	controller_handler = NULL;
	controller_argument = NULL;
	controller_unregister_calls++;

	/* Succeeded: the controller line has no handler. */
	return 0;
}

/*
 * Refuses a message interrupt: the fake devices have only pins.
 */
int
kern_irq_register_msi(
	const char *source,
	kern_irq_handler_t handler,
	void *argument,
	int *irq,
	uint64_t *address,
	uint32_t *event)
{
	(void)source;
	(void)handler;
	(void)argument;
	(void)irq;
	(void)address;
	(void)event;

	/* No message interrupt is ever asked for. */
	return ENOTSUP;
}

/*
 * Refuses a message interrupt removal: none was ever installed.
 */
int
kern_irq_unregister_msi(
	int irq)
{
	(void)irq;

	/* No message interrupt is ever installed. */
	return ENOTSUP;
}

/*
 * Runs every case and reports the result.
 */
int
main(
	void)
{
	static struct drv_pci_bus_ops hooked_ops;

	/* A host with all four pin hooks. */
	memset(&hooked_ops, 0, sizeof(hooked_ops));
	hooked_ops.config_read = config_read;
	hooked_ops.config_write = config_write;
	hooked_ops.allocate_irqs = allocate_irqs;
	hooked_ops.intx_establish = host_intx_establish;
	hooked_ops.intx_disestablish = host_intx_disestablish;
	hooked_ops.intx_mask = host_intx_mask;
	hooked_ops.intx_unmask = host_intx_unmask;

	/* Starts the PCI core once; every case adds its own root buses. */
	assert(drv_pci_init() == 0);

	/* 1: two pins raised in one interrupt run both lines, then one W1C and one EOI. */
	test_two_pins_at_once(&hooked_ops);

	/* 2: an event bit alone, and an empty status, still W1C and EOI once. */
	test_event_and_empty_status(&hooked_ops);

	/* 3: two hosts with the same pin number keep separate lines. */
	test_two_hosts_same_pin(&hooked_ops);

	/* 4 and 5: the host drains its own dispatch; handlers run without the host lock. */
	test_drain(&hooked_ops);

	/* The core refuses a host with only some hooks and maps a host's refusal. */
	test_establish_errors();

	/* 6: a host without hooks keeps the controller's path, one EOI per interrupt. */
	test_controller_path();

	/* Succeeded: every case held. */
	printf("pci intx hook: PASS\n");
	return 0;
}

/* Reads a fake device's configuration space; empty slots read as all ones. */
static int
config_read(
	void *context,
	const struct drv_pci_address *address,
	unsigned offset,
	unsigned width,
	uint32_t *result)
{
	struct fake_host *host;
	unsigned slot;

	host = context;

	/* An empty slot reads as no device. */
	if (address->bus != 0 ||
	    address->function != 0 ||
	    address->device < TEST_SLOT_FIRST ||
	    address->device >= TEST_SLOT_FIRST + TEST_SLOT_COUNT) {
		*result = UINT32_MAX;
		return 0;
	}

	/* Reads the bytes of the slot's space. */
	slot = address->device - TEST_SLOT_FIRST;
	assert(offset + width <= sizeof(host->config[slot]));
	*result = 0;
	memcpy(result, host->config[slot] + offset, width);

	/* Succeeded: the value read. */
	return 0;
}

/* Writes a fake device's configuration space; the devices have no BARs. */
static int
config_write(
	void *context,
	const struct drv_pci_address *address,
	unsigned offset,
	unsigned width,
	uint32_t value)
{
	struct fake_host *host;
	unsigned slot;

	host = context;

	/* Refuses a write to an empty slot. */
	if (address->bus != 0 ||
	    address->function != 0 ||
	    address->device < TEST_SLOT_FIRST ||
	    address->device >= TEST_SLOT_FIRST + TEST_SLOT_COUNT)
		return EINVAL;

	/* A BAR probe reads back zero: no BAR is implemented. */
	slot = address->device - TEST_SLOT_FIRST;
	assert(offset + width <= sizeof(host->config[slot]));
	if (offset >= 0x10U &&
	    offset < 0x28U &&
	    width == 4U &&
	    value == UINT32_MAX)
		value = 0;

	/* Writes the bytes into the slot's space. */
	memcpy(host->config[slot] + offset, &value, width);

	/* Succeeded: the value written. */
	return 0;
}

/* Gives a device its pin: the vector is the host's own pin number for the slot. */
static int
allocate_irqs(
	void *context,
	struct drv_pci_device *device,
	enum drv_pci_irq_type type,
	unsigned minimum,
	unsigned maximum,
	struct drv_pci_irq *irqs,
	unsigned *count)
{
	struct drv_pci_address address;
	struct fake_host *host;

	host = context;

	/* Refuses anything but one pin interrupt. */
	if (type != DRV_PCI_IRQ_INTX || minimum != 1U || maximum < 1U)
		return ENOTSUP;

	/* Reports the slot's pin. */
	drv_pci_device_address(device, &address);
	irqs[0].type = DRV_PCI_IRQ_INTX;
	irqs[0].index = 0;
	irqs[0].vector = host->slot_vector[address.device - TEST_SLOT_FIRST];
	irqs[0].private_data[0] = 0;
	irqs[0].private_data[1] = 0;
	*count = 1;

	/* Succeeded: one pin. */
	return 0;
}

/* Puts a line in the host's table, leaving its pin masked. */
static int
host_intx_establish(
	void *context,
	unsigned vector,
	drv_pci_intx_run_t run,
	void *line)
{
	struct fake_host *host;
	struct fake_pin *pin;
	int error;

	host = context;

	/* Refuses a pin the host does not have. */
	if (vector < 1U || vector > TEST_PIN_COUNT)
		return EINVAL;

	/* Fills the pin's entry unless another line holds it. */
	pthread_mutex_lock(&host->lock);

	pin = &host->pins[vector - 1U];
	error = 0;
	if (pin->run != NULL) {
		error = EBUSY;
	} else {
		pin->run = run;
		pin->line = line;
		host->establish_calls++;
	}

	pthread_mutex_unlock(&host->lock);

	/* Failed: the pin's entry belongs to another line. */
	if (error != 0)
		return error;

	/* Succeeded: the line is in the table, its pin still masked. */
	return 0;
}

/* Empties a pin's entry and waits until no interrupt still runs it. */
static int
host_intx_disestablish(
	void *context,
	unsigned vector,
	drv_pci_intx_run_t run,
	void *line)
{
	struct fake_host *host;
	struct fake_pin *pin;
	unsigned waited;
	unsigned in_flight;
	unsigned enabled_before;

	host = context;

	/* Refuses a pin the host does not have. */
	if (vector < 1U || vector > TEST_PIN_COUNT)
		return EINVAL;

	/* Masks the pin and empties its entry, after checking the entry is this line's. */
	pthread_mutex_lock(&host->lock);

	pin = &host->pins[vector - 1U];
	if (pin->run != run || pin->line != line) {
		pthread_mutex_unlock(&host->lock);

		return EINVAL;
	}
	enabled_before = host->enabled_pins;
	host->enabled_pins &= ~(1U << (vector - 1U));
	pin->run = NULL;
	pin->line = NULL;
	host->disestablish_calls++;

	pthread_mutex_unlock(&host->lock);

	/* Waits, one fake tick at a time, for the interrupts still inside run. */
	for (waited = 0; waited < TEST_DRAIN_TICKS; waited++) {
		/* Reads how many interrupts still hold the pin. */
		pthread_mutex_lock(&host->lock);

		in_flight = pin->in_flight;

		pthread_mutex_unlock(&host->lock);

		/* Drained: no hart runs the line and none will. */
		if (in_flight == 0)
			return 0;

		/* Lets the fake clock move on. */
		clock_tick();
	}

	/* Puts the entry and the pin back as they were before the call. */
	pthread_mutex_lock(&host->lock);

	pin->run = run;
	pin->line = line;
	host->enabled_pins = enabled_before;

	pthread_mutex_unlock(&host->lock);

	/* Failed: an interrupt did not come back within the bound. */
	return EBUSY;
}

/* Stops a pin from interrupting. */
static void
host_intx_mask(
	void *context,
	unsigned vector)
{
	struct fake_host *host;

	host = context;
	assert(vector >= 1U && vector <= TEST_PIN_COUNT);

	/* Clears the pin's enable bit. */
	pthread_mutex_lock(&host->lock);

	host->enabled_pins &= ~(1U << (vector - 1U));

	pthread_mutex_unlock(&host->lock);
}

/* Lets a pin interrupt. */
static void
host_intx_unmask(
	void *context,
	unsigned vector)
{
	struct fake_host *host;

	host = context;
	assert(vector >= 1U && vector <= TEST_PIN_COUNT);

	/* Sets the pin's enable bit. */
	pthread_mutex_lock(&host->lock);

	host->enabled_pins |= 1U << (vector - 1U);

	pthread_mutex_unlock(&host->lock);
}

/*
 * Serves one interrupt of the host's own line.
 *
 * It runs every raised and enabled pin's line without the host lock,
 * acknowledges the status after all of them, and ends the interrupt once,
 * whatever the status held.
 */
static void
host_interrupt(
	struct fake_host *host,
	kern_irq_ack_t acknowledge)
{
	drv_pci_intx_run_t run;
	struct fake_pin *pin;
	void *line;
	uint32_t status;
	unsigned vector;
	unsigned pin_bit;

	/* Reads the status the interrupt reports. */
	pthread_mutex_lock(&host->lock);

	status = host->status;

	pthread_mutex_unlock(&host->lock);

	/* Runs each pin the status raised and the core let through. */
	for (vector = 1; vector <= TEST_PIN_COUNT; vector++) {
		pin_bit = 1U << (TEST_PIN_SHIFT + vector - 1U);
		if ((status & pin_bit) == 0)
			continue;

		/* Takes the pin's entry and counts this interrupt on it. */
		pthread_mutex_lock(&host->lock);

		pin = &host->pins[vector - 1U];
		run = pin->run;
		line = pin->line;
		if ((host->enabled_pins & (1U << (vector - 1U))) == 0)
			run = NULL;
		if (run != NULL)
			pin->in_flight++;

		pthread_mutex_unlock(&host->lock);

		/* A masked or empty pin is not served. */
		if (run == NULL)
			continue;

		/* Stops here when the test asks, between in_flight++ and run. */
		pthread_mutex_lock(&host->lock);

		if (host->park_vector == vector) {
			host->parked = 1;
			pthread_cond_broadcast(&host->park_changed);
			while (!host->released)
				pthread_cond_wait(&host->park_changed, &host->lock);
		}

		pthread_mutex_unlock(&host->lock);

		/* Runs the line's handlers with the host lock released. */
		run(line);

		/* Gives the pin back. */
		pthread_mutex_lock(&host->lock);

		pin->in_flight--;

		pthread_mutex_unlock(&host->lock);
	}

	/* Acknowledges the status only after every handler, as a level pin needs. */
	pthread_mutex_lock(&host->lock);

	host->status &= ~status;
	host->acknowledge_calls++;

	pthread_mutex_unlock(&host->lock);

	/* Ends the host line's interrupt once. */
	kern_irq_send_eoi(acknowledge);
}

/* Delivers one interrupt on a thread of its own, standing for another hart. */
static void *
interrupt_thread_main(
	void *argument)
{
	struct interrupt_thread *thread;

	thread = argument;

	/* Serves the interrupt. */
	host_interrupt(thread->host, thread->acknowledge);

	/* Succeeded: the interrupt is over. */
	return NULL;
}

/* Moves the fake clock one tick, releasing a parked interrupt at its tick. */
static void
clock_tick(
	void)
{
	struct timespec pause;

	/* Counts the tick. */
	clock_ticks++;

	/* Lets the parked interrupt go on when its tick comes. */
	if (release_host != NULL &&
	    release_tick != 0 &&
	    clock_ticks == release_tick) {
		pthread_mutex_lock(&release_host->lock);

		release_host->released = 1;
		pthread_cond_broadcast(&release_host->park_changed);

		pthread_mutex_unlock(&release_host->lock);
	}

	/* Gives the other thread a moment to run. */
	pause.tv_sec = 0;
	pause.tv_nsec = 1000000L;
	nanosleep(&pause, NULL);
}

/* Counts a device's interrupt and checks that no host lock is held. */
static int
fixture_handler(
	void *argument)
{
	struct handler_fixture *fixture;
	int locked;

	fixture = argument;

	/* 5: the host called run with its own lock released. */
	if (fixture->host != NULL) {
		locked = pthread_mutex_trylock(&fixture->host->lock);
		assert(locked == 0);
		pthread_mutex_unlock(&fixture->host->lock);
	}

	/* Counts the call. */
	fixture->calls++;

	/* Takes the handler off from inside the dispatch when asked. */
	if (fixture->remove_self)
		fixture->remove_result = drv_pci_device_disestablish_irq_checked(fixture->device, fixture->cookie);

	/* The interrupt was this device's. */
	return 1;
}

/* Starts a fake host whose two slots raise the two given pins. */
static void
host_init(
	struct fake_host *host,
	unsigned first_vector,
	unsigned second_vector)
{
	uint32_t identity;
	uint32_t class_code;
	unsigned slot;

	/* Empties the host. */
	memset(host, 0, sizeof(*host));
	pthread_mutex_init(&host->lock, NULL);
	pthread_cond_init(&host->park_changed, NULL);

	/* Gives each slot a pin. */
	host->slot_vector[0] = first_vector;
	host->slot_vector[1] = second_vector;

	/* Puts a device with a vendor, a class and an interrupt pin in each slot that has a pin. */
	for (slot = 0; slot < TEST_SLOT_COUNT; slot++) {
		if (host->slot_vector[slot] == 0)
			continue;
		identity = ((0x2000U + slot) << 16) | 0x1234U;
		class_code = 0x0c030001U;
		memcpy(host->config[slot] + 0x00U, &identity, sizeof(identity));
		memcpy(host->config[slot] + 0x08U, &class_code, sizeof(class_code));
		host->config[slot][0x3dU] = (uint8_t)host->slot_vector[slot];
	}
}

/* Creates and scans a root bus on a host. */
static struct drv_pci_bus *
bus_create(
	uint16_t segment,
	const struct drv_pci_bus_ops *ops,
	struct fake_host *host)
{
	struct drv_pci_bus *bus;
	int error;

	/* Creates the root bus. */
	error = drv_pci_bus_create_root(segment, 0, ops, host, NULL, &bus);
	assert(error == 0);

	/* Finds the bus's devices. */
	error = drv_pci_bus_scan(bus);
	assert(error == 0);

	/* Succeeded: the scanned bus. */
	return bus;
}

/* Allocates a device's pin and puts the fixture's handler on it. */
static void
device_attach(
	struct handler_fixture *fixture,
	struct fake_host *host,
	uint16_t segment,
	unsigned slot)
{
	struct drv_pci_address address;
	struct drv_pci_irq irq;
	unsigned count;
	int error;

	/* Finds the device. */
	memset(fixture, 0, sizeof(*fixture));
	fixture->host = host;
	memset(&address, 0, sizeof(address));
	address.segment = segment;
	address.bus = 0;
	address.device = (uint8_t)slot;
	address.function = 0;
	fixture->device = drv_pci_find_device(&address);
	assert(fixture->device != NULL);

	/* Allocates its pin. */
	count = 0;
	error = drv_pci_device_allocate_irqs(fixture->device, DRV_PCI_IRQ_ALLOW_INTX, 1, 1, &irq, &count);
	assert(error == 0);
	assert(count == 1U);
	assert(irq.type == DRV_PCI_IRQ_INTX);

	/* Puts the handler on the pin. */
	error = drv_pci_device_establish_irq(fixture->device, &irq, fixture_handler, fixture, "intx-hook", &fixture->cookie);
	assert(error == 0);
}

/* Takes a fixture's handler off. */
static void
device_detach(
	struct handler_fixture *fixture)
{
	int error;

	/* Takes the handler off. */
	error = drv_pci_device_disestablish_irq_checked(fixture->device, fixture->cookie);
	assert(error == 0);
	fixture->cookie = NULL;
}

/* 1: two pins raised in one interrupt. */
static void
test_two_pins_at_once(
	const struct drv_pci_bus_ops *ops)
{
	static struct fake_host host;
	struct handler_fixture first;
	struct handler_fixture second;
	unsigned eoi_before;

	/* One host with INTA on slot 2 and INTB on slot 3. */
	host_init(&host, 1, 2);
	(void)bus_create(10, ops, &host);
	device_attach(&first, &host, 10, TEST_SLOT_FIRST);
	device_attach(&second, &host, 10, TEST_SLOT_FIRST + 1U);

	/* Each pin got its own line, installed in the host and let through. */
	assert(host.establish_calls == 2U);
	assert(host.enabled_pins == 0x3U);
	assert(controller_register_calls == 0U);

	/* Raises both pins in one interrupt. */
	eoi_before = eoi_calls;
	host.status = (1U << TEST_PIN_SHIFT) | (1U << (TEST_PIN_SHIFT + 1U));
	host_interrupt(&host, 0x101U);

	/* Both handlers ran once, the status was acknowledged once, and one EOI went out. */
	assert(first.calls == 1U);
	assert(second.calls == 1U);
	assert(host.acknowledge_calls == 1U);
	assert(host.status == 0U);
	assert(eoi_calls == eoi_before + 1U);
	assert(last_acknowledge == 0x101U);

	/* Takes both off: each was its line's last handler, so the host emptied both pins. */
	device_detach(&first);
	device_detach(&second);
	assert(host.disestablish_calls == 2U);
	assert(host.enabled_pins == 0U);
	assert(host.pins[0].run == NULL);
	assert(host.pins[1].run == NULL);
}

/* 2: an event bit alone, and a status of zero. */
static void
test_event_and_empty_status(
	const struct drv_pci_bus_ops *ops)
{
	static struct fake_host host;
	struct handler_fixture fixture;
	unsigned eoi_before;

	/* One host with INTA on slot 2. */
	host_init(&host, 1, 0);
	(void)bus_create(11, ops, &host);
	device_attach(&fixture, &host, 11, TEST_SLOT_FIRST);

	/* An event bit alone runs no handler but is acknowledged and ended once. */
	eoi_before = eoi_calls;
	host.status = TEST_EVENT_BIT;
	host_interrupt(&host, 0x201U);
	assert(fixture.calls == 0U);
	assert(host.acknowledge_calls == 1U);
	assert(eoi_calls == eoi_before + 1U);
	assert(last_acknowledge == 0x201U);

	/* A status of zero is acknowledged and ended once as well. */
	host.status = 0;
	host_interrupt(&host, 0x202U);
	assert(fixture.calls == 0U);
	assert(host.acknowledge_calls == 2U);
	assert(eoi_calls == eoi_before + 2U);
	assert(last_acknowledge == 0x202U);

	/* A pin the core masked is not served even when the status shows it. */
	host_intx_mask(&host, 1);
	host.status = 1U << TEST_PIN_SHIFT;
	host_interrupt(&host, 0x203U);
	assert(fixture.calls == 0U);
	assert(eoi_calls == eoi_before + 3U);
	host_intx_unmask(&host, 1);

	device_detach(&fixture);
}

/* 3: two hosts that number their pins the same way. */
static void
test_two_hosts_same_pin(
	const struct drv_pci_bus_ops *ops)
{
	static struct fake_host host_a;
	static struct fake_host host_b;
	struct handler_fixture on_a;
	struct handler_fixture on_b;
	struct handler_fixture shared_b;

	/* Both hosts carry INTA on slot 2; host B also has INTA on slot 3. */
	host_init(&host_a, 1, 0);
	host_init(&host_b, 1, 1);
	(void)bus_create(20, ops, &host_a);
	(void)bus_create(21, ops, &host_b);
	device_attach(&on_a, &host_a, 20, TEST_SLOT_FIRST);
	device_attach(&on_b, &host_b, 21, TEST_SLOT_FIRST);
	device_attach(&shared_b, &host_b, 21, TEST_SLOT_FIRST + 1U);

	/* Each host installed one line on its pin 1; B's second device shares B's line. */
	assert(host_a.establish_calls == 1U);
	assert(host_b.establish_calls == 1U);
	assert(host_a.pins[0].line != host_b.pins[0].line);

	/* A's interrupt runs only A's handler. */
	host_a.status = 1U << TEST_PIN_SHIFT;
	host_interrupt(&host_a, 0x301U);
	assert(on_a.calls == 1U);
	assert(on_b.calls == 0U);
	assert(shared_b.calls == 0U);

	/* B's interrupt runs both of B's handlers on its one line. */
	host_b.status = 1U << TEST_PIN_SHIFT;
	host_interrupt(&host_b, 0x302U);
	assert(on_a.calls == 1U);
	assert(on_b.calls == 1U);
	assert(shared_b.calls == 1U);

	/* Taking B's handlers off leaves A's line alone. */
	device_detach(&shared_b);
	assert(host_b.disestablish_calls == 0U);
	device_detach(&on_b);
	assert(host_b.disestablish_calls == 1U);
	assert(host_a.disestablish_calls == 0U);
	assert(host_a.pins[0].run != NULL);
	assert(host_a.enabled_pins == 0x1U);

	/* A still serves its pin. */
	host_a.status = 1U << TEST_PIN_SHIFT;
	host_interrupt(&host_a, 0x303U);
	assert(on_a.calls == 2U);

	device_detach(&on_a);
}

/* 4: the host drains its own dispatch; 5 is checked by every handler call. */
static void
test_drain(
	const struct drv_pci_bus_ops *ops)
{
	static struct fake_host host;
	struct handler_fixture fixture;
	struct interrupt_thread thread;
	pthread_t interrupt;
	unsigned eoi_before;
	unsigned disestablish_before;
	int error;

	/* One host with INTA on slot 2. */
	host_init(&host, 1, 0);
	(void)bus_create(30, ops, &host);
	device_attach(&fixture, &host, 30, TEST_SLOT_FIRST);

	/*
	 * (b) An interrupt parked between in_flight++ and run outlasts the
	 * bound: the host gives EBUSY back with its entry and pin restored,
	 * and the core leaves the line registered and let through.
	 */
	host.park_vector = 1;
	host.status = 1U << TEST_PIN_SHIFT;
	thread.host = &host;
	thread.acknowledge = 0x401U;
	error = pthread_create(&interrupt, NULL, interrupt_thread_main, &thread);
	assert(error == 0);
	pthread_mutex_lock(&host.lock);
	while (!host.parked)
		pthread_cond_wait(&host.park_changed, &host.lock);
	pthread_mutex_unlock(&host.lock);

	release_host = &host;
	release_tick = 0;
	clock_ticks = 0;
	error = drv_pci_device_disestablish_irq_checked(fixture.device, fixture.cookie);
	assert(error == EBUSY);
	assert(clock_ticks == TEST_DRAIN_TICKS);
	assert(host.pins[0].run != NULL);
	assert(host.enabled_pins == 0x1U);

	/* The parked interrupt goes on: the handler runs and one EOI goes out. */
	eoi_before = eoi_calls;
	pthread_mutex_lock(&host.lock);
	host.released = 1;
	pthread_cond_broadcast(&host.park_changed);
	pthread_mutex_unlock(&host.lock);
	error = pthread_join(interrupt, NULL);
	assert(error == 0);
	assert(fixture.calls == 1U);
	assert(eoi_calls == eoi_before + 1U);

	/* (c) Removal from inside the handler is refused by the core before the host. */
	disestablish_before = host.disestablish_calls;
	host.park_vector = 0;
	fixture.remove_self = 1;
	host.status = 1U << TEST_PIN_SHIFT;
	host_interrupt(&host, 0x402U);
	assert(fixture.remove_result == EBUSY);
	assert(host.disestablish_calls == disestablish_before);
	assert(host.pins[0].run != NULL);
	fixture.remove_self = 0;

	/*
	 * (a) An interrupt parked between in_flight++ and run makes the host
	 * wait; once the clock lets it go on, the removal succeeds.
	 */
	host.parked = 0;
	host.released = 0;
	host.park_vector = 1;
	host.status = 1U << TEST_PIN_SHIFT;
	thread.acknowledge = 0x403U;
	error = pthread_create(&interrupt, NULL, interrupt_thread_main, &thread);
	assert(error == 0);
	pthread_mutex_lock(&host.lock);
	while (!host.parked)
		pthread_cond_wait(&host.park_changed, &host.lock);
	pthread_mutex_unlock(&host.lock);

	release_tick = 10;
	clock_ticks = 0;
	error = drv_pci_device_disestablish_irq_checked(fixture.device, fixture.cookie);
	assert(error == 0);
	assert(clock_ticks >= 10U);
	assert(clock_ticks < TEST_DRAIN_TICKS);
	assert(host.pins[0].run == NULL);
	assert(host.pins[0].in_flight == 0U);
	assert(host.enabled_pins == 0U);
	error = pthread_join(interrupt, NULL);
	assert(error == 0);

	/* The interrupt that was in flight still ran its handler before the line went. */
	assert(fixture.calls == 3U);
	release_host = NULL;
	release_tick = 0;
}

/* The core refuses partial hooks and maps a host's refusals. */
static void
test_establish_errors(
	void)
{
	static struct drv_pci_bus_ops partial_ops;
	static struct fake_host host;
	struct drv_pci_address address;
	struct drv_pci_device *device;
	struct drv_pci_irq irq;
	void *cookie;
	unsigned count;
	int error;

	/* A host that set only some hooks. */
	memset(&partial_ops, 0, sizeof(partial_ops));
	partial_ops.config_read = config_read;
	partial_ops.config_write = config_write;
	partial_ops.allocate_irqs = allocate_irqs;
	partial_ops.intx_establish = host_intx_establish;
	partial_ops.intx_mask = host_intx_mask;

	/* Its device's pin is refused, and nothing reaches the host or the controller. */
	host_init(&host, 1, 5);
	(void)bus_create(40, &partial_ops, &host);
	memset(&address, 0, sizeof(address));
	address.segment = 40;
	address.device = TEST_SLOT_FIRST;
	device = drv_pci_find_device(&address);
	assert(device != NULL);
	count = 0;
	error = drv_pci_device_allocate_irqs(device, DRV_PCI_IRQ_ALLOW_INTX, 1, 1, &irq, &count);
	assert(error == 0);
	error = drv_pci_device_establish_irq(device, &irq, fixture_handler, NULL, "intx-hook", &cookie);
	assert(error == EINVAL);
	assert(host.establish_calls == 0U);
	assert(controller_register_calls == 0U);
}

/* 6: a host without hooks. */
static void
test_controller_path(
	void)
{
	static struct drv_pci_bus_ops controller_ops;
	static struct fake_host host;
	struct handler_fixture first;
	struct handler_fixture second;
	unsigned eoi_before;

	/* A host with no pin hooks whose two devices share the controller's line. */
	memset(&controller_ops, 0, sizeof(controller_ops));
	controller_ops.config_read = config_read;
	controller_ops.config_write = config_write;
	controller_ops.allocate_irqs = allocate_irqs;
	host_init(&host, TEST_CONTROLLER_IRQ, TEST_CONTROLLER_IRQ);
	(void)bus_create(50, &controller_ops, &host);
	device_attach(&first, NULL, 50, TEST_SLOT_FIRST);
	device_attach(&second, NULL, 50, TEST_SLOT_FIRST + 1U);

	/* One controller line, installed once and let through once. */
	assert(controller_register_calls == 1U);
	assert(controller_unmask_calls == 1U);
	assert(controller_handler != NULL);

	/* The controller's interrupt runs both handlers and ends with one EOI. */
	eoi_before = eoi_calls;
	controller_handler(TEST_CONTROLLER_IRQ, 0x501U, controller_argument);
	assert(first.calls == 1U);
	assert(second.calls == 1U);
	assert(eoi_calls == eoi_before + 1U);
	assert(last_acknowledge == 0x501U);

	/* The last handler masks and removes the controller line. */
	device_detach(&first);
	assert(controller_unregister_calls == 0U);
	device_detach(&second);
	assert(controller_mask_calls == 1U);
	assert(controller_unregister_calls == 1U);
	assert(controller_handler == NULL);
}
