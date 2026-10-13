<!-- awesome-plan project=zedbsd record=queue -->

# q1090 / WS074 RTL Grid absolute positioning design

Status: finished（2026-10-14 07:40–07:41 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（09:10 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1090-i01 | [ws074-p217](phase217/phase.md) partial | q1089代表WPTのRTL subtest期待340/230対実0、Grid明示track・direction/justify-selfと既存static_x/position_placeの評価順を追跡。固定幅とauto幅の小HTMLをhostで再現し、RTL grid areaと幅確定後のstart alignmentを一般的に表す内部設計、次の有限実装scope・リスクを証拠化する。読み取り調査と証拠fixtureのみ。製品browser/runner/WPT/公開ABI/Master変更なし、Acid/全Grid再測定なし | cleared（部分）。[証拠](phase217/evidence/20261014-q1090/README.md)、p217 whole uncleared | p217 q1089 LTR origin |

Dependency graph: q1089 LTR origin → q1090 RTL設計 → 後続RTL修正Queue → p189再測定。

Upcoming Work Outlook: private right-edge static metadataでRTLの幅後start alignmentを実装し、固定WPT水平RTLを再計測。縦書き/他alignmentも残る。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
