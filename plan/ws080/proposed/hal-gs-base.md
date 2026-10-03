<!-- awesome-plan project=zedbsd record=ws080-proposed-hal-gs-base -->

# 案: amd64 の user の GS base（swapgs、案 A）— HAL・kernel・UAPI の差分

2026-09-29 ws080-p001。**案であり未適用・未 compile**（下の「状態」）。適用は ws080-p002 で、**hal の API の部分は差分ごとのユーザーの承認の後**。
方式（案 A: swapgs）は 2026-09-28 にユーザーが決定（[ws.md](../ws.md) の判断 1）。設計の全体は [design.md](../design.md) §3.1。

## 状態

- 下の差分は source の読みから書いた案で、**tree に当てて compile・起動した確認はしていない**。p001 の中で worktree に一時的に当てて vmunix の build と
  boot test で確かめようとしたが、kernel の入口の code の変更として実行の環境（auto mode の classifier）に拒まれた。確認は承認の後の p002 で行う。
- 承認を分けて求める単位:
  - **A1（HAL の API、承認が要る）**: `include/hal/arch/amd64.h` に 2 つの関数の宣言、`struct hal_gpregs` の `gs_base` の意味の変更。
  - **A2（HAL の責務の変更、承認が要る）**: amd64 の HAL が user の GS base を task の状態として持ち、kernel の入口・出口で `swapgs` する
    （`trap.S`・`dispatch.S`・`percpu.c`・`task.c`・`task.h`・`defs.h`）。hal.h の宣言は変えないが、HAL の責務（user に戻るときの GS の内容）が変わる。
  - **A3（HAL の実装、承認は不要だが同じ review に含める）**: hardware の debug の点を user の address に限る（`task.c` の `debug_point_field`）。
  - **K1（kernel・UAPI、承認は不要、p002 で同時に）**: `thread_self` の GET/SET_GSBASE、`thread_create` の最初の GS base。

## 今の状態（2026-09-29 の source）

- `percpu.c`: `IA32_GS_BASE`（0xc0000101）= その CPU の `struct amd64_percpu`（kernel の上半分の address）。`swapgs` はどこにも無い。
  user に戻っても GS base は per-CPU の pointer のまま。user の `%gs:` の参照は supervisor の page で #PF になる。
- `trap.S`: `amd64_syscall_fast_entry`（SYSCALL）が最初の命令で `%gs:AMD64_PERCPU_SYSCALL_SCRATCH` に user の RSP を書く（GS = kernel が前提）。
  全ての vector は `amd64_common_interrupt`（`SAVE_AND_CALL`）に入り、出口は `iretq` か `amd64_sysret`（`sysretq`）。
- `dispatch.S`: 新しい user の task は `amd64_user_task_entry`、fork の子は `amd64_user_frame_entry` から `iretq` で user へ。
- IST: #DF（8）は IST1、NMI（2）は IST2。#MC（18）・#DB（1）は IST 無し。
- CR4: PGE（7）・OSFXSR（9）・OSXMMEXCPT（10）だけ。**FSGSBASE（16）は立っていない**。
- FS base: task の `tls` と `IA32_FS_BASE` を context switch で保存・復元（`hal_task_set_tls` 等）。`struct hal_gpregs` の `gs_base` は「reserved」で常に 0。
- debug の点（`hal_task_set_debug_points`）: 種類・長さ・整列は検査するが、address が user の範囲かは検査しない。

## 差分（案）

### defs.h（A2）

```c
#define AMD64_MSR_GS_BASE      0xc0000101U
/*
 * The GS base SWAPGS exchanges with IA32_GS_BASE.  While the kernel runs it
 * holds the user's GS base; while user code runs, the per-CPU state.
 */
#define AMD64_MSR_KERNEL_GS_BASE 0xc0000102U
```

### trap.S（A2）

1. 共通の入口: CS の ring で入れ替える。SYSCALL の入口は自分で入れ替えた後の label に入る。

```asm
.global amd64_common_interrupt
amd64_common_interrupt:
	testb $3, 24(%rsp)		/* vector, error, rip の上の CS */
	jz amd64_common_interrupt_swapped
	swapgs
.global amd64_common_interrupt_swapped
amd64_common_interrupt_swapped:
	SAVE_AND_CALL
```

2. `SAVE_AND_CALL` の `iretq` の出口: user へ戻るときだけ戻す。

```asm
	addq $16, %rsp
	testb $3, 8(%rsp)		/* rip の上の CS */
	jz 1f
	swapgs
1:
	iretq
```

3. `amd64_sysret`: user の RSP を読み込む前に戻す（`sysretq` は常に ring 3 へ）。

```asm
	movq 16(%rsp), %rcx
	movq 32(%rsp), %r11
	cli
	swapgs
	movq 40(%rsp), %rsp
	sysretq
```

4. `amd64_syscall_fast_entry`: 最初の命令で入れ替え、frame を作った後は入れ替え済みの label へ。

