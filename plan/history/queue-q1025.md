<!-- awesome-plan project=zedbsd record=q1025 -->

# q1025 / WS074 Window unique-id named lookup

Status: finished（2026-10-14 00:50〜00:53 JST）。Approval: この chat のユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を [p192の有限部分scope](../ws074/phase192/phase.md) に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1025-i01 | [p192](../ws074/phase192/phase.md) partial | 一意なHTML idのWindow named live lookup、own property優先、変更/削除、host/WPT/Acid回帰。重複/name/iframeは後続 | cleared（partial）。p192 whole uncleared |

Outcome: 局所fixtureのbare/window参照、id変更・削除、既存own propertyを確認。host build warning0、native property65/65、realm30/30、代表WPT14/14 PASS、Acid3 score100/100・画素差0、Acid2差1865不変。[証拠](../ws074/phase192/evidence/20261014-q1025/README.md)。2対象WPTの先頭ReferenceErrorは消えたが、別の未実装でPASS未達。

Remaining: p192重複/name/iframe等、p189全1,783件、p190全2,022件、p173全3,805件100%。共有Queue/Master/GitHub publicationはQ1と競合回避のためpending。pushなし。
