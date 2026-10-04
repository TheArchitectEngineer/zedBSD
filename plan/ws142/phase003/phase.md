<!-- awesome-plan project=zedbsd record=ws142-p003 -->

# ws142-p003: タッチパッドの gesture

Status: cleared（2026-10-05 Q1: T1-132 で p003-guest を 2 回とも PASS（速さの計算の直し 3e5b9f20 の後）、wiseview-pad.png を Q1 が目視（pad の gesture で開いた Wiseview））。以前: in-progress（2026-10-05 P1 generation17。実装・build・host の試験まで。QEMU は Q1 経由で T1 に依頼。結果の判定と実機の UAT まで cleared にしない）
Disposition: normal
Parent: [WS142](../ws.md)
Queue: Q1 の指示（2026-10-05、D1・D3・D10 決定済み）

## 範囲と受け入れ

- 下の端から 2 本指の上 → Wiseview（BOTTOM2）、どこからでも 3 本指の上 → Wiseview（UP3、D3）、左の端から右・右の端から左の 2 本指 → 左・右の仮想デスクトップ（LEFT2・RIGHT2）。指に付いて動く（D10）。
- 3 本指の tap は TAP3（切り替え、p005 で UI）。中 button は 3 本指で pad を押す click（D1）。
- fullscreen の間は compositor の gesture を動かさない（D6 の決定: 「fullscreen はコンポジタが動作しません」）。
- build warning 0（zedBSD・Linux）、host の試験、境界の検査、QEMU（T1、touchinject の pad）、実機（UAT）。

## 実装（2026-10-05）

| 所 | 内容 |
| --- | --- |
| `userland/desktop/wayland/touchpad.h`・`.c` | action `ZWL_TOUCHPAD_GESTURE`（`gesture` BOTTOM2・UP3・LEFT2・RIGHT2・TAP3、`phase` BEGIN・UPDATE・END・CANCEL、`travel_um`（道に沿って内向き・上が正）、`speed`（µm/s、平滑））。`zwl_touchpad_set_size`（absinfo の最大）。端の帯 6 mm。2 本指が両方とも同じ端の帯に触れていれば、重心の 4 mm で決める: 内向きで主の軸が副の 2 倍以上なら gesture、それ以外は scroll（決めるまでの移動は止めておき、scroll になったらその分を scroll する）。端に触れない 2 本指は今まで通りすぐ scroll。3 本指は 8 mm 上（縦が主）で UP3、それ以外の 3 本指は何もしない（pointer も動かさない）。指は同じ report に揃わないので、本数が変わったら決め直す（動きが 3 mm 未満なら端も取り直す）。gesture 中に指が増える・pad を押す・device が消える → CANCEL、指が減る・全部離れる → END。gesture の後に残った指は何もしない。3 本指の tap → TAP3（中 button は押し込み） |
| `input.c` | `zwl_touchpad_set_size(x.maximum, y.maximum)`、log に `size=X,Y`。gesture の action を `zwl_glass_gesture` へ（client には渡さない） |
| `shell.c`・`zwl.h` | `zwl_glass_gesture`: log `ZWL GESTURE kind=… phase=… travel_um=… speed=…`。始めてよいのは greeter・lock・fullscreen（`bar_cover`、D6）・Wiseview・デスクトップの swipe・App Home のどれも無い時。BOTTOM2・UP3: `wiseview_gesture` と新しい `wiseview_pad`、開き具合 = 移動 / 40 mm、END で 0.35 超か 100 mm/s 以上なら開く、それ以外と CANCEL は閉じる。LEFT2・RIGHT2: `desktop_dragging` と新しい `desktop_pad`、`desktop_offset` = 移動 × 幅 / 60 mm（隣が無い側は 1/4）、END で幅の半分以上か 100 mm/s 以上で隣へ（`desktop_turn`、via=pad）、それ以外は戻る。TAP3 は log だけ（p005） |

## 確認

