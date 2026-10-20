/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The PowerVR driver's later stage: loading and starting the firmware.
 *
 * The stage runs on a thread of its own once the early stage has kept the
 * core.  It waits for the root file system, reads the firmware file and
 * keeps it, lays the firmware out in memory the core's MIPS processor
 * maps, builds the records and command buffers the firmware expects,
 * takes the core out of reset, waits for the firmware to say it started,
 * and sends it one health check.  The outcome is one line of the boot
 * log.
 *
 * On any failure the stage stops the firmware, puts the core back in
 * reset, returns every block of memory, and powers the core down.
 */

#include <stddef.h>
#include <stdint.h>

#include <kern/clock.h>
#include <kern/device-io.h>
#include <kern/irq.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/sched.h>
#include <kern/sysctl.h>
#include <kern/thread.h>
#include <uapi/errno.h>

#include "drivers/gpu/powervr/powervr-private.h"
#include "drivers/gpu/powervr/firmware-file.h"
#include "drivers/gpu/powervr/firmware-image.h"
#include "drivers/gpu/powervr/interface.h"
#include "drivers/gpu/powervr/mips-table.h"

/*
 * Where the firmware file is, and the most bytes the driver accepts.
 *
 * Values referenced:
 * Imagination linux-firmware branch powervr 8a58f818
 * powervr/rogue_36.50.54.182_v1.fw (the file's name; it is 128 KiB).
 */
#define FIRMWARE_PATH		"/lib/firmware/powervr/rogue_36.50.54.182_v1.fw"
#define FIRMWARE_LIMIT		0x400000U

/*
 * How long the firmware has to say it started, and how long the stop
 * waits on each unit going idle.
 *
 * Values referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_fw.c
 * (FW_BOOT_TIMEOUT_USEC 5 s); pvr_fw_startstop.c (POLL_TIMEOUT_USEC 1 s).
 */
#define BOOT_WAIT_MS		5000U
#define IDLE_WAIT_MS		1000U

/*
 * The pause around taking the firmware processor out of reset, which
 * needs at least 16 core cycles on each side.
 *
 * Values referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_fw_startstop.c
 * (pvr_fw_start: 3 us on each side).
 */
#define RELEASE_PAUSE_US	3U

/*
 * How far the later stage got, so a failure undoes exactly what was done.
 *
 * Each stage includes those before it.
 */
enum late_stage {
	LATE_NOTHING = 0,
	LATE_FILE,
	LATE_MEMORY,
	LATE_INTERRUPT,
	LATE_STARTED
};

static void late_thread(void *argument);
static enum powervr_stage_reason firmware_reason(const char *step, int error);
static int bring_up(struct powervr_core *core, enum late_stage *reached, const char **step);
static int check_processor(const struct powervr_fw_image *image, unsigned *bus_bits);
static int lay_out(struct powervr_core *core, unsigned bus_bits);
static int make_program(struct powervr_core *core);
static int load_program(struct powervr_core *core);
static int make_command_buffers(struct powervr_core *core);
static int make_records(struct powervr_core *core);
static void fill_records(struct powervr_core *core);
static void fill_init_records(struct powervr_core *core);
static int install_interrupt(struct powervr_core *core);
static void remove_interrupt(struct powervr_core *core);
static void interrupt_handler(int irq, kern_irq_ack_t acknowledge, void *argument);
static int start_processor(struct powervr_core *core);
static void fence_reset(struct powervr_core *core, int second_reset);
static void init_cache(struct powervr_core *core);
static void init_wrapper(struct powervr_core *core);
static int wait_started(struct powervr_core *core);
static int health_check(struct powervr_core *core);
static void stop_processor(struct powervr_core *core);
static int wait_register(struct powervr_core *core, uint32_t offset, uint64_t mask, uint64_t value, int wide);
static void tear_down(struct powervr_core *core, enum late_stage reached);
static void free_blocks(struct powervr_core *core);
static void free_file(struct powervr_core *core);
static uint64_t ms_deadline(unsigned milliseconds);
static void firmware_dump(struct powervr_core *core, uint32_t slot);
static void step_start(const char *name);
static void step_done(const char *name, int error);
static void watch_thread(void *argument);

/* How often a step that has not ended is reported again. */
#define WATCH_REPORT_MS		5000U

/*
 * The trace the firmware keeps, for the dump of a command it did not
 * execute: its main group on, the last words printed.
 *
 * Values referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_rogue_fwif.h
 * (ROGUE_FWIF_LOG_TYPE_TRACE 0x1, ROGUE_FWIF_LOG_TYPE_GROUP_MAIN 0x2,
 * GROUP_MTS 0x4, GROUP_POW 0x200; struct rogue_fwif_tracebuf_space:
 * trace_pointer 0, the assert buffer at 16 with path 200 bytes, info 200
 * bytes and the line; struct rogue_fwif_osdata: interrupt_count 560,
 * kccb_cmds_executed 568; struct rogue_fwif_sysdata: pow_state 8;
 * struct rogue_fwif_sysinit: firmware_started_timestamp 360;
 * struct rogue_fwif_osinit: the hardware and firmware identities at 40
 * and 56, each its layout and BVNC) and pvr_rogue_fwif_check.h.
 */
#define DUMP_TRACE_LOG_TYPE		0x207U
#define DUMP_TRACE_POINTER		8U
#define DUMP_ASSERT_PATH		24U
#define DUMP_ASSERT_INFO		224U
#define DUMP_ASSERT_LINE		424U
#define DUMP_ASSERT_TEXT		200U
#define DUMP_TRACE_LAST_WORDS		48U
#define DUMP_OS_DATA_INTERRUPTS		560U
#define DUMP_SYSTEM_DATA_POWER		8U
#define DUMP_SYSTEM_INIT_STARTED_TIME	360U

/*
 * The step the later stage is in, for its log lines and for the thread
 * that reports a step taking long: the name (NULL between steps), the
 * tick it began, and whether the stage has ended.
 */
static const char *step_name;
static uint64_t step_began;
static int stage_ended;

/*
 * Starts the later stage on a thread of its own: it waits for the root
 * file system, loads the firmware and starts it.
 */
int
powervr_late_start(
	struct powervr_core *core)
{
	struct thread *thread;
	int error;

	/* Creates the thread; nobody joins it, and its end releases it. */
	error = kthread_create(late_thread, core, SCHED_PRIORITY_DEFAULT, &thread);
	if (error != 0)
		return error;

	/* Makes it runnable: kthread_create leaves a new thread stopped. */
	thread_start(thread);

	/* Succeeded: the stage runs on its own from here. */
	return 0;
}

/* Runs the later stage and reports its outcome on one line. */
static void
late_thread(
	void *argument)
{
	struct powervr_core *core;
	struct powervr_fw_image *image;
	struct thread *watcher;
	enum late_stage reached;
	const char *step;
	unsigned replies;
	int error;

	/* The core the early stage kept. */
	core = argument;
	reached = LATE_NOTHING;
	step = "file";

	/* The thread that reports a step taking long; the stage goes on without it. */
	__atomic_store_n(&stage_ended, 0, __ATOMIC_RELEASE);
	error = kthread_create(watch_thread, NULL, SCHED_PRIORITY_DEFAULT, &watcher);
	if (error != 0) {
		kern_logf("powervr: firmware: no thread to report long steps\n");
	} else {
		thread_start(watcher);
	}

	/* Reads the file once the root is live, and keeps it for later resets. */
	step_start("file");
	error = powervr_firmware_read(FIRMWARE_PATH, FIRMWARE_LIMIT, &core->firmware.file, &core->firmware.file_size);
	step_done("file", error);
	if (error == 0) {
		reached = LATE_FILE;

		/* Brings the firmware up from the kept file. */
		error = bring_up(core, &reached, &step);
	}

	/* On failure undoes what was done and powers the core down. */
	if (error != 0) {
		tear_down(core, reached);
		kern_logf("powervr: firmware %s failed (%d); powered down\n", step, error);
		powervr_stage_fail(POWERVR_STAGE_FIRMWARE, firmware_reason(step, error));
		__atomic_store_n(&stage_ended, 1, __ATOMIC_RELEASE);
		kern_gpu_attach_end();
		return;
	}

	/* Takes any replies the firmware queued meanwhile. */
	replies = powervr_notices_take(core, NULL, NULL);

	/* Reports the running firmware on one line. */
	image = &core->firmware.image;
	kern_logf("powervr: firmware %u.%u build %u running, health check ok, %u interrupts, %u replies\n",
		  (unsigned)image->version_major,
		  (unsigned)image->version_minor,
		  (unsigned)image->version_build,
		  (unsigned)core->firmware.interrupts,
		  replies);
	powervr_stage_ok(POWERVR_STAGE_FIRMWARE);

	/* Publishes the render node over the running firmware; on failure the core goes down the same way. */
	step_start("render node");
	error = powervr_render_start(core);
	step_done("render node", error);
	__atomic_store_n(&stage_ended, 1, __ATOMIC_RELEASE);
	if (error != 0) {
		tear_down(core, LATE_STARTED);
		powervr_render_abandon(core);
		kern_logf("powervr: render node failed (%d); powered down\n", error);
		powervr_stage_fail(POWERVR_STAGE_RENDER_NODE, powervr_stage_reason_of(error));
		kern_gpu_attach_end();
		return;
	}

	/* The render node is published: the GPU no longer counts as attaching. */
	powervr_stage_ok(POWERVR_STAGE_RENDER_NODE);
	kern_gpu_attach_end();
}

