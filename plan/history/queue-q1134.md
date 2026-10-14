<!-- awesome-plan project=zedbsd record=q1134 -->

# q1134 / WS074 fixed Grid502 recensus

Status: finished（2026-10-14 10:18–10:24 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p189有限部分scope](../ws074/phase189/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1134-i01 | [p189](../ws074/phase189/phase.md) partial | 固定Grid502全再測定、q1105 whole/subtest遷移・旧PASS後退・残FAIL分類、ID/hash/checkpoint/raw検証 | cleared。p189 whole uncleared |

Outcome: 110→124 whole PASS、392→378 FAIL。16 FAIL→PASS、旧PASS→FAIL 2（empty-grid、行track animation）。subtest398改善/505後退。502 ID/hash/checkpoint/gzip raw一致。[証拠](../ws074/phase189/evidence/20261014-q1134/README.md)。

Resume: 旧PASS後退を最優先で調査。残alignment192、grid-definition42、parsing31、grid-items28等。p189/p173全件100%未達。共有Queue/Master/GitHub publicationはQ1との競合を避けpending projection。pushなし。
