<!-- awesome-plan project=zedbsd record=queue -->

# q1191 / WS074 horizontal Grid vertical absolute child border

Status: finished（2026-10-14 13:58–13:59 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（15:28 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1191-i01 | [ws074-p217](phase217/phase.md) partial | positioned horizontal-tb LTR/RTL固定2×2 inline-grid、px tracks/gap0、親frame0、直接vertical-lr/LTRまたはRTL auto-size ASCII absolute子row1/col1 left5明示・borderのみ（padding/margin0）を縦inline経路で測る最小修正。独立LTR/RTL最小ページChromium矩形一致、固定non-static009水平2 callback先頭子全検査通過、旧007/008 whole PASS、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加。他frame/子/auto line、WPT/runner/ABI/Master変更なし。009 wholeは基準外 | cleared（[証拠](phase217/evidence/20261014-q1191/README.md)） | q1190 frame診断 |

Dependency graph: q1190 frame診断 → q1191-i01。p217/p189/p173 wholeは部分受け入れに含めない。

Upcoming Work Outlook: non-static008–012 frame variants、auto lines、stretch005–016、RTL字形視覚、RTL行軸017、baseline、runner未対応。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
