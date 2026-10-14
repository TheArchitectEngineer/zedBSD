<!-- awesome-plan project=zedbsd record=q1152 -->

# q1152 / WS074 vertical Grid item margins

Status: finished（2026-10-14 11:53–11:57 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p225有限部分scope](../ws074/phase225/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1152-i01 | [p225](../ws074/phase225/phase.md) partial | 2×2縦auto Gridの非負px marginのtrack内容寄与・子サイズ・配置、固定010/014・旧PASS/host/Acid | cleared。p225 whole uncleared |

Outcome: 固定010/014 whole FAIL→PASS、旧Grid12件PASS。通常余白とtop/left追加の独立ページがChromium 153と全box一致。warning0 build、host CSS51/JS14/Grid48、Acid3 score100/inventory100/差0、Acid2差1865。[証拠](../ws074/phase225/evidence/20261014-q1152/README.md)。border011/015・padding012/016は従来経路で初回差不変。WPT/runner/ABI/Master無変更。

Resume: p225 border011/015・padding012/016、固定Grid502全件recensus、他Grid/runner。p173全3,805件100%未達。共有Queue/Master/GitHub publication pending、pushなし。
