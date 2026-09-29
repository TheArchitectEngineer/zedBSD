<!-- awesome-plan project=zedbsd record=ws092 -->

# WS092: text editor（simple）

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG006
Related Milestones: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: p003 in-progress（2026-09-29）: 設計（[phase003](phase003/phase.md)）・libkeiland の chooser の実装・editor の結線・host 試験（75/75）まで済み、build 通過。次は QEMU の desktop での確認 → p004（規約・回帰）。
引き継ぎの詳細（editor 側の受け口 `te_host.choose`・`TE_EVENT_CHOSEN`、p003 で決めること、試験と build の手順）は [p002](phase002/phase.md) の「引き継ぎ」
<!-- awesome-plan-current:end -->

## 目標（2026-09-29 ユーザー）

「テキストエディタの追加。（シンプルなものでいいです）」

- UTF-8 の plain text の開く・編集・保存（別名で保存）、undo・redo、選択・copy・paste（Wayland の clipboard と PRIMARY）、検索、行番号（任意）、
  慣性の scroll（WS090）、日本語の入力（WS095 の IME）。Files からの起動（WS093）。

## Phase

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| [ws092-p001](phase001/phase.md) | 設計（[design.md](design.md)） | cleared（2026-09-29） | — |
| [ws092-p002](phase002/phase.md) | 実装: 文書・undo・file・表示の行・編集・検索・clipboard と PRIMARY・touch・menu と titlebar、`/bin/textedit FILE`、App Home と image への登録（file chooser を除く） | cleared（2026-09-29。host 34/34、QEMU の desktop で開く・編集・保存・検索・dialog・context menu・App Home） | p001 |
| [ws092-p003](phase003/phase.md) | file chooser を共有の library（libkeiland の `keiland_file_chooser_*`）に作り、editor の Open・Save As につなぐ（2026-09-29 ユーザー「テキストエディタのファイルピッカーは、KeiのUIライブラリに入れるのがいいと思いました。」） | in-progress | p002 |
| ws092-p004 | 全文の規約と回帰 | planning | p003 |
