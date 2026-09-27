/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws044-p010: exercises ptrace(2) the way a debugger does, on AArch64 and,
 * as the regression of the shared code, on amd64.
 *
 * The child stops itself; the parent reads its registers, stops it on a
 * software breakpoint, steps one instruction, stops it on a
 * hardware watchpoint and a hardware breakpoint, changes a register, and
 * lets it exit with the value it put there.  Each check prints one line;
 * the last line is "ptrace-test: PASS" only when all of them held.
 */

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <machine/reg.h>
#include <unistd.h>

/*
 * How each architecture spells a software breakpoint and names the
 * registers the test reads: the breakpoint word written over the first
 * four bytes of a function, where the program counter is after the
 * breakpoint traps, and the first argument register.
 */
#if defined(__aarch64__)
#define BREAKPOINT_WORD(original)	((int)0xd4200000U)
#define BREAKPOINT_PC(address)		(address)
#define REG_PC(regs)			((regs).r_pc)
#define REG_SP(regs)			((regs).r_sp)
#define REG_ARG0(regs)			((regs).r_x[0])
#elif defined(__x86_64__)
#define BREAKPOINT_WORD(original)	(((original) & ~0xff) | 0xcc)
#define BREAKPOINT_PC(address)		((address) + 1U)
#define REG_PC(regs)			((regs).r_rip)
#define REG_SP(regs)			((regs).r_rsp)
#define REG_ARG0(regs)			((regs).r_rdi)
#endif

/*
 * The word the child stores to, which the watchpoint watches.
 */
volatile int watched_word;

/*
 * How often victim_tail ran.  The store keeps the compiler from dropping
 * the call, which would otherwise be seen to return its argument.
 */
volatile int tail_calls;

static int victim_store(int value) __attribute__((noinline));
static int victim_tail(int value) __attribute__((noinline));
static void run_child(void);
static int wait_stop(pid_t child, int expected_signal);
static int get_regs(pid_t child, struct reg *regs);
static int get_siginfo(pid_t child, struct ptrace_siginfo *info);
static int check(int condition, const char *what);

/*
 * Runs the tracer and reports whether every check held.
 */
