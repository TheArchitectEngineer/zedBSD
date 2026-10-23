/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The armv7 user ABI test, run as init (init=/abitest).
 *
 * It is an ordinary program linked against the shared C library through
 * /lib/ld.so, so reaching main already exercises the loader's ARM
 * relocations.  It then checks, one line each on the console: system
 * calls, thread-local storage in two threads, fork and wait, the
 * floating-point registers across task switches, across a synchronous and
 * an asynchronous signal handler that overwrites them, the floating-point
 * state a new program starts with, and the machine name.  The last line
 * is "ABI PASS", or "ABI FAIL <step>" for the first step that failed.
 *
 * The floating-point registers are loaded, kept and stored by assembly,
 * so that nothing the compiler does stands between the values and the
 * kernel's switch.
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <uapi/syscall.h>
#include <unistd.h>

/* The number of the 64-bit floating-point registers of VFPv3-D32. */
#define ABI_VFP_REGISTERS 32U

/* How many times a task gives up the processor while it holds its values. */
#define ABI_YIELD_COUNT 1000U

/* How long the asynchronous signal test waits, in loop turns, at most. */
#define ABI_ALARM_SPINS 0xffffffffU

/* The status values the parent and the child of the switch test put in FPSCR. */
#define ABI_PARENT_FPSCR 0x80c0001fU
#define ABI_CHILD_FPSCR 0x02400010U

/* The status value a signal handler leaves behind in FPSCR. */
#define ABI_HANDLER_FPSCR 0x01000000U

/* The status value a program puts in FPSCR just before it replaces itself. */
#define ABI_EXEC_FPSCR 0x00c00001U

/* The path of this program, as the kernel started it. */
#define ABI_PROGRAM "/abitest"

/* The argument that makes the program the exec test's new image. */
#define ABI_EXEC_CHECK "exec-check"

/*
 * The floating-point state one test holds: the thirty-two doubleword
 * registers and the status and control register.
 */
struct abi_vfp_state {
	uint64_t d[ABI_VFP_REGISTERS];
	uint32_t fpscr;
};

/*
 * The thread-local value the TLS test reads in two threads.
 *
 * Every thread starts with the initial value from the program's TLS
 * template, and each changes only its own copy.
 */
static __thread int abi_tls_value = 7;

/*
 * Whether the asynchronous signal's handler has run.
 *
 * The SIGALRM handler sets it, and the assembly loop of the asynchronous
 * test reads it from memory on every turn; the test clears it first.
 */
static volatile sig_atomic_t abi_alarm_seen;

/*
 * Whether the synchronous signal's handler has run.
 *
 * The SIGUSR1 handler sets it; the test clears it before it signals
 * itself and reads it after.
 */
static volatile sig_atomic_t abi_signal_seen;

static int check_syscalls(void);
static int check_tls(void);
static void *tls_thread(void *argument);
static int check_fork(void);
static int check_vfp_switch(void);
static int check_vfp_signal(void);
static int check_vfp_alarm(void);
static int check_exec(void);
static int check_machine(void);
static int exec_check(void);
static void fill_pattern(struct abi_vfp_state *state, uint64_t seed, uint32_t fpscr);
static int same_state(const struct abi_vfp_state *expected, const struct abi_vfp_state *seen);
static void vfp_hold_across_yields(const struct abi_vfp_state *pattern, struct abi_vfp_state *seen, unsigned count);
static void vfp_hold_across_signal(const struct abi_vfp_state *pattern, struct abi_vfp_state *seen, int signal_number);
static void vfp_hold_until_alarm(const struct abi_vfp_state *pattern, struct abi_vfp_state *seen);
static void clobber_vfp(void);
static void signal_handler(int signal_number);
static void alarm_handler(int signal_number);
static void open_console(void);
static void report(int step, const char *name, int passed);
static void rest(void);

/*
 * Runs the ABI checks in order and reports the outcome.
 */
