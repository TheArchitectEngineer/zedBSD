<!-- awesome-plan project=zedbsd record=queue -->

# q1165 / WS074 fixed vertical Grid absolute left and right

Status: finished（2026-10-14 12:39–12:42 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（14:09 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1165-i01 | [ws074-p217](phase217/phase.md) partial | q1164固定2×2 vertical-lr/rl LTR positioned inline-grid・明示px tracks・gap0・直接absolute auto-size ASCII子・明示span1・全offset autoに限定し、left/rightの物理軸配置をWPT原本に従い設定。固定column-axis003/004・row-axis003/004のwhole PASS、旧001/002/005代表、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加を検証。他alignment/auto lines/RTL/他Grid、WPT/runner/ABI/Master変更なし | cleared | q1164 end/center area |

Dependency graph: q1164 end/center area → q1165-i01。p217/p189/p173 wholeは部分受け入れに含めない。

Upcoming Work Outlook: flex/self alignment、auto lines、RTL行軸017、baseline、runner未対応。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。

Outcome: 固定column/row-axis003/004の4件whole FAIL→PASS、旧001/002/005の6件維持、10件40 callbacks PASS。host CSS51/JS14/Grid48、warning0 build、Acid3差0/Acid2差1865。[証拠](phase217/evidence/20261014-q1165/README.md)。p217/p189/p173 whole未達。
