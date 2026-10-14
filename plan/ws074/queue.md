<!-- awesome-plan project=zedbsd record=queue -->

# q1127 / WS074 calculated Grid track lengths

Status: finished（2026-10-14 09:51–09:54 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（11:21 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1127-i01 | [ws074-p221](phase221/phase.md) partial | Grid trackと`minmax()`両引数で既存の`calc()`長・割合構文/計算値を受理し、computed負trackを零に丸める。最小CSSOM/幅・行高、固定columns/rows-valid・invalidと旧PASS、CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加。その他math関数/auto-repeat/行名CSSOM、WPT/runner/公開ABI/Master変更なし | cleared（[証拠](phase221/evidence/20261014-q1127/README.md)）。p221 whole uncleared | q1126固定valid残15件/軸、既存calc parser/cascade |

Dependency graph: q1105固定Grid502 → q1123 parsing再測定 → q1124 rows CSSOM → q1125非負track構文 → q1126行名構文 → q1127 calc track。p220縦inlineは独立。

Upcoming Work Outlook: p221空行名CSSOM/auto-repeat・auto/areas・shorthand、p220非Grid vertical-lr行・描画、p217縦Grid軸、fr/gutter1 subtest。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
