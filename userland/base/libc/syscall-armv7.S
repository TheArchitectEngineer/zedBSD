/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The 32-bit ARM user-space system call entry point and signal restorer
 * of the shared C library.
 */

#include <hal/arch.h>

	.syntax unified
	.arm
	.text

/*
 * intptr_t __syscall6(number, a1, a2, a3, a4, a5, a6)
 *
 * The kernel takes the number in r7 and the arguments in r0 to r5, and
 * returns the result in r0.  The calling convention brings the first
 * three arguments in r1 to r3 and the last three on the stack, and has
 * the callee keep r4, r5 and r7.
 */
	.balign 4
	.global __syscall6
	.type __syscall6, %function
__syscall6:
	push	{r4, r5, r7, lr}
	mov	r7, r0
	mov	r0, r1
	mov	r1, r2
	mov	r2, r3
	ldr	r3, [sp, #16]
	ldr	r4, [sp, #20]
	ldr	r5, [sp, #24]
	svc	#0
	pop	{r4, r5, r7, pc}
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
	/* Names the frame by its token and context, and asks for the return. */
	ldr	r0, [sp, #HAL_TASK_SIGNAL_RESTORER_TOKEN_OFFSET]
	ldr	r1, [sp, #HAL_TASK_SIGNAL_RESTORER_CONTEXT_OFFSET]
	mov	r7, #80
	svc	#0
	udf	#0
	.size __signal_restorer, .-__signal_restorer

	.section .note.GNU-stack,"",%progbits
