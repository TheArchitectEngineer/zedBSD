<!-- awesome-plan project=zedbsd record=q1060 -->

# q1060 / WS074 static reftest re-census after Flex and textarea repairs

Status: finished（2026-10-14 03:19–03:23 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p190の有限部分scope](../ws074/phase190/phase.md)に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1060-i01 | [p190](../ws074/phase190/phase.md) partial | 固定静的1,561 IDをq1056同条件再測定、旧8回帰・新規PASS→FAIL・focus別遷移を分類。runner/browser/WPT/Master変更なし | cleared（partial）、p190 whole uncleared |

Outcome: 660 local PASS/901 pixel FAIL/0 error/0 timeout。旧8回帰全PASS、q1056比21 FAIL→PASS/1 PASS→FAIL。新規失敗はflexbox-flex-wrap-flexing.html。固定ID一致・checkpoint1,561行/report照合。[証拠](../ws074/phase190/evidence/20261014-q1060/README.md)。browser変更なしのためAcid再測定省略、前Queue q1059で差0確認。

Remaining: 新規回帰1件、901 pixel FAIL・runner未対応413 ID・公式環境同等性・p173全3,805件100%。共有Queue/Master/GitHub publicationはQ1との競合回避でpending。pushなし。
