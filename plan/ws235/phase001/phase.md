<!-- awesome-plan project=zedbsd record=ws235-p001 -->

# ws235-p001 調べと設計: RTL81xx（RTL8125 系）の共通の driver と RTL8127 の拡張

- 親: [WS235](../ws.md)。
- 状態: in-progress（2026-10-29 06:16 P1、q1669-i01）。
- 出典: 親の ws.md の「ユーザーの決定（2026-10-29）」（原文はそちら）。順はユーザーの「まずRTL81xxのドライバを、他のOSを参考にして作成して動くようにしてから、
  RTL8127の拡張部分を実装する」。
- 依存: なし。

## 再開点

- 2026-10-29 06:5x P1: 観測と資料の読みは済み（下）。設計の節を書いた。次は design-reviewer のレビュー → 指摘の反映 → ws.md の Phase の表の書き換え → cleared の判定を Q1 へ。

## 実機の観測（2026-10-29 06:17〜06:19 P1、10.0.10.3 の Debian 6.12.111、読みだけ、機械の変更なし）

RDNA 機の枠（P1、06:20〜07:05 の予定）を 06:19 に早く返した（lock は空にして返した）。機械は Debian のまま。

| 項目 | 観測 | 取り方 |
| --- | --- | --- |
| device | `03:00.0` `[10ec:8127]` rev 05、Subsystem 10ec:0123、IOMMU group 11 | `lspci -nn`・`lspci -vvv` |
| BAR | BAR0 I/O 0x4000（256）、BAR2 MMIO 64bit 0x81200000（256 KiB、register）、BAR4 MMIO 64bit 0x81260000（16 KiB、MSI-X）、ROM 128 KiB | `lspci -vvv` |
| 割り込み | MSI 1 本（64bit、mask 無し）、MSI-X 64 本（table BAR4+0x0、PBA BAR4+0x800）、INTx pin A | `lspci -vvv` |
| PCIe | endpoint は 16GT/s x1 が可能、root port 00:1c.5（PCH）が 8GT/s x1 まで → 8GT/s x1。kernel の表示「7.876 Gb/s available PCIe bandwidth, limited by 8.0 GT/s PCIe x1 link at 0000:00:1c.5」。MaxPayload 256、MaxReadReq 512、ASPM は無効、L1 PM substates は無効 | `lspci -vvv`、`dmesg` |
| 版の register | MMIO 0x40（TXCFG）= `0x6f900d80`。`& 0x7cf00000` = `0x6c900000` | BAR2 の resource2 を python の mmap で読み（PROT_READ） |
| MAC address | MMIO 0x00 = `370b861c`、0x04 = `00005d96` → `1c:86:0b:37:96:5d`。0x19e0・0x19e4 にも同じ値（0x19e4 = `00045d96`） | 同上 |
| PHY の状態 | MMIO 0x6c = `f080008b`（下 16 bit `0x008b`）: OpenBSD の bit の意味で FDX・LINK・100MBPS と 0x80 | 同上 |
| Linux の対応 | Debian 6.12.111 の kernel はこの device に bind しない（interface が出ない）。`/lib/firmware/rtl_nic/` に rtl8125*・rtl8126* はあるが rtl8127* は無い（firmware-realtek 20250410-2）。apt の source は trixie・security・updates だけ（backports 無し） | `ls`、`apt-cache policy` |
| 相手 | ユーザーの回答（Q1 経由、2026-10-29 クリック）「centris の 10GbE に直結」。Q1 の訂正: 直結の相手は centris の `enp94s0f1`（Intel X540-AT2 `[8086:1528]`、ixgbe、100baseT・1000baseT・10000baseT を広告、2.5G・5G は無い）。06:3x の centris の ethtool では `Link detected: no`。 | centris の `ethtool`・`lspci` |

