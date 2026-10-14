<!-- awesome-plan project=zedbsd record=queue -->

# q1163 / WS074 fixed vertical Grid absolute start area

Status: finished（2026-10-14 12:29–12:31 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（13:59 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1163-i01 | [ws074-p217](phase217/phase.md) partial | positioned vertical-lr/rl LTR inline-grid、2×2明示固定px tracks・gap0、直接absolute auto-size ASCII子、grid-column/rowとも明示span1、全offset auto、両axis start配置に限り、論理列→物理y・行→物理x（rlは右端）static areaを渡す。固定column-axis001/row-axis001 whole PASS、独立2×2最小ページChromium比較、旧水平/縦Grid WPT、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加を確認。他alignment/auto lines/RTL/他Grid、WPT/runner/ABI/Master変更なし | cleared | q1162 absolute縦文字、q1161診断 |

Dependency graph: q1162縦文字 + q1161診断 → q1163-i01。p217/p189/p173 wholeは部分受け入れに含めない。

Upcoming Work Outlook: positioned end/center/self等、auto lines、RTL行軸017、baseline、runner未対応。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。

Outcome: 固定column-axis001/row-axis001が2/2 whole FAIL→PASS、独立縦lr/rl absolute 2×2の全8矩形はChromium一致。旧縦Grid32+縦横2 PASS、host CSS51/JS14/Grid48、warning0 build、Acid3差0/Acid2差1865。[証拠](phase217/evidence/20261014-q1163/README.md)。p217/p189/p173 whole未達。
