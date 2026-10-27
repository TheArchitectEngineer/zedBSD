/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the GHCI 1.0 driver's core (ghci.c, cp.c, recover.c,
 * memory.c) against the GPU model (ghci-model.c) and the kernel stubs
 * (ghci-stubs.c): the attach, the order of the registers, submissions,
 * fences, the interrupt and the poll, the ring's room, device faults and
 * the recovery, hangs, the loss of a device, the gate and the detach
 * (design section 14, the scenes of p004).
 *
 * The test runs on one thread.  A wait of the driver is one tick of the
 * fake clock, in which the model runs and the scene's action may run:
 * the poll thread's work on a polled device, a fault, or the service
 * thread's recovery while the driver waits at the gate.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "drivers/gpu/gpu.h"
#include "drivers/gpu/ghci/ghci.h"
#include "drivers/gpu/ghci/ghci-cmd.h"
#include "drivers/gpu/ghci/ghci-regs.h"
#include "kern/clock.h"
#include "kern/lock.h"
#include "uapi/errno.h"
#include "uapi/gpu.h"
#include "ghci-model.h"
#include "ghci-stubs.h"

/* The NOP dwords of one ordinary submission of the scenes. */
#define TEST_SUBMIT_DWORDS		200U

/* The dwords of the submission larger than the ring (64 Ki dwords). */
#define TEST_LARGE_DWORDS		70000U

/* The submissions of the wrap scene. */
#define TEST_SUBMITS			1000U

/* The milliseconds the scenes wait for a ticket. */
#define TEST_WAIT_MS			100U

/* The PCI IDs the scenes give the device (the KC705 board). */
#define TEST_PCI_VENDOR			0x10eeU
#define TEST_PCI_DEVICE			0x7024U

/* The watchdog cycles the driver programs at the model's 100 MHz clock (2000 ms). */
#define TEST_WDT_CYCLES			200000000U

/*
 * The counts of the checks that held and that failed; main reports both
 * and fails when any check failed.
 */
static unsigned checks_passed;
static unsigned checks_failed;

/* The device of the scene that runs; start_scene fills it. */
static struct ghci_device device;

/*
 * How the fake bus behaves in the scene: polled leaves the device
 * without an interrupt, master_found says the firmware left bus
 * mastering on, master_broken has the bus turn it on without the model
 * seeing it (the GPU never fetches).
 */
static int test_polled;
static int test_master_found;
static int test_master_broken;
static unsigned test_release_busy;
static uint32_t test_ring_bytes;

/*
 * What the fake bus saw: the calls, and the state of the model at the
 * first interrupt and the first bus master: whether a reset had run,
 * whether RUN was off and IRQ_STATUS clear, whether the ring was set.
 */
static unsigned interrupt_takes;
static unsigned interrupt_releases;
static unsigned master_on_calls;
static unsigned master_off_calls;
static int irq_after_reset;
static int master_before_run;
static int ring_before_master;
static int release_after_quiet;
static int master_off_after_release;

/*
 * The scene's action at each sleep of the driver, the number of sleeps
 * it skips first, and the guard that keeps it from running inside its own
 * sleeps (a recovery sleeps too).
 */
static void (*scene_action)(void);
static unsigned scene_action_after;
static int scene_action_running;
static unsigned scene_action_runs;

static void expect(int held, const char *name);
static int start_scene(enum model_profile profile, const struct model_switches *switches);
static void prepare_scene(enum model_profile profile, const struct model_switches *switches);
static int start_device(void);
static void end_scene(const char *name);
static void check_locks(const char *name);
static void model_irq(void *argument);
static void sleep_hook(unsigned sleep, void *argument);
static int fake_interrupt_take(struct ghci_device *target);
static int fake_interrupt_release(struct ghci_device *target);
static int fake_bus_master(struct ghci_device *target, int on);
static int submit_nops(uint32_t dwords, struct ghci_ticket *ticket);
static void action_recover(void);
static void action_stall_then_fault(void);
static void action_session_fault(void);
static void action_session_fault_once(void);
static uint32_t heap_bytes(void);
static void scene_versions(void);
static void scene_attach_checks(void);
static void scene_attach_policy(void);
static void scene_submit(void);
static void scene_polled(void);
static void scene_device_fault(void);
static void scene_fence_before_fault(void);
static void scene_stale_record(void);
static void scene_hang(void);
static void scene_long_submit(void);
static void scene_room_wait(void);
static void scene_gate(void);
static void scene_lost(void);
static void scene_storm(void);
static void scene_check_fails(void);
static void scene_detach(void);

/*
 * The operations of the fake bus; they live as long as the test.
 */
static const struct ghci_bus_ops fake_bus_ops = {
	fake_interrupt_take,
	fake_interrupt_release,
	fake_bus_master
};

/*
 * Runs the scenes and reports.
 */
int
main(void)
{
	/* Keeps the driver's log out of the report. */
	stub.quiet = 1;

	/* Runs the scenes. */
	scene_versions();
	scene_attach_checks();
	scene_attach_policy();
	scene_submit();
	scene_polled();
	scene_device_fault();
	scene_fence_before_fault();
	scene_stale_record();
	scene_hang();
	scene_long_submit();
	scene_room_wait();
	scene_gate();
	scene_lost();
	scene_storm();
	scene_check_fails();
	scene_detach();
	model_fini();

	/* Reports the counts. */
	printf("ghci-driver-host-test: %u passed, %u failed\n", checks_passed, checks_failed);
	if (checks_failed != 0U) {
		printf("FAIL ghci-driver-host-test\n");
		return 1;
	}

	/* Succeeded: every check held. */
	printf("PASS ghci-driver-host-test\n");
	return 0;
}

/* The service thread is not started on the host: the scenes run its steps. */
int
ghci_service_start(
	struct ghci_device *target)
{
	(void)target;
	return 0;
}

/* The poll thread is not started on the host: the sleep hook does its work. */
int
ghci_poll_start(
	struct ghci_device *target)
{
	(void)target;
	return 0;
}

/* Counts a check, and reports one that failed by name. */
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

	/* A failed check is reported by name. */
	checks_failed++;
	printf("FAIL %s\n", name);
}