- **食い違い（未確認）**: RTL8127 の PHYSTAT は 100M full の link を示すが、centris の enp94s0f1 には carrier が無い。PHYSTAT の値が driver の無い状態で意味を持つか
  （古い値か既定の値か）、cable が本当に enp94s0f1 に繋がるかは未確認。p003 で PHY の初期化の後に両側の link を読んで確かめる。
- 他の device（Q1 の観測と同じ）: `04:00.0` RTL8111/8168（r8169、down）、USB の RTL8156（r8152、2.5G、10.0.10.3 を持つ）、GPU は RTX 2070。

## 取った資料（2026-10-29 P1、パラメータの無い GET、`build/tmp/ws235-src-061714/` に置いた。tree には入れない）

| 資料 | 版 | license | SHA-256 |
| --- | --- | --- | --- |
| OpenBSD `sys/dev/pci/if_rge.c` | `$OpenBSD: if_rge.c,v 1.48 2026/10/27 01:38:20 kevlo` | ISC（Kevin Lo） | 0ad25a8b67b57d0a99bb90d1dbc515e74527a6072fca8a0b4bf7c2c0f1c3cfc3 |
| OpenBSD `sys/dev/pci/if_rgereg.h` | 同じ日の master | ISC | 199a6dff6d3be21dabcb8ed91cd3cab149ba93fa2e50e5ec0d2e865011144436 |
| FreeBSD `sys/dev/rge/if_rge.c` | main（2026-10-29 の取得） | BSD-2-Clause（Kevin Lo、Adrian Chadd） | 7a424d8c8da10c24049c0bc643104ecdb2364cb8ce7d7dab309f0e9a0693ebf5 |
| Linux `drivers/net/ethernet/realtek/r8169_phy_config.c` | master（2026-10-29 の取得） | GPL-2.0-only（値・表だけを block ごとの出典で使う） | 11a8ef375261e3aa3575ac641ea413f3c951d2cbf3b716bfd241b2c9ef78819c |
| linux-firmware `WHENCE` | gitlab の main | — | a3453858252ab19d0655ce0a1d4144f583b13d0be56ae0ab9a7a2e7751c32bcd |

- 取れなかった物: raw.githubusercontent.com の Linux `r8169_main.c`・`r8169.h`・`drivers/net/phy/realtek/realtek_main.c` は 429（rate-limited）。規則どおり止め、
  別の host で取り直さない（Q1 2026-10-29「Linux の r8169_main.c は今は別の host でも取らない」）。cgit.freebsd.org の `sys/dev/rge/` の
  `if_rge_microcode.h`・`if_rgereg.h`・`if_rgevar.h`・`if_rge_hw.h`・`if_rge_vendor.h` は 502（server の誤り）で取れず、再試行していない。
- firmware: linux-firmware の WHENCE に `rtl_nic/rtl8127a-1.fw`（Version 0.0.5）、「Licence: Redistributable. See LICENSE.r8169 for details.」
  （SPDX `LicenseRef-LICENSE.r8169`）。file そのものは取っていない（使う時は zedBSD-firmware の手順で、Q1 に先に）。OpenBSD の firmware の snapshots の一覧
  （firmware.openbsd.org、2026-10-27）に rge の package は無い。OpenBSD の rge が読む `rge-rtl8127` の file の出どころと license は未確認。

## 調べの結果

### 1. RTL8125 系の driver（rge）の作りは RTL8127 に当たるか

当たる（資料で確かめた。実機での動作は未確認）。

- OpenBSD rge 1.48 と FreeBSD rge は、RTL8125・8125B・8125D・8126 と同じ driver の中で RTL8127 を扱う。版の判定は TXCFG の `& 0x7cf00000`
  （OpenBSD `if_rgereg.h` の `RGE_TXCFG_HWREV`）で、`0x6c900000` が RTL8127（OpenBSD `rge_attach`）。10.0.10.3 の観測値 `0x6c900000` と一致する。
  同じ値で MAC の OCP 0xd006 が 0x0007 なら RTL8127ATF（SFP+ の版）、それ以外は RTL8127（銅線）。この機械の OCP 0xd006 は未確認（読むには OCP の窓への書き込みが要るので今回は読まなかった）。
