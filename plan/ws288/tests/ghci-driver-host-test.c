/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the GHCI driver's device, ring, memory and native
 * command (src/drivers/gpu/ghci/ghci.c, cp.c, memory.c, blit.c,
 * native.c) against a model of a GHCI GPU.
 *
 * The model is a register window, local memory with a window onto part
 * of it, and a command processor that parses the ring when the doorbell
 * rings: padding, SET_REG of the blitter, BLIT (fill and copy of 32-bit
 * pixels), FENCE, WAIT_IDLE, INVALIDATE and NOP; DRAW is taken as a
 * fault of an unknown operation.  It raises the interrupt by calling the
 * driver's handler.  The kernel's services the driver uses (locks, wait
 * queues, the clock, MMIO, RAM, the GPU core) are stubs: the test runs on
 * one thread, and every sleep advances a fake clock and lets the model run.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/gpu/ghci/ghci.h"
#include "kern/clock.h"
#include "drivers/gpu/ghci/hcr.h"
#include "drivers/gpu/ghci/packets.h"
#include "uapi/gpu.h"
#include "uapi/gpu-op.h"

/* The model's register window (128 KiB) and the place of its extended capabilities. */
#define MODEL_WINDOW_WORDS		32768U
#define MODEL_EXTCAP			0x100U
#define MODEL_MEM_REGISTERS		0x180U

/* The model's local memory: 8 MiB at 2 GiB, of which the window shows the first 4 MiB. */
#define MODEL_LOCAL_BASE		0x80000000U
#define MODEL_LOCAL_BYTES		(8U << 20)
#define MODEL_WINDOW_BYTES		(4U << 20)
#define MODEL_WINDOW_PHYSICAL		0xe0000000ULL

/* The model's host RAM for the unified GPU: 8 MiB at 256 MiB, handed out by a bump allocator. */
#define MODEL_HOST_BASE			0x10000000U
#define MODEL_HOST_BYTES		(8U << 20)

/* The state words of the model's blitter. */
#define MODEL_STATE_WORDS		512U

/*
 * The counts of the checks that held and that failed.  Every check adds
 * to one of them; main reports both and fails when any check failed.
 */
static unsigned checks_passed;
static unsigned checks_failed;

/* The model's register window, which the driver maps as its registers. */
static uint32_t model_registers[MODEL_WINDOW_WORDS];

/* The model's local memory; the driver's window is its first part. */
static uint8_t model_local[MODEL_LOCAL_BYTES];

/* The model's host RAM and the next free byte of it. */
static uint8_t model_host[MODEL_HOST_BYTES];
static uint32_t model_host_next;

/*
 * The model's command processor: its state, ring pointers, interrupt
 * registers, fault record, blitter state and whether it is paused (a
 * paused processor parses nothing until the test resumes it).
 */
static unsigned model_state;
static uint32_t model_rptr;
static uint32_t model_wptr;
static uint32_t model_irq_status;
static uint32_t model_irq_enable;
static uint32_t model_fault_info;
static uint32_t model_fault_address;
static uint32_t model_blt[MODEL_STATE_WORDS];
static int model_paused;
static int model_running_now;
static unsigned model_packets_crossing;
static unsigned model_doorbells;

/* The device under test, whose interrupt handler the model calls. */
static struct ghci_device *model_device;

/* The fake clock: one tick per sleep. */
static uint64_t fake_ticks;

/* What the GPU core stubs saw: the registered operations, session errors, capacity notes. */
static const struct drv_gpu_ops *core_ops;
static void *core_private;
static unsigned core_session_errors;
static unsigned core_capacity_notes;
static int core_superuser;

static void expect(int held, const char *name);
static void model_reset(void);
static void model_run(void);
static uint8_t *model_memory(uint32_t address, uint32_t bytes);
static int model_packet(const uint32_t *ring, uint32_t ring_dwords, uint32_t at, uint32_t *length);
static void model_fault(uint32_t kind, uint32_t header);
static void model_blit(void);
static void model_interrupt(void);
static void model_setup_window(int discrete);
static void test_discrete(void);
static void test_unified(void);

/* The kernel's MMIO: the register window is the model's. */
uint32_t
kern_mmio_read32(
	const volatile void *address)
{
	uint32_t offset;
	uint32_t value;

	/* The register's offset in the window. */
	offset = (uint32_t)((const volatile uint8_t *)address - (const volatile uint8_t *)model_registers);

	/* The registers that the processor computes. */
	if (offset == GHCI_STATUS) {
		value = (model_state << 16) | GHCI_STATUS_MEM_READY | GHCI_STATUS_AXI_IDLE;
		if (model_state == GHCI_ST_HALTED)
			value |= GHCI_STATUS_HALTED;
		if (model_state == GHCI_ST_FAULTED)
			value |= GHCI_STATUS_FAULT;
		if (model_rptr == model_wptr)
			value |= GHCI_STATUS_RING_EMPTY;
		return value;
	}
	if (offset == GHCI_RING_RPTR)
		return model_rptr;
	if (offset == GHCI_RING_WPTR)
		return model_wptr;
	if (offset == GHCI_IRQ_STATUS)
		return model_irq_status;
	if (offset == GHCI_IRQ_ENABLE)
		return model_irq_enable;
	if (offset == GHCI_FAULT_INFO)
		return model_fault_info;
	if (offset == GHCI_FAULT_ADDR)
		return model_fault_address;
	if (offset == GHCI_CTRL)
		return model_registers[offset / 4U] & ~GHCI_CTRL_RESET;

	/* Every other register reads what is stored. */
	return model_registers[offset / 4U];
}

