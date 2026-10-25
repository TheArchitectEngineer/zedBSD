/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GHCI command processor from the driver's side: the ring and its
 * doorbell, the device's fence, the interrupt, the reset after a fault or
 * a hang, and the service thread that recovers and gives deferred memory
 * back.
 *
 * Every submission is followed in the ring by a fence of the driver's:
 * the device's one fence value, which only grows, written to the fence
 * page when everything before it has finished.  A submission is known by
 * its ticket, the fence value and the reset generation; a reset completes
 * every older ticket with an error and starts the ring again at index 0.
 */

#include <stddef.h>
#include <stdint.h>

#include <drivers/gpu/gpu.h>
#include <kern/clock.h>
#include <kern/device-io.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/sched.h>
#include <uapi/errno.h>

#include "ghci.h"
#include "hcr.h"
#include "packets.h"
#include "vk/vk-node.h"

/*
 * The interrupts the driver takes from the command processor: the fence,
 * a fault, the watchdog, the firmware's results of a frame, and the
 * display controller's events (a flip's record, an error, a change of
 * the connection; its vertical sync is not used).
 */
#define IRQ_DISPLAY			(GHCI_IRQ_FLIP | GHCI_IRQ_DISP_ERROR | GHCI_IRQ_HOTPLUG)
#define IRQ_TAKEN			(GHCI_IRQ_FENCE | GHCI_IRQ_FAULT | GHCI_IRQ_WATCHDOG | GHCI_IRQ_RESULT | IRQ_DISPLAY)

/* Every bit of IRQ_STATUS, cleared at the start. */
#define IRQ_ALL				0x1fffU

/* How often a wait for a fence looks at the fence page when no interrupt comes. */
#define FENCE_POLL_MS			10U

/* How long the stop waits for the service thread to leave. */
#define SERVICE_STOP_MS			5000U

/* How long the start waits for the processor to report RUNNING. */
#define RUN_WAIT_MS			100U

static void ring_put(struct ghci_device *device, uint32_t index, uint32_t value);
static uint32_t ring_get(struct ghci_device *device, uint32_t index);
static int ring_room(struct ghci_device *device, uint32_t need, uint32_t *pad);
static int ring_write(struct ghci_device *device, struct ghci_session *session, const uint32_t *packets, uint32_t dwords, int fenced, struct ghci_ticket *ticket);
static void refresh_completed(struct ghci_device *device);
static int fence_window_wait(struct ghci_device *device);
static void program_ring(struct ghci_device *device);
static void recover(struct ghci_device *device, const char *reason);
static void fail_sessions(struct ghci_device *device, uint64_t generation);
static void service_hang_check(struct ghci_device *device);

/*
 * Resets nothing but sets the aperture, the ring, the fences and the
 * interrupt of a GPU that is idle after its reset, then sets RUN.
 */
int
ghci_cp_start(
	struct ghci_device *device)
{
	uint32_t status;
	int error;

	/* The ring, the fence page and the bounce buffer of the transfers. */
	mutex_lock(&device->lock);

	error = ghci_memory_alloc(device, NULL, GHCI_USE_DRIVER, GHCI_RING_BYTES, &device->ring);
	if (error == 0)
		error = ghci_memory_alloc(device, NULL, GHCI_USE_DRIVER, GHCI_PAGE_BYTES, &device->fence_page);
	if (error == 0)
		error = ghci_memory_alloc(device, NULL, GHCI_USE_DRIVER, GHCI_BOUNCE_BYTES, &device->bounce);

	mutex_unlock(&device->lock);

	/* Without them the device cannot run. */
	if (error != 0) {
		kern_logf("ghci: %s: no memory for the ring (%d)\n", device->name, error);
		ghci_cp_stop(device);
		return error;
	}

	/* The ring starts empty and the fence at zero. */
	device->ring_dwords = GHCI_RING_BYTES / 4U;
	device->fence_issued = 0U;
	device->completed = 0U;
	device->generation = 1U;
	device->progress_tick = clock_ticks();
	program_ring(device);

