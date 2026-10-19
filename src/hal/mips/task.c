/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The MIPS tasks: the stack a kernel task first switches to, the stop of
 * a task whose function returned, and the check of the context switch
 * before anything relies on it.
 */

#include <hal/hal.h>
#include "asm.h"
#include "cpu.h"
#include "spin.h"
#include "task.h"

/* The argument the self-test's kernel task is started with. */
#define SELFTEST_ARGUMENT	0x6b3d9e15U

/*
 * The context self-test's own stack, the two saved stack pointers, the
 * turns the test task counts and the argument its kernel entry saw.
 * Used once, before anything else switches.
 */
static uint8_t self_stack[4096] __attribute__((aligned(16)));
static uintptr_t self_main_sp;
static uintptr_t self_task_sp;
static unsigned self_count;
static uint32_t self_argument;

static uintptr_t build_stack(uintptr_t return_address, uintptr_t s0, uintptr_t s1);
static void self_task(void);
static void self_entry_task(void *argument);

/*
 * Stops the processor when a kernel task's function returns, which a
 * task must never do.
 */
void
mips_task_returned(
	void)
{
	/* Reports the task, and stops: it has nothing to go back to. */
	hal_puts("fatal: mips kernel task returned\n");
	mips_halt_forever();
}

/*
 * Checks the context switch: a task built by hand is entered, yields and
 * is resumed, and a kernel task started through its first entry sees its
 * argument; Status comes back from each switch as it was but for IE,
 * which the first entry of a kernel task turns on.
 */
void
mips_context_selftest(
	void)
{
	uint32_t before;
	uint32_t after;
	bool enabled;

	/* Runs the test with interrupts off, as the switches of the kernel do. */
	enabled = mips_irq_save();
	before = mips_read_status();

	/* The first switch runs the hand-built task to its first yield. */
	self_task_sp = build_stack((uintptr_t)self_task, 0, 0);
	mips_task_dispatch(&self_main_sp, &self_task_sp);
	if (self_count != 1)
		HAL_FATAL("mips context selftest first switch");

	/* The second switch resumes it from that yield. */
	mips_task_dispatch(&self_main_sp, &self_task_sp);
	if (self_count != 2)
		HAL_FATAL("mips context selftest second switch");

	/* A switch leaves Status as it was. */
	after = mips_read_status();
	if (after != before)
		HAL_FATAL("mips context selftest status changed");

	/* A kernel task's first entry passes its argument and yields back once. */
	self_task_sp = build_stack((uintptr_t)mips_kernel_task_entry,
				   (uintptr_t)self_entry_task,
				   (uintptr_t)SELFTEST_ARGUMENT);
	mips_task_dispatch(&self_main_sp, &self_task_sp);
	if (self_argument != SELFTEST_ARGUMENT)
		HAL_FATAL("mips context selftest kernel task argument");

	/* The kernel task's entry turned IE on and nothing else. */
	after = mips_read_status();
	if ((after & ~(uint32_t)MIPS_STATUS_INTERRUPTS) != (before & ~(uint32_t)MIPS_STATUS_INTERRUPTS))
		HAL_FATAL("mips context selftest kernel task status");

	/* Puts the interrupt state back as the test found it. */
	(void)mips_irq_save();
	mips_irq_restore(enabled);

	hal_puts("MIPS32 HAL CONTEXT PASS\n");
}

/* Builds a stack that mips_task_dispatch() pops into a return address with s0 and s1 set. */
static uintptr_t
build_stack(
	uintptr_t return_address,
	uintptr_t s0,
	uintptr_t s1)
{
	uintptr_t *words;

	/* The dispatch words sit below the home of the first function's arguments. */
	words = (uintptr_t *)(self_stack + sizeof(self_stack) - MIPS_ARGUMENT_HOME_SIZE);
	words -= MIPS_DISPATCH_WORDS;
	hal_memset(words, 0, MIPS_DISPATCH_WORDS * sizeof(*words));
	words[0] = s0;
	words[1] = s1;
	words[MIPS_DISPATCH_RA] = return_address;

	/* Succeeded: the stack pointer the dispatch loads. */
	return (uintptr_t)words;
}

/* Counts two turns, yielding back to the test after each. */
static void
self_task(
	void)
{
	/* The first turn. */
	self_count = 1;
	mips_task_dispatch(&self_task_sp, &self_main_sp);

	/* The second turn. */
	self_count = 2;
	mips_task_dispatch(&self_task_sp, &self_main_sp);

	/* The test never resumes the task a third time. */
	mips_halt_forever();
}

/* Records the argument its first entry passed, and yields back to the test for good. */
static void
self_entry_task(
	void *argument)
{
	/* Keeps the argument for the test to compare. */
	self_argument = (uint32_t)(uintptr_t)argument;
	mips_task_dispatch(&self_task_sp, &self_main_sp);

	/* The test never resumes the task again. */
	mips_halt_forever();
}