```asm
amd64_syscall_fast_entry:
	swapgs
	movq %rsp, %gs:AMD64_PERCPU_SYSCALL_SCRATCH
	...
	pushq $INT_SYSCALL_FAST
	jmp amd64_common_interrupt_swapped
```

5. paranoid の入口: #DB（1）・NMI（2）・#DF（8）・#MC（18）は `swapgs` と ring の変化の間（入口の `swapgs` の前、出口の `swapgs` の後の
   `iretq`・`sysretq` の前）に来うる。CS では判断できないので `IA32_GS_BASE` の値で判断する: per-CPU の状態は上半分、user の GS base は下半分の
   address だけ（A1 の関数と K1 が検査し、FSGSBASE は立てないので user が任意の値を書けない）。入れ替えたかを r12（callee-saved）に持ち、戻りで戻す。

```asm
.macro ISR_PARANOID_NOERR n
.Lfault\n:
	pushq $0
	pushq $\n
	jmp amd64_paranoid_interrupt
.endm
/* ISR_PARANOID_ERR は CPU が error code を積む vector 用（0 を積まない）。8 に使う */

ISR_PARANOID_NOERR 1
ISR_PARANOID_NOERR 2
ISR_PARANOID_ERR   8
ISR_PARANOID_NOERR 18

.global amd64_paranoid_interrupt
amd64_paranoid_interrupt:
	/* SAVE_AND_CALL と同じ 15 の push、cld */
	xorl %r12d, %r12d
	movl $AMD64_MSR_GS_BASE, %ecx
	rdmsr
	testl %edx, %edx		/* 上半分（kernel）なら符号の bit が立つ */
	js 1f
	swapgs
	movl $1, %r12d
1:
	movq %rsp, %rdi
	movq %rsp, %rbx
	andq $-16, %rsp
	call int_handler
	movq %rbx, %rsp
	testl %r12d, %r12d
	jz 2f
	swapgs
2:
	/* 15 の pop、addq $16, %rsp、iretq（INT_SYSCALL_FAST にはならない） */
```

   `int_handler` は r12 を SysV の callee-saved として保つ。#DF は戻らない（fatal）。NMI の入れ子は IST2 の上で `iretq` までは起きない。

### dispatch.S（A2）

`amd64_user_task_entry` と `amd64_user_frame_entry` の `iretq` の前に、2. と同じ CS の検査と `swapgs`（どちらも ring 3 へ戻るが、検査で固定の前提にしない）。

### percpu.c（A2）

`amd64_percpu_select()` で `IA32_GS_BASE` を書いた後に `asm_write_msr(AMD64_MSR_KERNEL_GS_BASE, 0)`（その CPU に待っている user の値は無い）。
BSP と AP の両方がここを通る。

### task.h・task.c（A2）

- `struct amd64_task` に `uintptr_t user_gs_base;`（kernel の中を走る間の生の値は `IA32_KERNEL_GS_BASE`、この field は context switch の保存先）。
- `hal_task_context_switch()`: FS と並べて

```c
	from->tls = (uintptr_t)asm_read_msr(AMD64_MSR_FS_BASE);
	from->user_gs_base = (uintptr_t)asm_read_msr(AMD64_MSR_KERNEL_GS_BASE);
	...
	asm_write_msr(AMD64_MSR_FS_BASE, (uint64_t)to->tls);
	asm_write_msr(AMD64_MSR_KERNEL_GS_BASE, (uint64_t)to->user_gs_base);
```

  （user は selector の読み込みで GS base を 0 にだけできる。FS と同じく生の値を読んで保存するので、その場合も正しい。）
- fork（`hal_task_fork_current` の子の作成）: `child->user_gs_base = rdmsr(KERNEL_GS_BASE)`（親の生の値）。
- exec（`hal_task_exec_current`）: `current_task->user_gs_base = 0; wrmsr(KERNEL_GS_BASE, 0)`。
- 新しい task は 0（task の確保で 0 埋め）。
- `hal_task_get_user_gpregs()`: `registers->gs_base = hal_amd64_task_get_user_gs_base(task);`（今は常に 0）。
- `hal_task_set_user_gpregs()`: `gs_base` が user の address（`amd64_user_address_valid()`）でなければ -1、よければ `hal_amd64_task_set_user_gs_base()`。
- 新しい関数（A1 の宣言の実装）:

