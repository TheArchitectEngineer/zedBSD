<!-- awesome-plan project=zedbsd record=queue -->

# q1120 / WS074 inline-grid intrinsic width

Status: finished（2026-10-14 09:21–09:28 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（10:51 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1120-i01 | [ws074-p217](phase217/phase.md) partial | `display:inline-grid`をdistinct内部display値として保持し、inline-level atomicのshrink-to-fit幅へ通しつつGrid item/layout/intrinsic width経路を維持。最小auto幅250px/explicit幅/block-grid/RTL absolute fixture、固定column-axis012–016水平subtest、旧PASS、CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加。WPT/runner/公開ABI/Master変更なし | cleared（[証拠](phase217/evidence/20261014-q1120/README.md)）。p217 whole uncleared | q1119診断・q1118行境界 |

Dependency graph: q1093 Grid recensus → q1105全再測定 → q1108 positioned診断 → q1113方向残差診断 → q1118 auto-row block boundaries → q1119 inline-grid診断 → q1120 inline-grid幅。q1103→p220は縦書き外部後続。

Upcoming Work Outlook: [p220](phase220/phase.md)縦inline、positioned縦Grid軸、fr/gutter1 subtest、Grid areas/lines/CSSOM。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
