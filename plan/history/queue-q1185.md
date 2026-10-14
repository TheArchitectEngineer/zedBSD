<!-- awesome-plan project=zedbsd record=q1185 -->

# q1185 / WS074 vertical Grid horizontal absolute static area

Status: finished（2026-10-14 13:45–13:46 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p217有限部分scope](../ws074/phase217/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1185-i01 | [p217](../ws074/phase217/phase.md) partial | 固定縦lr/rl Grid直接横LTR absolute子、row1/col1左insetのstatic area/justify-right、独立lr/rl/WPT007先頭子/旧WPT/host/Acid | cleared。独立2矩形Chromium一致、両縦callback先頭子全検査PASS、WPT file wholeはFAILのまま、p217 whole未達 |

Outcome: WPT007縦lr/rl callbacks初回失敗は2番目子へ進んだ。水平2 callback PASS、旧WPT/host CSS51/JS14/Grid48、warning0 build、Acid3差0/Acid2差1865。[証拠](../ws074/phase217/evidence/20261014-q1185/README.md)。WPT/runner/ABI/Master無変更。

Resume: 縦Gridの2番目横absolute子、次に他area/RTL子。p189/p173全3,805件100%未達。共有Queue/Master/GitHub publication pending、pushなし。
