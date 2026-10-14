<!-- awesome-plan project=zedbsd record=q1144 -->

# q1144 / WS074 vertical-lr inline geometry

Status: finished（2026-10-14 11:03–11:11 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p220有限部分scope](../ws074/phase220/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1144-i01 | [p220](../ws074/phase220/phase.md) partial | vertical-lr/LTR text-only非折返し行・br行の物理geometry、DOM bounds/hit/dump、限定fallback、旧PASS/host/build/Acid回帰 | cleared。p220 whole uncleared |

Outcome: 単一10px文字span36×13→10×36、2行36×23→20×36。固定Grid stretch001/002 whole FAIL→PASS、旧PASS2・host・Acid3差0維持。[証拠](../ws074/phase220/evidence/20261014-q1144/README.md)。paintは横向きのままで対象外。q1142未達履歴は保持。

Resume: p225 vertical-lr family再測定、p220 CPU/GPU縦paint、他writing-mode/折返しへ有限に広げる。p220/p225/p217/p189/p173全件100%未達。共有Queue/Master/GitHub publication pending、pushなし。