	/* Leaves IDLE for RUNNING with the watchdog of one command on. */
	ghci_write(device, GHCI_CTRL, GHCI_CTRL_RUN | GHCI_CTRL_WDT_EN);
	error = ghci_poll(device, GHCI_STATUS, 0xfU << 16, GHCI_ST_RUNNING << 16, RUN_WAIT_MS);
	if (error != 0) {
		status = ghci_read(device, GHCI_STATUS);
		kern_logf("ghci: %s: command processor did not run (status %08x)\n", device->name, (unsigned)status);
		ghci_cp_stop(device);
		return EIO;
	}

	/* The processor takes the doorbell from here. */
	device->running = 1;

	/* Succeeded: the ring runs. */
	return 0;
}

/*
 * Waits for the driver's last fence and halts the command processor.
 */
void
ghci_cp_stop(
	struct ghci_device *device)
{
	struct ghci_ticket ticket;
	uint32_t state;
	int error;

	/* The last submission finishes first, when the processor runs. */
	if (device->running) {
		ticket.fence = device->fence_issued;
		ticket.generation = device->generation;
		error = ghci_cp_wait(device, &ticket, GHCI_FENCE_WAIT_MS);
		if (error != 0)
			kern_logf("ghci: %s: last fence not reached at the stop (%d)\n", device->name, error);
	}

	/* No more doorbells; the interrupt is off and stays off. */
	mutex_lock(&device->cp_lock);

	device->running = 0;
	device->irq_mask = 0U;
	ghci_write(device, GHCI_IRQ_ENABLE, 0U);

	/* A running processor is halted first, which quiesces its memory accesses. */
	state = GHCI_STATUS_STATE(ghci_read(device, GHCI_STATUS));
	if (state == GHCI_ST_RUNNING) {
		ghci_write(device, GHCI_CTRL, GHCI_CTRL_HALT);
		error = ghci_poll(device, GHCI_STATUS, GHCI_STATUS_HALTED | GHCI_STATUS_AXI_IDLE, GHCI_STATUS_HALTED | GHCI_STATUS_AXI_IDLE, GHCI_HALT_WAIT_MS);
		if (error != 0)
			kern_logf("ghci: %s: did not halt (status %08x)\n", device->name, (unsigned)ghci_read(device, GHCI_STATUS));
	}

	/* A reset leaves it idle; an empty aperture keeps it off the memory that goes back now. */
	ghci_write(device, GHCI_CTRL, GHCI_CTRL_RESET);
	(void)ghci_poll(device, GHCI_CTRL, GHCI_CTRL_RESET, 0U, GHCI_RESET_WAIT_MS);
	ghci_write(device, GHCI_APERTURE_BASE, 0U);
	ghci_write(device, GHCI_APERTURE_LIMIT, 0U);

	mutex_unlock(&device->cp_lock);

	/* The ring, the fence page and the bounce buffer go back. */
	mutex_lock(&device->lock);

	if (device->bounce != NULL)
		ghci_memory_free(device, device->bounce);
	if (device->fence_page != NULL)
		ghci_memory_free(device, device->fence_page);
	if (device->ring != NULL)
		ghci_memory_free(device, device->ring);
	device->bounce = NULL;
	device->fence_page = NULL;
	device->ring = NULL;

	mutex_unlock(&device->lock);
}

/*
 * Writes packets and the driver's fence into the ring and rings the
 * doorbell.  The device's lock is held when session is not NULL, since
 * the session's last fence is recorded.
 */
int
ghci_cp_submit(
	struct ghci_device *device,
	struct ghci_session *session,
	const uint32_t *packets,
	uint32_t dwords,
	struct ghci_ticket *ticket)
{
	int error;

	/* The packets and the fence after them. */
	error = ring_write(device, session, packets, dwords, 1, ticket);
	if (error != 0)
		return error;

	/* The session's memory now waits for this fence before it goes back. */
	if (session != NULL) {
		session->last_fence = ticket->fence;
		session->last_generation = ticket->generation;
		session->outstanding = 1;
	}

