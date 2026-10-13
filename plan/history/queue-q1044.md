<!-- awesome-plan project=zedbsd record=q1044 -->

# q1044 / WS074 bounded static reftest census

Status: finished（2026-10-14 01:47–01:55 JST）。Approval: この chat のユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を [p190の有限部分scope](../ws074/phase190/phase.md) に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1044-i01 | [p190](../ws074/phase190/phase.md) partial | static runner case別描画上限/逐次durable checkpoint、固定静的1,561 ID全再測定、100件sampleとfocus別照合。browser/WPT/Master/公開ABI変更なし | cleared（partial）。p190 whole uncleared |

Outcome: case別5秒上限で固定静的1,561件を全測定、500 local PASS/1,051 FAIL/10 timeout/0他error。[証拠](../ws074/phase190/evidence/20261014-q1044/README.md)。逐次fsync JSONL1,561行と最終report、選定ID/hash、100件sample全結果/画素差を照合。selection3/3+chain4/4+checkpoint2/2、Python compile/diff check PASS。browser/WPT/Master無変更のためhost build/Acid再計測省略。

Remaining: script関連407/variant6のrunner条件、1,051描画FAILと10 timeoutの原因、公式環境同等性、p173全3,805件100%。共有Queue/Master/GitHub publicationはQ1と競合回避のためpending。pushなし。
