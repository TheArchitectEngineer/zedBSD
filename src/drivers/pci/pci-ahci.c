/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * AHCI (SATA) host controller driver.
 *
 * Each SATA disk behind an AHCI HBA is published to the disk layer as sdN.
 * Commands are READ/WRITE DMA EXT, FLUSH CACHE EXT and IDENTIFY DEVICE,
 * issued one at a time per port through a bounce buffer, the way NVMe stages
 * its I/O.  NCQ, TRIM, link power management, hotplug, suspend and ATAPI are
 * not handled yet.
 */

#include <drivers/pci/pci-ahci.h>
#include <drivers/pci/pci-ahci-protocol.h>
#include <drivers/pci/pci.h>
#include <kern/kcrt.h>

#include <kern/clock.h>
#include <kern/disk.h>
#include <kern/lock.h>
#include <kern/sched.h>
#include <kern/thread.h>
#include <kern/waitq.h>
#include <uapi/sysctl.h>

#include <limits.h>
#include <stddef.h>
#include <uapi/errno.h>
#include "kern/klog.h"
#include "kern/kmem.h"
#include "kern/device-io.h"

/* The staging buffer one command moves through, and so the largest command. */
#define AHCI_BOUNCE_SIZE		(64U * 1024U)
#define AHCI_BOUNCE_ALIGN		4096U

/* The slot every command is issued in while commands run one at a time. */
#define AHCI_COMMAND_SLOT		0U

/* Bounds of the waits in the port bring-up and recovery, in milliseconds. */
#define AHCI_ENGINE_STOP_MS		500U
#define AHCI_COMRESET_HOLD_MS		2U
#define AHCI_LINK_UP_MS			1000U
#define AHCI_LINK_ABSENT_MS		100U
#define AHCI_DEVICE_READY_MS		10000U
#define AHCI_HANDOFF_MS			25U
#define AHCI_HANDOFF_BUSY_MS		2000U

/*
 * How long one command may run.  A spinning disk retrying a hard sector can
 * take tens of seconds before it answers.
 */
#define AHCI_COMMAND_TIMEOUT_MS		30000U
#define AHCI_IDENTIFY_TIMEOUT_MS	10000U

/*
 * The longest a waiter sleeps before it looks at the port itself, so a lost
 * or undelivered interrupt slows a command down instead of stalling it.
 */
#define AHCI_POLL_MS			10U

/* How many more times a request runs after a timeout or a link fault. */
#define AHCI_IO_RETRIES			2U

/* The longest model string IDENTIFY carries (words 27 to 46), plus NUL. */
#define AHCI_MODEL_TEXT_SIZE		41U

/*
 * One port of an HBA with a SATA disk behind it.
 *
 * A port is created by the probe once its link is up and its device answers
 * as an ATA disk; it lives as long as the kernel, because detach is refused.
 * The command mutex lets one command own the port at a time; the state lock
 * protects what the interrupt handler and the waiter share.
 */
struct ahci_port {
	struct ahci_controller *controller;
	unsigned number;
	volatile uint8_t *registers;

	struct drv_dma_buffer command_list_dma;
	struct drv_dma_buffer received_fis_dma;
	struct drv_dma_buffer tables_dma;
	struct drv_dma_buffer bounce_dma;
	unsigned command_list_allocated;
	unsigned received_fis_allocated;
	unsigned tables_allocated;
	unsigned bounce_allocated;

	struct mutex command_mutex;
	struct spinlock state_lock;
	struct wait_queue waitq;

	/*
	 * The slot mask of the command the HBA owns, or zero.  It is set by the
	 * issuer under the state lock and cleared by recovery.
	 */
	uint32_t issued;

	/*
	 * Every PxIS bit seen since the current command was issued.  The
	 * interrupt handler and the waiter both fold the hardware status into
	 * it under the state lock, so neither loses what the other cleared.
	 */
	uint32_t interrupt_status;

	/*
	 * Set when recovery could not bring the port back.  From then on every
	 * request fails at once instead of waiting for a dead link.
	 */
	unsigned dead;

	struct drv_ahci_identity identity;
	struct disk *disk;
};

/*
 * One AHCI HBA bound by the PCI core.
 *
 * It is created by attach and never freed, since detach is refused while its
 * disks may be in use.  The probe fills ports[] once, before it enables each
 * port's interrupts, so the interrupt handler reads a slot that is either
 * empty or final.
 */
struct ahci_controller {
	struct ahci_controller *next;
	unsigned index;
	struct drv_pci_device *pci;
	struct drv_pci_mapping mapping;
	struct drv_pci_enable_state enable_state;
	volatile uint8_t *registers;
	uint32_t capability;
	uint32_t capability2;
	uint32_t ports_implemented;
	uint32_t version;
	unsigned slot_count;

	struct drv_pci_irq irq;
	void *irq_cookie;

	unsigned bar_claimed;
	unsigned enable_state_saved;
	unsigned bar_mapped;
	unsigned irq_allocated;

	/* Set under the registry lock by the probe that takes this HBA. */
	unsigned probed;

	struct ahci_port *ports[DRV_AHCI_PORT_MAX];
};

/*
 * Every attached HBA, newest first.
 *
 * Attach adds to the list and the probe walks it; nothing is ever removed,
 * so a pointer read under the registry lock stays valid without it.
 */
static struct ahci_controller *ahci_controllers;

/*
 * The number the next attached HBA is known by in the log.
 *
 * It only increases, under the registry lock.
 */
static unsigned ahci_next_index;

/* Protects ahci_controllers, ahci_next_index and each HBA's probed flag. */
static struct spinlock ahci_registry_lock;

/*
 * Forward declarations.
 */
static int ahci_attach(struct drv_pci_device *device, const struct drv_pci_id *id);
static int ahci_detach(struct drv_pci_device *device, unsigned flags);
static void ahci_shutdown(struct drv_pci_device *device);
static int ahci_disk_submit(struct disk *disk, struct bio *bio);
static int ahci_disk_ioctl(struct disk *disk, unsigned long request, void *argument);

/* Device operation and registration tables. */
static const struct disk_ops ahci_disk_ops = {
	.submit = ahci_disk_submit,
	.ioctl = ahci_disk_ioctl,
};

static const struct drv_pci_id ahci_ids[] = {
	{
		.vendor = DRV_PCI_ANY_ID,
		.device = DRV_PCI_ANY_ID,
		.subvendor = DRV_PCI_ANY_ID,
		.subdevice = DRV_PCI_ANY_ID,
		.class_code = DRV_AHCI_PCI_CLASS,
		.class_mask = 0xffffffU,
	},
};

static struct drv_pci_driver ahci_driver = {
	.name = "ahci",
	.ids = ahci_ids,
	.id_count = sizeof(ahci_ids) / sizeof(ahci_ids[0]),
	.attach = ahci_attach,
	.detach = ahci_detach,
	.shutdown = ahci_shutdown,
};

