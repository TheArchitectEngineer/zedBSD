<!-- awesome-plan project=zedbsd record=queue -->

# q1161 / WS074 positioned Grid alignment diagnosis

Status: finished（2026-10-14 12:23–12:24 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（13:53 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1161-i01 | [ws074-p217](phase217/phase.md) partial | q1160固定Grid502の残FAIL中、`grid-column-axis-alignment-positioned-items-001..017`と`grid-row-axis-alignment-positioned-items-001..017`最大34 IDを抽出し、各first failed subtest/症状と旧q1141比較を集計。代表縦書き/RTL/水平ケースのWPT原本とGrid absolute area・text geometry経路を読み、次の有限実装候補/回帰条件を記録。読取のみ、browser/runner/WPT/ABI/Master/Acid変更なし | cleared | q1160 current、q1157 vertical-rl text、q1159 vertical Grid |

Dependency graph: q1160 current + q1157 text + q1159 Grid → q1161-i01。p217/p189/p173 wholeは部分受け入れに含めない。

Upcoming Work Outlook: positioned Grid、baseline、p221 parsing、p224 animation、runner未対応。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。

Outcome: positioned列軸/行軸34件は全whole FAIL。33件は水平2/4 PASS・縦の初回width期待10/実60、行軸017は別の水平RTL offsetLeft差。absolute rootが縦inlineを拒否し、縦Grid static areaも未実装。[証拠](phase217/evidence/20261014-q1161/README.md)。次はbounded absolute縦文字geometry。p217/p189/p173 whole未達。
