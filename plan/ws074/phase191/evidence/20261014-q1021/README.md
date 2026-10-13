# q1021-i01: text-decoration shorthand の全component解析

2026-10-14 00:32–00:35 JST。ユーザーの有限Queue事前承認を[p191の部分範囲](../../phase.md)に適用。`text-decoration` shorthandは従来先頭1 tokenだけをlineとして解析し、残りを無視していた。固定WPTの[shorthand文法](https://drafts.csswg.org/css-text-decor-4/#text-decoration-property)に沿ってline/style/color/thicknessの各componentを既存CSS tokenizerで確認し、重複style/color/thicknessとline群の分断、不正な複合値を拒否する。有効値は各longhand宣言へ展開し、未指定longhandは初期値にする。CSS色とlengthの字句範囲は現行browserの既存parser能力に従う。

[局所fixture](shorthand-grammar.html)の[出力](host-output.txt)は有効値の受理、不正な重複・line分断の拒否、computed line/style/colorへの展開を確認。thicknessのinline getterはこのQueueでは未実装で空のまま。`sh plan/ws074/tests/host-build.sh plain` warning/error0、host-cssom12/12、host-css-rule-model44/44、diff check PASS。`clang-format`は環境になくC全文とbrowser component全文を手動確認。

固定WPT `text-decoration-invalid.html` は**3/3 subtest PASS**（q1020は2/3）。固定textdecoration全16 IDの[report.json](report.json)と`test-01.json`–`test-16.json`は **13 PASS/3 FAIL**、subtest **153 PASS/26 FAIL**。q1020の12 PASS/4 FAIL、150/29から改善。残りはvalidのcanonical inline値、longhand getter、computed shorthandの3件。

[Acid回帰](acid-summary.json): Acid3 score100/100・inventory100/100・RGB差0/480000、Acid2差1865/120000。WPT本体、公開ABI、`master.md`は無変更。p191全16件PASSとp173全3,805件PASS 100%は未達。
