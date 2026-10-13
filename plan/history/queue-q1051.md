<!-- awesome-plan project=zedbsd record=q1051 -->

# q1051 / WS074 column flex auto height and min-height

Status: finished（2026-10-14 02:26–02:28 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p201の有限scope](../ws074/phase201/phase.md)に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1051-i01 | [p201](../ws074/phase201/phase.md) | nowrap縦Flexのheight:auto・pixel min-height・max-height:noneに限定し、内容必要高さとmin-heightの大きい方を主軸availableへ反映。固定vert001b/10件、host/CSS/build/Acid。wrap/percent/writing-mode、WPT/Master/公開ABI変更なし | cleared、p201 whole cleared |

Outcome: 固定vert001bがlocal pixel exact PASS、固定10件は8→9 PASS/1 FAIL/0 timeout。局所min-height2ケース、host12/12、CSS rule44/44・CSSOM12/12、build warning0、Acid3 score100/100・pixel差0、Acid2差1865。[証拠](../ws074/phase201/evidence/20261014-q1051/README.md)。

Remaining: wmvert-001 writing-mode、p196 script付きXHTML、p190 coverage、p173全3,805件100%。共有Queue/Master/GitHub publicationはQ1との競合回避でpending。pushなし。
