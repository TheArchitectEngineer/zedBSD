<!-- awesome-plan project=zedbsd record=ws288-p002 -->

# WS288 p002 amaterrace-gpu の骨組み・GHCI 1.0 の草稿・共通の block・TB と Verilator の枠・bitstream 1 の合成の実測

- 親: [ws.md](../ws.md)。設計: [design.md](../design.md) §8 の p002 の行（§1.3 clock・reset・quiesce、§2 GHCI、§3.6 見積もり、§4.1 MC/AMB、§7 repository）。
- 状態: in-progress（2026-10-23 P2 が実装と host の確認を終えた。clear の判定は Q1）。
- 受け入れ（design §8）: host で TB PASS、lint 0。加えて Q1 の依頼: bitstream 1 を Vivado の batch で ZYBO 向けに合成し LUT/FF/BRAM と timing を測り、§3.6 の見積もりを直す。
- 決定（2026-10-23、Q1 経由のユーザーの決定）: U1〜U17 は推しで承認（U1 Zlib、U3 Verilator を Debian から、cocotb 無し、U15、U16、U17）。U2 は当面 KC705 を USB JTAG だけで（PCIe は Future Work fw-kc705-pcie）。KC705 の gclk の目標 200 MHz。途中で届いた決定: SU に小さな per-warp の scoreboard（design §3.1、p008）、GHCI は標準の shader ISA を定め vendor の ISA は任意、CP の JIT 可、native の ISA の直接の投入も可、**command は zedBSD の kernel の command（Venus 由来）、ISA は RISC-V を元にした独自**（design v3 は P1）。これに従い `docs/ghci.md` は host interface だけを書き、command と ISA の節は「design v3、pending」の stub にした。旧の opcode の decoder は作っていない。

## 置き場所と commit

- amaterrace-gpu（`/home/awe/amaterrace-gpu`、main、push していない）: 70aaff5（LICENSE・.gitignore・include・rtl・tools/lint.sh）、655f1f2（TB と model、MC の error の追い越しの注記）、06edac2（atg_top の TB、HALT 中の COPY）、43f8bab（C の header、Verilator の harness）、053c1ba（docs、ZYBO の top、Vivado の script、BLT の clip の register 化）、217f0a0（boards/kc705/README.md）。間の b8a6373 は Q1 の README の変更（scoreboard の行）、8e25456・caf55b9 はユーザーの README。
- zedBSD: この phase.md、ws.md の行、design.md §3.6 の実測の行と係数の注記（worktree `p2-ws288-p002`、branch `agent/p2-ws288-p002`）。

## 作った物（amaterrace-gpu）

