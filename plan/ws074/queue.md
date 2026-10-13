<!-- awesome-plan project=zedbsd record=queue -->

# q1004 / WS074 Acid3 lane

Status: finished（2026-10-13 22:58〜23:02 JST、q1004-i01 cleared）
Owner: この Codex セッション単独。作業 tree `/tmp/zedbsd-ws074`、branch `codex/ws074-acid3`。メインセッションの Queue と source 所有を分離する。
Approval: 2026-10-13 このチャットでユーザーが [p181 の exact scope](phase181/phase.md) を示した質問へ「q1004 の範囲を承認する」と回答。対象は HTML `type` 属性 selector の大文字小文字と明示的 `s`/`i` flag の最小修正、DOM/form 保持、host 試験、warning0 build、固定 Acid3/Acid2 再計測、最大90分。差42921/1865の非増加が条件。
Timebox: 2026-10-13 22:58〜23:02 JST（4分、最大90分以内）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1004-i01 | [ws074-p181](phase181/phase.md) | HTML `type=HIDDEN` と UA selector、明示的 `s`/`i` の最小修正、DOM/form 保持、host 試験、warning0 build、固定 Acid3/Acid2、差42921/1865の非増加。最大90分 | cleared。form line24→0、本文 y373.50→349.50、Acid3差42921→18596、Acid2差1865不変。host fixture pass、CSS rule44/44、build warning0。[証拠](phase181/evidence/20261013-q1004/report.json) | p172/p179/p180 cleared、q1003 差画像/layout/paint を確認 |

Dependency graph: p172/p179/p180 → p181、q1003 の実測出力 → p181、p181 → p100 の最終受け入れ判断。q1000-i01 は uncleared 履歴として保持。

Upcoming Work Outlook: q1004 後の候補は [p182](phase182/phase.md) の非 CSS MIME の HTTP stylesheet が h1 を赤くする問題。得点の slash 色・基線、見えてはいけない上部の文字、Acid2 の残差も候補。全画面差0・score100/100・fail/Uncaught/crash/timeout0、回帰・全文規約を満たすまで p100 を cleared にしない。次 Queue は自動開始しない。

Coordination: q1000、q1001、q1002、q1003、q1004 は [履歴](../history/queue-q1000.md)・[履歴](../history/queue-q1001.md)・[履歴](../history/queue-q1002.md)・[履歴](../history/queue-q1003.md)・[履歴](../history/queue-q1004.md)に保存。共有 `plan/queue.md` へ投影。GitHub Issue/Project publication と push は設定に従い保留。
