<!-- awesome-plan project=zedbsd record=ws288-p017 -->

# WS288 p017 CP の CPU と firmware の土台

- 親: [ws.md](../ws.md)。設計: [design.md](../design.md) §8 の p017 の行（§2.4.7 の `atg_cpu`、§2.4.8 の firmware、§2.4.10 の `CPFW`、§6.9）。module の仕様は amaterrace-gpu の `spec/gpu/cp/atg_cpu*.md`。
- 状態: cleared（2026-10-24 Q1: amaterrace-gpu 903f861。atg_cpu（RV32IM、4 段、CPI 1.27）と子 7 つ、CPFW の配線、fw/cp/ の firmware（起動・poll の loop・PARK・GHCI_FRAME・session/memory/buffer/command buffer・FILL/COPY_BUFFER・QUEUE_SUBMIT → BLT の µop）。sim 42/42 PASS（atg_top_fw_tb: class V = class N の memory、record、RESULT/RETIRE、watchdog → RESET → .data の復元、CPFW LOAD）、lint 97 clean。bitstream 1'（ZYBO 2D + CPU）: LUT 8257・WNS +0.463 ns。実機の ZYBO で ghci-blt-board.py --fw 3/3 PASS。1 frame 13 command は 32.5k cycle（class N は 12.5k、command あたり約 15 µs）。残り: Verilator に CPU 無し、fw の host 試験無し、HALTED の class V の STEP、heap は DMEM（CP_HEAP は後））
- 範囲（Q1 の指示、2026-10-23）: `atg_cpu_alu`・`atg_cpu_muldiv`・`atg_cpu_core`・`atg_cpu_mem`・`atg_cpu_win`・`atg_cpu_dma`・`atg_cpu_fw` を spec から実装し、各 module の unit TB と、riscv32 の試験 program を流す CPU の TB を作る。`atg_cp_*`（P3）と `fw/cp/` は触らない。進め方はユーザーの指示（2026-10-23「トップダウンにVerilig実装を進めてください。スタブ、ドライバの使用は恐れずに、パズルのように部分的に進めてください。」）。

## 置き場所と commit

- amaterrace-gpu（worktree `/home/awe/amaterrace-worktrees/p2-cpu`、branch `p2-cpu`、main 1886a2c から、push していない）:
  bd784dc（alu・muldiv）、b2c1530（core と試験 program）、9c6059e（mem）、369964f（win）、cb1a99d（dma）、e7e603d（fw と `ghci_regs` の CPFW）、7bc317e（`atg_cpu_tb` と fw の mailbox の読みの修正）、
  1025b72〜41580a4（timing: core を 4 段に、win の応答を register から、branch の比較器、forwarding の選択を D で決める、fault_pc。試験 program の FAILHOST）、b6297d3（main 98885bf の merge、衝突なし）、0b78ff5（Verilator 5.032 の内部 error を避けて fw の function を wire に）。先頭は **0b78ff5**。
- zedBSD（worktree `/home/awe/zedBSD-worktrees/p2-ws288-p017`、branch `agent/p2-ws288-p017`）: この phase.md だけ。

## 作った物（amaterrace-gpu）

