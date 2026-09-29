<!-- awesome-plan project=zedbsd record=ws093 -->

# WS093: Files から app の起動（file の種類と app の対応）

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG006
Related Milestones: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: p001（設計）から
<!-- awesome-plan-current:end -->

## 目標（2026-09-29 ユーザー）

「ファイラーからアプリの起動（画像、テキストをダブルクリック）」

- Files で file を double click（touch では double tap）すると、種類（拡張子・内容）に応じた app（画像 → WS091、text → WS092、PDF → PDF Viewer、
  HTML → ブラウザ）が、その file を開いて起動する。対応の表（system の既定と利用者の上書き）、「このアプリで開く」の menu。WS071（完了）は変えず、
  この WS で Files に足す。

## Phase（案）

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| ws093-p001 | 設計 | planning | — |
