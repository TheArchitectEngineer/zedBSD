<!-- awesome-plan project=zedbsd record=q1184 -->

# q1184 / WS074 horizontal RTL Grid second-row vertical-rl text geometry

Status: finished（2026-10-14 13:43–13:44 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p220有限部分scope](../ws074/phase220/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1184-i01 | [p220](../ws074/phase220/phase.md) partial | 水平RTL固定2×2 Grid直接vertical-rl/RTL absolute子row2/col2、左上insetのrow-track inline room/static area、独立ページ/WPT007 RTL callback/旧WPT/host/Acid | cleared。独立矩形Chromium一致、固定RTL callback whole PASS、WPT file wholeはFAILのまま、p220 whole未達 |

Outcome: 固定non-static007水平LTR/RTL両callback PASS、縦2 callback FAIL。旧WPT/host CSS51/JS14/Grid48、warning0 build、Acid3差0/Acid2差1865。[証拠](../ws074/phase220/evidence/20261014-q1184/README.md)。WPT/runner/ABI/Master無変更。

Resume: 縦Grid直下の横absolute子static area、次に同系列frame variation。p189/p173全3,805件100%未達。共有Queue/Master/GitHub publication pending、pushなし。
