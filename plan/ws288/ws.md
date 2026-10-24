<!-- awesome-plan project=zedbsd record=ws288 -->

# WS288 FPGA の GPU の IP（ZYBO で workflow、本命は KC705）

- Master: [plan/master.md](../master.md)（2026-10-23 作成）。Primary Milestone: MG008。Related: WS286（ZYBO）。
- Status: incomplete（2026-10-23: p002 の実行で開始。Master の一覧の写しは Q1）
- 出典: 2026-10-23 ユーザー「Z-7010のロジック規模で、1シェーダーユニットのGPUを書けると思いますか？…まずはFPGAワークフローを確立するのが目的ではありますが。Kintex KC705ボードが本命のターゲットです。」「ディスプレイコントローラとGPUは別なIPなので、両立しなくてもいいです。このボードは最終ターゲットではないので、まずはworkflow確立がゴールですね。」「了解です。この設計で行きましょう。」

## 構成（ユーザーと合意、2026-10-23）

- display controller
- command processor（ring・AXI-Lite の register・doorbell・割り込み・fence）
- blitter（矩形の copy・fill の 2D の DMA と簡単な合成）
- vertex fetcher（頂点の読みと primitive の組み立て）
- rasterizer（edge function・half-space 方式）
- shader unit（small cache 付き。SM・CU の 1 つに当たる単位: SIMD の lane、scheduler、BRAM の register file、任意の共有 memory。Vulkan の workgroup の行き先、lane の数が subgroup の幅。複数の unit の束ね方はハードウェアの内部の都合で API には出さない）
- texture unit（small cache 付き）
- ROP（blend・depth・stencil・書き戻し）
- memory controller（AXI の抽象: 内部の共通の memory の口と arbiter。ZYBO は PS の DDR の HP port、KC705 は MIG の DDR3）
- 性能の counter と debug の register（小さく）

## 進め方（案）

- ZYBO（XC7Z010）: display controller → blitter → command processor → rasterizer＋ROP → shader unit 1 つ → texture unit。display controller と GPU は別の IP で、7010 で同時に載らなくてよい。
- KC705（XC7K325T）: shader unit を複数並べる。ARM が無いので、x86 の zedBSD から PCIe の GPU として使う案と soft CPU の案がある（後で決める）。Vivado の BASIC（$0）で 325T を扱える（2026-10-23 AMD の page で確認）。
- RTL の置き場所（2026-10-23 ユーザー「RTLのツリーは分けます。」）: GPU は https://github.com/awemorris/amaterrace-gpu 、CPU は https://github.com/awemorris/sukuna-risc.git （2026-10-23 ユーザー「mc64をsukuna-riscに改名しました。」）。zedBSD の tree には driver・道具・試験を置く。別の repository には RTL・script・project だけを置き、計画（plan/ws288/）は zedBSD のまま（2026-10-23 ユーザー「planはこのままzedBSDを使いましょう。」）。ZYBO の plio と KC705 の bringup は zedBSD に残す。
- RTL の進め方（2026-10-23 ユーザー「まずブロックをトップダウンに分割して、ブロックのI/Oだけ定義して、空のモジュールを書きましょう。仕様はモジュールごとにテキストファイルにしましょう。ステートもかなり大まかにはテキストに記載しておきましょう。これで、だいぶ下位のAIモデルでもVerilogが記述できるはずです。」）: top-down に module へ分割 → port だけの空の module と module ごとの仕様の text（状態の大まかな遷移を含む）→ 中身は仕様だけを読んで書く。
- 道具: 2026-10-23 から、支障が無い限り Vivado（2026.1、BASIC の license）を主に使う。DDR3 の PHY などは MIG などの AMD の IP を使う（ユーザー「もし特に作業に支障がなければ、Vivadoを使っていきましょう。DDR3 PHYなんかもMIGがないと無理です。無理ではないが、トークンの無駄と思います。」、Master の decisions-log）。JTAG の書き込みは openFPGALoader、openXC7 の道具は残す。以前の方針は「まず OSS（openXC7・openFPGALoader）、難しければ AMD の tool」。

- **引き継ぎ（2026-10-24 00:46、週次の上限の前のラップアップ）: [handover-20261024.md](handover-20261024.md)**。

- **2026-10-24 ユーザー「では、ZYBOではDCの検証ができたのでもうそれでよしとして、KC705だけをターゲットに変えましょう。」** → 目標の板は KC705 だけ。ZYBO の raster・compute の bitstream（U17）は作らない。ZYBO は DC（p005）の検証まで。

