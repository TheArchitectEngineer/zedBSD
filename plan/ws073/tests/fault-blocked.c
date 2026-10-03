/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * BUG-086 probe: a fault whose signal is blocked or ignored kills the
 * process with that signal instead of retaking the same instruction for
 * ever, and a fault whose signal has a handler still reaches it.
 *
 *   fault-blocked
 *
 * Every case runs in a child.  The child arms a 5 second alarm, so a
 * kernel that retakes the fault for ever shows up as a death by SIGALRM
 * rather than a hang.  Prints FAULT:PASS and exits 0 when every case ends
 * as expected.
 */

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* How a case sets up the signal of its fault before it faults. */
enum fault_setup {
	FAULT_BLOCKED,
	FAULT_IGNORED,
	FAULT_HANDLED_BLOCKED,
	FAULT_HANDLED
};

/* Which fault a case takes. */
enum fault_kind {
	FAULT_NULL_READ,
	FAULT_DIVIDE,
	FAULT_ILLEGAL
};

/*
 * The number of cases that ended differently from what they expected.
 *
 * Only the parent counts; each child reports through its wait status.
 */
static int failures;

/*
 * The signal and code the handler of a handled case must see.
 *
 * The child sets them before it faults; the handler compares.
 */
static volatile int expected_signo;
static volatile int expected_code;

/*
 * Whether the handled case's fault reads address zero.
 *
 * Only then does the handler check the reported address.
 */
static volatile int expected_null_address;

static void fault_handler(int signo, siginfo_t *info, void *context);
static void take_fault(enum fault_kind kind);
static void set_up_signal(int signo, enum fault_setup setup);
static void run_child(enum fault_kind kind, int signo, enum fault_setup setup);
static int case_passed(int status, int signo, enum fault_setup setup);
static void run_case(const char *name, enum fault_kind kind, int signo, enum fault_setup setup);

/*
 * Runs every case and reports the outcome.
 */
int
main(
	void)
{
	/* A blocked fault signal takes its default action. */
	run_case("segv-blocked", FAULT_NULL_READ, SIGSEGV, FAULT_BLOCKED);
	run_case("fpe-blocked", FAULT_DIVIDE, SIGFPE, FAULT_BLOCKED);
	run_case("ill-blocked", FAULT_ILLEGAL, SIGILL, FAULT_BLOCKED);

	/* An ignored fault signal takes its default action. */
	run_case("segv-ignored", FAULT_NULL_READ, SIGSEGV, FAULT_IGNORED);
	run_case("fpe-ignored", FAULT_DIVIDE, SIGFPE, FAULT_IGNORED);
	run_case("ill-ignored", FAULT_ILLEGAL, SIGILL, FAULT_IGNORED);

	/* A blocked fault signal with a handler takes its default action too. */
	run_case("segv-handled-blocked", FAULT_NULL_READ, SIGSEGV,
	    FAULT_HANDLED_BLOCKED);

	/* A handled fault signal reaches its handler. */
	run_case("segv-handled", FAULT_NULL_READ, SIGSEGV, FAULT_HANDLED);
	run_case("fpe-handled", FAULT_DIVIDE, SIGFPE, FAULT_HANDLED);
	run_case("ill-handled", FAULT_ILLEGAL, SIGILL, FAULT_HANDLED);

	/* Reports a failed case. */
	if (failures != 0) {
		printf("FAULT:FAIL %d cases\n", failures);
		return 1;
	}

	/* Succeeded: every case ended as expected. */
	printf("FAULT:PASS\n");
	return 0;
}

/* Checks what the fault's handler was told and ends the child with it. */
static void
fault_handler(
	int signo,
	siginfo_t *info,
	void *context)
{
	uintptr_t address;

	/* The context is not needed; the information says what faulted. */
	(void)context;

	/* The handler must have seen the fault's own signal. */
	if (signo != expected_signo)
		_exit(105);

	/* The handler must have seen the fault's code. */
	if (info->si_code != expected_code)
		_exit(106);

	/* A NULL read reports the address it read. */
	address = (uintptr_t)info->si_addr;
	if (expected_null_address && address != 0)
		_exit(107);

	/* Succeeded: the handler saw the fault. */
	_exit(0);
}

/* Takes one fault of the given kind. */
static void
take_fault(
	enum fault_kind kind)
{
	volatile int *null_pointer;
	volatile int sink;

