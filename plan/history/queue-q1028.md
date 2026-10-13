<!-- awesome-plan project=zedbsd record=q1028 -->

# q1028 / WS074 same-document fragment navigation

Status: finished（2026-10-14 01:02〜01:09 JST）。Approval: この chat のユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を [p193の有限部分scope](../ws074/phase193/phase.md) に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1028-i01 | [p193](../ws074/phase193/phase.md) partial | Location.hash同一文書URL更新、fragment対象探索、details/hidden祖先reveal・beforematch・scroll、fixture/固定WPT/host/Acid回帰 | cleared（partial）。p193 whole uncleared |

Outcome: URL・同期beforematch・reveal中断を局所fixtureで確認。固定beforematch WPTはno outputからcompletion付きFAILに進行。次の一般的な欠落は`Element.attributes`。host build warning0、native property65/65、realm30/30、Acid3 score100/100・画素差0、Acid2差1865不変。[証拠](../ws074/phase193/evidence/20261014-q1028/README.md)。

Remaining: p193固定対象PASS、p192 named-property残件、p189全1,783件、p190全2,022件、p173全3,805件100%。共有Queue/Master/GitHub publicationはQ1と競合回避のためpending。pushなし。
