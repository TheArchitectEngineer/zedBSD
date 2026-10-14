<!-- awesome-plan project=zedbsd record=q1153 -->

# q1153 / WS074 vertical Grid item border and padding

Status: finished（2026-10-14 11:58–12:01 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p225有限部分scope](../ws074/phase225/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1153-i01 | [p225](../ws074/phase225/phase.md) partial | 2×2縦auto Gridの非負px border/paddingのtrack内容寄与・子内容高、固定011/012/015/016・旧PASS/host/Acid | cleared。p225 whole uncleared |

Outcome: 固定011/012/015/016 whole FAIL→PASS、旧Grid14件PASS。独立border/paddingページがChromium 153と各4矩形一致。warning0 build、host CSS51/JS14/Grid48、Acid3 score100/inventory100/差0、Acid2差1865。[証拠](../ws074/phase225/evidence/20261014-q1153/README.md)。WPT/runner/ABI/Master無変更。

Resume: 固定Grid502全件recensus、p225他Grid/runner。p173全3,805件100%未達。共有Queue/Master/GitHub publication pending、pushなし。