| path | 中身 |
| --- | --- |
| `rtl/gpu/cp/atg_cpu_alu.v` | RV32I の ALU と分岐の比較（組み合わせ） |
| `rtl/gpu/cp/atg_cpu_muldiv.v` | MUL*（DSP、start から 3 cycle）、DIV/REM*（32 step の restoring、34 cycle）、0 除算と INT_MIN/-1 は RISC-V の値 |
| `rtl/gpu/cp/atg_cpu_core.v` | RV32IM の 4 段（F → D（register の読み）→ E → W）。W から D と E への forwarding で data hazard の stall は無い。taken の分岐・jump・mret・割り込みで 2 bubble。load/store は 1 つ、d_ready まで payload を保つ。割り込みは 0x10、隠れた mepc、`mret`（0x30200073）、入れ子無し、WFI。fault（不正命令、CSR/ECALL/EBREAK、misaligned の access と jump 先、IMEM の外の fetch）で PC を出して reset まで止まる |
| `rtl/gpu/cp/atg_cpu_mem.v` | IMEM・DMEM の BRAM（`$readmemh` の初期値、read-first、出力は次の enable まで保つ） |
| `rtl/gpu/cp/atg_cpu_win.v` | data bus の decode と制御の window（PKT_*、DONE、UOP_FIFO、STATE の読み、FENCE_LAST、CYCLE、RESULT、IRQ、IRQ_EN）、sub-bus。どの access も受けた次の cycle に register から答える（2 cycle） |
| `rtl/gpu/cp/atg_cpu_dma.v` | line の DMA: 読みは 4 つまで outstanding（tag 0〜3、line buffer 4 つ。応答は back-pressure 無しなので空きの buffer がある時だけ発行）、書きは DMEM から集めて全 byte で書き、応答まで数える。CPFW の image の load（line の端数の word 数も可） |
| `rtl/gpu/cp/atg_cpu_fw.v` | CPFW の register、LOAD の順序（core を reset に保ち IMEM → DMEM）、RUN/HALT（PARK の握手）、fault の報告 |
| `include/ghci_regs.vh`・`.h` | `GHCI_CPFW_*` の offset と bit |
| `sim/tb/atg_cpu_{alu,muldiv,core,mem,win,dma,fw}_tb.v`・`atg_cpu_tb.v` | unit TB と atg_cpu 全体の TB |
| `sim/tb/cpu/` | 試験 program: `gen.py`（rv32i・rv32m を Python の RV32IM の参照 model で期待値つきで生成）、`irq.S`、`fault.S`、`cpu_top.S`（firmware のする事を window で一通り）、`cpu_reload.S`（CPFW LOAD で載せる像）、`link.ld`、build 済みの `.hex` |
| `tools/cpu-tests.sh` | 共有の clang（`/home/awe/zedBSD-claude1/build/llvm/bin`、読むだけ）で `.S` → `.hex`。sim/run.sh は compiler を要らない |

## spec の変更（同じ commit で直した）

- CPFW の HCR の配置: 11 個の register が 0x120〜0x13F（8 word）に入らず SA が 0x140 に固定なので、0x120 CTRL … 0x138 VERSION、0x13C 予約、**MBOX0〜3 を 0x150〜0x15C**。Q1 が承認し（2026-10-23）、zedBSD の design.md §2.2 を Q1 が 83a1e4c0d で直した。DMEM の base は spec の 0x0001_0000 のまま（Q1 確認）。
- window に **IRQ_EN（0x0D、reset 0）** を足した。halt_req は firmware が消せない level なので、mask が無いと handler が戻れない（livelock）。boot の前の割り込みも防ぐ。
- FW_STATUS の書き込みの [16] **PARK**: HALT（CPFW_CTRL.HALT か CTRL.HALT）の時、firmware が packet を終えて PARK を書き、そこで core が止まる（packet の途中で止まって v_done が来ない詰まりを避ける）。CPFW_STATUS に [3] STOPPED・[4] LOADING。
- CPFW の reset: atg_cpu の rst_n は CP の core_rst_n（CTRL.RESET）なので、残すべき IBASE/IWORDS/DBASE/DWORDS と READY は reset を持たない register にした（初期値は configuration）。
- core: **3 段 → 4 段**（spec は「3 stages」だったが、3 段の形は BRAM → register の読み → ALU → data の decode → load の data → register の書き込みが 1 cycle で、7010 の 10 ns で WNS −8.2 ns だった）。i_rdata は i_en = 0 の間保たれる、fault の原因、run = 0 の時の進行中の access・乗除算の完了、fault_pc は fault と共に有効でその後保持。muldiv: busy/done の cycle。mem: read-first・出力の保持・同じ word の衝突。win: PKT_INFO の taken、DONE は FIFO が空いてから v_done 1 回、v_valid が一度落ちるまで古い packet を見せない、window は word 単位、どの access も 2 cycle（d_ready が同じ cycle の d_addr に依らない: timing）。dma: buffer と tag、busy 中の GO、load の待ち、error の消え方。

