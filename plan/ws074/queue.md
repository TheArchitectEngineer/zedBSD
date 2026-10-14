<!-- awesome-plan project=zedbsd record=queue -->

# q1125 / WS074 nonnegative Grid track sizes

Status: finished（2026-10-14 09:42–09:45 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（11:12 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1125-i01 | [ws074-p221](phase221/phase.md) partial | Grid trackの直値/minmax両引数/fit-content引数で負の長さ・割合を構文時に拒否し、0と正値を維持。最小CSSOM host、固定`grid-template-columns-invalid`/`rows-invalid`の負数7×2 subtest、valid代表/旧PASS、CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加。行名・その他track grammar、WPT/runner/公開ABI/Master変更なし | cleared（[証拠](phase221/evidence/20261014-q1125/README.md)）。p221 whole uncleared | q1123・q1124固定失敗証拠、CSS Grid §7.2.1 |

Dependency graph: q1105固定Grid502 → q1123 parsing再測定 → q1124 rows CSSOM → q1125非負track構文。p220縦inlineは独立。

Upcoming Work Outlook: p221行名/暗黙track・auto/areas・shorthand、p220非Grid vertical-lr行・描画、p217縦Grid軸、fr/gutter1 subtest。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