static uint32_t ahci_read(const struct ahci_controller *controller, size_t offset);
static void ahci_write(struct ahci_controller *controller, size_t offset, uint32_t value);
static uint32_t ahci_port_read(const struct ahci_port *port, size_t offset);
static void ahci_port_write(struct ahci_port *port, size_t offset, uint32_t value);
static void ahci_delay_ms(unsigned milliseconds);
static int ahci_port_wait_clear(struct ahci_port *port, size_t offset, uint32_t mask, unsigned milliseconds);
static void ahci_attach_undo(struct ahci_controller *controller);
static int ahci_irq(void *argument);
static unsigned ahci_port_service_locked(struct ahci_port *port);
static void ahci_probe_controller(struct ahci_controller *controller);
static void ahci_bios_handoff(struct ahci_controller *controller);
static void ahci_port_probe(struct ahci_controller *controller, unsigned number);
static int ahci_port_memory_allocate(struct ahci_port *port);
static void ahci_port_memory_free(struct ahci_port *port);
static int ahci_port_engine_stop(struct ahci_port *port);
static int ahci_port_receive_stop(struct ahci_port *port);
static void ahci_port_release(struct ahci_port *port);
static int ahci_port_link_reset(struct ahci_port *port);
static int ahci_port_device_ready(struct ahci_port *port);
static void ahci_port_engine_start(struct ahci_port *port);
static int ahci_port_identify(struct ahci_port *port);
static int ahci_port_publish(struct ahci_port *port);
static int ahci_port_execute(struct ahci_port *port, uint8_t command, uint64_t lba, uint32_t count, int write, size_t length, unsigned timeout_ms, int *retryable);
static unsigned ahci_port_wait_result(struct ahci_port *port, uint32_t slot_mask, unsigned timeout_ms);
static int ahci_port_recover(struct ahci_port *port);
static int ahci_port_transfer(struct ahci_port *port, struct bio *bio, size_t *transferred);
static int ahci_port_flush(struct ahci_port *port);

/*
 * Registers the AHCI class driver with the PCI bus.
 */
int
drv_pci_ahci_driver_register(
	void)
{
	int error;

	/* Prepares the registry the probe later walks. */
	spin_init(&ahci_registry_lock, LOCK_RANK_DEVICE, "AHCI HBA registry");

	/* Asks the PCI core to bind every AHCI-class function to this driver. */
	error = drv_pci_driver_register(&ahci_driver);
	if (error != 0)
		return error;

	/* Succeeded: matching HBAs attach as the bus is enumerated. */
	return 0;
}

/*
 * Brings up the ports of every attached HBA and publishes their disks.
 */
void
drv_pci_ahci_probe_ports(
	void)
{
	struct ahci_controller *controller;
	unsigned long irq;

	/* Takes each HBA no probe has taken yet, one at a time. */
	for (;;) {
		irq = spin_lock_irqsave(&ahci_registry_lock);

		controller = ahci_controllers;
		while (controller != NULL && controller->probed)
			controller = controller->next;
		if (controller != NULL)
			controller->probed = 1;

		spin_unlock_irqrestore(&ahci_registry_lock, irq);

		/* Stops once every HBA has been probed. */
		if (controller == NULL)
			break;

		/* Brings up this HBA's ports outside the registry lock. */
		ahci_probe_controller(controller);
	}
}

/* Binds one AHCI function: maps its registers and installs its interrupt. */
static int
ahci_attach(
	struct drv_pci_device *device,
	const struct drv_pci_id *id)
{
	struct ahci_controller *controller;
	struct drv_pci_address address;
	struct drv_pci_bar bar;
	const char *stage;
	unsigned irq_count;
	unsigned long irq;
	uint32_t control;
	int error;

	(void)id;

	/* Allocates the controller record. */
	controller = kern_malloc(sizeof(*controller));
	if (controller == NULL)
		return ENOMEM;

	kern_memset(controller, 0, sizeof(*controller));
	controller->pci = device;
	drv_pci_device_address(device, &address);

	/* Takes ownership of the ABAR. */
	stage = "ABAR claim";
	error = drv_pci_device_claim_bar(device, DRV_AHCI_ABAR_INDEX);
	if (error != 0)
		goto fail;
	controller->bar_claimed = 1;

	/* The ABAR must be a memory BAR. */
	stage = "ABAR inspection";
	error = drv_pci_device_bar(device, DRV_AHCI_ABAR_INDEX, &bar);
	if (error != 0)
		goto fail;
	if (bar.type != DRV_PCI_BAR_MEMORY32 && bar.type != DRV_PCI_BAR_MEMORY64) {
		error = ENODEV;
		goto fail;
	}

	/* Remembers the PCI enable bits so a failed attach can put them back. */
	stage = "PCI command save";
	error = drv_pci_device_save_enable_state(device, &controller->enable_state);
	if (error != 0)
		goto fail;
	controller->enable_state_saved = 1;

	/* Maps the ABAR uncached for register access. */
	stage = "ABAR map";
	error = drv_pci_device_map_bar(
		device,
		DRV_AHCI_ABAR_INDEX,
		DRV_PCI_MAP_READ | DRV_PCI_MAP_WRITE | DRV_PCI_MAP_NOCACHE,
		&controller->mapping);
	if (error != 0)
		goto fail;
	controller->bar_mapped = 1;

	/* The mapping must reach at least the first port's registers. */
	if (controller->mapping.address == NULL ||
	    controller->mapping.size < drv_ahci_port_offset(1U)) {
		error = ENODEV;
		goto fail;
	}

	/* Lets the function decode its memory BAR. */
	stage = "PCI memory enable";
	error = drv_pci_device_enable_memory(device);
	if (error != 0)
		goto fail;

	/* Reads what the HBA says it can do. */
	controller->registers = controller->mapping.address;
	controller->capability = ahci_read(controller, DRV_AHCI_REG_CAP);
	controller->capability2 = ahci_read(controller, DRV_AHCI_REG_CAP2);
	controller->ports_implemented = ahci_read(controller, DRV_AHCI_REG_PI);
	controller->version = ahci_read(controller, DRV_AHCI_REG_VS);
	controller->slot_count = drv_ahci_slot_count(controller->capability);

	/*
	 * Puts the HBA in AHCI mode with its interrupt off.  The interrupt is
	 * turned on by the probe, after the BIOS has handed the HBA over.
	 */
	control = ahci_read(controller, DRV_AHCI_REG_GHC);
	control |= DRV_AHCI_GHC_AE;
	control &= ~DRV_AHCI_GHC_IE;
	ahci_write(controller, DRV_AHCI_REG_GHC, control);

	/* Takes one MSI vector, or the shared line when MSI is absent. */
	stage = "IRQ allocation";
	irq_count = 0;
	error = drv_pci_device_allocate_irqs(
		device,
		DRV_PCI_IRQ_ALLOW_MSI | DRV_PCI_IRQ_ALLOW_INTX,
		1,
		1,
		&controller->irq,
		&irq_count);
	if (error != 0)
		goto fail;
	if (irq_count != 1U) {
		error = EIO;
		goto fail;
	}
	controller->irq_allocated = 1;

	/* Installs the handler that completes commands. */
	stage = "IRQ establishment";
	error = drv_pci_device_establish_irq(
		device,
		&controller->irq,
		ahci_irq,
		controller,
		"ahci",
		&controller->irq_cookie);
	if (error != 0)
		goto fail;

	/* Lets shutdown find this controller from its PCI function. */
	stage = "driver data";
	error = drv_pci_device_set_driver_data(device, controller);
	if (error != 0)
		goto fail;

	/* Publishes the HBA for the probe. */
	irq = spin_lock_irqsave(&ahci_registry_lock);

	controller->index = ahci_next_index;
	ahci_next_index++;
	controller->next = ahci_controllers;
	ahci_controllers = controller;

	spin_unlock_irqrestore(&ahci_registry_lock, irq);

	kern_logf("ahci%u: PCI %04x:%02x:%02x.%u version=%08x ports=%08x "
		  "slots=%u %s%s\n",
		  controller->index,
		  address.segment,
		  address.bus,
		  address.device,
		  address.function,
		  controller->version,
		  controller->ports_implemented,
		  controller->slot_count,
		  controller->irq.type == DRV_PCI_IRQ_MSI ? "MSI" : "INTx",
		  (controller->capability & DRV_AHCI_CAP_S64A) != 0U ? " 64-bit" : "");

	/* Succeeded: the HBA is bound and waits for the probe. */
	return 0;

fail:
	kern_logf("ahci: attach failed at %s (%d)\n", stage, error);
	ahci_attach_undo(controller);
	kern_free(controller);

	/* Reports why the HBA could not be bound. */
	return error;
}

