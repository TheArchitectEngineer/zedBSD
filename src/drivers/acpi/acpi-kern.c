/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The ACPI driver's kernel side: the operating system services the AML
 * interpreter asks for (aml-os.h), the address space handlers for system
 * memory, system I/O and PCI configuration space, the SCI interrupt and
 * the thread that handles its events, and the attachment at boot that
 * finds the firmware's tables, loads them and starts the events and the
 * Embedded Controller.
 *
 * The RSDP comes from the platform as the boot handoff "acpi.rsdp" (the
 * physical address of the RSDP the HAL validated).  A platform that does
 * not give it has no ACPI, and the driver stays off.
 */

#include <stdarg.h>

#include <hal/hal.h>
#include <kern/kcrt.h>
#include <uapi/errno.h>

#include <drivers/acpi/acpi.h>
#include <drivers/pci/pci.h>

#include "kern/clock.h"
#include "kern/irq.h"
#include "kern/klog.h"
#include "kern/kmem.h"
#include "kern/lock.h"
#include "kern/platform.h"
#include "kern/sched.h"
#include "kern/thread.h"
#include "kern/waitq.h"

#include "acpi-tables.h"
#include "aml-internal.h"
#include "aml-os.h"

/*
 * The size of one page of the device mappings.
 */
#define PAGE_SIZE 4096U

/*
 * How many pages of system memory the handler keeps mapped.
 */
#define MEMORY_CACHE_SLOTS 16U

/*
 * The stack the interpreter may use below its entry, in bytes.
 *
 * A kernel thread has 16 KiB; the deepest real firmware measured on the
 * host needs about 4 KiB (ws049-p005), and AML that nests deeper fails
 * with E2BIG instead of overflowing.
 */
#define STACK_BUDGET (8U * 1024U)

/*
 * How long a log line may be.
 */
#define LOG_LINE_MAX 256U

/*
 * One page of system memory the handler has mapped.
 */
struct memory_mapping {
	uint64_t page;
	uint8_t *virtual_address;
	uint64_t used;
};

/*
 * The lock that serializes the interpreter.
 *
 * drv_acpi_attach() initializes it before the first table loads; the
 * interpreter takes it on entry and lets it go while AML sleeps.
 */
static struct mutex interpreter_lock;

/*
 * The firmware's tables, found at attachment and kept for LoadTable.
 */
static struct drv_acpi_firmware firmware;

/*
 * The pages of system memory mapped for operation regions, and the
 * counter that orders their use.  Only the handler, under the interpreter
 * lock, touches them; a slot with a NULL address is free.
 */
static struct memory_mapping memory_cache[MEMORY_CACHE_SLOTS];
static uint64_t memory_cache_clock;

/*
 * The lock the SCI interrupt and the event code share, the queue the
 * event thread sleeps on, and the number of SCIs it has not handled yet;
 * the thread takes the count, and the interrupt adds to it.
 */
static struct spinlock event_lock;
static struct wait_queue event_queue;
static unsigned event_work;

static int start_events(void);
static void sci_interrupt(int irq, kern_irq_ack_t acknowledge, void *argument);
static void event_thread(void *argument);
static void power_button(enum drv_acpi_fixed_event event, void *argument);
static int read_physical(uint64_t address, void *buffer, size_t length, void *argument);
static int memory_handler(const struct drv_acpi_region_access *access, uint64_t *value, void *argument);
static int memory_bytes(const struct drv_acpi_region_access *access, uint64_t *value);
static void memory_move(uint8_t *virtual_address, unsigned width, bool write, uint64_t *value);
static int memory_page(uint64_t page, uint8_t **virtual_address);
static int io_handler(const struct drv_acpi_region_access *access, uint64_t *value, void *argument);
static int pci_handler(const struct drv_acpi_region_access *access, uint64_t *value, void *argument);
static int pci_read(struct drv_pci_device *device, unsigned offset, unsigned width, uint64_t *value);
static int pci_write(struct drv_pci_device *device, unsigned offset, unsigned width, uint64_t value);

/*
 * Finds the firmware's ACPI tables, loads them, and initializes the
 * namespace and the devices.
 */