/*
 * Starts a scene: a model of a profile, the stubs with the driver's lock
 * order and forbidden waits, and the device started.  Returns what the
 * start returned.
 */
static int
start_scene(
	enum model_profile profile,
	const struct model_switches *switches)
{
	int error;

	/* The model and the stubs, then the device. */
	prepare_scene(profile, switches);
	error = start_device();
	return error;
}

/*
 * Prepares a scene without starting the device: the model of a profile
 * and the stubs with the driver's lock order and forbidden waits.  A
 * scene may change the model before start_device().
 */
static void
prepare_scene(
	enum model_profile profile,
	const struct model_switches *switches)
{
	static const char *const order[] = {
		"ghci device", "ghci display", "ghci ring", "ghci interrupt"
	};

	/* The model and the stubs, with the rules the driver must keep. */
	model_init(profile, switches);
	stub_reset();
	stub_lock_order(order, 4U);
	stub_forbid_wait("ghci fence", "ghci device");
	stub_forbid_wait("ghci fence", "ghci display");
	stub_forbid_wait("ghci fence", "ghci ring");
	stub_forbid_wait("ghci gate", "ghci device");
	stub_forbid_wait("ghci gate", "ghci display");
	stub_forbid_wait("ghci gate", "ghci ring");
	stub_set_sleep_hook(sleep_hook, NULL);

	/* The fake bus. */
	interrupt_takes = 0U;
	interrupt_releases = 0U;
	master_on_calls = 0U;
	master_off_calls = 0U;
	irq_after_reset = 0;
	master_before_run = 0;
	ring_before_master = 0;
	release_after_quiet = 1;
	master_off_after_release = 1;
	scene_action = NULL;
	scene_action_after = 0U;
	scene_action_running = 0;
	scene_action_runs = 0U;
	model_set_bus_master(test_master_found);
}

/* Starts the device of the prepared scene as the bus glue leaves it.  Returns what the start returned. */
static int
start_device(void)
{
	int error;

	/* The device as the bus glue leaves it. */
	memset(&device, 0, sizeof(device));
	device.registers = stub_bar0();
	device.register_bytes = 4096U;
	device.bus_ops = &fake_bus_ops;
	device.pci_vendor = TEST_PCI_VENDOR;
	device.pci_device = TEST_PCI_DEVICE;
	device.bus_master_found = test_master_found;
	device.ring_bytes = test_ring_bytes;

	/* Starts it. */
	error = ghci_device_start(&device);
	return error;
}

/* Ends a scene: stops the device and checks how it waited and locked. */
static void
end_scene(
	const char *name)
{
	int error;

	/* A device that started stops. */
	scene_action = NULL;
	if (device.state != GHCI_STATE_DETACHED && device.fence_page != NULL) {
		error = ghci_device_stop(&device);
		expect(error == 0, name);
	}

	/* The locks and the waits. */
	check_locks(name);

	/* The flags of the next scene start again. */
	test_polled = 0;
	test_master_found = 0;
	test_master_broken = 0;
	test_release_busy = 0U;
	test_ring_bytes = 0U;
}

/* Checks that the driver kept its lock order and its rules on waits, and holds nothing. */
static void
check_locks(
	const char *name)
{
	char label[128];

	/* Each count by name. */
	snprintf(label, sizeof(label), "%s: lock order", name);
	expect(stub.order_violations == 0U, label);
	snprintf(label, sizeof(label), "%s: waits", name);
	expect(stub.wait_violations == 0U, label);
	snprintf(label, sizeof(label), "%s: double locks", name);
	expect(stub.double_locks == 0U && stub.unlock_errors == 0U, label);
	snprintf(label, sizeof(label), "%s: nothing held", name);
	expect(stub.held_count == 0U, label);
	snprintf(label, sizeof(label), "%s: MMIO", name);
	expect(stub.mmio_errors == 0U, label);
}

/*
 * The model's interrupt line, wired to the driver's handler.  While the
 * driver holds its interrupt lock (it writes IRQ_ENABLE inside it) the
 * handler would spin on another processor; here the line stays high and
 * the next tick of the model calls the handler.
 */
static void
model_irq(
	void *argument)
{
	if (stub_holds("ghci interrupt"))
		return;
	(void)ghci_cp_interrupt(argument);
}

/* Each sleep of the driver: the poll of a polled device, then the scene's action. */
static void
sleep_hook(
	unsigned sleep,
	void *argument)
{
	(void)argument;

	/* The poll thread's work. */
	if (device.polled && device.fence_page != NULL && device.state != GHCI_STATE_DETACHED)
		(void)ghci_cp_interrupt(&device);

	/* The scene's action, never inside itself. */
	if (scene_action == NULL || scene_action_running || sleep < scene_action_after)
		return;
	scene_action_running = 1;
	scene_action_runs++;
	scene_action();
	scene_action_running = 0;
}

/* Takes the interrupt: the model's line, unless the scene polls; records the order. */
static int
fake_interrupt_take(
	struct ghci_device *target)
{
	/* What the model was when the interrupt came: reset, not running, nothing pending. */
	interrupt_takes++;
	irq_after_reset = model.resets != 0U && (model.ctrl & GHCI_CTRL_RUN) == 0U;

	/* A polled scene takes none. */
	target->polled = test_polled;
	if (!test_polled)
		model_set_irq_handler(model_irq, target);
	return 0;
}

/* Lets the interrupt go, or answers EBUSY as many times as the scene says. */
static int
fake_interrupt_release(
	struct ghci_device *target)
{
	(void)target;

	/* A handler still running elsewhere. */
	if (test_release_busy != 0U) {
		test_release_busy--;
		return EBUSY;
	}

	/* The device is reset and quiet when its interrupt goes. */
	interrupt_releases++;
	if (model.ctrl != 0U || model_read(GHCI_REG_IRQ_ENABLE) != 0U)
		release_after_quiet = 0;
	model_set_irq_handler(NULL, NULL);
	return 0;
}

