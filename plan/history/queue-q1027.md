<!-- awesome-plan project=zedbsd record=q1027 -->

# q1027 / WS074 HTMLElement.hidden reflection

Status: finished（2026-10-14 00:57〜01:00 JST）。Approval: この chat のユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を [p193の有限部分scope](../ws074/phase193/phase.md) に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1027-i01 | [p193](../ws074/phase193/phase.md) partial | hidden missing/normal/until-foundのIDL反映、fixture/固定WPT/host/Acid回帰。ancestor revealingは後続 | cleared（partial）。p193 whole uncleared |

Outcome: 固定hidden-idl17/17とhidden-until-found-idl1/1 subtest PASS。host build warning0、native property65/65、Acid3 score100/100・画素差0、Acid2差1865不変。[証拠](../ws074/phase193/evidence/20261014-q1027/README.md)。固定2025 WPTが期待する`undefined`属性除去は2026 living Standardのotherwise設定と異なるため明示して採用。beforematch WPTはfragment navigation未完でno output。

Remaining: p193 ancestor revealing等、p192 named-property残件、p189全1,783件、p190全2,022件、p173全3,805件100%。共有Queue/Master/GitHub publicationはQ1と競合回避のためpending。pushなし。