## 確認（host。QEMU・板は無し）

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| lint | `tools/lint.sh`（Verilator 5.032 `-Wall`） | merge の後、0b78ff5 で全部 clean（全 module と atg_top の各形、board top） |
| unit TB | `sim/run.sh`（iverilog 12.0、warning も FAIL） | main 98885bf の merge の後（b6297d3 + 0b78ff5）に全 23 PASS（atg_cpu の 8 つ: atg_cpu、atg_cpu_alu、atg_cpu_core、atg_cpu_dma、atg_cpu_fw、atg_cpu_mem、atg_cpu_muldiv、atg_cpu_win、と既存・P1/P3 の 15）。merge の前に `SEED=5`（atg_cpu）、`SEED=7`（atg_cpu_dma）でも PASS |
| Verilator の harness | `sim/verilator/run.sh` | PASS verilator（6120 cycle、既存の物で CPU は入っていない） |
| core の命令の試験 | `atg_cpu_core_tb` | rv32i（ALU の rr/imm 400、分岐 150 + 後ろ向きの loop、JAL/JALR 30、LUI/AUIPC、x0、load/store の全幅を model と照合 264）、rv32m（8 種 × corner 13×7 + random 200、結果の即時使用）、irq（loop・WFI・1〜40 cycle の全距離の割り込みで結果が変わらない、entry 42 回、入れ子無し、0x10 に入る）、fault 11 種（PC と停止、後の store が起きない）、stall（data port の random な待ち + run の random な 0 で rv32i・rv32m・irq が通る、handshake の保持、run = 0 の 40 cycle 後に stopped） |
| TB の感度 | 変異を入れて TB が落ちるか（build/mut、使い捨て） | SRA→SRL、REM の符号、入れ子、LH の符号拡張、mepc、data port の handshake、DMA の tag・error 後の格納・書きの数え: どれも FAIL になる事を確かめた |
| 性能（TB の中） | `atg_cpu_core_tb` の info | 4 段で rv32i 6220 命令 / 7916 cycle（CPI 1.27、TB の data port は即答。atg_cpu の中では load/store が 2 cycle なのでもう少し増える）、rv32m 6432 命令 / 25399 cycle（除算が多い） |

TB の途中で見つけて直した物: `atg_cpu_fw` の mailbox の読みを function で書いていて、iverilog の `@*` が function の中の register を見ず、同じ index を読み続けると古い値になった（`atg_cpu_tb` の poll で発見。mux の wire に直した）。試験 program の失敗の報告（`2n+1` を計算して TOHOST へ）が、壊れた forwarding の下では 1 になって PASS に見えた（変異の試験で発見）: 失敗は別の番地 FAILHOST（0xF000_0010）への store にし、TB は register file を random な値で始める（X が古い値の読みを隠さない）。main の merge の後、Verilator 5.032 が board top の lint で `atg_cpu_fw` の function `merge` に内部 error（V3Gate）を出したので wire の式に直した。

## 合成の見積もり（Vivado 2026.1、out-of-context、atg_cpu だけ）

out-of-context（`synth_design -mode out_of_context -top atg_cpu`、opt・place・route、clock だけの制約、IO の遅延は無し）。script は使い捨てで `build/vivado-cpu/`（git に無い）。`tools/vivado-build.sh` は board top 用なので使っていない。IMEM/DMEM の初期値は `cpu_top` の像。

| 形 | part、clock | LUT（うち LUTRAM） | FF | BRAM36 | DSP | WNS / WHS |
| --- | --- | --- | --- | --- | --- | --- |
| ZYBO（32 byte の line） | xc7z010clg400-1、100 MHz | 3000（252） | 1600 | 12 | 4 | +0.341 / +0.088 ns |
| KC705（64 byte の line） | xc7k325tffg900-2、200 MHz | 3392 | 1857 | 12 | 4 | +0.116 / +0.077 ns |

ZYBO の内訳（階層）: core 1089（muldiv 472、alu 82 + 分岐の比較器）、dma 652（line buffer の LUTRAM 184）、fw 389、mem 409、win 414。design §2.4.7 の見積もり（LUT 2.5k、FF 1.5k、DSP 1、BRAM36 12）に比べ LUT +0.5k、DSP 4（33×33 の積を 1 cycle で: DSP を 1 つにするには積を多 cycle にする。今は未対応）。OOC なので、全体の bitstream 1' に入れた時の timing は未確認。
経過（ZYBO の WNS）: 3 段 −8.2 → 4 段 −5.0 → win の応答を register から −1.9 → fault を e_done から外す・d_valid の gate を load/store の検査だけに −0.43 → forwarding の選択を D で −0.04 → fault_pc の enable を外す +0.27 → 分岐の比較器 +0.34（KC705 も +0.12）。

## 未実施・残り