/*
 * The stage line's reason for a firmware that did not come up at step:
 * an image that does not parse is "bad image", otherwise the error's own.
 */
static enum powervr_stage_reason
firmware_reason(
	const char *step,
	int error)
{
	int image_step;

	/* The image step fails only on what the file holds. */
	image_step = kern_strcmp(step, "image") == 0;
	if (image_step && error != ENOENT && error != ETIMEDOUT)
		return POWERVR_REASON_BAD_IMAGE;

	/* Otherwise the error says why. */
	return powervr_stage_reason_of(error);
}

/*
 * Starts the firmware again from the kept file after a reset: stops the
 * processor, returns the firmware's memory, powers the core off and on,
 * and brings the firmware up as at boot.  On failure the core is left
 * powered down with nothing kept.
 */
int
powervr_firmware_restart(
	struct powervr_core *core)
{
	enum late_stage reached;
	const char *step;
	int error;

	/* Stops the processor and the interrupt, and returns the firmware's memory. */
	stop_processor(core);
	remove_interrupt(core);
	free_blocks(core);

	/* Powers the core off and on, its domain, clocks and resets with it. */
	powervr_power_down(core);
	error = powervr_power_up(core);
	if (error != 0) {
		free_file(core);
		return error;
	}

	/* Brings the firmware up from the kept file, without the root file system. */
	reached = LATE_FILE;
	error = bring_up(core, &reached, &step);
	if (error != 0) {
		tear_down(core, reached);
		return error;
	}

	/* Takes any replies the restarted firmware queued. */
	(void)powervr_notices_take(core, NULL, NULL);

	/* Succeeded: the firmware runs and answers again. */
	return 0;
}

/*
 * Brings the firmware up from the kept file: checks it, lays it out,
 * starts it and checks it answers.
 *
 * Reached and step report how far it got and which step failed.
 */
static int
bring_up(
	struct powervr_core *core,
	enum late_stage *reached,
	const char **step)
{
	struct powervr_firmware *firmware;
	uint64_t bvnc;
	unsigned bus_bits;
	int error;

	/* Checks the file is for this core and describes it. */
	firmware = &core->firmware;
	*step = "image";
	bvnc = powervr_bvnc_pack(core->branch, core->version, core->units, core->config);
	step_start("image");
	error = powervr_fw_image_parse(firmware->file, firmware->file_size, bvnc, &firmware->image);
	step_done("image", error);
	if (error != 0)
		return error;

	/* Checks the firmware runs on a MIPS processor on a bus wider than 32 bits. */
	step_start("processor");
	error = check_processor(&firmware->image, &bus_bits);
	step_done("processor", error);
	if (error != 0)
		return error;

	/* Lays out the program, the command buffers and the records. */
	*step = "memory";
	*reached = LATE_MEMORY;
	step_start("memory");
	error = lay_out(core, bus_bits);
	step_done("memory", error);
	if (error != 0)
		return error;

	/* Installs the handler of the firmware's interrupt. */
	*step = "interrupt";
	step_start("interrupt");
	error = install_interrupt(core);
	step_done("interrupt", error);
	if (error != 0)
		return error;
	*reached = LATE_INTERRUPT;

	/* Takes the core out of reset; the processor runs from here. */
	*step = "start";
	step_start("start");
	error = start_processor(core);
	step_done("start", error);
	*reached = LATE_STARTED;
	if (error != 0)
		return error;

	/* Waits for the firmware to say it started. */
	*step = "boot";
	step_start("boot");
	error = wait_started(core);
	step_done("boot", error);
	if (error != 0)
		return error;

	/* Sends one health check and waits for the firmware to execute it. */
	*step = "health check";
	step_start("health check");
	error = health_check(core);
	step_done("health check", error);
	if (error != 0)
		return error;

	/* Succeeded: the firmware runs and answers. */
	return 0;
}

/* Checks the firmware is for a MIPS processor on a wide bus, and reports the bus width. */
static int
check_processor(
	const struct powervr_fw_image *image,
	unsigned *bus_bits)
{
	uint64_t width;
	int mips;
	int error;

	/* Refuses firmware for another processor. */
	mips = powervr_fw_image_has_feature(image, POWERVR_FEATURE_MIPS);
	if (!mips)
		return ENOEXEC;

	/*
	 * Refuses a bus of 32 bits or less, on which the MIPS processor's
	 * table takes another form the driver does not build.
	 *
	 * Procedure referenced:
	 * Linux v7.3 drivers/gpu/drm/imagination/pvr_fw_mips.c
	 * (pvr_mips_wrapper_init: MIPS firmware only on a bus wider than 32
	 * bits).
	 */
	error = powervr_fw_image_feature_value(image, POWERVR_FEATURE_PHYS_BUS_WIDTH, &width);
	if (error != 0)
		return ENOEXEC;
	if (width <= 32U || width > 40U)
		return ENOEXEC;

	/* Succeeded: the bus width the table entries use. */
	*bus_bits = (unsigned)width;
	return 0;
}

/* Lays out the program, the command buffers and the records in the firmware's heap. */
static int
lay_out(
	struct powervr_core *core,
	unsigned bus_bits)
{
	int error;

	/* Makes the heap's empty page table. */
	error = powervr_heap_init(&core->firmware.heap, bus_bits, core->description.dma_limit);
	if (error != 0)
		return error;

	/* Places the code and the data where the firmware expects them, and loads the program. */
	error = make_program(core);
	if (error != 0)
		return error;

	/* Makes the kernel's and the firmware's command buffers. */
	error = make_command_buffers(core);
	if (error != 0)
		return error;

	/* Makes the records the firmware reads at boot and keeps up to date. */
	error = make_records(core);
	if (error != 0)
		return error;

	/* Fills the records, which name each other and the command buffers. */
	fill_records(core);

	/* Succeeded: the firmware's memory is ready. */
	return 0;
}

/*
 * Places the code at the bottom of the heap and the data where the
 * private data section names, then loads the program into them.
 *
 * Procedure referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_fw.c (pvr_fw_process: the
 * code at offset 0 of the heap, the data at the private data section's
 * base for a processor with a fixed data address, both uncached to the
 * processor).
 */
static int
make_program(
	struct powervr_core *core)
{
	struct powervr_firmware *firmware;
	const struct powervr_fw_section *private_data;
	const struct powervr_fw_section *boot_code;
	const struct powervr_fw_section *exceptions;
	uint32_t data_offset;
	int error;

	/* Refuses a layout without code or data. */
	firmware = &core->firmware;
	if (firmware->image.code_size == 0U || firmware->image.data_size == 0U)
		return EINVAL;

	/* Refuses a layout that lacks a section the boot needs. */
	private_data = powervr_fw_image_section(&firmware->image, POWERVR_SECTION_MIPS_PRIVATE);
	boot_code = powervr_fw_image_section(&firmware->image, POWERVR_SECTION_MIPS_BOOT_CODE);
	exceptions = powervr_fw_image_section(&firmware->image, POWERVR_SECTION_MIPS_EXCEPTIONS);
	if (private_data == NULL || boot_code == NULL || exceptions == NULL)
		return EINVAL;

