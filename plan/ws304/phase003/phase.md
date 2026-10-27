<!-- awesome-plan project=zedbsd record=ws304-p003 -->

# ws304-p003 実機（Pi 5 ＋ M.2 の NVMe）での確かめ

- 親: [WS304](../ws.md)。依存: p002、実機の用意（ユーザー: 新しい SwitchBot、2026-10-27 の夜の予定）。
- Status: planned（実機待ち）
- Disposition: normal

## 目的・受け入れ

Pi 5 に M.2 HAT と NVMe を付けた実機で、pcie1 の link が上がり、NVMe が `/dev/nvme0n1` として見え、読めること。RP1 の USB・Ethernet を壊さないこと。

## 手順

1. image: `config/ci/config-rpi5.mk` で作る（`CONFIG_DRIVER_PCI_NVME=y`、`config.txt` に `dtparam=pciex1`）。p002 の merge の後の main の SHA で。
2. HAT の型番を記録する（単一の NVMe であること。switch つきなら R8）。HAT の電源の LED を見る（ユーザー）。
3. 起動して `dmesg` で次の行を見る（値は版の違いで少し変わってよい）:
   - `rpi5-pcie: PHY calibration done already` または `rpi5-pcie: PHY calibration left as it is: RP1's link is up`
   - `rpi5-mip: messages to PCI fffffff000, data 8-15, GIC lines 287-294`
   - `pcie: brcmstb resetting at 1000110000`、`pcie: brcmstb at 1000110000 revision <n> (BCM2712)`
   - `pcie: brcmstb inbound 1 PCI 1000000000 CPU 0 size 1000000000`、`pcie: brcmstb inbound 2 PCI fffffff000 CPU 1000131000 size 1000`
   - `pcie: brcmstb link up, generation 2, x1`（Gen 1 の device なら 1）
   - `pcie: brcmstb 01:00.0 BAR0 at 80000000 (16384 bytes)`（大きさは device による）
   - `rpi5-pcie: segment 1 started, message interrupts through the MIP`
   - `nvme: PCI controller 0001:01:00.0 version=... MSI-X`
   - `nvme: /dev/nvme0n1 namespace=1 blocks=... block-size=512 ...`
   - RP1 の USB・Ethernet の行がこれまでと同じであること、`rpi5-serror` の行が出ないこと。
4. 読みの試験（書かない）: `dd if=/dev/nvme0n1 of=/dev/null bs=1048576 count=256` が終わり、時間を記録する。`dd if=/dev/nvme0n1 bs=1048576 count=64 | cksum` を 2 回流し、同じ値であること。`blkid` か `diskpart` で区画が読めること（区画があれば）。
5. 失敗の切り分け:
   - `link down`: HAT の LED、FFC の向き、`dtparam=pciex1` の有無。`pciex1_tperst_clk_ms`（例 10）を足して再試験。
   - `PHY register ... write did not finish`: MDIO の列を読む（R4）。
   - link は上がり `nvme: attach failed at ...` か時間切れ: MSI が来ていない疑い（R1）。gdbstub は無いので、log の GIC line と `hw` の割り込みの数で見る。MSI を切る switch は作らない（R5）。uncleared にして設計を直す。
   - SError: `rpi5-serror` の行の段の名前で場所を見る。
6. 任意: Pi 4（Pi 4 の VL805 の USB が今までどおり動くこと。pci-brcmstb.c を共有するため）。T1 か Q1 の判断で。

## 記録

- 実機の証拠と host 試験の証拠を分けて書く。やっていない確かめは未実施と書く。