- QEMU・板の確認は無い（この部分は RTL だけ。板の確認は bitstream 1' の Phase の部分）。
- `atg_cp_regs` の EXTCAP の CPFW の entry、`atg_cp` の `x_rdata` への atg_cpu の hcr_rdata の配線、CAPS[17]/[18] は P3/P1 の module で、未確認。
- design の M21（draw 1 つの変換の時間）と R26（firmware の大きさ）の測定は firmware が出来てから。
- `atg_cpu_core_tb` の data port の model は即答と random な待ちで、atg_cpu_win の 2 cycle の形そのものは `atg_cpu_tb` で通している。
- 試験 program の `.hex` は commit してあり、`tools/cpu-tests.sh` で作り直せる（gen.py は決まった seed で同じ物を出す）。


## 後半: firmware と class V の道（2026-10-23〜24 P2、Q1 の指示「the CP firmware base and the class V path」）

置き場所: amaterrace-gpu の worktree `/home/awe/amaterrace-worktrees/p2-fw`、branch `p2-fw`（main 122817f から、push していない）。commit **23b7379**（firmware、TB、watchdog、文書）、**d0ad3bb**（STATE の broadcast の register、timing）。zedBSD: worktree `/home/awe/zedBSD-worktrees/p2-ws288-p017b`（branch `agent/p2-ws288-p017b`）、この phase.md と `plan/tools/zybo/ghci-blt-board.py` の `--fw`。

### 作った物・変えた物（amaterrace-gpu）

| path | 中身 |
| --- | --- |
| `fw/cp/`（`boot.S`・`link.ld`・`hw.h`・`fw.h`・`main.c`・`packet.c`・`cmd.c`・`object.c`・`uop.c`・`dma.c`・`heap.c`・`lib.c`・`README.md`） | C の firmware（freestanding、rv32im、compiler-rt 無しで memcpy/memset/memcmp/`__udivdi3`/`__umoddi3` を自前）。起動ごとに `.data` を像の原本から写す、poll の loop（WFI は使わない: IRQ_EN を開けてから WFI の間の割り込みで戻り先の WFI を誰も起こさない）、halt の要求で PARK、heartbeat。GHCI_FRAME の parser と結果の record、session・memory・buffer と bind・command pool と buffer・BEGIN/END/RESET・記録の CMD_FILL_BUFFER と CMD_COPY_BUFFER（32 bit か 16 bit の pixel の BLT の µop）・QUEUE_SUBMIT（記録の µop を再生） |
| `fw/cp/images/atg1-cp-{zybo,kc705}.{imem,dmem}.hex` | BRAM の初期値（commit してある。IMEM 12.8 KB、DMEM の像 588 B、line 32/64） |
| `tools/fw-build.sh` | 共有の clang（読むだけ）で build して上の像を書く |
| `include/ghci_frame.h`、`docs/ghci.md` §9・付録 | GHCI の gpu-op（0x10100〜、案）、GHCI_FRAME（n = 8）、staging の record `[number][flags][result index][引数]`、result `{VkResult, aux}`、frame の result `{VkResult, 解いた record の数}` |
| `rtl/gpu/cp/atg_cp_ctl.v`（+ spec・TB） | port `wdt_hold`: watchdog の数えを止める（reset しない）。TB に「hold の間は数えない」を追加 |
| `rtl/gpu/cp/atg_cp.v`（+ spec） | watchdog は class V の packet が開いている間（`v_valid`）も数え、µop の無い時の `cpu_stall` で止める（design §2.4.7）。STATE の broadcast（`st_*`）を 1 段の register に（下の timing） |
| `sim/tb/atg_top_fw_tb.v` | atg_top（HAS_CPU、firmware の像）を ZYBO と KC705 の形で、host の port だけで |
| `boards/zybo/atg_zybo_2dfw.v`・`.xdc`、`tools/vivado-build.tcl`、spec | bitstream 1'（板で実績のある atg_zybo_2d の写しに HAS_CPU と像）。tcl は `fw/cp/images/*.hex` を run の directory に写す |

CPFW の EXTCAP の entry（0x108: id 6 → 0x120）、`x_rdata`、CAPS[17]/[18] は main 122817f で既に配線されていた（確認だけ、TB と板で確認）。

