<!-- awesome-plan project=zedbsd record=queue -->

# q1126 / WS074 Grid line-name grammar

Status: finished（2026-10-14 09:47–09:49 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（11:17 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1126-i01 | [ws074-p221](phase221/phase.md) partial | Grid track listの`[line names]`構文を検証し、隣接した括弧群、禁止名`auto`/`span`、不正token/未閉鎖を拒否する。既存の有効な行名・repeat()の受理を保持。最小host、固定columns/rows-invalid・validと旧PASS、CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加。行名保存/CSSOMや他のGrid機能、WPT/runner/公開ABI/Master変更なし | cleared（[証拠](phase221/evidence/20261014-q1126/README.md)）。p221 whole uncleared | q1125固定残差、CSS Grid §7.2.2 |

Dependency graph: q1105固定Grid502 → q1123 parsing再測定 → q1124 rows CSSOM → q1125非負track構文 → q1126行名構文。p220縦inlineは独立。

Upcoming Work Outlook: p221行名CSSOM/暗黙track・auto/areas・shorthand、p220非Grid vertical-lr行・描画、p217縦Grid軸、fr/gutter1 subtest。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
