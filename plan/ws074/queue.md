<!-- awesome-plan project=zedbsd record=queue -->

# q1095 / WS074 explicit Grid shorthand tracks

Status: finished（2026-10-14 07:59–08:02 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（09:29 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1095-i01 | [ws074-p218](phase218/phase.md) partial | `grid: <既存対応track-list> / <既存対応track-list>`明示行/列形だけ認識し、両側成功時のみ既存rows/columns longhandへatomic展開。gap保持、前後longhandのcascade順、`none`側、invalid/auto-flow/areas拒否。Grid file headerの水平RTL反映。最小static geometry、固定WPT代表の幅subtest、CSS parser/Grid/JS、warning0 build、Acid3差0・Acid2非増加。implicit six longhand/CSSOM全機能、Grid502再測定、WPT/runner/公開ABI/Master変更なし | cleared（部分）。[証拠](phase218/evidence/20261014-q1095/README.md)、p218 whole uncleared | p218 q1094設計 |

Dependency graph: q1093 Grid recensus → q1094 shorthand設計 → q1095明示track形 → 後続implicit/CSSOM。q1092縦書き設計は独立後続。

Upcoming Work Outlook: q1095で残るimplicit longhand/CSSOM/auto-flowとbaseline alignmentを別要因として進め、関連Grid再測定。縦書きは別に段階的に進める。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