/* The kernel's MMIO writes: the model reacts to the control and ring registers. */
void
kern_mmio_write32(
	volatile void *address,
	uint32_t value)
{
	uint32_t offset;

	/* The register's offset in the window. */
	offset = (uint32_t)((volatile uint8_t *)address - (volatile uint8_t *)model_registers);

	/* IRQ_STATUS is write-1-to-clear; IRQ_ENABLE may raise a pending interrupt. */
	if (offset == GHCI_IRQ_STATUS) {
		model_irq_status &= ~value;
		return;
	}
	if (offset == GHCI_IRQ_ENABLE) {
		model_irq_enable = value;
		return;
	}

	/* CTRL: a reset, RUN and HALT. */
	if (offset == GHCI_CTRL) {
		if ((value & GHCI_CTRL_RESET) != 0U) {
			model_reset();
			return;
		}
		model_registers[offset / 4U] = value;
		if ((value & GHCI_CTRL_RUN) != 0U && model_state == GHCI_ST_IDLE)
			model_state = GHCI_ST_RUNNING;
		if ((value & GHCI_CTRL_HALT) != 0U && model_state == GHCI_ST_RUNNING)
			model_state = GHCI_ST_HALTED;
		return;
	}

	/* The doorbell: the processor parses up to the new write pointer. */
	if (offset == GHCI_RING_WPTR) {
		model_wptr = value;
		model_doorbells++;
		model_run();
		return;
	}

	/* Every other register is stored. */
	model_registers[offset / 4U] = value;
}

/* The barriers do nothing on the host. */
void
kern_io_write_barrier(void)
{
}

/* The locks: the test runs on one thread. */
int
mutex_init(
	struct mutex *mutex,
	enum lock_rank rank,
	const char *name)
{
	(void)rank;
	(void)name;
	memset(mutex, 0, sizeof(*mutex));
	return 0;
}

void
mutex_lock(
	struct mutex *mutex)
{
	(void)mutex;
}

void
mutex_unlock(
	struct mutex *mutex)
{
	(void)mutex;
}

void
spin_init(
	struct spinlock *lock,
	enum lock_rank rank,
	const char *name)
{
	(void)rank;
	(void)name;
	memset(lock, 0, sizeof(*lock));
}

unsigned long
spin_lock_irqsave(
	struct spinlock *lock)
{
	(void)lock;
	return 0;
}

void
spin_unlock_irqrestore(
	struct spinlock *lock,
	unsigned long enabled)
{
	(void)lock;
	(void)enabled;
}

/* The wait queues: a sleep is one tick of the fake clock in which the model runs. */
void
waitq_init(
	struct wait_queue *queue,
	const char *name)
{
	(void)name;
	memset(queue, 0, sizeof(*queue));
}

uint64_t
waitq_sequence(
	const struct wait_queue *queue)
{
	(void)queue;
	return 0;
}

int
waitq_sleep(
	struct wait_queue *queue,
	struct spinlock *condition_lock,
	uint64_t observed,
	uint64_t deadline,
	unsigned flags)
{
	(void)queue;
	(void)condition_lock;
	(void)observed;
	(void)deadline;
	(void)flags;
	fake_ticks++;
	model_run();
	return 0;
}

void
waitq_wake_all(
	struct wait_queue *queue)
{
	(void)queue;
}

/* The clock and the scheduler's sleep. */
uint64_t
clock_ticks(void)
{
	return fake_ticks;
}

void
sched_sleep(
	uint64_t timeout_tick)
{
	/* The clock jumps to the deadline (at least one tick), and the model runs. */
	if (timeout_tick > fake_ticks)
		fake_ticks = timeout_tick;
	else
		fake_ticks++;
	model_run();
}

/* The log goes to standard output. */
int
kern_logf(
	const char *format,
	...)
{
	va_list arguments;
	int written;

	va_start(arguments, format);
	printf("  log: ");
	written = vprintf(format, arguments);
	va_end(arguments);
	return written;
}

/* The kernel's heap is the host's. */
void *
kern_malloc(
	size_t size)
{
	return malloc(size);
}

void *
kern_calloc(
	size_t count,
	size_t size)
{
	return calloc(count, size);
}

void
kern_free(
	void *pointer)
{
	free(pointer);
}

/* RAM below 4 GiB: the model's host RAM, a bump allocator that never reuses. */
int
kern_pmem_alloc_limited(
	size_t size,
	size_t alignment,
	uint64_t max_address,
	size_t boundary,
	struct kern_pmem *run)
{
	uint32_t start;

	(void)boundary;

	/* The next aligned run, if it fits and the device can name it. */
	start = (model_host_next + (uint32_t)alignment - 1U) & ~((uint32_t)alignment - 1U);
	if (start + size > MODEL_HOST_BYTES)
		return 4;
	if ((uint64_t)MODEL_HOST_BASE + start + size - 1U > max_address)
		return 4;
	model_host_next = start + (uint32_t)size;
	run->paddr = MODEL_HOST_BASE + start;
	run->size = size;
	return 0;
}

