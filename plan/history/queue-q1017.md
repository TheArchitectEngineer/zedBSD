<!-- awesome-plan project=zedbsd record=q1017 -->

# q1017 / WS074 text-decoration-line CSSOM

Status: finished（2026-10-14 00:16〜00:19 JST）
Approval: この chat のユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」（00:07 JST）を、[p191の有限部分scope](../ws074/phase191/phase.md)に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1017-i01 | [p191](../ws074/phase191/phase.md) partial | text-decoration-lineの値解析/非継承computed値とcanonical順、既存underline描画維持、局所fixture/build/固定WPT/Acid回帰 | cleared（partial）。p191 whole uncleared |

Prerequisites: q1015全16件baseline、q1016 CSS.supports出力。

Outcome: 対象WPT `text-decoration-line-computed.html` は18/18 subtest PASS。固定textdecoration全16件は5 PASS/11 FAIL（前回4/12）、subtest79 PASS/100 FAIL（47/132）。局所fixture、build warning0、CSSOM12/12、rule model44/44、Acid3 score100/100・RGB差0、Acid2差1865不変。[証拠](../ws074/phase191/evidence/20261014-q1017/README.md)。WPT/Master/公開ABI無変更。

Remaining: textdecoration11 FAIL、p189/p190 runner coverage、p173全3,805件100%。共有Queue/Master/GitHub publicationはQ1と競合回避のためpending。pushなし。