	/* Refuses boot code or exception vectors outside the code. */
	if (boot_code->memory != POWERVR_MEMORY_CODE || exceptions->memory != POWERVR_MEMORY_CODE)
		return EINVAL;

	/* The data's place is its private section's base within the heap. */
	data_offset = private_data->base & (POWERVR_HEAP_BYTES - 1U);

	/* The code goes first, at the bottom of the heap. */
	error = powervr_object_create(&firmware->heap, firmware->image.code_size, 0, POWERVR_MIPS_UNCACHED, &firmware->code);
	if (error != 0)
		return error;

	/* The data goes at its fixed place. */
	error = powervr_object_create(&firmware->heap, firmware->image.data_size, data_offset, POWERVR_MIPS_UNCACHED, &firmware->data);
	if (error != 0)
		return error;

	/* Loads the program and the boot loader's parameters. */
	error = load_program(core);
	if (error != 0)
		return error;

	/* Succeeded: the program is in place. */
	return 0;
}

/*
 * Loads the program from the kept file into the code and data blocks,
 * then gives the boot loader its parameters.
 *
 * The program is placed in kernel memory first, because the sections are
 * not in file order, and copied to the blocks through their views.
 */
static int
load_program(
	struct powervr_core *core)
{
	struct powervr_firmware *firmware;
	const struct powervr_fw_section *boot_data;
	const struct powervr_fw_section *stack;
	uint64_t table_pages[POWERVR_MIPS_TABLE_PAGES];
	uint8_t *code;
	uint8_t *data;
	uint32_t page;
	int error;

	/* Finds the boot data page and the stack, both of which live in the data. */
	firmware = &core->firmware;
	boot_data = powervr_fw_image_section(&firmware->image, POWERVR_SECTION_MIPS_BOOT_DATA);
	stack = powervr_fw_image_section(&firmware->image, POWERVR_SECTION_MIPS_STACK);
	if (boot_data == NULL || stack == NULL)
		return EINVAL;
	if (boot_data->memory != POWERVR_MEMORY_DATA || stack->memory != POWERVR_MEMORY_DATA)
		return EINVAL;

	/* Takes a cleared buffer for the code. */
	code = kern_malloc(firmware->image.code_size);
	if (code == NULL)
		return ENOMEM;
	kern_memset(code, 0, firmware->image.code_size);

	/* Takes a cleared buffer for the data. */
	data = kern_malloc(firmware->image.data_size);
	if (data == NULL) {
		kern_free(code);
		return ENOMEM;
	}
	kern_memset(data, 0, firmware->image.data_size);

	/* Places the program's segments, then copies both buffers to the blocks. */
	error = powervr_fw_image_load(&firmware->image, code, data);
	if (error == 0) {
		powervr_object_fill(&firmware->code, 0, code, firmware->image.code_size);
		powervr_object_fill(&firmware->data, 0, data, firmware->image.data_size);
	}
	kern_free(data);
	kern_free(code);
	if (error != 0)
		return error;

	/* Names the stack, the core's registers and the page table to the boot loader. */
	for (page = 0; page < POWERVR_MIPS_TABLE_PAGES; page++)
		table_pages[page] = powervr_object_physical(&firmware->heap.table, page * POWERVR_MIPS_PAGE_BYTES);
	powervr_mips_boot_parameters(firmware->data.view + boot_data->alloc_offset,
				     powervr_object_physical(&firmware->data, stack->alloc_offset),
				     core->description.registers,
				     table_pages);

	/* Succeeded: the program and its parameters are in place. */
	return 0;
}

/*
 * Makes the kernel's command buffer with its return slots, and the
 * firmware's command buffer, each with its control block.
 *
 * Procedure referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_ccb.c (pvr_ccb_init: both
 * offsets zero, the wrap mask one less than the power-of-two slot count,
 * the command size; all of it uncached).
 */
static int
make_command_buffers(
	struct powervr_core *core)
{
	struct powervr_firmware *firmware;
	struct powervr_heap *heap;
	int error;

	/* The kernel's buffer's control block. */
	firmware = &core->firmware;
	heap = &firmware->heap;
	error = powervr_object_create(heap, POWERVR_CCB_CONTROL_BYTES, POWERVR_HEAP_ANYWHERE, POWERVR_MIPS_UNCACHED, &firmware->kccb_control);
	if (error != 0)
		return error;

	/* The kernel's buffer. */
	error = powervr_object_create(heap, POWERVR_KERNEL_CCB_SLOTS * POWERVR_COMMAND_BYTES, POWERVR_HEAP_ANYWHERE, POWERVR_MIPS_UNCACHED, &firmware->kccb);
	if (error != 0)
		return error;

	/* One return slot per command of the kernel's buffer. */
	error = powervr_object_create(heap, POWERVR_KERNEL_CCB_SLOTS * 4U, POWERVR_HEAP_ANYWHERE, POWERVR_MIPS_UNCACHED, &firmware->kccb_returns);
	if (error != 0)
		return error;

	/* The firmware's buffer's control block. */
	error = powervr_object_create(heap, POWERVR_CCB_CONTROL_BYTES, POWERVR_HEAP_ANYWHERE, POWERVR_MIPS_UNCACHED, &firmware->fwccb_control);
	if (error != 0)
		return error;

	/* The firmware's buffer. */
	error = powervr_object_create(heap, POWERVR_FIRMWARE_CCB_SLOTS * POWERVR_COMMAND_BYTES, POWERVR_HEAP_ANYWHERE, POWERVR_MIPS_UNCACHED, &firmware->fwccb);
	if (error != 0)
		return error;

	/* Fills the kernel's control block: empty, its slot count and command size. */
	powervr_object_put32(&firmware->kccb_control, POWERVR_CCB_WRITE, 0);
	powervr_object_put32(&firmware->kccb_control, POWERVR_CCB_READ, 0);
	powervr_object_put32(&firmware->kccb_control, POWERVR_CCB_WRAP_MASK, POWERVR_KERNEL_CCB_SLOTS - 1U);
	powervr_object_put32(&firmware->kccb_control, POWERVR_CCB_COMMAND_BYTES, POWERVR_COMMAND_BYTES);

	/* Fills the firmware's control block the same way. */
	powervr_object_put32(&firmware->fwccb_control, POWERVR_CCB_WRITE, 0);
	powervr_object_put32(&firmware->fwccb_control, POWERVR_CCB_READ, 0);
	powervr_object_put32(&firmware->fwccb_control, POWERVR_CCB_WRAP_MASK, POWERVR_FIRMWARE_CCB_SLOTS - 1U);
	powervr_object_put32(&firmware->fwccb_control, POWERVR_CCB_COMMAND_BYTES, POWERVR_COMMAND_BYTES);
	firmware->fwccb_read = 0;

	/* Succeeded: both buffers are empty. */
	return 0;
}

/*
 * Makes the records the firmware reads at boot and keeps up to date.
 *
 * Procedure referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_fw.c
 * (pvr_fw_create_structures and pvr_fw_create_fwif_connection_ctl: which
 * records exist, every one uncached; the connection control, the per-OS
 * and the system initialisation records at their fixed places) and
 * pvr_fw_trace.c (pvr_fw_trace_init: one trace buffer for the one
 * thread).
 */
static int
make_records(
	struct powervr_core *core)
{
	struct powervr_firmware *firmware;
	struct powervr_heap *heap;
	int error;

	/* The connection control, at its fixed place. */
	firmware = &core->firmware;
	heap = &firmware->heap;
	error = powervr_object_create(heap, POWERVR_CONNECTION_BYTES, POWERVR_HEAP_CONNECTION_OFFSET, POWERVR_MIPS_UNCACHED, &firmware->connection);
	if (error != 0)
		return error;

	/* The power synchronisation word the firmware reports power changes in. */
	error = powervr_object_create(heap, 4U, POWERVR_HEAP_ANYWHERE, POWERVR_MIPS_UNCACHED, &firmware->power_sync);
	if (error != 0)
		return error;

	/* The recovery information buffer. */
	error = powervr_object_create(heap, POWERVR_HWR_INFO_BYTES, POWERVR_HEAP_ANYWHERE, POWERVR_MIPS_UNCACHED, &firmware->hwr_info);
	if (error != 0)
		return error;

	/* The word an MMU cache flush is synchronised on. */
	error = powervr_object_create(heap, 4U, POWERVR_HEAP_ANYWHERE, POWERVR_MIPS_UNCACHED, &firmware->mmu_sync);
	if (error != 0)
		return error;

