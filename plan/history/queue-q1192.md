<!-- awesome-plan project=zedbsd record=q1192 -->

# q1192 / WS074 horizontal Grid second vertical absolute child border

Status: finished（2026-10-14 14:00–14:01 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p217有限部分scope](../ws074/phase217/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1192-i01 | [p217](../ws074/phase217/phase.md) partial | 固定水平LTR/RTL Gridのborder-only縦rl absolute2番目子、独立矩形/WPT009先頭2子/旧007/008/host/Acid | cleared。独立2矩形Chromium一致、水平2 callback先頭2子全検査PASS、009 wholeはFAILのまま、p217 whole未達 |

Outcome: WPT009水平2 callbackの初回失敗は4番目縦RTL子へ進んだ。縦2 callback whole PASS、旧WPT/host CSS51/JS14/Grid48、warning0 build、Acid3差0/Acid2差1865。[証拠](../ws074/phase217/evidence/20261014-q1192/README.md)。WPT/runner/ABI/Master無変更。

Resume: 水平Gridの4番目border-only縦RTL absolute子。p217/p189/p173全3,805件100%未達。共有Queue/Master/GitHub publication pending、pushなし。