/* Refuses to unbind an HBA whose disks may be in use. */
static int
ahci_detach(
	struct drv_pci_device *device,
	unsigned flags)
{
	(void)device;
	(void)flags;

	/* Hot removal is not handled yet, so the binding stays. */
	return EBUSY;
}

/* Stops the HBA's DMA before the machine resets or powers off. */
static void
ahci_shutdown(
	struct drv_pci_device *device)
{
	struct ahci_controller *controller;
	struct ahci_port *port;
	uint32_t control;
	uint32_t command;
	unsigned number;

	/* Finds the controller attach recorded on the function. */
	controller = drv_pci_device_driver_data(device);
	if (controller == NULL)
		return;

	/* Turns the HBA's interrupt off. */
	control = ahci_read(controller, DRV_AHCI_REG_GHC);
	control &= ~DRV_AHCI_GHC_IE;
	ahci_write(controller, DRV_AHCI_REG_GHC, control);

	/*
	 * Stops each running port's command engine and FIS reception.  The
	 * caches were flushed by the sync before shutdown; this only keeps the
	 * HBA from writing memory the next kernel or firmware owns.
	 */
	for (number = 0; number < DRV_AHCI_PORT_MAX; number++) {
		port = controller->ports[number];
		if (port == NULL)
			continue;

		ahci_port_write(port, DRV_AHCI_PX_IE, 0U);
		command = ahci_port_read(port, DRV_AHCI_PX_CMD);
		command &= ~(DRV_AHCI_PX_CMD_ST | DRV_AHCI_PX_CMD_FRE);
		ahci_port_write(port, DRV_AHCI_PX_CMD, command);
	}
}

/* Serves one block request against an AHCI disk. */
static int
ahci_disk_submit(
	struct disk *disk,
	struct bio *bio)
{
	struct ahci_port *port;
	size_t transferred;
	uint64_t last;
	int error;

	/* Refuses a request without a disk this driver published. */
	if (disk == NULL || bio == NULL)
		return EINVAL;
	port = disk->d_data;
	if (port == NULL)
		return EINVAL;

	/* Refuses an operation the driver does not implement. */
	if (bio->b_op != BIO_READ &&
	    bio->b_op != BIO_WRITE &&
	    bio->b_op != BIO_FLUSH)
		return EOPNOTSUPP;

	/* A read or a write must name blocks that are all on the disk. */
	if (bio->b_op != BIO_FLUSH) {
		if (bio->b_block_count == 0U)
			return EINVAL;
		if (bio->b_mapped_block >= port->identity.sector_count)
			return EOVERFLOW;
		last = bio->b_mapped_block + bio->b_block_count;
		if (last > port->identity.sector_count)
			return EOVERFLOW;
		if (last < bio->b_mapped_block)
			return EOVERFLOW;
	}

	/* Runs the request with the port to itself. */
	transferred = 0;
	mutex_lock(&port->command_mutex);

	if (bio->b_op == BIO_FLUSH) {
		error = ahci_port_flush(port);
	} else {
		error = ahci_port_transfer(port, bio, &transferred);
	}

	mutex_unlock(&port->command_mutex);

	/* Completes the request with what was moved before any failure. */
	bio_complete(bio, error, transferred);

	/* Succeeded: the request is completed. */
	return 0;
}

/* Reports a disk's default geometry for partition tools. */
static int
ahci_disk_ioctl(
	struct disk *disk,
	unsigned long request,
	void *argument)
{
	struct disk_geometry *geometry;
	struct ahci_port *port;
	uint64_t cylinders;

	/* Only the geometry request is answered. */
	if (request != DISK_IOCTL_GET_GEOMETRY)
		return EOPNOTSUPP;

	/* Refuses a missing disk or a missing answer buffer. */
	if (disk == NULL)
		return EINVAL;
	port = disk->d_data;
	geometry = argument;
	if (port == NULL || geometry == NULL)
		return EINVAL;

	/* Uses the disk's own geometry when IDENTIFY still reports one. */
	if (port->identity.heads != 0U && port->identity.sectors_per_track != 0U) {
		geometry->cylinders = port->identity.cylinders;
		geometry->heads = port->identity.heads;
		geometry->sectors_per_track = port->identity.sectors_per_track;

		/* Succeeded: the disk's reported geometry. */
		return 0;
	}

	/* Otherwise uses the conventional 255 heads of 63 sectors. */
	cylinders = port->identity.sector_count / (255U * 63U);
	if (cylinders > UINT32_MAX)
		cylinders = UINT32_MAX;
	geometry->cylinders = (uint32_t)cylinders;
	geometry->heads = 255U;
	geometry->sectors_per_track = 63U;

	/* Succeeded: the synthesized geometry. */
	return 0;
}

/* Reads one HBA register. */
static uint32_t
ahci_read(
	const struct ahci_controller *controller,
	size_t offset)
{
	uint32_t value;

	/* Reads the register through the uncached mapping. */
	value = kern_mmio_read32(controller->registers + offset);

	/* Reports the register's value. */
	return value;
}

/* Writes one HBA register. */
static void
ahci_write(
	struct ahci_controller *controller,
	size_t offset,
	uint32_t value)
{
	/* Writes the register through the uncached mapping. */
	kern_mmio_write32(controller->registers + offset, value);
}

/* Reads one port register. */
static uint32_t
ahci_port_read(
	const struct ahci_port *port,
	size_t offset)
{
	uint32_t value;

	/* Reads the register inside the port's block. */
	value = kern_mmio_read32(port->registers + offset);

	/* Reports the register's value. */
	return value;
}

/* Writes one port register. */
static void
ahci_port_write(
	struct ahci_port *port,
	size_t offset,
	uint32_t value)
{
	/* Writes the register inside the port's block. */
	kern_mmio_write32(port->registers + offset, value);
}

/* Lets at least the given time pass, yielding the CPU meanwhile. */
static void
ahci_delay_ms(
	unsigned milliseconds)
{
	uint64_t deadline;
	uint64_t now;

	/* One extra tick covers the part of the current tick already gone. */
	deadline = sched_ticks() + kern_ms_to_ticks(milliseconds) + 1U;

	/* Yields until the deadline passes. */
	for (;;) {
		now = sched_ticks();
		if (now >= deadline)
			break;
		sched_yield();
	}
}

