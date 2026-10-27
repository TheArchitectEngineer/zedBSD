<!-- awesome-plan project=zedbsd record=ws304-p002 -->

# ws304-p002 実装・host 試験・build

- 親: [WS304](../ws.md)。Queue: q1618 / q1618-i01（担当 P3、2026-10-27）。依存: p001（版 2）。
- Status: cleared（2026-10-27。host 試験 3 本 PASS、rpi5・rpi4 の vmunix の build warning 0。実機は p003）
- Disposition: normal

## 目的・受け入れ

design.md 版 2 を実装し、register の列と窓の計算の host 試験、`config-rpi5.mk` と変えた共有の所に効く `config-rpi4.mk` の build（warning 0）を通す。Pi 4 の pci-brcmstb を壊さない。実機の確かめ（p003）の手順を書く。

## 変えた物

| file | 内容 |
| --- | --- |
| `include/drivers/pci/pci-brcmstb.h` | variant（0 = BCM2711）、segment、inbound の窓の配列（最大 3）、max_link_speed、tperst_ms、QoS、coherent、board の操作（bridge の reset、MSI の establish・disestablish） |
| `src/drivers/pci/pci-brcmstb.c` | BCM2712 の describe（全部の dma-ranges、max-link-speed、tperst、fifo-qos-map）、check、start（board の bridge reset → PERST# → SerDes → MISC_CTRL → inbound と remap → endian・INTR2 → MDIO の 54 MHz refclk・PHY_CTL_15・UBUS/AXI・timeout・QoS → 世代 → CLKREQ を切って PERST# を離す（tperst）→ endpoint を 1 秒まで待つ → clkreq safe → outbound・class・BAR）、失敗の時の SerDes off と bridge reset、DMA（64 bit、offset 0x10_0000_0000、64 GiB）、segment、MSI の hook の表、allocate の MSI-X・64 bit MSI、link の毎回の確かめ。BCM2711 の path は変えない。file 先頭の `plan/ws048/design.md` への参照を外した（Guardrail の開発の管理情報） |
| `src/drivers/platform/rpi5/rpi5-pcie.c`・`.h`（新） | RP1 の外の bcm2712-pcie を起こす。RP1 の root complex（子に RP1 の bus）は触らない。segment = linux,pci-domain（0 は拒む）。reset block の bridge の line（write-only の SET・CLEAR）、rescal（済みか RP1 が生きていれば飛ばす）、MIP、NVMe の登録と namespace の公開 |
| `src/drivers/platform/rpi5/rpi5-mip.c`・`.h`（新） | MIP1: msi-ranges・msi-offset・doorbell を読み、全部の line を start の時に edge にして確かめ（失敗なら MSI 無し = INTx）、establish（lock の外で register と unmask）、仕事なしの時の最大 8 回の呼び直し、disestablish（1 秒の drain） |
| `src/kern/platform/rpi5.c` | start_rp1 の後に start_pcie、refresh で namespace、各段の後の SError の確かめ |
| `platform/arm64/vmunix.mk` | rpi5 に rpi5-pcie.c・rpi5-mip.c・pci-brcmstb.c、`CONFIG_DRIVER_PCI_NVME=y` で pci-nvme.c |
| `config/ci/config-rpi5.mk` | `CONFIG_DRIVER_PCI_NVME := y` |
| `platform/arm64/config-rpi5.txt` | `dtparam=pciex1`（D1） |
| `plan/ws304/tests/` | `host-test.mk`、`brcmstb2712-host-test.c`、`rpi5-mip-host-test.c`、`brcmstb-trace.c`、`enable-pcie1.py` |

## 確かめ（2026-10-27、worktree `/home/awe/zedBSD-worktrees/p3-ws304`）

| コマンド | 結果 |
| --- | --- |
| `make -f plan/ws304/tests/host-test.mk run` | `brcmstb2712: 1144 checks passed`、`rpi5-mip: 878 checks passed`、`brcmstb-trace: the BCM2711 sequence is unchanged (117 lines)`（WS の始まりの main 13c03a376 の pci-brcmstb.c と今の物の、BCM2711 の register の列の全部が `cmp` で同じ） |
| 変異の確かめ（build/ws304-host/mut-*.c、試験は直さず driver の 1 行を壊す） | segment の行を消す・remap の enable を消す・endpoint の待ちを消す・MSI の 64 bit の判定を逆にする、の 4 つとも brcmstb2712 が FAIL |
| `make -j16 ZEDBSD_CONFIG=config/ci/config-rpi5.mk vmunix` | exit 0、warning 0、`arm64 vmunix check: PASS`（pci-nvme.c が arm64 で初めて build される） |
| `make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk vmunix` | exit 0、warning 0、`arm64 vmunix check: PASS` |
| WS048 の `brcmstb-host-test.c`（closed の WS の物、1 回だけ、capability の stub を足して） | `ws048-brcmstb: 1790 checks passed`（設計の版 1 の実装の時点。以後の BCM2711 の確かめは trace の比較） |
| `git diff --check` | 出力なし |

- host 試験の DTB は `build/sources/firmware/rpi5-boot/3d301dd924bcd758a4c8cb19fe8531031f033f43/` の C1・D0（pcie1 を okay にした写しは `enable-pcie1.py` で作る）。
- 未実施: QEMU（Pi 5 の QEMU は無い）、実機（p003）、Pi 4 の実機の起動（PCIe の path は QEMU の raspi4b に無い）、`config-rpi5.mk` の image の build（kernel だけ build した）。

## 残り

- p003（実機）。p004（規約の全文の見直し）。
- 削除の依頼（Q1）: `plan/ws304/tests/brcmstb2711-stub.c`（未追跡、WS048 の試験を 1 回流すために作った物。commit していない）。
