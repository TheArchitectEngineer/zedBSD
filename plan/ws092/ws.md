<!-- awesome-plan project=zedbsd record=ws092 -->

# WS092: text editor（simple）

<!-- awesome-plan-current:start -->
Status: completed（2026-09-29、QEMU の Venus。実機は未実施）
Primary Milestone: MG006
Related Milestones: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: 完了。実機（5330 の touch・慣性・clipboard）の確認はユーザー
<!-- awesome-plan-current:end -->

## 目標（2026-09-29 ユーザー）

「テキストエディタの追加。（シンプルなものでいいです）」

## 結果

- `/bin/textedit [FILE]`（App Home の Text Editor）: UTF-8 の plain text の開く・編集・保存・別名で保存、undo・redo、選択・copy・paste
  （Wayland の clipboard と中 click の PRIMARY）、検索、行番号、menu と titlebar、context menu、touch（tap で caret、double tap で語の選択、
  1 本指の drag は慣性の scroll、long press で context menu）。
- 共有の file chooser（libkeiland の `keiland_file_chooser_*`、KEILAND_VERSION 12、2026-09-29 ユーザー「テキストエディタのファイルピッカーは、
  KeiのUIライブラリに入れるのがいいと思いました。」）: 別の xdg_toplevel の窓、Open・Save（上書きの確認）、sidebar・filter・隠し file・path の入力・touch。
- Files からの起動は WS093（double click で textedit が開く）。設計は [design.md](design.md)（WS090 が chooser を部品の library に移すときの参照）。
- 確認（QEMU の Venus）: host 75/75（chooser）・34/34（editor の核）、desktop で Open・Save As・置き換え・取り消し、注入の touch、PRIMARY、
  Terminal との clipboard の往復、Files から開いて保存。規約（style-check）0、build warning 0、boot test PASS。

## 制限・移管

- 日本語の入力は WS095（IME）。
- touch の drag での選択（long press の後の drag・つまみ）は作っていない（design J10）。
- chooser の WS090 への移動、複数選択、新しい folder、chooser の key の repeat は、使う app が来たときに足す。
- libkeiland は xdg_wm_base の binding を process に 1 つ持ち続ける（BUG-112 の回避）。BUG-112 は ws035-p132 で直ったので、外すかは後の判断。
- 試験は `plan/tools/keiland/host-chooser.sh`・`plan/tools/textedit/host-core.sh`・`plan/tools/textedit/qmp-keys.py`（master の Tools 節）。

## Phase

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws092-p001 | 設計 | cleared（2026-09-29） |
| ws092-p002 | 実装（file chooser を除く） | cleared（host 34/34、QEMU の desktop） |
| ws092-p003 | 共有の file chooser（libkeiland）と Open・Save As | cleared（host 75/75、QEMU） |
| ws092-p004 | 全文の規約と回帰 | cleared（style-check 0、boot test PASS） |
| ws092-p005 | 受け入れの残り（touch・PRIMARY・clipboard・Files）と試験の plan/tools への移動 | cleared（QEMU で全て PASS） |

Phase の記録は git の履歴にある（2026-09-29 に削除）。
