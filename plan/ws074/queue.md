<!-- awesome-plan project=zedbsd record=queue -->

# q1171 / WS074 opposite vertical writing-mode absolute self alignment

Status: finished（2026-10-14 12:52–12:56 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（14:22 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1171-i01 | [ws074-p217](phase217/phase.md) partial | CSS align-self:self-start/endの宣言値を保持し、従来Grid/Flex in-flow同writing-mode経路では旧start/end動作を保つ。q1168固定2×2 vertical-lr/rl LTR positioned inline-grid、明示px tracks/gap0、直接absolute auto-size ASCII子、子は逆vertical writing-mode/LTR・両axis明示span1・全offset autoに限り、子自身のblock-start/endを物理x static areaへ写す。固定column-axis010/011 whole PASS、旧column001–009・row001–011、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加を検証。auto lines/RTL親/他Grid、WPT/runner/Master変更なし | cleared | q1168縦寸法 + q1170静的位置 |

Dependency graph: q1168縦寸法 + q1170静的位置 → q1171-i01。p217/p189/p173 wholeは部分受け入れに含めない。

Upcoming Work Outlook: RTL字形視覚検証、auto lines、RTL行軸017、baseline、runner未対応。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。

Outcome: 固定column-axis010/011 whole FAIL→PASS、column/row001–011の22件88 callbacks PASS。computed align-self値self-start/end、host CSS51/CSSOM12/JS14/Grid48/Flex5、warning0 build、Acid3差0/Acid2差1865。[証拠](phase217/evidence/20261014-q1171/README.md)。p217/p189/p173 whole未達。