| 確認 | 結果 |
| --- | --- |
| zedBSD の compositor（`make BUILD=build/q713 build/q713/bin/wayland`） | 成功、warning 0 |
| Linux の Keiland（`keiland-linux.mk all`） | 成功、warning 0 |
| `sh plan/ws142/tests/run-host-gesture.sh`（5330 の大きさ、ASan・UBSan でも） | 45 checks ok: BOTTOM2（BEGIN 1・UPDATE・END、20 mm、travel は増えるだけ、250 mm/s は flick、25 mm/s は違う、scroll・click なし）、指が別の report に来ても BOTTOM2、真ん中・端に沿う・片方だけ端・外向きは gesture なし（scroll）、LEFT2・RIGHT2（30 mm 内向き）、UP3（15 mm、pointer は動かない）、3 本指の横は何も出ない、TAP3（button なし）、3 本指の押し込みは中 button、端での 2 本指 tap は右 button、指が増える・押す・release_all で CANCEL、指が減って END（残りの指は動かさない）、大きさ不明なら端なし |
| `sh plan/ws159/tests/run-host-touchpad.sh` | 25 checks ok（今までの規則は変わらない） |
| style-check（変えた file） | 指摘 0（ついでに shell.c の q722 の空行・input.c の既存の 1 件を直した） |
| `plan/tools/keiland-os-boundary/check.sh` | PASS |
| QEMU（`plan/ws142/tests/p003-guest.sh`） | **未実施**。T1 に依頼（Q1 経由） |
| 実機（5330） | **未実施**（UAT） |

## QEMU の試験（T1 への依頼）

- image: pen の image（`plan/ws079/tests/build-pen-image.sh BUILD`、main の最新）。`plan/ws079/tests/pen-guest.sh start IMAGE`。
- 試験: `plan/ws142/tests/p003-guest.sh BUILD [OUTDIR]`（BUILD の bin/wayland を写す）。
- 合格: 全行 ok（bottom2-begin・end・follows・opens・esc-closes、short-begin・cancel、right2-begin・desktop、left2-begin・desktop・no-neighbour、up3-begin・opens・esc-closes、tap3、scroll-no-gesture、fullscreen-bar-hidden・gesture-logged・no-wiseview、alive、no-error）。`wiseview-pad.png` に Wiseview が写る（ユーザーに見せる）。

## 実機で見ること（UAT に足す）

- 端の帯 6 mm・決める 4 mm／8 mm・開き 40 mm・デスクトップ 60 mm・flick 100 mm/s は仮の値。5330 で、下の端から 2 本指を上げると Wiseview が指に付いて開くか、左右の端からデスクトップが滑るか、普通の 2 本指の scroll が gesture に化けないかを見て調整する。
- ws159-p005 の 2.10（3 本指の tap）は中 click から TAP3 に変わる（切り替えの UI は p005 まで log だけ）。

- 2026-10-05 T1-126: 1 回目 FAIL、2 回目 PASS。log では 8 mm をゆっくり動かした短い BOTTOM2 の END の速さが `418068`（約 418 mm/s。本当は約 11 mm/s）で、flick と見て Wiseview が開いた。開いたままなので、続く right2・left2 は始まらず（Wiseview が出ている間は始めない）、fullscreen の段の数もずれた（連鎖）。他の速い gesture の END にも `2340150`・`3131932` があった。**原因は実装**: 速さを report ごとの瞬間の速さの平均で出していて、compositor が report をまとめて読む（間隔 1 ms 未満を 1 ms とみなす）と瞬間の速さが極端になる。直し（`touchpad.c`）: 最近の 16 個の report の travel と時刻を持ち、速さは「100 ms 以上前の最新の report から今まで」の travel ÷ 時間（最短 50 ms）で出す。END は指を離した時刻で量るので、止まってから離すと遅い。host の試験に、ゆっくりの指の report が 180 ms ごとに 3 個ずつまとまって届く場合（flick ではなく約 33 mm/s）と、速く動かして 200 ms 止まってから離す場合（report の有無とも flick ではない）を足した（50 checks ok、ASan・UBSan でも）。試験は、間違って開いた時に Esc で閉じて後の段を独立させた（`plan/ws142/tests/p003-guest.sh`）。再試験は T1。

## 残り

- QEMU の結果の判定、実機の UAT。
- TAP3 と 2 本指の左右（切り替えの中での移動）は p005。

## Q1 の判定（2026-10-05）

T1-132 で p003-guest を 2 回とも PASS（速さの計算の直し 3e5b9f20 の後）、wiseview-pad.png を Q1 が目視（pad の gesture で開いた Wiseview）。**cleared**。