/* Turns bus mastering on or off in the model; records the order. */
static int
fake_bus_master(
	struct ghci_device *target,
	int on)
{
	(void)target;

	/* Off, after the interrupt went. */
	if (!on) {
		master_off_calls++;
		if (model.irq_handler != NULL)
			master_off_after_release = 0;
		model_set_bus_master(0);
		return 0;
	}

	/* On: the ring is set and the processor does not run yet. */
	master_on_calls++;
	master_before_run = (model.ctrl & GHCI_CTRL_RUN) == 0U;
	ring_before_master = model_read(GHCI_REG_RING_SIZE) != 0U && model_read(GHCI_REG_CP_HEAP_BASE) != 0U;
	if (!test_master_broken)
		model_set_bus_master(1);
	return 0;
}

/* Submits dwords of NOPs as the driver's own work. */
static int
submit_nops(
	uint32_t dwords,
	struct ghci_ticket *ticket)
{
	static uint32_t packets[TEST_LARGE_DWORDS];
	uint32_t done;
	uint32_t length;
	int error;

	/* NOPs of up to 255 dwords. */
	done = 0U;
	while (done < dwords) {
		length = dwords - done;
		if (length > GHCI_PACKET_DWORDS_MAX)
			length = GHCI_PACKET_DWORDS_MAX;
		(void)ghci_cmd_nop(&packets[done], length);
		done += length;
	}

	/* The submission. */
	error = ghci_cp_submit(&device, 0U, packets, dwords, ticket);
	return error;
}

/* The scene's action: the service thread's recovery. */
static void
action_recover(void)
{
	ghci_service_step(&device, GHCI_WORK_RECOVER);
}

/*
 * The scene's action for the wait for room: the clock jumps far past the
 * hang time (a wait for room has no timeout of its own), then a fault.
 */
static void
action_stall_then_fault(void)
{
	if (scene_action_runs < 100U)
		stub.ticks += kern_ms_to_ticks(GHCI_HANG_MS);
	if (scene_action_runs == 100U)
		model_inject(GHCI_FAULT_KIND_AXI_ERROR, GHCI_UNIT_CP, 0U, 0x2000U, 0);
}

/* The scene's action: a session's fault under PROT (the processor goes on), each time. */
static void
action_session_fault(void)
{
	model_inject(GHCI_FAULT_KIND_PAGE_FAULT, GHCI_UNIT_SU, 1U, 0x4000U, 0);
}

/* The scene's action: a session's fault under PROT, the first time only. */
static void
action_session_fault_once(void)
{
	if (scene_action_runs == 1U)
		action_session_fault();
}

/* Reports the bytes of CP_HEAP the driver gave the model. */
static uint32_t
heap_bytes(void)
{
	return model_read(GHCI_REG_CP_HEAP_LIMIT) - model_read(GHCI_REG_CP_HEAP_BASE);
}

/* The versions and the magic the attach takes and refuses. */
static void
scene_versions(void)
{
	static const uint32_t versions[] = { 0x00010000U, 0x00010003U, 0x00020000U, 0U };
	static const int taken[] = { 1, 1, 0, 0 };
	char label[96];
	unsigned index;
	int error;

	/* Each version: 1.x is taken, anything else is not GHCI 1.0. */
	for (index = 0U; index < 4U; index++) {
		prepare_scene(MODEL_KC705_UMA, NULL);
		model.window[GHCI_REG_VERSION / 4U] = versions[index];
		error = start_device();
		snprintf(label, sizeof(label), "versions: %08x", (unsigned)versions[index]);
		if (taken[index])
			expect(error == 0, label);
		else
			expect(error == ENODEV && stub.registrations == 0U && model.resets == 0U, label);
		end_scene(label);
	}

	/* A wrong magic is not GHCI. */
	prepare_scene(MODEL_KC705_UMA, NULL);
	model.window[GHCI_REG_MAGIC / 4U] = 0x12345678U;
	error = start_device();
	expect(error == ENODEV && model.resets == 0U, "versions: magic");
	end_scene("versions: magic");
}

/*
 * The checks of the attach: SCRATCH, MEM_READY, the reset and its wait,
 * the first fence, a device a kernel before left running or faulted, and
 * the order of the registers.
 */
