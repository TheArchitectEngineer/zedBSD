<!-- awesome-plan project=zedbsd record=queue -->

# q1187 / WS074 vertical-rl Grid third horizontal RTL absolute area

Status: finished（2026-10-14 13:50–13:52 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（15:20 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1187-i01 | [ws074-p217](phase217/phase.md) partial | positioned vertical-rl LTR固定2×2 inline-grid、明示px tracks/gap0、親/子frame0、直接absolute horizontal-tb/RTL auto-size ASCII子、row2/col1明示span1・top明示・justify-self:startでGrid areaを最小修正。独立最小ページChromium矩形一致、固定non-static007縦rl callbackの最初3子全検査通過、縦lr先頭3子/水平2 callback、旧縦positioned001、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加を確認。他orthogonal/RTL親/parent frame/auto line、WPT/runner/ABI/Master変更なし。WPT全file wholeは基準外 | cleared（[証拠](phase217/evidence/20261014-q1187/README.md)） | q1186縦先頭2子 |

Dependency graph: q1186縦先頭2子 → q1187-i01。p217/p189/p173 wholeは部分受け入れに含めない。

Upcoming Work Outlook: 縦lr Grid4番目横RTL子、non-static008–012 frame variants、auto lines、stretch005–016、RTL字形視覚、RTL行軸017、baseline、runner未対応。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
