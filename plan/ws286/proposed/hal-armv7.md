# 案: HAL の API の差分（ARMv7-A、WS286）— **U1 の承認待ち、未適用（p004 で入れる）**

2026-10-23 P1（[ws286-p001](../phase001/phase.md) 版 2、設計 [design.md](../design.md) §3.9・§3.13）。hal.h と `include/kern/boot.h` は変えない。
形は Guardrail の 2026-10-18「新しい port の事前の承認（形を限る）」（WS263〜266）と 2026-10-19（WS271・272）と同じ: arch.h の 1 節、新しい `include/hal/arch/<arch>.h`（aarch64.h・riscv32.h と同じ節の並び）、`hal_get_arch_handoff` の名前付きの値。それ以外の HAL の API の変更は含まない。
**U1（ユーザー）: この 3 点（D1〜D3）を同じ形で事前承認するか。** 承認の後に案から変わる時は改めて差分を示す。

**状態（版 3、2026-10-23 P2）**: U1 は 2026-10-23 にユーザーが推しで決定（「U項目は読みましたが、全部推しでOKでした。」）。review（[review-20261023.md](../phase001/review-20261023.md)）の対応（design 版 3）で D1〜D3 は変わらない: 影の table・20 word の例外の frame・VFP の sync/forget・SCU と ACTLR・`PHYS_BASE` は全部 HAL の内側、C2 の直し（virt を raw の image で Linux の約束で入れる）でも `armv7.initrd` の出所は同じ `/chosen`、`armv7.fdt` は `r2` の DTB。D2 の `hal_atomic_uint_try_acquire` の失敗の時の `clrex` はそのまま（A15 の erratum 830321 は例外の戻りの monitor の閉じ方の話で、Linux も例外の戻りだけ dummy の `strex` にする: design §3.3）。

## D1. `include/hal/arch.h`（`HAL_ARCH_RISCV32` の節の後）
```c
#elif defined(HAL_ARCH_ARMV7)

#include <hal/arch/armv7.h>
```

## D2. `include/hal/arch/armv7.h`（新規、riscv32.h と同じ節の並び。本文の要点）
- `#define HAL_TIMER_FREQUENCY (1000U)`（zybo は private timer、virt は generic timer の CNTP。counter の速さは BSP が持つ）。
- signal frame の head: riscv32 と同じ 16 byte（`uint32_t token; uint32_t reserved; uintptr_t context_pointer; uint32_t padding;`）、ALIGNMENT 16、HAS_RESTORER・HAS_SIGNO・HAS_INFO_POINTER は 0、TOKEN_OFFSET 0、CONTEXT_POINTER_OFFSET 8、RESTORER の 2 つも 0・8（restorer は `lr` で渡す、design §3.9）。
- `#define HAL_ATOMIC_STYLE HAL_ATOMIC_STYLE_NATIVE`（ARMv7-A の `ldrex/strex` と 64 bit の `ldrexd/strexd`。p004 で `llvm-nm -u` に `__atomic_*`・`__sync_*` が出たら `HAL_ATOMIC_STYLE_ENTER` に変える。その時は改めて示す）。
```c
static inline bool
hal_atomic_uint_try_acquire(
	volatile unsigned *value)
{
	unsigned previous;
	unsigned failed;

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
	return previous == 0U;
}

static inline void
hal_atomic_relax(void)
{
	__asm__ volatile("yield" ::: "memory");
}
```
（comment は本番では riscv32.h の形の複数行にする。取れなかった時は `clrex` で排他の monitor を閉じる。`yield` は ARMv7-A の hint で、A9 では nop として走る。spin を `wfe`/`sev` にしないのは、CPU1 が BootROM の `wfe` の loop に居る間に `sev` を出さないため（design §9 R20）。）
- debugging:
```c
struct hal_gpregs {
	uint32_t r[13];		/* r0-r12 */
	uint32_t sp;
	uint32_t lr;
	uint32_t pc;		/* the restart address of the task */
	uint32_t cpsr;		/* the user's status: NZCVQ, GE, E, T; mode bits read as user */
	uint32_t tpidruro;	/* the thread pointer */
};

struct hal_fpregs {
	uint64_t d[32];		/* VFPv3-D32; NEON's q0-q15 are the same registers */
	uint32_t fpscr;
	uint32_t fpexc;		/* read only through the debugging interface */
};

struct hal_vregs {
	uint32_t reserved;
};
```
  `HAL_DEBUG_HAS_SINGLE_STEP 0`（ARMv7 に hardware の single step は無い）、`HAL_DEBUG_POINT_MAX 1`、`HAL_DEBUG_LENGTH_MAX 8`、KIND の 4 つは 0（A9 の DBGBCR/DBGWCR の利用は後）。
- `hal_task_set_debug_registers` で `cpsr` の書き込みは条件 flag・GE・T だけを受け、mode・I・F・A は kernel が決める（design §3.9 の「spsr.T を持つ frame を user が壊せない」）。
- checks: `sizeof(uintptr_t) == 4`、head が 16 byte、offset の 4 つ（riscv32.h と同じ文、名前を armv7 に）。

## D3. `hal_get_arch_handoff` の名前付きの値（hal.h は不変、HAL の責務の追加）
- `boot.command-line`: DTB の `/chosen/bootargs`（JTAG の `jtag-boot.sh --append`、QEMU virt の `-append`、後の SD の起動の U-Boot の `bootargs`）。無ければ NULL。他の port と同じ名前・形。
- `armv7.fdt`: DTB の物理の範囲（`uint64_t` 2 つ: 物理 address と大きさ、riscv32 の `riscv32.fdt` と同じ形）。DTB が無い時（zybo の QEMU で fallback も無い時）は NULL。
- `armv7.initrd`: ramdisk の物理の範囲（同じ形、`/chosen` の `linux,initrd-start`・`linux,initrd-end`）。無ければ NULL。`src/kern/platform/armv7-virt.c`・`zybo.c` が `drv_ramdisk_create` に渡す（riscv-virt.c の `publish_ramdisk` と同じ確かめ）。
- 戻り値は hal.h の契約どおり HAL が持つ object への pointer、無い名前・値の無い時は NULL。
- 版 1 の `armv7.boot-cpu` は使う側が無いので外した（WS271 の review の Minor 4 と同じ判断。SMP の WS で要れば改めて示す）。
