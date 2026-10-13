<!-- awesome-plan project=zedbsd record=q1040 -->

# q1040 / WS074 fixed crashtest full census

Status: finished（2026-10-14 01:35–01:36 JST）。Approval: この chat のユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を [p190の有限部分scope](../ws074/phase190/phase.md) に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1040-i01 | [p190](../ws074/phase190/phase.md) partial | q1039 runnerのall-focus選択、固定crashtest48一意IDの同条件全測定と分類。browser/WPT/Master/公開ABI変更なし | cleared（partial）。p190 whole uncleared |

Outcome: 固定crashtest48件すべてlocal rendererでPASS、crash/timeout/process error/no-image/unsupportedは0。[証拠](../ws074/phase190/evidence/20261014-q1040/README.md)。全IDとfocus分布をmanifest照合、details3件はq1039画像hash一致。選択/分類control6/6、Python compile/diff check PASS。browser/WPT/Master無変更のためhost build/Acid再計測省略。

Remaining: reftest1,974件のcoverage/描画差、testharness残件、公式環境同等性、p173全3,805件100%。共有Queue/Master/GitHub publicationはQ1と競合回避のためpending。pushなし。