static void
scene_attach_checks(void)
{
	struct model_switches switches;
	int error;

	/* A window whose SCRATCH does not answer is refused before anything is set. */
	memset(&switches, 0, sizeof(switches));
	switches.scratch_stuck = 1;
	error = start_scene(MODEL_KC705_UMA, &switches);
	expect(error == EIO && stub.registrations == 0U && interrupt_takes == 0U, "attach: SCRATCH");
	end_scene("attach: SCRATCH");

	/* MEM_READY a little late is waited for; one that never comes is refused. */
	memset(&switches, 0, sizeof(switches));
	switches.mem_ready_reads = 20U;
	error = start_scene(MODEL_KC705_UMA, &switches);
	expect(error == 0, "attach: MEM_READY late");
	end_scene("attach: MEM_READY late");
	switches.mem_ready_reads = 1000000U;
	error = start_scene(MODEL_KC705_UMA, &switches);
	expect(error == EIO && interrupt_takes == 0U, "attach: MEM_READY never");
	end_scene("attach: MEM_READY never");

	/* A reset that ends late is waited for; one that never ends is refused. */
	memset(&switches, 0, sizeof(switches));
	switches.reset_reads = 50U;
	error = start_scene(MODEL_KC705_UMA, &switches);
	expect(error == 0, "attach: reset late");
	end_scene("attach: reset late");
	memset(&switches, 0, sizeof(switches));
	switches.reset_stuck = 1;
	error = start_scene(MODEL_KC705_UMA, &switches);
	expect(error == EIO && interrupt_takes == 0U, "attach: reset stuck");
	end_scene("attach: reset stuck");

	/*
	 * The first reset waits for the quiesce of the watchdog a kernel before
	 * left (2^28 cycles at 100 MHz: 4 x 2684 ms + 500 ms = 11237 ms), so a
	 * reset that takes 10.5 s is not refused.
	 */
	memset(&switches, 0, sizeof(switches));
	switches.reset_reads = 10500U;
	error = start_scene(MODEL_KC705_UMA, &switches);
	expect(error == 0, "attach: reset wait from the old WDT_CYCLES");
	end_scene("attach: reset wait from the old WDT_CYCLES");

	/* An unknown clock leaves WDT_CYCLES at its default and waits 10 s for a reset. */
	prepare_scene(MODEL_KC705_UMA, NULL);
	model.window[GHCI_REG_GCLK_HZ / 4U] = 0U;
	error = start_device();
	expect(error == 0 && device.reset_wait_ms == 10000U && model_read(GHCI_REG_WDT_CYCLES) == 0x10000000U, "attach: unknown clock");
	end_scene("attach: unknown clock");

	/*
	 * The watchdog and the reset's wait: 2000 ms at 100 MHz, and four
	 * watchdog periods and 500 ms for the quiesce (8500 ms).
	 */
	error = start_scene(MODEL_KC705_UMA, NULL);
	expect(error == 0, "attach: started");
	expect(model_read(GHCI_REG_WDT_CYCLES) == TEST_WDT_CYCLES, "attach: WDT_CYCLES");
	expect(device.reset_wait_ms == 8500U, "attach: reset wait from WDT_CYCLES and GCLK_HZ");

	/* The order: the interrupt after the reset, bus mastering after the ring and before RUN. */
	expect(interrupt_takes == 1U && irq_after_reset, "attach: interrupt after the reset");
	expect(master_on_calls == 1U && master_before_run && ring_before_master, "attach: bus master after the ring, before RUN");
	expect(model.dma_without_master == 0U, "attach: no fetch without bus master");
	expect((model.ctrl & (GHCI_CTRL_RUN | GHCI_CTRL_WDT_EN | GHCI_CTRL_IRQ_GLOBAL)) ==
	    (GHCI_CTRL_RUN | GHCI_CTRL_WDT_EN | GHCI_CTRL_IRQ_GLOBAL), "attach: CTRL");
	expect(model_read(GHCI_REG_IRQ_ENABLE) == (GHCI_IRQ_FENCE | GHCI_IRQ_FAULT | GHCI_IRQ_WATCHDOG), "attach: IRQ_ENABLE");
	expect(model_count_commands(GHCI_OP_FENCE) == 1U && device.fence_interrupts == 1U, "attach: first fence and its interrupt");
	expect(stub.registrations == 1U && stub.register_errors == 0U && device.ops.capabilities == 0U, "attach: registered without capabilities");
	expect(strcmp(device.name, "ghci:10ee:7024") == 0, "attach: name");
	end_scene("attach: started");

	/* A GPU that never fetches the ring: the first fence does not come, and the attach undoes itself. */
	test_master_broken = 1;
	error = start_scene(MODEL_KC705_UMA, NULL);
	expect(error == EIO && stub.registrations == 0U, "attach: no first fence");
	expect(interrupt_releases == 1U && master_off_calls == 1U && (model.ctrl & GHCI_CTRL_RUN) == 0U, "attach: undone");
	expect(device.ring.memory == NULL && device.fence_page == NULL && device.heap == NULL, "attach: memory given back");
	end_scene("attach: no first fence");

	/* A device a kernel before this one left faulted, or running its ring, is reset and taken. */
	prepare_scene(MODEL_KC705_UMA, NULL);
	model.state = MODEL_FAULTED;
	model.ctrl = GHCI_CTRL_RUN;
	error = start_device();
	expect(error == 0 && model.state == MODEL_RUNNING, "attach: left faulted");
	end_scene("attach: left faulted");
	prepare_scene(MODEL_KC705_UMA, NULL);
	model.state = MODEL_RUNNING;
	model.ctrl = GHCI_CTRL_RUN;
	model.ring_wptr = 17U;
	error = start_device();
	expect(error == 0 && model.ring_rptr == model.ring_wptr, "attach: left running");
	end_scene("attach: left running");
}

/* The executor and CP_HEAP by profile. */
static void
scene_attach_policy(void)
{
	int error;

	/* The KC705: the executor, and CP_HEAP for every session and chunk. */
	error = start_scene(MODEL_KC705_UMA, NULL);
	expect(error == 0 && device.executor == 1, "policy: kc705 executor");
	expect(heap_bytes() == ghci_heap_device_bytes(device.caps.va_bits), "policy: kc705 CP_HEAP");
	end_scene("policy: kc705");

	/* Without shader units: the display only. */
	error = start_scene(MODEL_NO_SU, NULL);
	expect(error == 0 && device.executor == 0, "policy: no-su display only");
	end_scene("policy: no-su");

	/* Flat: the executor all the same, and CP_HEAP of session 0's root only. */
	error = start_scene(MODEL_FLAT, NULL);
	expect(error == 0 && device.executor == 1, "policy: flat executor");
	expect(heap_bytes() == 16384U, "policy: flat CP_HEAP 16 KiB");
	expect(model_count_commands(GHCI_OP_BIND_SESSION) == 0U, "policy: flat binds no session");
	end_scene("policy: flat");

	/* PROT: the executor, as without PROT. */
	error = start_scene(MODEL_PROT, NULL);
	expect(error == 0 && device.executor == 1, "policy: prot executor");
	end_scene("policy: prot");
}