int
main(
	int argc,
	char **argv)
{
	int passed;
	int differs;

	/* Answers the exec test when this image is the new program. */
	differs = 1;
	if (argc > 1)
		differs = strcmp(argv[1], ABI_EXEC_CHECK);
	if (differs == 0) {
		passed = exec_check();
		if (passed)
			return 0;

		/* The new program started with another program's status. */
		return 1;
	}

	/* As init the program has no descriptors: the console becomes all three. */
	open_console();
	printf("ABI armv7 user ABI test\n");
	fflush(stdout);

	/*
	 * Each step stops the test where it fails, with the last line naming
	 * it.  1. The system calls' results and errors.
	 */
	passed = check_syscalls();
	report(1, "syscall", passed);

	/* 2. The thread-local variables, two threads and errno. */
	passed = check_tls();
	report(2, "tls", passed);

	/* 3. A child and its exit status. */
	passed = check_fork();
	report(3, "fork", passed);

	/* 4. The VFP registers across switches to another process. */
	passed = check_vfp_switch();
	report(4, "vfp-switch", passed);

	/* 5. The VFP registers across a signal handler that changes them. */
	passed = check_vfp_signal();
	report(5, "vfp-signal", passed);

	/* 6. The same across a signal that interrupts a loop. */
	passed = check_vfp_alarm();
	report(6, "vfp-alarm", passed);

	/* 7. The VFP state a new image starts with. */
	passed = check_exec();
	report(7, "exec", passed);

	/* 8. The machine name uname reports. */
	passed = check_machine();
	report(8, "uname", passed);

	/* Every step passed. */
	printf("ABI PASS\n");
	fflush(stdout);
	rest();

	/* Succeeded: rest() does not return. */
	return 0;
}

/* Checks that system calls take their arguments and return their results. */
static int
check_syscalls(
	void)
{
	static const char text[] = "ABI write\n";
	pid_t pid;
	ssize_t written;
	int closed;

	/* The process number, which is positive. */
	pid = getpid();
	if (pid <= 0)
		return 0;

	/* A write reports the length it wrote. */
	written = write(STDOUT_FILENO, text, sizeof(text) - 1U);
	if (written != (ssize_t)(sizeof(text) - 1U))
		return 0;

	/* A failing call reports its error through errno. */
	errno = 0;
	closed = close(-1);
	if (closed != -1)
		return 0;
	if (errno != EBADF)
		return 0;

	/* Succeeded: the calls behave. */
	return 1;
}

/* Checks that two threads each have their own TLS block and errno. */
static int
check_tls(
	void)
{
	pthread_t thread;
	void *thread_result;
	int error;

	/* The main thread's own copy, and an errno the other thread must not touch. */
	abi_tls_value = 11;
	errno = ENOENT;

	/* Runs the other thread to its end. */
	error = pthread_create(&thread, NULL, tls_thread, NULL);
	if (error != 0)
		return 0;

	/* Waits for it to end. */
	error = pthread_join(thread, &thread_result);
	if (error != 0)
		return 0;

	/* The other thread saw the template's value and its own errno. */
	if (thread_result != (void *)1)
		return 0;

	/* The main thread's copies are as it left them. */
	if (abi_tls_value != 11)
		return 0;
	if (errno != ENOENT)
		return 0;

	/* Succeeded: the blocks are separate. */
	return 1;
}

/* Checks the TLS block of a new thread and reports the result as its value. */
static void *
tls_thread(
	void *argument)
{
	int closed;

	(void)argument;

	/* A new thread starts with the value of the TLS template. */
	if (abi_tls_value != 7)
		return (void *)0;

	/* Its own copy changes without the main thread's. */
	abi_tls_value = 22;
	if (abi_tls_value != 22)
		return (void *)0;

	/* Its own errno takes a failing call's error. */
	errno = 0;
	closed = close(-1);
	if (closed != -1)
		return (void *)0;
	if (errno != EBADF)
		return (void *)0;

	/* Succeeded: the thread's own block. */
	return (void *)1;
}

