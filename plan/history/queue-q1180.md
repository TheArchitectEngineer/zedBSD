<!-- awesome-plan project=zedbsd record=q1180 -->

# q1180 / WS074 horizontal Grid vertical-lr absolute text geometry

Status: finished（2026-10-14 13:27–13:31 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p220有限部分scope](../ws074/phase220/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1180-i01 | [p220](../ws074/phase220/phase.md) partial | 水平固定2×2 Grid直接縦左absolute子、明示area左insetのrow-track inline room、最小ページ/固定non-static007先頭子/旧WPT/host/Acid | cleared。最小矩形Chromium一致、固定先頭子全検査PASS、WPT wholeはFAILのまま、p220 whole未達 |

Outcome: callback初回失敗はwidth10→60から2番目子width30→70へ進んだ。旧WPT/host CSS51/JS14/Grid48、warning0 build、Acid3差0/Acid2差1865。[証拠](../ws074/phase220/evidence/20261014-q1180/README.md)。WPT/runner/ABI/Master無変更。

Resume: 水平Grid直下のvertical-rl absolute子geometry、次に縦Grid横子static area。p189/p173全3,805件100%未達。共有Queue/Master/GitHub publication pending、pushなし。

Time correction (2026-10-14 13:32 JST): Earlier minute labels ran ahead of the wall clock. The corrected end minute is verified by local commit `6d6cc8023 13:31:51` JST; the start minute follows the recorded Queue order. Approval, scope and outcomes did not change.