	/* Succeeded: the packets are in the ring. */
	return 0;
}

/*
 * Writes packets of the driver's own into the ring without a fence after
 * them, and rings the doorbell.
 *
 * A flip needs no fence: its record says when it was taken, and a fence
 * would drain every unit of the GPU behind the other sessions' work.
 */
int
ghci_cp_submit_unfenced(
	struct ghci_device *device,
	const uint32_t *packets,
	uint32_t dwords)
{
	struct ghci_ticket ticket;
	int error;

	/* The packets alone. */
	error = ring_write(device, NULL, packets, dwords, 0, &ticket);
	if (error != 0)
		return error;

	/* Succeeded: the packets are in the ring. */
	return 0;
}

/*
 * Waits a bounded time for a ticket.
 */
int
ghci_cp_wait(
	struct ghci_device *device,
	const struct ghci_ticket *ticket,
	unsigned milliseconds)
{
	uint64_t deadline;
	uint64_t now;
	uint64_t wake;
	uint64_t sequence;
	unsigned long irq;
	int passed;
	int error;

	/* The wait ends at the deadline. */
	deadline = clock_ticks() + kern_ms_to_ticks(milliseconds);

	/* Looks at the fence page, sleeping between looks, until the ticket is done. */
	for (;;) {
		refresh_completed(device);

		/* The ticket's state, read with the interrupt's lock. */
		irq = spin_lock_irqsave(&device->irq_lock);

		error = ETIMEDOUT;
		passed = ghci_fence_passed(device->completed, ticket->fence);
		if (ticket->generation != device->generation) {
			error = EIO;
		} else if (passed) {
			error = 0;
		}

		/* Not done: sleeps until a fence interrupt or the next look. */
		now = clock_ticks();
		if (error == ETIMEDOUT && now < deadline) {
			wake = now + kern_ms_to_ticks(FENCE_POLL_MS);
			if (wake > deadline)
				wake = deadline;
			sequence = waitq_sequence(&device->fence_wake);
			(void)waitq_sleep(&device->fence_wake, &device->irq_lock, sequence, wake, 0U);
		}

		spin_unlock_irqrestore(&device->irq_lock, irq);

		/* Ends at an answer, or at the deadline. */
		if (error != ETIMEDOUT)
			break;
		if (now >= deadline)
			break;
	}

	/* Reports a ticket that a reset ended, or a wait that ran out. */
	if (error != 0)
		return error;

	/* Succeeded: the fence has passed. */
	return 0;
}

/*
 * Reports the ticket of the newest submission.
 */
void
ghci_cp_last(
	struct ghci_device *device,
	struct ghci_ticket *ticket)
{
	unsigned long irq;

	/* The issued fence and its generation, read together. */
	irq = spin_lock_irqsave(&device->irq_lock);

	ticket->fence = device->fence_issued;
	ticket->generation = device->generation;

	spin_unlock_irqrestore(&device->irq_lock, irq);
}

/*
 * Reports the fence the GPU has completed, read afresh from the fence page.
 */
uint32_t
ghci_cp_completed(
	struct ghci_device *device)
{
	unsigned long irq;
	uint32_t completed;

	/* The fence page may have moved without an interrupt yet. */
	refresh_completed(device);

	/* The completed value, read with the interrupt's lock. */
	irq = spin_lock_irqsave(&device->irq_lock);

	completed = device->completed;

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* Reports the value. */
	return completed;
}

/*
 * Reports whether a ticket is done.
 */
int
ghci_cp_done(
	struct ghci_device *device,
	uint32_t fence,
	uint64_t generation)
{
	unsigned long irq;
	int done;

	/* The fence page may have moved without an interrupt yet. */
	refresh_completed(device);

	/* A reset ended every ticket of an older generation; otherwise the fence decides. */
	irq = spin_lock_irqsave(&device->irq_lock);

	done = 1;
	if (generation == device->generation)
		done = ghci_fence_passed(device->completed, fence);

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* Reports whether the ticket is done. */
	return done;
}

