# q1019-i01: text-decoration-style / text-decoration-skip-ink CSSOM

2026-10-14 00:26–00:28 JST。ユーザーの有限Queue事前承認を[p191の部分範囲](../../phase.md)に適用。[CSS Text Decoration](https://drafts.csswg.org/css-text-decor-4/)と固定WPTの値に合わせ、`text-decoration-style` の5 keyword（初期`solid`、非継承）と`text-decoration-skip-ink` の3 keyword（初期`auto`、継承）をCSS値parser、cascade、inline/computed CSSOMに追加。wavy/double描画と実際のink skippingはこの部分scope外で、視覚的な全面準拠は主張しない。

[局所fixture](decoration-longhands.html)の[出力](host-output.txt)は親のstyle `wavy`/子`solid`、親子skip-ink `all`、初期`solid`/`auto`、不正値2件の拒否を確認。`sh plan/ws074/tests/host-build.sh plain` warning/error0、host-cssom12/12、host-css-rule-model44/44、diff check PASS。`clang-format` は環境にないためC全文とbrowser component全文を手動確認。

対象4 WPT（style valid/computed各5、skip-ink valid/computed各3 subtest）は**16/16 subtest、4/4 test PASS**。invalid値の既存2 WPTもPASS維持。固定textdecoration全16 IDの[report.json](report.json)と`test-01.json`–`test-16.json`は **10 PASS/6 FAIL**、subtest **144 PASS/35 FAIL**。q1018の6 PASS/10 FAIL、128/51から改善。未実行やFAILをPASSに算入しない。

[Acid回帰](acid-summary.json)はAcid3 score100/100、inventory100/100、RGB差0/480000、Acid2差1865/120000。WPT本体、公開ABI、`master.md`は無変更。p191全16件PASSとp173全3,805件PASS 100%は未達。
