<!-- awesome-plan project=zedbsd record=q1196 -->

# q1196 / WS074 framed Grid auto-inset diagnosis

Status: finished（2026-10-14 14:07–14:08 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p217有限部分scope](../ws074/phase217/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1196-i01 | [p217](../ws074/phase217/phase.md) partial | 固定012親/子frame・auto insetの水平/縦Grid先頭子独立矩形、raw、2 layout経路 | cleared。水平親は縦text経路と静的X、縦親はstatic Y/Xが不一致 |

Outcome: 4親独立最小ページをChromium比較。固定012 rawは水平幅26→76、縦offsetTop75→3/offsetLeft173→11。[証拠](../ws074/phase217/evidence/20261014-q1196/README.md)。読取のみ、browser/runner/WPT/ABI/Master無変更。

Resume: 水平親のframed/all-auto縦absolute先頭子を独立修正、次に縦親static area。p217/p189/p173全3,805件100%未達。共有Queue/Master/GitHub publication pending、pushなし。