/* Checks that a child runs and that its exit status reaches the parent. */
static int
check_fork(
	void)
{
	pid_t child;
	pid_t waited;
	int status;

	/* The child exits with a status the parent can recognize. */
	child = fork();
	if (child < 0)
		return 0;
	if (child == 0)
		_exit(42);

	/* Collects that child. */
	waited = waitpid(child, &status, 0);
	if (waited != child)
		return 0;

	/* It exited normally with its status. */
	if (!WIFEXITED(status))
		return 0;
	if (WEXITSTATUS(status) != 42)
		return 0;

	/* Succeeded: fork and wait work. */
	return 1;
}

/*
 * Checks that two processes keep their own floating-point registers while
 * they take turns on the processor.
 */
static int
check_vfp_switch(
	void)
{
	struct abi_vfp_state pattern;
	struct abi_vfp_state seen;
	pid_t child;
	pid_t waited;
	int status;
	int child_kept;
	int parent_kept;

	/* The child holds its values across its yields and reports by its status. */
	child = fork();
	if (child < 0)
		return 0;
	if (child == 0) {
		fill_pattern(&pattern, 0x6368696c64000000ULL, ABI_CHILD_FPSCR);
		vfp_hold_across_yields(&pattern, &seen, ABI_YIELD_COUNT);
		child_kept = same_state(&pattern, &seen);
		if (child_kept)
			_exit(0);
		_exit(1);
	}

	/* The parent does the same with values of its own. */
	fill_pattern(&pattern, 0x706172656e740000ULL, ABI_PARENT_FPSCR);
	vfp_hold_across_yields(&pattern, &seen, ABI_YIELD_COUNT);
	parent_kept = same_state(&pattern, &seen);

	/* Collects the child's verdict. */
	waited = waitpid(child, &status, 0);
	if (waited != child)
		return 0;
	if (!WIFEXITED(status))
		return 0;
	if (WEXITSTATUS(status) != 0) {
		printf("ABI vfp-switch: the child lost its registers\n");
		return 0;
	}

	/* The parent's verdict. */
	if (!parent_kept) {
		printf("ABI vfp-switch: the parent lost its registers\n");
		return 0;
	}

	/* Succeeded: both kept their registers. */
	return 1;
}

/*
 * Checks that a signal handler that overwrites the floating-point
 * registers leaves the interrupted code's values in place when it returns.
 */
static int
check_vfp_signal(
	void)
{
	struct abi_vfp_state pattern;
	struct abi_vfp_state seen;
	struct sigaction action;
	int same;
	int error;

	/* Installs the handler that overwrites the registers. */
	memset(&action, 0, sizeof(action));
	action.sa_handler = signal_handler;
	sigemptyset(&action.sa_mask);
	error = sigaction(SIGUSR1, &action, NULL);
	if (error != 0)
		return 0;

	/* Signals the process itself while it holds its values. */
	abi_signal_seen = 0;
	fill_pattern(&pattern, 0x7369676e616c0000ULL, ABI_PARENT_FPSCR);
	vfp_hold_across_signal(&pattern, &seen, SIGUSR1);

	/* The handler ran. */
	if (abi_signal_seen == 0)
		return 0;

	/* The values survived it. */
	same = same_state(&pattern, &seen);
	if (!same)
		return 0;

	/* Succeeded: the handler's registers did not leak back. */
	return 1;
}

/*
 * Checks the same for a signal that arrives through an interrupt while the
 * program runs in a loop of its own.
 */
static int
check_vfp_alarm(
	void)
{
	struct abi_vfp_state pattern;
	struct abi_vfp_state seen;
	struct sigaction action;
	int same;
	int error;

	/* Installs the handler that overwrites the registers. */
	memset(&action, 0, sizeof(action));
	action.sa_handler = alarm_handler;
	sigemptyset(&action.sa_mask);
	error = sigaction(SIGALRM, &action, NULL);
	if (error != 0)
		return 0;

	/* Spins with the values held until the alarm's handler has run. */
	abi_alarm_seen = 0;
	fill_pattern(&pattern, 0x616c61726d000000ULL, ABI_CHILD_FPSCR);
	(void)alarm(1);
	vfp_hold_until_alarm(&pattern, &seen);

	/* The alarm came before the loop gave up. */
	if (abi_alarm_seen == 0)
		return 0;

	/* The values survived it. */
	same = same_state(&pattern, &seen);
	if (!same)
		return 0;

	/* Succeeded: the interrupted loop kept its registers. */
	return 1;
}

