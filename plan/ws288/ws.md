<!-- awesome-plan project=zedbsd record=ws288 -->

# WS288 FPGA の GPU の IP（ZYBO で workflow、本命は KC705）

- Master: [plan/master.md](../master.md)（2026-10-23 作成）。Primary Milestone: MG008。Related: WS286（ZYBO）。
- Status: planning
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
- RTL の置き場所（2026-10-23 ユーザー「RTLのツリーは分けます。」）: GPU は https://github.com/awemorris/amaterrace-gpu 、CPU は https://github.com/awemorris/sukuna-risc （旧 mc64）。zedBSD の tree には driver・道具・試験を置く。別の repository には RTL・script・project だけを置き、計画（plan/ws288/）は zedBSD のまま（2026-10-23 ユーザー「planはこのままzedBSDを使いましょう。」）。ZYBO の plio と KC705 の bringup は zedBSD に残す。
- 道具: 2026-10-23 から、支障が無い限り Vivado（2026.1、BASIC の license）を主に使う。DDR3 の PHY などは MIG などの AMD の IP を使う（ユーザー「もし特に作業に支障がなければ、Vivadoを使っていきましょう。DDR3 PHYなんかもMIGがないと無理です。無理ではないが、トークンの無駄と思います。」、Master の decisions-log）。JTAG の書き込みは openFPGALoader、openXC7 の道具は残す。以前の方針は「まず OSS（openXC7・openFPGALoader）、難しければ AMD の tool」。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| p001 | 設計（block ごとの interface、AXI の口、命令の集合の最初の案、zedBSD の driver との境界） | WS286 p003（PL の workflow） | planning |

## 現在の状態・再開点

- 2026-10-23: 構成をユーザーと合意。WS286 で PL の workflow（p003）ができてから p001 に入る。
- 2026-10-23: Vivado 2026.1（~/opt/amd/2026.1/Vivado、settings64.sh）が host にある（ユーザー）。
- 2026-10-23: KC705 を host に接続（ユーザー、ZYBO を外して）。USB JTAG（Digilent Adept、FT2232H、serial 210203368704）を `openFPGALoader -c digilent --detect` で確認: index 0 xc7k325t（idcode 0x3651093）。USB-UART（CP2103）は未接続（別の USB の cable が要る）。ZYBO の JTAG の serial は 210279785182 で、cfg の adapter serial で区別できる。
- 2026-10-23 P1（worktree p1-kc705）: KC705 の workflow を確認（[記録](phase001-kc705-bringup.md)）。openXC7 と Vivado 2026.1（BASIC の license で xc7k325t が通る）の両方で `platform/fpga/kc705/bringup` を作り、JTAG で ID・200 MHz の COUNTER・SCRATCH・LED を確認。button S（AB12）が 1 のままだったのは pin・極性ではなく openXC7（nextpnr 3e5c2cdd）が HP bank の入力の対に書く FASM の誤り。`plan/tools/kc705/fasm-hp-input-pair.awk` で回避し、Vivado の bitstream と IOB の設定が一致。再開点: Q1 の判定・Tools の登録、button・DIP を押した時の目視（ユーザー）。
