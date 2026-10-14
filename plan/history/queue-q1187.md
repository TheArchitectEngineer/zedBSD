<!-- awesome-plan project=zedbsd record=q1187 -->

# q1187 / WS074 vertical-rl Grid third horizontal RTL absolute area

Status: finished（2026-10-14 13:50–13:52 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p217有限部分scope](../ws074/phase217/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1187-i01 | [p217](../ws074/phase217/phase.md) partial | 固定縦rl Grid直接横RTL absolute子、row2/col1 top inset/justify-start、独立最小ページ/WPT007縦rl先頭3子/旧WPT/host/Acid | cleared。独立矩形Chromium一致、縦rl callback whole PASS、WPT file wholeはFAILのまま、p217 whole未達 |

Outcome: WPT007水平2 callbackと縦rl callback whole PASS、縦lr callbackは4番目子でFAIL。旧WPT/host CSS51/JS14/Grid48、warning0 build、Acid3差0/Acid2差1865。[証拠](../ws074/phase217/evidence/20261014-q1187/README.md)。WPT/runner/ABI/Master無変更。

Resume: 縦lr Gridの4番目横RTL absolute子。p189/p173全3,805件100%未達。共有Queue/Master/GitHub publication pending、pushなし。
