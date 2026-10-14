<!-- awesome-plan project=zedbsd record=queue -->

# q1154 / WS074 fixed Grid502 recensus after item frames

Status: finished（2026-10-14 12:03–12:06 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（13:33 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1154-i01 | [ws074-p189](phase189/phase.md) partial | q1151と同一固定Grid502 ID/WPT/metadata/runner/font/settle/process条件で4 bounded shardを全再測定。ID/hash/checkpoint/gzip rawを照合し、whole/subtestのPASS変化・旧PASS後退・残FAILを分類。読取のみ。browser/runner/WPT/ABI/Master/Acid無変更 | cleared | q1153 border/padding、q1151 baseline |

Dependency graph: q1153 item frame + q1151 baseline → q1154-i01。p189/p173 wholeは部分受け入れに含めない。

Upcoming Work Outlook: p225他Grid軸/track、p217 alignment、p224 animation、runner未対応。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。

Outcome: 固定Grid502で136→142 whole PASS、366→360 FAIL、subtest6改善・旧PASS後退0。502 ID/hash/checkpoint/gzip raw一致。[証拠](phase189/evidence/20261014-q1154/README.md)。p189/p173 whole未達。
