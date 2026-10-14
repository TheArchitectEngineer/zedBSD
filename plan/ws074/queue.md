<!-- awesome-plan project=zedbsd record=queue -->

# q1162 / WS074 absolute vertical text in fixed Grid

Status: finished（2026-10-14 12:24–12:28 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（13:54 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1162-i01 | [ws074-p220](phase220/phase.md) partial | positioned vertical-lr/rl LTR inline-gridの直接absolute子、両Grid軸明示固定px track/gap0、auto物理width/height、ASCII textと明示brだけ、非replaced/非float/非marker/非uprightに限り、既存縦inline geometry/sideways paintを適用。固定positioned列軸/行軸001と代表017の縦subtest初回width10/height60の修正を確認し、whole残差はstatic areaと分離。非Grid・旧vertical Grid/水平WPT、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加を検証。Grid static area、WPT/runner/ABI/Master変更なし | cleared | q1161 absolute diagnosis、q1157 vertical text |

Dependency graph: q1161 absolute diagnosis + q1157 text → q1162-i01。p220/p217/p189/p173 wholeは部分受け入れに含めない。

Upcoming Work Outlook: p217 vertical Grid static area、RTL row-axis017、baseline、parser、runner未対応。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。

Outcome: absolute縦文字の10×60 Ahem寸法検査を代表WPT列軸001/行軸001・017で通し、初回差をoffsetTop/offsetLeftへ進めた。3 wholeはstatic area未修正でFAIL。独立最小ページ10×36、hit/paint・CPU/GPU同一、旧Grid WPT34 PASS、host CSS51/JS14/Grid48、warning0 build、Acid3差0/Acid2差1865。[証拠](phase220/evidence/20261014-q1162/README.md)。p220/p217/p189/p173 whole未達。
