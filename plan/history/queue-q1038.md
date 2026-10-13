<!-- awesome-plan project=zedbsd record=q1038 -->

# q1038 / WS074 hidden-attribute static reftests

Status: finished（2026-10-14 01:32 JST）。Approval: この chat のユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を [p190の有限部分scope](../ws074/phase190/phase.md) に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1038-i01 | [p190](../ws074/phase190/phase.md) partial | 固定details focusのhidden directory reftest残り4件を測定/未対応分類し、q1037の21件と合わせた25件の状態を記録。browser/WPT/Master/公開ABI変更なし | cleared（partial）。p190 whole uncleared |

Outcome: hidden directory4件のうちstatic2件は0 local PASS/2 FAIL/0 error、scripted2件は未実行。[証拠](../ws074/phase190/evidence/20261014-q1038/README.md)。q1037と合わせdetails focus25 reftest中23件実測、12 local PASS/11 FAIL/2未実行。ID/固定commit/参照とdiff check PASS。コード無変更のためhost build/Acid再計測省略。

Remaining: scripted2件とcrashtest3件、p190全2,022件coverage、p173全3,805件100%。共有Queue/Master/GitHub publicationはQ1と競合回避のためpending。pushなし。