int
drv_acpi_attach(void)
{
	uint64_t *rsdp;
	int error;

	/* Stays off on a platform that gives no RSDP. */
	rsdp = kern_boot_handoff("acpi.rsdp");
	if (rsdp == NULL) {
		kern_logf("acpi: the platform gives no RSDP; ACPI is off\n");
		return ENODEV;
	}

	/* Prepares the lock before the interpreter first runs. */
	error = mutex_init(&interpreter_lock, LOCK_RANK_DEVICE, "acpi interpreter");
	if (error != 0)
		return error;

	/* Finds the tables from the RSDP. */
	error = drv_acpi_firmware_discover(*rsdp, read_physical, NULL, &firmware);
	if (error != 0) {
		kern_logf("acpi: no usable tables at RSDP 0x%llx (error %d)\n", (unsigned long long)*rsdp, error);
		return error;
	}

	/* Installs the handlers of the spaces the kernel reaches directly. */
	error = drv_acpi_region_install(DRV_ACPI_SPACE_SYSTEM_MEMORY, memory_handler, NULL);
	if (error == 0)
		error = drv_acpi_region_install(DRV_ACPI_SPACE_SYSTEM_IO, io_handler, NULL);
	if (error == 0)
		error = drv_acpi_region_install(DRV_ACPI_SPACE_PCI_CONFIG, pci_handler, NULL);
	if (error != 0)
		return error;

	/* Loads the DSDT and the SSDTs. */
	error = drv_acpi_firmware_load(&firmware);
	if (error != 0) {
		kern_logf("acpi: the DSDT did not load (error %d)\n", error);
		return error;
	}

	/* Prepares the objects, tells firmware the spaces are there, and runs _INI. */
	error = drv_acpi_initialize_objects();
	if (error != 0)
		kern_logf("acpi: object preparation failed (error %d)\n", error);
	error = drv_acpi_region_connect_all();
	if (error != 0)
		kern_logf("acpi: _REG failed (error %d)\n", error);
	drv_acpi_initialize_devices();

	/* Starts the SCI and its thread, then the Embedded Controller. */
	error = start_events();
	if (error != 0)
		kern_logf("acpi: no ACPI events (error %d)\n", error);
	error = drv_acpi_ec_attach();
	if (error != 0 && error != ENODEV)
		kern_logf("acpi: the Embedded Controller did not attach (error %d)\n", error);

	/* Succeeded. */
	kern_logf("acpi: %u tables listed, namespace ready\n", firmware.count);
	return 0;
}

/*
 * Allocates interpreter memory from the kernel heap.
 */
void *
drv_acpi_os_alloc(
	size_t size)
{
	void *pointer;

	/* Allocates it. */
	pointer = kern_malloc(size);

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
	/* Freeing nothing is allowed. */
	if (pointer == NULL)
		return;

	/* Frees it. */
	kern_free(pointer);
}

/*
 * Writes a line of the interpreter's log to the kernel log.
 */
void
drv_acpi_os_log(
	const char *format,
	...)
{
	char line[LOG_LINE_MAX];
	va_list arguments;

	/* Formats the line. */
	va_start(arguments, format);
	kern_vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);

	/* Writes it. */
	kern_logf("%s", line);
}

/*
 * Reports the stack budget of the interpreter.
 */
size_t
drv_acpi_os_stack_budget(void)
{
	/* Reports the budget. */
	return STACK_BUDGET;
}

/*
 * Sleeps for a number of milliseconds; the interpreter has let its lock go.
 */
void
drv_acpi_os_sleep(
	uint64_t milliseconds)
{
	uint64_t ticks;

	/* Sleeps until the tick the time ends at. */
	ticks = kern_ms_to_ticks(milliseconds);
	sched_sleep(sched_ticks() + ticks);
}

/*
 * Waits for a number of microseconds without sleeping.
 */
void
drv_acpi_os_stall(
	uint64_t microseconds)
{
	unsigned wait;

	/* A stall longer than the counter's range is cut, as firmware stalls are short. */
	wait = (unsigned)microseconds;
	if (microseconds > 0xffffffffULL)
		wait = 0xffffffffU;

	/* Spins on the monotonic counter. */
	kern_usleep_range(wait, wait);
}

/*
 * Reads the monotonic clock in the 100-nanosecond units of the Timer
 * operator.
 */