/* Waits for the given bits of a port register to read as zero. */
static int
ahci_port_wait_clear(
	struct ahci_port *port,
	size_t offset,
	uint32_t mask,
	unsigned milliseconds)
{
	uint64_t deadline;
	uint64_t now;
	uint32_t value;

	/* Polls the register, yielding between reads, until the deadline. */
	deadline = sched_ticks() + kern_ms_to_ticks(milliseconds) + 1U;
	for (;;) {
		/* Stops as soon as the bits read as zero. */
		value = ahci_port_read(port, offset);
		if ((value & mask) == 0U)
			return 0;

		/* Gives up once the wait has run its course. */
		now = sched_ticks();
		if (now >= deadline)
			break;

		sched_yield();
	}

	/* The bits stayed set for the whole wait. */
	return ETIMEDOUT;
}

/* Gives back what a failed attach had taken, newest first. */
static void
ahci_attach_undo(
	struct ahci_controller *controller)
{
	/* Removes the handler and gives back the vector. */
	if (controller->irq_cookie != NULL)
		drv_pci_device_disestablish_irq(controller->pci, controller->irq_cookie);
	if (controller->irq_allocated)
		drv_pci_device_free_irqs(controller->pci, &controller->irq, 1);

	/* Unmaps the registers. */
	if (controller->bar_mapped)
		drv_pci_device_unmap_bar(controller->pci, &controller->mapping);

	/* Puts the PCI enable bits back as attach found them. */
	if (controller->enable_state_saved) {
		(void)drv_pci_device_restore_enable_state(
			controller->pci,
			&controller->enable_state);
	}

	/* Releases the ABAR. */
	if (controller->bar_claimed)
		drv_pci_device_release_bar(controller->pci, DRV_AHCI_ABAR_INDEX);
}

/* Takes an HBA interrupt: collects each port's status and wakes its waiter. */
static int
ahci_irq(
	void *argument)
{
	struct ahci_controller *controller;
	struct ahci_port *port;
	unsigned long irq;
	uint32_t pending;
	unsigned number;

	controller = argument;

	/* Reads which ports raised the interrupt. */
	pending = ahci_read(controller, DRV_AHCI_REG_IS);
	if (pending == 0U || pending == UINT32_MAX)
		return 0;

	/* Clears and records each raising port's status. */
	for (number = 0; number < DRV_AHCI_PORT_MAX; number++) {
		if ((pending & (UINT32_C(1) << number)) == 0U)
			continue;
		port = controller->ports[number];
		if (port == NULL)
			continue;

		irq = spin_lock_irqsave(&port->state_lock);

		(void)ahci_port_service_locked(port);

		spin_unlock_irqrestore(&port->state_lock, irq);
	}

	/* Acknowledges the ports only after their own status was cleared. */
	ahci_write(controller, DRV_AHCI_REG_IS, pending);

	/* Succeeded: the interrupt was this HBA's. */
	return 1;
}

/*
 * Folds a port's interrupt status into its record and wakes the waiter.
 *
 * Returns nonzero when the port had something to report.
 */
static unsigned
ahci_port_service_locked(
	struct ahci_port *port)
{
	uint32_t status;
	uint32_t errors;

	/* Reads what the port raised since the last look. */
	status = ahci_port_read(port, DRV_AHCI_PX_IS);
	if (status == 0U || status == UINT32_MAX)
		return 0;

	/*
	 * A PhyRdy or connect change is held by PxSERR, not PxIS; clearing the
	 * SError bits is what lets those two drop.
	 */
	if ((status & (DRV_AHCI_PX_IS_PRCS | DRV_AHCI_PX_IS_PCS)) != 0U) {
		errors = ahci_port_read(port, DRV_AHCI_PX_SERR);
		ahci_port_write(port, DRV_AHCI_PX_SERR, errors);
	}

	/*
	 * Clears exactly the bits read, so a later event is not lost, and then
	 * the port's summary bit, which a waiter that got here before the
	 * interrupt handler would otherwise leave set.
	 */
	ahci_port_write(port, DRV_AHCI_PX_IS, status);
	ahci_write(port->controller, DRV_AHCI_REG_IS, UINT32_C(1) << port->number);

	/* Keeps the bits for the command's waiter and wakes it. */
	port->interrupt_status |= status;
	waitq_wake_all(&port->waitq);

	/* Succeeded: the port had status to report. */
	return 1;
}

/* Takes an HBA over from the firmware and brings up each implemented port. */
static void
ahci_probe_controller(
	struct ahci_controller *controller)
{
	uint32_t control;
	uint32_t pending;
	size_t needed;
	unsigned number;
	int error;

	/* Lets the HBA fetch commands and post FISes. */
	error = drv_pci_device_set_bus_master(controller->pci, true);
	if (error != 0) {
		kern_logf("ahci%u: bus mastering refused (%d)\n",
			  controller->index,
			  error);
		return;
	}

	/* Asks the firmware to let go of the HBA where it supports handoff. */
	if ((controller->capability2 & DRV_AHCI_CAP2_BOH) != 0U)
		ahci_bios_handoff(controller);

	/* Clears stale port interrupts and turns the HBA's interrupt on. */
	pending = ahci_read(controller, DRV_AHCI_REG_IS);
	ahci_write(controller, DRV_AHCI_REG_IS, pending);
	control = ahci_read(controller, DRV_AHCI_REG_GHC);
	control |= DRV_AHCI_GHC_AE | DRV_AHCI_GHC_IE;
	ahci_write(controller, DRV_AHCI_REG_GHC, control);

	/* Brings up each implemented port whose registers the mapping covers. */
	for (number = 0; number < DRV_AHCI_PORT_MAX; number++) {
		if ((controller->ports_implemented & (UINT32_C(1) << number)) == 0U)
			continue;

		needed = drv_ahci_port_offset(number) + DRV_AHCI_PORT_STRIDE;
		if (needed > controller->mapping.size)
			continue;

		ahci_port_probe(controller, number);
	}
}

/* Requests HBA ownership from the firmware (AHCI 1.3 section 10.6.3). */
static void
ahci_bios_handoff(
	struct ahci_controller *controller)
{
	uint64_t deadline;
	uint64_t now;
	uint32_t handoff;

	/* Sets OS ownership; the firmware answers by clearing its own bit. */
	handoff = ahci_read(controller, DRV_AHCI_REG_BOHC);
	handoff |= DRV_AHCI_BOHC_OOS;
	ahci_write(controller, DRV_AHCI_REG_BOHC, handoff);

	/* Waits 25 ms for the firmware to release the HBA. */
	ahci_delay_ms(AHCI_HANDOFF_MS);
	handoff = ahci_read(controller, DRV_AHCI_REG_BOHC);

	/* A firmware that says it is busy gets up to two seconds more. */
	if ((handoff & DRV_AHCI_BOHC_BB) != 0U) {
		deadline = sched_ticks() + kern_ms_to_ticks(AHCI_HANDOFF_BUSY_MS);
		while ((handoff & DRV_AHCI_BOHC_BOS) != 0U) {
			/* Stops waiting at the deadline. */
			now = sched_ticks();
			if (now >= deadline)
				break;

			sched_yield();
			handoff = ahci_read(controller, DRV_AHCI_REG_BOHC);
		}
	}

	/* Goes on regardless; a firmware that never lets go is only logged. */
	if ((handoff & DRV_AHCI_BOHC_BOS) != 0U) {
		kern_logf("ahci%u: firmware kept HBA ownership (%08x)\n",
			  controller->index,
			  handoff);
	}
}

/*
 * Brings up one port: resets its link, checks for an ATA disk, starts the
 * command engine, identifies the disk and publishes it.
 */
