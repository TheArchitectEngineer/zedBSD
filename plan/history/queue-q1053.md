<!-- awesome-plan project=zedbsd record=q1053 -->

# q1053 / WS074 fixed static reftest re-census

Status: finished（2026-10-14 02:37–02:42 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p190の有限部分scope](../ws074/phase190/phase.md)に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1053-i01 | [p190](../ws074/phase190/phase.md) partial | 固定静的reftest1,561 IDをq1044同条件で全再測定し、ID/focus別遷移・未対応413理由を集計。browser/WPT/Master/公開ABI変更なし | cleared（partial）。p190 whole uncleared |

Outcome: 518 local PASS/684 FAIL/359 error/0 timeout、旧q1044は500/1,051/0/10。39 FAIL→PASS・10 timeout→PASS、8 PASS→FAIL・23 PASS→error。359 errorは全てDOCTYPE付きXHTMLの読み込みがENOTSUPとなる条件。[証拠](../ws074/phase190/evidence/20261014-q1053/README.md)。固定ID全1,561とcheckpoint/reportを完全照合、未対応413分類不変。コード無変更のためhost build/Acid再測定はscope外。

Remaining: DOCTYPE parser subset、旧PASS→FAIL8件、script関連407/variant6のrunner条件、684描画FAIL、p173全3,805件100%。共有Queue/Master/GitHub publicationはQ1との競合回避でpending。pushなし。
