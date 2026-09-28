<!-- awesome-plan project=zedbsd record=ws080 -->

# WS080: `ld.coff` — Win64 PE/COFF の動的ローダ

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG002
Related Milestones: MG006
Objectives: O1
Parent: [Master](../master.md)
Queue: なし
Resume point: p001（設計）から。GS base は案 A（swapgs）に決定（差分の承認は p001 の後）。path は `/usr/libexec/ld.coff`・`/usr/lib/coff64/` に決定（Win64 の名前は使わない）。source は `userland/base/ld-coff/`・`userland/desktop/w64/` に決定。native の橋は置かない（互換の DLL が UAPI を直接呼ぶ）に決定。残りの判断: 優先度
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
3. **source の置き場**: 2026-09-28 ユーザー → `userland/base/ld-coff/`（ローダ）と `userland/desktop/w64/`（互換の DLL、PE の DLL として build、`/usr/lib/coff64/` に install）。
4. **互換の DLL から zedBSD の機能を呼ぶ道**: 2026-09-28 ユーザー「kei.dllみたいなブリッジはなしで、kernel32.dllとかが直接にzedBSD UAPIを呼び、
   Waylandコンポジタと通信するのがいいと思います。ブリッジdll を挟まない利点は明確で、ブリッジ層が1枚減る、bootstrapが簡単、デバッグしやすい、
   呼び出しコストも減る。特に kernel32.dll がファイル・VM・thread・process系のUAPIを直接叩くのは自然です。」
   → **橋の DLL は置かない。** `kernel32.dll` 等は zedBSD の UAPI（syscall）を直接呼び、GUI の DLL（user32 等）は Wayland の compositor と直接通信する。
   p001 で設計する点（main の整理）:
   - **UAPI の header（2026-09-28 ユーザー）**: 「既存のUAPIのヘッダを直接使わずに、LLP64で使える専用の、整数の幅を指定したuint64_tとか
     uint32_tとかで表現しているヘッダを、機械的に生成するのがいいと思うなあ。」→ 既存の `include/uapi` を PE の build で直接使わない。
     LLP64 用の専用の header を**生成の script で機械的に作る**（`long`→`int64_t`/`uint64_t` 等、全ての型を固定幅に、struct の offset と大きさを
     LP64 の元と照合する `_Static_assert` も生成）。生成物と script は tree に置き、UAPI が変わったら再生成と照合の試験で検出する。
   - **syscall の stub**: Microsoft x64 ABI の関数から zedBSD の syscall へ移る小さな asm の stub。**stub の仕様に register の規則を明記する**
     （2026-09-28 ユーザー「syscall は RCX と R11 を破壊するので、Microsoft x64 ABIのvolatile register規則とzedBSD syscall ABIのclobber規則を
     stub仕様に明記しておく」）。今の zedBSD の amd64 の syscall の ABI（`src/hal/amd64/int.c`・`src/libc/crt/crt0-amd64.S`）:
     番号は RAX、引数は RBX・R10（`syscall` の時。`int` の時は RCX）・RDX・RSI・RDI・RBP、戻り値は RAX、`syscall` 命令が RCX（戻り先）と R11（RFLAGS）を壊す。
     Microsoft x64 ABI: 引数は RCX・RDX・R8・R9 と stack（shadow の 32 byte の上、5 番目は [rsp+0x28]）、volatile は RAX・RCX・RDX・R8〜R11・XMM0〜5、
     **non-volatile は RBX・RBP・RDI・RSI・RSP・R12〜R15・XMM6〜15**。帰結:
     - RCX・R11 を syscall が壊すのは MS の volatile の範囲なので問題ない。ただし第 1 引数が RCX で来るので、`syscall` の前に RBX 等へ移す。
     - **zedBSD の引数の register の RBX・RSI・RDI・RBP は MS では non-volatile** なので、stub が push/pop で保存・復元する（事故の本命）。
     - 5・6 番目の引数は stack から読む。MS ABI には red zone が無いので stub は rsp の下を使わない。
     - kernel が syscall の間に XMM6〜15 を保つこと（kernel の FPU の扱い）を p001 で確かめて仕様に書く。保たないなら stub が保存する。
     - 戻り値（負の errno 等）から Win32 の error（`SetLastError`）への変換は stub でなく kernel32 の側で行う。
     - stub の host の試験: 全ての non-volatile の register に印を置いて syscall を呼び、戻った後に全て保たれていることを検査する。
   - **PE の側の最小の runtime**: libc の無い PE の code 用に、memcpy・文字列・errno 相当等の最小の部品を**静的な library**（DLL ではない）として
     各互換 DLL に link する（実行時の層は増えない）。
   - **thread**: `CreateThread` は zedBSD の thread の UAPI で作り、新しい thread の GS base を TEB に向ける（p002 の GS base の汎用の機能）。
   - **Wayland**: user32 等が PE の code として Wayland の wire protocol を話す。自前の libwayland（`userland/desktop/libwayland`）の source を
     PE の target で build できるか（依存する libc の関数を上の runtime で満たせるか）を調べる。
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
