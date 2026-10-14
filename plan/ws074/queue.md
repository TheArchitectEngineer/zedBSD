<!-- awesome-plan project=zedbsd record=queue -->

# q1128 / WS074 empty Grid line-name CSSOM

Status: finished（2026-10-14 09:55–09:57 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（11:25 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1128-i01 | [ws074-p221](phase221/phase.md) partial | `grid-template-columns/rows`の有効なJS style設定値から空行名群`[]`をCSSOM直列化時に除去する。非空行名・repeat/track値・無効値は保持。最小CSSOM、固定columns/rows-valid・invalidと旧PASS、CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加。auto-repeat/他のGrid CSSOM、WPT/runner/公開ABI/Master変更なし | cleared（[証拠](phase221/evidence/20261014-q1128/README.md)）。p221 whole uncleared | q1127固定valid残7件/軸、空行名2件/軸 |

Dependency graph: q1105固定Grid502 → q1123 parsing再測定 → q1124 rows CSSOM → q1125非負track構文 → q1126行名構文 → q1127 calc track → q1128空行名CSSOM。p220縦inlineは独立。

Upcoming Work Outlook: p221 auto-repeat・auto/areas・shorthand、p220非Grid vertical-lr行・描画、p217縦Grid軸、fr/gutter1 subtest。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
