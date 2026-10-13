<!-- awesome-plan project=zedbsd record=q1015 -->

# q1015 / WS074 textdecoration 全16 testharness baseline

Status: finished（2026-10-14 00:08〜00:11 JST）
Approval: この chat のユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」（00:07 JST）を、[WS074の有限scope](../ws074/queue.md) に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1015-i01 | [p189](../ws074/phase189/phase.md) partial | textdecoration公式16 testharness IDを全測定し、結果と共通失敗原因を記録。結果を取得できない時だけrunner最小修正 | cleared（partial）。p189 whole uncleared |

Prerequisites: q1012固定manifest、q1013/q1014 callback runner確認済み。

Outcome: 16/16 IDにcompletionあり、4 PASS/12 FAIL、subtest47 PASS/132 FAIL。infra error0。`CSS` global欠落とCSSOM text-decoration longhand/shorthand不足が共通原因候補。[証拠](../ws074/phase189/evidence/20261014-q1015/README.md)。WPT/browser/Master source無変更。Acid3再測定省略。通常sandboxではlocalhost bind不可のため承認済み elevated 実行。

Remaining: textdecoration領域12 FAIL、p189の残るtestharness、p190 reftest/crashtest、p173全3,805 baseline/100%。共有Queue/Master/GitHub publicationはQ1と競合回避のためpending。pushなし。
