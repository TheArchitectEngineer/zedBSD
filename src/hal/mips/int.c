/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The MIPS trap handling: the dispatch of each exception trap.S saves a
 * frame for, by Cause.ExcCode, and the Status every return erets with.
 *
 * The interrupts are the timer of coprocessor 0 and the board's
 * controller.  A system call is answered by the HAL's own check until
 * the kernel takes them.  Every other exception is a fault no handler
 * exists for yet; it is reported with the frame and the processor stops.
 */

#include <hal/hal.h>
#include "asm.h"
#include "bsp.h"
#include "clock.h"
#include "cpu.h"
#include "status.h"
#include "trap.h"

/*
 * The system call number the trap self-test makes, and the answer the
 * handler gives it.  Neither is a call of the kernel's.
 */
#define TRAP_SELFTEST_CALL	0x5a17c0deU
#define TRAP_SELFTEST_ANSWER	0xa5e83f21U

/*
 * The top of the kernel stack an exception from user mode moves to.
 *
 * Read by trap.S at every entry from user mode; set by the task switch
 * for the task it resumes.  Zero while no task has entered user mode.
 */
uint32_t mips_kernel_stack_pointer;

/*
 * The interrupts the dispatch found no source for, since boot.  Kept for
 * the debugger; it only grows.
 */
static volatile uint32_t trap_spurious_interrupts;

static void trap_interrupt(struct mips_trap_frame *frame);
static void trap_system_call(struct mips_trap_frame *frame);
static void trap_fault(const struct mips_trap_frame *frame, uint32_t code) __attribute__((noreturn));
static uint32_t trap_selftest_call(uint32_t number, uint32_t *error);

/*
 * Handles the exception whose frame trap.S saved.
 *
 * On return the frame's Status is the one the eret uses: rebuilt for a
 * return to user, the kernel's own with today's interrupt mask for a
 * return to the kernel.
 */
void
mips_trap(
	struct mips_trap_frame *frame)
{
	uint32_t code;
	uint32_t current;

	/* The exception's code, from Cause. */
	code = (frame->cause >> MIPS_CAUSE_CODE_SHIFT) & MIPS_CAUSE_CODE_MASK;

	/* Hands the exception to its handler. */
	switch (code) {
	case MIPS_EXCEPTION_INTERRUPT:
		trap_interrupt(frame);
		break;
	case MIPS_EXCEPTION_SYSTEM_CALL:
		trap_system_call(frame);
		break;
	default:
		trap_fault(frame, code);
	}

	/* Builds the Status of the return; a saved user Status is never trusted. */
	current = mips_read_status();
	if ((frame->status & MIPS_STATUS_MODE_MASK) != 0) {
		frame->status = mips_user_status(current, false, false);
	} else {
		frame->status = mips_kernel_status(frame->status, current);
	}
}

/*
 * Checks that a system call from the kernel goes through the trap entry
 * and comes back with its answer and past its instruction.
 */
void
mips_trap_selftest(
	void)
{
	uint32_t answer;
	uint32_t error;

	/* Makes the test's call, which only this file's handler answers. */
	error = 1;
	answer = trap_selftest_call(TRAP_SELFTEST_CALL, &error);
	if (answer != TRAP_SELFTEST_ANSWER)
		HAL_FATAL("mips trap selftest answer");
	if (error != 0)
		HAL_FATAL("mips trap selftest error mark");

	hal_puts("MIPS32 HAL TRAP PASS\n");
}

/* Serves every interrupt line that is both pending and unmasked. */
static void
trap_interrupt(
	struct mips_trap_frame *frame)
{
	uint32_t pending;
	bool served;

	/* The lines pending at the exception that the interrupted code let in. */
	pending = frame->cause & frame->status & MIPS_CAUSE_PENDING_MASK;
	served = false;

	/* The tick of coprocessor 0. */
	if ((pending & MIPS_INTERRUPT_TIMER) != 0) {
		mips_clock_interrupt();
		served = true;
	}

	/* The board's interrupt controller. */
	if ((pending & MIPS_INTERRUPT_BOARD) != 0) {
		mips_board_interrupt();
		served = true;
	}

	/* Counts an interrupt with no line the HAL serves. */
	if (!served)
		trap_spurious_interrupts++;
}

/*
 * Answers a system call.
 *
 * Only the self-test's call exists until the kernel takes system calls;
 * any other number is a fault.  The result is v0, the error mark a3, and
 * the return goes past the syscall instruction.
 */
static void
trap_system_call(
	struct mips_trap_frame *frame)
{
	/* Refuses a call nothing answers yet. */
	if (frame->r[MIPS_REGISTER_V0] != TRAP_SELFTEST_CALL)
		trap_fault(frame, MIPS_EXCEPTION_SYSTEM_CALL);

	/* Answers the self-test and moves past the instruction. */
	frame->r[MIPS_REGISTER_V0] = TRAP_SELFTEST_ANSWER;
	frame->r[MIPS_REGISTER_A3] = 0;
	frame->exception_pc += 4;
}

/* Reports an exception no handler takes, and stops. */
static void
trap_fault(
	const struct mips_trap_frame *frame,
	uint32_t code)
{
	/* Prints the frame's exception registers and the stack pointer. */
	hal_printf("MIPS32 TRAP FAULT code=%u cause=%08x epc=%08x badvaddr=%08x status=%08x sp=%08x ra=%08x\n",
		   code,
		   frame->cause,
		   frame->exception_pc,
		   frame->bad_address,
		   frame->status,
		   frame->r[MIPS_REGISTER_SP],
		   frame->r[31]);

	/* Stops for good. */
	mips_halt_forever();
}

/* Makes a system call from the kernel with a number, reporting v0 and the a3 mark. */
static uint32_t
trap_selftest_call(
	uint32_t number,
	uint32_t *error)
{
	register uint32_t call_number __asm__("$2");
	register uint32_t mark __asm__("$7");

	/* Traps with the number in v0; the handler answers in v0 and a3. */
	call_number = number;
	mark = *error;
	__asm__ volatile("syscall" : "+r"(call_number), "+r"(mark) : : "memory");
	*error = mark;

	/* Succeeded: the handler's answer. */
	return call_number;
}
