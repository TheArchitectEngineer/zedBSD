<!-- awesome-plan project=zedbsd record=queue -->

# q1112 / WS074 positioned Grid physical inline edges

Status: finished（2026-10-14 08:52–08:53 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（10:22 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1112-i01 | [ws074-p217](phase217/phase.md) partial | 水平LTR/RTL Gridの直接absolute子、有効明示area、四inset auto、水平子について`justify-self/items:left/right/self-start`の物理辺をprivate area幅と最終子幅で計算。固定row-axis positioned-items003/004/008/010/011の水平subtest、最小LTR/RTL/子方向/明示inset/無効line、旧PASS、Grid/CSS/JS、warning0 build、Acid3差0/Acid2非増加で検証。縦書き/他CSS配置/runner/WPT/公開ABI/Masterは範囲外 | cleared（部分）。[証拠](phase217/evidence/20261014-q1112/README.md)、p217 whole uncleared | q1111診断・q1110 private area |

Dependency graph: q1093 Grid recensus → q1105全再測定 → q1108 positioned診断 → q1109 block軸整列 → q1110 inline軸整列 → q1111残差診断 → q1112物理辺。q1103→p220は縦書き外部後続。

Upcoming Work Outlook: p217 positioned 012–016の面/方向残差、[p220](phase220/phase.md)縦inline、fr/gutter1 subtest、Grid areas/lines/CSSOM。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