	/* Selects the fault. */
	switch (kind) {
	case FAULT_NULL_READ:
		null_pointer = NULL;
		sink = *null_pointer;
		break;
	case FAULT_DIVIDE:
		/*
		 * The division is written out, because a compiler may turn a
		 * C division by a value it cannot see into one without idiv.
		 */
		__asm__ __volatile__(
		    "xorl %%ecx, %%ecx\n\t"
		    "movl $1, %%eax\n\t"
		    "cltd\n\t"
		    "idivl %%ecx"
		    :
		    :
		    : "eax", "ecx", "edx");
		sink = 0;
		break;
	case FAULT_ILLEGAL:
		__asm__ __volatile__("ud2");
		sink = 0;
		break;
	}

	/* The value read is not used; only the fault matters. */
	(void)sink;
}

/* Installs, ignores or blocks the signal as the case asks. */
static void
set_up_signal(
	int signo,
	enum fault_setup setup)
{
	struct sigaction action;
	sigset_t set;
	int error;

	/* Installs the handler for the handled cases. */
	if (setup == FAULT_HANDLED || setup == FAULT_HANDLED_BLOCKED) {
		memset(&action, 0, sizeof(action));
		action.sa_sigaction = fault_handler;
		action.sa_flags = SA_SIGINFO;
		sigemptyset(&action.sa_mask);
		error = sigaction(signo, &action, NULL);
		if (error != 0)
			_exit(101);
	}

	/* Ignores the signal for the ignored case. */
	if (setup == FAULT_IGNORED) {
		memset(&action, 0, sizeof(action));
		action.sa_handler = SIG_IGN;
		sigemptyset(&action.sa_mask);
		error = sigaction(signo, &action, NULL);
		if (error != 0)
			_exit(102);
	}

	/* Blocks the signal for the blocked cases. */
	if (setup == FAULT_BLOCKED || setup == FAULT_HANDLED_BLOCKED) {
		sigemptyset(&set);
		sigaddset(&set, signo);
		error = sigprocmask(SIG_BLOCK, &set, NULL);
		if (error != 0)
			_exit(103);
	}
}

/* Runs one case in the child: sets the signal up and faults. */
static void
run_child(
	enum fault_kind kind,
	int signo,
	enum fault_setup setup)
{
	/* A kernel that retakes the fault for ever dies of the alarm. */
	alarm(5);

	/* Tells the handler what the fault must report. */
	expected_signo = signo;
	expected_null_address = 0;
	if (kind == FAULT_NULL_READ) {
		expected_code = SEGV_MAPERR;
		expected_null_address = 1;
	} else if (kind == FAULT_DIVIDE) {
		expected_code = FPE_INTDIV;
	} else {
		expected_code = ILL_ILLOPC;
	}

	/* Prepares the signal, then faults; a handled case ends in its handler. */
	set_up_signal(signo, setup);
	take_fault(kind);

	/* The fault did not stop the child. */
	_exit(104);
}

/* Reports whether a child ended as its case expects. */
static int
case_passed(
	int status,
	int signo,
	enum fault_setup setup)
{
	int exited;
	int signaled;
	int exit_status;
	int term_signal;

	/* Decodes the wait status. */
	exited = WIFEXITED(status);
	signaled = WIFSIGNALED(status);
	exit_status = WEXITSTATUS(status);
	term_signal = WTERMSIG(status);

	/* A handled case exits 0 from its handler. */
	if (setup == FAULT_HANDLED) {
		if (!exited)
			return 0;
		if (exit_status != 0)
			return 0;
		return 1;
	}

	/* Every other case dies of the fault's own signal. */
	if (!signaled)
		return 0;
	if (term_signal != signo)
		return 0;

	/* Succeeded: the child died of its fault. */
	return 1;
}

/* Runs one case in a child and checks how the child ended. */
static void
run_case(
	const char *name,
	enum fault_kind kind,
	int signo,
	enum fault_setup setup)
{
	pid_t child;
	pid_t waited;
	int status;
	int passed;

	/* Starts the child. */
	fflush(stdout);
	child = fork();
	if (child < 0) {
		printf("FAULT:FAIL %s: fork\n", name);
		failures++;
		return;
	}

	/* The child runs the case and never returns. */
	if (child == 0)
		run_child(kind, signo, setup);

	/* Waits for the child to end. */
	waited = waitpid(child, &status, 0);
	if (waited != child) {
		printf("FAULT:FAIL %s: waitpid\n", name);
		failures++;
		return;
	}

	/* Reports a case that ended as expected. */
	passed = case_passed(status, signo, setup);
	if (passed) {
		printf("FAULT:ok %s\n", name);
		return;
	}

	/* Reports how a failed case ended instead. */
	printf("FAULT:FAIL %s: status 0x%x\n", name, (unsigned)status);
	failures++;
}