- **2026-10-24 ユーザー（FP16 だけの GPU の話の後）「了解です。そのまま勧めましょう。」→ KC705 の SU は案 A: fp32 は FPU の少ない lane（例: NPHYS 8 の SU に FPU 1〜2 lane、32 lane を順に流す）、fp16 の packed の演算（PACKED）は全 lane。Vulkan の fp32 の正しさは保ち、速さを fp16 に寄せる。案 B（頂点の変換は VF/RX の固定の回路で fp32、SU は fp16 を主に）は合わせて検討。FPU の lane の数は p012〜p015 の合成で決める。**
- **2026-10-24 ユーザー「FP32は全laneでいいと思います。」→ 直前の案 A（fp32 は FPU の少ない lane）を改める: KC705 の SU は fp32 の FPU を全 lane に持つ（NPHYS 分）。fp16 の packed（PACKED）も持つ。SU の数・NPHYS は KC705 の合成で決める。**

- **2026-10-24 ユーザー「あとDCはKC705に移植して、HDMI出力を可能にしたいですね。VGAは正直、RTLは残していいけど、もう合成しなくていいです。」→ DC は KC705 に移して HDMI を出す（KC705 の HDMI は ADV7511 の encoder 経由: I2C の設定（`atg_dc_i2c`、今は stub）と並列の画素の data。pin と設定は UG810・board の資料で確かめる、U7）。VGA の RTL（`atg_dc_vga`）は残すが、今後の bitstream では合成しない。**

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| p001 | 設計（block ごとの interface、AXI の口、命令の集合の最初の案、zedBSD の driver との境界）: [design.md](design.md) 版 4（版 2 の 38 件 + 版 3 の 49 件 + 版 4 の review に対応、U1〜U38（U29〜U36・U40 は取り下げ、U39 は閉じた）。版 4: SU ↔ SA の大まかな整合だけ（`rq` = r63、decoupled な SA、案 B。SA の設計と実装は延期で Appendix A の sketch、p020〜p022 は Future Work）、§7.6 の top-down の分割と module ごとの spec（p023、p004 より前）、版 3: ring の command = gpu-op、CP は `atg_cpu` の firmware、GHCI の標準の shader ISA「GSI1」）、[proposed/README-additions.md](proposed/README-additions.md) | WS286 p003（PL の workflow） | in-progress（2026-10-23 P2 が版 2、P1 が版 3。clear の判定は Q1。p002〜p023 の案は design.md §8） |
| p002 | amaterrace-gpu の骨組み、`docs/ghci.md` 1.0 の草稿（host interface。command と ISA は design v3 の pending）、`am_axi_regs`・`am_axi_master`（quiesce）・`am_afifo`・`atg_mc`、TB と Verilator の枠、bitstream 1 の合成の実測: [phase002](phase002/phase.md) | p001、U1、U3 | cleared（2026-10-23 Q1: amaterrace-gpu 70aaff5〜217f0a0 で骨組み・LICENSE（Zlib）・docs/ghci.md の host の interface の草稿・am_axi_regs/am_axi_master/am_afifo/atg_mc・BLT・CP の仮の部分、Verilator 5.032 の lint 11/11 clean、iverilog の TB 5 本 PASS、Verilator の harness PASS、bitstream 1 を Vivado 2026.1 で xc7z010 に配置配線: LUT 2451・FF 2632・BRAM 0.5・100 MHz で WNS +0.473 ns。板は p003。command と ISA は design 版 3 待ちで ghci.md は stub） |
| p003 | CP v0（KICK と DBG だけ: HCR、状態、IRQ、STATE、非同期の DBG、EXTCAP の PERF）+ BLT（FILL・COPY・TILE/UNTILE・16 bpp・blend-over・逆向き）+ ZYBO の top、板で `fpgareg` だけの端から端: [phase003](phase003/phase.md) | p002 | cleared（2026-10-23 Q1: 実機の ZYBO（SD 起動、fpgareg だけ、driver 無し）で ghci-blt-board.py が 5 回とも全 case PASS（fill32・copy32 の非整列・fill16・copy16・blend・tile・untile・重なりの逆向き/順向き・reset の後の fill、識別・CAPS・EXTCAP/PERF・aperture・DBG、IRQ・HALT・FAULT → RESET）。amaterrace-gpu の lint 11 clean、sim 5 本 PASS（seed 2・3・42 も）、Verilator PASS。Vivado: LUT 3802・FF 3719・BRAM 0.5・100 MHz WNS +0.190 ns。HP0 は reset の値で 64 bit、level shifter 有効、FCLK0 100 MHz を JTAG で確認。速さ: FILL 640 MB/s、COPY 64 MB/s（read の prefetch は残件）。merge 01847a20a） |
| [p004](phase004/phase.md) | CP の子 module（regs・state・ring・seq・fence・ctl・irq・dbg、am_amb_mux）を spec から実装、atg_cp を配線だけに、ring（版 3 の封筒）・FENCE・watchdog・FAULT・RESET、p023 の bug 2 件と TB の漏れ、MC の router の木との統合 | p003、p023 | cleared（2026-10-23 Q1: amaterrace-gpu 98885bf で lint 97/97、sim 18/18 PASS（seed 2/3/42 も）、Verilator PASS、わざとの誤り 4 件で FAIL を確認。Vivado ZYBO 2D: LUT 5027・100 MHz WNS +0.570 ns。実機の ZYBO（SD 起動、fpgareg だけ）で ghci-blt-board.py 3/3 PASS: ring の FILL + FENCE の割り込み、HALTED の DBG、class V の fault → RESET → ring の先頭から。watchdog・WAIT_FENCE・STEP・RESULT・ring の折り返しは simulation だけ） |
| [p005](phase005/phase.md) | DC（bitstream 2）: atg_dc_regs・tgen・dma・vga・tmds を spec から実装、ZYBO の pclk（PLL + MMCM、BUFGMUX）・TMDS の出力・XDC、TEST_PATTERN → DDR の framebuffer | p002、p023 | cleared（2026-10-24 Q1: 実機の ZYBO で ghci-dc-board.py 28/28 PASS（frame の速さ・vsync・DMA の SCAN_SUM・flip・underflow 無し）、V1（640×480 の test pattern）をユーザーが目視「テストパターンを、目視できました」。ユーザー「では、ZYBOではDCの検証ができたのでもうそれでよしとして、KC705だけをターゲットに変えましょう。」で V2〜V5 は行わない。HDMI の 720p は TB と timing だけ） |
| [p017](phase017/phase.md) | CP の制御の CPU `atg_cpu` と firmware の土台、class V の経路 | p004 | cleared（2026-10-24 Q1: amaterrace-gpu 903f861。atg_cpu（RV32IM、4 段、CPI 1.27）と子 7 つ、CPFW の配線、fw/cp/ の firmware（起動・poll の loop・PARK・GHCI_FRAME・session/memory/buffer/command buffer・FILL/COPY_BUFFER・QUEUE_SUBMIT → BLT の µop）。sim 42/42 PASS（atg_top_fw_tb: class V = class N の memory、record、RESULT/RETIRE、watchdog → RESET → .data の復元、CPFW LOAD）、lint 97 clean。bitstream 1'（ZYBO 2D + CPU）: LUT 8257・WNS +0.463 ns。実機の ZYBO で ghci-blt-board.py --fw 3/3 PASS。1 frame 13 command は 32.5k cycle（class N は 12.5k、command あたり約 15 µs）。残り: Verilator に CPU 無し、fw の host 試験無し、HALTED の class V の STEP、heap は DMEM（CP_HEAP は後）） |
| [p008](phase008/phase.md) | SU v0 と GSI1（整数の経路は済み: dec・alu・rf・wb・opd・sb・sched・fetch・icache・exec、NPHYS 8/16/32 で同じ結果。FP・LSU・cache・xu・div・pk は part 2） | p004、U12 | in-progress（2026-10-24 P1 part 2） |
| [p009](phase009/phase.md) | VF + SU の vertex/fragment（exp・lda）+ TU。part A = VF の子 4 つ、B = exp・lda と pipeline の結線、C = TU と tex | p008、p007 | in-progress（2026-10-24: part A cleared（30dc42a、VF）、part B cleared（ce607b4、shading の三角形）、part C cleared（00bb02b、TU・texture の三角形）。残り: design §8 の p009 の行の qd・subgroup・deadlock・PACKED・kc705_raster の確認、Vulkan に合わない mip linear・clamp to border） |
| [p012](phase012/phase.md) | KC705 の RTL: board の top、JTAG の bridge、MIG・MMCM の wrapper、DC の HDMI（ADV7511）、sim まで（板は p024） | p004、p005、U7 | in-progress（2026-10-24: part A cleared（9e00981、JTAG の bridge・I2C・YCbCr 4:2:2 の HDMI・board の top の sim）。残り: CP の EXTCAP の DISP の entry） |
| [p024](phase024/phase.md) | KC705 の板での確認（MIG の IP、bitstream、JTAG で fill → DBG、HDMI の color bar、scene を板で） | p012 | planned（2026-10-24 新設） |
| [p025](phase025/phase.md) | timing の調整（KC705 で 200 MHz、blend の 2 段化など） | RTL が揃った後 | planned（2026-10-24 新設） |
| p023 | module の木・空の module・module ごとの spec（design §7.6）。part 1（P1）: まだ無い 86 module の port だけの Verilog と spec、`atg_top` の block の parameter、board の top、MC の router の木（2026-10-23 ユーザーの要求で `atg_rt` 等を実装）: [phase023](phase023/phase.md)。part 2（P2）: 既存の module の spec | p001（版 4）、p002、U37、U38 | cleared（2026-10-23 Q1: part 1（P1）= module の木の空の module と spec（CP・CPU・SU 24・VF・RX・TU・ROP・MC と router の木 atg_rt・DC・SA の port の予約・ZYBO と KC705 の board の top）、part 2（P2）= 既存 10 module の spec。lint 97/97 clean、sim 18/18 PASS。amaterrace-gpu main 98885bf まで） |

