# q1020-i01: text-decoration-color CSSOM

2026-10-14 00:29–00:31 JST。ユーザーの有限Queue事前承認を[p191の部分範囲](../../phase.md)に適用。[CSS Text Decorationの色longhand](https://drafts.csswg.org/css-text-decor-4/#text-decoration-color-property)に沿い、CSS色parserとinline CSSOMを接続し、非継承の初期`currentcolor`をstyle modelに保持した。明示的な`inherit`で親の`currentcolor`を受けても、computed CSSOMでは子自身の文字色で解決する。underlineの実描画色はこの部分scopeの対象外。

[局所fixture](decoration-color.html)の[出力](host-output.txt)は親red、子初期green、子の明示inheritでもgreen、rgbaのalpha、不正値拒否を確認。`sh plan/ws074/tests/host-build.sh plain` warning/error0、host-cssom12/12、host-css-rule-model44/44、diff check PASS。`clang-format`は環境になく、C全文とbrowser component全文を手動確認。

対象2 WPT（color valid/computed各3 subtest）は **6/6 subtest、2/2 test PASS**。invalid値の既存WPTもPASS維持。固定textdecoration全16 IDの[report.json](report.json)と`test-01.json`–`test-16.json`は **12 PASS/4 FAIL**、subtest **150 PASS/29 FAIL**。q1019の10 PASS/6 FAIL、144/35から改善。残る4件はすべて`text-decoration` shorthand関係。

[Acid回帰](acid-summary.json): Acid3 score100/100・inventory100/100・RGB差0/480000、Acid2差1865/120000。WPT本体、公開ABI、`master.md`は無変更。p191全16件PASSとp173全3,805件PASS 100%は未達。
