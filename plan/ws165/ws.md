<!-- awesome-plan project=zedbsd record=ws165 -->

# WS165: 手書きの入力

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」。根拠: p001〜p003・p005 cleared（段 1・段 2）、2026-10-06 夜 ユーザー H5「いったんacceptして、追加のフェーズを第2段でやりましょう。」、p004 の実機の UAT は 2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」）
<!-- retained-after-completion --> phase001 の phase.md は残す: userland/desktop/wayland/hand-cloud.h:10、userland/packages/fonts/hand-hershey/Makefile:6・convert.py:2 が設計の §3・§4（ユーザーの決定 H3）を参照している。source の comment を直す（または仕様を docs/ へ移す）までの間
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

手書きの入力。goal の設定が難しいので段階化する（段 1: 1 文字ずつの英数字・かな・記号）。

## ユーザーの決定

- 2026-10-05「手書き入力（ゴール設定が難しいので段階化する）」
- 2026-10-05 夜: 設計の H1〜H4 をユーザーが決定（H1: 段 1 は 1 文字ずつ、数字・英字・ひらがな・カタカナ・記号（約 250 字）、書き順に依らない、top-1 ≥ 90%・top-4 ≥ 98%。H3: 手本の字形は Hershey の font、p001 §4）。
- 2026-10-06 夜 H5「いったんacceptして、追加のフェーズを第2段でやりましょう。」（p002 を top-1 88%・top-4 98.3% で受け入れ、第 2 段は p005）
- 2026-10-08 ユーザー: 規約の全文の Phase はベータ3 → p004 の規約は WS の外の一括の見直しへ。
- 2026-10-13「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」

## 結果

- 手本の data（`userland/packages/fonts/hand-hershey`、228 字）と点群の照合、compositor の `kwl_hand_recognize`、ink の記録、スクリーンキーボードの手書きの key が main にある。
- 証拠: host の認識率（top-1 90.6%、p005）、QEMU の T1-280（p003）と T1-318 (c) の osk-guest PASS（p005）。実機は 2026-10-13 のユーザーの一括の cleared（個別の観察の記録は無い）。

## 制限・移管

- 連続の筆記・漢字は範囲の外（新しい WS で）。認識の頑健さの追加は WS177 p009。
- 規約の全文（p004 の後半）は WS の外の一括の見直しへ。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws165-p001 | 要件と段の設計（段 1、点群の照合） | cleared（H1〜H5） |
| ws165-p002 | 手本の data と点群の照合、host 試験 | cleared（H5 で受け入れ） |
| ws165-p003 | compositor の `kwl_hand_recognize`、ink の記録、T1 | cleared（T1-280） |
| ws165-p004 | 実機の UAT、規約の全文（directory なし） | planned のまま WS を閉じた（UAT は 2026-10-13 の一括で OK、規約の全文は WS の外の一括の見直しへ） |
| ws165-p005 | 第 2 段: 区別できない字の組を分けて認識率を上げる | cleared（T1-318 (c)） |

## 記録の所在

phase001 は上の理由で残す。他の Phase の記録と試験は削除する（Q1）。git の commit `fb1f6afee` の `plan/ws165/` にある。Queue の履歴は [plan/history](../history/index.md)。