/*
 * The interrupt handler of the device: the fence, a fault, the watchdog,
 * the firmware's results and the display controller's events.
 */
int
ghci_cp_interrupt(
	void *argument)
{
	struct ghci_device *device;
	uint32_t status;
	unsigned long irq;
	int handled;

	/* Masks the GPU's interrupt and reads what is pending. */
	device = argument;
	ghci_write(device, GHCI_IRQ_ENABLE, 0U);
	status = ghci_read(device, GHCI_IRQ_STATUS) & IRQ_TAKEN;

	/* Clears what was pending (write 1 to clear). */
	if (status != 0U)
		ghci_write(device, GHCI_IRQ_STATUS, status);

	/* Unmasks what the driver takes; a bit still pending then sends a new message. */
	ghci_write(device, GHCI_IRQ_ENABLE, device->irq_mask);

	/* An interrupt with nothing pending is another device's on a shared line. */
	handled = 0;
	if (status != 0U)
		handled = 1;

	/* Nothing of the GPU's to serve. */
	if (status == 0U)
		return handled;

	/* A fence wrote the fence page: the waiters, the job markers and the reclaim look again. */
	if ((status & GHCI_IRQ_FENCE) != 0U) {
		refresh_completed(device);
		irq = spin_lock_irqsave(&device->irq_lock);

		device->service_work |= GHCI_WORK_RECLAIM | GHCI_WORK_REPLY;

		spin_unlock_irqrestore(&device->irq_lock, irq);
	}

	/* The firmware wrote a frame's results: the service thread completes its replies. */
	if ((status & GHCI_IRQ_RESULT) != 0U) {
		irq = spin_lock_irqsave(&device->irq_lock);

		device->service_work |= GHCI_WORK_REPLY;

		spin_unlock_irqrestore(&device->irq_lock, irq);
	}

	/* A fault or the watchdog: the first fault is recorded and the service thread resets. */
	if ((status & (GHCI_IRQ_FAULT | GHCI_IRQ_WATCHDOG)) != 0U) {
		irq = spin_lock_irqsave(&device->irq_lock);

		if (!device->fault_pending) {
			device->fault_info = ghci_read(device, GHCI_FAULT_INFO);
			device->fault_address = ghci_read(device, GHCI_FAULT_ADDR);
		}
		device->fault_pending = 1;
		device->service_work |= GHCI_WORK_RECOVER;

		spin_unlock_irqrestore(&device->irq_lock, irq);
	}

	/*
	 * A flip's record, a scan-out error or a change of the connection: the
	 * service thread reads the controller, and whoever waits for a flip
	 * reads the record page again.
	 */
	if ((status & IRQ_DISPLAY) != 0U) {
		irq = spin_lock_irqsave(&device->irq_lock);

		device->service_work |= GHCI_WORK_DISPLAY;

		spin_unlock_irqrestore(&device->irq_lock, irq);

		waitq_wake_all(&device->display.flip_wake);
	}

	/* Wakes the fence waiters and the service thread. */
	waitq_wake_all(&device->fence_wake);
	waitq_wake_all(&device->service_wake);

	/* Succeeded: the interrupt was the device's. */
	return handled;
}

/*
 * Stops the service thread and waits for it to leave.
 */
void
ghci_service_stop(
	struct ghci_device *device)
{
	unsigned long irq;
	unsigned waited;
	int running;

	/* Asks the thread to leave. */
	ghci_service_wake(device, GHCI_WORK_STOP);

	/* Waits, a bounded time, until it says it has left. */
	for (waited = 0U; waited < SERVICE_STOP_MS; waited += 10U) {
		irq = spin_lock_irqsave(&device->irq_lock);

		running = device->service_running;

		spin_unlock_irqrestore(&device->irq_lock, irq);

		/* The thread has left. */
		if (!running)
			return;

		ghci_sleep_ms(10U);
	}

	/* A thread that does not leave is reported; the device stays allocated. */
	kern_logf("ghci: %s: service thread did not stop\n", device->name);
}