uint64_t
drv_acpi_os_timer(void)
{
	uint64_t counter;
	uint64_t frequency;
	uint64_t units;
	bool available;

	/* Reads the counter; without one the clock stands still. */
	available = kern_rtc_read_counter(&counter, &frequency);
	if (!available || frequency == 0)
		return 0;

	/* Converts whole seconds and the rest separately so that nothing overflows. */
	units = (counter / frequency) * 10000000ULL;
	units += ((counter % frequency) * 10000000ULL) / frequency;

	/* Reports the time. */
	return units;
}

/*
 * Takes the interpreter lock.
 */
void
drv_acpi_os_lock(void)
{
	/* Takes it, sleeping while another thread runs AML. */
	mutex_lock(&interpreter_lock);
}

/*
 * Lets the interpreter lock go.
 */
void
drv_acpi_os_unlock(void)
{
	/* Lets it go. */
	mutex_unlock(&interpreter_lock);
}

/*
 * Reports whether the calling thread holds the interpreter lock.
 */
bool
drv_acpi_os_lock_owned(void)
{
	int owned;

	/* Asks the mutex. */
	owned = mutex_owned(&interpreter_lock);
	if (owned)
		return true;

	/* Reports that another thread, or none, holds it. */
	return false;
}

/*
 * Reads an I/O port for the event and EC code.
 */
int
drv_acpi_os_port_read(
	uint32_t port,
	unsigned width,
	uint32_t *value)
{
	/* Refuses a port beyond the 64 KiB space. */
	if (port > 0xffffU)
		return EFAULT;

	/* Reads at the width. */
	switch (width) {
	case 8:
		*value = hal_io_inp8((uint16_t)port);
		return 0;
	case 16:
		*value = hal_io_inp16((uint16_t)port);
		return 0;
	case 32:
		*value = hal_io_inp32((uint16_t)port);
		return 0;
	default:
		break;
	}

	/* Refuses another width. */
	return EINVAL;
}

/*
 * Writes an I/O port for the event and EC code.
 */
int
drv_acpi_os_port_write(
	uint32_t port,
	unsigned width,
	uint32_t value)
{
	/* Refuses a port beyond the 64 KiB space. */
	if (port > 0xffffU)
		return EFAULT;

	/* Writes at the width. */
	switch (width) {
	case 8:
		hal_io_outp8((uint16_t)port, (uint8_t)value);
		return 0;
	case 16:
		hal_io_outp16((uint16_t)port, (uint16_t)value);
		return 0;
	case 32:
		hal_io_outp32((uint16_t)port, value);
		return 0;
	default:
		break;
	}

	/* Refuses another width. */
	return EINVAL;
}

/*
 * Takes the lock the SCI interrupt shares with the event code.
 */
unsigned long
drv_acpi_os_event_lock(void)
{
	unsigned long state;

	/* Takes it with interrupts off, as the interrupt takes it too. */
	state = spin_lock_irqsave(&event_lock);

	/* Reports the interrupt state to restore. */
	return state;
}

/*
 * Lets the event lock go.
 */
void
drv_acpi_os_event_unlock(
	unsigned long state)
{
	/* Lets it go and restores the interrupt state. */
	spin_unlock_irqrestore(&event_lock, state);
}

/*
 * Finds a table LoadTable asks for among the tables the firmware lists.
 */