int
kern_pmem_free(
	struct kern_pmem *run)
{
	(void)run;
	return 0;
}

void *
kern_pmem_to_kernel(
	hal_physaddr_t address)
{
	return &model_host[address - MODEL_HOST_BASE];
}

/* The GPU core: the registration is remembered; the reports are counted. */
int
drv_gpu_register(
	const struct drv_gpu_ops *ops,
	void *private_data,
	struct drv_gpu_device **result)
{
	core_ops = ops;
	core_private = private_data;
	*result = (struct drv_gpu_device *)&core_ops;
	return 0;
}

int
drv_gpu_unregister(
	struct drv_gpu_device *device)
{
	(void)device;
	core_ops = NULL;
	return 0;
}

void
drv_gpu_report_session_error(
	struct drv_gpu_device *device,
	void *session,
	int error)
{
	(void)device;
	(void)session;
	(void)error;
	core_session_errors++;
}

void
drv_gpu_capacity_changed(
	struct drv_gpu_device *device)
{
	(void)device;
	core_capacity_notes++;
}

/* The credentials: root or not, as the test says. */
const struct ucred *
cred_current(void)
{
	return NULL;
}

int
cred_is_superuser(
	const struct ucred *credentials)
{
	(void)credentials;
	return core_superuser;
}

/* The parts of the driver the test does not run: the thread, the display and the compiler. */
int
ghci_service_start(
	struct ghci_device *device)
{
	(void)device;
	return 0;
}

const struct ghci_isa_backend *
ghci_isa_select(
	uint32_t caps,
	uint32_t isa_id)
{
	(void)caps;
	(void)isa_id;
	return NULL;
}

int
ghci_display_attach(
	struct ghci_device *device)
{
	device->display.present = 0;
	return 0;
}

void
ghci_display_detach(
	struct ghci_device *device)
{
	(void)device;
}

void
ghci_display_session_close(
	struct ghci_device *device,
	struct ghci_session *session)
{
	(void)device;
	(void)session;
}

int
ghci_display_interrupt(
	struct ghci_device *device)
{
	(void)device;
	return 0;
}

const struct drv_gpu_display_ops ghci_display_ops;

/*
 * Runs both GPUs: a discrete one with a window onto part of its local
 * memory, and a unified one.
 */
int
main(void)
{
	/* The discrete GPU, then the unified one. */
	test_discrete();
	test_unified();

	/* Reports the counts of the checks. */
	printf("ghci-driver-host-test: %u passed, %u failed\n", checks_passed, checks_failed);

	/* A failed check fails the test. */
	if (checks_failed != 0U)
		return 1;

	/* Succeeded: every check held. */
	return 0;
}

/* Counts one check, and names it when it failed. */
static void
expect(
	int held,
	const char *name)
{
	/* A check that held is only counted. */
	if (held) {
		checks_passed++;
		return;
	}

	/* A check that failed is counted and named. */
	checks_failed++;
	printf("FAIL %s\n", name);
}

/* Resets the model's processor: idle, empty ring, no interrupt, no fault (the aperture and ring base stay). */
static void
model_reset(void)
{
	model_state = GHCI_ST_IDLE;
	model_rptr = 0U;
	model_wptr = 0U;
	model_irq_status = 0U;
	model_fault_info = 0U;
	model_fault_address = 0U;
	model_registers[GHCI_FENCE_LAST / 4U] = 0U;
	model_registers[GHCI_CTRL / 4U] = 0U;
}

/* Reports the bytes of GPU memory at an address, or NULL outside the aperture or any memory. */
static uint8_t *
model_memory(
	uint32_t address,
	uint32_t bytes)
{
	uint64_t end;
	uint32_t base;
	uint32_t limit;

	/* Every access is checked against the aperture. */
	end = (uint64_t)address + bytes;
	base = model_registers[GHCI_APERTURE_BASE / 4U];
	limit = model_registers[GHCI_APERTURE_LIMIT / 4U];
	if (address < base || end > limit)
		return NULL;

	/* Local memory. */
	if (address >= MODEL_LOCAL_BASE && end <= (uint64_t)MODEL_LOCAL_BASE + MODEL_LOCAL_BYTES)
		return &model_local[address - MODEL_LOCAL_BASE];

	/* Host RAM. */
	if (address >= MODEL_HOST_BASE && end <= (uint64_t)MODEL_HOST_BASE + MODEL_HOST_BYTES)
		return &model_host[address - MODEL_HOST_BASE];

	/* Nothing is there. */
	return NULL;
}

/* Records the first fault and stops the processor. */
static void
model_fault(
	uint32_t kind,
	uint32_t header)
{
	if (model_state != GHCI_ST_FAULTED) {
		model_fault_info = (kind << 12) | (GHCI_PKT_GET_TAG(header) << 24);
		model_fault_address = 0U;
	}
	model_state = GHCI_ST_FAULTED;
	model_irq_status |= GHCI_IRQ_FAULT;
}