	/* The system data the firmware keeps. */
	error = powervr_object_create(heap, POWERVR_SYSTEM_DATA_BYTES, POWERVR_HEAP_ANYWHERE, POWERVR_MIPS_UNCACHED, &firmware->system_data);
	if (error != 0)
		return error;

	/* The page the core reads in place of a faulting one. */
	error = powervr_object_create(heap, POWERVR_MIPS_PAGE_BYTES, POWERVR_HEAP_ANYWHERE, POWERVR_MIPS_UNCACHED, &firmware->fault_page);
	if (error != 0)
		return error;

	/* The utilisation buffer. */
	error = powervr_object_create(heap, POWERVR_UTILISATION_BYTES, POWERVR_HEAP_ANYWHERE, POWERVR_MIPS_UNCACHED, &firmware->utilisation);
	if (error != 0)
		return error;

	/* The runtime configuration. */
	error = powervr_object_create(heap, POWERVR_RUNTIME_BYTES, POWERVR_HEAP_ANYWHERE, POWERVR_MIPS_UNCACHED, &firmware->runtime);
	if (error != 0)
		return error;

	/* The one thread's trace buffer. */
	error = powervr_object_create(heap, POWERVR_TRACE_WORDS * 4U, POWERVR_HEAP_ANYWHERE, POWERVR_MIPS_UNCACHED, &firmware->trace_buffer);
	if (error != 0)
		return error;

	/* The trace control block. */
	error = powervr_object_create(heap, POWERVR_TRACE_CONTROL_BYTES, POWERVR_HEAP_ANYWHERE, POWERVR_MIPS_UNCACHED, &firmware->trace_control);
	if (error != 0)
		return error;

	/* The per-OS data the firmware keeps. */
	error = powervr_object_create(heap, POWERVR_OS_DATA_BYTES, POWERVR_HEAP_ANYWHERE, POWERVR_MIPS_UNCACHED, &firmware->os_data);
	if (error != 0)
		return error;

	/* The per-OS initialisation record, at its fixed place. */
	error = powervr_object_create(heap, POWERVR_OS_INIT_BYTES, POWERVR_HEAP_OS_INIT_OFFSET, POWERVR_MIPS_UNCACHED, &firmware->os_init);
	if (error != 0)
		return error;

	/* The system initialisation record, at its fixed place. */
	error = powervr_object_create(heap, POWERVR_SYSTEM_INIT_BYTES, POWERVR_HEAP_SYSTEM_INIT_OFFSET, POWERVR_MIPS_UNCACHED, &firmware->system_init);
	if (error != 0)
		return error;

	/* Succeeded: every record exists, cleared. */
	return 0;
}

/*
 * Fills the records, which name each other and the command buffers.
 *
 * Procedure referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_fw.c (fw_osdata_init,
 * fw_fault_page_init, fw_sysdata_init, fw_runtime_cfg_init and
 * fw_gpu_util_fwcb_init: the values each record starts with) and
 * pvr_fw_trace.c (tracebuf_ctrl_init: tracing off, the buffer's address
 * and size).
 */
static void
fill_records(
	struct powervr_core *core)
{
	struct powervr_firmware *firmware;
	uint64_t clusters;
	uint64_t slc_kilobytes;
	uint32_t word;
	int error;

	/* The per-OS data names the power synchronisation word. */
	firmware = &core->firmware;
	powervr_object_put32(&firmware->os_data, POWERVR_OS_DATA_POWER_SYNC, powervr_object_address(&firmware->power_sync, 0));

	/* The fault page reads as a recognisable pattern. */
	for (word = 0; word < POWERVR_MIPS_PAGE_BYTES / 4U; word++)
		powervr_object_put32(&firmware->fault_page, word * 4U, POWERVR_FAULT_PAGE_FILL);

	/* Overlapping data masters needs a big enough system level cache. */
	error = powervr_fw_image_feature_value(&firmware->image, POWERVR_FEATURE_SLC_KILOBYTES, &slc_kilobytes);
	if (error != 0 || slc_kilobytes < POWERVR_DM_OVERLAP_MIN_KB)
		powervr_object_put32(&firmware->system_data, POWERVR_SYSTEM_DATA_FLAGS, POWERVR_FLAG_NO_DM_OVERLAP);

	/*
	 * The runtime configuration: the core clock's real rate, every
	 * cluster on, no active power management and no watchdog.
	 */
	clusters = 1;
	(void)powervr_fw_image_feature_value(&firmware->image, POWERVR_FEATURE_NUM_CLUSTERS, &clusters);
	powervr_object_put32(&firmware->runtime, POWERVR_RUNTIME_PERSISTENT, 1);
	powervr_object_put32(&firmware->runtime, POWERVR_RUNTIME_CORE_CLOCK, (uint32_t)core->core_rate);
	powervr_object_put32(&firmware->runtime, POWERVR_RUNTIME_CLUSTERS, (uint32_t)clusters);
	powervr_object_put32(&firmware->runtime, POWERVR_RUNTIME_WATCHDOG, 0);

	/* The trace: its main, scheduler and power groups on, the one thread's buffer named and sized. */
	powervr_object_put32(&firmware->trace_control, POWERVR_TRACE_LOG_TYPE, DUMP_TRACE_LOG_TYPE);
	powervr_object_put32(&firmware->trace_control, POWERVR_TRACE_BUFFER_ADDRESS, powervr_object_address(&firmware->trace_buffer, 0));
	powervr_object_put32(&firmware->trace_control, POWERVR_TRACE_WORDS_FIELD, POWERVR_TRACE_WORDS);

	/* The two initialisation records name everything else. */
	fill_init_records(core);
}

/*
 * Fills the per-OS and the system initialisation records.
 *
 * Procedure referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_fw.c (fw_osinit_init and
 * fw_sysinit_init: no work estimation buffer, no counter dump, no
 * alignment checks, no performance tracing, the started flag clear and
 * the marker 1).
 */
static void
fill_init_records(
	struct powervr_core *core)
{
	struct powervr_firmware *firmware;
	struct powervr_object *os_init;
	struct powervr_object *system_init;

	/* The per-OS record names the command buffers, the recovery buffer and the per-OS data. */
	firmware = &core->firmware;
	os_init = &firmware->os_init;
	powervr_object_put32(os_init, POWERVR_OS_INIT_KCCB_CONTROL, powervr_object_address(&firmware->kccb_control, 0));
	powervr_object_put32(os_init, POWERVR_OS_INIT_KCCB, powervr_object_address(&firmware->kccb, 0));
	powervr_object_put32(os_init, POWERVR_OS_INIT_KCCB_RETURNS, powervr_object_address(&firmware->kccb_returns, 0));
	powervr_object_put32(os_init, POWERVR_OS_INIT_FWCCB_CONTROL, powervr_object_address(&firmware->fwccb_control, 0));
	powervr_object_put32(os_init, POWERVR_OS_INIT_FWCCB, powervr_object_address(&firmware->fwccb, 0));
	powervr_object_put32(os_init, POWERVR_OS_INIT_HWR_INFO, powervr_object_address(&firmware->hwr_info, 0));
	powervr_object_put32(os_init, POWERVR_OS_INIT_OS_DATA, powervr_object_address(&firmware->os_data, 0));

	/* The identity checks carry their layout version; the firmware fills the identities. */
	powervr_object_put32(os_init, POWERVR_OS_INIT_HW_IDENTITY, POWERVR_IDENTITY_LAYOUT);
	powervr_object_put32(os_init, POWERVR_OS_INIT_FW_IDENTITY, POWERVR_IDENTITY_LAYOUT);

	/* The system record names the fault page by its physical address and the execution bases. */
	system_init = &firmware->system_init;
	powervr_object_put64(system_init, POWERVR_SYSTEM_INIT_FAULT_PAGE, powervr_object_physical(&firmware->fault_page, 0));
	powervr_object_put64(system_init, POWERVR_SYSTEM_INIT_PDS_BASE, POWERVR_PDS_HEAP_BASE);
	powervr_object_put64(system_init, POWERVR_SYSTEM_INIT_USC_BASE, POWERVR_USC_HEAP_BASE);

