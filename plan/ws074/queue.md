<!-- awesome-plan project=zedbsd record=queue -->

# q1001 / WS074 Acid3 lane

Status: finished（2026-10-13、q1001-i01 cleared）
Owner: この Codex セッション。作業 tree `/tmp/zedbsd-ws074`、branch `codex/ws074-acid3`。メインセッションの P1/P2/P3 Queue と source 所有を分離する。
Approval: 2026-10-13 このチャットのユーザーが「p179の範囲を承認する」と回答。対象は [p179](phase179/phase.md) の参照背景画像が欠ける一般的な原因の調査・最小修正・同条件での Acid3/Acid2 再計測、最大90分。最終目標の[p100](phase100/phase.md)全体をこの Queue に含めない。
Timebox: 2026-10-13 22:30〜22:35 JST（5分、最大90分以内）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1001-i01 | [ws074-p179](phase179/phase.md) | 背景画像の CSS 値、URL 解決、HTTP 取得、PNG decode、layout/paint を最小ページで切り分ける。確認した一般的な欠陥だけを WS074 source に最小修正し、同じ固定 Acid3/Acid2 と関連 host 試験で確認する | cleared。参照画像を描画、Acid3差56647、Acid2差1865不変。[証拠](phase179/evidence/20261013-q1001/report.json) | p172 cleared、p178 q1000 の baseline と画像を確認 |

Dependency graph: p172 → p179、p178 q1000 baseline → p179、p179 の verified 出力 → p178 再試行。p100 の whole clearance は q1001 の局所成果だけでは成立しない。

Upcoming Work Outlook: p179 の verified 出力の後に p178 の局所差を再選定する。全画面で差画素0、得点100/100、fail/Uncaught/crash/timeout 0、および必要な回帰・全文規約を満たすまで p100 を cleared にしない。次の Queue はこの Queue の終了時に自動開始しない。

Coordination: q1000 は[履歴](../history/queue-q1000.md)、q1001 は[履歴](../history/queue-q1001.md)へ保存。q1001 は既存の q924〜q939 と衝突しない番号として 2026-10-13 に選んだ。共有 `plan/queue.md` へ投影。独立 branch の main 統合は別メインセッションとの書込重複を確認してから行う。GitHub Issue/Project publication は設定に従い保留。次 Queue は未選定。
