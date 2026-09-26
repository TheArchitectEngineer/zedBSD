<!-- awesome-plan project=zedbsd record=ws044p010 -->

# ws044-p010: aarch64 の ptrace（kernel と HAL）

Phase ID: `ws044-p010`
Parent: [WS044](../ws.md)
Status: **cleared**（2026-09-27、WS036/WS044 の subagent。QEMU まで）
Phase disposition: normal

## 範囲

p003（lldb）を分けた最初の Phase。debugger が使う ptrace(2) を aarch64 でも動かす: register の読み書き、
software breakpoint（BRK）、1 命令の step、hardware breakpoint と watchpoint。lldb の package（p011）と lldb の aarch64 の
register context（p003）は後の Phase。

## 変更

| 所在 | 変更 |
| --- | --- |
| `include/uapi/reg.h` | aarch64 の `struct reg`（`r_x[31]`・`r_sp`・`r_pc`・`r_pstate`・`r_tpidr`）と `struct fpreg`（`fp_v[32][16]`・`fp_fpsr`・`fp_fpcr`） |
| `include/hal/arch/aarch64.h` | `struct hal_gpregs`・`hal_fpregs`・`hal_vregs`（空。aarch64 に FP の外の vector の状態は無い）と `HAL_DEBUG_*`（step 有り、点は 8 = 命令と data の各 4 まで、長さ 8、4 種とも可）。hal.h が「arch の header が定める」とする部分の aarch64 の分 |
| `src/hal/arm64/debug.c`（新規） | `hal_task_get/set_user_gpregs`・`fpregs`・`vregs`（vregs は拒否）、`hal_task_set/get_single_step`（`MDSCR_EL1.SS` と保存した `SPSR.SS`）、`hal_task_set/get_debug_points`（`DBGBVR/BCR`・`DBGWVR/WCR`、EL0 だけで一致、数は `ID_AA64DFR0_EL1` から）。`arm64_debug_init`（OS lock を外し `MDSCR_EL1.MDE`）、`arm64_debug_switch`（task の切り替えで step と点を載せる） |
| `src/hal/arm64/task.h`・`task.c`・`cmain.c` | task に step と点の field、切り替えで `arm64_debug_switch`、起動で `arm64_debug_init` |
| `src/hal/arm64/int.c` | EL0 からの software step（EC 0x32）→ `HAL_TRAP_CAUSE_SINGLE_STEP`、hardware breakpoint（0x30）と watchpoint（0x34、FAR と WnR）→ `HAL_TRAP_CAUSE_DEBUG_POINT` |
| `src/kern/ptrace.c` | register の変換を arch ごとに（x86_64・aarch64）。`PT_GET/SETXMMREGS` は x86_64 だけ。text への書き込みは書く間「書けて実行できない」にする（AArch64 は W+X を拒む）。実行可能に戻すときに HAL が命令の流れを同期するので、他の process の VA で `ic ivau` をしない |
| `src/kern/syscall.c`・`platform/arm64/vmunix.mk` | aarch64 でも `kern_ptrace`。rpi4 の kernel に `ptrace.c`、HAL に `debug.c` |

HAL の扱い: `include/hal/hal.h` は変えていない。`include/hal/arch/aarch64.h` への追加は、hal.h が宣言だけして
「architecture が自分の header で定める」とした構造体と macro の aarch64 の分（amd64 の `include/hal/arch/amd64.h` と同じ形）で、
2026-09-25 の規則の「実装の補完」と読んだ（判断の記録は下）。

## 検証

| 試験 | 結果 |
| --- | --- |
| rpi4・amd64（外部 package 無しの config）の `disk-image` | warning 0 |
| `plan/ws044/tests/ptrace-test.c`（`build-ptrace-test.sh` で作り、FAT の partition に置き、guest で実行）QEMU raspi4b | **29/29 PASS**（2 回）: SIGSTOP の停止、`PT_GETREGS`・`PT_GETFPREGS`、`PT_READ_I`・`PT_WRITE_I` で BRK、BRK の停止（pc と `TRAP_BRKPT`）、`PT_SETREGS`、1 命令の step（`TRAP_TRACE`）、watchpoint（`TRAP_HWBKPT`、`si_addr`）、hardware breakpoint、x0 を書き換えて子の終了状態で確かめる |
| 同じ試験を amd64（QEMU q35・KVM・NVMe、shared な `ptrace.c` の回帰） | **29/29 PASS**（int3 版） |
| rpi4 の `boot-test.sh`、`dyntest` | PASS |
| amd64 の `boot-test.sh` | PASS |
| style-check | 新規 `debug.c`・`ptrace-test.c` 0 件。`ptrace.c` 67 → 65、`int.c`・`task.c`・`syscall.c` は増えていない |
| 実機（Raspberry Pi 4） | 未実施（ユーザー） |

## 判断が要る点（可逆な既定を選んだ）

- `include/hal/arch/aarch64.h` の debug の構造体と `HAL_DEBUG_*` を承認なしで足した。hal.h の宣言・契約・責務は変えず、hal.h が arch に任せた部分の
  aarch64 の分を埋めたと読んだ。API の変更と見るなら差分は `git show` で戻せる（ptrace を arm64 から外せば元どおり）。