/* Checks that a new program starts with the floating-point status cleared. */
static int
check_exec(
	void)
{
	static char *const arguments[] = { ABI_PROGRAM, ABI_EXEC_CHECK, NULL };
	static char *const environment[] = { NULL };
	pid_t child;
	pid_t waited;
	int status;

	/* The child sets a status of its own and replaces itself with the check. */
	child = fork();
	if (child < 0)
		return 0;
	if (child == 0) {
		__asm__ volatile("vmsr fpscr, %0" : : "r"(ABI_EXEC_FPSCR) : "memory");
		(void)execve(ABI_PROGRAM, arguments, environment);
		_exit(2);
	}

	/* Collects the new program's verdict. */
	waited = waitpid(child, &status, 0);
	if (waited != child)
		return 0;
	if (!WIFEXITED(status))
		return 0;
	if (WEXITSTATUS(status) != 0) {
		printf("ABI exec: the new program reported %d\n", WEXITSTATUS(status));
		return 0;
	}

	/* Succeeded: the new program started clean. */
	return 1;
}

/* Checks that the system names its machine armv7. */
static int
check_machine(
	void)
{
	struct utsname name;
	int different;
	int error;

	/* Asks the C library for the names. */
	error = uname(&name);
	if (error != 0)
		return 0;

	/* The machine name is the port's. */
	printf("ABI machine=%s\n", name.machine);
	different = strcmp(name.machine, "armv7");
	if (different != 0)
		return 0;

	/* Succeeded: the name is right. */
	return 1;
}

/*
 * Reports whether this new program's floating-point status is cleared.
 *
 * Only FPSCR is looked at: the loader and the C library may already have
 * used the registers, but nothing before main sets the status.
 */
static int
exec_check(
	void)
{
	uint32_t fpscr;

	/* Reads the status the program started with. */
	__asm__ volatile("vmrs %0, fpscr" : "=r"(fpscr));
	if (fpscr != 0)
		return 0;

	/* Succeeded: the old program's status did not carry over. */
	return 1;
}

/* Fills a register image with values derived from a seed. */
static void
fill_pattern(
	struct abi_vfp_state *state,
	uint64_t seed,
	uint32_t fpscr)
{
	unsigned index;

	/* Gives every register a value no other register or test has. */
	for (index = 0; index < ABI_VFP_REGISTERS; index++)
		state->d[index] = seed | ((uint64_t)index << 8) | (uint64_t)(index * 7U + 1U);

	/* The status register the image carries. */
	state->fpscr = fpscr;
}

/* Reports whether two register images are the same. */
static int
same_state(
	const struct abi_vfp_state *expected,
	const struct abi_vfp_state *seen)
{
	unsigned index;

	/* Compares every register and names the first that differs. */
	for (index = 0; index < ABI_VFP_REGISTERS; index++) {
		if (expected->d[index] != seen->d[index]) {
			printf("ABI d%u expected %08x%08x seen %08x%08x\n",
			    index,
			    (unsigned)(expected->d[index] >> 32),
			    (unsigned)expected->d[index],
			    (unsigned)(seen->d[index] >> 32),
			    (unsigned)seen->d[index]);
			return 0;
		}
	}

	/* The status register too. */
	if (expected->fpscr != seen->fpscr) {
		printf("ABI fpscr expected %08x seen %08x\n",
		    (unsigned)expected->fpscr,
		    (unsigned)seen->fpscr);
		return 0;
	}

	/* Succeeded: the images match. */
	return 1;
}

