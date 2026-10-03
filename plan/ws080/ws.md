<!-- awesome-plan project=zedbsd record=ws080 -->

# WS080: `ld.coff` — Win64 PE/COFF の動的ローダ

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG002
Related Milestones: MG006
Objectives: O1
Parent: [Master](../master.md)
Queue: なし
Resume point: p001 の設計の文書（[design.md](design.md)）と HAL の差分の案（[proposed/hal-gs-base.md](proposed/hal-gs-base.md)、未適用）まで完了（2026-09-29）。design-reviewer の review、design.md §19 の判断 D1〜D17、HAL の承認（A1・A2）を待つ。p003・p004・p005 は承認を待たずに始められる。GS base は案 A（swapgs）に決定（差分の承認は p001 の後）。path は `/usr/libexec/ld.coff`・`/usr/lib/coff64/` に決定（Win64 の名前は使わない）。source は `userland/base/ld-coff/`・`userland/desktop/w64/` に決定。native の橋は置かない（互換の DLL が UAPI を直接呼ぶ）に決定。優先度: デモ critical の後に loader を先に仕上げ、DLL は下位のモデルで継続（決定）
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
5. **優先度と進め方**（2026-09-28 ユーザー）: デモ critical（PDF Viewer・Notes・Keiland の仕上げ・ブラウザ）の実装の後に、**loader（p001〜p008）を早めに完成させる**。
   **互換の DLL（p009 以降の kernel32・ucrtbase・user32 等）は下位のモデルの subagent にひたすら実装させる**（Agent の model を sonnet・haiku 等に）。
   そのため p001 の設計で、DLL の API ごとの実装の単位・試験の型（host の試験と guest の EXE）・完了の判定を、下位のモデルが迷わず回せる形に決めておく。

## Phase（2026-09-28 に分けた。上の決定を反映）

| Phase | 目的 | 完了の条件 | Status | 依存 |
| --- | --- | --- | --- | --- |
| [ws080-p001](phase001/phase.md) | 設計の文書（`plan/ws080/design.md`）: 3 つの層、kernel の汎用の機能、`ld.coff` の構成、module の namespace・探索・import/export・依存の graph・初期化の順、MS x64 ABI の境界、最小の TEB/PEB の offset、PE の側の runtime、LLP64 の header の生成、syscall の stub の仕様（上の register の規則）、試験の PE の作り方（clang + `lld-link`）、`LoadLibrary`・`GetProcAddress` が実行時に `ld.coff` の loader を使う道（橋の DLL を置かない形で）、Wayland の道。**GS base の HAL の差分を plan に置く** | 設計の文書と HAL の差分の案、design-reviewer の review | uncleared（文書と案は完了。review・判断・承認待ち） | — |
| ws080-p002 | HAL と kernel: `swapgs` の方式、thread ごとの user の GS base の保存・復元、user が GS base を設定・取得する汎用の UAPI、CR4.FSGSBASE の扱い | **HAL の差分の承認の後**。全ての入口（syscall・割り込み・例外・NMI）の試験、既存の回帰（boot test・desktop・i915 の実機）、user の GS base が thread の切り替えで保たれる試験 | planning | p001、HAL の承認 |
| ws080-p003 | kernel: exec が `MZ`/`PE\0\0` を見て `/usr/libexec/ld.coff` を interpreter として起こす（argv の約束） | PE を exec すると ld.coff が起き、元の path と argv を受け取る試験 | planning | p001 |
| ws080-p004 | LLP64 の UAPI の header の生成の script と生成物（固定幅の型、LP64 の元との offset・大きさの `_Static_assert`） | 生成物が clang の windows の target で通り、LP64 との照合が全て合う。UAPI の変更を検出する試験 | planning | p001 |
| ws080-p005 | PE の側の build の基盤: make の規則（clang `--target=x86_64-pc-windows-msvc` + `lld-link`）、PE の側の最小の runtime（静的な library）、syscall の stub、CRT 無しの試験用の EXE・DLL の fixture | stub の register の保存の host の試験、fixture の EXE・DLL ができる | planning | p001、p004 |
| ws080-p006 | `ld.coff` の核: PE32+ の解析、`SizeOfImage` の確保、section の配置と 0 埋め、base relocation（ABSOLUTE・DIR64、ASLR 前提）、section の保護（恒久の RWX を作らない） | host の試験（fixture と壊れた PE の fuzz） | planning | p005 |
| ws080-p007 | DLL の loader: module の namespace、探索（実行ファイルの directory → `/usr/lib/coff64/` → `COFF_LIBRARY_PATH`）、大文字小文字を区別しない名前、import（名前・ordinal）、export（名前・ordinal・forward、循環の検出）、依存の graph（LOADING/LOADED/INITIALIZED）、IAT | host の試験（依存の graph・forward・循環・ordinal） | planning | p006 |
| ws080-p008 | 実行: MS x64 ABI の thunk、最小の TEB/PEB（GS:[0x30]・GS:[0x60]、`offsetof` の assert）、GS base の設定、`DllMain(DLL_PROCESS_ATTACH)`、EXE の `AddressOfEntryPoint` への移動、return → process の終了 | **v0.1 の到達点**: guest で自作の最小の EXE + 自作の DLL が動く | planning | p002、p003、p007 |
| ws080-p009 | 最初の互換の DLL（`userland/desktop/w64/kernel32`）: `ExitProcess`・`GetStdHandle`・`WriteFile`・`ReadFile`・`CreateFileW`・`CloseHandle`・`GetCommandLineW`・`VirtualAlloc/Free/Protect`・`GetLastError/SetLastError`・`GetModuleHandleW`・`LoadLibraryW`・`GetProcAddress`・`CreateThread`（UAPI と GS base）。UAPI を直接呼ぶ | guest で console の hello world の EXE、file の読み書き、thread の EXE | planning | p008 |
| ws080-p010 | CRT への道: `ucrtbase.dll` の最小（printf 系・malloc・文字列）か、先に CRT を静的に link した program（MinGW 系）で試すかを決めて最初の段を作る | CRT を使う hello world | planning | p009 |
| ws080-p011 | v0.1 の後の拡張の計画と最初の一歩: static TLS・TLS callback・`.pdata` の unwind の登録・`DLL_THREAD_ATTACH`、user32 等が Wayland と直接話す道 | 計画の文書と最初の一歩 | planning | p010 |
| ws080-p012 | 全文規約確認と回帰（必須の最終確認） | 規約と回帰 | planning | 全 Phase |

実行の順（案）: p001 → （承認を待つ間に）p003・p004・p005 → p006 → p007 → p002（承認の後）→ p008 → p009 → p010 → p011 → p012。
p002 は全ての kernel の入口を変えるので、単独の Phase にして広い回帰（boot test・desktop の Venus・i915 の実機）を行う。
