<!-- awesome-plan project=zedbsd record=q1167 -->

# q1167 / WS074 fixed vertical Grid absolute self alignment

Status: finished（2026-10-14 12:41 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p217有限部分scope](../ws074/phase217/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1167-i01 | [p217](../ws074/phase217/phase.md) partial | 同writing-mode固定2×2縦Grid absolute子のself-start/end、固定row-axis008/009・旧positioned001–009・host/Acid | cleared。p217 whole uncleared |

Outcome: 固定row-axis008/009 whole FAIL→PASS、column/row-axis001–009の18件72 callbacks PASS。host CSS51/JS14/Grid48、warning0 build、Acid3 score100/inventory100/差0、Acid2差1865。[証拠](../ws074/phase217/evidence/20261014-q1167/README.md)。WPT/runner/ABI/Master無変更。

Resume: 逆writing-mode子010/011、auto lines、RTL行軸017、baseline。p189/p173全3,805件100%未達。共有Queue/Master/GitHub publication pending、pushなし。