	/* It names the other records by their firmware addresses. */
	powervr_object_put32(system_init, POWERVR_SYSTEM_INIT_RUNTIME, powervr_object_address(&firmware->runtime, 0));
	powervr_object_put32(system_init, POWERVR_SYSTEM_INIT_TRACE, powervr_object_address(&firmware->trace_control, 0));
	powervr_object_put32(system_init, POWERVR_SYSTEM_INIT_SYSTEM_DATA, powervr_object_address(&firmware->system_data, 0));
	powervr_object_put32(system_init, POWERVR_SYSTEM_INIT_UTILISATION, powervr_object_address(&firmware->utilisation, 0));

	/* The core clock's real rate, the started flag clear and the marker set. */
	powervr_object_put32(system_init, POWERVR_SYSTEM_INIT_CORE_CLOCK, (uint32_t)core->core_rate);
	powervr_object_put32(system_init, POWERVR_SYSTEM_INIT_STARTED, 0);
	powervr_object_put32(system_init, POWERVR_SYSTEM_INIT_MARKER, 1);
}

/* Installs the handler of the firmware's interrupt, with nothing pending. */
static int
install_interrupt(
	struct powervr_core *core)
{
	int error;

	/* Drops an event left from before, so the first one counted is the firmware's. */
	powervr_write32(core, POWERVR_MIPS_IRQ_CLEAR, POWERVR_MIPS_IRQ_EVENT);
	core->firmware.interrupts = 0;

	/* Installs the handler and lets the line through. */
	error = kern_irq_register((int)core->description.interrupt, interrupt_handler, core);
	if (error != 0)
		return error;
	core->firmware.interrupt_installed = 1;
	kern_irq_unmask((int)core->description.interrupt);

	/* Succeeded: the firmware's events are counted. */
	return 0;
}

/* Removes the handler of the firmware's interrupt. */
static void
remove_interrupt(
	struct powervr_core *core)
{
	/* A handler that was never installed has nothing to remove. */
	if (!core->firmware.interrupt_installed)
		return;

	/* Stops the line, then removes the handler. */
	kern_irq_mask((int)core->description.interrupt);
	(void)kern_irq_unregister((int)core->description.interrupt, interrupt_handler, core);
	core->firmware.interrupt_installed = 0;
}

/*
 * Takes the firmware's event and counts it.
 *
 * It reads and clears one register, moves a counter only it writes, and
 * wakes the render node's service; it takes no lock of the firmware's.
 *
 * Procedure referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_fw_mips.c
 * (pvr_mips_irq_pending and pvr_mips_irq_clear).
 */
static void
interrupt_handler(
	int irq,
	kern_irq_ack_t acknowledge,
	void *argument)
{
	struct powervr_core *core;
	uint32_t status;

	/* The core the handler was installed for. */
	(void)irq;
	core = argument;

	/*
	 * Clears the event when the firmware raised one.  The count tells the
	 * later stage the interrupt reached the host.
	 */
	status = powervr_read32(core, POWERVR_MIPS_IRQ_STATUS);
	if ((status & POWERVR_MIPS_IRQ_EVENT) != 0U) {
		powervr_write32(core, POWERVR_MIPS_IRQ_CLEAR, POWERVR_MIPS_IRQ_EVENT);
		core->firmware.interrupts++;

		/* Wakes the render node's service, which reads what the firmware wrote. */
		powervr_render_interrupt(core);
	}

	/* Retires the acknowledgement before returning, as every handler must. */
	kern_irq_send_eoi(acknowledge);
}

/*
 * Takes the core out of reset in steps and lets its processor run.
 *
 * Procedure referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_fw_startstop.c
 * (pvr_fw_start: the secure bus protection off, everything into soft
 * reset, then out of reset in three steps around the cache, wrapper and
 * bus setup, the second reset register with them; a MIPS processor needs
 * no BIF set up).
 */
static int
start_processor(
	struct powervr_core *core)
{
	const struct powervr_fw_image *image;
	uint64_t everything;
	int second_reset;
	int pbe2;
	int secure;

	/* Chooses the reset masks the core's features call for. */
	image = &core->firmware.image;
	pbe2 = powervr_fw_image_has_feature(image, POWERVR_FEATURE_PBE2_IN_XE);
	second_reset = powervr_fw_image_has_feature(image, POWERVR_FEATURE_XE_TPU2);
	secure = powervr_fw_image_has_feature(image, POWERVR_FEATURE_SECURE_RESET);
	everything = POWERVR_SOFT_RESET_ALL;
	if (pbe2)
		everything = POWERVR_SOFT_RESET_ALL_PBE2;

	/* Turns off the secure bus protection that blocks the setup; the read fences the write. */
	if (secure) {
		powervr_write32(core, POWERVR_SYSTEM_BUS_SECURE, 0);
		(void)powervr_read32(core, POWERVR_SYSTEM_BUS_SECURE);
	}

	/* Puts everything in soft reset. */
	powervr_write64(core, POWERVR_SOFT_RESET, everything);
	if (second_reset)
		powervr_write64(core, POWERVR_SOFT_RESET2, POWERVR_SOFT_RESET2_ALL);
	fence_reset(core, second_reset);

	/* Takes the Rascal and the dusts out of reset. */
	powervr_write64(core, POWERVR_SOFT_RESET, everything ^ POWERVR_SOFT_RESET_RASCAL_DUSTS);
	if (second_reset)
		powervr_write64(core, POWERVR_SOFT_RESET2, 0);
	fence_reset(core, second_reset);

	/* Takes everything out of reset but the Garten, which holds the processor. */
	powervr_write64(core, POWERVR_SOFT_RESET, POWERVR_SOFT_RESET_GARTEN);
	if (second_reset)
		powervr_write64(core, POWERVR_SOFT_RESET2, 0);
	fence_reset(core, second_reset);

	/* Sets up the system level cache, the MIPS wrapper and the bus attributes. */
	kern_logf("powervr: firmware: start: out of reset but the Garten; cache\n");
	init_cache(core);
	kern_logf("powervr: firmware: start: MIPS wrapper\n");
	init_wrapper(core);
	powervr_write64(core, POWERVR_AXI_ACE_LITE, POWERVR_AXI_ACE_LITE_VALUE);

	/* Lets the processor out of reset with a pause on each side. */
	kern_logf("powervr: firmware: start: processor out of reset (started flag before 0x%x)\n",
		  (unsigned)powervr_object_get32(&core->firmware.system_init, POWERVR_SYSTEM_INIT_STARTED));
	kern_usleep_range(RELEASE_PAUSE_US, RELEASE_PAUSE_US * 2U);
	powervr_write64(core, POWERVR_SOFT_RESET, 0);
	(void)powervr_read64(core, POWERVR_SOFT_RESET);
	kern_usleep_range(RELEASE_PAUSE_US, RELEASE_PAUSE_US * 2U);

	/* Succeeded: the processor runs the boot loader. */
	return 0;
}

/*
 * Reads the reset registers back, so the writes leave the register
 * interface before the next step.
 */
static void
fence_reset(
	struct powervr_core *core,
	int second_reset)
{
	/* Reads the first register, and the second when the core has one. */
	(void)powervr_read64(core, POWERVR_SOFT_RESET);
	if (second_reset)
		(void)powervr_read64(core, POWERVR_SOFT_RESET2);
}

/*
 * Sets the system level cache's address decoding, keeping its hazard
 * check, and bypasses the burst combiner for short cache lines.
 *
 * Procedure referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_fw_startstop.c
 * (rogue_slc_init: only the low 32 bits are written; the lazy write-back
 * override of BRN 71242 applies only without multicore support).
 */
static void
init_cache(
	struct powervr_core *core)
{
	const struct powervr_fw_image *image;
	uint64_t line_bits;
	uint32_t control;
	int error;

	/* Keeps the hazard check and selects the hashed address decoding. */
	image = &core->firmware.image;
	control = powervr_read32(core, POWERVR_SLC_CONTROL) & POWERVR_SLC_HAZARD_CHECK;
	control |= POWERVR_SLC_HASH_DECODE;

	/* Bypasses the burst combiner when the cache line is shorter than 1024 bits. */
	error = powervr_fw_image_feature_value(image, POWERVR_FEATURE_SLC_LINE_BITS, &line_bits);
	if (error == 0 && line_bits < POWERVR_SLC_COMBINER_MIN_BITS)
		control |= POWERVR_SLC_BYPASS_COMBINER;

