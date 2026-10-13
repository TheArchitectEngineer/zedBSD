<!-- awesome-plan project=zedbsd record=queue -->

# q1005 / WS074 Acid3 lane

Status: finished（2026-10-13 23:06〜23:09 JST）
Owner: この Codex セッション単独。作業 tree `/tmp/zedbsd-ws074`、branch `codex/ws074-acid3`。メインセッションの Queue と source 所有を分離する。
Approval: 2026-10-13 このチャットでユーザーが [p182 の exact scope](phase182/phase.md) を示した質問へ「q1005 の範囲を承認する」と回答。対象は非 quirks HTML の HTTP stylesheet MIME 判定、実 HTTP host 試験、warning0 build、固定 Acid3/Acid2、差18596/1865の非増加、最大90分。続けてユーザーは画素差0まで最大90分 Queue を何度でも自動実行するよう指示。後続も各 exact scope を記録してから単独で着手する。
Timebox: 2026-10-13 23:06 JST 開始、最大90分。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1005-i01 | [ws074-p182](phase182/phase.md) | 非 quirks HTML の HTTP stylesheet MIME。text/css 適用、text/html 非適用、Content-Type 欠落/quirks/ローカル経路を維持。実 HTTP host 試験、warning0 build、固定 Acid3/Acid2、差18596/1865の非増加。最大90分 | cleared | p172/p179/p180/p181 cleared、q1004 差画像/style/paint を確認 |

Dependency graph: p172/p179/p180/p181 → p182、q1004 の実測出力 → p182、p182 → p100 の最終受け入れ判断。q1000-i01 は uncleared 履歴として保持。

Upcoming Work Outlook: 差画像から次の局所原因を選び、最大90分の exact scope を記録して自動継続する。得点の slash 色・基線、見えてはいけない上部の文字、Acid2 の残差が候補。全画面差0・score100/100・fail/Uncaught/crash/timeout0、回帰・全文規約を満たすまで p100 を cleared にしない。

Coordination: q1000、q1001、q1002、q1003、q1004 は [履歴](../history/queue-q1000.md)・[履歴](../history/queue-q1001.md)・[履歴](../history/queue-q1002.md)・[履歴](../history/queue-q1003.md)・[履歴](../history/queue-q1004.md)に保存。共有 `plan/queue.md` へ投影。GitHub Issue/Project publication と push は設定に従い保留。

Outcome: q1005-i01/p182 cleared。実 HTTP fixture 修正前 FAIL→修正後 PASS、host build warning0。Acid3 score100/100・inventory100/100・例外0・差13302/480000（5294減）、Acid2差1865/120000不変。[証拠](phase182/evidence/20261013-q1005/report.json)。p100全画面差0・WS074受入は未達。
