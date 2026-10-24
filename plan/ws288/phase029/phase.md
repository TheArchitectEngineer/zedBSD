<!-- awesome-plan project=zedbsd record=ws288-p029 -->

# ws288-p029 PCIe の board で host の memory だけを使う（unified memory の検証）

- 親: [WS288](../ws.md)。GHCI §11.3（unified と discrete は buffer の確保の場所の違いだけ）。
- 状態: planned（2026-10-25 02:06 Q1 新設）
- 依存: p028（DDR3 を VRAM にした discrete の形で guest の zedBSD から Vulkan が動いた）。
- 承認: 2026-10-25 02:06 ユーザー「了解です。MIGの検証コストが低いなら、むしろdiscrete VRAMを先に検証して、あとでCPUができる前にUnified memoryでも動く検証としてPCIeだけどホストメモリだけ使う検証を行うのがよさそうです。」

## 範囲

- KC705 の GPU の全部の memory の口（MC、DC の scanout を含む）を PCIe の DMA（XDMA の AXI bridge）で host の memory に向ける形の bitstream。DDR3 は使わない（MIG を入れない構成）。
- GHCI の MEM の capability で unified を示し、driver（p006）と firmware（p018）が buffer を host の memory（guest の memory、VFIO の IOMMU で対応付け）に確保する経路。
- PCIe の遅れ（1 回の読みで約 1 µs）に合わせて MC・cache の出しておける読みの数（tag）を足す。atomic は GPU の中の read-modify-write のまま、CPU と同時に使う buffer の扱いを決める。
- 目的: KC705 の上に CPU（Sukuna RISC）ができる前に、GPU と driver が unified memory でも動くことを確かめる。

## 受け入れ

p028 と同じ Vulkan の program が、host の memory だけを使う構成で guest の zedBSD から動き、readback が参照と一致。

## 記録

- 2026-10-25 02:06 Q1: 新設（ユーザーの決定）。DDR3 を VRAM にした discrete の形（p013・p028）が先。