	/* Writes the low half; the high half keeps its reset value. */
	powervr_write32(core, POWERVR_SLC_CONTROL, control);
}

/*
 * Sets up the MIPS wrapper: where the processor sees the core's
 * registers, that it boots in microMIPS, and the remap windows that put
 * its boot code, boot data and exception vectors on their pages.
 *
 * Procedure referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_fw_mips.c
 * (pvr_mips_wrapper_init: the four remap windows, the fifth only for BRN
 * 63553, the Garten idle bit left to the processor, the debug probe on).
 */
static void
init_wrapper(
	struct powervr_core *core)
{
	struct powervr_firmware *firmware;
	const struct powervr_fw_section *boot_code;
	const struct powervr_fw_section *boot_data;
	const struct powervr_fw_section *exceptions;
	uint64_t boot_code_page;
	uint64_t boot_data_page;
	uint64_t exceptions_page;
	int quirk_63553;

	/* Finds the physical pages of the boot code, the boot data and the exception vectors. */
	firmware = &core->firmware;
	boot_code = powervr_fw_image_section(&firmware->image, POWERVR_SECTION_MIPS_BOOT_CODE);
	boot_data = powervr_fw_image_section(&firmware->image, POWERVR_SECTION_MIPS_BOOT_DATA);
	exceptions = powervr_fw_image_section(&firmware->image, POWERVR_SECTION_MIPS_EXCEPTIONS);
	boot_code_page = powervr_object_physical(&firmware->code, boot_code->alloc_offset);
	boot_data_page = powervr_object_physical(&firmware->data, boot_data->alloc_offset);
	exceptions_page = powervr_object_physical(&firmware->code, exceptions->alloc_offset);

	/* Puts the register window at its virtual base and boots in microMIPS. */
	powervr_write32(core, POWERVR_MIPS_WRAPPER, (POWERVR_MIPS_REGISTER_WINDOW >> POWERVR_MIPS_REGISTER_ALIGN) | POWERVR_MIPS_MICROMIPS_BOOT);

	/* Remaps the boot code. */
	powervr_write64(core, POWERVR_MIPS_REMAP1_INPUT, POWERVR_MIPS_BOOT_INPUT | POWERVR_MIPS_REMAP_ENABLE);
	powervr_write64(core, POWERVR_MIPS_REMAP1_OUTPUT, powervr_mips_remap_target(boot_code_page));

	/*
	 * Remaps address 0 to the boot code too, so the erratum's stray
	 * accesses there do not run off into unmapped memory.
	 */
	quirk_63553 = powervr_fw_image_has_quirk(&firmware->image, POWERVR_QUIRK_63553);
	if (quirk_63553) {
		powervr_write64(core, POWERVR_MIPS_REMAP5_INPUT, POWERVR_MIPS_REMAP_ENABLE);
		powervr_write64(core, POWERVR_MIPS_REMAP5_OUTPUT, powervr_mips_remap_target(boot_code_page));
	}

	/* Remaps the boot data and the exception vectors. */
	powervr_write64(core, POWERVR_MIPS_REMAP2_INPUT, POWERVR_MIPS_DATA_INPUT | POWERVR_MIPS_REMAP_ENABLE);
	powervr_write64(core, POWERVR_MIPS_REMAP2_OUTPUT, powervr_mips_remap_target(boot_data_page));
	powervr_write64(core, POWERVR_MIPS_REMAP3_INPUT, POWERVR_MIPS_CODE_INPUT | POWERVR_MIPS_REMAP_ENABLE);
	powervr_write64(core, POWERVR_MIPS_REMAP3_OUTPUT, powervr_mips_remap_target(exceptions_page));

	/* Leaves the Garten's idle bit to the processor and turns on its debug probe. */
	powervr_write64(core, POWERVR_GARTEN_WRAPPER, POWERVR_GARTEN_IDLE_BY_FIRMWARE);
	powervr_write32(core, POWERVR_MIPS_DEBUG, 0);
}

/* Waits for the firmware to set its started flag. */
static int
wait_started(
	struct powervr_core *core)
{
	uint64_t deadline;
	uint64_t now;
	uint32_t started;

	/* Polls the flag in short sleeps until it is set or the bound is spent. */
	deadline = ms_deadline(BOOT_WAIT_MS);
	for (;;) {
		started = powervr_object_get32(&core->firmware.system_init, POWERVR_SYSTEM_INIT_STARTED);
		if (started != 0U)
			break;

		/* The bound is spent without the flag. */
		now = sched_ticks();
		if (now >= deadline) {
			kern_logf("powervr: firmware: boot timed out waiting for the started flag (0x%x)\n", (unsigned)started);
			return ETIMEDOUT;
		}

		/* Sleeps one tick. */
		sched_sleep(now + 1U);
	}

	/* What the firmware wrote at its start: the flag, its time, and the identities it filled. */
	kern_logf("powervr: firmware: boot: started 0x%x at %u, marker %u, hardware identity %u/0x%x, firmware identity %u/0x%x\n",
		  (unsigned)started,
		  (unsigned)powervr_object_get32(&core->firmware.system_init, DUMP_SYSTEM_INIT_STARTED_TIME),
		  (unsigned)powervr_object_get32(&core->firmware.system_init, POWERVR_SYSTEM_INIT_MARKER),
		  (unsigned)powervr_object_get32(&core->firmware.os_init, POWERVR_OS_INIT_HW_IDENTITY),
		  (unsigned)powervr_object_get32(&core->firmware.os_init, POWERVR_OS_INIT_HW_IDENTITY + 8U),
		  (unsigned)powervr_object_get32(&core->firmware.os_init, POWERVR_OS_INIT_FW_IDENTITY),
		  (unsigned)powervr_object_get32(&core->firmware.os_init, POWERVR_OS_INIT_FW_IDENTITY + 8U));

	/* Succeeded: the firmware runs. */
	return 0;
}

/*
 * Sends one health check through the kernel's command buffer and waits
 * for the firmware to mark it executed.
 *
 * Procedure referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_ccb.c
 * (pvr_kccb_send_cmd_reserved_powered: the command into the slot at the
 * write offset, its return slot cleared, a barrier, the write offset
 * moved, then a kick of the general purpose data master) and pvr_fw.c
 * (pvr_fw_mts_schedule: the kick ordered after the memory writes).
 */
static int
health_check(
	struct powervr_core *core)
{
	uint32_t command[POWERVR_COMMAND_WORDS];
	uint32_t answer;
	uint32_t index;
	uint32_t slot;
	int error;

	/* The command: its type alone. */
	answer = 0;
	for (index = 0; index < POWERVR_COMMAND_WORDS; index++)
		command[index] = 0;
	command[POWERVR_COMMAND_TYPE / 4U] = POWERVR_COMMAND_HEALTH_CHECK;

	/* Sends it and waits for the return slot to say it was executed. */
	error = powervr_command_send(core, command, &slot);
	if (error != 0)
		return error;
	error = powervr_command_wait(core, slot, POWERVR_COMMAND_WAIT_MS, &answer);
	if (error != 0) {
		kern_logf("powervr: firmware: health check timed out waiting for its return slot (%d, 0x%x)\n", error, (unsigned)answer);
		firmware_dump(core, slot);
		return error;
	}

	/* Succeeded: the firmware executed the command. */
	return 0;
}

/*
 * Stops the firmware's processor: waits, each wait bounded, for the bus
 * and the cache to go idle, then holds the whole core in soft reset.
 *
 * A wait that runs out does not stop the stop; the reset follows anyway,
 * and the power domain goes off after it.
 *
 * Procedure referenced:
 * Linux v7.3 drivers/gpu/drm/imagination/pvr_fw_startstop.c (pvr_fw_stop:
 * the data masters unassociated from the thread, the BIF and SLC status
 * waits, and for a core with the MARS layout the processor's idle bits
 * in place of the sidekick's).
 */
static void
stop_processor(
	struct powervr_core *core)
{
	const struct powervr_fw_image *image;
	uint64_t layout_mars;
	uint64_t everything;
	int xt_top;
	int pbe2;
	int error;

	/* Reads which waits apply to this core. */
	image = &core->firmware.image;
	layout_mars = 0;
	(void)powervr_fw_image_feature_value(image, POWERVR_FEATURE_LAYOUT_MARS, &layout_mars);
	xt_top = powervr_fw_image_has_feature(image, POWERVR_FEATURE_XT_TOP);
	pbe2 = powervr_fw_image_has_feature(image, POWERVR_FEATURE_PBE2_IN_XE);