static void
ahci_port_probe(
	struct ahci_controller *controller,
	unsigned number)
{
	struct ahci_port *port;
	uint32_t signature;
	uint32_t command;
	int error;

	/* Allocates the port record. */
	port = kern_malloc(sizeof(*port));
	if (port == NULL) {
		kern_logf("ahci%u: port %u: out of memory\n",
			  controller->index,
			  number);
		return;
	}

	/* Initializes the port record and its locks. */
	kern_memset(port, 0, sizeof(*port));
	port->controller = controller;
	port->number = number;
	port->registers = controller->registers + drv_ahci_port_offset(number);
	(void)mutex_init(&port->command_mutex, LOCK_RANK_DISK, "AHCI port command");
	spin_init(&port->state_lock, LOCK_RANK_DISK, "AHCI port state");
	waitq_init(&port->waitq, "AHCI port command");

	/* Takes the port away from whatever the firmware left running. */
	ahci_port_write(port, DRV_AHCI_PX_IE, 0U);
	error = ahci_port_engine_stop(port);
	if (error == 0)
		error = ahci_port_receive_stop(port);
	if (error != 0) {
		kern_logf("ahci%u: port %u: engine does not stop\n",
			  controller->index,
			  number);
		kern_free(port);
		return;
	}

	/* Gives the port its command list, FIS area, tables and bounce buffer. */
	error = ahci_port_memory_allocate(port);
	if (error != 0) {
		kern_logf("ahci%u: port %u: DMA memory unavailable (%d)\n",
			  controller->index,
			  number,
			  error);
		ahci_port_memory_free(port);
		kern_free(port);
		return;
	}

	/* Spins the device up and turns FIS reception on before the reset. */
	command = ahci_port_read(port, DRV_AHCI_PX_CMD);
	command |= DRV_AHCI_PX_CMD_SUD;
	if ((controller->capability & DRV_AHCI_CAP_CPD) != 0U)
		command |= DRV_AHCI_PX_CMD_POD;
	command |= DRV_AHCI_PX_CMD_FRE;
	ahci_port_write(port, DRV_AHCI_PX_CMD, command);

	/* Resets the link; an empty port ends here. */
	error = ahci_port_link_reset(port);
	if (error != 0) {
		ahci_port_release(port);
		return;
	}

	/* Waits for the device to finish its power-on reset. */
	error = ahci_port_device_ready(port);
	if (error != 0) {
		kern_logf("ahci%u: port %u: device stays busy (tfd=%08x)\n",
			  controller->index,
			  number,
			  ahci_port_read(port, DRV_AHCI_PX_TFD));
		ahci_port_release(port);
		return;
	}

	/* Only an ATA disk is used; ATAPI and port multipliers come later. */
	signature = ahci_port_read(port, DRV_AHCI_PX_SIG);
	if (signature != DRV_AHCI_SIG_ATA) {
		kern_logf("ahci%u: port %u: signature %08x not supported\n",
			  controller->index,
			  number,
			  signature);
		ahci_port_release(port);
		return;
	}

	/*
	 * Publishes the port to the interrupt handler, then lets the port
	 * interrupt and starts its command engine.
	 */
	ahci_port_write(port, DRV_AHCI_PX_SERR, UINT32_MAX);
	ahci_port_write(port, DRV_AHCI_PX_IS, UINT32_MAX);
	controller->ports[number] = port;
	kern_io_write_barrier();
	ahci_port_write(port, DRV_AHCI_PX_IE, DRV_AHCI_PX_IE_DEFAULT);
	ahci_port_engine_start(port);

	/* Asks the disk what it is. */
	error = ahci_port_identify(port);
	if (error == 0)
		error = ahci_port_publish(port);
	if (error == 0)
		return;

	/*
	 * Takes the port back out of service.  The handler slot stays filled:
	 * the port record is kept, since an interrupt may still be in flight.
	 */
	ahci_port_write(port, DRV_AHCI_PX_IE, 0U);
	(void)ahci_port_engine_stop(port);
	port->dead = 1;
}

/* Allocates the memory the HBA reads commands from and writes FISes to. */
static int
ahci_port_memory_allocate(
	struct ahci_port *port)
{
	struct ahci_controller *controller;
	struct drv_dma_device *dma;
	size_t tables_size;
	uint64_t highest;
	int error;

	controller = port->controller;
	dma = drv_pci_device_dma(controller->pci);
	tables_size = (size_t)controller->slot_count * DRV_AHCI_TABLE_SIZE;

	/* The command list: one 32-byte header per slot, 1 KiB aligned. */
	error = drv_dma_alloc_coherent(
		dma,
		DRV_AHCI_COMMAND_LIST_SIZE,
		DRV_AHCI_COMMAND_LIST_ALIGN,
		&port->command_list_dma);
	if (error != 0)
		return error;
	port->command_list_allocated = 1;

	/* The received FIS area, 256 bytes aligned. */
	error = drv_dma_alloc_coherent(
		dma,
		DRV_AHCI_RECEIVED_FIS_SIZE,
		DRV_AHCI_RECEIVED_FIS_ALIGN,
		&port->received_fis_dma);
	if (error != 0)
		return error;
	port->received_fis_allocated = 1;

	/* One command table per slot, each on a 128-byte boundary. */
	error = drv_dma_alloc_coherent(
		dma,
		tables_size,
		DRV_AHCI_TABLE_ALIGN,
		&port->tables_dma);
	if (error != 0)
		return error;
	port->tables_allocated = 1;

	/* The bounce buffer every command's data moves through. */
	error = drv_dma_alloc_coherent(
		dma,
		AHCI_BOUNCE_SIZE,
		AHCI_BOUNCE_ALIGN,
		&port->bounce_dma);
	if (error != 0)
		return error;
	port->bounce_allocated = 1;

	/* Clears what the HBA will read before it can read it. */
	kern_memset(port->command_list_dma.address, 0, DRV_AHCI_COMMAND_LIST_SIZE);
	kern_memset(port->received_fis_dma.address, 0, DRV_AHCI_RECEIVED_FIS_SIZE);
	kern_memset(port->tables_dma.address, 0, tables_size);

	/* An HBA without 64-bit addressing can only reach the low 4 GiB. */
	highest = port->command_list_dma.device_address;
	if (port->received_fis_dma.device_address > highest)
		highest = port->received_fis_dma.device_address;
	if (port->tables_dma.device_address + tables_size > highest)
		highest = port->tables_dma.device_address + tables_size;
	if (port->bounce_dma.device_address + AHCI_BOUNCE_SIZE > highest)
		highest = port->bounce_dma.device_address + AHCI_BOUNCE_SIZE;
	if ((controller->capability & DRV_AHCI_CAP_S64A) == 0U &&
	    highest > UINT32_MAX)
		return ERANGE;

	/* Points the port at its command list and FIS area. */
	ahci_port_write(
		port,
		DRV_AHCI_PX_CLB,
		(uint32_t)(port->command_list_dma.device_address & 0xffffffffU));
	ahci_port_write(
		port,
		DRV_AHCI_PX_CLBU,
		(uint32_t)(port->command_list_dma.device_address >> 32));
	ahci_port_write(
		port,
		DRV_AHCI_PX_FB,
		(uint32_t)(port->received_fis_dma.device_address & 0xffffffffU));
	ahci_port_write(
		port,
		DRV_AHCI_PX_FBU,
		(uint32_t)(port->received_fis_dma.device_address >> 32));

	/* Succeeded: the port has all of its DMA memory. */
	return 0;
}