int
main(void)
{
	struct ptrace_debug_points points;
	struct ptrace_siginfo info;
	struct fpreg fpregs;
	struct reg regs;
	uintptr_t store_address;
	uintptr_t tail_address;
	int original;
	int status;
	int failures;
	int error;
	pid_t child;
	pid_t waited;

	/* Names the functions the points are placed on. */
	failures = 0;
	store_address = (uintptr_t)&victim_store;
	tail_address = (uintptr_t)&victim_tail;

	/* Starts the child, which stops itself before running anything traced. */
	child = fork();
	if (child < 0) {
		perror("fork");
		return 1;
	}

	/* The child runs the traced side and never returns. */
	if (child == 0)
		run_child();

	/* Waits for the child's own stop. */
	error = wait_stop(child, SIGSTOP);
	failures += check(error == 0, "stopped with SIGSTOP");

	/* Reads the stopped child's registers. */
	error = get_regs(child, &regs);
	failures += check(error == 0 && REG_PC(regs) != 0 && REG_SP(regs) != 0,
	    "PT_GETREGS gives a program counter and a stack pointer");
	error = ptrace(PT_GETFPREGS, child, (void *)&fpregs, 0);
	failures += check(error == 0, "PT_GETFPREGS");

	/* Plants a breakpoint at the first instruction of victim_store. */
	errno = 0;
	original = ptrace(PT_READ_I, child, (void *)store_address, 0);
	failures += check(errno == 0, "PT_READ_I reads the text");
	error = ptrace(PT_WRITE_I, child, (void *)store_address,
	    BREAKPOINT_WORD(original));
	failures += check(error == 0, "PT_WRITE_I plants a breakpoint in the text");

	/* Runs the child into the software breakpoint. */
	error = ptrace(PT_CONTINUE, child, (void *)1, 0);
	failures += check(error == 0, "PT_CONTINUE");
	error = wait_stop(child, SIGTRAP);
	failures += check(error == 0, "stopped with SIGTRAP at the breakpoint");
	error = get_regs(child, &regs);
	failures += check(error == 0 &&
	    REG_PC(regs) == BREAKPOINT_PC(store_address),
	    "the program counter is at the breakpoint");
	error = get_siginfo(child, &info);
	failures += check(error == 0 && info.psi_siginfo.si_code == TRAP_BRKPT,
	    "the stop is a software breakpoint (TRAP_BRKPT)");

	/* Restores the instruction, returns to it, and steps over it. */
	REG_PC(regs) = store_address;
	error = ptrace(PT_SETREGS, child, (void *)&regs, 0);
	failures += check(error == 0, "PT_SETREGS returns to the breakpoint");
	error = ptrace(PT_WRITE_I, child, (void *)store_address, original);
	failures += check(error == 0, "PT_WRITE_I restores the instruction");
	error = ptrace(PT_STEP, child, (void *)1, 0);
	failures += check(error == 0, "PT_STEP");
	error = wait_stop(child, SIGTRAP);
	failures += check(error == 0, "stopped with SIGTRAP after one step");
	error = get_regs(child, &regs);
	failures += check(error == 0 && REG_PC(regs) > store_address &&
	    REG_PC(regs) <= store_address + 16U,
	    "the step ran one instruction");
	error = get_siginfo(child, &info);
	failures += check(error == 0 && info.psi_siginfo.si_code == TRAP_TRACE,
	    "the stop is a step (TRAP_TRACE)");

	/* Watches the word the child stores to. */
	memset(&points, 0, sizeof(points));
	points.pdps_count = 1;
	points.pdps_point[0].pdp_address = (uintptr_t)&watched_word;
	points.pdps_point[0].pdp_length = sizeof(watched_word);
	points.pdps_point[0].pdp_kind = PTRACE_DEBUG_WRITE;
	error = ptrace(PT_SET_DEBUG_POINTS, child, (void *)&points, 0);
	failures += check(error == 0, "PT_SET_DEBUG_POINTS sets a watchpoint");
	error = ptrace(PT_CONTINUE, child, (void *)1, 0);
	failures += check(error == 0, "PT_CONTINUE to the store");
	error = wait_stop(child, SIGTRAP);
	failures += check(error == 0, "stopped with SIGTRAP at the store");
	error = get_siginfo(child, &info);
	failures += check(error == 0 &&
	    info.psi_siginfo.si_code == TRAP_HWBKPT &&
	    (uintptr_t)info.psi_siginfo.si_addr == (uintptr_t)&watched_word,
	    "the watchpoint names the watched word (TRAP_HWBKPT)");

	/* Replaces the watchpoint with a breakpoint on victim_tail and steps past the store. */
	memset(&points, 0, sizeof(points));
	points.pdps_count = 1;
	points.pdps_point[0].pdp_address = tail_address;
	points.pdps_point[0].pdp_length = 1;
	points.pdps_point[0].pdp_kind = PTRACE_DEBUG_EXECUTE;
	error = ptrace(PT_SET_DEBUG_POINTS, child, (void *)&points, 0);
	failures += check(error == 0, "PT_SET_DEBUG_POINTS sets a breakpoint");
	error = ptrace(PT_STEP, child, (void *)1, 0);
	failures += check(error == 0, "PT_STEP over the watched store");
	error = wait_stop(child, SIGTRAP);
	failures += check(error == 0, "stopped after the store");

	/* Runs to the hardware breakpoint. */
	error = ptrace(PT_CONTINUE, child, (void *)1, 0);
	failures += check(error == 0, "PT_CONTINUE to victim_tail");
	error = wait_stop(child, SIGTRAP);
	failures += check(error == 0, "stopped with SIGTRAP at victim_tail");
	error = get_regs(child, &regs);
	failures += check(error == 0 && REG_PC(regs) == tail_address,
	    "the hardware breakpoint stops at victim_tail");
	error = get_siginfo(child, &info);
	failures += check(error == 0 && info.psi_siginfo.si_code == TRAP_HWBKPT,
	    "the stop is a hardware breakpoint (TRAP_HWBKPT)");

	/* Clears the points, changes the argument and lets the child exit with it. */
	memset(&points, 0, sizeof(points));
	error = ptrace(PT_SET_DEBUG_POINTS, child, (void *)&points, 0);
	failures += check(error == 0, "PT_SET_DEBUG_POINTS clears the points");
	REG_ARG0(regs) = 7;
	error = ptrace(PT_SETREGS, child, (void *)&regs, 0);
	failures += check(error == 0, "PT_SETREGS writes the argument register");
	error = ptrace(PT_CONTINUE, child, (void *)1, 0);
	failures += check(error == 0, "PT_CONTINUE to the exit");

	/* Collects the exit status the changed register decided. */
	status = 0;
	waited = waitpid(child, &status, 0);
	if (waited != child)
		status = -1;
	failures += check(WIFEXITED(status) && WEXITSTATUS(status) == 7,
	    "the child exits with the value written into the register");

	/* Reports the outcome. */
	if (failures != 0) {
		printf("ptrace-test: FAIL (%d)\n", failures);
		return 1;
	}

	/* Reports that every check held. */
	printf("ptrace-test: PASS\n");
	return 0;
}

