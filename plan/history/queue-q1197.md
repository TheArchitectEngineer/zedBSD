<!-- awesome-plan project=zedbsd record=q1197 -->

# q1197 / WS074 framed horizontal Grid first vertical auto-inset child

Status: finished（2026-10-14 14:08–14:13 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p217有限部分scope](../ws074/phase217/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1197-i01 | [p217](../ws074/phase217/phase.md) partial | framed水平LTR/RTL Gridのall-auto縦lr absolute先頭子の測定/static右端、独立矩形/WPT012先頭子/旧007–011/host/Acid | cleared。独立2矩形Chromium一致、水平2 callback先頭子全検査PASS、012 wholeはFAILのまま、p217 whole未達 |

Outcome: WPT012水平2 callbackの初回失敗は2番目子へ進んだ。縦2 callback初回失敗は非悪化、旧WPT/host CSS51/JS14/Grid48、warning0 build、Acid3差0/Acid2差1865。[証拠](../ws074/phase217/evidence/20261014-q1197/README.md)。WPT/runner/ABI/Master無変更。

Resume: framed水平Gridの2番目all-auto縦rl absolute子。p217/p189/p173全3,805件100%未達。共有Queue/Master/GitHub publication pending、pushなし。
