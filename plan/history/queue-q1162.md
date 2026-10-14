<!-- awesome-plan project=zedbsd record=q1162 -->

# q1162 / WS074 absolute vertical text in fixed Grid

Status: finished（2026-10-14 12:24–12:28 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p220有限部分scope](../ws074/phase220/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1162-i01 | [p220](../ws074/phase220/phase.md) partial | 固定px縦Grid直接absolute ASCII子の縦inline/shrink-to-fit、寸法/paint/hit、代表WPT/旧PASS/host/Acid | cleared。p220 whole uncleared |

Outcome: 最小lr/rl両子10×36px、代表WPT縦子幅10・高さ60を通過しoffsetTop/offsetLeft差へ進む。3 whole FAILはstatic area残差。旧Grid34 PASS、host CSS51/JS14/Grid48、warning0 build、Acid3 score100/inventory100/差0、Acid2差1865。[証拠](../ws074/phase220/evidence/20261014-q1162/README.md)。Grid static area/WPT/runner/ABI/Master無変更。

Resume: p217縦Grid static areaと別途水平RTL行軸017。p189/p173全3,805件100%未達。共有Queue/Master/GitHub publication pending、pushなし。
