<!-- awesome-plan project=zedbsd record=queue -->

# q1121 / WS074 vertical inline text geometry diagnosis

Status: finished（2026-10-14 09:30–09:35 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（11:00 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1121-i01 | [ws074-p220](phase220/phase.md) partial | 固定column-axis positioned-items012–016のvertical-lr/rl初回幅期待10/実60についてWPT source、最小非Grid/位置指定Grid fixture、inline line/fragment・文字方向・描画/DOM geometry経路を読み取り調査。原因と最初の有限実装scope・回帰条件を証拠化。製品browser/runner/WPT/公開ABI/Master/Acid変更なし | cleared（[証拠](phase220/evidence/20261014-q1121/README.md)）。p220 whole uncleared | p217 q1120水平inline-grid幅、p217 q1103設計 |

Dependency graph: q1103縦inline設計 → q1120水平inline-grid幅 → q1121縦文字診断。p217縦Gridはp220の検証済み出力を待つ。

Upcoming Work Outlook: p220縦inlineの最小実装、p217縦Grid軸、fr/gutter1 subtest、Grid areas/lines/CSSOM。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
