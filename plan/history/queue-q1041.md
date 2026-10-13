<!-- awesome-plan project=zedbsd record=q1041 -->

# q1041 / WS074 anchor-positioning static reftest census

Status: finished（2026-10-14 01:37 JST）。Approval: この chat のユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を [p190の有限部分scope](../ws074/phase190/phase.md) に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1041-i01 | [p190](../ws074/phase190/phase.md) partial | 固定anchor-positioning reftest77 IDの静的適格全件測定、未実行理由と画素差/代表画像、ID照合。browser/WPT/Master/公開ABI変更なし | cleared（partial）。p190 whole uncleared |

Outcome: 固定77 ID中static21件を測定し0 local PASS/21 FAIL/0 error、scripted56件未実行。[証拠](../ws074/phase190/evidence/20261014-q1041/README.md)。差画素1,665～80,000、中央値10,798。ID/固定commit/参照とdiff check PASS。コード無変更のためhost build/Acid再計測省略。

Remaining: anchor-positioning描画の一般欠陥、scripted56件を含むp190 reftest coverage、p173全3,805件100%。共有Queue/Master/GitHub publicationはQ1と競合回避のためpending。pushなし。