| path | 中身 |
| --- | --- |
| `LICENSE` | Zlib（zedBSD と同じ文）。GHCI の仕様の文書も Zlib（U1） |
| `.gitignore` | `build/`、`.Xil/`、`*.bit`・`*.dcp`・`*.log`・`*.jou`・`*.vvp`・`*.vcd`・`*.fst`・`obj_dir/`・`*~`。Vivado の project・IP の生成物・bitstream は commit しない |
| `include/ghci_regs.vh`・`ghci_regs.h` | HCR の offset と bit、CAPS/STATUS/IRQ/FAULT_INFO の field、BLT の STATE の範囲（他の STATE の範囲は design v3 まで置かない） |
| `include/ghci_packets.h` | stub（fence の wrap の比較だけ。command は design v3） |
| `rtl/common/am_sync.v` | 2 段の同期 |
| `rtl/common/am_fifo.v` | 同期の FWFT FIFO（distributed RAM） |
| `rtl/common/am_afifo.v` | 非同期 FIFO（gray の pointer、2 段の同期、両側の reset の約束） |
| `rtl/common/am_axi_regs.v` | AXI3/AXI4 の slave → register bus（plio の形: burst は先頭の address、読みは 1 cycle 後、待たせない） |
| `rtl/common/am_axi_master.v` | line の要求 → AXI の INCR burst（32 byte × 64 bit = 4 beat、64 byte × 512 bit = 1 beat）、outstanding 読み 4・書き 4、ID 0 で順序、SLVERR/DECERR → error、**quiesce**（halt で新しい受理を止め、W を出し切り、全応答の後に idle） |
| `rtl/gpu/mc/atg_mc.v` | AMB の arbiter（HIGH_MASK の固定優先 + round-robin、1 cycle に 1 grant）、**aperture の検査**（外は AXI に出さず error の応答と fault の pulse）、quiesce・flush、応答の routing |
| `rtl/gpu/cp/atg_cp.v` | CP の stub: 識別の register、SCRATCH、COUNTER、CTRL（RUN は保持だけ、HALT、RESET を §1.3 の順で）、STATUS、IRQ、APERTURE、FAULT_ADDR/INFO、STATE[0..511]（BRAM18）と alias、KICK.BLIT、非同期の DBG_ADDR/DATA/STATUS（MC client 0）、AXI_STUCK。ring・fence・EXTCAP は無い（CAPS[0] = 0） |
| `rtl/gpu/blt/atg_blt.v` | BLT の骨: 32 bpp の FILL と COPY（行を line の区間に切り、COPY は 1〜2 line を読んで word 単位で shift）、clip。TILE・UNTILE・16 bpp・blend・逆向きは FAULT 種別 2（p003） |
| `rtl/gpu/top/atg_top.v` | am_axi_regs + CP + BLT + MC（client 0 CP、1 BLT） |
| `boards/zybo/atg_zybo_2d.v`・`.xdc` | bitstream 1「2D」: PS7 の primitive を直に（plio と同じ）、GP0 → GHCI、HP0 ← GPU の master（PS7.v の AXI3 の幅: ID 6、LEN 4、SIZE 2、WSTRB 8）、IRQ_F2P[0]、FCLK0 100 MHz、LD0 heartbeat・LD1 IRQ |
| `boards/kc705/README.md` | p012 で top を置く旨 |
| `sim/run.sh`、`sim/tb/*_tb.v`、`sim/tb/axi_host.vh`、`sim/models/axi_mem.v` | iverilog の unit TB（`PASS NAME`）、host の task、AXI の slave memory の model（random の ready と latency、protocol の検査、error の領域） |
| `sim/verilator/harness.cpp`・`run.sh` | Verilator の system harness の骨: DDR の model、`ghci_regs.h` を使う host、FILL/COPY を C++ の参照と比べ、DBG で読み戻す |
| `tools/lint.sh` | Verilator `-Wall` で全 module と atg_top の ZYBO・KC705 の 2 つの形 |
| `tools/vivado-build.sh`・`.tcl` | Vivado の non-project batch（zedBSD の `plan/tools/kc705/vivado-build.sh` の Debian 13 の libncurses と license の扱いを写した）。出力は `build/vivado/TOP/` |
| `docs/ghci.md` | GHCI 1.0 の草稿: 規約、HCR、EXTCAP、CP の状態、ring の約束、完全な fence と割り込み、fault・watchdog・quiesce・reset、STATE の仕組みと BLT の範囲、**command の節と ISA の節は design v3 の pending の stub**（ユーザーの決定を書いた）、DBG と KICK、DC の capability（DISP の register）、memory の model、版、Amaterrace の実装の現状 |
| `docs/style.md` | RTL・TB・script の規約（design §7.3） |

## 確認（host のみ。QEMU・実機は無し）

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| Verilator の導入（U3） | `sudo apt-get install -y verilator` | Verilator 5.032（Debian 5.032-1+b2） |
| lint | `tools/lint.sh` | 9 module と atg_top の 2 形（ZYBO 32 byte/64 bit、KC705 64 byte/512 bit）の 11 件すべて clean（warning 0） |
| iverilog の unit TB | `sim/run.sh`（iverilog 12.0、`-g2005 -Wall`、build の warning も FAIL 扱い） | PASS am_afifo、am_axi_master、am_axi_regs、atg_mc、atg_top（`SEED=42` でも am_afifo・am_axi_regs が PASS） |
| am_afifo | 2 つの無関係な clock（10 ns・7.3 ns、途中で入れ替え）、random の valid/ready | 6000 word が順に 1 回ずつ、満ちる・空になる |
| am_axi_master | ZYBO と KC705 の 2 形 × 2000 要求を axi_mem と参照で比較 | data、順序、error の領域、halt 中の `req_ready` = 0、idle が bound の中、protocol の違反 0 |
| atg_mc | 3 client（0 が固定優先）× 600 要求、1/40 が aperture の外、random の quiesce | data と順序、外の要求は AXI に出ず error と fault（client・line・向き）、quiesce 中に grant 0、全 client が grant を得る |
| atg_top | host の口だけで、2 形 | 識別、SCRATCH の byte strobe、STATE の alias（部分の strobe は無視）、DBG の読み書き（aperture の外は DBG_STATUS.error だけ）、FILL・COPY（別の alignment）・clip、random の 40 回の FILL/COPY を参照の memory（256 KiB）と比較、HALT を COPY の途中で → HALTED で BLT busy のまま → 再開して結果が同じ、aperture の fault（FAULTED、FAULT_INFO 種別 1・client 1・write、IRQ、FAULTED 中の STATE の書き込みは無視）→ RESET → IDLE、aperture は残る、次の FILL が通る、TILE は種別 2、HALT/HALTED の IRQ、IRQ_SET/W1C と IRQ の線 |
| Verilator の harness | `sim/verilator/run.sh` | PASS verilator（6108 cycle、build の warning 0） |
| C の header | `cc -std=c99 -Wall -Wextra -pedantic -Werror` | 通る |