/* Frees whatever DMA memory a port holds; the HBA must no longer use it. */
static void
ahci_port_memory_free(
	struct ahci_port *port)
{
	struct drv_dma_device *dma;

	dma = drv_pci_device_dma(port->controller->pci);

	/* Frees each buffer that was allocated. */
	if (port->bounce_allocated)
		drv_dma_free_coherent(dma, &port->bounce_dma);
	if (port->tables_allocated)
		drv_dma_free_coherent(dma, &port->tables_dma);
	if (port->received_fis_allocated)
		drv_dma_free_coherent(dma, &port->received_fis_dma);
	if (port->command_list_allocated)
		drv_dma_free_coherent(dma, &port->command_list_dma);
	port->bounce_allocated = 0;
	port->tables_allocated = 0;
	port->received_fis_allocated = 0;
	port->command_list_allocated = 0;
}

/* Stops a port's command engine and waits until it has stopped. */
static int
ahci_port_engine_stop(
	struct ahci_port *port)
{
	uint32_t command;
	int error;

	/* Clears ST; the HBA answers by clearing CR (and PxCI). */
	command = ahci_port_read(port, DRV_AHCI_PX_CMD);
	command &= ~DRV_AHCI_PX_CMD_ST;
	ahci_port_write(port, DRV_AHCI_PX_CMD, command);

	/* Waits for the command list to go idle. */
	error = ahci_port_wait_clear(
		port,
		DRV_AHCI_PX_CMD,
		DRV_AHCI_PX_CMD_CR,
		AHCI_ENGINE_STOP_MS);
	if (error != 0)
		return error;

	/* Succeeded: the engine is stopped. */
	return 0;
}

/* Stops a port's FIS reception and waits until it has stopped. */
static int
ahci_port_receive_stop(
	struct ahci_port *port)
{
	uint32_t command;
	int error;

	/* Clears FRE; the HBA answers by clearing FR. */
	command = ahci_port_read(port, DRV_AHCI_PX_CMD);
	command &= ~DRV_AHCI_PX_CMD_FRE;
	ahci_port_write(port, DRV_AHCI_PX_CMD, command);

	/* Waits for FIS reception to go idle. */
	error = ahci_port_wait_clear(
		port,
		DRV_AHCI_PX_CMD,
		DRV_AHCI_PX_CMD_FR,
		AHCI_ENGINE_STOP_MS);
	if (error != 0)
		return error;

	/* Succeeded: no more FISes are written to memory. */
	return 0;
}

/* Gives back an unused port; its memory is freed only once the HBA let go. */
static void
ahci_port_release(
	struct ahci_port *port)
{
	int error;

	/* Stops the engine and FIS reception. */
	error = ahci_port_engine_stop(port);
	if (error == 0)
		error = ahci_port_receive_stop(port);

	/* A port that does not stop keeps its memory, which the HBA may write. */
	if (error != 0) {
		kern_logf("ahci%u: port %u: does not stop; memory kept\n",
			  port->controller->index,
			  port->number);
		return;
	}

	/* Frees the port's memory and record. */
	ahci_port_memory_free(port);
	kern_free(port);
}

/*
 * Resets a port's link with a COMRESET and waits for a device to answer.
 *
 * The engine must be stopped.  Returns ENODEV when no device is present.
 */
static int
ahci_port_link_reset(
	struct ahci_port *port)
{
	uint64_t started;
	uint64_t absent_after;
	uint64_t deadline;
	uint64_t now;
	uint32_t control;
	uint32_t detection;

	/*
	 * Sends COMRESET for at least a millisecond, keeping the link out of
	 * the Partial and Slumber power states.
	 */
	control = ahci_port_read(port, DRV_AHCI_PX_SCTL);
	control &= ~(DRV_AHCI_PX_SCTL_DET_MASK | DRV_AHCI_PX_SCTL_IPM_MASK);
	control |= DRV_AHCI_PX_SCTL_IPM_NONE | DRV_AHCI_PX_SCTL_DET_INIT;
	ahci_port_write(port, DRV_AHCI_PX_SCTL, control);
	ahci_delay_ms(AHCI_COMRESET_HOLD_MS);
	control &= ~DRV_AHCI_PX_SCTL_DET_MASK;
	ahci_port_write(port, DRV_AHCI_PX_SCTL, control);

	/*
	 * Waits for an established link.  A port that shows no device at all
	 * after 100 ms is taken to be empty, so empty ports do not each cost
	 * the full second.
	 */
	started = sched_ticks();
	absent_after = started + kern_ms_to_ticks(AHCI_LINK_ABSENT_MS) + 1U;
	deadline = started + kern_ms_to_ticks(AHCI_LINK_UP_MS) + 1U;
	for (;;) {
		/* Stops once the PHY reports an established link. */
		detection = ahci_port_read(port, DRV_AHCI_PX_SSTS) &
			    DRV_AHCI_PX_SSTS_DET_MASK;
		if (detection == DRV_AHCI_PX_SSTS_DET_PHY)
			break;

		/* An empty port shows nothing at all; a slow link gets the full wait. */
		now = sched_ticks();
		if (detection == DRV_AHCI_PX_SSTS_DET_NONE && now >= absent_after)
			return ENODEV;
		if (now >= deadline)
			return ENODEV;

		sched_yield();
	}

	/* Clears the errors the link bring-up recorded. */
	ahci_port_write(port, DRV_AHCI_PX_SERR, UINT32_MAX);

	/* Succeeded: a device answers on the link. */
	return 0;
}

/* Waits for the device to clear BSY and DRQ after a reset. */
static int
ahci_port_device_ready(
	struct ahci_port *port)
{
	int error;

	/* A disk spinning up can take seconds before it reports ready. */
	error = ahci_port_wait_clear(
		port,
		DRV_AHCI_PX_TFD,
		DRV_AHCI_PX_TFD_BSY | DRV_AHCI_PX_TFD_DRQ,
		AHCI_DEVICE_READY_MS);
	if (error != 0)
		return error;

	/* Succeeded: the device can take a command. */
	return 0;
}

/* Starts a port's command engine. */
static void
ahci_port_engine_start(
	struct ahci_port *port)
{
	uint32_t command;

	/* Sets ST; FRE is already on and the device is idle. */
	command = ahci_port_read(port, DRV_AHCI_PX_CMD);
	command |= DRV_AHCI_PX_CMD_ST;
	ahci_port_write(port, DRV_AHCI_PX_CMD, command);
}

/* Reads and decodes the disk's IDENTIFY DEVICE answer. */
static int
ahci_port_identify(
	struct ahci_port *port)
{
	uint16_t words[DRV_AHCI_IDENTIFY_SIZE / 2U];
	char model[AHCI_MODEL_TEXT_SIZE];
	uint32_t reasons;
	int retryable;
	int error;

	/* Issues IDENTIFY DEVICE; the answer lands in the bounce buffer. */
	mutex_lock(&port->command_mutex);

	error = ahci_port_execute(
		port,
		DRV_AHCI_ATA_IDENTIFY,
		0U,
		0U,
		0,
		DRV_AHCI_IDENTIFY_SIZE,
		AHCI_IDENTIFY_TIMEOUT_MS,
		&retryable);
	if (error == 0)
		kern_memcpy(words, port->bounce_dma.address, sizeof(words));

	mutex_unlock(&port->command_mutex);

