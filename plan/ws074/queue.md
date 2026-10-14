<!-- awesome-plan project=zedbsd record=queue -->

# q1144 / WS074 vertical-lr inline geometry

Status: finished（2026-10-14 11:11 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（12:33 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1144-i01 | [ws074-p220](phase220/phase.md) partial | vertical-lr/LTRの非Grid text-only block内の単一非折返し行と明示br行で、論理inline advance/line block幅を物理height/widthに写し、auto高さ・intrinsic幅・DOM bounds/hit/dumpを整合。float/atomic/replaced/marker/縦paintは範囲外。最小10px文字fixture（1行36×10、複数行）、旧horizontal/vertical PASS、CSS/JS/Grid host、warning0 build、Acid3差0/Acid2非増加を確認。固定vertical-lr Grid WPT2件は観測するがPASSは部分基準外。WPT/runner/公開ABI/Master変更なし | cleared：限定geometry/回帰PASS、対象Grid WPT2件もPASS。[証拠](phase220/evidence/20261014-q1144/README.md) | p220 q1121診断、q1142軸変換、q1143後退0 |

Dependency graph: p220 q1121診断 + q1142軸変換 + q1143後退0 → q1144-i01。p220/p225/p217/p189/p173 wholeは部分受け入れに含めない。

Upcoming Work Outlook: p225固定vertical-lr family全件再測定、p220縦paint CPU/GPU整合。その後vertical-rl/RTL、Grid abspos/baseline、p224 animation、runner未対応。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