## 現在の状態・再開点

- 2026-10-23: 構成をユーザーと合意。WS286 で PL の workflow（p003）ができてから p001 に入る。
- 2026-10-23 P2（ws288-p001、worktree p2-ws288-p001）: ユーザーの amaterrace-gpu/README.md（1791b32: GHCI・DC・CP・BLT・VF・RX・SU・TU・ROP・MC、Warp = 32-lane scalar with 16-bit packed）に合わせて [design.md](design.md) を書いた。上の「進め方（案）」の ZYBO の順（display controller → blitter → command processor）を CP + BLT → DC に変える案（design §8、U14: `DBG_ADDR/DATA` で DDR を読めるので driver も画面も無しに板で照合できる）。7010 には全 block が入らない見込みで、block を分けた bitstream で確かめる案（U17）。決定が要る U1〜U17 は design §9.2。
- 2026-10-23: Vivado 2026.1（~/opt/amd/2026.1/Vivado、settings64.sh）が host にある（ユーザー）。
- 2026-10-23: KC705 を host に接続（ユーザー、ZYBO を外して）。USB JTAG（Digilent Adept、FT2232H、serial 210203368704）を `openFPGALoader -c digilent --detect` で確認: index 0 xc7k325t（idcode 0x3651093）。USB-UART（CP2103）は未接続（別の USB の cable が要る）。ZYBO の JTAG の serial は 210279785182 で、cfg の adapter serial で区別できる。
- 2026-10-23 P1（worktree p1-ws288-d3）: design.md を版 3 に（ユーザーの決定「コマンドは現在のカーネルのコマンド（Venus由来）」「ISA は RISC-V ベースでカスタム」「GHCI は標準の shader ISA を定義」「小さな scoreboard」）: §2.4 = ring の class V（gpu-op をほぼそのまま中継、CP の RV32IM の firmware が解く）と class N（版 2 の µop）、§3 = GSI1（64 bit 固定、低位は RISC-V の 32 bit の形）、§8 = p017（CPU + firmware の土台）・p018（firmware の Vulkan の object model）・p019（driver の中継 `vk/`）を新設し 392 LW、U18〜U30 の判断待ち。同日のユーザーの要件（shader ISA の register 1 つを FIFO に予約、SU と decoupled な optional の systolic array、重みは MC から stream）は §3.9（`rq` = register 63、`atg_sa`、`CAPS[25]`、p020・p021）。P2 の p002（HCR・ring・fence・IRQ）とは §2.4.10 で整合（ring の封筒だけ p004 で版 3 の形に）。
- 2026-10-23 P1（worktree p1-kc705）: KC705 の workflow を確認（[記録](phase001-kc705-bringup.md)）。openXC7 と Vivado 2026.1（BASIC の license で xc7k325t が通る）の両方で `platform/fpga/kc705/bringup` を作り、JTAG で ID・200 MHz の COUNTER・SCRATCH・LED を確認。button S（AB12）が 1 のままだったのは pin・極性ではなく openXC7（nextpnr 3e5c2cdd）が HP bank の入力の対に書く FASM の誤り。`plan/tools/kc705/fasm-hp-input-pair.awk` で回避し、Vivado の bitstream と IOB の設定が一致。再開点: Q1 の判定・Tools の登録、button・DIP を押した時の目視（ユーザー）。
- 2026-10-23 P2（ws288-p002、worktree p2-ws288-p002）: amaterrace-gpu に骨組み・共通の block・CP の stub・BLT の骨・MC・TB・Verilator の harness・Vivado の script・`docs/ghci.md` の草稿を commit（70aaff5〜217f0a0、push していない）。bitstream 1 の実測で design §3.6 の係数を直した（[phase002](phase002/phase.md)）。再開点: Q1 の判定。p003（CP v0 と ZYBO の板）は KICK と DBG だけで進められ、ring と command（p004）は design v3 の後。
- 2026-10-23 P3（ws288-p003、worktree p3-ws288-p003）: CP v0 と BLT の全 option を amaterrace-gpu に、板は SD 起動の ZYBO に fpgaload・fpgareg を scp、system.dtb を GPU の 64 MiB の予約付き（`plan/ws288/tests/zybo-gpu-sd.dts`）に。`config/ci/config-zybo.mk` の root に fpgaload・fpgareg（次の image から）。再開点: Q1 の判定・Tools の登録（`ghci-blt-board.py`・`ps-regs.sh`）。p004（ring・fence）は design v3 の後。COPY の性能（64 MB/s、読みの latency で直列）は残り（[phase003](phase003/phase.md)）。
