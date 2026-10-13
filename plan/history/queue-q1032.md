<!-- awesome-plan project=zedbsd record=q1032 -->

# q1032 / WS074 details testharness full focus measurement

Status: finished（2026-10-14 01:19 JST）。Approval: この chat のユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を [p193の有限部分scope](../ws074/phase193/phase.md) に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1032-i01 | [p193](../ws074/phase193/phase.md) partial | 固定details testharness全18 IDの単一HTTP実測と結果分類。browser/WPT/Master変更なし | cleared（partial）。p193 whole uncleared |

Outcome: 18 ID中6 PASS・10 FAIL・2 no_output。残るfragment/scroll、reveal再入、computed style、text fragment/testdriver、details layoutを分類。[証拠](../ws074/phase193/evidence/20261014-q1032/README.md)。25 reftest/3 crashtestはp190 runner待ち。コード無変更のためhost build/Acid再計測省略。

Remaining: p193全46件、p194 Attr/NamedNodeMap full、p189全1,783件、p190全2,022件、p173全3,805件100%。共有Queue/Master/GitHub publicationはQ1と競合回避のためpending。pushなし。
