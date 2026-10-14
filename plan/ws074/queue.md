<!-- awesome-plan project=zedbsd record=queue -->

# q1169 / WS074 RTL vertical Grid absolute text geometry

Status: finished（2026-10-14 12:47–12:48 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（14:17 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1169-i01 | [ws074-p220](phase220/phase.md) partial | positioned LTR固定2×2 vertical-lr/rl inline-grid、明示px track/gap0の直接absolute auto-size ASCII子で、子が同writing-mode・direction:rtlの場合に縦inline寸法経路へ通す。固定row-axis010/011の縦初回width10/height期待値、旧001/008/009とcolumn010/011初回、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加を検証。文字描画の全面一致/static area/auto lines/他Grid、WPT/runner/ABI/Master変更なし | cleared | q1168訂正、q1162縦文字 |

Dependency graph: q1168訂正 + q1162縦文字 → q1169-i01。p220/p217/p189/p173 wholeは部分受け入れに含めない。

Upcoming Work Outlook: 逆writing-mode/RTL子static area、RTL字形視覚検証、auto lines、RTL行軸017、baseline、runner未対応。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。

Outcome: row-axis010/011縦初回width60→10、後続offsetTop差へ進んだ。旧6件とcolumn010/011幅改善維持、host CSS51/JS14/Grid48、warning0 build、Acid3差0/Acid2差1865。[証拠](phase220/evidence/20261014-q1169/README.md)。q1169部分cleared、p220/p217/p189/p173 whole未達。
