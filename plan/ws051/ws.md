<!-- awesome-plan project=zedbsd record=ws051 -->

# WS051: USB-C の DisplayPort Alternate Mode

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: p001〜p005a cleared、p004c は 2026-10-13 ユーザーの一括の cleared。2026-10-08 ユーザー「USB-C DPでディスプレイ出力ができました！」「拡張モードで出力されました。」「ミラーもうまくいきました。」。残りの p005b と p006 の対象の旧 file は WS245 p039・p040・p052 で xd に書き直された）
Completed: 2026-10-27（q1608 の精査）
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

USB-C の port につないだ DisplayPort の display（USB-C の monitor、USB-C から DP・HDMI への変換）を i915 が DisplayPort Alternate Mode で駆動でき、抜き差しを Vulkan の Display の拡張の経路で Keiland に知らせ、Keiland の指示で画面を出す（mirror・拡張・出力 off は Keiland が決める）。i915 は GOP の出力先以外に自分の判断で scanout を始めない（Guardrail「GPU の driver の scanout の規則」）。設計は [design.md](design.md)（review は [2026-10-04](design-review-2026-10-04.md)・[2026-10-07](design-review-2026-10-07.md)）。

## ユーザーの決定

- 2026-09-24: 「USB-C DisplayPort Alternative Modeの実装。」
- 2026-10-04: 「次の新規実装項目は、USB-C の DisplayPort Alternate Modeの実現を目標にします。その次が電源管理です。これらは併走できると思います。共通のpredecessorがAMLですね。」
- 2026-10-04（WS050 §10 の決定 5、Q1 経由）: HPD・pin は i915 の TCSS・FIA から、UCSI 2.0 以上で取れる時は UCSI からも。WS051 は UCSI を待たずに進める。向きは取れれば表示し受け入れの条件にしない（2026-10-05 の §13 (2)）。
- 2026-10-04（WS051 §10、Q1 経由）: TBT-alt は範囲外。driver は GOP の出力先を引き継ぎ自分で出力先を変えない（外部 display の優先はやめる）。USB-C の DP の接続は Keiland が Vulkan の Display の拡張で通知を受け、mirror・拡張・出力 off を決める。→ Guardrail「GPU の driver の scanout の規則」。
- 2026-10-08: 「USB-C DPでディスプレイ出力ができました！」「拡張モードで出力されました。」「ミラーもうまくいきました。」。2026-10-08 夜: 「WS050・WS051・WS090・WS183 は実機で動作を確認」。規約の全文はベータ3（WS の外の一括の見直しへ）。
- 2026-10-13: 「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」
- 2026-10-27（q1608）: 「…masterとWSを修正して、completedやcanceledにしてください。」

## 結果

- i915 の TCSS・Type-C PHY（DKL）・TC PLL・DDI・FIA の手順（p001〜p003）、TC の port の核・AUX の power domain・HPD の配送（p002b）、TC の AUX・DPCD・EDID と外部 DP の object（p004a）、display の UAPI での出力（link training・modeset・scanout、p004b）、GOP が USB-C の時の引き継ぎ（p004c）、抜き差しの検出と `GPU_DISPLAY_EVENT_CHANGE`（p005a）。
- 実機（5330、TC2）: ユーザーが USB-C DP の出力・拡張・ミラーを確認（2026-10-08）。5330 の VFIO の ktest（T1-334・T1-350・T1-354）。
- これらの旧 file（tc.c・dp-ext など）は後に WS245（p039・p040・p052）で `xd` に書き直され、5330 の回帰が PASS（WS245 completed 2026-10-27）。

## 制限・移管

- p005b（scanout 中の抜けの停止・IRQ_HPD の retrain）は実装・host・build まで、実機は未実施。対象の code は WS245 の xd に置き換わった。
- S0ix の口（M10）は WS052（Sleep、保留）と一緒。
- TC1 と抜き差しの繰り返しは未確認（p004a・p005a の記録）。eDP の off の付け替えの失敗は BUG-266。
- 規約の全文（p006）は WS の外の一括の見直しへ（対象の旧 file は WS245 で置き換え）。
- 5330 の USB-C DP の Linux の実測（`m3-5330-20261007/`）は WS245 の design が oracle として残すと決めたので、この directory に残す。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws051-p001 | 調査と設計 | cleared（2026-10-08 Q1） |
| ws051-p002 | TC PLL の enable の番地、VBT の DVO の code、host の試験 | cleared（2026-10-07 Q1、T1-334） |
| ws051-p002b | TC の port の核、TC の AUX の power domain、HPD の配送 | cleared（2026-10-07、T1-350） |
| ws051-p003 | DKL PHY と TC PLL、DDI の TC の clock ほか | cleared（2026-10-07、T1-354） |
| ws051-p004a | TC の AUX・DPCD・EDID の診断、外部 DP の object | cleared（2026-10-08 Q1、TC1 は未確認） |
| ws051-p004b | display の UAPI での出力 | cleared（2026-10-08 Q1、BUG-266 は別） |
| ws051-p004c | GOP が USB-C の時の引き継ぎ | cleared（2026-10-13 ユーザー「UATの結果待ちのものは…」） |
| ws051-p005a | 抜き差しの検出と事象 | cleared（2026-10-08 Q1、繰り返しと TC1 は未） |
| ws051-p005b | scanout 中の抜けの停止、IRQ_HPD の retrain、S0ix の口 | in-progress のまま WS を閉じた（実装・host・build まで、実機は未実施。code は WS245 で xd に置き換え） |
| ws051-p006 | 規約の全文の確認と最終の確認 | planned のまま WS を閉じた（WS の外の一括の見直しへ） |

## 記録の所在

Phase の計画・結果・試験は削除する。git の commit `fb1f6afee` の `plan/ws051/` にある。Queue の履歴は [plan/history](../history/index.md)。