- 版ごとの表（OpenBSD `rge_attach`）: 0x60900000 RTL8125、0x64100000 RTL8125B、0x64900000・0x64a00000 RTL8126（2 版）、0x68800000・0x68900000 RTL8125D（2 版）、
  0x6c900000 RTL8127。RTL8122・8121・8120 は rge の表に無い（版の値・PCI の ID は未確認）。
- ユーザーの伝聞「RTL8127 は RTL8125 などの driver でもそのまま使える」について: rge の作り（OCP の窓、MAC・PHY の MCU の patch、EPHY の表、descriptor の ring、
  ISR・IMR）は共通だが、RTL8127 には固有の分岐がある（下の 2）。RTL8127 の分岐の無い古い RTL8125 の driver は、TXCFG の版が表に無いので attach しない
  （OpenBSD の `default: unknown version`）。分岐の無い RTL8125 の手順のままで link と通信が成り立つかは未確認。

### 2. RTL8127 の固有の差（OpenBSD rge 1.48・FreeBSD rge から）

| 差 | 内容 | 共通の段で要るか |
| --- | --- | --- |
| descriptor の形 | `RGE_FLAG_DESCV2`: TX の descriptor が 16 byte（v1 は 32 byte）、RX も v2 の形。TSO の bit は v2 の extsts。v2 の時は RXCFG の v1 の有効の bit を落とし、MAC OCP 0xeb58 と MMIO 0xd8 の bit を v2 に合わせる（`rge_init`） | rge は RTL8127 で常に v2。RXCFG の v1 の bit を残せば RTL8127 が v1 で動くかは未確認（ユーザーの伝聞「8125 の driver でそのまま」が当たるならここ） |
| PCI の拡張の config 空間 | `rge_init` が config 0x70c・0x890（RTL8126・8127 は L1 の timeout の無効）を書く | 要る（zedBSD の PCI の config の 0x100 以上の読み書き） |
| MAC の MCU | `rge_mac_config_ext_mcu`: RTL8126・8127 は版を読み比べ、違えば 3 page（0〜255、256〜511、残り）に書き、break point の register を書く | 要る（firmware の MAC の部） |
| PHY の MCU | `rge_phy_config_mcu`: 4097 個を書き、patch の要求を一度下げてから残りを書く。ram code の版 0x0051 | 要る（firmware の PHY の部） |
| PHY の設定 | `rge_phy_config_mac_r27`: 値の表（mac_cfg_value など）と OCP の書き込み | 要る |
| EPHY | `mac_r27_ephy` の表と `rge_r27_write_ephy` | 要る |
| media | 10G の広告（`RGE_ADV_10000TFDX` 0x1000）、PHYSTAT の 10G の bit 0x4000 | 要る（相手の X540 は 2.5G・5G を持たないので 10G か 1G） |
| 割り込み | MSI-X が 30 本以上なら多 vector: link が vector 29、TX が 8+i、RX の queue は最大 8（RSS）。IMR・ISR は V2・V4 の register | 拡張（共通の段は 1 vector） |
| ATF | MAC OCP 0xd006 == 0x0007 なら SFP+ の版 | 判定だけ（この機械は銅線と見られる、未確認） |

### 3. firmware

- rge は attach で firmware の file を読み（`rge_loadfirmware`）、失敗すると attach をやめる。file の形は OpenBSD の独自の形（`maclen`・`phylen` の header と
  (reg, val) の 16 bit の組の列）。MAC の部の末尾 4 個が MCU の版（`rge_mcu_get_bin_version`）。
- zedBSD で使える候補は linux-firmware の `rtl_nic/rtl8127a-1.fw`（再配布可、LICENSE.r8169）。形は Linux の r8169 の firmware の形で、OpenBSD の形とは違う
  （中身の形は未確認。r8169 の firmware の解釈の code は GPL なので写さない。形の事実を file の観測と資料から決める）。