/*
 * Loads a register image, gives up the processor a number of times with
 * sched_yield, and stores what the registers hold afterwards.
 */
static void
vfp_hold_across_yields(
	const struct abi_vfp_state *pattern,
	struct abi_vfp_state *seen,
	unsigned count)
{
	uint32_t fpscr;

	/* Loads, yields in a loop of system calls, and stores. */
	__asm__ volatile(
	    "vmsr fpscr, %[status_in]\n\t"
	    "vldmia %[source], {d0-d15}\n\t"
	    "add ip, %[source], #128\n\t"
	    "vldmia ip, {d16-d31}\n\t"
	    "mov r4, %[count]\n"
	    "1:\n\t"
	    "mov r7, %[yield]\n\t"
	    "svc #0\n\t"
	    "subs r4, r4, #1\n\t"
	    "bne 1b\n\t"
	    "vstmia %[target], {d0-d15}\n\t"
	    "add ip, %[target], #128\n\t"
	    "vstmia ip, {d16-d31}\n\t"
	    "vmrs %[status_out], fpscr"
	    : [status_out] "=&r"(fpscr)
	    : [source] "r"(pattern->d), [target] "r"(seen->d),
	      [status_in] "r"(pattern->fpscr), [count] "r"(count),
	      [yield] "i"(KERN_SYS_sched_yield)
	    : "r0", "r4", "r7", "ip", "cc", "memory",
	      "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
	      "d8", "d9", "d10", "d11", "d12", "d13", "d14", "d15",
	      "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23",
	      "d24", "d25", "d26", "d27", "d28", "d29", "d30", "d31");

	/* Keeps the status register the registers came back with. */
	seen->fpscr = fpscr;
}

/*
 * Loads a register image, sends the process a signal by a system call of
 * its own, and stores what the registers hold after the handler returned.
 */
static void
vfp_hold_across_signal(
	const struct abi_vfp_state *pattern,
	struct abi_vfp_state *seen,
	int signal_number)
{
	uint32_t fpscr;

	/* Loads, signals itself (getpid, then kill), and stores. */
	__asm__ volatile(
	    "vmsr fpscr, %[status_in]\n\t"
	    "vldmia %[source], {d0-d15}\n\t"
	    "add ip, %[source], #128\n\t"
	    "vldmia ip, {d16-d31}\n\t"
	    "mov r7, %[getpid]\n\t"
	    "svc #0\n\t"
	    "mov r1, %[signal]\n\t"
	    "mov r7, %[kill]\n\t"
	    "svc #0\n\t"
	    "vstmia %[target], {d0-d15}\n\t"
	    "add ip, %[target], #128\n\t"
	    "vstmia ip, {d16-d31}\n\t"
	    "vmrs %[status_out], fpscr"
	    : [status_out] "=&r"(fpscr)
	    : [source] "r"(pattern->d), [target] "r"(seen->d),
	      [status_in] "r"(pattern->fpscr), [signal] "r"(signal_number),
	      [getpid] "i"(KERN_SYS_getpid), [kill] "i"(KERN_SYS_kill)
	    : "r0", "r1", "r7", "ip", "cc", "memory",
	      "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
	      "d8", "d9", "d10", "d11", "d12", "d13", "d14", "d15",
	      "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23",
	      "d24", "d25", "d26", "d27", "d28", "d29", "d30", "d31");

	/* Keeps the status register the registers came back with. */
	seen->fpscr = fpscr;
}

/*
 * Loads a register image, spins until the alarm's handler has run or the
 * loop gives up, and stores what the registers hold afterwards.
 */
static void
vfp_hold_until_alarm(
	const struct abi_vfp_state *pattern,
	struct abi_vfp_state *seen)
{
	uint32_t fpscr;

