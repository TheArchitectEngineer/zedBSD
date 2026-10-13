<!-- awesome-plan project=zedbsd record=ws182 -->

# WS182: 電源ボタンのメニュー（Log Out・Shut Down などを選ぶ）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」。全 Phase が cleared。旧: incomplete、p002 は T1-377 QEMU PASS で 5320・5330 の短押し・長押しの実機待ち）
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O1
Parent: [Master](../master.md)
Queue: なし
<!-- awesome-plan-current:end -->

## 目的

ユーザー（2026-10-07）「電源ボタンのハンドリングは、あとで実装でいいです。ログオフ、電源オフ、などのメニューを表示できるようにしたいです。独立WSにして、ベータ2の最後に実装しましょう。」
電源ボタンを押すと Keiland が Log Out・Shut Down などのメニュー（App Home の Power Off の dialog、ws099-p037）を出す。

## 結果（2026-10-13 完了）

- 設計（p001）: 電源ボタンの事象 → メニュー、greeter・lock の時はメニューを出さない（D1、2026-10-08 ユーザー「現状ではオーケーです」）、WS052 p012 との関係、1 回の押下で 2 つの Notify（押下と解放）の扱い。
- 実装（p002）: libkeiland-backend-zedbsd の `events_power_release`（押下の後 2000 ms 以内の次の record を解放として落とし `KL EVENTS power-button release gap_ms=N`）、compositor の `kwl_backend_power_button`（sleep の前後・greeter・lock・dialog の表示中は skip、それ以外でメニュー）。zedBSD と Linux の build warning 0、host 試験 PASS、QEMU は T1-377 PASS。
- AAT のシナリオ `tests/scenarios/os/power/power-button.md` をメニュー・解放の gap・lock の手順に更新した。
- 実機（5320・5330 の短押し・長押し）: 2026-10-13 ユーザーの指示（上の原文）で確認済みの扱い。

## 制限・移管

- 実機の `gap_ms` の値（短押しで数百、1.5 s の長押しで約 1500）の記録は無い。2 s の窓はそのまま。
- ws132-p008 の D1（短押しで S0i3）はこの WS で置き換わった。Sleep をメニューに足すのは WS052。
- lock の画面の大きな時計は [WS187](../ws187/ws.md)、PIN・Password・Hardware Key の選択は WS172 p007 へ（2026-10-08 ユーザーの同じ返事の追加の要望）。
- 規約の全文の見直しの Phase は無い（規約の整形はベータ3、2026-10-08 ユーザー）。

## Phase

| Phase | 内容 | 最終の状態 |
| --- | --- | --- |
| ws182-p001 | 設計: 電源ボタンの事象 → メニュー、greeter・lock の時、WS052 p012 との関係、押下と解放の 2 つの Notify | cleared（2026-10-08 Q1、D1 はユーザー「現状ではオーケーです」） |
| ws182-p002 | 実装と QEMU・実機（5320・5330）の確認 | cleared（2026-10-13 ユーザー、上の原文。T1-377 QEMU PASS） |

Phase の directory とこの WS だけの試験は、完了の規則（AGENTS.md）で削除する（git の履歴に残る、削除は Q1）。
