<!-- awesome-plan project=zedbsd record=queue -->

# q1129 / WS074 fixed Grid parsing recensus

Status: finished（2026-10-14 09:58–09:59 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（11:28 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1129-i01 | [ws074-p221](phase221/phase.md) partial | q1123と同じ固定Grid parsing41 ID/manifest/WPT/HTTP/fonts/timeout/settleで現行browserを全件再測定。whole/subtest変化・後退・初回差を比較し、最大3の次実装候補を選ぶ。読取/runner実行のみ。browser/WPT/runner/ABI/Master変更なし | cleared（[証拠](phase221/evidence/20261014-q1129/README.md)）。p221 whole uncleared | q1123 baseline、q1124–q1128 targeted improvements |

Dependency graph: q1105固定Grid502 → q1123 parsing再測定 → q1124 rows CSSOM → q1125非負track構文 → q1126行名構文 → q1127 calc track → q1128空行名CSSOM → q1129全41件再測定。p220縦inlineは独立。

Upcoming Work Outlook: p221 `grid-auto-flow`構文/computed CSSOM、次にauto-columns/rowsとareas。p220非Grid vertical-lr行・描画、p217縦Grid軸、fr/gutter1 subtest。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