/* Runs the blitter on its state: a fill or a copy of 32-bit pixels. */
static void
model_blit(void)
{
	uint32_t op;
	uint32_t width;
	uint32_t height;
	uint32_t row;
	uint32_t column;
	uint8_t *dst;
	uint8_t *src;
	uint32_t dst_row;
	uint32_t src_row;
	uint32_t value;

	op = model_blt[GHCI_S_BLT_OP] & 7U;
	width = model_blt[GHCI_S_BLT_SIZE] & 0xffffU;
	height = model_blt[GHCI_S_BLT_SIZE] >> 16;

	/* One row at a time, each checked against the aperture. */
	for (row = 0U; row < height; row++) {
		dst_row = model_blt[GHCI_S_BLT_DST_BASE] +
		    ((model_blt[GHCI_S_BLT_DST_XY] >> 16) + row) * model_blt[GHCI_S_BLT_DST_STRIDE] +
		    (model_blt[GHCI_S_BLT_DST_XY] & 0xffffU) * 4U;
		dst = model_memory(dst_row, width * 4U);
		if (dst == NULL) {
			model_fault(GHCI_FAULT_APERTURE, 0U);
			return;
		}

		/* A fill writes the colour; a copy reads its source row. */
		if (op == GHCI_BLT_FILL) {
			for (column = 0U; column < width; column++) {
				value = model_blt[GHCI_S_BLT_COLOR];
				memcpy(&dst[column * 4U], &value, 4U);
			}
			continue;
		}
		src_row = model_blt[GHCI_S_BLT_SRC_BASE] +
		    ((model_blt[GHCI_S_BLT_SRC_XY] >> 16) + row) * model_blt[GHCI_S_BLT_SRC_STRIDE] +
		    (model_blt[GHCI_S_BLT_SRC_XY] & 0xffffU) * 4U;
		src = model_memory(src_row, width * 4U);
		if (src == NULL) {
			model_fault(GHCI_FAULT_APERTURE, 0U);
			return;
		}
		memmove(dst, src, width * 4U);
	}
}

/*
 * Parses one packet at a ring index: 0 with its length, or a fault.
 * ring_dwords is the ring's size; a packet may not run past its end or
 * past the write pointer.
 */
static int
model_packet(
	const uint32_t *ring,
	uint32_t ring_dwords,
	uint32_t at,
	uint32_t *length)
{
	uint32_t header;
	uint32_t class;
	uint32_t op;
	uint32_t flags;
	uint32_t count;
	uint32_t index;
	uint32_t available;
	uint8_t *word;

	header = ring[at];
	class = GHCI_PKT_GET_CLASS(header);
	op = (header >> 24) & 0x3fU;
	flags = (header >> 8) & 0xffU;
	count = header & 0xffU;
	if (class == GHCI_PKT_CLASS_PAD || class == GHCI_PKT_CLASS_V)
		count = header & 0xffffU;
	*length = 1U + count;

	/* A packet that crosses the end of the ring or the write pointer is a ring error. */
	available = model_wptr - at;
	if (model_wptr < at)
		available = ring_dwords - at;
	if (at + *length > ring_dwords) {
		model_packets_crossing++;
		model_fault(GHCI_FAULT_RING, header);
		return -1;
	}
	if (*length > available) {
		model_fault(GHCI_FAULT_RING, header);
		return -1;
	}

	/* Padding is skipped; only standard micro-operations are known. */
	if (class == GHCI_PKT_CLASS_PAD)
		return 0;
	if (class != GHCI_PKT_CLASS_STD) {
		model_fault(GHCI_FAULT_OPCODE, header);
		return -1;
	}

	/* Each operation. */
	switch (op) {
	case GHCI_OP_NOP:
	case GHCI_OP_WAIT_IDLE:
	case GHCI_OP_INVALIDATE:
		break;

	case GHCI_OP_SET_REG:
		for (index = 0U; index + 1U < count; index += 2U)
			model_blt[ring[at + 1U + index] % MODEL_STATE_WORDS] = ring[at + 2U + index];
		break;

	case GHCI_OP_BLIT:
		model_blit();
		break;

	case GHCI_OP_FENCE:
		word = model_memory(ring[at + 1U], 4U);
		if (word == NULL) {
			model_fault(GHCI_FAULT_APERTURE, header);
			return -1;
		}
		memcpy(word, &ring[at + 2U], 4U);
		model_registers[GHCI_FENCE_LAST / 4U] = ring[at + 2U];
		if ((flags & GHCI_FENCE_IRQ) != 0U)
			model_irq_status |= GHCI_IRQ_FENCE;
		break;

	default:
		model_fault(GHCI_FAULT_OPCODE, header);
		return -1;
	}

	/* A fault of the blitter stops the parse too. */
	if (model_state == GHCI_ST_FAULTED)
		return -1;

	/* The packet was taken. */
	return 0;
}

/* Parses the ring up to the write pointer, unless the processor is paused or not running. */
static void
model_run(void)
{
	const uint32_t *ring;
	uint8_t *ring_bytes;
	uint32_t ring_dwords;
	uint32_t length;
	int error;

	/* A nested call (from the interrupt) does nothing. */
	if (model_running_now)
		return;
	model_running_now = 1;

	/* Parses packet after packet while running. */
	ring_dwords = (1U << model_registers[GHCI_RING_SIZE / 4U]) / 4U;
	ring_bytes = model_memory(model_registers[GHCI_RING_BASE / 4U], ring_dwords * 4U);
	while (!model_paused && model_state == GHCI_ST_RUNNING && model_rptr != model_wptr && ring_bytes != NULL) {
		ring = (const uint32_t *)ring_bytes;
		error = model_packet(ring, ring_dwords, model_rptr, &length);
		if (error != 0)
			break;
		model_rptr = (model_rptr + length) % ring_dwords;
	}

	/* The interrupt, when one is pending and enabled. */
	model_running_now = 0;
	model_interrupt();
}