/* Submissions, fences and the interrupt; the ring wraps, and a submission larger than the ring. */
static void
scene_submit(void)
{
	struct ghci_ticket ticket;
	unsigned index;
	unsigned wraps;
	uint32_t last;
	int failures;
	int error;

	/* A thousand submissions, each waited for, the ring wrapping several times. */
	error = start_scene(MODEL_KC705_UMA, NULL);
	expect(error == 0, "submit: started");
	failures = 0;
	wraps = 0U;
	last = device.ring.wptr;
	for (index = 0U; index < TEST_SUBMITS; index++) {
		error = submit_nops(TEST_SUBMIT_DWORDS, &ticket);
		if (error == 0)
			error = ghci_cp_wait(&device, &ticket, TEST_WAIT_MS);
		if (error != 0)
			failures++;
		if (device.ring.wptr < last)
			wraps++;
		last = device.ring.wptr;
	}
	expect(failures == 0, "submit: every ticket passed");
	expect(wraps >= 3U, "submit: the ring wrapped");
	expect(model.faults == 0U, "submit: no framing fault");
	expect(model_count_commands(GHCI_OP_BIND_SESSION) == 2U * TEST_SUBMITS, "submit: session bound around each");
	expect(model_count_commands(GHCI_OP_FENCE) == TEST_SUBMITS + 1U, "submit: one fence each");
	expect(device.completed == device.fence_placed, "submit: completed");

	/* A submission larger than the ring goes in pieces. */
	error = submit_nops(TEST_LARGE_DWORDS, &ticket);
	expect(error == 0, "submit: larger than the ring placed");
	error = ghci_cp_wait(&device, &ticket, TEST_WAIT_MS);
	expect(error == 0 && model.faults == 0U, "submit: larger than the ring passed");
	end_scene("submit");

	/* A 4 KiB ring: a thousand submissions meet its end, its full state and its one free slot. */
	test_ring_bytes = 4096U;
	error = start_scene(MODEL_KC705_UMA, NULL);
	expect(error == 0 && model_read(GHCI_REG_RING_SIZE) == 4096U, "small ring: started");
	failures = 0;
	wraps = 0U;
	last = device.ring.wptr;
	for (index = 0U; index < TEST_SUBMITS; index++) {
		error = submit_nops(TEST_SUBMIT_DWORDS, &ticket);
		if (error == 0)
			error = ghci_cp_wait(&device, &ticket, TEST_WAIT_MS);
		if (error != 0)
			failures++;
		if (device.ring.wptr < last)
			wraps++;
		last = device.ring.wptr;
	}
	expect(failures == 0 && wraps >= 100U && model.faults == 0U, "small ring: every ticket passed");
	model_hold(1);
	error = submit_nops(TEST_SUBMIT_DWORDS, &ticket);
	scene_action = NULL;
	expect(error == 0, "small ring: placed while held");
	model_hold(0);
	error = submit_nops(TEST_LARGE_DWORDS, &ticket);
	if (error == 0)
		error = ghci_cp_wait(&device, &ticket, TEST_WAIT_MS);
	expect(error == 0 && model.faults == 0U, "small ring: larger than the ring passed");
	end_scene("small ring");
}

/* A polled device: no interrupt is enabled, the poll moves the fences. */
static void
scene_polled(void)
{
	struct ghci_ticket ticket;
	int error;

	/* Started polled. */
	test_polled = 1;
	error = start_scene(MODEL_KC705_UMA, NULL);
	expect(error == 0 && device.polled, "polled: started");
	expect(model_read(GHCI_REG_IRQ_ENABLE) == 0U && (model.ctrl & GHCI_CTRL_IRQ_GLOBAL) == 0U, "polled: no interrupt enabled");

	/* A submission passes on the poll alone. */
	error = submit_nops(TEST_SUBMIT_DWORDS, &ticket);
	if (error == 0)
		error = ghci_cp_wait(&device, &ticket, TEST_WAIT_MS);
	expect(error == 0 && model.irq_calls == 0U, "polled: ticket passed");

	/* A fault is found by the poll too. */
	model_inject(GHCI_FAULT_KIND_AXI_ERROR, GHCI_UNIT_CP, 0U, 0x1000U, 0);
	ghci_sleep_ms(1U);
	expect(device.recovering == 1, "polled: fault found");
	ghci_service_step(&device, 0U);
	expect(device.recovering == 0 && device.state == GHCI_STATE_RUNNING && device.resets == 1U, "polled: recovered");
	end_scene("polled");
}

/*
 * A device fault: the gate closes, the processor is reset, the tickets
 * of the work in flight fail, and the next work runs.  A fault that
 * leaves the processor running (a session's fault under PROT) is halted
 * first.
 */
static void
scene_device_fault(void)
{
	struct ghci_ticket first;
	struct ghci_ticket second;
	struct ghci_ticket third;
	enum ghci_gate gate;
	int error;

	/* Two submissions the processor holds. */
	error = start_scene(MODEL_KC705_UMA, NULL);
	expect(error == 0, "fault: started");
	model_hold(1);
	error = submit_nops(TEST_SUBMIT_DWORDS, &first);
	expect(error == 0, "fault: first placed");
	error = submit_nops(TEST_SUBMIT_DWORDS, &second);
	expect(error == 0, "fault: second placed");

	/* The fault closes the gate. */
	model_inject(GHCI_FAULT_KIND_AXI_ERROR, GHCI_UNIT_CP, 0U, 0x1000U, 0);
	expect(device.recovering == 1 && device.fault_records == 1U, "fault: recorded");
	gate = ghci_cp_enter(&device, 0U, 0);
	expect(gate == GHCI_GATE_RETRY, "fault: gate closed");

	/* The recovery resets; the work in flight fails, and the device runs again. */
	ghci_service_step(&device, GHCI_WORK_RECOVER);
	expect(device.recovering == 0 && device.state == GHCI_STATE_RUNNING && device.resets == 1U, "fault: recovered");
	error = ghci_cp_wait(&device, &first, TEST_WAIT_MS);
	expect(error == EIO, "fault: first failed");
	error = ghci_cp_wait(&device, &second, TEST_WAIT_MS);
	expect(error == EIO, "fault: second failed");
	error = submit_nops(TEST_SUBMIT_DWORDS, &third);
	if (error == 0)
		error = ghci_cp_wait(&device, &third, TEST_WAIT_MS);
	expect(error == 0, "fault: next work runs");
	expect(device.storm_count == 1U && device.oom_count == 0U, "fault: counted once");

	/* The watchdog of session 0's packet is a device fault too. */
	model_inject_watchdog(0U);
	ghci_service_step(&device, GHCI_WORK_RECOVER);
	expect(device.state == GHCI_STATE_RUNNING && device.resets == 2U && device.storm_count == 2U, "fault: session 0 watchdog");
	end_scene("fault");

	/* Under PROT a session's fault leaves the processor running: it is halted before the reset. */
	error = start_scene(MODEL_PROT, NULL);
	expect(error == 0, "fault: prot started");
	model_inject(GHCI_FAULT_KIND_PAGE_FAULT, GHCI_UNIT_SU, 1U, 0x3000U, 0);
	expect(device.recovering == 1 && model.state == MODEL_RUNNING, "fault: prot session fault, processor running");
	ghci_service_step(&device, GHCI_WORK_RECOVER);
	expect(model.halts == 1U && device.resets == 1U && device.state == GHCI_STATE_RUNNING, "fault: prot halted and reset");
	end_scene("fault: prot");
}

