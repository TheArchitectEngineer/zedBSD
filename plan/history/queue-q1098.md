<!-- awesome-plan project=zedbsd record=q1098 -->

# q1098 / WS074 horizontal text-line Grid baseline

Status: finished（2026-10-14 08:09–08:11 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p219有限部分scope](../ws074/phase219/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1098-i01 | [p219](../ws074/phase219/phase.md) partial | 固定pixel行の水平Grid直接inline text先頭line baselineをempty合成baseline groupへ追加し、最小2行、固定style-changes005/006、旧PASS、host/Acid回帰を確認 | cleared。p219 whole uncleared |

Outcome: 固定005/006は各3/6→6/6 subtest・whole FAIL→PASS。旧001/002・100 subtest PASS維持。warning0 build、Grid48/CSS51/JS14、Acid3差0/Acid2差1865。[証拠](../ws074/phase219/evidence/20261014-q1098/README.md)。

Resume: style-changes8件を同条件で再測定し、残る縦書き4件と他baseline条件を分ける。p219/p217/p189/p173全件100%未達。共有Queue/Master/GitHub publicationはQ1との競合を避けpending projection。pushなし。
