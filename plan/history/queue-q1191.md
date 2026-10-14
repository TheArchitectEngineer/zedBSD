<!-- awesome-plan project=zedbsd record=q1191 -->

# q1191 / WS074 horizontal Grid vertical absolute child border

Status: finished（2026-10-14 13:58–13:59 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p217有限部分scope](../ws074/phase217/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1191-i01 | [p217](../ws074/phase217/phase.md) partial | 固定水平LTR/RTL Gridのborder-only縦lr absolute先頭子、独立矩形/WPT009先頭子/旧007/008/host/Acid | cleared。独立2矩形Chromium一致、水平2 callback先頭子全検査PASS、009 wholeはFAILのまま、p217 whole未達 |

Outcome: WPT009水平2 callbackの初回失敗は2番目縦rl子へ進んだ。縦2 callback whole PASS、旧WPT/host CSS51/JS14/Grid48、warning0 build、Acid3差0/Acid2差1865。[証拠](../ws074/phase217/evidence/20261014-q1191/README.md)。WPT/runner/ABI/Master無変更。

Resume: 水平Gridの2番目border-only縦rl absolute子。p217/p189/p173全3,805件100%未達。共有Queue/Master/GitHub publication pending、pushなし。
