<!-- awesome-plan project=zedbsd record=queue -->

# q1160 / WS074 fixed Grid502 recensus after vertical-rl

Status: finished（2026-10-14 12:20–12:22 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（13:50 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1160-i01 | [ws074-p189](phase189/phase.md) partial | q1154と同一固定Grid502 ID/WPT/metadata/runner/font/settle/process条件で4 bounded shardを全再測定。ID/hash/checkpoint/gzip rawを照合し、whole/subtestのPASS変化・旧PASS後退・残FAILを分類。読取のみ。browser/runner/WPT/ABI/Master/Acid無変更 | cleared | q1159 vertical-rl auto、q1154 baseline |

Dependency graph: q1159 vertical-rl auto + q1154 baseline → q1160-i01。p189/p173 wholeは部分受け入れに含めない。

Upcoming Work Outlook: p217 alignment、p221 parsing、p224 animation、runner未対応。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。

Outcome: 固定Grid502で142→158 whole PASS、360→344 FAIL、subtest16改善・旧PASS後退0。502 ID/hash/checkpoint/gzip raw一致。[証拠](phase189/evidence/20261014-q1160/README.md)。p189/p173 whole未達。
