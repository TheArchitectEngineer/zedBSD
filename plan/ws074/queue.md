<!-- awesome-plan project=zedbsd record=queue -->

# q1091 / WS074 RTL Grid absolute start positioning

Status: finished（2026-10-14 07:42–07:48 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（09:12 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1091-i01 | [ws074-p217](phase217/phase.md) partial | 水平RTL Gridの`justify-content:normal`固定trackをinline-start（右）に配し、位置指定gridの直接absolute子で両axis inset auto、有効明示row/column、inline start alignmentに限ってprivate right-edge static metadataを幅確定後に使う。固定/auto幅最小HTML、q1089 LTR/inset/無効line、固定WPT水平RTL2件、旧PASS代表、classic JS14、warning0 host build、Acid3差0・Acid2非増加で検証。縦書き/他alignment/full Grid/runner/WPT/公開ABI/Master変更なし | cleared（部分）。[証拠](phase217/evidence/20261014-q1091/README.md)、p217 whole uncleared | q1090設計、q1089 LTR |

Dependency graph: q1089 LTR origin → q1090 RTL設計 → q1091 RTL修正 → p189再測定。

Upcoming Work Outlook: 固定WPT代表の縦書き2 subtestで軸対応を調査し、一般欠陥を修正してからGrid502全件を再計測。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