- patch 無しで link が上がるか、ram code の版（PHY OCP 0x801e）が電源の投入の後に何であるかは未確認。p003 で、まず patch 無しで link を試し、
  要れば firmware を zedBSD-firmware の手順で入れる（Q1 へ先に）。

### 4. 同じ機械の Linux の値

- Debian 6.12 では取れない（driver も firmware も無い）。Linux の本線は RTL8127 を持つ（r8169_phy_config.c の `rtl8127a_1_hw_phy_config`、`RTL_GIGA_MAC_VER_80`）が、
  入った版は未確認。取るには (a) 新しい kernel（trixie-backports の追加か本線の build）と linux-firmware の rtl8127a-1.fw、(b) Realtek の配布の driver の
  どちらかが要る。どちらも 10.0.10.3 の Debian の変更なので、**ユーザーの判断が要る**（Q1 へ）。
- 判断が出るまで、受け入れの比べは PCIe の上限（7.876 Gb/s の表示、ユーザー「7Gbit/sくらい」）を使う。

## 設計

### 置き場所と名前

- `src/drivers/ethernet/rtl8125.c`（共通の core）、`rtl8125-chip.c`（版の表と chip ごとの hook）、`rtl8125-regs.h`、`rtl8125-tables.c`（EPHY・PHY の値の表、
  block ごとの「Values referenced:」）。名前・型・macro は独自（rge・r8169 の symbol を写さない）。
- config: `CONFIG_DRIVER_PCI_RTL8125`（menuconfig の Drivers）。試験の config は `plan/ws235/tests/config-amd64-rtl8127.mk`
  （`config/ci/config-amd64-server.mk` はユーザーの物で変えない。WS の目標の「server の config で有効にする」はユーザーに頼む形で最後に）。

### 出典と license

- OpenBSD rge（ISC）・FreeBSD rge（BSD-2）: 手順を参考にし、値・表は block ごとに「Values referenced:」「Procedure referenced:」で出典を書く。code を写す時は
  license の著作権表示を該当の block に付ける（guardrail）。既定は写さず、独自に書く。
- Linux r8169（GPL）: 値・表だけ、block ごとの出典つき。code・comment・構造は写さない。手順の引用は WS039 にしか許可が無いので、要る時は Q1 経由でユーザーに聞く。

### 段（ユーザーの順: RTL81xx の共通の driver で動かす → RTL8127 の拡張）

1. 共通の骨格: PCI の ID（10ec:8125・8126・8127、他は未確認）、BAR2 の MMIO、bus master、版の判定（TXCFG）、MAC・PHY・CSI・EPHY の OCP の窓、reset、
   MAC address、1 vector の割り込み（MSI-X の 1 本か MSI）、net device の登録（link は down のまま）。
2. MAC・PHY の初期化と link: hw_init（MCU の無効化と MCU の patch）、EPHY・PHY の表、auto-negotiation（10G・1G・100M の広告）、PHYSTAT の読みと link の報告、
   link の変化の割り込み。firmware は patch 無しから試す。
3. ring と割り込み: RX・TX の ring（RTL8127 は v2 の descriptor、他は v1）、1 queue、1 vector、network stack への接続（offload 無し、stack に口が無い）。
4. 実機の確かめ（共通の段）: link（両側）、DHCP か static、ping、ssh、TCP の転送。
5. RTL8127 の拡張: 多 vector の MSI-X（link・TX・RX の queue）、RSS と複数の RX queue、割り込みの抑え（moderation）、RX の zero copy、PCIe の上限（約 7 Gbit/s）に近づける。
   checksum・TSO の offload は stack の口（kern/net）が要るので、driver の側の用意と計測までにし、口は WS221 と組で Q1 に諮る。
   net の worker は 1 本なので、7 Gbit/s に届かない時の律速が driver か stack かを profile で分けて記録する。