/*
 * A fence the device wrote before a fault whose interrupt was not yet
 * taken: its ticket passes, the later one fails.
 */
static void
scene_fence_before_fault(void)
{
	struct ghci_ticket written;
	struct ghci_ticket lost;
	int error;

	/* The first submission runs with the interrupt unheard; the second is held. */
	error = start_scene(MODEL_KC705_UMA, NULL);
	expect(error == 0, "fence before fault: started");
	model_set_irq_handler(NULL, NULL);
	error = submit_nops(TEST_SUBMIT_DWORDS, &written);
	expect(error == 0, "fence before fault: first placed");
	model_hold(1);
	error = submit_nops(TEST_SUBMIT_DWORDS, &lost);
	expect(error == 0, "fence before fault: second placed");

	/* The fault, then the interrupt with both events. */
	model_inject(GHCI_FAULT_KIND_AXI_ERROR, GHCI_UNIT_CP, 0U, 0x1000U, 0);
	model_set_irq_handler(model_irq, &device);
	model_tick();
	expect(device.recovering == 1, "fence before fault: fault taken");
	expect(device.completed != written.seq, "fence before fault: no completion while recovering");

	/* The recovery: the written fence passed, the other failed. */
	ghci_service_step(&device, GHCI_WORK_RECOVER);
	error = ghci_cp_wait(&device, &written, TEST_WAIT_MS);
	expect(error == 0, "fence before fault: written ticket passed");
	error = ghci_cp_wait(&device, &lost, TEST_WAIT_MS);
	expect(error == EIO, "fence before fault: held ticket failed");
	end_scene("fence before fault");
}

/*
 * A fault record of an older recovery generation is dropped: a stale
 * exhaustion of CP_HEAP does not make a fault of the processor an
 * exhaustion.
 */
static void
scene_stale_record(void)
{
	struct ghci_fault_record *record;
	int error;

	/* One recovery moves the generation on. */
	error = start_scene(MODEL_KC705_UMA, NULL);
	expect(error == 0, "stale: started");
	model_inject(GHCI_FAULT_KIND_AXI_ERROR, GHCI_UNIT_CP, 0U, 0x1000U, 0);
	ghci_service_step(&device, GHCI_WORK_RECOVER);
	expect(device.recovery_generation == 1U && device.storm_count == 1U, "stale: first recovery");

	/* A record of the older generation, as a late handler would have left it. */
	record = &device.faults[0];
	memset(record, 0, sizeof(*record));
	record->generation = 0U;
	record->fault_info = GHCI_FAULT_KIND_MMU_OOM | (GHCI_UNIT_CP << GHCI_FAULT_UNIT_SHIFT);
	device.fault_records = 1U;

	/* A new fault of the processor: counted as a fault, not as an exhaustion. */
	model_inject(GHCI_FAULT_KIND_AXI_ERROR, GHCI_UNIT_CP, 0U, 0x1000U, 0);
	ghci_service_step(&device, GHCI_WORK_RECOVER);
	expect(device.oom_count == 0U && device.storm_count == 2U && device.state == GHCI_STATE_RUNNING, "stale: dropped");
	end_scene("stale");
}

/* A hang: work that does not move for the hang time is reset; a fence alone that does not come too. */
static void
scene_hang(void)
{
	struct ghci_ticket ticket;
	int error;

	/* The processor holds a submission. */
	error = start_scene(MODEL_KC705_UMA, NULL);
	expect(error == 0, "hang: started");
	model_hold(1);
	error = submit_nops(TEST_SUBMIT_DWORDS, &ticket);
	expect(error == 0, "hang: placed");

	/* Before the hang time nothing happens; after it the recovery resets. */
	ghci_service_step(&device, 0U);
	stub.ticks += kern_ms_to_ticks(GHCI_HANG_MS) / 2U;
	ghci_service_step(&device, 0U);
	expect(device.recovering == 0 && device.resets == 0U, "hang: not before the hang time");
	stub.ticks += kern_ms_to_ticks(GHCI_HANG_MS);
	ghci_service_step(&device, 0U);
	expect(device.resets == 1U && device.state == GHCI_STATE_RUNNING, "hang: reset");
	error = ghci_cp_wait(&device, &ticket, TEST_WAIT_MS);
	expect(error == EIO, "hang: ticket failed");

	/* A fence alone (as a VM operation's) that is not written is a hang too. */
	model_hold(1);
	error = ghci_cp_submit(&device, 0U, NULL, 0U, &ticket);
	expect(error == 0, "hang: fence placed");
	ghci_service_step(&device, 0U);
	stub.ticks += kern_ms_to_ticks(GHCI_HANG_MS) + 1U;
	ghci_service_step(&device, 0U);
	expect(device.resets == 2U, "hang: fence alone reset");
	end_scene("hang");
}

/* A long submission whose processor keeps moving RING_RPTR is not a hang. */
static void
scene_long_submit(void)
{
	struct ghci_ticket ticket;
	uint32_t packets[5U * GHCI_DW_WAIT_FENCE];
	uint64_t word_bus;
	uint8_t *word;
	uint64_t value;
	unsigned index;
	int error;

	/* A word in host memory the submission waits on, five times. */
	error = start_scene(MODEL_KC705_UMA, NULL);
	expect(error == 0, "long: started");
	error = model_host_alloc(8U, 8U, 0xffffffffU, &word_bus);
	expect(error == 0, "long: word");
	word = model_dma(word_bus, 8U);
	for (index = 0U; index < 5U; index++)
		(void)ghci_cmd_wait_fence(&packets[index * GHCI_DW_WAIT_FENCE], word_bus, index + 1U);
	error = ghci_cp_submit(&device, 0U, packets, 5U * GHCI_DW_WAIT_FENCE, &ticket);
	expect(error == 0, "long: placed");

	/* Each half hang time the word moves on, and the processor with it. */
	for (index = 0U; index < 5U; index++) {
		ghci_service_step(&device, 0U);
		stub.ticks += kern_ms_to_ticks(GHCI_HANG_MS) * 3U / 4U;
		ghci_service_step(&device, 0U);
		value = index + 1U;
		memcpy(word, &value, 8U);
		model_tick();
	}
	error = ghci_cp_wait(&device, &ticket, TEST_WAIT_MS);
	expect(error == 0 && device.resets == 0U, "long: not a hang");
	end_scene("long");
}

