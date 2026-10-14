<!-- awesome-plan project=zedbsd record=q1161 -->

# q1161 / WS074 positioned Grid alignment diagnosis

Status: finished（2026-10-14 12:23–12:24 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p217有限部分scope](../ws074/phase217/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1161-i01 | [p217](../ws074/phase217/phase.md) partial | 固定positioned列軸/行軸34件のfirst callbackとsource・absolute text/static area分離 | cleared。p217 whole uncleared |

Outcome: 全34件whole FAIL、33件で水平2/4 PASS・縦初回width期待10/実60、行軸017は水平RTL offsetLeft差を含む。[証拠](../ws074/phase217/evidence/20261014-q1161/README.md)。読取のみ、browser/runner/WPT/ABI/Master/Acid無変更。

Resume: bounded absolute縦文字geometry、次に縦Grid static area、別途RTL行軸017。p189/p173全3,805件100%未達。共有Queue/Master/GitHub publication pending、pushなし。