6. 他の chip の表（8125・8125B・8125D・8126、未確かめ）。
7. 規約の全文の見直し。

### 受け入れ（WS）

- 10.0.10.3 の RTL8127 で link が上がり（centris の X540 の側も ethtool で同じ速さ）、IPv4（ping・ssh）、TCP の転送が PCIe の上限 約 7 Gbit/s
  （同じ機械の Linux の値が取れればそれ）に見合う事。値は zedBSD と Linux を同じ相手・同じ道具で並べる。
- 試験の相手の centris の enp94s0f1 に IP を付けるなど centris の設定の変更は、先に Q1 に案を送る（Q1 2026-10-29）。

## zedBSD の側の口（2026-10-29 P1、tree の読み）

- net device: `include/kern/net/net-device.h` の `struct net_device_ops`（open・close・transmit・poll_receive・ioctl・release・set_mtu）、
  `net_device_alloc` → field（name・flags・mtu・mtu_max・link_mbps・hwaddr）→ `net_device_create`。RX は割り込みで `net_device_schedule_poll`、
  net の worker が `poll_receive(dev, budget)` を呼び、driver は `net_device_receive` で渡す。link は `net_device_set_carrier` と `link_mbps`。
  手本は `src/drivers/ethernet/dw-eqos.c`（登録・TX・RX の写し・link の poll の kthread）。
- packet: `include/kern/net/packet-buf.h`。storage は物理で連続（`packet_buf_physical` → `drv_dma_physical_address` で TX は in place の DMA）。
  RX は dw-eqos と同じく、DMA の buffer から新しい packet に写す形を既定に（zero copy は拡張の候補）。jumbo は 9216 byte の storage が 32 個。
- **stack に checksum・TSO の offload の口が無い**（packet-buf・net-device に flag が無い）。driver の共通の段では offload を使わない。offload・TSO・
  複数の RX queue を活かす口は kern/net の変更で、この WS の範囲の外（WS221 の TCP の改善と組で Q1 へ。この WS の拡張の段では driver の側の用意まで）。
- PCI: `drv_pci_driver_register`（ID の表）、`drv_pci_device_claim_bar`・`map_bar`（NOCACHE）・`enable_memory`・`set_bus_master`、
  `drv_pci_device_config_read32`・`write32`（0x70c・0x890 の拡張の config 空間に届くかは p002 で確かめる）、
  `drv_pci_device_allocate_irqs`（MSI-X・MSI・INTx、min・max の本数）・`establish_irq`（handler は割り込みの文脈、`int h(void *)`）。手本は intel-ax211-pci.c・pci-nvme.c（1 vector）。
- DMA: `drv_pci_device_dma` → `drv_dma_alloc_coherent`（ring、256 byte 揃え）。
- 待ち: `kern_usleep_range`（短い busy wait）、`sched_sleep`（ms）。lock は `spin_lock_irqsave`（割り込みと共有）・`mutex`。log は `kern_logf`。
- 周期の仕事: callout は無い。dw-eqos と同じく kthread で link を 1 秒ごとに読む（rge も 1 秒の tick で PHYSTAT を読む）。
- firmware: `/lib/firmware/<name>` を `file_openat`・`file_content_lease_*` で読む（intel-ax211-firmware.c の形）。package は `userland/firmware/`、
  driver との対応は `config/drivers/firmware.list`。
- build: `config/drivers/pci.drivers`・`config/drivers/menu.list` の `Ethernet|`、Makefile の既定 `?= n` と `-D`、`platform/amd64/vmunix.mk` の source の一覧、
  `src/kern/platform/pcat.c` の登録の呼び出し。

## 未実施

- design-reviewer のレビュー。
- RTL8127ATF かどうか（OCP 0xd006）、PHY の ram code の版、firmware の形。
- 同じ機械の Linux の値（ユーザーの判断待ち）。
