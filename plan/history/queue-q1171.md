<!-- awesome-plan project=zedbsd record=q1171 -->

# q1171 / WS074 opposite vertical writing-mode absolute self alignment

Status: finished（2026-10-14 12:52–12:56 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p217有限部分scope](../ws074/phase217/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1171-i01 | [p217](../ws074/phase217/phase.md) partial | align-self:self-start/end保持、従来Grid/Flex正規化、固定逆writing-mode縦Grid absolute static area、column010/011・旧001–011・host/Acid | cleared。p217 whole uncleared |

Outcome: column010/011 whole FAIL→PASS、column/row001–011の22件88 callbacks PASS。computed self値、host CSS51/CSSOM12/JS14/Grid48/Flex5、warning0 build、Acid3 score100/inventory100/差0、Acid2差1865。[証拠](../ws074/phase217/evidence/20261014-q1171/README.md)。WPT/runner/公開ABI/Master無変更。

Resume: positioned auto lines、RTL行軸017、baseline、RTL字形視覚。p189/p173全3,805件100%未達。共有Queue/Master/GitHub publication pending、pushなし。