/*
 * Stores to the watched word; the breakpoints are planted here.
 */
static int
victim_store(
	int value)
{
	watched_word = value;

	/* Returns what the tail makes of the value. */
	return victim_tail(value);
}

/*
 * Returns its argument; the hardware breakpoint stops here.
 */
static int
victim_tail(
	int value)
{
	int calls;

	/*
	 * Counts the call.  The count is read back and tested so that the
	 * compiler cannot see that the argument is returned unchanged and
	 * use the caller's constant instead of the result.
	 */
	tail_calls++;
	calls = tail_calls;
	if (calls < 0)
		value = -value;

	/* Returns the value the debugger may have changed. */
	return value;
}

/*
 * Runs the traced child: stops itself, then exits with what victim_store returns.
 */
static void
run_child(void)
{
	int value;
	int error;

	/* Asks to be traced and waits for the tracer. */
	error = ptrace(PT_TRACE_ME, 0, NULL, 0);
	if (error != 0)
		_exit(100);
	kill(getpid(), SIGSTOP);

	/* Exits with the value the tracer arranged. */
	value = victim_store(1);
	_exit(value);
}

/*
 * Waits until the child stops with the expected signal.
 */
static int
wait_stop(
	pid_t child,
	int expected_signal)
{
	int status;
	int stopped;
	int signal_number;
	pid_t waited;

	/* Waits for the next change of the child. */
	waited = waitpid(child, &status, 0);
	if (waited != child)
		return -1;

	/* Requires a stop with the expected signal. */
	stopped = WIFSTOPPED(status);
	if (!stopped)
		return -1;
	signal_number = WSTOPSIG(status);
	if (signal_number != expected_signal)
		return -1;

	/* Succeeded. */
	return 0;
}

/*
 * Reads the child's integer registers.
 */
static int
get_regs(
	pid_t child,
	struct reg *regs)
{
	int error;

	/* Asks the kernel for the registers of the stopped thread. */
	error = ptrace(PT_GETREGS, child, (void *)regs, 0);
	if (error != 0)
		return -1;

	/* Succeeded. */
	return 0;
}

/*
 * Reads what the signal that stopped the child said.
 */
static int
get_siginfo(
	pid_t child,
	struct ptrace_siginfo *info)
{
	int error;

	/* Asks the kernel for the signal information of the stop. */
	error = ptrace(PT_GET_SIGINFO, child, (void *)info, 0);
	if (error != 0)
		return -1;

	/* Succeeded. */
	return 0;
}

/*
 * Prints one check and counts it when it failed.
 */
static int
check(
	int condition,
	const char *what)
{
	/* Reports a check that held. */
	if (condition) {
		printf("ok: %s\n", what);
		return 0;
	}

	/* Reports a check that failed. */
	printf("FAIL: %s\n", what);
	return 1;
}