/*
 * A wait for the ring's room ends only when RING_RPTR moves or a
 * recovery begins: the clock running far past the hang time does not end
 * it, a fault does, and the submission placed in part fails.
 */
static void
scene_room_wait(void)
{
	struct ghci_ticket ticket;
	unsigned sleeps;
	int error;

	/* The processor holds the ring; a submission larger than the ring waits for room. */
	error = start_scene(MODEL_KC705_UMA, NULL);
	expect(error == 0, "room: started");
	model_hold(1);
	scene_action = action_stall_then_fault;
	sleeps = stub.sleeps;
	error = submit_nops(TEST_LARGE_DWORDS, &ticket);
	scene_action = NULL;
	expect(error == EIO, "room: placed in part, failed");
	expect(scene_action_runs >= 100U && stub.sleeps - sleeps >= 100U, "room: no timeout of its own");
	expect(device.recovering == 1, "room: recovery pending");

	/* The recovery, then the ring takes work again. */
	ghci_service_step(&device, GHCI_WORK_RECOVER);
	error = submit_nops(TEST_SUBMIT_DWORDS, &ticket);
	if (error == 0)
		error = ghci_cp_wait(&device, &ticket, TEST_WAIT_MS);
	expect(error == 0 && device.state == GHCI_STATE_RUNNING, "room: runs again");
	end_scene("room");
}

/*
 * The gate: a caller holding the device's and the display's locks lets
 * both go while a recovery runs, and starts again; nothing waits at the
 * gate holding a lock.
 */
static void
scene_gate(void)
{
	enum ghci_gate gate;
	int error;

	/* A fault the service has not taken yet. */
	error = start_scene(MODEL_KC705_UMA, NULL);
	expect(error == 0, "gate: started");
	model_inject(GHCI_FAULT_KIND_AXI_ERROR, GHCI_UNIT_CP, 0U, 0x1000U, 0);

	/* The caller waits at the gate; the recovery runs meanwhile and takes every lock. */
	scene_action = action_recover;
	mutex_lock(&device.lock);
	mutex_lock(&device.display_lock);
	gate = ghci_cp_enter(&device, GHCI_HOLD_DEVICE | GHCI_HOLD_DISPLAY, 1);
	scene_action = NULL;
	expect(gate == GHCI_GATE_RETRY, "gate: retry after the recovery");
	expect(stub.held_count == 0U && stub.double_locks == 0U, "gate: every lock let go");
	expect(device.recovering == 0 && device.resets == 1U, "gate: recovery ran while waiting");

	/* The gate is open again. */
	gate = ghci_cp_enter(&device, 0U, 1);
	expect(gate == GHCI_GATE_OPEN && stub_holds("ghci ring"), "gate: open");
	ghci_cp_leave(&device);
	end_scene("gate");
}

/*
 * The loss of a device: checks that fail three times in a row; the gate
 * then answers LOST to whoever waits, submissions and opens fail, and the
 * core hears of it.
 */
static void
scene_lost(void)
{
	struct ghci_ticket ticket;
	enum ghci_gate gate;
	void *session;
	int error;

	/* A fault, and a GPU that fetches nothing after it: every check fails. */
	error = start_scene(MODEL_KC705_UMA, NULL);
	expect(error == 0, "lost: started");
	model_hold(1);
	error = submit_nops(TEST_SUBMIT_DWORDS, &ticket);
	expect(error == 0, "lost: placed");
	model_inject(GHCI_FAULT_KIND_AXI_ERROR, GHCI_UNIT_CP, 0U, 0x1000U, 0);
	model_set_bus_master(0);

	/* A caller waits at the gate while the recovery fails three times. */
	scene_action = action_recover;
	gate = ghci_cp_enter(&device, 0U, 1);
	scene_action = NULL;
	expect(gate == GHCI_GATE_LOST, "lost: the gate answers LOST");
	expect(device.state == GHCI_STATE_LOST && device.recovery_failures == GHCI_RECOVERY_TRIES, "lost: three checks failed");
	expect(device.resets == GHCI_RECOVERY_TRIES && stub.errors_reported == 1U, "lost: reported");

	/* Everything after fails. */
	error = ghci_cp_wait(&device, &ticket, TEST_WAIT_MS);
	expect(error == EIO, "lost: pending ticket failed");
	error = submit_nops(TEST_SUBMIT_DWORDS, &ticket);
	expect(error == EIO, "lost: submission refused");
	error = device.ops.open(&device, &session);
	expect(error == EIO, "lost: open refused");

	/* The detach stops a lost device and gives its memory back once bus mastering is off. */
	error = ghci_device_stop(&device);
	expect(error == 0 && device.state == GHCI_STATE_DETACHED && device.ring.memory == NULL, "lost: detached");
	end_scene("lost");

	/* A reset that never ends loses the device at once. */
	error = start_scene(MODEL_KC705_UMA, NULL);
	expect(error == 0, "lost: started again");
	model.switches.reset_stuck = 1;
	model_inject(GHCI_FAULT_KIND_AXI_ERROR, GHCI_UNIT_CP, 0U, 0x1000U, 0);
	ghci_service_step(&device, GHCI_WORK_RECOVER);
	expect(device.state == GHCI_STATE_LOST && device.resets == 0U, "lost: reset stuck");
	model.switches.reset_stuck = 0;
	model.reset_hung = 0;
	end_scene("lost: reset stuck");
}

/*
 * Faults that come too often: three faults no session caused within a
 * minute, or two exhaustions of CP_HEAP, lose the device; faults spread
 * wider do not.
 */
