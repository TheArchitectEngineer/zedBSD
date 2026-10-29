<!-- awesome-plan project=zedbsd record=ws315-p003 -->
# ws315-p003: 5330 の実機（RTL8156 の vendor mode）

Status: in-progress（2026-10-30 04:16 P3 q1694-i01: k1 FAIL（SSH 無し）、5330 の電源の入れ直し待ち）
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
- 次: 入れ直しの後、(1) 同じ kernel を `CFG_ADD=usb.rtl815x=off`（NCM）で起動し k1 の log を読む、(2) vendor mode で起動。