/* Calls the driver's handler while a pending interrupt is enabled. */
static void
model_interrupt(void)
{
	unsigned rounds;

	/* The level stays while the handler leaves bits pending; a few rounds at most. */
	for (rounds = 0U; rounds < 4U; rounds++) {
		if ((model_irq_status & model_irq_enable) == 0U)
			return;
		if (model_device == NULL)
			return;
		(void)ghci_cp_interrupt(model_device);
	}
}

/* Writes the identification, the capabilities and the extended capability list of the model. */
static void
model_setup_window(
	int discrete)
{
	/* An idle processor with nothing in its window yet. */
	memset(model_registers, 0, sizeof(model_registers));
	memset(model_local, 0x5a, sizeof(model_local));
	memset(model_host, 0xa5, sizeof(model_host));
	model_host_next = 0U;
	model_reset();
	model_paused = 0;

	/* GHCI 1.0, Amaterrace's IDs, a processor, a blitter and 32-byte lines. */
	model_registers[GHCI_MAGIC / 4U] = GHCI_MAGIC_VALUE;
	model_registers[GHCI_VERSION / 4U] = 0x00010000U;
	model_registers[GHCI_VENDOR_ID / 4U] = GHCI_FOURCC('A', 'M', 'A', 'T');
	model_registers[GHCI_DEVICE_ID / 4U] = GHCI_FOURCC('A', 'T', 'G', '1');
	model_registers[GHCI_CAPS / 4U] = GHCI_CAPS_CP | GHCI_CAPS_BLT | (5U << 20);

	/* A discrete GPU lists its MEM capability; a unified one has no list. */
	if (!discrete)
		return;
	model_registers[GHCI_EXTCAP / 4U] = MODEL_EXTCAP;
	model_registers[MODEL_EXTCAP / 4U] = GHCI_XCAP_MEM | (1U << 8);
	model_registers[MODEL_EXTCAP / 4U + 1U] = MODEL_MEM_REGISTERS;
	model_registers[(MODEL_MEM_REGISTERS + GHCI_MEM_LOCAL_BASE) / 4U] = MODEL_LOCAL_BASE;
	model_registers[(MODEL_MEM_REGISTERS + GHCI_MEM_LOCAL_SIZE) / 4U] = MODEL_LOCAL_BYTES;
	model_registers[(MODEL_MEM_REGISTERS + GHCI_MEM_WINDOW_SIZE) / 4U] = MODEL_WINDOW_BYTES;
}

/* Builds a native command of a fill and a fence into stream; returns its dwords. */
static uint32_t
native_fill(
	uint32_t *stream,
	uint32_t base,
	uint32_t pixels,
	uint32_t color,
	uint32_t fence_address,
	uint32_t fence_value)
{
	stream[0] = GPU_OP_GHCI_NATIVE;
	stream[1] = 0U;
	stream[2] = 3U;
	stream[3] = GHCI_PKT_N(0U, GHCI_OP_SET_REG, 0U, 0U, 12U);
	stream[4] = GHCI_S_BLT_OP;
	stream[5] = GHCI_BLT_FILL;
	stream[6] = GHCI_S_BLT_DST_BASE;
	stream[7] = base;
	stream[8] = GHCI_S_BLT_DST_STRIDE;
	stream[9] = pixels * 4U;
	stream[10] = GHCI_S_BLT_DST_XY;
	stream[11] = 0U;
	stream[12] = GHCI_S_BLT_SIZE;
	stream[13] = (1U << 16) | pixels;
	stream[14] = GHCI_S_BLT_COLOR;
	stream[15] = color;
	stream[16] = GHCI_PKT_N(0U, GHCI_OP_BLIT, 0U, 0U, 0U);
	ghci_packet_fence(0U, fence_address, fence_value, 0, &stream[17]);
	return 20U;
}

