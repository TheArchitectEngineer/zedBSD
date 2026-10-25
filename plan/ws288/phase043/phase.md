<!-- awesome-plan project=zedbsd record=ws288-p043 -->

# ws288-p043 GHCI の driver を VisionFive 2（riscv64）の M.2 の KC705 で

- 親: [WS288](../ws.md)。関係: [WS256 p024](../../ws256/phase024/phase.md)（VF2 の PCIe1・NVMe）、[p024](../phase024/phase.md)（KC705 の板）、[p013](../phase013/phase.md)（PCIe の bridge）、[p028](../phase028/phase.md)（PC の VFIO、後に）。
- 状態: in-progress（2026-10-26 06:13 Q1 新設）
- 由来: ユーザーの指示（Master の decisions-log の 2026-10-26 06:13）。

## 範囲

1. GHCI の driver（src/drivers/gpu/ghci/）を riscv64 の VF2 の kernel で build する（platform/riscv64/vmunix.mk、config の vf2）。CONFIG_GHCI_NATIVE と試験の program（userland/tests/ghci-blit など、今は amd64 だけ）を riscv64 にも。
2. VF2 の PCIe の違いに合わせる: 割り込みは INTx（PLDA は MSI を mask、pci-attach の MSI の他に INTx の道）、非一貫の DMA（ring・fence・buffer の cache の clean・invalidate、または uncached の別名 kern_pmem_map_uncached。U74 は Svpbmt 無し）、DMA は 4 GiB より下、BAR2（64 MiB、BAR0 128 KiB、32 bit）が PCIe1 の窓に入らない時は BAR2 を使わずに動く（GHCI は ring・fence が host の memory、BAR2 は framebuffer の窓だけで閉じてよい）。
3. 試験: KC705（bitstream p1-n4 0dd9f72、sha256 678b9aad…、JTAG で書く）を VF2 の M.2 に挿し、lspci、attach、CP の firmware、fill・copy（ghci-blit）、表示（ADV7511 の HDMI）、できれば vkdemo。serial と SSH で debug（ユーザーの許可、生きた serial の対話は可）。

## 受け入れ

VF2 の zedBSD が KC705 の GHCI の GPU を attach し、fill・copy の結果が一致する。表示と Vulkan は届いた所まで記録。実機の証拠（serial・SSH）と未実施を分けて書く。

## 記録

- 2026-10-26 06:13 Q1: 新設。実装は P3（WS256 p024 の P2 と file を分ける: P3 は pci-plda.c・vf2-pcie.c を触らず、要る変更は Q1 に）。
- 2026-10-26 06:20 Q1: 板の bitstream（p1-n4 0dd9f72）の PCIe の bridge（boards/kc705/lib/atg_kc705_pcie.v）は MSI_enable が 1 の時だけ INTX_MSI_Request を上げる（`else if (msi_en && msi_req && !ack_q)`）ので、MSI の無い VF2（PLDA は MSI を出さない、zedBSD の pci-plda.c の未実装、hardware は MSI を持つ）では GPU の割り込みが届かない。→ P3 に driver の polling の形（fence の word と IRQ_STATUS を約 1 ms で）を VF2 の既定に、INTx の道は後の bitstream 用に残すよう伝えた。BAR は BAR0 128 KiB・BAR2 64 MiB（32 bit、ip.tcl）で、範囲の「BAR2 1 GiB」は誤り。PCIe1 の 32 bit の窓（NVMe の BAR0 が 0x3800_0000）に入る見込み。bridge は Gen2 x4、M.2 では x1。後の bitstream の候補: MSI が無効の時に INTx を出す（PG055 の INTX_MSI_Request の legacy の意味を確かめて）。
- 2026-10-26 06:24 P3 → Q1: polling（割り込みが取れない時か CONFIG_GHCI_POLL=y で 1 ms の kthread、IRQ_ENABLE は 0）、host の memory は非一貫の時か CONFIG_GHCI_HOST_UNCACHED=y で uncached の別名（kern_pmem_map_uncached）、BAR2 が無くても続ける。visionfive2.c の VF2_DMA_DEFAULT_COHERENT は 1（PLDA の DMA が coherent と報告）なので VF2 の試験の config は uncached を強制。P2 の file への提案 2 つを Q1 が P2 に回した: A（必須）vf2-pcie.c の register_drivers() に drv_pci_ghci_driver_register（drv_pci_driver_register は root の bus だけを辿るので bus 1 の KC705 が渡らない）、B pci-plda.c の assign_bar が入らない BAR で ENOSPC → assign_resources が止まり root port が設定されない → BAR を 0 にして続ける。今の bitstream は BAR0 0x3800_0000・BAR2 0x3C00_0000 で 128 MiB の窓にちょうど入る。coherent の既定は P2 に p023 の結果を聞いた。
