<!-- awesome-plan project=zedbsd record=queue -->

# q1002 / WS074 Acid3 lane

Status: finished（2026-10-13 22:39〜22:45 JST、q1002-i01 の部分 scope cleared）
Owner: この Codex セッション単独。作業 tree `/tmp/zedbsd-ws074`、branch `codex/ws074-acid3`。メインセッションの Queue と source 所有を分離する。
Approval: 2026-10-13 このチャットでユーザーが [p178 の exact scope](phase178/next-queue-proposal.md) を示した質問へ「自動で承認します」と回答。対象は得点「100/100」の `right:0`/auto幅/絶対配置の最小原因調査、一般的な欠陥の最小修正、焦点を絞った host 試験、同条件の Acid3/Acid2 再計測、最大90分。将来の未提示 Queue への包括承認とは扱わない。
Timebox: 2026-10-13 22:39〜22:45 JST（6分、最大90分以内）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1002-i01 | [ws074-p178](phase178/phase.md) | [承認範囲](phase178/next-queue-proposal.md)の得点位置ずれ1領域。`libbrowser/layout/` の絶対配置・auto幅・right指定を最小ページで切り分け、原因確定時のみ最小修正。host-position等、warning0 build、同じ固定 Acid3/Acid2 で score100/100・inventory100/100・例外0、差56647/1865の非増加を確認 | cleared（部分 scope）。score 右端x625、ROI差9572→800、全体差56647→43486、Acid2差1865不変。[証拠](phase178/evidence/20261013-q1002/report.json)。p178 whole は uncleared | p172 cleared、p179 cleared、q1001 の差画像と layout/paint を確認 |

Dependency graph: p172 → p178、p179 の参照画像修復 → p178 q1002-i01、p178 局所出力 → p100 の最終受け入れ判断。q1000-i01 は uncleared 履歴として保持。

Upcoming Work Outlook: q1002 後の候補は [p180](phase180/phase.md) の iframe 子の親ページへの誤描画。別に得点の slash 色・基線、Acid2 の残差がある。全画面差0・score100/100・fail/Uncaught/crash/timeout0、回帰・全文規約を満たすまで p100 を cleared にしない。次 Queue は自動開始しない。

Coordination: q1000、q1001、q1002 は [履歴](../history/queue-q1000.md)・[履歴](../history/queue-q1001.md)・[履歴](../history/queue-q1002.md)に保存。共有 `plan/queue.md` へ投影。次 Queue は未選定。GitHub Issue/Project publication と push は設定に従い保留。
