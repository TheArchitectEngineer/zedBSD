<!-- awesome-plan project=zedbsd record=q1156 -->

# q1156 / WS074 non-Grid vertical-rl diagnosis

Status: finished（2026-10-14 12:08–12:09 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p220有限部分scope](../ws074/phase220/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1156-i01 | [p220](../ws074/phase220/phase.md) partial | 非Grid vertical-rl単一/2行文字のChromium/browser DOM、layout/paint、CPU/GPU比較 | cleared。p220 whole uncleared |

Outcome: Chromiumは右始まり縦進行、browserは左始まり横進行。browser CPU/GPUはbyte同一の横描画。[証拠](../ws074/phase220/evidence/20261014-q1156/README.md)。読取と証拠ページのみ、browser/runner/WPT/ABI/Master/Acid無変更。

Resume: p220 bounded vertical-rl文字geometry/paint、続いてp225 Grid物理行反転。p189/p173全3,805件100%未達。共有Queue/Master/GitHub publication pending、pushなし。
