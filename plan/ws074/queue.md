<!-- awesome-plan project=zedbsd record=queue -->

# q1197 / WS074 framed horizontal Grid first vertical auto-inset child

Status: finished（2026-10-14 14:08–14:13 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（15:38 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1197-i01 | [ws074-p217](phase217/phase.md) partial | positioned horizontal-tb LTR/RTL固定2×2 inline-grid、px tracks/gap0、親非対称px margin/border/padding、直接vertical-lr auto-size ASCII absolute子row1/col1明示span1・全auto inset・justify-self:right・子複合frameで、縦inline測定とGrid area右端static Xを最小修正。独立LTR/RTL最小ページChromium矩形一致、固定non-static012水平2 callback先頭子全検査通過、縦2 callback初回失敗非悪化、旧007–011 whole PASS、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加。他子/auto line、WPT/runner/ABI/Master変更なし。012 wholeは基準外 | cleared（[証拠](phase217/evidence/20261014-q1197/README.md)） | q1196 framed診断 |

Dependency graph: q1196 framed診断 → q1197-i01。p217/p189/p173 wholeは部分受け入れに含めない。

Upcoming Work Outlook: non-static008–012 frame variants、auto lines、stretch005–016、RTL字形視覚、RTL行軸017、baseline、runner未対応。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
