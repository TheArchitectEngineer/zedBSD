<!-- awesome-plan project=zedbsd record=q1195 -->

# q1195 / WS074 horizontal Grid vertical absolute child margin and frame

Status: finished（2026-10-14 14:05–14:06 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p217有限部分scope](../ws074/phase217/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1195-i01 | [p217](../ws074/phase217/phase.md) partial | 固定水平LTR/RTL Gridのmargin/border/padding複合縦absolute3配置、独立6矩形/WPT011 whole/旧007–010/host/Acid | cleared。独立6矩形Chromium一致、WPT011 whole PASS/4 callback PASS、p217 whole未達 |

Outcome: WPT011は4 callbackすべてPASS。旧WPT/host CSS51/JS14/Grid48、warning0 build、Acid3差0/Acid2差1865。[証拠](../ws074/phase217/evidence/20261014-q1195/README.md)。WPT/runner/ABI/Master無変更。

Resume: non-static012の親Grid frameとauto inset。p217/p189/p173全3,805件100%未達。共有Queue/Master/GitHub publication pending、pushなし。
