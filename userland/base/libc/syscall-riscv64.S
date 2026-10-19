/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The 64-bit RISC-V user-space system call entry point and signal
 * restorer of the shared C library.
 */

#include <hal/arch.h>

	.text

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