int
drv_acpi_os_table(
	const char *signature,
	const char *oem_id,
	const char *oem_table_id,
	const uint8_t **data,
	size_t *length)
{
	int error;

	/* Looks it up in the root table's list. */
	error = drv_acpi_firmware_find(&firmware, signature, oem_id, oem_table_id, data, length);
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Reads the event hardware and starts the event thread and the SCI. */
static int
start_events(void)
{
	struct thread *thread;
	unsigned irq;
	int error;

	/* The event lock and queue exist before anything takes them. */
	spin_init(&event_lock, LOCK_RANK_DEVICE, "acpi event");
	waitq_init(&event_queue, "acpi event");

	/* Reads the hardware and enables the runtime GPEs. */
	error = drv_acpi_events_init(firmware.fadt, firmware.fadt_length);
	if (error != 0)
		return error;

	/* Starts the thread that handles the events. */
	error = kthread_create(event_thread, NULL, SCHED_PRIORITY_DEFAULT, &thread);
	if (error != 0)
		return error;

	/* Takes the SCI. */
	irq = drv_acpi_sci_irq();
	error = kern_irq_register((int)irq, sci_interrupt, NULL);
	if (error != 0)
		return error;

	/* Logs the power button until a driver takes it. */
	error = drv_acpi_fixed_event_install(DRV_ACPI_EVENT_POWER_BUTTON, power_button, NULL);
	if (error != 0 && error != ENODEV)
		return error;

	/* Succeeded. */
	kern_logf("acpi: SCI on IRQ %u\n", irq);
	return 0;
}

/* The SCI: masks and records the events that fired, then wakes the thread. */
static void
sci_interrupt(
	int irq,
	kern_irq_ack_t acknowledge,
	void *argument)
{
	unsigned long state;
	bool pending;

	UNUSED_PARAMETER(irq);
	UNUSED_PARAMETER(argument);

	/* Masks what fired; the level SCI goes quiet with it. */
	pending = drv_acpi_sci_interrupt();

	/* Hands the work to the thread. */
	if (pending) {
		state = spin_lock_irqsave(&event_lock);
		event_work++;
		waitq_wake_all(&event_queue);
		spin_unlock_irqrestore(&event_lock, state);
	}

	/* Ends the interrupt. */
	kern_irq_send_eoi(acknowledge);
}

/* Handles the events the SCI recorded, for the life of the system. */
static void
event_thread(
	void *argument)
{
	unsigned long state;
	uint64_t sequence;

	UNUSED_PARAMETER(argument);

	/* Sleeps until the SCI records work, then handles it. */
	for (;;) {
		/* Waits for work. */
		state = spin_lock_irqsave(&event_lock);
		while (event_work == 0) {
			sequence = waitq_sequence(&event_queue);
			(void)waitq_sleep(&event_queue, &event_lock, sequence, 0, 0);
		}

		/* The work counted so far is taken as a whole. */
		event_work = 0;
		spin_unlock_irqrestore(&event_lock, state);

		/* Runs the handlers and the AML. */
		drv_acpi_events_process();
	}
}

/* Logs a press of the power button; the power management WS will act on it. */
static void
power_button(
	enum drv_acpi_fixed_event event,
	void *argument)
{
	UNUSED_PARAMETER(event);
	UNUSED_PARAMETER(argument);

	/* Logs it. */
	kern_logf("acpi: power button\n");
}

/* Reads physical memory for the table finder, one page at a time. */
static int
read_physical(
	uint64_t address,
	void *buffer,
	size_t length,
	void *argument)
{
	uint8_t *destination;
	void *mapping;
	uint64_t page;
	size_t offset;
	size_t part;
	int error;

	UNUSED_PARAMETER(argument);

	/* Copies each page's part of the range. */
	destination = buffer;
	while (length != 0) {
		page = address & ~(uint64_t)(PAGE_SIZE - 1U);
		offset = (size_t)(address - page);
		part = PAGE_SIZE - offset;
		if (part > length)
			part = length;

		/* Maps the page for reading. */
		error = hal_space_map_device((hal_physaddr_t)page, PAGE_SIZE, HAL_SPACE_READ, &mapping);
		if (error != HAL_OK)
			return EFAULT;

		/* Copies the part, then lets the mapping go. */
		kern_memcpy(destination, (const uint8_t *)mapping + offset, part);
		hal_space_unmap_device(mapping, PAGE_SIZE);
		destination += part;
		address += part;
		length -= part;
	}

	/* Succeeded. */
	return 0;
}

/* Reads or writes system memory for an operation region. */
static int
memory_handler(
	const struct drv_acpi_region_access *access,
	uint64_t *value,
	void *argument)
{
	uint8_t *virtual_address;
	uint64_t page;
	uint64_t last_page;
	unsigned bytes;
	int error;

	UNUSED_PARAMETER(argument);

	/* An access that crosses a page is made one byte at a time. */
	bytes = access->width / 8U;
	page = access->address & ~(uint64_t)(PAGE_SIZE - 1U);
	last_page = (access->address + bytes - 1U) & ~(uint64_t)(PAGE_SIZE - 1U);
	if (last_page != page) {
		error = memory_bytes(access, value);
		return error;
	}

	/* Maps the page. */
	error = memory_page(page, &virtual_address);
	if (error != 0)
		return error;
	virtual_address += access->address - page;

	/* Makes the access at its width. */
	memory_move(virtual_address, access->width, access->write, value);

	/* Succeeded. */
	return 0;
}

/* Moves an access that crosses a page one byte at a time. */
static int
memory_bytes(
	const struct drv_acpi_region_access *access,
	uint64_t *value)
{
	uint8_t *virtual_address;
	uint64_t address;
	uint64_t byte;
	unsigned bytes;
	unsigned index;
	int error;

	/* A read starts from zero. */
	bytes = access->width / 8U;
	if (!access->write)
		*value = 0;

	/* Moves each byte through its own page. */
	for (index = 0; index < bytes; index++) {
		address = access->address + index;

		/* Maps the byte's page. */
		error = memory_page(address & ~(uint64_t)(PAGE_SIZE - 1U), &virtual_address);
		if (error != 0)
			return error;
		virtual_address += address % PAGE_SIZE;

		/* Moves the byte. */
		byte = (*value >> (index * 8U)) & 0xffU;
		memory_move(virtual_address, 8, access->write, &byte);
		if (!access->write)
			*value |= byte << (index * 8U);
	}

	/* Succeeded: every byte moved. */
	return 0;
}

/* Reads or writes mapped memory at one width. */
static void
memory_move(
	uint8_t *virtual_address,
	unsigned width,
	bool write,
	uint64_t *value)
{
	/* Chooses the accessor by the width. */
	switch (width) {
	case 8:
		if (write) {
			hal_mmio_write8(virtual_address, (uint8_t)*value);
		} else {
			*value = hal_mmio_read8(virtual_address);
		}

		break;
	case 16:
		if (write) {
			hal_mmio_write16(virtual_address, (uint16_t)*value);
		} else {
			*value = hal_mmio_read16(virtual_address);
		}

		break;
	case 32:
		if (write) {
			hal_mmio_write32(virtual_address, (uint32_t)*value);
		} else {
			*value = hal_mmio_read32(virtual_address);
		}

		break;
	default:
		if (write) {
			hal_mmio_write64(virtual_address, *value);
		} else {
			*value = hal_mmio_read64(virtual_address);
		}

		break;
	}
}

/* Finds a mapping of a page of system memory, mapping it in place of the oldest. */
static int
memory_page(
	uint64_t page,
	uint8_t **virtual_address)
{
	struct memory_mapping *slot;
	struct memory_mapping *oldest;
	void *mapping;
	unsigned index;
	int error;

	/* Uses a mapping of the page when there is one. */
	oldest = &memory_cache[0];
	memory_cache_clock++;
	for (index = 0; index < MEMORY_CACHE_SLOTS; index++) {
		slot = &memory_cache[index];

		/* Reports the cached page. */
		if (slot->virtual_address != NULL && slot->page == page) {
			slot->used = memory_cache_clock;
			*virtual_address = slot->virtual_address;
			return 0;
		}

		/* Remembers the least recently used slot, a free one first. */
		if (slot->virtual_address == NULL || slot->used < oldest->used)
			oldest = slot;
	}

	/* Maps the page uncached, as firmware memory and devices want it. */
	error = hal_space_map_device((hal_physaddr_t)page, PAGE_SIZE, HAL_SPACE_READ | HAL_SPACE_WRITE | HAL_SPACE_NOCACHE, &mapping);
	if (error != HAL_OK)
		return EFAULT;

	/* Lets the oldest mapping go and keeps the new one in its slot. */
	if (oldest->virtual_address != NULL)
		hal_space_unmap_device(oldest->virtual_address, PAGE_SIZE);
	oldest->page = page;
	oldest->virtual_address = mapping;
	oldest->used = memory_cache_clock;

	/* Succeeded. */
	*virtual_address = mapping;
	return 0;
}

/* Reads or writes system I/O ports for an operation region. */
static int
io_handler(
	const struct drv_acpi_region_access *access,
	uint64_t *value,
	void *argument)
{
	uint16_t port;

	UNUSED_PARAMETER(argument);

	/* Refuses a port beyond the 64 KiB space. */
	if (access->address > 0xffffU)
		return EFAULT;
	port = (uint16_t)access->address;

	/* Makes the access at its width; 64 bits are two double words. */
	switch (access->width) {
	case 8:
		if (access->write) {
			hal_io_outp8(port, (uint8_t)*value);
		} else {
			*value = hal_io_inp8(port);
		}

		break;
	case 16:
		if (access->write) {
			hal_io_outp16(port, (uint16_t)*value);
		} else {
			*value = hal_io_inp16(port);
		}

		break;
	case 32:
		if (access->write) {
			hal_io_outp32(port, (uint32_t)*value);
		} else {
			*value = hal_io_inp32(port);
		}

		break;
	default:
		if (access->write) {
			hal_io_outp32(port, (uint32_t)*value);
			hal_io_outp32((uint16_t)(port + 4U), (uint32_t)(*value >> 32));
		} else {
			*value = hal_io_inp32(port);
			*value |= (uint64_t)hal_io_inp32((uint16_t)(port + 4U)) << 32;
		}

		break;
	}

	/* Succeeded. */
	return 0;
}

/* Reads or writes PCI configuration space for an operation region. */
static int
pci_handler(
	const struct drv_acpi_region_access *access,
	uint64_t *value,
	void *argument)
{
	struct drv_pci_address address;
	struct drv_pci_device *device;
	int error;

	UNUSED_PARAMETER(argument);

	/* Finds the function the region belongs to among the enumerated ones. */
	address.segment = access->pci_segment;
	address.bus = access->pci_bus;
	address.device = access->pci_device;
	address.function = access->pci_function;
	device = drv_pci_find_device(&address);
	if (device == NULL) {
		kern_logf("acpi: PCI_Config region of absent function %x:%x.%x\n",
			  access->pci_bus, access->pci_device, access->pci_function);
		return ENODEV;
	}

	/* Refuses an offset outside the extended configuration space. */
	if (access->address > 0xfffU)
		return EFAULT;

	/* Makes the access. */
	if (access->write) {
		error = pci_write(device, (unsigned)access->address, access->width, *value);
	} else {
		error = pci_read(device, (unsigned)access->address, access->width, value);
	}

	/* Reports a failed access. */
	if (error != 0)
		return error;

	/* Succeeded. */
	return 0;
}

/* Reads configuration space at one width; 64 bits are two double words. */
static int
pci_read(
	struct drv_pci_device *device,
	unsigned offset,
	unsigned width,
	uint64_t *value)
{
	uint32_t low;
	uint32_t high;
	uint16_t word;
	uint8_t byte;
	int error;

	/* Chooses the access by the width. */
	switch (width) {
	case 8:
		error = drv_pci_device_config_read8(device, offset, &byte);
		*value = byte;
		break;
	case 16:
		error = drv_pci_device_config_read16(device, offset, &word);
		*value = word;
		break;
	case 32:
		error = drv_pci_device_config_read32(device, offset, &low);
		*value = low;
		break;
	default:
		error = drv_pci_device_config_read32(device, offset, &low);
		if (error != 0)
			break;
		error = drv_pci_device_config_read32(device, offset + 4U, &high);
		*value = (uint64_t)low | (uint64_t)high << 32;
		break;
	}

	/* Reports a failed read. */
	if (error != 0)
		return EIO;

	/* Succeeded. */
	return 0;
}

/* Writes configuration space at one width; 64 bits are two double words. */
static int
pci_write(
	struct drv_pci_device *device,
	unsigned offset,
	unsigned width,
	uint64_t value)
{
	int error;

	/* Chooses the access by the width. */
	switch (width) {
	case 8:
		error = drv_pci_device_config_write8(device, offset, (uint8_t)value);
		break;
	case 16:
		error = drv_pci_device_config_write16(device, offset, (uint16_t)value);
		break;
	case 32:
		error = drv_pci_device_config_write32(device, offset, (uint32_t)value);
		break;
	default:
		error = drv_pci_device_config_write32(device, offset, (uint32_t)value);
		if (error != 0)
			break;
		error = drv_pci_device_config_write32(device, offset + 4U, (uint32_t)(value >> 32));
		break;
	}

	/* Reports a failed write. */
	if (error != 0)
		return EIO;

	/* Succeeded. */
	return 0;
}