	/* Loads, reads the flag on every turn, and stores. */
	__asm__ volatile(
	    "vmsr fpscr, %[status_in]\n\t"
	    "vldmia %[source], {d0-d15}\n\t"
	    "add ip, %[source], #128\n\t"
	    "vldmia ip, {d16-d31}\n\t"
	    "mov r4, %[spins]\n"
	    "1:\n\t"
	    "ldr r0, [%[flag]]\n\t"
	    "cmp r0, #0\n\t"
	    "bne 2f\n\t"
	    "subs r4, r4, #1\n\t"
	    "bne 1b\n"
	    "2:\n\t"
	    "vstmia %[target], {d0-d15}\n\t"
	    "add ip, %[target], #128\n\t"
	    "vstmia ip, {d16-d31}\n\t"
	    "vmrs %[status_out], fpscr"
	    : [status_out] "=&r"(fpscr)
	    : [source] "r"(pattern->d), [target] "r"(seen->d),
	      [status_in] "r"(pattern->fpscr), [spins] "r"(ABI_ALARM_SPINS),
	      [flag] "r"(&abi_alarm_seen)
	    : "r0", "r4", "ip", "cc", "memory",
	      "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
	      "d8", "d9", "d10", "d11", "d12", "d13", "d14", "d15",
	      "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23",
	      "d24", "d25", "d26", "d27", "d28", "d29", "d30", "d31");

	/* Keeps the status register the registers came back with. */
	seen->fpscr = fpscr;
}

/* Overwrites every floating-point register and the status, as a handler may. */
static void
clobber_vfp(
	void)
{
	static const uint64_t junk[ABI_VFP_REGISTERS] = { 0 };

	/* Zeroes the registers and leaves a status of its own. */
	__asm__ volatile(
	    "vldmia %[source], {d0-d15}\n\t"
	    "vldmia %[source], {d16-d31}\n\t"
	    "vmsr fpscr, %[status]"
	    :
	    : [source] "r"(junk), [status] "r"(ABI_HANDLER_FPSCR)
	    : "memory",
	      "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
	      "d8", "d9", "d10", "d11", "d12", "d13", "d14", "d15",
	      "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23",
	      "d24", "d25", "d26", "d27", "d28", "d29", "d30", "d31");
}

/* The synchronous signal's handler: records the signal and overwrites the registers. */
static void
signal_handler(
	int signal_number)
{
	(void)signal_number;

	/* Tells the test the handler ran. */
	abi_signal_seen = 1;
	clobber_vfp();
}

/* The alarm's handler: records the signal and overwrites the registers. */
static void
alarm_handler(
	int signal_number)
{
	(void)signal_number;

	/* Ends the test's loop, which reads the flag from memory. */
	abi_alarm_seen = 1;
	clobber_vfp();
}

/* Opens the console as standard input, output and error. */
static void
open_console(
	void)
{
	int descriptor;

	/* A descriptor already open (when run from a shell) is kept. */
	descriptor = open("/dev/console", O_RDWR);
	if (descriptor < 0)
		return;

	/* Puts the console at 0, 1 and 2. */
	if (descriptor != STDIN_FILENO)
		(void)dup2(descriptor, STDIN_FILENO);
	if (descriptor != STDOUT_FILENO)
		(void)dup2(descriptor, STDOUT_FILENO);
	if (descriptor != STDERR_FILENO)
		(void)dup2(descriptor, STDERR_FILENO);
	if (descriptor > STDERR_FILENO)
		(void)close(descriptor);
}

/*
 * Prints one step's result.  A failed step is the last: the final line
 * names it, and the program rests there.
 */
static void
report(
	int step,
	const char *name,
	int passed)
{
	/* A passing step says so and the test goes on. */
	if (passed) {
		printf("ABI %d %s ok\n", step, name);
		fflush(stdout);
		return;
	}

	/* A failing one ends the test. */
	printf("ABI %d %s failed\n", step, name);
	printf("ABI FAIL %d\n", step);
	fflush(stdout);
	rest();
}

/* Waits forever, so that init never exits and the console stays readable. */
static void
rest(
	void)
{
	/* Sleeps in long stretches; nothing wakes the program but a signal. */
	for (;;)
		(void)sleep(3600);
}
