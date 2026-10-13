<!-- awesome-plan project=zedbsd record=queue -->

# q1104 / WS074 explicit Grid template shorthand

Status: finished（2026-10-14 08:23–08:25 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（09:53 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1104-i01 | [ws074-p218](phase218/phase.md) partial | `grid-template: <既存対応track-list> / <既存対応track-list>`と単独`none`を既存rows/columnsへatomic展開。areas/line names/CSSOM getter/auto-flow等は対象外。最小geometryとcascade/invalid検証、固定grid-template-shorthand-valid代表のsubtest推移、旧Grid/JS/CSS、warning0 build、Acid3差0・Acid2非増加。WPT/runner/公開ABI/Master変更なし | cleared（部分）。[証拠](phase218/evidence/20261014-q1104/README.md)、p218 whole uncleared | q1095 grid明示track parser・q1103と独立 |

Dependency graph: q1093 Grid recensus → q1095明示track形 → q1104 grid-template明示track形。q1096〜q1103は独立のbaseline/縦書き鎖で、q1103→p220は外部後続。

Upcoming Work Outlook: [p220](phase220/phase.md)の縦inline最初の有限実装、p217へのGrid統合、他baseline群、Grid areas/lines/CSSOM。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
