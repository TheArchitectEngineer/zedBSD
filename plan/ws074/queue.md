<!-- awesome-plan project=zedbsd record=queue -->

# q1003 / WS074 Acid3 lane

Status: finished（2026-10-13 22:49〜22:54 JST、q1003-i01 cleared）
Owner: この Codex セッション単独。作業 tree `/tmp/zedbsd-ws074`、branch `codex/ws074-acid3`。メインセッションの Queue と source 所有を分離する。
Approval: 2026-10-13 このチャットでユーザーが [p180 の exact scope](phase180/phase.md) を示した質問へ「q1003 の範囲を承認する」と回答。対象は iframe 子の誤描画の最小原因調査、DOM/frame 動作を保つ一般的な最小修正、焦点を絞った host 試験、同条件の Acid3/Acid2 再計測、最大90分。差43486/1865の非増加が条件。
Timebox: 2026-10-13 22:49〜22:54 JST（5分、最大90分以内）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1003-i01 | [ws074-p180](phase180/phase.md) | iframe 内 `FAIL` が親に描かれる原因。0×0/通常寸法で DOM 子と frame load を保った最小修正、host 試験、warning0 build、固定 Acid3/Acid2、差43486/1865の非増加。最大90分 | cleared。iframe `FAIL` 2 item 消失。Acid3差43486→42921、Acid2差1865不変。host fixture pass、frame load 52/52、build warning0。[証拠](phase180/evidence/20261013-q1003/report.json) | p172/p179 cleared、q1002 差画像/layout/paint を確認 |

Dependency graph: p172/p179 → p180、p178 q1002 の実測出力 → p180、p180 → p100 の最終受け入れ判断。q1000-i01 は uncleared 履歴として保持。

Upcoming Work Outlook: q1003 後の候補は [p181](phase181/phase.md) の HTML `type=HIDDEN`/UA selector と form line 高さの調査。得点の slash 色・基線、Acid2 の残差も候補。全画面差0・score100/100・fail/Uncaught/crash/timeout0、回帰・全文規約を満たすまで p100 を cleared にしない。次 Queue は自動開始しない。

Coordination: q1000、q1001、q1002、q1003 は [履歴](../history/queue-q1000.md)・[履歴](../history/queue-q1001.md)・[履歴](../history/queue-q1002.md)・[履歴](../history/queue-q1003.md)に保存。共有 `plan/queue.md` へ投影。GitHub Issue/Project publication と push は設定に従い保留。