/*
 * Wakes the service thread for work.
 */
void
ghci_service_wake(
	struct ghci_device *device,
	unsigned work)
{
	unsigned long irq;

	/* Records the work and wakes the thread. */
	irq = spin_lock_irqsave(&device->irq_lock);

	device->service_work |= work;

	spin_unlock_irqrestore(&device->irq_lock, irq);

	waitq_wake_all(&device->service_wake);
}

/*
 * Runs the service thread: sleeps until work comes or the period ends,
 * and does it, until asked to stop.
 */
void
ghci_service_main(
	void *argument)
{
	struct ghci_device *device;
	uint64_t sequence;
	uint64_t wake;
	unsigned long irq;
	unsigned work;

	device = argument;

	/* Serves work until asked to stop. */
	for (;;) {
		/* Sleeps until work comes or the period ends. */
		irq = spin_lock_irqsave(&device->irq_lock);

		if (device->service_work == 0U) {
			wake = clock_ticks() + kern_ms_to_ticks(GHCI_SERVICE_PERIOD_MS);
			sequence = waitq_sequence(&device->service_wake);
			(void)waitq_sleep(&device->service_wake, &device->irq_lock, sequence, wake, 0U);
		}
		work = device->service_work;
		device->service_work = 0U;

		spin_unlock_irqrestore(&device->irq_lock, irq);

		/* Leaves when the device stops. */
		if ((work & GHCI_WORK_STOP) != 0U)
			break;

		/* The work of this round. */
		ghci_service_step(device, work);
	}

	/* Tells the stop that the thread has left. */
	irq = spin_lock_irqsave(&device->irq_lock);

	device->service_running = 0;

	spin_unlock_irqrestore(&device->irq_lock, irq);
}

/*
 * Does one round of the service's work: the recovery from a fault, the
 * hang check and the return of deferred memory.
 */
void
ghci_service_step(
	struct ghci_device *device,
	unsigned work)
{
	uint64_t given;

	/* A fault resets the processor. */
	if ((work & GHCI_WORK_RECOVER) != 0U)
		recover(device, "fault");

	/* Work that stopped moving resets it too. */
	service_hang_check(device);

	/* The display's records, errors and connection, and the end of a lease. */
	ghci_display_service(device, work);

	/*
	 * The executor's frames whose results are in and its job markers whose
	 * fences passed, before the reclaim: a frame's reply blob is held
	 * until its replies are written.
	 */
	ghci_vk_service(device);

	/* Memory the GPU is done with goes back. */
	mutex_lock(&device->lock);

	given = ghci_memory_reclaim(device);

	mutex_unlock(&device->lock);

	/* The core hears of the room, after the lock is let go. */
	if (given != 0U && device->gpu != NULL)
		drv_gpu_capacity_changed(device->gpu);
}

/* Stores one dword of the ring. */
static void
ring_put(
	struct ghci_device *device,
	uint32_t index,
	uint32_t value)
{
	volatile uint32_t *ring;

	/* The ring is the processor's view of its memory: the window or host RAM. */
	ring = (volatile uint32_t *)device->ring->view;
	ring[index] = value;
}

/* Loads one dword of the ring. */
static uint32_t
ring_get(
	struct ghci_device *device,
	uint32_t index)
{
	volatile uint32_t *ring;
	uint32_t value;

	/* The same view as ring_put. */
	ring = (volatile uint32_t *)device->ring->view;
	value = ring[index];

	/* Reports the dword. */
	return value;
}

/* Waits for room for a run in the ring, and plans its padding.  cp_lock is held. */
static int
ring_room(
	struct ghci_device *device,
	uint32_t need,
	uint32_t *pad)
{
	uint32_t rptr;
	unsigned waited;
	int error;

	/* Looks at the read pointer every millisecond, a bounded time. */
	for (waited = 0U; waited <= GHCI_RING_WAIT_MS; waited++) {
		rptr = ghci_read(device, GHCI_RING_RPTR);
		error = ghci_ring_plan(device->wptr, rptr, device->ring_dwords, need, pad);
		if (error != EAGAIN)
			break;

		ghci_sleep_ms(1U);
	}

	/* A run no ring can hold, or a ring that never made room. */
	if (error == EAGAIN)
		return EBUSY;
	if (error != 0)
		return error;

	/* Succeeded: the run fits. */
	return 0;
}

