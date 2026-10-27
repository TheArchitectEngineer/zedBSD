<!-- awesome-plan project=zedbsd record=ws304-p001 -->

# ws304-p001 調査と設計（BCM2712 の pcie1、MIP、NVMe）

- 親: [WS304](../ws.md)。Queue: q1618 / q1618-i01（担当 P3、2026-10-27）。
- Status: cleared（2026-10-27。設計の版 2、design-reviewer の反映済み。判断の項目 D1〜D7 は [design.md](../design.md) §11、推しどおりに p002 で実装。D7（HAT の型番）はユーザーへの確かめ待ち）
- Disposition: normal

## 目的・受け入れ

BCM2712 の pcie1 と BCM2711 の差、firmware の初期化の扱い、MSI（MIP）、inbound の窓と 4 GiB の上の DMA、非一貫の cache、pci-nvme の arm64 の扱い、dtb からの値の取り方を調べ、設計を書き、design-reviewer の敵対的レビューを受けて反映する。code は書かない。

## 成果

- [design.md](../design.md) 版 1・版 2（2026-10-27）。§2 dtb の事実、§3 BCM2711 との差、§4 pci-brcmstb.c の変え方、§5 Pi 5 の platform、§6 MIP、§7 build、§8 試験、§9 危険、§10 review の反映、§11 判断。
- 主な結論: pcie1 は firmware に頼らず全部を自分で起こす（`dtparam=pciex1` は dtb の status を okay にするだけ、review で確認）。bridge の reset は外の brcmstb-reset（id 43）、PERST# は 0x4064 bit 2、inbound は dma-ranges ごとの RC BAR と UBUS remap（PCI = 物理 + 0x10_0000_0000、MIP1 の窓 0xff_ffff_f000）、MSI は MIP1（data 8〜15、GIC INTID 287〜294）。pci-nvme は `drv_dma_alloc_coherent` だけを使い、非一貫の device には uncached の写像が渡るので、cache の保守は要らない。HAL・toolchain は変えない。
- 出典（値と手順だけ、code は写さない）: Raspberry Pi Linux rpi-6.12.y の pcie-brcmstb.c・reset-brcmstb.c・reset-brcmstb-rescal.c・irq-bcm2712-mip.c、OpenBSD bcm2712_mip.c 1.1、zedBSD-firmware の Pi 5 の dtb、PCIe Base 4.0、PCI Local Bus 3.0。取得物は scratchpad に置き commit しない。

## design-reviewer

- 2026-10-27 実施（1 回）。重大 1（H1 driver の中の config access の segment）・中 7・軽 11。全部を版 2 に反映（design.md §10）。
- 手順の逸脱: p002 の実装を review の結果の前に始めた（L11）。review の指摘は全部 p002 の code に入れてから確かめた。

## 未実施・制限

- 実機の値（REVISION、firmware が残す pcie1 の状態、rescal の STATUS、MIP1 の SPI の式の実際）は未確認。p003 で読む。
- D7: 買った M.2 HAT の型番は未確認（switch つきの 2 枚の HAT は範囲の外、R8）。
