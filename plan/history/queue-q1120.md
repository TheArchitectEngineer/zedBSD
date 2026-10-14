<!-- awesome-plan project=zedbsd record=q1120 -->

# q1120 / WS074 inline-grid intrinsic width

Status: finished（2026-10-14 09:21–09:28 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p217有限部分scope](../ws074/phase217/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1120-i01 | [p217](../ws074/phase217/phase.md) partial | `inline-grid` distinct display/atomic intrinsic幅、Grid維持、固定5件/旧PASS/Acid検証 | cleared。p217 whole uncleared |

Outcome: 最小auto250px/explicit400px/block Grid1008px、CSSOMとRTL絶対位置PASS。固定列軸012–016で水平RTL5 subtest FAIL→PASS、水平10/10 PASS、縦書き10 FAIL継続。旧whole PASS2件とAcid3差0維持。[証拠](../ws074/phase217/evidence/20261014-q1120/README.md)。

Resume: p220縦書きレイアウトとGrid軸差を有限scopeで扱う。p217/p189/p173全件100%未達。共有Queue/Master/GitHub publicationはQ1との競合を避けpending projection。pushなし。