/*
 * Writes a run of packets into the ring, with the driver's fence after it
 * when fenced is nonzero (the ticket is then the fence's), and rings the
 * doorbell.
 */
static int
ring_write(
	struct ghci_device *device,
	struct ghci_session *session,
	const uint32_t *packets,
	uint32_t dwords,
	int fenced,
	struct ghci_ticket *ticket)
{
	uint32_t fence_packet[GHCI_FENCE_PACKET_DWORDS];
	uint32_t index;
	uint32_t need;
	uint32_t pad;
	uint32_t tag;
	uint32_t fence;
	uint32_t last;
	unsigned long irq;
	int idle;
	int error;

	/* The run is the packets and, when fenced, the fence after them. */
	need = dwords;
	if (fenced)
		need = dwords + GHCI_FENCE_PACKET_DWORDS;
	tag = GHCI_TAG_DRIVER;
	if (session != NULL)
		tag = session->tag;

	/* One writer of the ring at a time. */
	mutex_lock(&device->cp_lock);

	/* A device that stopped or was lost takes nothing. */
	if (!device->running || device->lost) {
		mutex_unlock(&device->cp_lock);
		return EIO;
	}

	/* The fence values in flight stay well inside half the circle. */
	error = fence_window_wait(device);
	if (error != 0) {
		mutex_unlock(&device->cp_lock);
		return error;
	}

	/* Waits for room, which the processor makes as it parses. */
	error = ring_room(device, need, &pad);
	if (error != 0) {
		mutex_unlock(&device->cp_lock);
		return error;
	}

	/* The padding up to the end of the ring, which the processor skips. */
	if (pad != 0U) {
		ring_put(device, device->wptr, GHCI_PKT_PAD(pad - 1U));
		for (index = 1U; index < pad; index++)
			ring_put(device, device->wptr + index, 0U);
		device->wptr = 0U;
	}

	/* The packets. */
	for (index = 0U; index < dwords; index++)
		ring_put(device, device->wptr + index, packets[index]);

	/* The driver's fence after them, which raises the fence interrupt. */
	if (fenced) {
		fence = device->fence_issued + 1U;
		ghci_packet_fence(tag, device->fence_page->gpu_address, fence, 1, fence_packet);
		for (index = 0U; index < GHCI_FENCE_PACKET_DWORDS; index++)
			ring_put(device, device->wptr + dwords + index, fence_packet[index]);

		/*
		 * The fence is issued before the doorbell, so the interrupt of its
		 * write always finds it issued; an idle device starts its hang clock
		 * now.
		 */
		irq = spin_lock_irqsave(&device->irq_lock);

		idle = 0;
		if (device->completed == device->fence_issued)
			idle = 1;
		device->fence_issued = fence;
		if (idle)
			device->progress_tick = clock_ticks();
		ticket->fence = fence;
		ticket->generation = device->generation;

		spin_unlock_irqrestore(&device->irq_lock, irq);
	}

	/*
	 * The ring's words must reach the GPU before the doorbell: a ring in
	 * local memory is read back (the read returns after the writes), and
	 * the write barrier orders the doorbell after them.
	 */
	last = device->wptr + need - 1U;
	if (device->ring->kind != GHCI_MEMORY_HOST)
		(void)ring_get(device, last);
	kern_io_write_barrier();

	/* The doorbell. */
	device->wptr = (device->wptr + need) % device->ring_dwords;
	ghci_write(device, GHCI_RING_WPTR, device->wptr);

	mutex_unlock(&device->cp_lock);

	/* Succeeded: the packets are in the ring. */
	return 0;
}

