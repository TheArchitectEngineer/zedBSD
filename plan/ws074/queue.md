<!-- awesome-plan project=zedbsd record=queue -->

# q1153 / WS074 vertical Grid item border and padding

Status: finished（2026-10-14 11:58–12:01 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（13:28 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1153-i01 | [ws074-p225](phase225/phase.md) partial | q1152の2×2 implicit auto vertical-lr/LTR・definite content-box・gap0・単一ASCII文字子・start/stretch経路で、子content-boxの非負px border/padding frameを物理track寄与と確定列高/行幅へ反映。margin既存維持。border011/015・padding012/016 whole PASS、独立frameページChromium比較、旧001–010/013–014と縦横WPT、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加。WPT/runner/ABI/Master変更なし | cleared | q1152 margin、q1151 baseline |

Dependency graph: q1152 margin + q1151 baseline → q1153-i01。p225/p189/p173 wholeは部分受け入れに含めない。

Upcoming Work Outlook: p225他Grid軸/track・p220他writing-mode/複雑inline/装飾、p217 baseline、p224 animation、runner未対応。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。

Outcome: 固定011/012/015/016が4/4 whole FAIL→PASS、旧14件もPASS。border/padding独立ページの全boxがChromium 153と一致。warning0 build、host CSS51/JS14/Grid48、Acid3差0・Acid2差1865。[証拠](phase225/evidence/20261014-q1153/README.md)。p225/p189/p173 whole未達。次の有限Queueで固定Grid502を全件再測定。
