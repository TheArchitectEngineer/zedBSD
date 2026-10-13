<!-- awesome-plan project=zedbsd record=q1048 -->

# q1048 / WS074 block formatting context margin collapse

Status: finished（2026-10-14 02:12–02:14 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p198の有限部分scope](../ws074/phase198/phase.md)に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1048-i01 | [p198](../ws074/phase198/phase.md) partial | float BFC先頭/末尾子margin相殺の最小修正、通常flow維持、固定10件とhost/Acid回帰。WPT/Master/公開ABI変更なし | cleared（partial）。p198 whole uncleared |

Outcome: CSS2.2のBFC親/子margin非相殺を適用。geometry4/4、WPT test/reference再layout14/14、host build warning0、Acid3差0/Acid2差1865。[証拠](../ws074/phase198/evidence/20261014-q1048/README.md)。固定10件は3 local PASS/7 FAILで数不変。vert001a差22,000→5,600、vert006差27,820→5,560。vert001bは共通参照の修正と残るtest側min-height問題で差10,240→22,520。先行PASSの喪失なし。

Remaining: 7描画FAILのうちphysical left/rightとmin-height等の別原因、p196 script付きXHTML、p190 coverage、p173全3,805件100%。共有Queue/Master/GitHub publicationはQ1と競合回避でpending。pushなし。