/* Runs the discrete GPU: start, memory, transfers, the ring's wrap, faults, hangs, the native command, the stop. */
static void
test_discrete(void)
{
	static struct ghci_device device;
	struct gpu_resource_create storage_request;
	struct gpu_blob_create blob_request;
	struct gpu_info info;
	struct drv_gpu_mapping mapping;
	struct ghci_ticket ticket;
	struct ghci_ticket first;
	struct ghci_allocation *storage;
	struct ghci_allocation *blob;
	struct ghci_allocation *other;
	struct ghci_session *session;
	uint32_t packets[GHCI_BLIT_PACKET_DWORDS + 1U];
	uint32_t stream[32];
	uint32_t pattern[1024];
	uint32_t readback[1024];
	uint32_t word;
	uint32_t dwords;
	uint32_t resource_id;
	uint32_t wraps;
	uint32_t previous_wptr;
	uint64_t free_before;
	unsigned index;
	unsigned resets;
	int failures;
	int error;
	void *object;
	void *opaque;

	/* The model and the device's bus fields, as the PCI glue fills them. */
	model_setup_window(1);
	memset(&device, 0, sizeof(device));
	device.registers = (volatile uint8_t *)model_registers;
	device.register_bytes = sizeof(model_registers);
	device.window = model_local;
	device.window_physical = MODEL_WINDOW_PHYSICAL;
	device.window_bytes = MODEL_WINDOW_BYTES;
	model_device = &device;

	/* The start: identification, reset, memory, ring, registration. */
	error = ghci_device_start(&device);
	expect(error == 0, "discrete start");
	expect(strcmp(device.name, "ghci:AMAT:ATG1") == 0, "discrete name");
	expect(device.local_base == MODEL_LOCAL_BASE && device.local_size == MODEL_LOCAL_BYTES, "discrete local memory");
	expect(device.local_window == MODEL_WINDOW_BYTES, "discrete window");
	expect(model_state == GHCI_ST_RUNNING, "discrete processor runs");
	expect(model_registers[GHCI_APERTURE_BASE / 4U] == MODEL_LOCAL_BASE, "discrete aperture base is the local memory");
	expect(model_registers[GHCI_APERTURE_LIMIT / 4U] == MODEL_LOCAL_BASE + MODEL_LOCAL_BYTES, "discrete aperture limit is the local memory's end");
	expect(device.ring != NULL && device.ring->kind == GHCI_MEMORY_VISIBLE, "discrete ring in the window");
	expect(model_registers[GHCI_RING_BASE / 4U] == device.ring->gpu_address, "discrete ring base");
	expect(model_registers[GHCI_RING_SIZE / 4U] == GHCI_RING_SIZE_LOG2, "discrete ring size");
	expect(model_irq_enable == (GHCI_IRQ_FENCE | GHCI_IRQ_FAULT | GHCI_IRQ_WATCHDOG), "discrete interrupts enabled");
	expect(core_ops != NULL && core_private == &device, "discrete registered");

	/* The device's capabilities: storage, blobs, transfers, mappings and the test's native command. */
	core_ops->open(core_private, &opaque);
	session = opaque;
	error = core_ops->get_info(core_private, opaque, &info);
	expect(error == 0, "discrete info");
	expect(info.capabilities == (GPU_CAP_RESOURCE | GPU_CAP_BLOB | GPU_CAP_TRANSFER | GPU_CAP_MAPPING | GPU_CAP_COMMAND), "discrete capabilities");
	expect(strcmp(info.driver_name, "ghci:AMAT:ATG1") == 0, "discrete driver name");
	expect(session->tag == 1U, "discrete first session takes tag 1");

	/* Storage is in the window, cleared, and maps as device memory. */
	memset(&storage_request, 0, sizeof(storage_request));
	storage_request.bytes = 10000U;
	storage_request.usage = GPU_RESOURCE_USAGE_STORAGE;
	error = core_ops->resource_create(core_private, opaque, &storage_request, &object);
	storage = object;
	expect(error == 0 && storage->kind == GHCI_MEMORY_VISIBLE, "discrete storage in the window");
	expect(storage->bytes == 12288U && storage->requested == 10000U, "discrete storage in whole pages");
	failures = 0;
	for (index = 0U; index < storage->bytes; index++) {
		if (storage->view[index] != 0U)
			failures++;
	}
	expect(failures == 0, "discrete storage cleared by the blitter");
	error = core_ops->resource_map(core_private, opaque, object, &mapping);
	expect(error == 0 && mapping.attributes == DRV_GPU_MAPPING_DEVICE, "discrete storage maps as device memory");
	expect(mapping.physical == MODEL_WINDOW_PHYSICAL + (storage->gpu_address - MODEL_LOCAL_BASE), "discrete storage's physical place");

	/* A blob that is not mappable is the GPU's own memory past the window, named by its address. */
	memset(&blob_request, 0, sizeof(blob_request));
	blob_request.bytes = sizeof(pattern);
	error = core_ops->blob_create(core_private, opaque, &blob_request, &object, &resource_id);
	blob = object;
	expect(error == 0 && blob->kind == GHCI_MEMORY_HIDDEN, "discrete device blob past the window");
	expect(resource_id == blob->gpu_address && blob->view == NULL, "discrete blob named by its GPU address");
	error = core_ops->resource_map(core_private, opaque, object, &mapping);
	expect(error == EINVAL, "discrete hidden blob does not map");

	/* A write and a read of hidden memory go through the bounce buffer, by the blitter. */
	for (index = 0U; index < 1024U; index++)
		pattern[index] = 0x01020304U * (index + 1U);
	error = core_ops->resource_write(core_private, opaque, object, 0U, pattern, sizeof(pattern));
	expect(error == 0, "discrete write to hidden memory");
	expect(memcmp(&model_local[blob->gpu_address - MODEL_LOCAL_BASE], pattern, sizeof(pattern)) == 0, "discrete hidden memory holds the write");
	memset(readback, 0, sizeof(readback));
	error = core_ops->resource_read(core_private, opaque, object, 0U, readback, sizeof(readback));
	expect(error == 0 && memcmp(readback, pattern, sizeof(pattern)) == 0, "discrete read of hidden memory");
	error = core_ops->resource_read(core_private, opaque, object, 2U, readback, 8U);
	expect(error == EINVAL, "discrete unaligned read of hidden memory");
	error = core_ops->resource_read(core_private, opaque, object, 4090U, readback, 8U);
	expect(error == EINVAL, "discrete read past the blob");
	error = core_ops->blob_create(core_private, opaque, &blob_request, &object, &resource_id);
	expect(error == 0, "discrete second blob");
	other = object;
	blob_request.flags = GPU_BLOB_SHAREABLE;
	error = core_ops->blob_create(core_private, opaque, &blob_request, &object, &resource_id);
	expect(error == ENOTSUP, "discrete shareable blob refused");
	blob_request.flags = 0U;
	blob_request.blob_id = 5U;
	error = core_ops->blob_create(core_private, opaque, &blob_request, &object, &resource_id);
	expect(error == EINVAL, "discrete blob of a Vulkan memory refused");
	blob_request.blob_id = 0U;

	/* The native command: refused for a user, taken for root, and the fill lands. */
	core_superuser = 0;
	dwords = native_fill(stream, storage->gpu_address, 256U, 0xdeadbeefU, storage->gpu_address + 4096U, 77U);
	error = core_ops->command(core_private, opaque, stream, dwords * 4U);
	expect(error == EPERM, "discrete native command needs root");
	core_superuser = 1;
	error = core_ops->command(core_private, opaque, stream, dwords * 4U);
	expect(error == 0, "discrete native command taken");
	memcpy(&word, &storage->view[1020], 4U);
	expect(word == 0xdeadbeefU, "discrete native fill landed");
	memcpy(&word, &storage->view[4096], 4U);
	expect(word == 77U, "discrete native fence landed");
	expect(session->outstanding == 1, "discrete session has work in flight");
	dwords = native_fill(stream, blob->gpu_address, 1025U, 0U, storage->gpu_address, 1U);
	error = core_ops->command(core_private, opaque, stream, dwords * 4U);
	expect(error == EINVAL, "discrete native fill past its blob refused");

	/* The ring wraps many times: every submission's fence passes, no packet crosses the end. */
	wraps = 0U;
	failures = 0;
	model_packets_crossing = 0U;
	for (index = 0U; index < 5000U; index++) {
		/* Runs of 1 to 20 invalidations, so the padding falls in many places. */
		previous_wptr = device.wptr;
		dwords = 1U + index % 20U;
		for (word = 0U; word < dwords; word++)
			ghci_packet_invalidate(0U, 1U, &packets[word]);
		error = ghci_cp_submit(&device, NULL, packets, dwords, &ticket);
		if (error != 0)
			failures++;
		if (device.wptr < previous_wptr)
			wraps++;
		error = ghci_cp_wait(&device, &ticket, 100U);
		if (error != 0)
			failures++;
	}
	expect(failures == 0, "discrete ring stress submissions pass");
	expect(wraps >= 4U, "discrete ring wrapped four times");
	expect(model_packets_crossing == 0U, "discrete no packet crosses the ring's end");

	/* The fence wraps around 2^32. */
	device.fence_issued = 0xfffffff0U;
	device.completed = 0xfffffff0U;
	failures = 0;
	for (index = 0U; index < 40U; index++) {
		ghci_packet_invalidate(0U, 1U, &packets[0]);
		error = ghci_cp_submit(&device, NULL, packets, 1U, &ticket);
		if (error == 0)
			error = ghci_cp_wait(&device, &ticket, 100U);
		if (error != 0)
			failures++;
	}
	expect(failures == 0 && device.completed == 0x18U, "discrete fence wraps around 2^32");

	/* Deferred release: with the processor paused, a destroyed resource waits for the session's fence. */
	model_paused = 1;
	dwords = native_fill(stream, storage->gpu_address, 16U, 1U, storage->gpu_address + 4096U, 78U);
	error = core_ops->command(core_private, opaque, stream, dwords * 4U);
	expect(error == 0, "discrete paused native command");
	free_before = ghci_range_free_bytes(&device.hidden);
	core_ops->resource_destroy(core_private, opaque, other);
	expect(device.deferred != NULL, "discrete destroyed blob deferred");
	expect(ghci_range_free_bytes(&device.hidden) == free_before, "discrete deferred blob still in use");
	ghci_service_step(&device, GHCI_WORK_RECLAIM);
	expect(device.deferred != NULL, "discrete reclaim waits for the fence");
	model_paused = 0;
	model_run();
	ghci_service_step(&device, GHCI_WORK_RECLAIM);
	expect(device.deferred == NULL, "discrete reclaim after the fence");
	expect(ghci_range_free_bytes(&device.hidden) == free_before + 4096U, "discrete blob memory back");
	expect(core_capacity_notes != 0U, "discrete capacity announced");

	/* A fault: the processor stops, the service resets it, old tickets end with an error, the ring runs again. */
	resets = device.resets;
	packets[0] = GHCI_PKT_N(0U, GHCI_OP_DRAW, session->tag, 0U, 0U);
	error = ghci_cp_submit(&device, session, packets, 1U, &first);
	expect(error == 0 && model_state == GHCI_ST_FAULTED, "discrete fault stops the processor");
	expect((device.service_work & GHCI_WORK_RECOVER) != 0U, "discrete fault wakes the recovery");
	expect(GHCI_FAULT_KIND(device.fault_info) == GHCI_FAULT_OPCODE && GHCI_FAULT_JOB_TAG(device.fault_info) == session->tag, "discrete fault recorded with the session's tag");
	ghci_service_step(&device, GHCI_WORK_RECOVER);
	expect(device.resets == resets + 1U && model_state == GHCI_ST_RUNNING, "discrete reset after the fault");
	expect(device.wptr == 0U && model_rptr == 0U, "discrete ring starts again at 0");
	error = ghci_cp_wait(&device, &first, 100U);
	expect(error == EIO, "discrete faulted ticket ends with an error");
	expect(core_session_errors == 1U && session->outstanding == 0, "discrete session told of its loss");
	ghci_packet_invalidate(0U, 1U, &packets[0]);
	error = ghci_cp_submit(&device, NULL, packets, 1U, &ticket);
	if (error == 0)
		error = ghci_cp_wait(&device, &ticket, 100U);
	expect(error == 0, "discrete work runs after the reset");

	/* A hang: the processor is paused with work outstanding until the hang time passes. */
	model_paused = 1;
	error = ghci_cp_submit(&device, NULL, packets, 1U, &first);
	expect(error == 0, "discrete submission that will hang");
	ghci_service_step(&device, 0U);
	expect(device.resets == resets + 1U, "discrete no reset before the hang time");
	fake_ticks += kern_ms_to_ticks(GHCI_HANG_MS) + 1U;
	ghci_service_step(&device, 0U);
	expect(device.resets == resets + 2U, "discrete reset after the hang time");
	model_paused = 0;
	error = ghci_cp_wait(&device, &first, 100U);
	expect(error == EIO, "discrete hung ticket ends with an error");

	/* The close gives everything back; the stop halts and resets the processor with an empty aperture. */
	core_ops->close(core_private, opaque);
	expect(device.session_count == 0U, "discrete session closed");
	error = ghci_device_stop(&device);
	expect(error == 0, "discrete stop");
	expect(model_state == GHCI_ST_IDLE, "discrete processor idle after the stop");
	expect(model_registers[GHCI_APERTURE_LIMIT / 4U] == 0U, "discrete aperture empty after the stop");
	expect(device.visible.count == 0U && device.hidden.count == 0U, "discrete local memory all back");
	model_device = NULL;
}

