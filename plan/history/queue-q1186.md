<!-- awesome-plan project=zedbsd record=q1186 -->

# q1186 / WS074 vertical Grid second horizontal absolute area

Status: finished（2026-10-14 13:47–13:49 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p217有限部分scope](../ws074/phase217/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1186-i01 | [p217](../ws074/phase217/phase.md) partial | 固定縦lr/rl Grid直接横LTR absolute子、row1/col2右insetのstatic area/justify-center、独立lr/rl/WPT007先頭2子/旧WPT/host/Acid | cleared。独立2矩形Chromium一致、両縦callback先頭2子全検査PASS、WPT file wholeはFAILのまま、p217 whole未達 |

Outcome: WPT007縦lr/rl callbacks初回失敗は3番目子へ進んだ。水平2 callback PASS、旧WPT/host CSS51/JS14/Grid48、warning0 build、Acid3差0/Acid2差1865。[証拠](../ws074/phase217/evidence/20261014-q1186/README.md)。WPT/runner/ABI/Master無変更。

Resume: 縦Gridの3番目absolute子、次に4番目子/RTL方向。p189/p173全3,805件100%未達。共有Queue/Master/GitHub publication pending、pushなし。
