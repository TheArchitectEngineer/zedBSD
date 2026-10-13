<!-- awesome-plan project=zedbsd record=q1021 -->

# q1021 / WS074 text-decoration shorthand grammar

Status: finished（2026-10-14 00:32〜00:35 JST）
Approval: この chat のユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」（00:07 JST）を、[p191の有限部分scope](../ws074/phase191/phase.md)に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1021-i01 | [p191](../ws074/phase191/phase.md) partial | shorthandのline/style/color/thickness全component検証・longhand宣言展開。不正値拒否、fixture/build/固定WPT/Acid回帰。canonical/computed CSSOMは後続 | cleared（partial）。p191 whole uncleared |

Prerequisites: q1020までのlonghand CSSOM baseline。

Outcome: invalid WPT3/3 subtest PASS（前回2/3）。固定textdecoration全16件13 PASS/3 FAIL（前回12/4）、subtest153 PASS/26 FAIL（150/29）。局所fixture、build warning0、CSSOM12/12、rule model44/44、Acid3 score100/100・RGB差0、Acid2差1865不変。[証拠](../ws074/phase191/evidence/20261014-q1021/README.md)。WPT/Master/公開ABI無変更。

Remaining: shorthandのvalid/computed/CSSOM getterの3 FAIL、p189/p190 runner coverage、p173全3,805件100%。共有Queue/Master/GitHub publicationはQ1と競合回避のためpending。pushなし。
