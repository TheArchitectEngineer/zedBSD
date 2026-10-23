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
- 道具: 2026-10-23 から、支障が無い限り Vivado（2026.1、BASIC の license）を主に使う。DDR3 の PHY などは MIG などの AMD の IP を使う（ユーザー「もし特に作業に支障がなければ、Vivadoを使っていきましょう。DDR3 PHYなんかもMIGがないと無理です。無理ではないが、トークンの無駄と思います。」、Master の decisions-log）。JTAG の書き込みは openFPGALoader、openXC7 の道具は残す。以前の方針は「まず OSS（openXC7・openFPGALoader）、難しければ AMD の tool」。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| p001 | 設計（block ごとの interface、AXI の口、命令の集合の最初の案、zedBSD の driver との境界）: [design.md](design.md) 版 3（版 2 の 38 件 + 版 3 の review の 49 件に対応、U1〜U30。版 3: ring の command = gpu-op、CP は `atg_cpu` の firmware、GHCI の標準の shader ISA「GSI1」）、[proposed/README-additions.md](proposed/README-additions.md) | WS286 p003（PL の workflow） | in-progress（2026-10-23 P2 が版 2、P1 が版 3。clear の判定は Q1。p002〜p021 の案は design.md §8） |
| p002 | amaterrace-gpu の骨組み、`docs/ghci.md` 1.0 の草稿（host interface。command と ISA は design v3 の pending）、`am_axi_regs`・`am_axi_master`（quiesce）・`am_afifo`・`atg_mc`、TB と Verilator の枠、bitstream 1 の合成の実測: [phase002](phase002/phase.md) | p001、U1、U3 | cleared（2026-10-23 Q1: amaterrace-gpu 70aaff5〜217f0a0 で骨組み・LICENSE（Zlib）・docs/ghci.md の host の interface の草稿・am_axi_regs/am_axi_master/am_afifo/atg_mc・BLT・CP の仮の部分、Verilator 5.032 の lint 11/11 clean、iverilog の TB 5 本 PASS、Verilator の harness PASS、bitstream 1 を Vivado 2026.1 で xc7z010 に配置配線: LUT 2451・FF 2632・BRAM 0.5・100 MHz で WNS +0.473 ns。板は p003。command と ISA は design 版 3 待ちで ghci.md は stub） |

## 現在の状態・再開点

- 2026-10-23: 構成をユーザーと合意。WS286 で PL の workflow（p003）ができてから p001 に入る。
- 2026-10-23 P2（ws288-p001、worktree p2-ws288-p001）: ユーザーの amaterrace-gpu/README.md（1791b32: GHCI・DC・CP・BLT・VF・RX・SU・TU・ROP・MC、Warp = 32-lane scalar with 16-bit packed）に合わせて [design.md](design.md) を書いた。上の「進め方（案）」の ZYBO の順（display controller → blitter → command processor）を CP + BLT → DC に変える案（design §8、U14: `DBG_ADDR/DATA` で DDR を読めるので driver も画面も無しに板で照合できる）。7010 には全 block が入らない見込みで、block を分けた bitstream で確かめる案（U17）。決定が要る U1〜U17 は design §9.2。
- 2026-10-23: Vivado 2026.1（~/opt/amd/2026.1/Vivado、settings64.sh）が host にある（ユーザー）。
- 2026-10-23: KC705 を host に接続（ユーザー、ZYBO を外して）。USB JTAG（Digilent Adept、FT2232H、serial 210203368704）を `openFPGALoader -c digilent --detect` で確認: index 0 xc7k325t（idcode 0x3651093）。USB-UART（CP2103）は未接続（別の USB の cable が要る）。ZYBO の JTAG の serial は 210279785182 で、cfg の adapter serial で区別できる。
- 2026-10-23 P1（worktree p1-ws288-d3）: design.md を版 3 に（ユーザーの決定「コマンドは現在のカーネルのコマンド（Venus由来）」「ISA は RISC-V ベースでカスタム」「GHCI は標準の shader ISA を定義」「小さな scoreboard」）: §2.4 = ring の class V（gpu-op をほぼそのまま中継、CP の RV32IM の firmware が解く）と class N（版 2 の µop）、§3 = GSI1（64 bit 固定、低位は RISC-V の 32 bit の形）、§8 = p017（CPU + firmware の土台）・p018（firmware の Vulkan の object model）・p019（driver の中継 `vk/`）を新設し 392 LW、U18〜U30 の判断待ち。同日のユーザーの要件（shader ISA の register 1 つを FIFO に予約、SU と decoupled な optional の systolic array、重みは MC から stream）は §3.9（`rq` = register 63、`atg_sa`、`CAPS[25]`、p020・p021）。P2 の p002（HCR・ring・fence・IRQ）とは §2.4.10 で整合（ring の封筒だけ p004 で版 3 の形に）。
- 2026-10-23 P1（worktree p1-kc705）: KC705 の workflow を確認（[記録](phase001-kc705-bringup.md)）。openXC7 と Vivado 2026.1（BASIC の license で xc7k325t が通る）の両方で `platform/fpga/kc705/bringup` を作り、JTAG で ID・200 MHz の COUNTER・SCRATCH・LED を確認。button S（AB12）が 1 のままだったのは pin・極性ではなく openXC7（nextpnr 3e5c2cdd）が HP bank の入力の対に書く FASM の誤り。`plan/tools/kc705/fasm-hp-input-pair.awk` で回避し、Vivado の bitstream と IOB の設定が一致。再開点: Q1 の判定・Tools の登録、button・DIP を押した時の目視（ユーザー）。
- 2026-10-23 P2（ws288-p002、worktree p2-ws288-p002）: amaterrace-gpu に骨組み・共通の block・CP の stub・BLT の骨・MC・TB・Verilator の harness・Vivado の script・`docs/ghci.md` の草稿を commit（70aaff5〜217f0a0、push していない）。bitstream 1 の実測で design §3.6 の係数を直した（[phase002](phase002/phase.md)）。再開点: Q1 の判定。p003（CP v0 と ZYBO の板）は KICK と DBG だけで進められ、ring と command（p004）は design v3 の後。
