<!-- awesome-plan project=zedbsd record=ws313-p005 -->

# ws313-p005: USB（PHY・CRG の xHCI）と keyboard・mouse

Status: in-progress（実装済み・実機未試験）
Queue: q1675-i02（P7 の配下 A）
WS: [WS313](../ws.md)、設計 [design.md](../design.md) §3、調べ [peripherals.md](../peripherals.md)

## 目的

CRG の xHCI（0xfe340000、SPI 130）と USB 2 PHY1 を起こし、既存の `pci-xhci.c` に渡して hub の後ろの 4 port の keyboard・mouse・disk を使えるようにする（`drv_s905x5m_usb_init`・`drv_s905x5m_usb_refresh`）。

## 実装（2026-10-29、commit 470b4720d）

- `src/drivers/platform/s905x5m/s905x5m-usb.c`:
  - PHY（DT の `phys` → usbphy1 u2p@0 の生の reg、usb-reset-bit 4、phy-reset-level-bits 9、pll-settings[3]・[4]）: SYS_USB の gate（REG1 bit 26）→ level の bit 9 を立てて解除 → USB の reset の pulse（0xfe002000 に 1<<4）→ r0 の host_device（bit 0）を立て POR（bit 3）を落とす → level の bit 9 を落とし・立てて PHY の設定の reset → trim（efuse 0xfe010330 bit 12 が有効なら bits 11:8 + 2、上限 12、無ければ 9 を thermometer で cfg+0x10 の下 12 bit）、cfg+0xc = 0x39、cfg+0x38 の bit 27 を落とす、cfg+0x50 = 0x7e18 → r1 の phy_rdy（bit 0）を 1 ms まで待つ → PLL（cfg+0x40: bit 27、0x549540|bit 27、bit 26、bit 25、bit 24、bit 31 の lock を待つ、5 回まで）。bit の位置は HK の `include/linux/amlogic/usb-v2.h`（union u2p_r0_v2・u2p_r1_v2）で確かめた。
  - controller: base + 0x20fc の bit 0 を落として host（UB の xhci-crg.c）。CAPLENGTH・HCIVERSION・HCSPARAMS1 と各 port の PORTSC を log。読む前に「about to read the controller ... (role register, then xHCI CAPLENGTH)」の 1 行を出す（power domain が off で止まる時の目印）。
  - rp1-usb.c の形の仮想の root bus（device 0 function 0、config space は memory、class 0x0c0330、32 bit の memory BAR = 0xfe340000、size 0x8000、INTx pin A）。DMA device は address_bits 32（buffer は 4 GiB より下、pci-xhci.c の変更は無し）。
  - 割り込み: DT の interrupts（SPI 130、edge）→ GIC 162 を edge で登録、handler から PCI core の line を走らせ EOI。polling の thread（割り込みが無ければ 2 ms、あれば 50 ms）。refresh で `drv_pci_xhci_probe_roots()` の後に PORTSC を再び log。
- USB の driver の登録（storage・UAS・HID・hub・xHCI）は rp1-usb.c と同じ順。

## 確かめ

- build: p004 と同じ（C5 の vmunix、warning 0、文字列 `s905x5m-usb: about to read` が image にある）。
- host 試験: 無し（PCI core と xHCI の側は既存の物。PHY の手順は実機でしか確かめられない）。
- 実機: 未実施。QEMU: 対象外。

## 夜の serial で見る行

- `s905x5m-usb: waking the PHY at fe348000 (configuration fe34c000)`（ここで止まれば PHY の block の power）
- `s905x5m-usb: PHY trim efuse ..., N steps (...)`、`PHY not ready (r1 ...)`（出れば ready が来ない）、`PHY PLL not locked (...), try N`、`PHY r0 ... r1 ... PLL ...`（PLL の bit 31 が lock）
- `s905x5m-usb: about to read the controller at fe340000 ...` の次に `role ... -> ..., CAPLENGTH .. HCIVERSION .... HCSPARAMS1 ...` が出るか（出なければ U2H の power domain が off の疑い、SMC が要る）
- `s905x5m-usb: port N PORTSC ... (before the reset)` と `(after the probe)`、`interrupt 162 routed (edge)`、`polling the controller every 50 ms`、その後の xHCI・hub・HID の log

## 未確認・危険

- U-Boot が USB を起こしていない時の power domain（U2H）。kernel から SMC を呼ぶ口は無い（HAL の要求は今はしない。夜に CAPLENGTH が読めるかで決める）。
- vendor の PCI の ID（0x1b8e、device 0x0001）は仮の値。xHCI の driver は class で bind する。
- `phy-meson-aml-usb2-com.c` は host の時に set_mode を init の中で呼ぶ（current_mode が host の時）。ここでは常に呼ぶ。
- 再開点: 夜の試験の結果を見て直す。
