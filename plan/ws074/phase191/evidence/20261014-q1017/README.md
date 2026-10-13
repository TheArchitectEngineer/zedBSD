# q1017-i01: text-decoration-line の値とcomputed style

2026-10-14 00:16–00:19 JST。ユーザーの有限Queue事前承認を [p191の部分範囲](../../phase.md) に適用。固定WPT [text-decoration-line-computed.html](https://github.com/web-platform-tests/wpt/blob/2d66b9b7998bb58c336138c178323ddee857b586/css/css-text-decor/parsing/text-decoration-line-computed.html) の18 subtestを対象。CSS Text Decoration Level 4の[値文法とcomputed値](https://drafts.csswg.org/css-text-decor-4/#text-decoration-line-property)に沿い、`none`、4つのline keywordの重複なし組合せ、`spelling-error`、`grammar-error`を解析し、computed styleを規定順で直列化した。従来のunderline描画フラグは保持し、他のline描画は対象外。

[局所fixture](text-decoration-line.html) の[出力](host-output.txt)はruleの逆順指定→`underline overline`、重複値の拒否、親`line-through`・子`none`（non-inherited）、inlineの逆順指定→`underline overline blink`を確認。`sh plan/ws074/tests/host-build.sh plain` warning/error0、host-cssom12/12、host-css-rule-model44/44、diff check PASS。`clang-format` は環境にないためC全文とbrowser component全文を手動確認。

対象WPTは **18/18 subtest PASS、test PASS**。固定 textdecoration全16 IDの再計測は[report.json](report.json) と `test-01.json`–`test-16.json`: **5 PASS/11 FAIL**、subtest **79 PASS/100 FAIL**。q1016の4 PASS/12 FAIL、47/132から改善。`text-decoration-line-valid.html` は複合値のCSSOM inline直列化などで残るFAILを含む。未実行をPASSとしていない。

[Acid回帰](acid-summary.json): Acid3 score100/100・inventory100/100・RGB差0/480000、Acid2差1865/120000で前回と同じ。WPT本体、公開ABI、`master.md` は無変更。p191全16件PASS、p173全3,805件PASS 100%は未達。
