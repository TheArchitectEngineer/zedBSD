/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

#ifndef HAL_HAL_ARCH_ARMV7_H
#define HAL_HAL_ARCH_ARMV7_H

/*
 * The periodic tick, in hertz.  As on the other ports the kernel keeps a
 * millisecond tick; the board's timer (the generic timer on the
 * emulator's virt board, the Cortex-A9 private timer on the ZYBO) is
 * programmed from it, and the counter's own rate belongs to the board.
 */
#define HAL_TIMER_FREQUENCY	(1000U)

/*
 * Signal Frame
 */

/*
 * Signal frame head: the 16-byte head of the other 32-bit ports, its
 * pointer 32 bits wide and the rest of its slot kept as padding, without
 * a restorer, signal number or siginfo pointer in it.  The handler
 * returns to the restorer through the link register.
 */
#if !defined(__ASSEMBLER__) && !defined(_ASM_SRC_)
struct hal_task_signal_frame_head {
	uint32_t token;
	uint32_t reserved;
	uintptr_t context_pointer;
	uint32_t padding;
};
#endif

#define HAL_TASK_SIGNAL_FRAME_ALIGNMENT			16

#define HAL_TASK_SIGNAL_FRAME_HAS_RESTORER		0
#define HAL_TASK_SIGNAL_FRAME_HAS_SIGNO			0
#define HAL_TASK_SIGNAL_FRAME_HAS_INFO_POINTER		0

#define HAL_TASK_SIGNAL_FRAME_TOKEN_OFFSET		0
#define HAL_TASK_SIGNAL_FRAME_CONTEXT_POINTER_OFFSET	8
#define HAL_TASK_SIGNAL_RESTORER_TOKEN_OFFSET		0
#define HAL_TASK_SIGNAL_RESTORER_CONTEXT_OFFSET		8

/*
 * Atomic
 */

/*
 * Atomic style: ARMv7-A has the exclusive load and store on words and on
 * doublewords, so every atomic operation of the kernel is a sequence of
 * its own and no guard is shared (the native style).
 */
#define HAL_ATOMIC_STYLE	HAL_ATOMIC_STYLE_NATIVE

#if !defined(__ASSEMBLER__) && !defined(_ASM_SRC_)

/*
 * Takes a lock word: stores one where the word held zero, with acquire
 * ordering, and reports whether the word was zero.
 *
 * A word found held is left with clrex, so that the exclusive monitor is
 * not left open by the attempt.
 */
static __inline bool
hal_atomic_uint_try_acquire(
	volatile unsigned *value)
{
	unsigned previous;
	unsigned failed;

	/* Retries the store while another observer broke the reservation. */
	__asm__ volatile("1:\tldrex %0, [%2]\n\t"
			 "cmp %0, #0\n\t"
			 "bne 2f\n\t"
			 "strex %1, %3, [%2]\n\t"
			 "cmp %1, #0\n\t"
			 "bne 1b\n\t"
			 "dmb ish\n\t"
			 "b 3f\n"
			 "2:\tclrex\n"
			 "3:"
			 : "=&r"(previous), "=&r"(failed)
			 : "r"(value), "r"(1U)
			 : "cc", "memory");

	/* The lock was free when the word held zero. */
	if (previous == 0U)
		return true;

	/* Another holder has the lock. */
	return false;
}

/*
 * Lets the processor rest in a spin.
 *
 * yield is the ARMv7-A spin hint, which the Cortex-A9 runs as a no-op.
 * A spin does not wait with wfe and wake its waiters with sev: an sev
 * would also wake the ZYBO's second processor, which waits in the boot
 * ROM's wfe loop until the HAL starts it.
 */
static __inline void
hal_atomic_relax(
	void)
{
	/* Spends one hint slot. */
	__asm__ volatile("yield" ::: "memory");
}

#endif

/*
 * Debugging
 */

#if !defined(__ASSEMBLER__) && !defined(_ASM_SRC_)
/*
 * The user integer registers of a task: r0 to r12, the user stack
 * pointer and link register, the restart address, the user's status
 * (the flags, GE, E and T; its mode reads as user), and the thread
 * pointer the task reads through TPIDRURO.
 */
struct hal_gpregs {
	uint32_t r[13];
	uint32_t sp;
	uint32_t lr;
	uint32_t pc;
	uint32_t cpsr;
	uint32_t tpidruro;
};

/*
 * The floating-point and Advanced SIMD state: the thirty-two 64-bit
 * registers of VFPv3-D32 (NEON's q0 to q15 are the same registers), the
 * status and control register, and the exception register, which the
 * debugging interface only reads.
 */
struct hal_fpregs {
	uint64_t d[32];
	uint32_t fpscr;
	uint32_t fpexc;
};

/*
 * The vector state.  The Advanced SIMD registers are the floating-point
 * registers, so the set is empty and reading or writing it is refused.
 */
struct hal_vregs {
	uint32_t reserved;
};
#endif

/*
 * ARMv7 has no single-step facility of its own, so a task cannot be
 * asked to take one instruction here yet.
 */
#define HAL_DEBUG_HAS_SINGLE_STEP	0

/*
 * No hardware debug point can be given to a task yet.  The array bound
 * stays one so that the kernel's tables keep a size; a set with any point
 * in it is refused.
 */
#define HAL_DEBUG_POINT_MAX		1
#define HAL_DEBUG_LENGTH_MAX		8

#define HAL_DEBUG_KIND_EXECUTE_OK	0
#define HAL_DEBUG_KIND_WRITE_OK		0
#define HAL_DEBUG_KIND_READ_OK		0
#define HAL_DEBUG_KIND_ACCESS_OK	0

/*
 * Checks
 */
#if !defined(__ASSEMBLER__) && !defined(_ASM_SRC_)
_Static_assert(
	sizeof(uintptr_t) == 4,
	"armv7 signal frame requires ILP32");
_Static_assert(
	sizeof(struct hal_task_signal_frame_head) == 16,
	"armv7 signal frame head size");
_Static_assert(
	__builtin_offsetof(struct hal_task_signal_frame_head,
			   token) == HAL_TASK_SIGNAL_FRAME_TOKEN_OFFSET,
	"armv7 signal token offset");
_Static_assert(
	__builtin_offsetof(struct hal_task_signal_frame_head,
			   context_pointer) ==
		HAL_TASK_SIGNAL_FRAME_CONTEXT_POINTER_OFFSET,
	"armv7 signal context offset");
_Static_assert(
	HAL_TASK_SIGNAL_RESTORER_TOKEN_OFFSET ==
		HAL_TASK_SIGNAL_FRAME_TOKEN_OFFSET,
	"armv7 restorer token offset");
_Static_assert(
	HAL_TASK_SIGNAL_RESTORER_CONTEXT_OFFSET ==
		HAL_TASK_SIGNAL_FRAME_CONTEXT_POINTER_OFFSET,
	"armv7 restorer context offset");
#endif

#endif
