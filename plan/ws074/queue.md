<!-- awesome-plan project=zedbsd record=queue -->

# q1168 / WS074 opposite vertical writing-mode absolute text geometry

Status: finished（2026-10-14 12:42–12:45 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（14:12 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1168-i01 | [ws074-p220](phase220/phase.md) partial | positioned LTR fixed 2×2 vertical-lr/rl inline-grid、明示px track/gap0の直接absolute auto-size ASCII子に限り、子が親と逆のvertical writing-modeでも縦inline geometryへ通す。固定column/row-axis010/011の縦初回width10/高さ期待一致、旧001–009代表、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加を検証。static area/逆direction/auto lines/他Grid、WPT/runner/ABI/Master変更なし | uncleared（row-axis010/011は逆directionでscope外。column側改善は保持） | q1166診断、q1162縦文字 |

Dependency graph: q1166診断 + q1162縦文字 → q1168-i01。p220/p217/p189/p173 wholeは部分受け入れに含めない。

Upcoming Work Outlook: 逆writing-mode子static area、auto lines、RTL行軸017、baseline、runner未対応。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。

Outcome: column-axis010/011の縦初回width60→10、後続static差へ進んだがwhole FAIL。row-axis010/011は実際は逆direction子でwidth60維持し、q1168の明示除外条件に当たるためattempt uncleared。旧6件/host/Acid維持。[証拠](phase220/evidence/20261014-q1168/README.md)。次はRTL縦文字の新Queue。p220/p217/p189/p173 whole未達。
