<!-- awesome-plan project=zedbsd record=ws190 -->

# WS190: 文字の欄と Text Editor の指での選択（ダブルタップ・端の drag・コピーの popup）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: p001〜p004 は各 phase.md で cleared（T1-443・T1-445・T1-449 PASS、p004 の規約は 2026-10-09）。ws.md の表が古かった）
Completed: 2026-10-27（q1608 の精査）
Primary Milestone: MG006
Related Milestones: なし
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

libkeiland の 1 行の欄（kl_field）と複数行の欄（text area）、Text Editor で、ダブルタップで語を選んで選択の mode に入り、開始端・終了端の handle を drag で決め、指を離すとコピー・切り取り・貼り付け・すべて選択の popup が出る。

## ユーザーの決定

- 2026-10-08（由来）「1行および複数行のテキストフィールドコンポーネントでは、テキストをダブルタップすると、選択モードになり、開始端、終了端をドラッグ操作で決定でき、指を離すと、コピーと切り取りのボタンがポップアップするといいなあ。別実装になるけど、テキストエディタも。今実装しているドラッグ処理のあとに実装しよう。」
- 2026-10-08（クリックの回答）popup のボタンはコピー・切り取り・貼り付け・すべて選択。

## 結果

- libkeiland の欄に kl_text_touch を組み込み、popup の部品（kl_text_bar）を足した。Text Editor に同じ操作と popup を足した（main）。
- QEMU: T1-443 1〜5 PASS（6 は OSK の無い image で not-run）、T1-445 1〜6 PASS、T1-449 の速い drag PASS。規約の見直し（p004）は build と host 試験 PASS。

## 制限・移管

- T1-443 の 6（OSK との重なり）は not-run。実機の確認は未実施。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws190-p001 | 設計（kl_field・text area への kl_text_touch、popup の部品、Text Editor の popup） | cleared（2026-10-08） |
| ws190-p002 | libkeiland の欄 | cleared（2026-10-08、T1-443・T1-449） |
| ws190-p003 | Text Editor | cleared（2026-10-08、T1-445） |
| ws190-p004 | 規約の全文の見直し | cleared（2026-10-09） |

## 記録の所在

Phase の計画・設計・結果・試験は削除した。git の commit `fb1f6afee` の `plan/ws190/` にある。Queue の履歴は [plan/history](../history/index.md)。
