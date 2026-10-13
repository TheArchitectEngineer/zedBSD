<!-- awesome-plan project=zedbsd record=q1058 -->

# q1058 / WS074 residual Flex reftest regressions

Status: finished（2026-10-14 03:06–03:12 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p206の有限部分scope](../ws074/phase206/phase.md)に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1058-i01 | [p206](../ws074/phase206/phase.md) partial | 旧8回帰中残3 Flex IDを画像/幾何で切り分け、一般的欠陥のみ最小修正、固定3/Flex10とhost/build/Acid回帰。WPT/runner/ABI/Master変更なし | cleared（partial）、p206 whole uncleared |

Outcome: auto主軸reverseとFlex min/max凍結後再配分で固定3件0→2 local pixel exact PASS。縦textarea1件は198差画素。[証拠](../ws074/phase206/evidence/20261014-q1058/README.md)。host physical21/21・CSS rule51/51・CSSOM12/12、warning0 build、固定Flex10/10 PASS、Acid3差0、Acid2差1865。

Remaining: 旧8回帰中1件、p190他画素FAIL/runner未対応、p173全3,805件100%。共有Queue/Master/GitHub publicationはQ1との競合回避でpending。pushなし。
