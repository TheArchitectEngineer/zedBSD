<!-- awesome-plan project=zedbsd record=queue -->

# q1174 / WS074 positioned Grid explicit inset area

Status: finished（2026-10-14 13:06–13:11 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（14:36 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1174-i01 | [ws074-p217](phase217/phase.md) partial | positioned水平LTR/RTL Grid直接absolute子の有効明示row/column areaで、一軸または両軸に明示物理insetがある場合、各明示insetはGrid領域を基準とし、未指定軸はstatic Grid area/align-self/justify-selfを保持する。最小2×2ページChromium比較、固定non-static-positioned-items001–006 whole PASS、旧all-auto positioned代表と明示inset回帰、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加を確認。縦/auto line/他Grid、WPT/runner/ABI/Master変更なし | uncleared（水平12 callbacks PASS、縦12 FAILのためwhole0/6） | q1173分類 |

Dependency graph: q1173分類 → q1174-i01。p217/p189/p173 wholeは部分受け入れに含めない。

Upcoming Work Outlook: margin/border/padding位置、orthogonal stretch、RTL字形視覚、auto lines、RTL行軸017、baseline、runner未対応。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。

Outcome: 最小Grid4矩形はChromiumと一致。固定non-static001–006の水平LTR/RTL12 callbacks PASS、縦12 FAILでwhole0/6。q1174-i01 uncleared、水平修正保持。host/Acid回帰なし。[証拠](phase217/evidence/20261014-q1174/README.md)。次は縦Gridの明示inset areaを別Queue。p217/p189/p173 whole未達。
