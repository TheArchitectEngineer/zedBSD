<!-- awesome-plan project=zedbsd record=queue -->

# q1156 / WS074 non-Grid vertical-rl diagnosis

Status: finished（2026-10-14 12:08–12:09 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（13:38 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1156-i01 | [ws074-p220](phase220/phase.md) partial | q1144非Grid単一行/明示改行vertical-lr最小ページをvertical-rlへ変え、ChromiumとbrowserのDOM矩形、layout/paint dump、CPU/GPU画像を比較。子の物理右始まり/行進行とpaint方向を切り分け、p220実装とp225 Grid依存の有限境界を記録。読取と証拠ページのみ。browser/runner/WPT/ABI/Master/Acid無変更 | cleared | q1155 vertical-rl16全FAIL、q1144 vertical-lr geometry |

Dependency graph: q1144 geometry + q1155 classification → q1156-i01。p220/p225/p189/p173 wholeは部分受け入れに含めない。

Upcoming Work Outlook: vertical-rl inline geometry/paint、p225 fixed/auto Grid、p217 alignment、runner未対応。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。

Outcome: 非Grid vertical-rl単一/2行のDOM矩形はChromium右始まり縦進行、browser左始まり横進行。CPU/GPU描画はbyte同一だが横向き。[証拠](phase220/evidence/20261014-q1156/README.md)。p220/p225/p189/p173 whole未達。
