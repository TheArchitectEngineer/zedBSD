/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The 64-bit RISC-V start file of a static program, with the system call
 * veneer and the signal restorer the static C library calls.
 *
 * The kernel enters _start with the stack pointer at the argument count,
 * followed by the argument vector, its terminating null and the
 * environment.  Nothing is addressed through the global pointer: every
 * object is built without linker relaxation, so gp is left as the kernel
 * set it.
 */

#include <hal/arch.h>

	.text
	.balign 16
	.global _start
	.type _start, %function
_start:
	/* No frame and no return address: a stack walk stops here. */
	mv	s0, zero
	mv	ra, zero

	/* The argument count, the argument vector and the environment. */
	mv	s1, sp
	ld	s2, 0(s1)
	addi	s3, s1, 8
	slli	t0, s2, 3
	add	s4, s3, t0
	addi	s4, s4, 8

	/* Sets the C library up, then runs the program. */
	mv	a0, s2
	mv	a1, s3
	mv	a2, s4
	call	__libc_init
	mv	a0, s2
	mv	a1, s3
	mv	a2, s4
	call	main

	/* Leaves with main's status; _exit does not return. */
	call	_exit
1:
	ebreak
	j	1b
	.size _start, .-_start

/*
 * intptr_t __syscall6(number, a1, a2, a3, a4, a5, a6)
 *
 * The kernel takes the number in a7 and the arguments in a0 to a5, and
 * returns the result in a0.
 */
	.balign 4
	.global __syscall6
	.type __syscall6, %function
__syscall6:
	mv	a7, a0
	mv	a0, a1
	mv	a1, a2
	mv	a2, a3
	mv	a3, a4
	mv	a4, a5
	mv	a5, a6
	ecall
	ret
	.size __syscall6, .-__syscall6

/*
 * A signal handler returns here.  The stack pointer is back at the frame
 * the kernel built, whose token and context the return request names.
 * System call 80 is the signal return; it does not come back.
 */
	.balign 4
	.global __signal_restorer
	.type __signal_restorer, %function
__signal_restorer:
	lwu	a0, HAL_TASK_SIGNAL_RESTORER_TOKEN_OFFSET(sp)
	ld	a1, HAL_TASK_SIGNAL_RESTORER_CONTEXT_OFFSET(sp)
	li	a7, 80
	ecall
	ebreak
	.size __signal_restorer, .-__signal_restorer

	.section .note.GNU-stack,"",%progbits