	/* Unassociates the data masters from the processor's thread. */
	powervr_write32(core, POWERVR_MTS_INTERRUPT_ASSOC, 0);
	powervr_write32(core, POWERVR_MTS_BACKGROUND_ASSOC, 0);

	/* Waits for the MMUs and the outstanding reads to drain. */
	error = wait_register(core, POWERVR_BIF_MMU_STATUS, POWERVR_BIF_MMU_STATUS_MASK, 0, 0);
	if (error == 0)
		error = wait_register(core, POWERVR_BIFPM_MMU_STATUS, POWERVR_BIFPM_MMU_STATUS_MASK, 0, 0);
	if (error == 0 && !xt_top)
		error = wait_register(core, POWERVR_BIF_READS_STATUS, POWERVR_BIF_READS_STATUS_MASK, 0, 0);
	if (error == 0)
		error = wait_register(core, POWERVR_BIFPM_READS_STATUS, POWERVR_BIFPM_READS_STATUS_MASK, 0, 0);
	if (error == 0)
		error = wait_register(core, POWERVR_SLC_STATUS, POWERVR_SLC_STATUS_MASK, 0, 1);

	/* With the MARS layout the processor and its bus arbiter report idle on their own. */
	if (error == 0 && layout_mars != 0U)
		(void)wait_register(core, POWERVR_MARS_IDLE, POWERVR_MARS_IDLE_FIRMWARE, POWERVR_MARS_IDLE_FIRMWARE, 0);

	/* Holds the whole core in soft reset. */
	everything = POWERVR_SOFT_RESET_ALL;
	if (pbe2)
		everything = POWERVR_SOFT_RESET_ALL_PBE2;
	powervr_write64(core, POWERVR_SOFT_RESET, everything);
	(void)powervr_read64(core, POWERVR_SOFT_RESET);
}

/* Waits, bounded, until a register's masked bits read as the value. */
static int
wait_register(
	struct powervr_core *core,
	uint32_t offset,
	uint64_t mask,
	uint64_t value,
	int wide)
{
	uint64_t deadline;
	uint64_t now;
	uint64_t current;

	/* Polls in short sleeps until the bits match or the bound is spent. */
	deadline = ms_deadline(IDLE_WAIT_MS);
	for (;;) {
		/* Reads the register at its width. */
		if (wide) {
			current = powervr_read64(core, offset);
		} else {
			current = powervr_read32(core, offset);
		}

		/* Stops once the masked bits read as asked. */
		if ((current & mask) == value)
			break;

		/* The bound is spent without the bits. */
		now = sched_ticks();
		if (now >= deadline) {
			kern_logf("powervr: firmware: stop timed out waiting for register 0x%x (0x%llx, mask 0x%llx)\n",
				  (unsigned)offset, (unsigned long long)current, (unsigned long long)mask);
			return ETIMEDOUT;
		}

		/* Sleeps one tick. */
		sched_sleep(now + 1U);
	}

	/* Succeeded: the register reads as asked. */
	return 0;
}

/* Undoes the later stage as far as it got, and powers the core down. */
static void
tear_down(
	struct powervr_core *core,
	enum late_stage reached)
{
	/* Stops a processor that was let out of reset. */
	if (reached >= LATE_STARTED)
		stop_processor(core);

	/* Removes the interrupt handler before the memory it reports on goes. */
	if (reached >= LATE_INTERRUPT)
		remove_interrupt(core);

	/* Returns the firmware's memory and the kept file. */
	free_blocks(core);
	free_file(core);

	/* Puts the resets back, stops the clocks in reverse and turns the domain off. */
	powervr_power_down(core);
}

/* Returns every block and the page table; the kept file stays. */
static void
free_blocks(
	struct powervr_core *core)
{
	struct powervr_firmware *firmware;
	struct powervr_heap *heap;

	/* Returns the blocks in the reverse of the order they were made. */
	firmware = &core->firmware;
	heap = &firmware->heap;
	powervr_object_destroy(heap, &firmware->system_init);
	powervr_object_destroy(heap, &firmware->os_init);
	powervr_object_destroy(heap, &firmware->os_data);
	powervr_object_destroy(heap, &firmware->trace_control);
	powervr_object_destroy(heap, &firmware->trace_buffer);
	powervr_object_destroy(heap, &firmware->runtime);
	powervr_object_destroy(heap, &firmware->utilisation);
	powervr_object_destroy(heap, &firmware->fault_page);
	powervr_object_destroy(heap, &firmware->system_data);
	powervr_object_destroy(heap, &firmware->mmu_sync);
	powervr_object_destroy(heap, &firmware->hwr_info);
	powervr_object_destroy(heap, &firmware->power_sync);
	powervr_object_destroy(heap, &firmware->connection);
	powervr_object_destroy(heap, &firmware->fwccb);
	powervr_object_destroy(heap, &firmware->fwccb_control);
	powervr_object_destroy(heap, &firmware->kccb_returns);
	powervr_object_destroy(heap, &firmware->kccb);
	powervr_object_destroy(heap, &firmware->kccb_control);
	powervr_object_destroy(heap, &firmware->data);
	powervr_object_destroy(heap, &firmware->code);
	powervr_heap_fini(heap);
}

/* Returns the kept file. */
static void
free_file(
	struct powervr_core *core)
{
	struct powervr_firmware *firmware;

	/* A file that was never read holds nothing. */
	firmware = &core->firmware;
	if (firmware->file != NULL)
		kern_free(firmware->file);
	firmware->file = NULL;
	firmware->file_size = 0;
}

/* Reports the scheduler tick a number of milliseconds from now, at least one tick away. */
static uint64_t
ms_deadline(
	unsigned milliseconds)
{
	uint64_t ticks;

	/* Rounds up, so a short bound still waits one tick. */
	ticks = ((uint64_t)milliseconds * KERN_CLOCK_HZ + 999U) / 1000U;
	if (ticks == 0U)
		ticks = 1;

	/* Succeeded: the deadline. */
	return sched_ticks() + ticks;
}

/* Notes that a step of the later stage begins, on one line. */
static void
step_start(
	const char *name)
{
	/* The tick first, so the thread that reports never sees a name with an old tick. */
	__atomic_store_n(&step_began, sched_ticks(), __ATOMIC_RELEASE);
	__atomic_store_n(&step_name, name, __ATOMIC_RELEASE);
	kern_logf("powervr: firmware: %s start\n", name);
}

/* Notes that a step ended, with how long it took and how it went, on one line. */
static void
step_done(
	const char *name,
	int error)
{
	uint64_t ticks;
	unsigned milliseconds;

	/* The time the step took. */
	ticks = sched_ticks() - __atomic_load_n(&step_began, __ATOMIC_ACQUIRE);
	milliseconds = (unsigned)(ticks * 1000U / KERN_CLOCK_HZ);
	__atomic_store_n(&step_name, (const char *)NULL, __ATOMIC_RELEASE);

	/* Done, or failed with its error. */
	if (error == 0) {
		kern_logf("powervr: firmware: %s done (%u ms)\n", name, milliseconds);
	} else {
		kern_logf("powervr: firmware: %s failed (%d) after %u ms\n", name, error, milliseconds);
	}
}

/*
 * Reports, every few seconds, a step of the later stage that has not
 * ended, until the stage ends.  It runs beside the stage, so a step that
 * never returns is still named.
 */
static void
watch_thread(
	void *argument)
{
	const char *name;
	uint64_t began;
	uint64_t now;
	uint64_t reported;
	uint64_t interval;

	(void)argument;
	interval = ((uint64_t)WATCH_REPORT_MS * KERN_CLOCK_HZ + 999U) / 1000U;
	reported = 0;
	while (!__atomic_load_n(&stage_ended, __ATOMIC_ACQUIRE)) {
		/* Sleeps a second. */
		sched_sleep(sched_ticks() + KERN_CLOCK_HZ);

		/* A step that has run past the next report time is named. */
		name = __atomic_load_n(&step_name, __ATOMIC_ACQUIRE);
		began = __atomic_load_n(&step_began, __ATOMIC_ACQUIRE);
		now = sched_ticks();
		if (name == NULL || now - began < interval) {
			reported = 0;
			continue;
		}
		if (reported != 0 && now - reported < interval)
			continue;
		reported = now;
		kern_logf("powervr: firmware: still in %s after %u s\n", name, (unsigned)((now - began) / KERN_CLOCK_HZ));
	}
}

