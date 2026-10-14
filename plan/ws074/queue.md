<!-- awesome-plan project=zedbsd record=queue -->

# q1146 / WS074 orthogonal children in fixed vertical Grid

Status: finished（2026-10-14 11:19 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（12:48 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1146-i01 | [ws074-p225](phase225/phase.md) partial | vertical-lr/LTR・inline-grid・明示固定px track・gap0・単一span・in-flow非replaced/auto寸法の限定物理軸経路で、horizontal-tb子のstart/stretchを正しく扱う。最小2×2横書き子と固定vertical-lr stretch WPT005–008、旧001–004・horizontal/vertical PASS、CSS/JS/Grid host、warning0 build、Acid3差0/Acid2非増加、範囲外fallbackを確認。WPT/runner/公開ABI/Master変更なし | cleared：対象4 whole PASS、回帰維持。[証拠](phase225/evidence/20261014-q1146/README.md) | q1142縦Grid軸、q1144 text geometry、q1145固定family診断 |

Dependency graph: q1142軸変換 + q1144文字geometry + q1145診断 → q1146-i01。p225/p217/p189/p173 wholeは部分受け入れに含めない。

Upcoming Work Outlook: 固定Grid502全再測定後、p225明示250px Grid/可変trackの009–016、p220 CPU/GPU縦paint、vertical-rl/RTLとabspos、p217 baseline、p224 animation、runner未対応。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
