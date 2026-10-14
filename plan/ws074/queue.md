<!-- awesome-plan project=zedbsd record=queue -->

# q1152 / WS074 vertical Grid item margins

Status: finished（2026-10-14 11:57 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（13:23 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1152-i01 | [ws074-p225](phase225/phase.md) partial | q1150の2×2 implicit auto vertical-lr/LTR・definite content-box・gap0・単一ASCII文字子・start/stretch限定経路で、非負px marginのみ内容寄与・子の解決済み物理サイズ・配置へ一般的に反映。枠線/パディングは対象外。独立margin最小ページのChromium比較、固定010/014 whole PASS、旧001–009/013と縦横WPT、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加を確認。WPT/runner/ABI/Master変更なし | cleared。[証拠](phase225/evidence/20261014-q1152/README.md) | q1150基準、q1151 recensus |

Dependency graph: q1150基準 + q1151 recensus → q1152-i01。p225/p189/p173 wholeは部分受け入れに含めない。

Upcoming Work Outlook: p225 border011/015・padding012/016、p220他writing-mode/複雑inline/装飾、p217 baseline、p224 animation、runner未対応。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
