<!-- awesome-plan project=zedbsd record=ws288-p013 -->

# ws288-p013 KC705 の PCIe（GPU の board として host に出す）

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §8 の p013 の行・§9.2（host link A'）。
- 状態: in-progress（2026-10-25 15:49 Q1、q1579 P2: 板の要らない部分 = bridge の RTL と sim を先に。板の部分は p024 と一緒）
- 依存: p012、p024（板の bring-up）、p006（driver の PCIe の attach は p006 の範囲）。
- 承認: 2026-10-25 ユーザー「了解です。SAはfuture workに回します。p009, msaaを優先して実装してください。そのあと、p010,p018,p019,p006,p011を実装します。KC705のDDR3をVRAMにしてPCIeのGPUボードにすることで、QEMUからパススルーで利用し、ゲストのzedBSDからVulkanを動作させることを、最初のゴールにします。必要なphaseは作成してください。」

## 範囲

- KC705 の PCIe（Gen2 x4 か x8、XDMA または 7 series の PCIe の hard block）: BAR0 = GHCI の register・doorbell、BAR2 = **DDR3 を VRAM** として見せる窓（GHCI §11 の discrete の形、MEM の capability）、MSI（または MSI-X）、GPU の DMA は DDR3 の上（host memory への DMA は後、下の Future Work）。
- bitstream の載せ方と link の保ち方（JTAG で載せた後の PCIe の再列挙: `pcie-reload.sh` の rescan、または flash の bitstream）。host の PC の電源の扱い（SwitchBot）を含む。
- host の Linux で VFIO に bind できること（IOMMU group の確認）。

## 先に行う部分（2026-10-25 15:49 ユーザー「PCIeブリッジを今から作ってください。」）

板の無い範囲を先に行う（板・bitstream・VFIO の bind は p024 の後）:

1. 設計の短い節（phase.md に）: 使う IP（7 series の XDMA の AXI-Lite master = BAR0、AXI の bypass か AXI master = BAR2、MSI の割り込みの口。Vivado 2026.1 の xc7k325t で使える物を確かめる）、clock の領域（PCIe の user clock 125/250 MHz と GPU の gclk の境は am_afifo・am_sync）、BAR の大きさ（BAR0 = GHCI の HCR の窓と DISP2 の窓、BAR2 = DDR3 の窓の大きさと base の register）、MSI のどの IRQ の bit をどう送るか、reset の順。
2. RTL（`rtl/` か `boards/kc705/`、vendor の primitive・IP は `boards/` と `rtl/common/xilinx/` だけ）: BAR0 の AXI-Lite → GHCI の register の口（今の JTAG の bridge と同じ口へ、両方を残す）、BAR2 の AXI → MC の client（DDR3 の窓、MEM の capability の範囲の検査）、GHCI の IRQ → MSI、clock の境。算術は p037 の規則（rtl/common の am_ の module）。
3. sim: XDMA の口の振る舞いの model（`sim/models/`）で、BAR0 で GHCI の ID を読む・doorbell、BAR2 で DDR3 の model への読み書き、IRQ → MSI が届く、を TB で。lint clean。
4. board top（atg_kc705_raster）に PCIe の口を足す（IP の instance は wrapper の stub で lint が通る形、IP の生成の Tcl を tools/ か boards/kc705/ に）。

## 受け入れ

host の Linux で KC705 が PCIe の device として見え、BAR0 の GHCI の ID の register と BAR2 の DDR3 の読み書きが通る（host の小さな道具）、MSI が届く。VFIO に bind できる。

## 記録

- 2026-10-25 01:08 Q1: Future Work fw-kc705-pcie から戻して計画（ユーザーの最初のゴール）。
- 2026-10-25 15:49 Q1: ユーザー「PCIeブリッジを今から作ってください。」→ 板の要らない部分（上の 1〜4）を P2 に（q1579、p038 の今の OOC の記録の後）。
