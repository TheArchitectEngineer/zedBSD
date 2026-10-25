<!-- awesome-plan project=zedbsd record=ws288-p043 -->

# ws288-p043 GHCI の driver を VisionFive 2（riscv64）の M.2 の KC705 で

- 親: [WS288](../ws.md)。関係: [WS256 p024](../../ws256/phase024/phase.md)（VF2 の PCIe1・NVMe）、[p024](../phase024/phase.md)（KC705 の板）、[p013](../phase013/phase.md)（PCIe の bridge）、[p028](../phase028/phase.md)（PC の VFIO、後に）。
- 状態: in-progress（2026-10-26 06:13 Q1 新設）
- 由来: ユーザーの指示（Master の decisions-log の 2026-10-26 06:13）。

## 範囲

1. GHCI の driver（src/drivers/gpu/ghci/）を riscv64 の VF2 の kernel で build する（platform/riscv64/vmunix.mk、config の vf2）。CONFIG_GHCI_NATIVE と試験の program（userland/tests/ghci-blit など、今は amd64 だけ）を riscv64 にも。
2. VF2 の PCIe の違いに合わせる: 割り込みは INTx（PLDA は MSI を mask、pci-attach の MSI の他に INTx の道）、非一貫の DMA（ring・fence・buffer の cache の clean・invalidate、または uncached の別名 kern_pmem_map_uncached。U74 は Svpbmt 無し）、DMA は 4 GiB より下、BAR2（1 GiB）が PCIe1 の窓に入らない時は BAR2 を使わずに動く（GHCI は ring・fence が host の memory、BAR2 は framebuffer の窓だけで閉じてよい）。
3. 試験: KC705（bitstream p1-n4 0dd9f72、sha256 678b9aad…、JTAG で書く）を VF2 の M.2 に挿し、lspci、attach、CP の firmware、fill・copy（ghci-blit）、表示（ADV7511 の HDMI）、できれば vkdemo。serial と SSH で debug（ユーザーの許可、生きた serial の対話は可）。

## 受け入れ

VF2 の zedBSD が KC705 の GHCI の GPU を attach し、fill・copy の結果が一致する。表示と Vulkan は届いた所まで記録。実機の証拠（serial・SSH）と未実施を分けて書く。

## 記録

- 2026-10-26 06:13 Q1: 新設。実装は P3（WS256 p024 の P2 と file を分ける: P3 は pci-plda.c・vf2-pcie.c を触らず、要る変更は Q1 に）。
