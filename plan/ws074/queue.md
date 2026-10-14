<!-- awesome-plan project=zedbsd record=queue -->

# q1185 / WS074 vertical Grid horizontal absolute static area

Status: finished（2026-10-14 13:45–13:46 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（15:15 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1185-i01 | [ws074-p217](phase217/phase.md) partial | positioned vertical-lr/rl LTR固定2×2 inline-grid、明示px tracks/gap0、親/子frame0、直接absolute horizontal-tb/LTR auto-size ASCII子、row1/col1明示span1・left明示で、横子のGrid areaとjustify-self:right静的Y位置を最小修正。独立lr/rl最小ページChromium矩形一致、固定non-static007縦lr/rl callbackの先頭子全検査通過、旧縦positioned001と水平LTR/RTL callback、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加を確認。他orthogonal/RTL子/parent frame/auto line、WPT/runner/ABI/Master変更なし。WPT全file wholeは基準外 | cleared。[証拠](phase217/evidence/20261014-q1185/README.md) | q1184水平両callback |

Dependency graph: q1184水平両callback → q1185-i01。p217/p189/p173 wholeは部分受け入れに含めない。

Upcoming Work Outlook: 縦Grid横子の他3 area、non-static008–012 frame variants、auto lines、stretch005–016、RTL字形視覚、RTL行軸017、baseline、runner未対応。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
