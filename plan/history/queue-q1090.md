<!-- awesome-plan project=zedbsd record=q1090 -->

# q1090 / WS074 RTL Grid absolute position design

Status: finished（2026-10-14 07:40–07:41 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p217有限部分scope](../ws074/phase217/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1090-i01 | [p217](../ws074/phase217/phase.md) partial | RTL Grid direct absolute子のstatic位置をWPT・固定/auto幅最小HTML・配置順から調査。製品browser/runner/WPT/ABI/Master変更なし | cleared。p217 whole uncleared |

Outcome: 最小RTL 400px Gridの固定20px子は実x8/期待x388、auto24px子は実x8/期待x384。[証拠](../ws074/phase217/evidence/20261014-q1090/README.md)。Grid static_xは幅確定前の左端だけを表し、position_placeは幅確定後にもそれを左端として読む。private right-edge metadataを次の有限実装候補にした。

Resume: q1091でRTL startだけ幅確定後に右端基準で配置、代表WPT・回帰・Acidを測る。縦書き/他alignment、p189/p173全件100%は未達。共有Queue/Master/GitHub publicationはQ1との競合を避けpending projection。pushなし。
