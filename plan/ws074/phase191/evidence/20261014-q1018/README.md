# q1018-i01: text-decoration-line inline CSSOM の規定順

2026-10-14 00:20–00:25 JST。ユーザーの有限Queue事前承認を[p191の部分範囲](../../phase.md)に適用。固定WPT [text-decoration-line-valid.html](https://github.com/web-platform-tests/wpt/blob/2d66b9b7998bb58c336138c178323ddee857b586/css/css-text-decor/parsing/text-decoration-line-valid.html)の逆順複合値49件が、inline CSSOMから指定順のまま返る原因を修正した。既存CSS tokenizer/`text-decoration-line` parserの値を用い、[CSS Text Decorationのcanonical順](https://drafts.csswg.org/css-text-decor-4/#text-decoration-line-property)へ直列化する共通処理を追加。computed styleも同じ処理を使用。CSS-wide値と他のproperty、不正な重複値の挙動は維持。

[局所fixture](inline-canonical.html)の[出力](host-output.txt)は直接property、`setProperty`、`cssText`の各設定経路でcanonical化し、`inherit`と`display`の保持、重複値の無視を確認。`sh plan/ws074/tests/host-build.sh plain` warning/error0、host-cssom12/12、host-css-rule-model44/44、diff check PASS。`clang-format`は環境にないためC全文とbrowser component全文を手動確認。

対象WPTは **67/67 subtest PASS、test PASS**。最終sourceで固定textdecoration全16 IDを再計測した[report.json](report.json)と`test-01.json`–`test-16.json`は **6 PASS/10 FAIL**、subtest **128 PASS/51 FAIL**。q1017の5 PASS/11 FAIL、79/100から改善。未実行やFAILはPASSに算入しない。

[Acid回帰](acid-summary.json): Acid3 score100/100・inventory100/100・RGB差0/480000、Acid2差1865/120000で前回と同じ。WPT本体、公開ABI、`master.md` は無変更。p191全16件PASSとp173全3,805件PASS 100%は未達。
