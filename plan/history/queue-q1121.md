<!-- awesome-plan project=zedbsd record=q1121 -->

# q1121 / WS074 vertical inline text geometry diagnosis

Status: finished（2026-10-14 09:29–09:31 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p220有限部分scope](../ws074/phase220/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1121-i01 | [p220](../ws074/phase220/phase.md) partial | 固定縦Grid初回幅、非Grid/位置指定Grid最小例、inline/paint/geometry経路診断 | cleared。p220 whole uncleared |

Outcome: 縦書きWPT10 subtestで期待幅10px/実60px。非Grid縦書き絶対子も36×10pxで横書き行のまま。縦軸表現はinline/paint/bounds/hit/dumpに未接続。[証拠](../ws074/phase220/evidence/20261014-q1121/README.md)。

Resume: 非Grid vertical-lr/単一非折返しrunを有限実装し、縦paintとgeometryを揃える。後続でvertical-rl、Grid軸・abs配置。p220/p217/p189/p173全件100%未達。共有Queue/Master/GitHub publicationはQ1との競合を避けpending projection。pushなし。
