<!-- awesome-plan project=zedbsd record=q1117 -->

# q1117 / WS074 positioned Grid explicit-row content shift

Status: finished（2026-10-14 09:07–09:12 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p217有限部分scope](../ws074/phase217/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1117-i01 | [p217](../ws074/phase217/phase.md) partial | 水平Grid固定高さ/align-content:centerの明示行原点とin-flow配置、固定WPT5件と回帰 | cleared。p217 whole uncleared |

Outcome: 最小auto/明示2行とin-flow PASS。固定列軸012/015の最初のoffsetTop差が次のoffsetLeft差へ進むがwhole5件FAIL。旧PASS2件、Grid48/CSS51/JS14、warning0 build、Acid3画素差0、Acid2差1865。[証拠](../ws074/phase217/evidence/20261014-q1117/README.md)。

Resume: auto row境界・RTL containing-block xと[p220](../ws074/phase220/phase.md)縦書き。p217/p189/p173全件100%未達。共有Queue/Master/GitHub publicationはQ1との競合を避けpending projection。pushなし。
