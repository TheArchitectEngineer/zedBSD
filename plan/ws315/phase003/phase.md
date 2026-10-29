<!-- awesome-plan project=zedbsd record=ws315-p003 -->
# ws315-p003: 5330 の実機（RTL8156 の vendor mode）

Status: in-progress（2026-10-30 05:27 P3: k4 で vendor mode の受け入れの主な項目を満たした、残りは下の未実施。前の記録: 2026-10-30 04:16 P3 q1694-i01: k1 FAIL（SSH 無し）、5330 の電源の入れ直し待ち）
Parent: [WS315](../ws.md)
Queue: q1694 / q1694-i01（P3）
依存: p002

## 試験

- `plan/ws315/tests/5330-rtl.sh KERNEL NAME [SECONDS]`（MODE=--nvme-kernel、CFG_ADD で boot の語、出力 build/ws315-hw/NAME/summary.txt）。
- 受け入れ: send・recv 30 s（相手 10.0.10.2）を NCM（`usb.rtl815x=off`）と Linux r8152（WS221 p014: 2353.4・2353.6）と並べる、ping 1472、開いたままの MTU の変更、down・up、
  USB の他の device（Broadcom の ccid・AX211 の bt・camera）の非退行、抜き差し（5330 は遠隔で抜けない: 未実施になる見込み、ユーザーの拾える試験の一覧へ）。

## 結果（5330）

| run | kernel | 結果 |
| --- | --- | --- |
| k1（2026-10-30 04:03、`5330-rtl.sh build/ws315-5330/vmunix k1 30`、MODE=--nvme-kernel） | 5534277a3 の tree | FAIL: 5 分 SSH 無し、10.0.30.3 は ping も無し、NCM の時の MAC の host も無し。5330 は電源の入れ直し待ち（Q1 がユーザーに依頼）。原因は未確定 |

- k1 の後の直し（16122571a）: attach の待ち（autoload・PHY の状態・MAC の reset）を FreeBSD と同じく時間切れでも続ける（10 s、log）、attach・open の手順ごとの失敗の log、
  link を interrupt が告げない時に worker が 1 秒ごとに PHYSTATUS を読む（carrier が立たず DHCP が始まらない、を候補として）。
- 試験の script（7712411ab）: 前の起動の /var/log/kernel.log.old の driver の行を集める（届かなかった起動の原因を、次の起動で読む）。出力は build/ws315-hw/NAME。
| k2-ncm（04:5x、16122571a、`CFG_ADD=usb.rtl815x=off`） | NCM | 5330-run PASS: token で NCM（urbs=4/2、link 2500）、send 2062.8・recv 1473.7 Mbit/s、ping 1472 3/3、MTU 9000 は NCM の上限（1504）で拒否（想定どおり）、1500 に戻して ping 3/3。終わりの zedBSD の reboot の後に Debian が 5 分戻らず（cfg の行を戻せず: NVMe の zedbsd.cfg に `usb.rtl815x=off` が残る）。5330 は再び到達不能 |

- k1 の原因（k2-ncm が集めた /var/log/kernel.log.old）: `usb-rtl815x: ue0 chip=RTL8156B version=0x7410 mac=6c:1f:f7:06:14:8a mtu_max=9000 urbs=4/4 rx_block=49152`
  （attach は成功、MAC は Debian と同じ）、その後 `ue0 open failed (3)`（EINVAL）×5。原因: RX・TX の URB に付けた `drv_usb_urb_reserve_transfer` が同期の staging を作り、
  callback 付きの `drv_usb_urb_setup` を EINVAL で拒む。予約を外した（次の commit）。5330 の RTL8156 は RTL8156B（0x7410）だった。

- k2-ncm の「Debian が戻らない」の原因: 試験の script が SSH（ue0 を通る）の上で `ifconfig ue0 down` を流し、session が切れて up が走らず、5330-run の最後の reboot（SSH）も届かなかった。
  p014（xHCI の depth）ではない。直し 02e29e5cf（down・up を 5330 の上で detach して流す）。5330 は 2 回、ユーザーが電源を入れ直した。

| run | kernel | 結果 |
| --- | --- | --- |
| k3-ncm（05:2x、2d208a345 の tree、`CFG_ADD=usb.rtl815x=off`） | NCM（urbs 4/2） | 5330-run PASS、終わりの reboot から Debian に戻った（Q1 の指示: depth > 1 の teardown を伴う再起動を実機で 1 回）。send 1971.0・recv 1479.9、ping 1472 3/3、down・up（rc 0）の後の ping 3/3、MTU 9000 は NCM の上限で拒否（想定どおり）、RX errors 6・TX dropped 456639（queue の満杯の背圧） |
| **k4-rtl**（05:4x、同じ kernel、token なし） | **vendor mode**（RTL8156B 0x7410、urbs 4/4、RX block 48 KB） | 5330-run PASS、reboot から Debian に戻った。**send 2372.2・recv 2319.1 Mbit/s**（30 s、相手 10.0.10.2）、link up 2500 full-duplex、ping 1472 3/3、開いたままの MTU 9000 への変更（rc 0）と 1500 に戻した後の ping 3/3、down・up（rc 0）の後の ping 3/3、RX errors 0、TX dropped 177154（背圧） |

| driver | send Mbit/s | recv Mbit/s | 記録 |
| --- | --- | --- | --- |
| zedBSD NCM（WS221 p014） | 2097.0 | 1449.0 | WS221 p014 |
| zedBSD NCM（この Phase、k3-ncm） | 1971.0 | 1479.9 | build/ws315-hw/k3-ncm |
| **zedBSD vendor mode（k4-rtl）** | **2372.2** | **2319.1** | build/ws315-hw/k4-rtl |
| Debian r8152（WS221 p014） | 2353.4 | 2353.6 | WS221 p014 |

- 受けは NCM の 1.57 倍で Linux の 98.5 %、送りは Linux の 100.8 %。
- USB の他の device の非退行（k4 の kernel log）: webcam は uvc、Broadcom 58200 は usb-ccid、AX211 は usb-bt で k3-ncm と同じ。i915 の survey の diff は PASS（steady の register の差 0）。
- 未実施: camcap（NVMe の userland に無い、image の試験が要る）、send と recv の同時、相手の port の上げ下げ、抜き差し（5330 は遠隔で抜けない、ユーザーの拾える試験の一覧へ: Q1）、
  9000 の相手との jumbo の ping（相手が無い）、RTL8156（A、0x7020/0x7030）と 0x7400・0x7420 の実機（手元は 0x7410 だけ）。
