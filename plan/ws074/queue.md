<!-- awesome-plan project=zedbsd record=queue -->

# q1117 / WS074 positioned Grid explicit-row content shift

Status: finished（2026-10-14 09:07–09:12 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（10:37 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1117-i01 | [ws074-p217](phase217/phase.md) partial | 水平Gridの固定pixel height・`align-content:center`・正の余白について、明示start rowのdirect absolute子とin-flow Grid itemのrow原点にcontent shiftを加算。auto start rowは対象外のまま保つ。最小explicit row/auto row/in-flow fixture、固定column-axis012–016の最初の横書きassert・次失敗、旧PASS、Grid/CSS/JS、warning0 build、Acid3差0/Acid2非増加。percent height/他content値/縦書き/runner/WPT/ABI/Master範囲外 | cleared（部分）。[証拠](phase217/evidence/20261014-q1117/README.md)、p217 whole uncleared | q1113列軸診断・q1116 inline area |

Dependency graph: q1093 Grid recensus → q1105全再測定 → q1108 positioned診断 → q1113方向残差診断 → q1114 auto-start → q1115 auto-end → q1116 both-auto → q1117 row content shift。q1103→p220は縦書き外部後続。

Upcoming Work Outlook: p217 positioned auto row境界とRTL containing block、[p220](phase220/phase.md)縦inline、fr/gutter1 subtest、Grid areas/lines/CSSOM。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
