<!-- awesome-plan project=zedbsd record=ws080 -->

# WS080: `ld.coff` — Win64 PE/COFF の動的ローダ

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG002
Related Milestones: MG006
Objectives: O1
Parent: [Master](../master.md)
Queue: なし
Resume point: p001（設計）から。GS base は案 A（swapgs）に決定（差分の承認は p001 の後）。path は `/usr/libexec/ld.coff`・`/usr/lib/coff64/` に決定（Win64 の名前は使わない）。残りの判断: source の置き場（案 userland/base/ld-coff・userland/coff64）・native の橋・優先度
<!-- awesome-plan-current:end -->

## 目標

2026-09-28 ユーザーの仕様 [spec.md](spec.md)（原文）: Kei / zedBSD 上で Win64 PE32+ の実行形式を直接実行するユーザーランドの動的ローダ `ld.coff`。
Windows NT のプロセスの初期化を再現せず、PE/COFF の形式と Win64 ABI の互換の基盤を作り、その上に Win64 の DLL の互換実装を積む。
`ntdll.dll`・`LdrInitializeThunk` を経由せず、ロードの後 PE の `AddressOfEntryPoint` へ直接移る。zedBSD の kernel に Win32・NT の概念を持ち込まない。

v0.1 の完了の条件（spec §24）: PE32+ AMD64 の解析、EXE・DLL・section の mapping、base relocation、import（名前・ordinal）、export（名前・ordinal・
forward）、DLL の依存の解決、IAT の書き換え、Microsoft x64 ABI の呼び出し、`DLL_PROCESS_ATTACH`、EXE の entrypoint への移動、GS base の基本。
最初の実用の目標: 自作の最小の PE EXE + 自作の DLL が動く。その後 `kernel32.dll` → CRT → 上位の DLL。

## 調べた事実（2026-09-28 main）

- **GS base**: amd64 の HAL は `IA32_GS_BASE` を kernel の per-CPU のデータに使い（`src/hal/amd64/percpu.c`）、`swapgs` を使っていない。
  `struct hal_gpregs` の `gs_base` は「reserved; user code here has no gs base」（`include/hal/arch/amd64.h`）。user の FS base は thread ごとに
  保存・復元している（TLS）。**user の GS base を thread ごとに持つには、kernel の入口・出口の `swapgs`（または KERNEL_GS_BASE の扱い）と
  thread の文脈での保存・復元が要り、HAL の責務の変更（と hal.h の API の可能性）になる → 差分ごとのユーザーの事前の承認が要る。**
- **PE の実行の検出**: kernel の exec は ELF を扱う。`MZ`/`PE\0\0` を見て `ld.coff` を interpreter として起こす経路（spec §4）は kernel の変更。
- **試験の PE の作り方**: 共有の toolchain に `lld-link` がある（`build/llvm/bin/lld-link`）。clang の `--target=x86_64-pc-windows-msvc` と
  `-nostdlib`・`/entry:` で CRT 無しの最小の EXE・DLL を host で作れる見込み（p001 で確かめる）。

## 判断が要る点（ユーザーへ）

1. **GS base（spec §15）**: 2026-09-28 ユーザー「では推奨案にしましょう。」→ **案 A: `swapgs`**（Linux・FreeBSD と同じ）。kernel の per-CPU の
   pointer を `KERNEL_GS_BASE` に置き、kernel の入口・出口（syscall・割り込み・例外・NMI）で `swapgs` で入れ替え、user の GS base を thread の文脈に
   保存・復元する。入口の判断は保存した CS（user か kernel か）で行う。案 B（kernel が GS を使わず per-CPU を kernel stack の上から引く）は採らない。
   **方式は決定。具体的な差分（hal.h の `gs_base` の扱いを含む）は p001 で plan に置き、差分ごとの承認を得てから適用する**（AGENTS.md の HAL の規則）。
   p001 で CR4.FSGSBASE の状態（user の `rdgsbase` で kernel の pointer が見えるか）も確かめる。
2. **path**: 2026-09-28 ユーザー「zedBSDの既存レイアウトに合わせて、`/usr/libexec/ld.coff`、`/usr/lib/coff64/` を採用したいです。
   win64は商標の関係で使えないですね。」→ loader は `/usr/libexec/ld.coff`、互換の DLL は `/usr/lib/coff64/`。
   探索の順（spec §9 の読み替え）: 1. 実行ファイルと同じ directory → 2. `/usr/lib/coff64/` → 3. `COFF_LIBRARY_PATH`。
   **名前の規則**: 商標のため、OS に見える名前（path・program・package・menu・UI）に「Win64」「Windows」を使わない（`coff64` 等にする）。
   設計の文書で ABI を説明する技術の用語としての言及は可（main の解釈）。
3. **source の置き場**（案、path の決定に合わせて改めた）: `userland/base/ld-coff/`（ローダ）と `userland/coff64/`（互換の DLL、PE の DLL として build）。
4. **互換の DLL から zedBSD の機能を呼ぶ道**（spec §22）: 互換の DLL は PE の世界の code なので、zedBSD の syscall・libc を呼ぶ橋が要る
   （例: `ld.coff` が native の関数の表を PE の export として見せる内部の DLL、名前は案で `kei.dll`）。p001 で設計する。
5. 優先度: デモ（fg010）の後か、並べるか。

## Phase（案）

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| ws080-p001 | 設計: 層の分け方（spec §2）、kernel の汎用の機能（PE の exec の検出、GS base、page の保護）、`ld.coff` の module の namespace・探索・import/export・依存の graph・初期化の順、Microsoft x64 ABI の thunk、最小の TEB/PEB の offset、TLS と unwind の拡張の余地、互換の DLL から native を呼ぶ橋、試験の PE の作り方。GS base の HAL の差分を plan に置く | planning | — |
| ws080-p002 | kernel: PE の exec の検出で `ld.coff` を起こす、GS base（承認の後）、必要なら page の保護の補助 | planning | p001、HAL の承認 |
| ws080-p003 | `ld.coff` の PE32+ の解析と mapping（section・保護・base relocation `DIR64`、ASLR 前提）、host の試験 | planning | p001 |
| ws080-p004 | DLL の loader（namespace・探索の順・大文字小文字を区別しない名前・依存の graph・LOADING/LOADED/INITIALIZED）、import（名前・ordinal）、export（名前・ordinal・forward、循環の検出）、IAT | planning | p003 |
| ws080-p005 | Microsoft x64 ABI の境界の thunk、最小の TEB/PEB（GS:[0x30]・GS:[0x60]、`offsetof` の assert）、`DllMain(DLL_PROCESS_ATTACH)`、EXE の entrypoint への移動と return → process の終了。自作の最小の EXE + DLL が動く | planning | p002、p004 |
| ws080-p006 | 最初の互換の DLL（`kernel32.dll` の最小: `ExitProcess`・`GetStdHandle`・`WriteFile`・`GetCommandLineA` 等）と native の橋。hello world の EXE | planning | p005 |
| ws080-p007 | v0.1 以降の拡張の順の計画（static TLS・TLS callback・unwind の登録・ucrtbase） | planning | p006 |
| ws080-p009 | 全文規約確認と回帰（必須の最終確認） | planning | 全 Phase |
