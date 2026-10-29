<!-- awesome-plan project=zedbsd record=ws315-p002 -->
# ws315-p002: 実装（RTL8156・8156B の vendor mode の driver）

Status: in-progress（2026-10-30 P3 q1694-i01: 初版 5534277a3、host 試験 PASS、amd64・5330 の build warning 0。5330 の実機の試験中）
Parent: [WS315](../ws.md)
Queue: q1694 / q1694-i01（P3）
依存: p001（[design.md](../design.md)、§12 の review の反映）

## 実装

| file | 内容 |
| --- | --- |
| `include/drivers/usb/usb-rtl815x.h`・`src/drivers/usb/usb-rtl815x-frame.c` | 純粋な部分: version → chip、記述子の世代（v1・v2）の形、RX の block の解析（FCS を除く、壊れた記述子で止める、checksum の bit）、TX の block の組み立て（揃え、入るだけ、checksum の指示と offset の上限）、PHYSTATUS の速さ、register の dword の shift と byte enable |
| `src/drivers/usb/usb-rtl815x.c` | driver: match 120（vendor の interface、endpoint 1・2・3、boot の token `usb.rtl815x=off` で 0）、register の読み書き（control 転送、OCP の窓）、attach（version、chip の初期化、MAC、自動交渉の広告、queue depth 4/4、URB の transfer reservation、`ue<n>`、worker の thread）、open（MAC の reset と enable、RX の集約の時間と大きさ、filter）、close、set_mtu（開いたまま RMS・EARLY_SIZE）、TX の pump（16 KB の block、checksum の offload）、RX の poll（48 KB の block を spare で出し直してから解析、frame ごとに packet へ copy して stack へ）、link の worker（interrupt の bit が変わった時に PHYSTATUS） |
| 組み込み | Makefile（`CONFIG_DRIVER_USB_RTL815X`、amd64 で NCM と同じ既定）、`platform/amd64/vmunix.mk`・`platform/pcat/vmunix.mk`、`src/kern/platform/pcat.c` の登録、`config/drivers/usb.drivers`・`menu.list`、`config/ci/` の amd64・intelmac（y）・pcat・rpi4・rpi4-server（n） |
| 試験 | `plan/ws315/tests/run-rtl815x-frame-test.sh`（host、-O2 と ASan・UBSan）、`plan/ws315/tests/config-5330-rtl.mk`、`plan/ws315/tests/5330-rtl.sh`（5330 の実機、1 本の script） |

- RTL8157 は attach で「未対応」と log して拒む（p004 で初期化を足す）。記述子 v2 の解析と組み立ては実装と host 試験済み。

## 確かめ

- host: `run-rtl815x-frame-test.sh` PASS（-O2、ASan・UBSan）。
- build: amd64（config/ci/config-amd64.mk）・5330（plan/ws315/tests/config-5330-rtl.mk）の vmunix の warning 0（5534277a3 の tree）。他の platform は未実施。
- 実機: 下の結果に。

## 結果

| run | kernel | 結果 |
| --- | --- | --- |
| k1（2026-10-30 04:03、`5330-rtl.sh build/ws315-5330/vmunix k1 30`、MODE=--nvme-kernel） | 5534277a3 の tree | FAIL: 5 分 SSH 無し、10.0.30.3 は ping も無し、NCM の時の MAC の host も無し。5330 は電源の入れ直し待ち（Q1 がユーザーに依頼）。原因は未確定 |

- k1 の後の直し（16122571a）: attach の待ち（autoload・PHY の状態・MAC の reset）を FreeBSD と同じく時間切れでも続ける（10 s、log）、attach・open の手順ごとの失敗の log、
  link を interrupt が告げない時に worker が 1 秒ごとに PHYSTATUS を読む（carrier が立たず DHCP が始まらない、を候補として）。
- 試験の script（7712411ab）: 前の起動の /var/log/kernel.log.old の driver の行を集める（届かなかった起動の原因を、次の起動で読む）。出力は build/ws315-hw/NAME。
- 次: 入れ直しの後、(1) 同じ kernel を `CFG_ADD=usb.rtl815x=off`（NCM）で起動し k1 の log を読む、(2) vendor mode で起動。