/* Reads the fence page and moves the completed fence forward, never back. */
static void
refresh_completed(
	struct ghci_device *device)
{
	volatile uint32_t *page;
	uint32_t value;
	unsigned long irq;
	int ahead;
	int moved;

	/* A device without its fence page has nothing to read. */
	if (device->fence_page == NULL)
		return;

	/* The value the GPU wrote last. */
	page = (volatile uint32_t *)device->fence_page->view;
	value = page[0];

	/* Only a value between the completed and the issued fence moves the completed one. */
	irq = spin_lock_irqsave(&device->irq_lock);

	moved = 0;
	ahead = ghci_fence_passed(value, device->completed);
	if (ahead && value != device->completed) {
		ahead = ghci_fence_passed(device->fence_issued, value);
		if (ahead) {
			device->completed = value;
			device->progress_tick = clock_ticks();
			moved = 1;
		}
	}

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* Wakes whoever waits for a fence. */
	if (moved)
		waitq_wake_all(&device->fence_wake);
}

/* Waits while the fences in flight span a quarter of the circle.  cp_lock is held. */
static int
fence_window_wait(
	struct ghci_device *device)
{
	struct ghci_ticket ticket;
	uint32_t distance;
	int error;

	/* Usually far inside the window. */
	distance = device->fence_issued - device->completed;
	if (distance < GHCI_FENCE_WINDOW)
		return 0;

	/* Waits for the oldest quarter to pass. */
	ticket.fence = device->fence_issued - GHCI_FENCE_WINDOW / 2U;
	ticket.generation = device->generation;
	error = ghci_cp_wait(device, &ticket, GHCI_FENCE_WAIT_MS);
	if (error != 0)
		return error;

	/* Succeeded: the window has room again. */
	return 0;
}

/*
 * Writes the aperture, the ring, the fences and the interrupt of an idle
 * processor, with the ring empty.
 */
static void
program_ring(
	struct ghci_device *device)
{
	volatile uint32_t *page;

	/* The fence page starts at the completed value. */
	page = (volatile uint32_t *)device->fence_page->view;
	page[0] = device->completed;
	kern_io_write_barrier();

	/* The memory the GPU may touch. */
	ghci_write(device, GHCI_APERTURE_BASE, device->aperture_base);
	ghci_write(device, GHCI_APERTURE_LIMIT, device->aperture_limit);

	/* The ring, empty. */
	device->wptr = 0U;
	ghci_write(device, GHCI_RING_BASE, device->ring->gpu_address);
	ghci_write(device, GHCI_RING_SIZE, GHCI_RING_SIZE_LOG2);

	/* The fence value the GPU continues from. */
	ghci_write(device, GHCI_FENCE_LAST, device->completed);

	/* The interrupts: everything old cleared, the driver's ones on (the handler restores the mask). */
	device->irq_mask = IRQ_TAKEN;
	ghci_write(device, GHCI_IRQ_STATUS, IRQ_ALL);
	ghci_write(device, GHCI_IRQ_ENABLE, device->irq_mask);
}

/*
 * Resets the processor after a fault or a hang, completes every fence
 * issued so far with an error, and runs the ring again from its start.
 */
