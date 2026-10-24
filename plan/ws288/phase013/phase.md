<!-- awesome-plan project=zedbsd record=ws288-p013 -->

# ws288-p013 KC705 の PCIe（GPU の board として host に出す）

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §8 の p013 の行・§9.2（host link A'）。
- 状態: planned（2026-10-25 01:08 Q1。Future Work fw-kc705-pcie から戻した）
- 依存: p012、p024（板の bring-up）、p006（driver の PCIe の attach は p006 の範囲）。
- 承認: 2026-10-25 ユーザー「了解です。SAはfuture workに回します。p009, msaaを優先して実装してください。そのあと、p010,p018,p019,p006,p011を実装します。KC705のDDR3をVRAMにしてPCIeのGPUボードにすることで、QEMUからパススルーで利用し、ゲストのzedBSDからVulkanを動作させることを、最初のゴールにします。必要なphaseは作成してください。」

## 範囲

- KC705 の PCIe（Gen2 x4 か x8、XDMA または 7 series の PCIe の hard block）: BAR0 = GHCI の register・doorbell、BAR2 = **DDR3 を VRAM** として見せる窓（GHCI §11 の discrete の形、MEM の capability）、MSI（または MSI-X）、GPU の DMA は DDR3 の上（host memory への DMA は後、下の Future Work）。
- bitstream の載せ方と link の保ち方（JTAG で載せた後の PCIe の再列挙: `pcie-reload.sh` の rescan、または flash の bitstream）。host の PC の電源の扱い（SwitchBot）を含む。
- host の Linux で VFIO に bind できること（IOMMU group の確認）。

## 受け入れ

host の Linux で KC705 が PCIe の device として見え、BAR0 の GHCI の ID の register と BAR2 の DDR3 の読み書きが通る（host の小さな道具）、MSI が届く。VFIO に bind できる。

## 記録

- 2026-10-25 01:08 Q1: Future Work fw-kc705-pcie から戻して計画（ユーザーの最初のゴール）。
