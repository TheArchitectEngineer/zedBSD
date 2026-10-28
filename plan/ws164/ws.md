<!-- awesome-plan project=zedbsd record=ws164 -->

# WS164: OS の起動時の Welcome の画面

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」。根拠: p001・p002 cleared（T1-269）、UAT は 2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」）
<!-- retained-after-completion --> phase001 の phase.md は残す: userland/desktop/settings/welcome.c:9 の comment が設計を参照している。source の comment を直す（または仕様を docs/ へ移す）までの間
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

最初の起動（または毎回）の Welcome の画面。既存の Start で足りるかの検討から始める。

## ユーザーの決定

- 2026-10-05「OS起動時のWelcome画面（すでにあるStartでもいいかも。要検討）」
- 2026-10-05 夜: 設計の H1〜H3 をユーザーが決定、H4 は Q1 が調整として承認（p001）。
- 2026-10-08 ユーザー: 規約の全文の Phase はベータ3 → p003 は WS の外の一括の見直しへ。
- 2026-10-13「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」

## 結果

- Settings の welcome の mode（`userland/desktop/settings/welcome.c`）、設定の key、compositor からの起動が main にある。
- 証拠: QEMU の T1-269（p002）、host の試験。実機は 2026-10-13 のユーザーの一括の cleared（個別の観察の記録は無い）。

## 制限・移管

- 規約の全文（p003）は WS の外の一括の見直しへ。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws164-p001 | 要件と設計 | cleared（H1〜H4） |
| ws164-p002 | 設定の key・compositor の起動・Settings の welcome の mode・host 試験・T1 | cleared（T1-269） |
| ws164-p003 | 規約の全文の見直し（directory なし） | 規約の全文は WS の外の一括の見直しへ |

## 記録の所在

phase001 は上の理由で残す。他の Phase の記録は削除する（Q1）。git の commit `fb1f6afee` の `plan/ws164/` にある。WS158 が使う host 試験 `run-host-welcome.sh` は plan/tools/ へ移す。Queue の履歴は [plan/history](../history/index.md)。