static void
recover(
	struct ghci_device *device,
	const char *reason)
{
	uint32_t info;
	uint32_t address;
	uint32_t status;
	uint64_t generation;
	unsigned long irq;
	int error;

	/*
	 * The display's flips are counted again across the reset: its lock is
	 * taken before the ring's, the order every presentation takes them in.
	 */
	ghci_display_recover_begin(device);

	/* No submission while the processor is reset. */
	mutex_lock(&device->cp_lock);

	/* A device that stopped or was already lost is left as it is. */
	if (!device->running || device->lost) {
		mutex_unlock(&device->cp_lock);
		ghci_display_recover_end(device, 0);
		return;
	}

	/* Says why, with the first fault's record. */
	irq = spin_lock_irqsave(&device->irq_lock);

	info = device->fault_info;
	address = device->fault_address;

	spin_unlock_irqrestore(&device->irq_lock, irq);

	status = ghci_read(device, GHCI_STATUS);
	kern_logf("ghci: %s: %s: status %08x fault %08x (kind %u, block %u, tag %u) at %08x; resetting\n",
	    device->name,
	    reason,
	    (unsigned)status,
	    (unsigned)info,
	    (unsigned)GHCI_FAULT_KIND(info),
	    (unsigned)GHCI_FAULT_BLOCK(info),
	    (unsigned)GHCI_FAULT_JOB_TAG(info),
	    (unsigned)address);

	/* The reset quiesces the GPU and keeps the aperture, the ring's place and the interrupt mask. */
	ghci_write(device, GHCI_CTRL, GHCI_CTRL_RESET);
	error = ghci_poll(device, GHCI_CTRL, GHCI_CTRL_RESET, 0U, GHCI_RESET_WAIT_MS);

	/* The reset stopped the firmware too; it runs again before the ring does. */
	if (error == 0)
		error = ghci_firmware_start(device);

	/* Every fence issued so far is done: with an error, for the generation that ends here. */
	irq = spin_lock_irqsave(&device->irq_lock);

	device->completed = device->fence_issued;
	device->generation++;
	generation = device->generation;
	device->fault_pending = 0;
	device->fault_info = 0U;
	device->fault_address = 0U;
	device->resets++;
	device->progress_tick = clock_ticks();
	if (error != 0)
		device->lost = 1;

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/*
	 * A GPU that does not come back from its reset is lost until the
	 * detach.  One that does is idle now: the display learns which of its
	 * flips were taken before the ring starts again.
	 */
	if (error != 0) {
		kern_logf("ghci: %s: reset did not end; the device is lost\n", device->name);
	} else {
		ghci_display_recover_count(device);
		program_ring(device);
		ghci_write(device, GHCI_CTRL, GHCI_CTRL_RUN | GHCI_CTRL_WDT_EN);
	}

	mutex_unlock(&device->cp_lock);

	/* The display places again what the reset lost and lets its lock go. */
	ghci_display_recover_end(device, error == 0);

	/* The waiters see the new generation. */
	waitq_wake_all(&device->fence_wake);

	/* The sessions whose work was in flight lose it. */
	fail_sessions(device, generation);

	/* The firmware restarted: the executor's frames and objects are gone, and it takes its heap again. */
	if (error == 0)
		ghci_vk_reset(device);
}

/* Reports the loss to every session whose last submission belonged to an ended generation. */
static void
fail_sessions(
	struct ghci_device *device,
	uint64_t generation)
{
	struct ghci_session *session;

	/* Walks the sessions under the device's lock. */
	mutex_lock(&device->lock);

	for (session = device->sessions;
	     session != NULL;
	     session = session->next) {
		/* A session with nothing in flight lost nothing. */
		if (!session->outstanding)
			continue;
		if (session->last_generation == generation)
			continue;

		/* The session's work is gone: its later submissions see the error. */
		session->outstanding = 0;
		if (device->gpu != NULL)
			drv_gpu_report_session_error(device->gpu, session, EIO);
	}

	mutex_unlock(&device->lock);
}

/* Resets the processor when issued work has not moved the fence for too long. */
static void
service_hang_check(
	struct ghci_device *device)
{
	uint64_t since;
	uint64_t limit;
	unsigned long irq;
	int busy;

	/* The fence page may have moved without an interrupt. */
	refresh_completed(device);

	/* Work is outstanding and the fence stood still since the last progress. */
	irq = spin_lock_irqsave(&device->irq_lock);

	busy = 0;
	if (device->completed != device->fence_issued)
		busy = 1;
	since = clock_ticks() - device->progress_tick;

	spin_unlock_irqrestore(&device->irq_lock, irq);

	/* A device without work, or with work that moves, is fine. */
	limit = kern_ms_to_ticks(GHCI_HANG_MS);
	if (!busy)
		return;
	if (since < limit)
		return;

	/* The work hangs. */
	recover(device, "no progress");
}