static void
scene_storm(void)
{
	unsigned index;
	int error;

	/* Three device faults within a minute. */
	error = start_scene(MODEL_KC705_UMA, NULL);
	expect(error == 0, "storm: started");
	for (index = 0U; index < 3U; index++) {
		model_inject(GHCI_FAULT_KIND_AXI_ERROR, GHCI_UNIT_CP, 0U, 0x1000U, 0);
		ghci_service_step(&device, GHCI_WORK_RECOVER);
	}
	expect(device.state == GHCI_STATE_LOST && device.resets == 2U, "storm: lost at the third");
	end_scene("storm");

	/* Two exhaustions of CP_HEAP within a minute. */
	error = start_scene(MODEL_KC705_UMA, NULL);
	expect(error == 0, "storm: oom started");
	for (index = 0U; index < 2U; index++) {
		model_inject(GHCI_FAULT_KIND_MMU_OOM, GHCI_UNIT_CP, 0U, 0U, 0);
		ghci_service_step(&device, GHCI_WORK_RECOVER);
	}
	expect(device.state == GHCI_STATE_LOST && device.resets == 1U && device.storm_count == 0U, "storm: lost at the second exhaustion");
	end_scene("storm: oom");

	/* Three device faults more than a minute apart. */
	error = start_scene(MODEL_KC705_UMA, NULL);
	expect(error == 0, "storm: spread started");
	for (index = 0U; index < 3U; index++) {
		model_inject(GHCI_FAULT_KIND_AXI_ERROR, GHCI_UNIT_CP, 0U, 0x1000U, 0);
		ghci_service_step(&device, GHCI_WORK_RECOVER);
		stub.ticks += kern_ms_to_ticks(GHCI_FAULT_STORM_MS / 2U + 1U);
	}
	expect(device.state == GHCI_STATE_RUNNING && device.resets == 3U, "storm: spread faults recovered");
	end_scene("storm: spread");
}

/*
 * The detach: refused while a session is open; it resets the GPU, lets
 * the interrupt go and turns bus mastering off; it keeps bus mastering
 * the firmware left on; and it waits for a recovery in progress.
 */
static void
scene_detach(void)
{
	void *session;
	int error;

	/* A session keeps the device. */
	error = start_scene(MODEL_KC705_UMA, NULL);
	expect(error == 0, "detach: started");
	error = device.ops.open(&device, &session);
	expect(error == 0, "detach: session opened");
	error = ghci_device_stop(&device);
	expect(error == EBUSY, "detach: refused with a session");
	device.ops.close(&device, session);

	/* The detach. */
	error = ghci_device_stop(&device);
	expect(error == 0 && device.state == GHCI_STATE_DETACHED, "detach: stopped");
	expect(model.ctrl == 0U && model_read(GHCI_REG_IRQ_ENABLE) == 0U, "detach: reset and quiet");
	expect(interrupt_releases == 1U && model.irq_handler == NULL, "detach: interrupt let go");
	expect(master_off_calls == 1U && model.bus_master == 0, "detach: bus master off");
	expect(release_after_quiet && master_off_after_release, "detach: reset, quiet, interrupt, then bus master");
	expect(device.ring.memory == NULL && device.fence_page == NULL && device.heap == NULL, "detach: memory given back");
	expect(stub.unregistrations == 1U, "detach: unregistered");
	end_scene("detach");

	/* An interrupt whose handler may still run keeps the device and its memory; a second try ends. */
	error = start_scene(MODEL_KC705_UMA, NULL);
	expect(error == 0, "detach: busy started");
	test_release_busy = 1U;
	error = ghci_device_stop(&device);
	expect(error == EBUSY && device.ring.memory != NULL && device.interrupt_taken, "detach: handler busy, kept");
	error = ghci_device_stop(&device);
	expect(error == 0 && device.ring.memory == NULL && interrupt_releases == 1U, "detach: second try");
	end_scene("detach: busy");

	/* Bus mastering the firmware left on stays on. */
	test_master_found = 1;
	error = start_scene(MODEL_KC705_UMA, NULL);
	expect(error == 0 && master_on_calls == 0U, "detach: firmware's bus master kept at the start");
	error = ghci_device_stop(&device);
	expect(error == 0 && master_off_calls == 0U && model.bus_master == 1, "detach: firmware's bus master kept at the stop");
	end_scene("detach: firmware bus master");

	/* A recovery in progress ends before the detach drives the GPU. */
	error = start_scene(MODEL_KC705_UMA, NULL);
	expect(error == 0, "detach: recovery started");
	model_inject(GHCI_FAULT_KIND_AXI_ERROR, GHCI_UNIT_CP, 0U, 0x1000U, 0);
	scene_action = action_recover;
	error = ghci_device_stop(&device);
	scene_action = NULL;
	expect(error == 0 && device.resets == 1U && device.state == GHCI_STATE_DETACHED, "detach: after the recovery");
	end_scene("detach: recovery");
}

/*
 * The last checks of a recovery: a session's fault that latches during
 * the reset leaves FAULT_INFO set after the fence comes back, so the
 * recovery goes round again; one at every pass loses the device.
 */
static void
scene_check_fails(void)
{
	struct model_switches switches;
	int error;

	/* A device fault, and a session's fault while the reset runs. */
	memset(&switches, 0, sizeof(switches));
	switches.reset_reads = 5U;
	error = start_scene(MODEL_PROT, &switches);
	expect(error == 0, "check: started");
	model_inject(GHCI_FAULT_KIND_AXI_ERROR, GHCI_UNIT_CP, 0U, 0x1000U, 0);
	scene_action = action_session_fault_once;
	ghci_service_step(&device, GHCI_WORK_RECOVER);
	scene_action = NULL;
	expect(device.resets == 2U && device.state == GHCI_STATE_RUNNING, "check: second pass recovered");
	expect(device.recovery_failures == 0U && device.recovering == 0, "check: failures cleared");
	end_scene("check");

	/* A session's fault at every pass: the device is lost. */
	error = start_scene(MODEL_PROT, &switches);
	expect(error == 0, "check: every pass started");
	model_inject(GHCI_FAULT_KIND_AXI_ERROR, GHCI_UNIT_CP, 0U, 0x1000U, 0);
	scene_action = action_session_fault;
	ghci_service_step(&device, GHCI_WORK_RECOVER);
	scene_action = NULL;
	expect(device.state == GHCI_STATE_LOST && stub.errors_reported == 1U, "check: lost");
	end_scene("check: every pass");
}
