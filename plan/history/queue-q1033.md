<!-- awesome-plan project=zedbsd record=q1033 -->

# q1033 / WS074 selected static details reftest validation

Status: finished（2026-10-14 01:20 JST）。Approval: この chat のユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を [p190の有限部分scope](../ws074/phase190/phase.md) に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1033-i01 | [p190](../ws074/phase190/phase.md) partial | 固定選定の静的details reftest最大10件実測、参照/画素差監査。browser/WPT/Master変更なし | cleared（partial）。p190 whole uncleared |

Outcome: 既存runnerは4 provisional PASS/6 FAIL/0 error。PASS1件はreference chainを辿らないfalse passと判明。[証拠](../ws074/phase190/evidence/20261014-q1033/README.md)。コード無変更のためhost build/Acid再計測省略。

Remaining: 参照グラフ判定、static全1,561候補、scripted/variant reftest、crashtest全48、p173全3,805件100%。共有Queue/Master/GitHub publicationはQ1と競合回避のためpending。pushなし。
