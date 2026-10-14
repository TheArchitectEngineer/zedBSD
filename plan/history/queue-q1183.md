<!-- awesome-plan project=zedbsd record=q1183 -->

# q1183 / WS074 horizontal RTL Grid vertical absolute text geometry

Status: finished（2026-10-14 13:39–13:42 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p220有限部分scope](../ws074/phase220/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1183-i01 | [p220](../ws074/phase220/phase.md) partial | 水平RTL固定2×2 Grid直接縦左/縦右RTL absolute子、row1左右insetのrow-track inline roomとY align、独立2子/WPT007先頭2子/旧WPT/host/Acid | cleared。独立2矩形Chromium一致、固定先頭2子全検査PASS、WPT file wholeはFAILのまま、p220 whole未達 |

Outcome: RTL callback初回失敗は1番目子width10→60から4番目vertical-rl/RTL子width40→60へ進んだ。旧WPT/host CSS51/JS14/Grid48、warning0 build、Acid3差0/Acid2差1865。[証拠](../ws074/phase220/evidence/20261014-q1183/README.md)。WPT/runner/ABI/Master無変更。

Resume: 水平RTL Grid第4縦RTL子、次に縦Grid横子static area。p189/p173全3,805件100%未達。共有Queue/Master/GitHub publication pending、pushなし。