TB の途中で直した物: am_axi_master の `idle` が最後の応答を出している cycle に 1 になっていた（`!rsp_valid` を足した。CP の RESET が応答を落とさない）。atg_mc で aperture の外の要求の error の応答が同じ client の先行の要求を追い越す事を確かめ、MC の注記に書いた（device の fault で、CP は quiesce して RESET を待つので害は無い）。

## bitstream 1 の合成の実測（Vivado 2026.1、BASIC、xc7z010clg400-1、配線後）

`tools/vivado-build.sh zybo atg_zybo_2d`（142 s）。

| 項目 | 値 |
| --- | --- |
| Slice LUT | **2,451**（13.9%）。内訳: am_axi_regs 284、atg_cp 601、atg_blt 1,392、atg_mc 165（その中の am_axi_master 148） |
| Slice Register | 2,632（7.5%）。BLT 1,175、MC 851（line の buffer）、CP 467、regs 111 |
| Block RAM | 0.5（STATE の BRAM18 1 つ） |
| DSP | 0 |
| timing（100 MHz、gclk = FCLK0） | WNS +0.473 ns、WHS +0.048 ns（fmax 約 105 MHz）。最悪の道は BLT の COPY の区間の計算（cw → seg_n → 最後の source の line の比較、CARRY4 12 段） |
| 1 回目 | WNS −0.204 ns: BLT の kick の時の clip の計算（加算・比較の連鎖）。clip の結果を 1 cycle の register に入れて直した（S_CLIP） |
| warning | 132（PS7 の未接続の port 100 と接続数 1、意図して使わない bit の 26、定数の LED の port 4、PS7 の POWER の property 1）。critical 0。RTL の誤りによる物は無い |
| bitstream | `build/vivado/atg_zybo_2d/atg_zybo_2d.bit`、2,083,942 byte、sha256 先頭 acacb298b9207798（commit しない） |

design §3.6 との比較（[design.md](../design.md) §3.6 に実測の行と係数の注記を足した）: bitstream 1 の全体は見込みの 4.7k LUT・BRAM 5 に対して 2.45k・0.5。BLT は 1.0k の見込みが 32 bpp の骨だけで 1.39k（2 line の buffer と shifter）。CP は stub なので 1.8k は据え置き（p003・p004 で測る）。MC の client あたりの増え方は 2 client では分けて測れない（推定のまま、p015）。

## 未実施・制限・残り

- 未実施: 板（ZYBO）での動作。bitstream は作ったが書いていない（p003 で `fpgaload` と `fpgareg`、zedBSD の ps7_init が HP0 を 64 bit の mode にしているか、level shifter の有効化も p003 で確かめる）。QEMU は対象外。
- 未確認: UG585 は host に無く、HP port の FIFO の深さと 32/64 bit の mode の register は読めていない（design R4）。port の幅だけは Vivado の unisim の `PS7.v` で確かめた（ID 6、AxLEN 4、AxSIZE 2、WSTRB 8、RCOUNT/WCOUNT 8 bit）。
- KC705 の形は lint と simulation だけ（合成は p012）。gclk ≠ mclk の `am_afifo` の挟み込みも p012。
- BLT の COPY は行ごとに先行の書き込みの完了を待ってから読む（重なりの安全のため。性能は p003 で）。逆向きの COPY・16 bpp・blend・tile は p003。
- `docs/ghci.md` の command（§9）と ISA（§10）は design v3（P1）を待つ。`include/ghci_packets.h` は stub。
- README の GHCI の節の書き足しは Q1（提案の文は Q1 への報告に付けた）。
- 試験の整理: 作った TB は amaterrace-gpu の中の回帰（`sim/run.sh`・`tools/lint.sh`・`sim/verilator/run.sh`）。zedBSD の Master の Tools への登録は Q1 の判断。