	/* Reports a disk that did not answer. */
	if (error != 0) {
		kern_logf("ahci%u: port %u: IDENTIFY failed (%d)\n",
			  port->controller->index,
			  port->number,
			  error);
		return error;
	}

	/* Decodes the addressing, the sector size and the cache. */
	drv_ahci_identify_text(words, 27U, 20U, model, sizeof(model));
	reasons = drv_ahci_identify_parse(words, &port->identity);
	if (reasons != 0U) {
		kern_logf("ahci%u: port %u: %s not supported (%08x)\n",
			  port->controller->index,
			  port->number,
			  model,
			  reasons);
		return EOPNOTSUPP;
	}

	kern_logf("ahci%u: port %u: %s sectors=%08x:%08x sector-size=%u%s\n",
		  port->controller->index,
		  port->number,
		  model,
		  (uint32_t)(port->identity.sector_count >> 32),
		  (uint32_t)port->identity.sector_count,
		  port->identity.sector_size,
		  port->identity.write_cache ? " write-cache" : "");

	/* Succeeded: the disk is identified. */
	return 0;
}

/* Publishes an identified disk to the disk layer. */
static int
ahci_port_publish(
	struct ahci_port *port)
{
	struct disk *disk;
	int error;

	/* Allocates the disk. */
	disk = disk_alloc();
	if (disk == NULL)
		return ENOMEM;

	/* Names it like every other ATA and SCSI disk. */
	error = disk_alloc_sd_name(disk);
	if (error != 0) {
		(void)disk_destroy(disk);
		return error;
	}

	/* Describes the disk; one request moves at most one bounce buffer. */
	disk->d_flags = 0;
	disk->d_block_size = port->identity.sector_size;
	disk->d_block_count = port->identity.sector_count;
	disk->d_max_transfer_blocks = AHCI_BOUNCE_SIZE / port->identity.sector_size;
	disk->d_ops = &ahci_disk_ops;
	disk->d_data = port;

	/* hw.diskstats lists it as an ATA disk. */
	disk_set_stats_kind(disk, DISK_STATS_KIND_IDE);

	/* Makes the disk visible; the port is fully running by now. */
	error = disk_create(disk);
	if (error != 0) {
		(void)disk_destroy(disk);
		return error;
	}

	port->disk = disk;
	kern_logf("ahci%u: port %u: /dev/%s\n",
		  port->controller->index,
		  port->number,
		  disk->d_name);

	/* Succeeded: the disk is published. */
	return 0;
}

/*
 * Runs one ATA command on a port and waits for it.
 *
 * The caller holds the command mutex and has put write data in the bounce
 * buffer; read data is left there.  On failure the port is recovered, and
 * *retryable says whether running the same command again may succeed.
 */
static int
ahci_port_execute(
	struct ahci_port *port,
	uint8_t command,
	uint64_t lba,
	uint32_t count,
	int write,
	size_t length,
	unsigned timeout_ms,
	int *retryable)
{
	struct drv_ahci_command_header *header;
	struct drv_ahci_fis_h2d fis;
	struct drv_ahci_prd *entries;
	const char *outcome;
	uint8_t *table;
	uint64_t table_address;
	uint32_t slot_mask;
	unsigned long irq;
	unsigned prd_count;
	unsigned result;
	int built;
	int recovered;

	*retryable = 0;

	/* A port that could not be recovered takes no more commands. */
	if (port->dead)
		return ENXIO;

	/* Builds the command FIS. */
	built = drv_ahci_fis_h2d_build(&fis, command, lba, count);
	if (!built)
		return EINVAL;

	/* Locates the slot's command table and clears its FIS area. */
	table = (uint8_t *)port->tables_dma.address +
		(size_t)AHCI_COMMAND_SLOT * DRV_AHCI_TABLE_SIZE;
	table_address = port->tables_dma.device_address +
			(uint64_t)AHCI_COMMAND_SLOT * DRV_AHCI_TABLE_SIZE;
	kern_memset(table, 0, DRV_AHCI_TABLE_PRDT_OFFSET);
	kern_memcpy(table, &fis, sizeof(fis));

	/* Describes the bounce buffer as the command's data. */
	prd_count = 0;
	if (length != 0U) {
		if (length > AHCI_BOUNCE_SIZE)
			return EINVAL;
		entries = (struct drv_ahci_prd *)(table + DRV_AHCI_TABLE_PRDT_OFFSET);
		built = drv_ahci_prdt_build(
			entries,
			DRV_AHCI_TABLE_PRD_MAX,
			port->bounce_dma.device_address,
			length,
			&prd_count);
		if (!built)
			return EINVAL;
	}

	/* Points the slot's command header at the table. */
	header = (struct drv_ahci_command_header *)port->command_list_dma.address +
		 AHCI_COMMAND_SLOT;
	built = drv_ahci_command_header_build(
		header,
		DRV_AHCI_FIS_H2D_DWORDS,
		write,
		prd_count,
		table_address);
	if (!built)
		return EINVAL;

	/*
	 * Issues the slot.  The table and header writes must reach memory
	 * before the HBA sees the PxCI bit.
	 */
	slot_mask = UINT32_C(1) << AHCI_COMMAND_SLOT;
	irq = spin_lock_irqsave(&port->state_lock);

	port->interrupt_status = 0;
	port->issued = slot_mask;
	kern_io_write_barrier();
	ahci_port_write(port, DRV_AHCI_PX_CI, slot_mask);

	spin_unlock_irqrestore(&port->state_lock, irq);

	/* Waits for the HBA to give the slot back or to report a failure. */
	result = ahci_port_wait_result(port, slot_mask, timeout_ms);
	if (result == DRV_AHCI_RESULT_DONE) {
		kern_io_read_barrier();
		return 0;
	}

	/* Names how the command ended for the log. */
	if (result == DRV_AHCI_RESULT_DEVICE_ERROR) {
		outcome = "device error";
	} else if (result == DRV_AHCI_RESULT_LINK_ERROR) {
		outcome = "link error";
	} else {
		outcome = "timeout";
	}

	/* Logs what the port shows before recovery clears it. */
	kern_logf("ahci%u: port %u: command %02x lba=%08x:%08x count=%u %s "
		  "is=%08x tfd=%08x serr=%08x\n",
		  port->controller->index,
		  port->number,
		  command,
		  (uint32_t)(lba >> 32),
		  (uint32_t)lba,
		  count,
		  outcome,
		  port->interrupt_status,
		  ahci_port_read(port, DRV_AHCI_PX_TFD),
		  ahci_port_read(port, DRV_AHCI_PX_SERR));

	/* Brings the port back for the next command. */
	recovered = ahci_port_recover(port);
	if (recovered != 0) {
		kern_logf("ahci%u: port %u: recovery failed; port out of service\n",
			  port->controller->index,
			  port->number);
		port->dead = 1;
		return EIO;
	}

	/* A device error is the disk's answer; repeating it changes nothing. */
	if (result == DRV_AHCI_RESULT_DEVICE_ERROR)
		return EIO;

	/* A link fault or a timeout may pass once the link is reset. */
	*retryable = 1;
	if (result == DRV_AHCI_RESULT_LINK_ERROR)
		return EIO;

	/* The command did not finish in time. */
	return ETIMEDOUT;
}

/*
 * Waits until an issued slot finishes, fails or runs out of time.
 *
 * Returns a DRV_AHCI_RESULT_* value; DRV_AHCI_RESULT_RUNNING means the
 * timeout passed.
 */