```c
/*
 * Sets one task's user GS base.
 *
 * Reports -1 for a value outside the lower canonical half: the paranoid
 * entries tell a user GS base from the kernel's by its half.
 */
int
hal_amd64_task_set_user_gs_base(
	hal_task_t handle,
	uintptr_t value)
{
	struct amd64_task *task;

	task = handle;

	/* Requires a task and a user address. */
	if (task == NULL)
		return -1;
	if (!amd64_user_address_valid(value))
		return -1;

	/* Records the value the next switch to the task loads. */
	task->user_gs_base = value;

	/* The running task's user value waits in IA32_KERNEL_GS_BASE. */
	if (task == current_task)
		asm_write_msr(AMD64_MSR_KERNEL_GS_BASE, (uint64_t)value);

	/* Succeeded. */
	return 0;
}

/*
 * Reports one task's user GS base.
 */
uintptr_t
hal_amd64_task_get_user_gs_base(
	hal_task_t handle)
{
	uintptr_t value;

	/* Reads the live value for the running task. */
	if (handle == current_task) {
		value = (uintptr_t)asm_read_msr(AMD64_MSR_KERNEL_GS_BASE);

		/* Returns the live user GS base. */
		return value;
	}

	/* Returns the saved value of an inactive task. */
	if (handle != NULL)
		return ((struct amd64_task *)handle)->user_gs_base;

	/* Returns the neutral value for an absent task. */
	return 0;
}
```

  `amd64_user_address_valid()` は今は file の後ろの static 関数なので、forward declaration を足す。

### task.c の debug の点（A3）

`debug_point_field()` の最後で、点の address と `address + length - 1` の両方が `amd64_user_address_valid()` でなければ -1。
理由: kernel の address の点は kernel の中で #DB を起こし、`swapgs` の窓の中なら GS の食い違いを起こしうる（paranoid の #DB がそれを受けるが、
そもそも user に kernel の address を見張らせる理由が無く、何を kernel が触るかの情報も漏らす）。

### include/hal/arch/amd64.h（A1、承認が要る）

```c
struct hal_gpregs {
	...
	uint64_t fs_base;
	uint64_t gs_base;	/* the user GS base (hal_amd64_task_set_user_gs_base) */
};

/*
 * The user GS base of a task.  User code reads through GS whatever the
 * value points at; the kernel attaches no meaning to it.  Only a lower-half
 * (user) address is accepted: the set call reports -1 for anything else.
 */
int hal_amd64_task_set_user_gs_base(hal_task_t task, uintptr_t value);
uintptr_t hal_amd64_task_get_user_gs_base(hal_task_t task);
```

`hal.h` 自身（arch に依らない API）は変えない。GS は amd64 に固有なので arch の header に置く（`hal_task_set_tls` の隣に arch 中立の名前で置く案は、
他の arch に意味が無いので採らない）。

### kernel・UAPI（K1、HAL の外）

- `include/uapi/thread.h`: `#define KERN_THREAD_SELF_GET_GSBASE 3U`、`#define KERN_THREAD_SELF_SET_GSBASE 4U`。
- `src/kern/syscall.c` の `sys_thread_self_call()`: `#if defined(HAL_ARCH_AMD64)` で GET は `hal_amd64_task_get_user_gs_base(curthread->task)`、
  SET は `hal_amd64_task_set_user_gs_base()` が -1 なら `-EINVAL`。他の arch は `-EOPNOTSUPP`。
- `sys_thread_create_call()`: 今は `args[4] != 0` を拒む。amd64 では `args[4]` を新しい thread の最初の GS base とし（0 は無し）、
  `hal_amd64_task_set_user_gs_base(thread->task, args[4])` が -1 なら thread を作る前に `-EINVAL`。他の arch は従来どおり 0 を要求。
- libc の `thread_self` の wrapper は変えない（新しい op は ld.coff と互換の DLL が直接呼ぶ）。

## 試験（p002 で行う）

1. 入口の全ての種類: syscall（`syscall` 命令と `int` の gate）、user の #PF・#UD・#BP・#DB（single step）、kernel の中の割り込み（timer・MSI）、
   user の中の割り込み、NMI（QEMU の monitor の `nmi`）を、GS base を設定した user の thread で起こし、戻った後に `%gs:0` の読みが設定した値の
   先を読むこと、kernel が落ちないことを確かめる guest の試験（`gsbase-probe`、native の ELF で `thread_self(SET_GSBASE)` を使う）。
2. thread の切り替えで保たれる: 2 つの thread が別の GS base を持ち、互いに yield しながら 100 万回 `%gs:0` を読み比べる。fork の子が親の値を持つ、
   exec で 0 に戻る、`thread_create` の `args[4]`。
3. 拒否: 上半分の address、非 canonical の address の SET が `-EINVAL`、`ptrace` の `gs_base` の書き込みも同じ。kernel の address の debug の点が拒まれる。
4. 回帰: boot test（`plan/tools/boot-test.sh`）、desktop（Venus、`plan/ws035/tests/zdesktop-guest.sh`）、HID・USB の既存の guest の試験、
   i915 の実機（ユーザー。swapgs は全ての入口に触るため、ws.md の p002 の条件どおり）。
5. SMP: 4 CPU の guest で 1・2 を同時に走らせる（AP の `amd64_percpu_select` の `KERNEL_GS_BASE` の初期化）。

## 性能

入口・出口で `swapgs` が 1 回ずつ（数 cycle）。context switch で `rdmsr`・`wrmsr` が 1 組増える（FS と同じ量）。paranoid の 4 つの vector は
`rdmsr` が 1 回増えるが、どれもまれ。
