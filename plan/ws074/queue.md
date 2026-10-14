<!-- awesome-plan project=zedbsd record=queue -->

# q1148 / WS074 vertical-lr text paint

Status: finished（2026-10-14 11:34 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（12:58 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1148-i01 | [ws074-p220](phase220/phase.md) partial | q1144の非折返しvertical-lr/LTR text-only行のLatin sideways字形を、縦の物理line/fragment座標と時計回り90度のbitmap配置でCPU/GPU描画。shadow/underlineは本scopeの対象外。非対称字形の画素参照・CPU/GPU一致、shader検証、warning0 build、旧縦/横WPT PASS、host/Acid3差0・Acid2非増加。別writing-mode/複雑inline/折返し、WPT/runner/公開ABI/Master変更なし。上記条件を満たさない時はunclearedで残差を記録 | cleared。[証拠](phase220/evidence/20261014-q1148/README.md) | q1144 geometry、q1147 recensus |

Dependency graph: q1144 geometry + q1147 baseline → q1148-i01。p220/p225/p189/p173 wholeは部分受け入れに含めない。

Upcoming Work Outlook: p225明示Grid寸法+auto trackの009–016、p220他writing-mode/複雑inline、p217 baseline、p224 animation、runner未対応。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
