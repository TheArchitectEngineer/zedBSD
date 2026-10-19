# 案: HAL の API の差分（MIPS32・MIPS64、WS271・WS272）— **U1 で事前承認済み、未適用（p002 で入れる）**

2026-10-19 P1（q1273、設計 [design.md](../design.md) §4）。hal.h と `include/kern/boot.h` は変えない。
形は 2026-10-18 の「新しい port の事前の承認（形を限る）」（WS263〜266）と同じ。U1（2026-10-19 14:29 ユーザー、クリック）「同じ形で事前承認」で承認済み。適用は ws271-p002。

## D1. `include/hal/arch.h`（`HAL_ARCH_PPC32` の節の後）
```c
#elif defined(HAL_ARCH_MIPS64)

#include <hal/arch/mips64.h>

#elif defined(HAL_ARCH_MIPS32)

#include <hal/arch/mips32.h>
```

## D2. `include/hal/arch/mips32.h`（新規、riscv32.h と同じ節の並び。本文の要点）
- `#define HAL_TIMER_FREQUENCY (1000U)`（CP0 Count/Compare、Count の速さは board が持つ）。
- signal frame の head: riscv32 と同じ 16 byte（`uint32_t token; uint32_t reserved; uintptr_t context_pointer; uint32_t padding;`）、ALIGNMENT 16、HAS_RESTORER・HAS_SIGNO・HAS_INFO_POINTER は 0、TOKEN_OFFSET 0、CONTEXT_POINTER_OFFSET 8、RESTORER の 2 つも 0・8。
- `#define HAL_ATOMIC_STYLE HAL_ATOMIC_STYLE_ENTER`（MIPS32 に 64 bit の ll/sc が無い）。
```c
static inline bool
hal_atomic_uint_try_acquire(
	volatile unsigned *value)
{
	unsigned previous;
	unsigned one;

	__asm__ volatile(".set push\n\t"
			 ".set noreorder\n"
			 "1:\tll %0, 0(%2)\n\t"
			 "li %1, 1\n\t"
			 "sc %1, 0(%2)\n\t"
			 "beqz %1, 1b\n\t"
			 "nop\n\t"
			 "sync\n\t"
			 ".set pop"
			 : "=&r"(previous), "=&r"(one)
			 : "r"(value)
			 : "memory");
	return previous == 0U;
}

static inline void
hal_atomic_relax(void)
{
	/* MIPS32r2 has no spin hint outside the MT ASE: a no-op wait slot. */
	__asm__ volatile("nop" ::: "memory");
}
```
（comment は本番では複数行の形にする。R2 の `pause` は R2 の基本の命令で、符号化が `sll $0,$0,5` なので古い CPU では nop になる。ただし `pause` は LLbit が落ちるまで待つ意味で、spin の hint（他の hart に譲る）の意味ではないので、`nop` を選ぶ（design-review-1 Minor 8 で版 1 の理由を直した）。）
- debugging:
```c
struct hal_gpregs {
	uint32_t r[32];		/* r[0] is zero; r[26], r[27] (k0, k1) read as zero */
	uint32_t hi;
	uint32_t lo;
	uint32_t pc;		/* EPC, the restart address of the task */
	uint32_t status;
	uint32_t cause;
	uint32_t badvaddr;
};

struct hal_fpregs {
	uint64_t f[32];		/* the 64-bit registers with Status.FR set */
	uint32_t fcsr;
	uint32_t fir;
};

struct hal_vregs {
	uint32_t reserved;
};
```
  `HAL_DEBUG_HAS_SINGLE_STEP 0`、`HAL_DEBUG_POINT_MAX 1`、`HAL_DEBUG_LENGTH_MAX 8`、KIND の 4 つは 0（Watch register は後で）。
- checks: `sizeof(uintptr_t) == 4`、head が 16 byte、offset の 4 つ（riscv32.h と同じ文、名前を mips32 に）。

## D3. `include/hal/arch/mips64.h`（新規）
- tick 1000 Hz、signal frame の head は riscv64.h と同じ 16 byte（`uint32_t token; uint32_t reserved; uintptr_t context_pointer;`、CONTEXT_POINTER_OFFSET 8）。
- atomic は riscv64.h と同じ native の style（`lld`/`scd` で 64 bit の操作が組める）。`hal_atomic_uint_try_acquire` は D2 と同じ ll/sc（32 bit の word）。
- `hal_gpregs` は `uint64_t r[32]; hi; lo; pc; status; cause; badvaddr;`、`hal_fpregs` は D2 と同じ。
- checks: `sizeof(uintptr_t) == 8` と offset。

## D4. `hal_get_arch_handoff` の名前付きの値（hal.h は不変、HAL の責務の追加）
- `boot.command-line`: `-append` の文字列（ppc32 と同じ名前・形）。
- 戻り値は hal.h 1284〜1291 行の契約どおり pointer（HAL が持つ object）で、無い名前・値の無い時は NULL（版 1 の「HAL_ERR_NOT_FOUND」は誤り、design-review-1 Minor 4）。`boot.command-line` は `-append` が無い時 NULL。
- 版 1 の `mips.initrd` は使う側が無いので外した。initrd を kernel に渡す時に、riscv32 の `riscv32.initrd` と同じ形（2 つの uint64）で `mips32.initrd`・`mips64.initrd` の名前を改めて案にする。