static unsigned
ahci_port_wait_result(
	struct ahci_port *port,
	uint32_t slot_mask,
	unsigned timeout_ms)
{
	struct thread *thread;
	uint64_t deadline;
	uint64_t now;
	uint64_t wake;
	uint64_t sequence;
	unsigned long irq;
	uint32_t command_issue;
	uint32_t task_file;
	unsigned result;
	int idle;

	deadline = sched_ticks() + kern_ms_to_ticks(timeout_ms) + 1U;

	/*
	 * Early VFS discovery runs in CPU0's idle thread, which cannot sleep on
	 * a wait queue (see nvme_io_wait_locked).  That context yields instead.
	 */
	idle = 1;
	thread = thread_current();
	if (thread != NULL && (thread->flags & THREAD_FLAG_IDLE) == 0U)
		idle = 0;

	/* Looks at the port, then sleeps or yields, until the slot resolves. */
	irq = spin_lock_irqsave(&port->state_lock);
	for (;;) {
		/* Folds in status the interrupt has not delivered yet. */
		(void)ahci_port_service_locked(port);
		command_issue = ahci_port_read(port, DRV_AHCI_PX_CI);
		task_file = ahci_port_read(port, DRV_AHCI_PX_TFD);
		result = drv_ahci_command_result(
			port->interrupt_status,
			task_file,
			command_issue,
			slot_mask);
		if (result != DRV_AHCI_RESULT_RUNNING)
			break;

		/* Leaves the slot to recovery once the timeout has passed. */
		now = sched_ticks();
		if (now >= deadline)
			break;

		/* Yields in the idle thread; sleeps briefly anywhere else. */
		if (idle) {
			spin_unlock_irqrestore(&port->state_lock, irq);
			sched_yield();
			irq = spin_lock_irqsave(&port->state_lock);
		} else {
			wake = now + kern_ms_to_ticks(AHCI_POLL_MS) + 1U;
			if (wake > deadline)
				wake = deadline;
			sequence = waitq_sequence(&port->waitq);
			(void)waitq_sleep(&port->waitq, &port->state_lock, sequence, wake, 0);
		}
	}

	/* A finished slot is no longer owned by the HBA. */
	if (result == DRV_AHCI_RESULT_DONE)
		port->issued = 0;

	spin_unlock_irqrestore(&port->state_lock, irq);

	/* Reports how the command ended. */
	return result;
}

/*
 * Brings a port back after a failed or timed-out command.
 *
 * Stopping the engine drops the command; a device still busy, or an engine
 * that will not stop, gets a COMRESET.  The caller holds the command mutex.
 */
static int
ahci_port_recover(
	struct ahci_port *port)
{
	unsigned long irq;
	uint32_t task_file;
	int stopped;
	int error;

	/* Stops the engine, which drops the failed command from PxCI. */
	stopped = ahci_port_engine_stop(port);

	/* Clears the recorded errors and interrupts. */
	ahci_port_write(port, DRV_AHCI_PX_SERR, UINT32_MAX);
	ahci_port_write(port, DRV_AHCI_PX_IS, UINT32_MAX);

	/* Resets the link when the engine or the device is stuck. */
	task_file = ahci_port_read(port, DRV_AHCI_PX_TFD);
	if (stopped != 0 ||
	    (task_file & (DRV_AHCI_PX_TFD_BSY | DRV_AHCI_PX_TFD_DRQ)) != 0U) {
		error = ahci_port_link_reset(port);
		if (error != 0)
			return error;

		error = ahci_port_device_ready(port);
		if (error != 0)
			return error;

		/* The engine must be idle before it is started again. */
		error = ahci_port_wait_clear(
			port,
			DRV_AHCI_PX_CMD,
			DRV_AHCI_PX_CMD_CR,
			AHCI_ENGINE_STOP_MS);
		if (error != 0)
			return error;

		ahci_port_write(port, DRV_AHCI_PX_IS, UINT32_MAX);
	}

	/* Forgets the dropped command. */
	irq = spin_lock_irqsave(&port->state_lock);

	port->issued = 0;
	port->interrupt_status = 0;

	spin_unlock_irqrestore(&port->state_lock, irq);

	/* Starts the engine again. */
	ahci_port_engine_start(port);

	/* Succeeded: the port takes commands again. */
	return 0;
}

/* Moves a read or write request through the bounce buffer, chunk by chunk. */
static int
ahci_port_transfer(
	struct ahci_port *port,
	struct bio *bio,
	size_t *transferred)
{
	uint8_t *bytes;
	uint64_t block;
	uint32_t remaining;
	uint32_t count;
	size_t length;
	unsigned attempt;
	uint8_t command;
	int write;
	int retryable;
	int error;

	/* Chooses the command for the direction. */
	write = 0;
	command = DRV_AHCI_ATA_READ_DMA_EXT;
	if (bio->b_op == BIO_WRITE) {
		write = 1;
		command = DRV_AHCI_ATA_WRITE_DMA_EXT;
	}

	/* Runs one command per bounce-buffer-sized chunk. */
	*transferred = 0;
	bytes = bio->b_data;
	block = bio->b_mapped_block;
	remaining = bio->b_block_count;
	while (remaining != 0U) {
		count = drv_ahci_chunk_blocks(
			remaining,
			port->identity.sector_size,
			AHCI_BOUNCE_SIZE);
		if (count == 0U)
			return EINVAL;
		length = (size_t)count * port->identity.sector_size;

		/*
		 * Runs the chunk, and again after a timeout or a link fault:
		 * reading, or writing the same data, twice is harmless.
		 */
		attempt = 0;
		for (;;) {
			if (write)
				kern_memcpy(port->bounce_dma.address, bytes, length);
			error = ahci_port_execute(
				port,
				command,
				block,
				count,
				write,
				length,
				AHCI_COMMAND_TIMEOUT_MS,
				&retryable);
			if (error == 0)
				break;
			if (!retryable)
				break;
			if (attempt >= AHCI_IO_RETRIES)
				break;
			attempt++;
		}

		/* Reports the failed chunk; the earlier chunks stay counted. */
		if (error != 0)
			return error;

		/* Hands read data back to the caller. */
		if (!write)
			kern_memcpy(bytes, port->bounce_dma.address, length);

		*transferred += length;
		bytes += length;
		block += count;
		remaining -= count;
	}

	/* Succeeded: every block of the request moved. */
	return 0;
}

/* Makes the disk's volatile write cache durable. */
static int
ahci_port_flush(
	struct ahci_port *port)
{
	unsigned attempt;
	int retryable;
	int error;

	/* A disk without an enabled write cache holds nothing to flush. */
	if (!port->identity.write_cache)
		return 0;

	/*
	 * Issues FLUSH CACHE EXT, which the 48-bit feature set this driver
	 * requires includes, and repeats it after a timeout or a link fault.
	 */
	attempt = 0;
	for (;;) {
		error = ahci_port_execute(
			port,
			DRV_AHCI_ATA_FLUSH_CACHE_EXT,
			0U,
			0U,
			0,
			0U,
			AHCI_COMMAND_TIMEOUT_MS,
			&retryable);
		if (error == 0)
			break;
		if (!retryable)
			break;
		if (attempt >= AHCI_IO_RETRIES)
			break;
		attempt++;
	}

	/* Reports a cache the disk could not flush. */
	if (error != 0)
		return error;

	/* Succeeded: the disk's cache is on the medium. */
	return 0;
}
