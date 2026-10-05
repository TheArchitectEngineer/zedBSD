<!-- awesome-plan project=zedbsd record=ws099-p033 -->

# ws099-p033: 最大化の中で新しく開く窓を最大化で開く

Status: in-progress（実装済み・T1 の試験待ち）
Disposition: normal
Parent: [WS099](../ws.md)
Queue: q756（Q1、2026-10-05、P2 g15）

## 範囲

2026-10-05 午後の UAT（ユーザー「ウィンドウを最大化している状態で、新たにアプリを起動したら、そのアプリは最大化しているのがいいです。タブレットをスクリーン全体で使っているという認識にします。」）。

## 決めたこと

- 「最大化の状態」は、今の desktop の**前面の窓**（sheet ならその親）が docked（最大化）で全画面でないこと。他の窓の状態は見ない。
- 前面の窓を元に戻した後に開く窓は、普通に浮いて開く（前面が浮いているので）。
- 除く: 親のある窓（dialog・sheet）、大きさの固定の窓（最小と最大の大きさが同じ）、全画面で始める窓、popup、login・lock の画面、glass でない look。
- 最大化で開いた窓の戻り先は docked の空間の 7/10 の大きさで中央。

## 実装（2026-10-05）

- `shell.c` に `zwl_glass_open_docked`: 上の条件なら、最初の configure の前に `maximized`・docked の位置と大きさ・戻り先を決める（log `ZWL GLASS open-docked surface= front= x= y= w= h=`）。
- `protocol.c`: toplevel の最初の空の commit で configure を送る前に呼ぶ。
- `display.c` の `place_window`: 最大化で開いた窓は浮いた窓の配置をしない。

## 検証（2026-10-05）

- build: zedBSD の `bin/wayland` warning 0、Linux の Keiland warning 0。style-check 変えた file 0。
- guest の試験 `plan/ws099/tests/p033-guest.sh` を作った（T1）。
- 未実施: QEMU（T1）、実機（UAT）。
