<!-- awesome-plan project=zedbsd record=ws207 -->

# WS207: Raspberry Pi 4 のオーディオ（再生と録音）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 ユーザーの回答（クリック）「completed」。実機の確認は未実施の物がある。同じ日の P3 の精査 q1608 の「incomplete・保留（Pi 4 の完了の範囲の外）」をこの回答が置き換えた）
Completed: 2026-10-27（ユーザーの回答）
Primary Milestone: MG006
Related Milestones: —
Objectives: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

Raspberry Pi 4 の音の出力（HDMI の音と 3.5 mm の PWM の出力）と録音（USB の audio class の入力など）を audiod から使えるようにする。

## ユーザーの決定

- 2026-10-11（由来）「RPi4のオーディオを実装。（再生、録音）」
- 2026-10-11「推し通りです。」: F1 3.5 mm の jack を先に、F3 HDMI の音は後の Phase。F2（録音の USB のマイク・headset の機種）は未回答。
- 2026-10-12「…そのあと、RPi4のWiFiとBluetoothを実装し、RPi4は完了です。」（オーディオは Pi 4 の完了の範囲に入っていない）
- 2026-10-27（クリック）「completed」

## 結果

- BCM2711 の DMA と PWM の音（3.5 mm の jack）を実装し、host の試験（ring）と build まで（p002、main）。
- Pi 4 の実機で 3.5 mm の音を確かめた記録は無い（未確認）。Pi 4 の音の不具合の直しは [BUG-318](../bugs/BUG-318.md)（resolved）。

## 制限・移管

- 録音（USB の audio class、p003）と HDMI の音（VCHIQ、p004）は実装していない。要る時は新しい WS。
- 実機の UAT と規約の全文（p005）は未実施。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws207-p001 | 調査と設計 | in-progress のまま WS を閉じた（第 1 版、F2 は未回答） |
| ws207-p002 | BCM2711 の DMA と PWM の音（3.5 mm の jack） | in-progress のまま WS を閉じた（実装・host・build 済み、実機は未確認） |
| ws207-p003 | USB の audio class の録音 | 未着手のまま WS を閉じた |
| ws207-p004 | HDMI の音（VCHIQ） | 未着手のまま WS を閉じた |
| ws207-p005 | 実機の UAT と規約の全文 | 未着手のまま WS を閉じた |

## 記録の所在

Phase の計画・結果・試験は削除した。git の commit `fb1f6afee` の `plan/ws207/` にある。Queue の履歴は [plan/history](../history/index.md)。