### 確認

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| sim | `sim/run.sh`（iverilog 12.0） | d0ad3bb で 30/30 PASS（新しい `atg_top_fw` を含む） |
| `atg_top_fw` | 同上 | ZYBO・KC705 とも `.frame`（class V の frame の fill + copy が class N の packet と同じ memory、model とも一致、13 record の結果、frame の結果、RESULT・RETIRE の IRQ）、`.unknown`（未知の command で frame が止まり後ろの record は走らない）、`.watchdog`（RUN 0 で誰も取らない class V packet → FAULT 種別 4 → RESET → firmware の再起動で `.data` が戻る）、`.reload`（CPFW LOAD: 0 の像で PC 0 の fault、次に memory の像（`.data` を変えた物）が走り frame が通る） |
| TB の感度 | fill の値を `data ^ 1` にした firmware | `atg_top_fw` が FAIL（戻して PASS） |
| lint | `tools/lint.sh`（Verilator 5.032） | 全 97 clean |
| Verilator の harness | `sim/verilator/run.sh` | PASS（CPU は入っていない、既存の物） |
| 合成（Vivado 2026.1、`tools/vivado-build.sh zybo atg_zybo_2dfw`） | 1 回目 | WNS −0.158 ns（ring → sequencer → index の decode → BLT の shadow の CE、route 80%） |
| 同上 | STATE の broadcast を register にした後（d0ad3bb） | **LUT 8257（46.9%）、FF 8065、BRAM36 13、DSP 4、100 MHz で WNS +0.463 ns、WHS +0.034 ns**、bitstream sha256 f1fd6c61698991f8…、`$readmem` の像を読んだと log に有る |
| **板（実機の ZYBO、SD 起動、10.0.30.2、fpgaload + fpgareg だけ）** | `plan/tools/zybo/ghci-blt-board.py --fw --bit …/atg_zybo_2dfw.bit`、続けて 2 回（load 済み） | 1 回目は RESULT の確認の誤り（`fpgareg wait --ack` が IRQ_STATUS を消す）だけ FAIL、record と frame の結果は正しかった。script を直して **3/3 PASS**: 識別（CAPS 0x00561803、EXTCAP PERF → CPFW）、既存の BLT・ring・HALT・fault・RESET の全 case、firmware の起動（state 2、VERSION、MBOX3 の `.data`）、class V の frame の fill + copy（64 word を model と照合）、13 record と frame の結果、起きた時の status に RESULT と RETIRE。2・3 回目は最初の RESET の後の再起動で `.data` が戻ることも板で見えた |

性能（M21 の代わりの測定、TB の ZYBO の形）: 13 record の frame が doorbell から fence まで 32.5k cycle（同じ byte の class N の fill + copy は 12.5k）。firmware の仕事は 1 command あたり約 1.5k cycle（100 MHz で 15 µs）。reader の slow path を noinline にする前は 44k。

### 設計からの違い・決めた事（Q1 の確認が要る物は ※）

- ※ GHCI_FRAME の「frame done」は、packet の dword3（RESULT flag）に `{VkResult, 解いた record の数}` を書く形にした（design §2.4.6 は場所を決めていない）。未知の command は frame をそこで終える（wire に長さが無い）。
- ※ heap は DMEM（32 word の chunk 48 個、object 96 個）。design §2.4.7 の CP_HEAP_BASE/LIMIT は window に出ていないので、GPU memory の heap は後（window に 2 register と ctl の port が要る）。
- 記録は wire の写しでなく µop の packet そのもの（SET_REG 11 組 + BLIT(wait)）。p017 の fill/copy には同じ意味で、p018 の状態の有る command では wire の記録が要るかもしれない。
- CPFW_CTRL.RUN は RESET で 0（atg_cpu_fw の spec のまま）。driver は RESET の後に RUN を書く（docs/ghci.md の付録に書いた）。
- STATE の broadcast の 1 cycle の遅れ: kick は最後の STATE の書き込みから 2 cycle 以上後（header の後に kick）なので block は kick の前に状態を持つ。全 sim と板で確認。

### 残り

- Verilator の harness に CPU と firmware（`sim/verilator/`）、`fw/cp/tests/` の host の単体試験: 未実施。
- HALTED での STEP の class V packet は走らない（CTRL.HALT の間 core は PARK）。
- `ghci-blt-board.py` の `--fw` 無しの形（bitstream 1）は今回の変更の後に板で流していない（`py_compile` だけ）。
- 規約の全文の見直し（p016）は未。