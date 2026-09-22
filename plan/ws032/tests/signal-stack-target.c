/* WS032: the stack a signal handler and a thread are entered on.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
 *
 * The calling convention says where the stack pointer stands when a
 * function is entered, and a compiler puts its sixteen-byte data where
 * that promise says it will land.  A handler entered one word out makes
 * every instruction that moves sixteen bytes at once fault, which is how
 * an ordinary call to sigaction() from a handler brought a program down.
 * So the alignment is checked in each of the three places a program runs.
 *
 * usage: compile on the target and run; it prints one line per check. */
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int failures;

static void check(const char *what, int ok)
{
	printf("SIG %-4s %s\n", ok ? "ok" : "FAIL", what);
	if (!ok)
		failures++;
}

/* A local the compiler aligns to sixteen shows where the stack really is. */
static int aligned(void)
{
	__attribute__((aligned(16))) char probe[16];

	return ((unsigned long)probe & 15UL) == 0UL;
}

/* A call that copies a structure with the processor's wide moves. */
static int wide_move_call(void)
{
	struct sigaction action;

	memset(&action, 0, sizeof(action));
	action.sa_handler = SIG_IGN;
	return sigaction(SIGUSR2, &action, NULL) == 0;
}

static volatile int handler_aligned;
static volatile int handler_called;
static volatile int handler_call_ok;

static void handler(int signo)
{
	(void)signo;
	handler_called++;
	handler_aligned = aligned();
	handler_call_ok = wide_move_call();
}

static void *thread_main(void *argument)
{
	(void)argument;
	check("a thread is entered on an aligned stack", aligned());
	check("a thread may call what moves sixteen bytes", wide_move_call());

	handler_called = 0;
	raise(SIGUSR1);
	check("a thread's handler runs", handler_called == 1);
	check("a thread's handler is entered on an aligned stack",
	      handler_aligned);
	check("a thread's handler may call what moves sixteen bytes",
	      handler_call_ok);
	return NULL;
}

int main(void)
{
	struct sigaction action;
	pthread_t thread;

	check("the first thread is entered on an aligned stack", aligned());
	check("the first thread may call what moves sixteen bytes",
	      wide_move_call());

	memset(&action, 0, sizeof(action));
	action.sa_handler = handler;
	if (sigaction(SIGUSR1, &action, NULL) != 0) {
		check("a handler can be installed", 0);
		return 1;
	}

	handler_called = 0;
	raise(SIGUSR1);
	check("a handler runs", handler_called == 1);
	check("a handler is entered on an aligned stack", handler_aligned);
	check("a handler may call what moves sixteen bytes", handler_call_ok);

	if (pthread_create(&thread, NULL, thread_main, NULL) != 0) {
		check("a thread can be created", 0);
		return 1;
	}
	(void)pthread_join(thread, NULL);

	printf("SIG done failures=%d\n", failures);
	return failures != 0;
}
