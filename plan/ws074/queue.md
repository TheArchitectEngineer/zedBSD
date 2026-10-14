<!-- awesome-plan project=zedbsd record=queue -->

# q1167 / WS074 fixed vertical Grid absolute self alignment

Status: finished（2026-10-14 12:41 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（14:11 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1167-i01 | [ws074-p217](phase217/phase.md) partial | q1165固定2×2 vertical-lr/rl LTR positioned inline-grid・明示px tracks・gap0・直接absolute auto-size ASCII子・明示span1・全offset auto・同writing-modeに限りjustify-self:self-start/endを物理列の上/下static areaへ写す。固定row-axis008/009 whole PASS、旧001–007/column008/009代表、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加を検証。逆writing-mode/auto lines/RTL/他Grid、WPT/runner/ABI/Master変更なし | cleared | q1166診断 |

Dependency graph: q1166診断 → q1167-i01。p217/p189/p173 wholeは部分受け入れに含めない。

Upcoming Work Outlook: 逆writing-mode子、auto lines、RTL行軸017、baseline、runner未対応。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。

Outcome: 固定row-axis008/009の2件whole FAIL→PASS、column/row-axis001–009の18件72 callbacks PASS。host CSS51/JS14/Grid48、warning0 build、Acid3差0/Acid2差1865。[証拠](phase217/evidence/20261014-q1167/README.md)。p217/p189/p173 whole未達。
