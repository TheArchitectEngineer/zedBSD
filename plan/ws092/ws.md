<!-- awesome-plan project=zedbsd record=ws092 -->

# WS092: text editor（simple）

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG006
Related Milestones: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: p001（設計、[design.md](design.md)）cleared（2026-09-29）。次は p002（実装）
<!-- awesome-plan-current:end -->

## 目標（2026-09-29 ユーザー）

「テキストエディタの追加。（シンプルなものでいいです）」

- UTF-8 の plain text の開く・編集・保存（別名で保存）、undo・redo、選択・copy・paste（Wayland の clipboard と PRIMARY）、検索、行番号（任意）、
  慣性の scroll（WS090）、日本語の入力（WS095 の IME）。Files からの起動（WS093）。

## Phase

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| [ws092-p001](phase001/phase.md) | 設計（[design.md](design.md)） | cleared（2026-09-29） | — |
| ws092-p002 | 実装: 文書・undo・file・表示の行・編集・検索・clipboard と PRIMARY・touch・menu と titlebar、`/bin/textedit FILE`、App Home と image への登録 | planning | p001 |
| ws092-p003 | 全文の規約と回帰 | planning | p002 |