/* Runs the unified GPU: everything in host RAM, the aperture open. */
static void
test_unified(void)
{
	static struct ghci_device device;
	struct gpu_resource_create storage_request;
	struct drv_gpu_mapping mapping;
	struct ghci_allocation *storage;
	struct ghci_ticket ticket;
	uint32_t packets[1];
	uint32_t stream[32];
	uint32_t dwords;
	uint32_t word;
	int error;
	void *object;
	void *opaque;

	/* The model without local memory, and the device without a window. */
	model_setup_window(0);
	memset(&device, 0, sizeof(device));
	device.registers = (volatile uint8_t *)model_registers;
	device.register_bytes = sizeof(model_registers);
	model_device = &device;

	/* The start puts the ring in host RAM and opens the aperture but its first line. */
	error = ghci_device_start(&device);
	expect(error == 0, "unified start");
	expect(device.local_size == 0U && device.ring->kind == GHCI_MEMORY_HOST, "unified ring in host RAM");
	expect(model_registers[GHCI_APERTURE_BASE / 4U] == 32U, "unified aperture from the second line");
	expect(model_registers[GHCI_APERTURE_LIMIT / 4U] == 0xffffffe0U, "unified aperture to the last line");

	/* Storage is host RAM, maps as RAM, and the GPU writes into it. */
	core_ops->open(core_private, &opaque);
	memset(&storage_request, 0, sizeof(storage_request));
	storage_request.bytes = 8192U;
	storage_request.usage = GPU_RESOURCE_USAGE_STORAGE;
	error = core_ops->resource_create(core_private, opaque, &storage_request, &object);
	storage = object;
	expect(error == 0 && storage->kind == GHCI_MEMORY_HOST, "unified storage in host RAM");
	expect(storage->view[0] == 0U && storage->view[8191] == 0U, "unified storage cleared");
	error = core_ops->resource_map(core_private, opaque, object, &mapping);
	expect(error == 0 && mapping.attributes == 0U && mapping.physical == storage->gpu_address, "unified storage maps as RAM");
	core_superuser = 1;
	dwords = native_fill(stream, storage->gpu_address, 64U, 0x11223344U, storage->gpu_address + 4096U, 9U);
	error = core_ops->command(core_private, opaque, stream, dwords * 4U);
	memcpy(&word, &storage->view[252], 4U);
	expect(error == 0 && word == 0x11223344U, "unified native fill of host RAM");
	ghci_packet_invalidate(0U, 1U, &packets[0]);
	error = ghci_cp_submit(&device, NULL, packets, 1U, &ticket);
	if (error == 0)
		error = ghci_cp_wait(&device, &ticket, 100U);
	expect(error == 0, "unified fence in host RAM");

	/* The close and the stop. */
	core_ops->close(core_private, opaque);
	error = ghci_device_stop(&device);
	expect(error == 0, "unified stop");
	model_device = NULL;
}
