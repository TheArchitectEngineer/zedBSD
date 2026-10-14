<!-- awesome-plan project=zedbsd record=q1157 -->

# q1157 / WS074 bounded non-Grid vertical-rl text

Status: finished（2026-10-14 12:10–12:13 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p220有限部分scope](../ws074/phase220/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1157-i01 | [p220](../ws074/phase220/phase.md) partial | 非Grid明示px content boxのvertical-rl/LTR単純ASCIIの右始まり行・縦paint、矩形/hit/dump/F/CPU-GPU/旧WPT/host/Acid | cleared。p220 whole uncleared |

Outcome: 単一/2行DOM矩形が右始まり、F字形は縦左参照から8px移動画素完全一致。CPU/GPU同一。旧Grid WPT18 PASS、縦右Grid対照FAIL不変、host CSS51/JS14/Grid48、warning0 build、Acid3 score100/inventory100/差0、Acid2差1865。[証拠](../ws074/phase220/evidence/20261014-q1157/README.md)。WPT/runner/ABI/Master無変更。

Resume: p225 fixed/auto Grid vertical-rl物理行反転、p220他orientation/複雑inline。p189/p173全3,805件100%未達。共有Queue/Master/GitHub publication pending、pushなし。
