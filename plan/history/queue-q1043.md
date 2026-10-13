<!-- awesome-plan project=zedbsd record=q1043 -->

# q1043 / WS074 fixed static reftest full census attempt

Status: finished（2026-10-14 01:42–01:46 JST）。Approval: この chat のユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を [p190の有限部分scope](../ws074/phase190/phase.md) に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1043-i01 | [p190](../ws074/phase190/phase.md) partial | 固定静的reftest全1,561 IDの同条件測定、focus別結果、代表画像、100件sampleとの照合。browser/WPT/Master/公開ABI変更なし | uncleared。380/1,561件で中断、p190 whole uncleared |

Outcome: 先頭380 IDは105 local PASS/273 FAIL/2 error。[部分証拠](../ws074/phase190/evidence/20261014-q1043/README.md)。XHTML2件が長時間を要し、隣接caseも遅かった。runnerは最後にのみreportを書くため、残り1,181件とper-reference画素差は未取得。90分timeboxを消費する前に停止し、case別上限/途中checkpointを次の設計にする。コード無変更、host build/Acid再計測省略。

Resume: q1044でper-case上限と結果逐次保存を付け、全件を再測定。共有Queue/Master/GitHub publicationはQ1と競合回避のためpending。pushなし。p173全3,805件100%未達。
