<!-- awesome-plan project=zedbsd record=ws080-p005 -->

# ws080-p005: PE の側の build の基盤・最小の runtime・syscall の stub・fixture（計画）

Status: planned（計画だけ。2026-10-04 ユーザー「P1は設計と計画のみに専念」で実装はしない）
Disposition: normal
Parent: [WS080](../ws.md)
Queue: q676 の 2 つ目（未着手）。Approval: 2026-10-04 ユーザー「PE/COFFローダも進めてオーケーです。」
依存: p001（設計、review 待ち）、**p004 の生成物**（runtime が `zuapi/errno.h`・`zuapi/syscall.h` を使う）。

## 範囲（design.md §14・§15.1・§15.2・§16.1）

1. **build の規則**: `userland/desktop/w64/pe.mk`（仮）。compile `build/llvm/bin/clang --target=x86_64-pc-windows-msvc -ffreestanding -nostdlibinc -fno-stack-protector -mno-stack-arg-probe -O2 -Wall -Wextra -Werror`、link `build/llvm/bin/lld-link /nologo /nodefaultlib /dynamicbase /entry:... /subsystem:console`（DLL は `/dll /def:`）、import library は `lld-link /lib /def: /machine:x64`。出力は `$(BUILD)/w64/`。platform の mk への登録（image に入れる）は p008 以降で main に依頼。
2. **runtime** `userland/desktop/w64/runtime/` → `coffrt.lib`: `memcpy`・`memmove`・`memset`・`memcmp`、`strlen` 等、UTF-8⇔UTF-16、`coff_error_from_errno()`、`__chkstk`、`_fltused`、`coff_format`（診断用）。新しい C は coding-style の全文。
3. **syscall の stub** `runtime/syscall.S`: `coff_syscall0`〜`6`、§14 の表と register の規則を source の先頭の comment に、RBX・RSI・RDI・RBP の push/pop、a4〜a6 は `[rsp+0x48/0x50/0x58]`、`.seh_*` の unwind。
4. **fixture** `userland/base/ld-coff/tests/fixtures/`: §16.1 の表のうち `gs.exe` 以外（ret42・reloc・probe+use-probe・ordinal・fwd・loop-a/b・cyc-a/b・missing・deps）。壊れた PE の集合は p006。

## 受け入れの条件

1. host の stub の試験: Linux の ELF の `ms_abi` の caller から試験用の変種の stub で `getpid`（Linux の番号）を呼び、RBX・RBP・RDI・RSI・R12〜R15・XMM6〜15 の印が保たれ、`syscall` の直前の register の割り当てが §14 の表どおり。
2. fixture の EXE・DLL が warning 0 で build でき、`llvm-readobj` で import・export・forward・ordinal・DIR64 の relocation が期待どおり（script で検査）。
3. runtime の部品の host の試験（mem*・UTF 変換・errno の表）。guest の試験（XMM の保存を含む）は p008 の後に T1 へ。

## 作業の順（案）

build の規則と ret42.exe → stub と host の試験 → runtime → 残りの fixture と readobj の検査。1 段ごとに WIP commit。

## 未決

- p004 が未完了（生成物が無い）。runtime の errno の表は p004 の後。stub と build の規則と fixture は p004 を待たずに始められる。
- `make` の入口（独立の `pe.mk` か platform の mk か）は main の判断。