/*
 * Prints what the host and the firmware share about a command the
 * firmware did not execute: the two command buffers' controls, the
 * command and its return slot with their firmware addresses, the
 * scheduler and the processor's registers, the interrupts, the
 * firmware's own counts and power state, an assertion it left, and the
 * last words of its trace.
 */
static void
firmware_dump(
	struct powervr_core *core,
	uint32_t slot)
{
	struct powervr_firmware *firmware;
	struct powervr_object *trace;
	char text[DUMP_ASSERT_TEXT + 1U];
	uint32_t pointer;
	uint32_t first;
	uint32_t word;
	uint32_t index;

	/* The command buffers' controls. */
	firmware = &core->firmware;
	kern_logf("powervr: dump: kccb control at 0x%x: write %u read %u wrap 0x%x size %u; fwccb control at 0x%x: write %u read %u\n",
		  (unsigned)powervr_object_address(&firmware->kccb_control, 0),
		  (unsigned)powervr_object_get32(&firmware->kccb_control, POWERVR_CCB_WRITE),
		  (unsigned)powervr_object_get32(&firmware->kccb_control, POWERVR_CCB_READ),
		  (unsigned)powervr_object_get32(&firmware->kccb_control, POWERVR_CCB_WRAP_MASK),
		  (unsigned)powervr_object_get32(&firmware->kccb_control, POWERVR_CCB_COMMAND_BYTES),
		  (unsigned)powervr_object_address(&firmware->fwccb_control, 0),
		  (unsigned)powervr_object_get32(&firmware->fwccb_control, POWERVR_CCB_WRITE),
		  (unsigned)powervr_object_get32(&firmware->fwccb_control, POWERVR_CCB_READ));

	/* The command in its slot and its return slot. */
	kern_logf("powervr: dump: slot %u at 0x%x: 0x%x 0x%x 0x%x 0x%x; return at 0x%x: 0x%x\n",
		  (unsigned)slot,
		  (unsigned)powervr_object_address(&firmware->kccb, slot * POWERVR_COMMAND_BYTES),
		  (unsigned)powervr_object_get32(&firmware->kccb, slot * POWERVR_COMMAND_BYTES),
		  (unsigned)powervr_object_get32(&firmware->kccb, slot * POWERVR_COMMAND_BYTES + 4U),
		  (unsigned)powervr_object_get32(&firmware->kccb, slot * POWERVR_COMMAND_BYTES + 8U),
		  (unsigned)powervr_object_get32(&firmware->kccb, slot * POWERVR_COMMAND_BYTES + 12U),
		  (unsigned)powervr_object_address(&firmware->kccb_returns, slot * 4U),
		  (unsigned)powervr_object_get32(&firmware->kccb_returns, slot * 4U));

	/* The records' firmware addresses as the per-OS record names them. */
	kern_logf("powervr: dump: os init at 0x%x names kccb control 0x%x kccb 0x%x returns 0x%x fwccb control 0x%x os data 0x%x\n",
		  (unsigned)powervr_object_address(&firmware->os_init, 0),
		  (unsigned)powervr_object_get32(&firmware->os_init, POWERVR_OS_INIT_KCCB_CONTROL),
		  (unsigned)powervr_object_get32(&firmware->os_init, POWERVR_OS_INIT_KCCB),
		  (unsigned)powervr_object_get32(&firmware->os_init, POWERVR_OS_INIT_KCCB_RETURNS),
		  (unsigned)powervr_object_get32(&firmware->os_init, POWERVR_OS_INIT_FWCCB_CONTROL),
		  (unsigned)powervr_object_get32(&firmware->os_init, POWERVR_OS_INIT_OS_DATA));

	/* The scheduler's and the processor's registers. */
	kern_logf("powervr: dump: mts schedule 0x%x interrupt assoc 0x%x background assoc 0x%x; soft reset 0x%llx; mips wrapper 0x%x garten 0x%llx; mips irq status 0x%x\n",
		  (unsigned)powervr_read32(core, POWERVR_MTS_SCHEDULE),
		  (unsigned)powervr_read32(core, POWERVR_MTS_INTERRUPT_ASSOC),
		  (unsigned)powervr_read32(core, POWERVR_MTS_BACKGROUND_ASSOC),
		  (unsigned long long)powervr_read64(core, POWERVR_SOFT_RESET),
		  (unsigned)powervr_read32(core, POWERVR_MIPS_WRAPPER),
		  (unsigned long long)powervr_read64(core, POWERVR_GARTEN_WRAPPER),
		  (unsigned)powervr_read32(core, POWERVR_MIPS_IRQ_STATUS));

	/* The interrupts the host took and the firmware's own counts and power state. */
	kern_logf("powervr: dump: host interrupts %u; firmware interrupts %u, kccb executed %u, power state %u; started 0x%x at %u\n",
		  (unsigned)firmware->interrupts,
		  (unsigned)powervr_object_get32(&firmware->os_data, DUMP_OS_DATA_INTERRUPTS),
		  (unsigned)powervr_object_get32(&firmware->os_data, POWERVR_OS_DATA_KCCB_EXECUTED),
		  (unsigned)powervr_object_get32(&firmware->system_data, DUMP_SYSTEM_DATA_POWER),
		  (unsigned)powervr_object_get32(&firmware->system_init, POWERVR_SYSTEM_INIT_STARTED),
		  (unsigned)powervr_object_get32(&firmware->system_init, DUMP_SYSTEM_INIT_STARTED_TIME));

	/* An assertion the firmware left: its file, its text and its line. */
	trace = &firmware->trace_control;
	for (index = 0; index < DUMP_ASSERT_TEXT; index++) {
		word = powervr_object_get32(trace, (DUMP_ASSERT_PATH + index) & ~3U);
		text[index] = (char)(word >> (8U * ((DUMP_ASSERT_PATH + index) & 3U)));
	}
	text[DUMP_ASSERT_TEXT] = '\0';
	kern_logf("powervr: dump: assert path \"%s\" line %u\n", text, (unsigned)powervr_object_get32(trace, DUMP_ASSERT_LINE));
	for (index = 0; index < DUMP_ASSERT_TEXT; index++) {
		word = powervr_object_get32(trace, (DUMP_ASSERT_INFO + index) & ~3U);
		text[index] = (char)(word >> (8U * ((DUMP_ASSERT_INFO + index) & 3U)));
	}
	text[DUMP_ASSERT_TEXT] = '\0';
	kern_logf("powervr: dump: assert info \"%s\"\n", text);

	/* The trace: where the firmware writes next, then the words before it, eight a line. */
	pointer = powervr_object_get32(trace, DUMP_TRACE_POINTER);
	kern_logf("powervr: dump: trace log type 0x%x, pointer %u of %u\n",
		  (unsigned)powervr_object_get32(trace, POWERVR_TRACE_LOG_TYPE), (unsigned)pointer, (unsigned)POWERVR_TRACE_WORDS);
	if (pointer > POWERVR_TRACE_WORDS)
		pointer = 0;
	first = pointer >= DUMP_TRACE_LAST_WORDS ? pointer - DUMP_TRACE_LAST_WORDS : 0;
	for (index = first; index + 8U <= pointer; index += 8U) {
		kern_logf("powervr: dump: trace %5u: %08x %08x %08x %08x %08x %08x %08x %08x\n",
			  (unsigned)index,
			  (unsigned)powervr_object_get32(&firmware->trace_buffer, (index + 0U) * 4U),
			  (unsigned)powervr_object_get32(&firmware->trace_buffer, (index + 1U) * 4U),
			  (unsigned)powervr_object_get32(&firmware->trace_buffer, (index + 2U) * 4U),
			  (unsigned)powervr_object_get32(&firmware->trace_buffer, (index + 3U) * 4U),
			  (unsigned)powervr_object_get32(&firmware->trace_buffer, (index + 4U) * 4U),
			  (unsigned)powervr_object_get32(&firmware->trace_buffer, (index + 5U) * 4U),
			  (unsigned)powervr_object_get32(&firmware->trace_buffer, (index + 6U) * 4U),
			  (unsigned)powervr_object_get32(&firmware->trace_buffer, (index + 7U) * 4U));
	}
}
