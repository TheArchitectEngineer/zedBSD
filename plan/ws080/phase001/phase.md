<!-- awesome-plan project=zedbsd record=ws080-p001 -->

# ws080-p001: `ld.coff` の設計

Status: uncleared（設計の文書と HAL の差分の案は完了。design-reviewer の review と、判断の点 D1〜D17・HAL の承認が未了）
Disposition: normal
Parent: [WS080](../ws.md)
Queue: main の依頼（2026-09-29、worktree `.claude/worktrees/ws080-coff`、branch `wt/ws080`、main 5b0522e2 から）
Approval: main の依頼「WS080 の p001（設計）」。GS base は案 A（swapgs）に決定済み。HAL の差分は案として `plan/ws080/proposed/` に置き、適用しない。

## 範囲と受け入れ（ws.md の表）

設計の文書（3 つの層、kernel の汎用の機能、`ld.coff` の構成、module の namespace・探索・import/export・依存の graph・初期化の順、MS x64 ABI の境界、
最小の TEB/PEB の offset、PE の側の runtime、LLP64 の header の生成、syscall の stub の仕様、試験の PE の作り方、`LoadLibrary`・`GetProcAddress` の道、
Wayland の道）と GS base の HAL の差分の案。完了の条件: 設計の文書と HAL の差分の案、design-reviewer の review。

## 成果物

- [design.md](../design.md): 設計の全体（§1〜§20）。判断の点は §19（D1〜D17、既定を選び理由を書いた）。
- [proposed/hal-gs-base.md](../proposed/hal-gs-base.md): amd64 の user の GS base の差分の案（A1: hal の API、A2: HAL の責務、A3: debug の点の制限、
  K1: kernel・UAPI）と p002 の試験。**未適用・未 compile**。

## 調べた事実（2026-09-29、source と host）

| 事実 | 出典 |
| --- | --- |
| amd64 の HAL は `IA32_GS_BASE` = per-CPU のまま user に戻る。`swapgs` 無し。CR4.FSGSBASE 無し（PGE・OSFXSR・OSXMMEXCPT だけ） | `percpu.c`・`trap.S`・`asm.c`・`space.c` |
| SYSCALL の入口は最初の命令で `%gs:` を使う。全ての vector は `amd64_common_interrupt`。#DF は IST1、NMI は IST2、#MC・#DB は IST 無し | `trap.S`・`int.c`・`descriptor.c` |
| user の rip・rsp の書き込みは user の address に限られる（`amd64_user_address_valid`）。signal の戻りは kernel の保存した frame。debug の点は address を検査しない | `task.c` |
| syscall は RAX（番号・戻り値）、引数 RBX・R10（`int` は RCX）・RDX・RSI・RDI・RBP。kernel は RAX・RCX・R11 以外の全ての整数の register を戻す | `int.c`・`trap.S` |
| kernel は `-mgeneral-regs-only`。vmunix（ws073-p032 の build）の逆アセンブルで XMM を触るのは FP の状態の保存・復元と self test だけ → syscall は XMM6〜15 を保つ | `platform/amd64/vmunix.mk`、`llvm-objdump` |
| exec は target の解決の loop で `#!` を見て interpreter に置き換える（深さ・循環の検査あり）。PE の検出はここに足せる | `src/kern/exec.c` |
| mmap の領域は 4 GiB から上（LP64）、乱数無し。user の ELF は 0x400000 に link。`MAP_FIXED_NOREPLACE` あり | `src/kern/vmspace.c`、`include/uapi/mman.h`、`platform/amd64/user.ld` |
| `thread_self` は TID・GET_TLS・SET_TLS の op。`thread_create` は `args[4] == 0` を要求（空き） | `src/kern/syscall.c`、`include/uapi/thread.h` |
| 共有の toolchain（`build/llvm/bin`: clang・lld-link・llvm-readobj・llvm-objdump、`llvm-lib`・`llvm-dlltool` は無い）で、CRT 無しの PE の EXE・DLL、DIR64 の relocation、forward と名前無しの ordinal の export、`lld-link /lib /def:` の import library、名前 `ld.coff` からの import ができる | `build/ws080/pe-probe/`（host、toolchain は読むだけ） |

## 確認

| 確認 | 結果 |
| --- | --- |
| PE の build の試作（`build/ws080/pe-probe/`）: `probe.exe`・`probe.dll`・`fwd.dll`・`u.exe` を clang `--target=x86_64-pc-windows-msvc` + `lld-link` で作り `llvm-readobj` で見る | 成功（design.md §15.1） |
| HAL の差分の案を worktree に一時的に当てて vmunix の build と boot test | **未実施**: 実行の環境（auto mode の classifier）が kernel の入口の code の変更を拒んだ。案は source の読みだけで書いた。確認は承認の後の p002 |
| design-reviewer の review | 未実施（main に依頼） |

## 残り・再開の条件

1. design-reviewer の review（main が起動するか、指示があればこの agent が行う）と、指摘の反映。
2. ユーザーの判断: design.md §19 の D1〜D17（既定で進めてよいか）、特に D1（libc 無しの ld.coff）、D2（synthetic の module `ld.coff`）、
   D3（UAPI の GS の op と `thread_create` の args[4]）、D5（argv）、D8（unload 無し）。
3. HAL の承認: [proposed/hal-gs-base.md](../proposed/hal-gs-base.md) の A1（hal の API）と A2（HAL の責務）。A3・K1 は承認不要だが同じ review に含める。
4. 1〜3 が済めば p001 cleared。p003・p004・p005 は承認を待たずに始められる（ws.md の実行の順）。

## Resume point

設計の文書と HAL の案まで完了（2026-09-29）。review・判断・承認を待つ。
