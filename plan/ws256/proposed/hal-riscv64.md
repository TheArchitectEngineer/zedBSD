<!-- awesome-plan project=zedbsd record=ws256-p001-proposed-hal -->

# WS256 の HAL の API の差分の案（承認の前は適用しない）

2026-10-18 P2（q1243、ws256-p001）。[design.md](../design.md) §2・§7 #13 の案。guardrail「HAL の API の変更は差分ごとの事前承認」に従い、ここに置くだけで `include/hal/` には触れていない。
`include/hal/hal.h` は**変えない**（宣言・契約・責務とも不変。riscv64 は全部の宣言を実装する。MSI・uncached・idle suspend・debug point は `HAL_ERR_UNSUPPORTED` を返す実装で、契約が許す形）。

## 差分 1: `include/hal/arch.h` に riscv64 の 1 行

既存の `#elif defined(HAL_ARCH_ARM64)` の節の後に:

```c
#elif defined(HAL_ARCH_RISCV64)

#include <hal/arch/riscv64.h>

```

理由: 他の arch と同じ選び方。`HAL_ARCH_RISCV64` は `platform/riscv64/vmunix.mk` と userland の flags が定義する。

## 差分 2: `include/hal/arch/riscv64.h`（新規）

`include/hal/arch/aarch64.h` と同じ節の並び（tick、signal frame、atomic、debugging、checks）。中身の案:

```c
/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

#ifndef HAL_HAL_ARCH_RISCV64_H
#define HAL_HAL_ARCH_RISCV64_H

/*
 * The periodic tick, in hertz.  As on the other 64-bit ports the kernel
 * keeps a millisecond tick; the counter runs at the device tree's
 * timebase (10 MHz on the QEMU virt machine, 4 MHz on the JH7110), and
 * the HAL derives the interval from this value.
 */
#define HAL_TIMER_FREQUENCY	(1000U)

/*
 * Signal Frame
 */

/*
 * Signal frame head: the same 16-byte head as AArch64, without a
 * restorer, signal number or siginfo pointer in it.
 */
#if !defined(__ASSEMBLER__) && !defined(_ASM_SRC_)
struct hal_task_signal_frame_head {
	uint32_t token;
	uint32_t reserved;
	uintptr_t context_pointer;
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
 * Atomic style: the A extension's atomic swap.
 */
#define HAL_ATOMIC_STYLE	HAL_ATOMIC_STYLE_NATIVE

#if !defined(__ASSEMBLER__) && !defined(_ASM_SRC_)

/*
 * Takes a lock word: swaps in one with acquire ordering and reports
 * whether the word was zero.
 */
static inline bool
hal_atomic_uint_try_acquire(
	volatile unsigned *value)
{
	unsigned previous;

	__asm__ volatile("amoswap.w.aq %0, %2, (%1)"
			 : "=&r"(previous)
			 : "r"(value), "r"(1U)
			 : "memory");
	return previous == 0U;
}

/*
 * Lets the hart rest in a spin.  The pause hint is encoded as a fence
 * with no predecessor or successor and is legal on a hart without the
 * hint extension, where it is an ordinary fence.
 */
static inline void
hal_atomic_relax(void)
{
	__asm__ volatile("pause" ::: "memory");
}

#endif

/*
 * Debugging
 */

#if !defined(__ASSEMBLER__) && !defined(_ASM_SRC_)
/*
 * The user integer registers of a task: the thirty-two general registers
 * with x[0] always zero (x1 is the return address, x2 the stack pointer,
 * x4 the thread pointer), and the address of the next instruction.
 */
struct hal_gpregs {
	uint64_t x[32];
	uint64_t pc;
};

/*
 * The floating-point state: the thirty-two 64-bit registers and the
 * control and status register.
 */
struct hal_fpregs {
	uint64_t f[32];
	uint32_t fcsr;
	uint32_t reserved;
};

/*
 * The vector state.  The processors this port runs on have no vector
 * extension, so the set is empty and reading or writing it is refused.
 */
struct hal_vregs {
	uint32_t reserved;
};
#endif

/*
 * Supervisor mode has no single-step bit and the firmware does not
 * delegate the debug triggers, so a task cannot be asked to take one
 * instruction here yet.
 */
#define HAL_DEBUG_HAS_SINGLE_STEP	0

/*
 * No hardware debug point can be given to a task.  The array bound stays
 * one so that the kernel's tables keep a size; a set with any point in
 * it is refused.
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
	sizeof(uintptr_t) == 8,
	"riscv64 signal frame requires LP64");
_Static_assert(
	sizeof(struct hal_task_signal_frame_head) == 16,
	"riscv64 signal frame head size");
_Static_assert(
	__builtin_offsetof(struct hal_task_signal_frame_head,
			   token) == HAL_TASK_SIGNAL_FRAME_TOKEN_OFFSET,
	"riscv64 signal token offset");
_Static_assert(
	__builtin_offsetof(struct hal_task_signal_frame_head,
			   context_pointer) ==
		HAL_TASK_SIGNAL_FRAME_CONTEXT_POINTER_OFFSET,
	"riscv64 signal context offset");
_Static_assert(
	HAL_TASK_SIGNAL_RESTORER_TOKEN_OFFSET ==
		HAL_TASK_SIGNAL_FRAME_TOKEN_OFFSET,
	"riscv64 restorer token offset");
_Static_assert(
	HAL_TASK_SIGNAL_RESTORER_CONTEXT_OFFSET ==
		HAL_TASK_SIGNAL_FRAME_CONTEXT_POINTER_OFFSET,
	"riscv64 restorer context offset");
#endif

#endif
```

注:
- `hal_atomic_relax` の `pause`（Zihintpause の hint、encoding 0x0100000f = `fence w, 0`）は、共有の clang 23 の assembler が `-march=rv64imac_zicsr_zifencei`（Zihintpause 無し）でも受ける事を 2026-10-18 に確かめた。Zihintpause の無い U74 では普通の fence として実行される（合法）。
- `HAL_DEBUG_HAS_SINGLE_STEP 0` と `HAL_DEBUG_KIND_*_OK 0` を kernel（`src/kern/ptrace.c`）がどう扱うかは p003 で確かめる（`hal_task_set_single_step` の失敗は `ENOTSUP` になる事は確かめた。`HAL_DEBUG_POINT_MAX` は配列の大きさに使われるので 1 を下限にした）。
- `struct hal_gpregs` に `sstatus` を入れない: user が変えられる処理器の状態は無い（FS は HAL が持つ）。debugger は pc と x だけ。

## 差分 3（HAL の API ではないが記録）: `include/kern/boot.h`

kernel の公開の boot の ledger（「記録された決定の後にだけ変える」）。design.md §7 #9 の既定: `KERN_RISCV64_HANDOFF_MAGIC` と `struct riscv64_boot_handoff`（`common` = `kern_boot_handoff` の MULTIBOOT、`parameters`、ZBL6 v6 の memory、`boot_hart`、`fdt_phys`/`fdt_size`、`initrd_phys`/`initrd_size`）。rpi4 の `struct rpi4_boot_handoff` と同じ置き方。具体の field の並びは p008 で書き、この file に追記してから適用する。

## SHA-256

承認の後、適用した差分の file の SHA-256 をここに書く（guardrail の表の形）。
