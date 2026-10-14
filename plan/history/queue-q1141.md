<!-- awesome-plan project=zedbsd record=q1141 -->

# q1141 / WS074 remaining Grid alignment triage

Status: finished（2026-10-14 10:49 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p217有限部分scope](../ws074/phase217/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1141-i01 | [p217](../ws074/phase217/phase.md) partial | q1140 alignment残192件first callback全分類、代表source/layoutと次の実装境界 | cleared。p217 whole uncleared |

Outcome: 初回幅110、offsetLeft37、offsetTop26、高さ18、その他1。vertical-lr/rl stretch各16とpositioned34はGrid論理軸→物理軸変換の共通候補。baseline43は独立原因。[証拠](../ws074/phase217/evidence/20261014-q1141/README.md)。読取のみ、コード/Acid無変更。

Resume: 固定track vertical-lrの最小軸変換を検証後に縦文字/abspos/RTLへ広げる。p217/p189/p173全件100%未達。共有Queue/Master/GitHub publicationはQ1との競合を避けpending projection。pushなし。
