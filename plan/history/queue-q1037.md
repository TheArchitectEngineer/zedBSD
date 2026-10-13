<!-- awesome-plan project=zedbsd record=q1037 -->

# q1037 / WS074 full static details reftest census

Status: finished（2026-10-14 01:31 JST）。Approval: この chat のユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を [p190の有限部分scope](../ws074/phase190/phase.md) に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1037-i01 | [p190](../ws074/phase190/phase.md) partial | 固定details root静的reftest全件をq1034 runnerで測定、manifest25件と照合、参照/画素差分類。browser/WPT/Master/公開ABI変更なし | cleared（partial）。p190 whole uncleared |

Outcome: 固定details rootのreftest21件は12 local PASS/9 FAIL/0 error。[証拠](../ws074/phase190/evidence/20261014-q1037/README.md)。参照連鎖2、details-content pseudo5、revert1、inline layout1がFAIL。focus残り4 reftestは別directoryで未実行。ID/固定commit整合とdiff check PASS。コード無変更のためhost build/Acid再計測省略。

Remaining: hidden-directory reftest4、details crashtest3、p190全2,022件coverage、p173全3,805件100%。共有Queue/Master/GitHub publicationはQ1と競合回避のためpending。pushなし。
