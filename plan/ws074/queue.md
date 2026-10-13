<!-- awesome-plan project=zedbsd record=queue -->

# q1103 / WS074 vertical inline text layout design

Status: finished（2026-10-14 08:22–08:23 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（09:52 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1103-i01 | [ws074-p217](phase217/phase.md) partial | 固定007/008のvertical-lr Ahem文字幅期待/実際、q1092最小host、CSS writing-mode/line/paint/DOM geometry経路を追い、1文字sideways inlineの最小実装境界と次の検証fixtureを設計する。読取と証拠のみ。browser/runner/WPT/公開ABI/Master変更なし、Acid/全Grid再測定なし | cleared（部分）。[証拠](phase217/evidence/20261014-q1103/README.md)、p217 whole uncleared | q1102残007/008・q1092 vertical host |

Dependency graph: q1093 Grid recensus → q1095明示track形 → q1096 empty baseline → q1097 style-changes測定 → q1098 line baseline → q1099 style-changes再測定 → q1100縦書き診断 → q1101空子x → q1102再測定 → q1103縦文字設計。q1092縦書き設計もq1100/q1103前提。

Upcoming Work Outlook: [p220](phase220/phase.md)の縦inline最初の有限実装、p217へのGrid統合、他baseline群、implicit shorthand/CSSOM。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
