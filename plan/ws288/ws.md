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
- 道具: まず OSS（openXC7・openFPGALoader）、難しければ AMD の tool（ユーザー）。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| p001 | 設計（block ごとの interface、AXI の口、命令の集合の最初の案、zedBSD の driver との境界） | WS286 p003（PL の workflow） | planning |

## 現在の状態・再開点

- 2026-10-23: 構成をユーザーと合意。WS286 で PL の workflow（p003）ができてから p001 に入る。
- 2026-10-23: Vivado 2026.1（~/opt/amd/2026.1/Vivado、settings64.sh）が host にある（ユーザー）。
- 2026-10-23: KC705 を host に接続（ユーザー、ZYBO を外して）。USB JTAG（Digilent Adept、FT2232H、serial 210203368704）を `openFPGALoader -c digilent --detect` で確認: index 0 xc7k325t（idcode 0x3651093）。USB-UART（CP2103）は未接続（別の USB の cable が要る）。ZYBO の JTAG の serial は 210279785182 で、cfg の adapter serial で区別できる。
