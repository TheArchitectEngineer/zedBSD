<!-- awesome-plan project=zedbsd record=queue -->

# q1007 / WS074 Acid3 lane

Status: finished（2026-10-13 23:16〜23:19 JST）
Owner: この Codex セッション単独。作業 tree `/tmp/zedbsd-ws074`、branch `codex/ws074-acid3`。メインセッションの Queue と source 所有を分離する。
Approval: 2026-10-13 ユーザーの最大90分 Queue を画素差0まで何度でも自動実行する継続指示。[p184 exact scope](../ws074/phase184/phase.md) を記録して選定。
Timebox: 2026-10-13 23:16 JST 開始、最大90分。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1007-i01 | [ws074-p184](../ws074/phase184/phase.md) | 成功した HTML object 子文書の fallback DOM は保持し親 layout/paint から除く。失敗/画像/iframeを維持。局所 HTTP host、frame load、warning0 build、固定 Acid3/Acid2、差12600/1865の非増加。最大90分 | cleared | p172/p179/p180/p181/p182/p183 cleared、q1006 差画像/style/paint を確認 |

Dependency graph: p172/p179/p180/p181/p182/p183 → p184、q1006 実測出力 → p184、p184 → p100 最終受け入れ判断。q1000-i01 は uncleared 履歴として保持。

Upcoming Work Outlook: q1007 後は差画像から次の局所原因を選び、最大90分 exact scope を記録して自動継続する。`:visited` 色による上部文字、本文や枠の位置差、Acid2残差が候補。全画面差0・score100/100・fail/Uncaught/crash/timeout0、回帰・全文規約を満たすまで p100 を cleared にしない。

Coordination: q1000、q1001、q1002、q1003、q1004、q1005、q1006 は [履歴](queue-q1000.md)・[履歴](queue-q1001.md)・[履歴](queue-q1002.md)・[履歴](queue-q1003.md)・[履歴](queue-q1004.md)・[履歴](queue-q1005.md)・[履歴](queue-q1006.md)に保存。共有 `plan/queue.md` へ投影。GitHub Issue/Project publication と push は設定に従い保留。

Previous outcome: q1005-i01/p182 cleared。実 HTTP fixture 修正前 FAIL→修正後 PASS、host build warning0。Acid3 score100/100・inventory100/100・例外0・差13302/480000（5294減）、Acid2差1865/120000不変。[証拠](phase182/evidence/20261013-q1005/report.json)。p100全画面差0・WS074受入は未達。

Outcome: q1006-i01/p183 cleared。HSL fixture14/14、build warning0。Acid3 score100/100・inventory100/100・例外0・差12600/480000（702減）、Acid2差1865/120000不変。[証拠](phase183/evidence/20261013-q1006/report.json)。p100全画面差0・WS074受入は未達。

Outcome: q1007-i01/p184 cleared。実 HTTP object fixture と frame load pass、build warning0。Acid3 score100/100・inventory100/100・例外0・差12417/480000（183減）、Acid2差1865/120000不変。[証拠](../ws074/phase184/evidence/20261013-q1007/report.json)。p100全画面差0・WS074受入は未達。
