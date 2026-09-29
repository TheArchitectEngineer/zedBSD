<!-- awesome-plan project=zedbsd record=ws093 -->

# WS093: Files から app の起動（file の種類と app の対応）

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG006
Related Milestones: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: main の依頼（worktree `.claude/worktrees/ws093-open`、branch `wt/ws093`）
Resume point: p001・p002 cleared（画像・text・HTML の既定、double click・Enter・double tap を QEMU で確認）。次は p003（Always Open With）
<!-- awesome-plan-current:end -->

## 目標（2026-09-29 ユーザー）

「ファイラーからアプリの起動（画像、テキストをダブルクリック）」

- Files で file を double click（touch では double tap）すると、種類（拡張子・内容）に応じた app（画像 → WS091、text → WS092、PDF → PDF Viewer、
  HTML → ブラウザ）が、その file を開いて起動する。対応の表（system の既定と利用者の上書き）、「このアプリで開く」の menu。WS071（完了）は変えず、
  この WS で Files に足す。

## Phase

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| [ws093-p001](phase001/phase.md) | 設計（[design.md](design.md)） | cleared（2026-09-29） | — |
| [ws093-p002](phase002/phase.md) | 既定の対応: 画像（png・jpeg・gif）→ Image Viewer、text → Text Editor、HTML → Browser（`files/apps.c` の組み込みの表）。host の試験、Venus の guest の double click・Enter・注入の touch の double tap、画面 | cleared（2026-09-29） | p001 |
| ws093-p003 | 利用者の上書きを画面から: 「Always Open With」の submenu と利用者の一覧（`~/.config/keiland/open-with`）への書き込み・「Use System Default」 | planned | p002 |
| ws093-p004 | 全文の規約と回帰（Files の回帰の該当、boot test） | planned | p003 |
